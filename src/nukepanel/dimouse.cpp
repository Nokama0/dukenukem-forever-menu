// The engine's DirectInput mouse, released while the panel is open (the rule and why: dimouse.h). Touches only
// Engine.dll's ole32 CoCreateInstance import slot (data) and dinput8.dll's device method bodies: Engine.dll's code
// is CEG-checked (docs/re/ceg.md).
#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <stdio.h>
#include <string.h>
#include <intrin.h>
#include "dimouse.h"
#include "stallrule.h"   // stall_fmt_addr
#include "clicklock.h"
#include "iat.h"
#include "hook.h"
#include "log.h"
#include "frame.h"

// IDirectInputDevice8 vtable slots; Engine.dll's calls match them (docs/re/input.md: Acquire +0x1C, Unacquire
// +0x20, GetDeviceState +0x24, SetCooperativeLevel +0x34, Initialize +0x44).
enum { SLOT_RELEASE = 2, SLOT_ACQUIRE = 7, SLOT_UNACQUIRE = 8, SLOT_GETDEVICESTATE = 9, SLOT_SETCOOP = 13,
       SLOT_GETDEVICEINFO = 15, SLOT_COUNT = 18 };

// Defined here rather than taken from dxguid.lib.
static const GUID kClsidDiDevice8 = { 0x25E609E5, 0xB259, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
static const GUID kIidDiDevice8A = { 0x54D41080, 0xDC15, 0x4833, { 0xA4, 0x1B, 0x74, 0x8F, 0x73, 0xA3, 0x81, 0x79 } };
static const GUID kIidDiDevice8W = { 0x54D41081, 0xDC15, 0x4833, { 0xA4, 0x1B, 0x74, 0x8F, 0x73, 0xA3, 0x81, 0x79 } };

typedef HRESULT (WINAPI* CoCreateInstanceFn)(const GUID& clsid, IUnknown* outer, DWORD ctx, const GUID& iid,
                                             void** out);
typedef HRESULT (STDMETHODCALLTYPE* DevFn)(void* self);
typedef ULONG (STDMETHODCALLTYPE* ReleaseFn)(void* self);
typedef HRESULT (STDMETHODCALLTYPE* GetDeviceStateFn)(void* self, DWORD size, void* buf);
typedef HRESULT (STDMETHODCALLTYPE* GetDeviceInfoFn)(void* self, void* info);
typedef HRESULT (STDMETHODCALLTYPE* SetCoopFn)(void* self, HWND wnd, DWORD flags);

static CoCreateInstanceFn s_realCoCreate;   // what the slot held: the export, or a shim's hook
// Trampolines, written by hook_install_at before its patch goes live.
static DevFn s_realAcquire, s_realUnacquire;
static ReleaseFn s_realRelease;
static GetDeviceStateFn s_realGetState;
static void* s_bodies[4];        // Acquire, Unacquire, GetDeviceState, Release from the first device's vtable
static int s_hooked;             // 0 not tried, 1 installed, -1 refused (never retried)
static GetDeviceInfoFn s_getInfo;   // the tracked device's
static int s_wide;               // the tracked device was created as IDirectInputDevice8W
static DmState s_dm = dm_init();
static unsigned s_stateSize = 0x14;   // the engine's last GetDeviceState size on the tracked device
static unsigned s_held, s_zeroed, s_heldTotal, s_zeroedTotal;
// The mouse's counts since the last dimouse_counts_take. Interlocked: the DI caller is not checked against the
// frame thread, so the detour and the take may run on two threads.
static volatile LONG s_rawDx, s_rawDy, s_rawDz;
static volatile LONG s_flight;   // dimouse_set_flight; read by the GetDeviceState detour
static unsigned s_hidden, s_hiddenTotal;   // flight reads handed to the engine as zeros
static volatile LONG s_buttons;   // dm_buttons of the engine's latest read (the Aimbot button)
static volatile LONG s_button0;   // dm_button0 of the engine's latest read (the Triggerbot's "user is firing")
static volatile LONG s_cociLogged;
static unsigned s_devicesLogged;
static int s_infoFailLogged;
static DWORD s_tid;
static int s_tidLogged;
// The shared engine mouse (clicklock.h cl_coop_level): SetCooperativeLevel's body, detoured once and on its own,
// so a refusal leaves the release above working. The last device and window logged, one line per pair.
static SetCoopFn s_realCoop;
static int s_coopTried, s_coopHooked;
static void* s_coopDev;
static HWND s_coopWnd;
static unsigned s_coopLines, s_coopShared;
static int s_mouseSeen;   // a device was tracked: the raw rows are watched from then on
static unsigned s_rawChanges;
static char s_rawLast[192];

static const char* kind_name(int k) {
  return k == DM_KIND_MOUSE ? "mouse" : k == DM_KIND_OTHER ? "not a mouse" : "unclassified";
}

static void guid_text(char* out, size_t cap, const GUID& g) {
  sprintf_s(out, cap, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", g.Data1, g.Data2, g.Data3,
            g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
}

// The rule's state is the game thread's; a call on another thread would make it a two-thread exchange.
static void note_thread(void) {
  const DWORD t = GetCurrentThreadId();
  if (!s_tid) s_tid = t;
  else if (!s_tidLogged && t != s_tid) {
    s_tidLogged = 1;
    np_log("dimouse: THREAD MISMATCH: device call on tid=%lu, first seen on tid=%lu", t, (unsigned long)s_tid);
  }
}

// GetDeviceInfo answers from Initialize on, so the engine's first Acquire is classified too; GetDeviceState's size
// is the fallback. Without either the device stays unclassified (passed through) until the next call.
static void classify(void* self, int haveSize, unsigned size) {
  DIDEVICEINSTANCEW info;   // the larger of the A and W layouts; dwDevType is at +36 in both
  memset(&info, 0, sizeof info);
  info.dwSize = s_wide ? sizeof(DIDEVICEINSTANCEW) : sizeof(DIDEVICEINSTANCEA);
  const HRESULT hr = s_getInfo ? s_getInfo(self, &info) : E_POINTER;
  int kind;
  char how[48];
  if (hr == DI_OK) {
    kind = dm_kind_by_devtype(info.dwDevType);
    sprintf_s(how, sizeof how, "devtype 0x%lx", (unsigned long)info.dwDevType);
  } else {
    if (!s_infoFailLogged) {
      s_infoFailLogged = 1;
      np_log("dimouse: GetDeviceInfo(%p) hr=0x%08lX; %s", self, (unsigned long)hr,
             haveSize ? "classified by GetDeviceState's size" : "retried at the next call");
    }
    if (!haveSize) return;
    kind = dm_kind_by_size(size);
    sprintf_s(how, sizeof how, "size 0x%x", size);
  }
  const int call = dm_set_kind(&s_dm, self, kind);
  if (kind == DM_KIND_MOUSE) np_log("dimouse: device %p is the mouse (%s)", self, how);
  else np_log("dimouse: device %p is not a mouse (%s); passed through", self, how);
  if (call == DM_CALL_UNACQUIRE) {
    const HRESULT u = s_realUnacquire(self);
    np_log("dimouse: classified while open: Unacquire(%p) called hr=0x%08lX", self, (unsigned long)u);
  }
}

static void before_engine_call(void* self, int haveSize, unsigned size) {
  if (!self || self != s_dm.device) return;
  note_thread();
  if (dm_needs_kind(&s_dm, self)) classify(self, haveSize, size);
}

// The process's raw-input mouse and keyboard rows: 0x230 NOLEGACY|CAPTUREMOUSE is the exclusive mouse's.
static void raw_rows(char* out, size_t cap) {
  RAWINPUTDEVICE d[16];
  UINT n = 16;
  const UINT got = GetRegisteredRawInputDevices(d, &n, sizeof d[0]);
  const unsigned long err = got == (UINT)-1 ? GetLastError() : 0;
  char m[96], k[96];
  cl_raw_row(d, got == (UINT)-1 ? -1 : (int)got, err, CL_RAW_MOUSE, m, sizeof m);
  cl_raw_row(d, got == (UINT)-1 ? -1 : (int)got, err, CL_RAW_KEYBOARD, k, sizeof k);
  _snprintf_s(out, cap, _TRUNCATE, "mouse %s; keyboard %s", m, k);
}

// The engine's EXCLUSIVE|FOREGROUND (diSetupKeyboardMouse Engine.dll!0x102B8B80, right after Initialize, before
// any Acquire) goes to dinput8 as NONEXCLUSIVE|FOREGROUND. A refused shared level is retried with the engine's.
static HRESULT STDMETHODCALLTYPE coop_detour(void* self, HWND wnd, DWORD flags) {
  before_engine_call(self, 0, 0);
  const bool ours = self && self == s_dm.device;
  const DWORD want = cl_coop_level(ours && s_dm.kind == DM_KIND_MOUSE, flags);
  char before[192] = "", after[192] = "";
  const bool logIt = ours && (self != s_coopDev || wnd != s_coopWnd) && s_coopLines < 32;
  if (logIt) raw_rows(before, sizeof before);
  HRESULT hr = s_realCoop(self, wnd, want);
  HRESULT hrShared = hr;
  const bool refused = want != flags && FAILED(hr);
  if (refused) hr = s_realCoop(self, wnd, flags);
  if (ours && want != flags && !refused) s_coopShared++;
  if (logIt) {
    s_coopDev = self;
    s_coopWnd = wnd;
    s_coopLines++;
    raw_rows(after, sizeof after);
    if (refused)
      np_log("dimouse: mouse %p window %p: level 0x%02lx -> 0x%02lx REFUSED hr=0x%08lX; the engine's level kept "
             "hr=0x%08lX; raw before: %s; after: %s", self, (void*)wnd, (unsigned long)flags, (unsigned long)want,
             (unsigned long)hrShared, (unsigned long)hr, before, after);
    else
      np_log("dimouse: mouse %p window %p: level 0x%02lx -> 0x%02lx %s hr=0x%08lX (%s); raw before: %s; after: %s",
             self, (void*)wnd, (unsigned long)flags, (unsigned long)want, want != flags ? "set" : "left",
             (unsigned long)hr, kind_name(s_dm.kind), before, after);
  }
  return hr;
}

// Each change of the engine's intent on its mouse, with the caller (SetMouseCapture's Acquire, AcquireDIMouse's
// Unacquire): the first 64, then every 64th. A re-acquire with the intent unchanged is not a transition.
static unsigned s_intentChanges;
static void note_intent(void* self, int before, void* from) {
  if (!self || self != s_dm.device || s_dm.kind != DM_KIND_MOUSE || s_dm.wants == before) return;
  if (++s_intentChanges > 64 && (s_intentChanges & 63u)) return;
  HMODULE h = 0;
  char path[MAX_PATH], where[96];
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCSTR)from, &h) && GetModuleFileNameA(h, path, MAX_PATH))
    stall_fmt_addr(where, sizeof where, path, (unsigned)(UINT_PTR)from, (unsigned)(UINT_PTR)h);
  else
    stall_fmt_addr(where, sizeof where, 0, (unsigned)(UINT_PTR)from, 0);
  np_log("dimouse: engine %s from %s: wants its mouse %d -> %d (panel %s, change %u)",
         s_dm.wants ? "Acquire" : "Unacquire", where, before, s_dm.wants, s_dm.open ? "open" : "closed",
         s_intentChanges);
}

static HRESULT STDMETHODCALLTYPE acquire_detour(void* self) {
  before_engine_call(self, 0, 0);
  const int before = s_dm.wants;
  const int real = dm_on_engine_acquire(&s_dm, self, 1);
  note_intent(self, before, _ReturnAddress());
  if (!real) { s_held++; s_heldTotal++; return DI_OK; }
  return s_realAcquire(self);
}

static HRESULT STDMETHODCALLTYPE unacquire_detour(void* self) {
  before_engine_call(self, 0, 0);
  const int before = s_dm.wants;
  const int real = dm_on_engine_acquire(&s_dm, self, 0);
  note_intent(self, before, _ReturnAddress());
  if (!real) { s_held++; s_heldTotal++; return DI_OK; }
  return s_realUnacquire(self);
}

// While open the engine reads zeros with DI_OK: no turn, and UpdateInput neither re-acquires nor re-creates.
static HRESULT STDMETHODCALLTYPE getstate_detour(void* self, DWORD size, void* buf) {
  before_engine_call(self, 1, size);
  if (self && self == s_dm.device && size > 0 && size <= 0x400) s_stateSize = size;
  if (!dm_on_getstate(&s_dm, self)) {
    if (self == s_dm.device) {
      InterlockedExchange(&s_button0, 0);
      InterlockedExchange(&s_buttons, 0);
    }
    s_zeroed++;
    s_zeroedTotal++;
    return (HRESULT)dm_zero_state(buf, size);
  }
  const HRESULT hr = s_realGetState(self, size, buf);
  // Raw mouse input: the counts the engine itself read (DIMOUSESTATE lX +0, lY +4), after the real call filled them.
  if (hr == DI_OK && buf && size >= 8 && self == s_dm.device && s_dm.kind == DM_KIND_MOUSE) {
    InterlockedExchangeAdd(&s_rawDx, ((const LONG*)buf)[0]);
    InterlockedExchangeAdd(&s_rawDy, ((const LONG*)buf)[1]);
    if (size >= 12) InterlockedExchangeAdd(&s_rawDz, ((const LONG*)buf)[2]);   // lZ +8: the wheel
    InterlockedExchange(&s_button0, dm_button0(buf, size));
    InterlockedExchange(&s_buttons, (LONG)dm_buttons(buf, size));
  } else if (hr != DI_OK && self == s_dm.device) {   // a lost device holds no button down
    InterlockedExchange(&s_button0, 0);
    InterlockedExchange(&s_buttons, 0);
  }
  if (hr == DI_OK && buf && size <= 0x400 && dm_getstate_hides(&s_dm, self, s_flight != 0)) {
    memset(buf, 0, size);   // after the counts were taken: the free camera's, never Duke's turn
    s_hidden++;
    s_hiddenTotal++;
  }
  return hr;
}

// The body is shared with dinput8's other objects (the keyboard, IDirectInput8): only the tracked device counts. It
// also reads the rule's plain statics on the assumption of the game's main thread (docs/re/engine-map.md section 5);
// another thread is logged once, behaviour unchanged.
static volatile LONG s_releaseTidLogged;
static ULONG STDMETHODCALLTYPE release_detour(void* self) {
  const unsigned long ft = frame_thread_id(), t = GetCurrentThreadId();
  if (ft && t != ft && !InterlockedExchange(&s_releaseTidLogged, 1))
    np_log("dimouse: THREAD MISMATCH: IDirectInputDevice8::Release on tid=%lu, frame thread %lu", t, ft);
  const ULONG refs = s_realRelease(self);
  if (dm_on_released(&s_dm, self, refs)) np_log("dimouse: device %p released", self);
  return refs;
}

static bool in_image(HMODULE mod, const void* p) {
  const unsigned char* base = (const unsigned char*)mod;
  if (!base || !hook_readable(base, sizeof(IMAGE_DOS_HEADER))) return false;
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
  if (!hook_readable(nt, sizeof *nt) || nt->Signature != IMAGE_NT_SIGNATURE) return false;
  const unsigned char* q = (const unsigned char*)p;
  return q >= base && q < base + nt->OptionalHeader.SizeOfImage;
}

// Once, from the first device's vtable: dinput8.dll's code bodies, never the vtable (shared by every device).
static void install_detours(void* const bodies[4]) {
  s_hooked = -1;
  const HMODULE di = GetModuleHandleW(L"dinput8.dll");
  static const char* const kNames[4] = { "IDirectInputDevice8::Acquire", "IDirectInputDevice8::Unacquire",
                                         "IDirectInputDevice8::GetDeviceState", "IDirectInputDevice8::Release" };
  for (int i = 0; i < 4; i++) {
    if (!in_image(di, bodies[i])) {
      np_log("dimouse: %s body %p is not in dinput8.dll (%p); nothing hooked, the panel gets no mouse", kNames[i],
             bodies[i], (void*)di);
      return;
    }
  }
  for (int i = 0; i < 4; i++)
    for (int j = i + 1; j < 4; j++)
      if (bodies[i] == bodies[j]) {
        np_log("dimouse: Acquire/Unacquire/GetDeviceState/Release share a body (%p/%p/%p/%p); nothing hooked",
               bodies[0], bodies[1], bodies[2], bodies[3]);
        return;
      }
  // A partial install is harmless: no device is tracked unless all four are in, so the live ones pass through.
  if (!hook_install_live(bodies[0], kNames[0], (void*)&acquire_detour, (void**)&s_realAcquire) ||
      !hook_install_live(bodies[1], kNames[1], (void*)&unacquire_detour, (void**)&s_realUnacquire) ||
      !hook_install_live(bodies[2], kNames[2], (void*)&getstate_detour, (void**)&s_realGetState) ||
      !hook_install_live(bodies[3], kNames[3], (void*)&release_detour, (void**)&s_realRelease)) {
    np_log("dimouse: a device method was not hooked; the panel gets no mouse");
    return;
  }
  memcpy(s_bodies, bodies, sizeof s_bodies);
  s_hooked = 1;
  np_log("dimouse: hooked Acquire/Unacquire/GetDeviceState at %p/%p/%p, Release at %p", bodies[0], bodies[1],
         bodies[2], bodies[3]);
}

// Separate from the four above: without it the engine mouse stays exclusive and the panel still works.
static void install_coop(void* body) {
  s_coopTried = 1;
  const HMODULE di = GetModuleHandleW(L"dinput8.dll");
  if (!in_image(di, body)) {
    np_log("dimouse: SetCooperativeLevel body %p is not in dinput8.dll (%p); the engine mouse stays exclusive", body,
           (void*)di);
    return;
  }
  for (int i = 0; i < 4; i++)
    if (s_bodies[i] == body) {
      np_log("dimouse: SetCooperativeLevel shares a body with slot %d (%p); the engine mouse stays exclusive", i, body);
      return;
    }
  if (!hook_install_live(body, "IDirectInputDevice8::SetCooperativeLevel", (void*)&coop_detour,
                         (void**)&s_realCoop)) {
    np_log("dimouse: SetCooperativeLevel not hooked; the engine mouse stays exclusive");
    return;
  }
  s_coopHooked = 1;
  np_log("dimouse: hooked SetCooperativeLevel at %p: the engine mouse is shared (EXCLUSIVE -> NONEXCLUSIVE)", body);
}

static void on_device(void* dev, int wide) {
  void** vt = hook_readable(dev, sizeof(void*)) ? *(void***)dev : 0;
  if (!vt || !hook_readable(vt, SLOT_COUNT * sizeof(void*))) {
    np_log("dimouse: device %p has no readable vtable; not tracked", dev);
    dm_on_created(&s_dm, 0);
    return;
  }
  void* const bodies[4] = { vt[SLOT_ACQUIRE], vt[SLOT_UNACQUIRE], vt[SLOT_GETDEVICESTATE], vt[SLOT_RELEASE] };
  if (!s_hooked) install_detours(bodies);
  if (s_hooked != 1 || memcmp(bodies, s_bodies, sizeof s_bodies) != 0) {
    if (s_hooked == 1)
      np_log("dimouse: device %p has other Acquire/Unacquire/GetDeviceState/Release bodies (%p/%p/%p/%p); "
             "not tracked", dev, bodies[0], bodies[1], bodies[2], bodies[3]);
    dm_on_created(&s_dm, 0);   // the old device is forgotten either way
    return;
  }
  if (!s_coopTried) install_coop(vt[SLOT_SETCOOP]);
  s_mouseSeen = 1;
  s_getInfo = (GetDeviceInfoFn)vt[SLOT_GETDEVICEINFO];
  s_wide = wide;
  note_thread();
  dm_on_created(&s_dm, dev);
  if (s_devicesLogged < 16) {
    s_devicesLogged++;
    np_log("dimouse: device %p created (clsid ok) %s tid=%lu panel %s", dev, wide ? "W" : "A",
           GetCurrentThreadId(), s_dm.open ? "open" : "closed");
  }
}

static HRESULT WINAPI cocreate_hook(const GUID& clsid, IUnknown* outer, DWORD ctx, const GUID& iid, void** out) {
  const HRESULT hr = s_realCoCreate(clsid, outer, ctx, iid, out);
  // The out-parameter is read only now that the call that fills it has returned.
  void* obj = (SUCCEEDED(hr) && out) ? *out : 0;
  const bool di = IsEqualGUID(clsid, kClsidDiDevice8) != 0;
  if (InterlockedIncrement(&s_cociLogged) <= 8) {
    char c[40], i[40];
    guid_text(c, sizeof c, clsid);
    guid_text(i, sizeof i, iid);
    np_log("dimouse: CoCreateInstance clsid %s iid %s hr=0x%08lX -> %p tid=%lu%s", c, i, (unsigned long)hr, obj,
           GetCurrentThreadId(), di ? " (DirectInputDevice8)" : "");
  }
  if (!di || !obj) return hr;
  const bool wide = IsEqualGUID(iid, kIidDiDevice8W) != 0;
  // Aggregation returns an inner IUnknown and another IID another vtable: slots 7/8/9 would name other code.
  if (outer || (!wide && !IsEqualGUID(iid, kIidDiDevice8A))) {
    np_log("dimouse: DirectInputDevice8 %p not tracked (outer %p, iid not IDirectInputDevice8A/W)", obj, (void*)outer);
    return hr;
  }
  on_device(obj, wide ? 1 : 0);
  return hr;
}

void dimouse_install(void) {
  const HMODULE eng = GetModuleHandleW(L"Engine.dll");
  if (!eng) {
    np_log("dimouse: Engine.dll not loaded at boot; CoCreateInstance not patched, the panel gets no mouse");
    return;
  }
  if (GetModuleHandleW(L"dinput8.dll")) np_log("dimouse: dinput8.dll already loaded at boot: a device may predate us");
  if (!iat_patch(eng, "ole32.dll", "CoCreateInstance", (void*)&cocreate_hook, (void**)&s_realCoCreate, "dimouse")) {
    np_log("dimouse: CoCreateInstance slot not patched; the panel gets no mouse");
    return;
  }
  np_log("dimouse: CoCreateInstance slot patched");
}

// The recorded intent can be stale (the engine drops its flag without an Unacquire when dinput already released the
// device), so the device is asked: a real read at the engine's own size.
void dimouse_open(void) {
  void* const dev = s_dm.device;
  int seen = DM_SEEN_UNKNOWN;
  HRESULT probe = S_FALSE;
  if (dev && s_dm.kind == DM_KIND_MOUSE) {
    static unsigned char scratch[0x400];
    probe = s_realGetState(dev, s_stateSize, scratch);
    seen = dm_seen_from_hr((long)probe);
  }
  const int recorded = s_dm.wants;
  if (dm_on_open(&s_dm, seen) == DM_CALL_UNACQUIRE) {
    const HRESULT hr = s_realUnacquire(dev);
    np_log("dimouse: open: GetDeviceState hr=0x%08lX (recorded wants %d); Unacquire(%p) called hr=0x%08lX",
           (unsigned long)probe, recorded, dev, (unsigned long)hr);
  } else {
    np_log("dimouse: open: nothing called (device %p %s, GetDeviceState hr=0x%08lX, recorded wants %d, now %d, "
           "hooked %d)", dev, kind_name(s_dm.kind), (unsigned long)probe, recorded, s_dm.wants, s_hooked);
  }
}

void dimouse_close(void) {
  void* const dev = s_dm.device;
  if (dm_on_close(&s_dm) == DM_CALL_ACQUIRE) {
    const HRESULT hr = s_realAcquire(dev);
    np_log("dimouse: close: Acquire(%p) called hr=0x%08lX; held total=%u zeroed total=%u", dev, (unsigned long)hr,
           s_heldTotal, s_zeroedTotal);
  } else {
    np_log("dimouse: close: nothing called (device %p %s, engine wants %d); held total=%u zeroed total=%u", dev,
           kind_name(s_dm.kind), s_dm.wants, s_heldTotal, s_zeroedTotal);
  }
}

void dimouse_set_flight(int on) {
  InterlockedExchange(&s_flight, on ? 1 : 0);
  np_log("dimouse: flight %s (device %p %s); %u read(s) hidden this flight, %u in all", on ? "on" : "off",
         s_dm.device, kind_name(s_dm.kind), s_hidden, s_hiddenTotal);
  s_hidden = 0;
}

void dimouse_forget(void) {
  if (s_dm.device) np_log("dimouse: device %p forgotten: its window is being destroyed", s_dm.device);
  dm_forget(&s_dm);
}

void dimouse_counts_take(int* dx, int* dy) {
  *dx = (int)InterlockedExchange(&s_rawDx, 0);
  *dy = (int)InterlockedExchange(&s_rawDy, 0);
}

int dimouse_wheel_take(void) { return (int)InterlockedExchange(&s_rawDz, 0); }

void dimouse_take(unsigned* acquireHeld, unsigned* zeroed) {
  *acquireHeld = s_held;
  *zeroed = s_zeroed;
  s_held = s_zeroed = 0;
}

int dimouse_button(int i) {
  if (i < 0 || i > 7 || !dimouse_tracking_mouse()) return 0;
  return ((unsigned)InterlockedCompareExchange(&s_buttons, 0, 0) >> i) & 1u;
}

int dimouse_button0(void) { return dimouse_tracking_mouse() && InterlockedCompareExchange(&s_button0, 0, 0) != 0; }

int dimouse_tracking_mouse(void) { return s_dm.device && s_dm.kind == DM_KIND_MOUSE ? 1 : 0; }

int dimouse_engine_wants(void) { return s_dm.device && s_dm.kind == DM_KIND_MOUSE && s_dm.wants ? 1 : 0; }

// Polled once a frame, logged on each change with a cumulative count: dinput8's worker registers and removes the
// rows asynchronously, so a line at an acquire change alone can read them a moment early.
void dimouse_frame(void) {
  if (!s_mouseSeen) return;
  char now[192];
  raw_rows(now, sizeof now);
  if (strcmp(now, s_rawLast) == 0) return;
  strcpy_s(s_rawLast, sizeof s_rawLast, now);
  if (++s_rawChanges > 64) return;   // counted on; the first 64 changes settle the cause
  np_log("dimouse: raw rows change %u: %s (shared level set %u time(s), hooked %d)", s_rawChanges, now,
         s_coopShared, s_coopHooked);
}

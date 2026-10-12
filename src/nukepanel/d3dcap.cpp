// D4 device capture. Each body is detoured once; the vtables are only read, never written.
#include <windows.h>
#include <d3d9.h>
#include <wchar.h>
#include "d3dcap.h"
#include "frame.h"
#include "hook.h"
#include "log.h"
#include "d3dbody.h"
#include "display.h"
#include "freecam.h"   // freecam_clear_flags
#include "d3dtrace.h"
#include "rtclear.h"
#include "rstall.h"
#include "stallrule.h"
#include "stall.h"
#include "d3drec.h"
#include <intrin.h>

// Frame pointers in this file's functions, so the frame record's RtlCaptureStackBackTrace can walk from a detour into
// its D3DDrv9 and Engine callers (freecam.md section 7).
#pragma optimize("y", off)

typedef IDirect3D9* (WINAPI* Create9Fn)(UINT);
typedef HRESULT (STDMETHODCALLTYPE* CreateDeviceFn)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                                    D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
typedef HRESULT (STDMETHODCALLTYPE* ResetFn)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
typedef HRESULT (STDMETHODCALLTYPE* PresentFn)(IDirect3DDevice9*, const RECT*, const RECT*, HWND,
                                               const RGNDATA*);
typedef HRESULT (STDMETHODCALLTYPE* ClearFn)(IDirect3DDevice9*, DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD);
typedef HRESULT (STDMETHODCALLTYPE* SetRtFn)(IDirect3DDevice9*, DWORD, IDirect3DSurface9*);
typedef HRESULT (STDMETHODCALLTYPE* StretchFn)(IDirect3DDevice9*, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*,
                                               const RECT*, D3DTEXTUREFILTERTYPE);
typedef HRESULT (STDMETHODCALLTYPE* FillFn)(IDirect3DDevice9*, IDirect3DSurface9*, const RECT*, D3DCOLOR);
typedef HRESULT (STDMETHODCALLTYPE* SetTexFn)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*);
typedef HRESULT (STDMETHODCALLTYPE* SetVsFn)(IDirect3DDevice9*, IDirect3DVertexShader9*);
typedef HRESULT (STDMETHODCALLTYPE* SetPsFn)(IDirect3DDevice9*, IDirect3DPixelShader9*);

// COM vtable slots (d3d9.h declaration order).
enum { SLOT_D3D_CREATEDEVICE = 16, SLOT_DEV_RESET = 16, SLOT_DEV_PRESENT = 17, SLOT_DEV_STRETCHRECT = 34,
       SLOT_DEV_COLORFILL = 35, SLOT_DEV_SETRENDERTARGET = 37, SLOT_DEV_CLEAR = 43, SLOT_DEV_SETTEXTURE = 65,
       SLOT_DEV_SETVERTEXSHADER = 92, SLOT_DEV_SETPIXELSHADER = 107 };

// Trampolines, written by hook_install_at itself before its patch goes live: a thread entering a
// detour at once must find its trampoline already set.
static Create9Fn s_realCreate9;
static CreateDeviceFn s_realCreateDevice;
static ResetFn s_realReset;
static PresentFn s_realPresent;
static ClearFn s_realClear;
// The free camera's frame clear: interlocked scalars only (the Clear may come from a render thread).
static volatile LONG s_flightClear, s_clearsSincePresent, s_clearHooked;
static volatile LONG s_clearFirsts, s_clearAdded, s_clearEngine, s_clearLastFlags;

// Bodies hooked so far (each once, by whichever thread gets there first).
static volatile LONG s_cdClaimed, s_devClaimed;
static void* volatile s_cdBody;
static void* volatile s_resetBody;
static void* volatile s_presentBody;

// Written by the CreateDevice thread, read by the presenting thread: aligned pointer-sized scalars.
static IDirect3DDevice9* volatile s_dev;
static HWND volatile s_window;
static HMODULE s_d3d9;                // set before Direct3DCreate9 is hooked, read-only after
static volatile LONG s_createTid;
static volatile LONG s_immediate;   // 1 when PresentationInterval was IMMEDIATE
static volatile LONG s_windowed;    // the params the device runs with: Windowed
// The frame trace (d3dtrace.h): state and count interlocked; the entries are written by the recording thread and
// read by the game thread only once the state says READY.
static SetRtFn s_realSetRt;
static StretchFn s_realStretch;
static FillFn s_realFill;
static volatile LONG s_trState, s_trN, s_trHooked;
static DtrEntry s_tr[DTR_CAP];
static const void* volatile s_trCurRt;   // render target 0 as the trace saw it last
static const void* s_trBb;
static const void* s_trRt0;
static D3DSURFACE_DESC s_trBbDesc;

static int tr_on(IDirect3DDevice9* dev) { return dev && dev == s_dev && s_trState == DTR_RECORDING; }

// Smear fix (rtclear.h): two threads touch the table (s_rtcReady, s_rtcCur). With r_ThreadRenderer 1 (engine-map.md 5)
// the SetRenderTarget, SetTexture and Clear detours run on the render job, rtc_present and reset_detour at Present.
// Safe: WaitTillNotRendering and StartRenderingThread are full barriers, so the job never runs during Present.
static SetTexFn s_realSetTex;
static RtcState s_rtc;
static const void* s_rtcCur;   // render target 0, as our SetRenderTarget detour saw it
static int s_rtcReady;
static volatile LONG s_rtcHooked;
static volatile LONG s_rtcFrames, s_rtcFills, s_rtcFillFails, s_rtcHistory, s_rtcEngine, s_rtcLearned, s_rtcFillUs;
static volatile LONG s_rtcFull, s_rtcKept, s_rtcFormat;
static const void* volatile s_bbNow;   // the back buffer as of the last Present
static volatile LONG s_bbBinds;        // the engine's SetRenderTarget(0, s_bbNow) calls (rstall.h rst_engine_bb_bind)
static LONG s_bbSeenWatch;             // game thread: s_bbBinds as the watch last saw it
// The latch fix (rstall.h rst_heal_rt) and the net's input: Lock's clears, those off the back buffer, heals done.
static volatile LONG s_lockFrames, s_lockMisses, s_heals, s_healFails;
static unsigned s_healLines;
static const void* volatile s_rtcKeptSurf;   // the feedback target's surface, from the game thread (d3dcap_rtclear_keep)
// IDirect3DTexture9, by value (no dxguid.lib).
static const GUID kIidTexture9 = { 0x85c31227, 0x3de5, 0x4f00, { 0x9b, 0x3a, 0xf1, 0x1a, 0xc3, 0x8c, 0x18, 0xb5 } };

// The render watch (rstall.h, freecam.md section 7). s_ownFrame is set while our own Present-time drawing runs, so its
// back-buffer binds never count as the engine's. The engine's calls name the render job's thread and, while a run of
// undrawn frames is on (s_watchHot), leave the stack of the last one: where the frame's work ended.
static volatile LONG s_ownFrame, s_watchHot, s_presentTid;
static volatile LONG s_jobTid, s_jobStackBase, s_callsJob, s_callsGame;
enum { LAST_FRAMES = 16 };
static unsigned s_lastRets[LAST_FRAMES];
static volatile LONG s_lastN, s_lastKind, s_lastTid;

// The frame record (d3drec.h): armed by the watch's stall and by a flight's first frame, it keeps one whole frame of the
// engine's calls on both threads with their callers; the game thread logs it a few lines a Present. The writers claim
// slots by an interlocked count; the game thread reads them only once the state says READY.
static SetVsFn s_realSetVs;
static SetPsFn s_realSetPs;
static volatile LONG s_recState, s_recN;
static DrecEntry s_rec[DREC_CAP];
static const void* s_recBb;
static const void* s_recRt0;
static const void* volatile s_recLastVs;
static const void* volatile s_recLastPs;
static int s_recCounts[DREC_WHY_COUNT], s_recWhy, s_recLogged;
static unsigned s_recNo;
static long s_recTotal;
static UINT_PTR s_selfBase, s_selfSize;   // our own module, left out of the callers

static int rec_on(IDirect3DDevice9* dev) { return dev && dev == s_dev && s_recState == DTR_RECORDING; }

static __declspec(noinline) DrecEntry* rec_add(int kind, void* ret) {
  const int slot = drec_slot(InterlockedIncrement(&s_recN) - 1);
  if (slot < 0) return 0;
  DrecEntry* e = &s_rec[slot];
  memset(e, 0, sizeof *e);
  e->kind = kind;
  e->game = (LONG)GetCurrentThreadId() == InterlockedCompareExchange(&s_presentTid, 0, 0);
  e->ret = (unsigned)(UINT_PTR)ret;
  void* bt[10];
  const USHORT got = RtlCaptureStackBackTrace(0, 10, bt, NULL);
  for (USHORT i = 0; i < got && e->btN < DREC_BT; ++i) {
    const UINT_PTR a = (UINT_PTR)bt[i];
    if ((a - s_selfBase < s_selfSize) || a == (UINT_PTR)ret) continue;
    e->bt[e->btN++] = (unsigned)a;
  }
  return e;
}

static void rec_present(IDirect3DDevice9* dev, int ev) {
  const LONG s = s_recState, next = dtr_step((int)s, ev);
  if (next == s) return;
  if (next == DTR_RECORDING) {
    s_recN = 0;
    IDirect3DSurface9* bb = 0;
    IDirect3DSurface9* rt = 0;
    if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) bb->Release();   // pointers only
    if (SUCCEEDED(dev->GetRenderTarget(0, &rt)) && rt) rt->Release();
    s_recBb = bb;
    s_recRt0 = rt;
    s_recLastVs = s_recLastPs = (const void*)1;   // the first bind of each is kept
  }
  InterlockedExchange(&s_recState, next);
}

static const char* const kWhy[DREC_WHY_COUNT] = { "a flight's first frame", "the render watch's stall" };

static int rec_arm(int why) {
  if (!InterlockedCompareExchange(&s_trHooked, 0, 0) || s_recState != DTR_IDLE) return 0;
  if (!drec_may_arm(s_recCounts, why)) return 0;
  s_recWhy = why;
  InterlockedExchange(&s_recState, dtr_step(DTR_IDLE, DTR_EV_ARM));
  return 1;
}

static const char* rec_kind(int k) {
  switch (k) {
    case DREC_RT: return "SetRenderTarget";
    case DREC_CLEAR: return "Clear";
    case DREC_STRETCH: return "StretchRect";
    case DREC_FILL: return "ColorFill";
    case DREC_TEX0: return "SetTexture0";
    case DREC_VS: return "SetVertexShader";
    case DREC_PS: return "SetPixelShader";
    default: return "?";
  }
}

// Game thread at Present entry: a READY record goes out 48 lines a Present, so no frame logs all 512.
static void rec_drain(void) {
  if (InterlockedCompareExchange(&s_recState, 0, 0) != DTR_READY) return;
  if (!s_recLogged) {
    s_recTotal = InterlockedCompareExchange(&s_recN, 0, 0);
    ++s_recNo;
    np_log("d3dcap: record %u (%s): %ld call(s), %d kept (the first %d, then the last %d); back buffer %p; render "
           "target 0 at the start %p; flying %ld", s_recNo, kWhy[s_recWhy], s_recTotal, drec_kept(s_recTotal), DREC_HEAD,
           DREC_RING, s_recBb, s_recRt0, s_flightClear);
  }
  const int kept = drec_kept(s_recTotal);
  for (int lines = 0; s_recLogged < kept && lines < 48; ++lines, ++s_recLogged) {
    const DrecEntry& e = s_rec[drec_order(s_recLogged, s_recTotal)];
    char from[96], bt[4][96];
    stall_name_addr(from, sizeof from, e.ret);
    for (unsigned i = 0; i < DREC_BT; ++i) {
      bt[i][0] = 0;
      if (i < e.btN) stall_name_addr(bt[i], sizeof bt[i], e.bt[i]);
    }
    np_log("d3dcap: record %u #%04ld %s %s a=%p%s b=%p idx %u flags 0x%x from %s ; %s %s %s %s", s_recNo,
           drec_call_no(s_recLogged, s_recTotal), e.game ? "game" : "job", rec_kind(e.kind), e.a,
           e.a && e.a == s_recBb ? " (back buffer)" : "", e.b, e.idx, e.flags, from, bt[0], bt[1], bt[2], bt[3]);
  }
  if (s_recLogged < kept) return;
  s_recLogged = 0;
  InterlockedExchange(&s_recState, dtr_step(DTR_READY, DTR_EV_TAKEN));
}

static int own_seh_read(unsigned addr, unsigned* out, void*) {
  __try {
    *out = *(const volatile unsigned*)(UINT_PTR)addr;
    return 1;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
}

// On the calling (engine) thread, inside its own stack's TEB limits; __asm keeps this function's EBP frame.
static __declspec(noinline) void note_last_call(int kind) {
  unsigned fp;
  __asm mov fp, ebp
  const NT_TIB* tib = (const NT_TIB*)NtCurrentTeb();
  const int n = stall_walk(fp, (unsigned)(UINT_PTR)tib->StackLimit, (unsigned)(UINT_PTR)tib->StackBase, own_seh_read,
                           0, s_lastRets, LAST_FRAMES);
  InterlockedExchange(&s_lastN, n);
  InterlockedExchange(&s_lastKind, kind);
  InterlockedExchange(&s_lastTid, (LONG)GetCurrentThreadId());
}

static void note_engine_call(int kind) {
  if (InterlockedCompareExchange(&s_ownFrame, 0, 0)) return;
  const LONG tid = (LONG)GetCurrentThreadId();
  if (tid == InterlockedCompareExchange(&s_presentTid, 0, 0)) {
    InterlockedIncrement(&s_callsGame);
  } else {
    if (tid != InterlockedCompareExchange(&s_jobTid, 0, 0)) {
      InterlockedExchange(&s_jobStackBase, (LONG)(UINT_PTR)((const NT_TIB*)NtCurrentTeb())->StackBase);
      InterlockedExchange(&s_jobTid, tid);
    }
    InterlockedIncrement(&s_callsJob);
  }
  if (InterlockedCompareExchange(&s_watchHot, 0, 0)) note_last_call(kind);
}

static int rtc_on(IDirect3DDevice9* dev) {
  return dev && dev == s_dev && s_rtcHooked && s_rtcReady && InterlockedCompareExchange(&s_flightClear, 0, 0);
}

// At the end of a Present while flying: a new frame, the back buffer as it is now.
static void rtc_present(IDirect3DDevice9* dev) {
  if (!InterlockedCompareExchange(&s_flightClear, 0, 0) || !s_rtcHooked) { s_rtcReady = 0; return; }
  IDirect3DSurface9* bb = 0;
  D3DSURFACE_DESC d;
  if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) { s_rtcReady = 0; return; }
  const bool got = SUCCEEDED(bb->GetDesc(&d));
  bb->Release();
  if (!got) { s_rtcReady = 0; return; }
  if (!s_rtcReady || s_rtc.bb != bb || s_rtc.w != (int)d.Width || s_rtc.h != (int)d.Height)
    rtc_init(&s_rtc, bb, (int)d.Width, (int)d.Height);
  rtc_set_kept(&s_rtc, s_rtcKeptSurf);
  rtc_frame(&s_rtc);
  s_rtcReady = 1;
  InterlockedIncrement(&s_rtcFrames);
}

static void tr_desc(IDirect3DSurface9* s, DtrEntry* e) {
  D3DSURFACE_DESC d;
  if (s && SUCCEEDED(s->GetDesc(&d))) { e->w = d.Width; e->h = d.Height; e->fmt = (unsigned)d.Format; }
}

static DtrEntry* tr_add(int kind) {
  const int slot = dtr_slot(InterlockedIncrement(&s_trN) - 1);
  if (slot < 0) return 0;
  DtrEntry* e = &s_tr[slot];
  memset(e, 0, sizeof *e);
  e->kind = kind;
  return e;
}

// The trace's state at a Present: its entry ends a recording, its end starts an armed one (with the back buffer and
// render target 0 as they are then).
static void tr_present(IDirect3DDevice9* dev, int ev) {
  const LONG s = s_trState, next = dtr_step((int)s, ev);
  if (next == s) return;
  if (next == DTR_RECORDING) {
    s_trN = 0;
    IDirect3DSurface9* bb = 0;
    IDirect3DSurface9* rt = 0;
    memset(&s_trBbDesc, 0, sizeof s_trBbDesc);
    if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) { bb->GetDesc(&s_trBbDesc); bb->Release(); }
    if (SUCCEEDED(dev->GetRenderTarget(0, &rt)) && rt) rt->Release();
    s_trBb = bb;
    s_trRt0 = rt;
    s_trCurRt = rt;
  }
  InterlockedExchange(&s_trState, next);
}

IDirect3DDevice9* d3dcap_device(void) { return s_dev; }
HWND d3dcap_window(void) { return s_window; }
unsigned long d3dcap_create_tid(void) { return (unsigned long)s_createTid; }
bool d3dcap_vsync_on(void) { return !s_immediate; }

// Slot address of a COM object's vtable, read with a readability check on both hops.
static void* com_slot(void* obj, int slot) {
  if (!hook_readable(obj, sizeof(void*))) return 0;
  void** vt = *(void***)obj;
  if (!hook_readable(vt + slot, sizeof(void*))) return 0;
  return vt[slot];
}

// Where a body read from a vtable lives decides whether it is detoured (d3dbody.h); anything but a body inside
// d3d9.dll is logged with its module.
static bool body_may_hook(int method, const char* name, void* body) {
  HMODULE mod = 0;
  if (body && !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  (LPCWSTR)body, &mod))
    mod = 0;
  const int verdict = d3dbody_verdict(method, body != 0, mod != 0 && mod == s_d3d9);
  if (verdict == D3DB_HOOK) return true;
  wchar_t full[MAX_PATH];
  const wchar_t* where = L"unknown";
  if (mod && GetModuleFileNameW(mod, full, MAX_PATH)) {
    const wchar_t* slash = wcsrchr(full, L'\\');
    where = slash ? slash + 1 : full;
  }
  if (verdict == D3DB_HOOK_LOGGED) {
    np_log("d3dcap: CreateDevice body %p is in %ls, not d3d9.dll; hooked anyway", body, where);
    return true;
  }
  np_log("d3dcap: %s at %p is in %ls, not d3d9.dll; not hooked", name, body, where);
  return false;
}

// dnCommon's own record of the render job's thread (engine-map.md section 5), read to confirm the detours' answer.
static DWORD dncommon_render_tid(void) {
  static const volatile DWORD* s_p;
  static int s_looked;
  if (!s_looked) {
    s_looked = 1;
    const HMODULE m = GetModuleHandleW(L"dnCommon.dll");
    if (m) s_p = (const volatile DWORD*)GetProcAddress(m, "?g_RenderThreadID@@3KA");
  }
  if (!s_p) return 0;
  __try { return *s_p; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static RstWatch s_rw;
static HANDLE s_jobThread;
static LONG s_jobOpenTid;
static unsigned s_rwLines;

// Game thread at Present entry (the job has finished: WaitTillNotRendering ran first). One frame of the render watch.
static void render_watch(void) {
  InterlockedExchange(&s_presentTid, (LONG)GetCurrentThreadId());
  if (!InterlockedCompareExchange(&s_trHooked, 0, 0)) return;
  const LONG jobTid = InterlockedCompareExchange(&s_jobTid, 0, 0);
  if (jobTid && jobTid != s_jobOpenTid) {
    s_jobOpenTid = jobTid;
    if (s_jobThread) CloseHandle(s_jobThread);
    s_jobThread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                             (DWORD)jobTid);
    const DWORD err = s_jobThread ? 0 : GetLastError();
    np_log("d3dcap: render watch: the engine's render calls come from tid %ld (game thread %ld; dnCommon "
           "g_RenderThreadID %lu), stack base %08lx; handle %s (err %lu)", jobTid, s_presentTid, dncommon_render_tid(),
           (unsigned long)s_jobStackBase, s_jobThread ? "open" : "NOT open", err);
  }
  const LONG binds = InterlockedCompareExchange(&s_bbBinds, 0, 0);
  const int rendered = s_bbNow ? binds != s_bbSeenWatch : -1;
  s_bbSeenWatch = binds;
  int sample = 0;
  const int ev = rst_step(&s_rw, rendered, &sample);
  // Outside a flight it only counts: a menu draws the back buffer without binding it, so a run there is no stall.
  const int flying = InterlockedCompareExchange(&s_flightClear, 0, 0) != 0;
  InterlockedExchange(&s_watchHot, flying && s_rw.frames >= 2);
  if (ev == RST_NONE || !flying || ++s_rwLines > 64) return;
  if (ev == RST_BACK) {
    np_log("d3dcap: render watch: the engine draws the back buffer again after %d frame(s)", s_rw.lastRun);
    return;
  }
  if (ev == RST_STALLED && rec_arm(DREC_WHY_STALL))
    np_log("d3dcap: render watch: the next frame is recorded (d3dcap: record lines)");
  DWORD code = 0;
  const int codeOk = s_jobThread && GetExitCodeThread(s_jobThread, &code);
  np_log("d3dcap: render watch: %d frame(s) presented without the engine binding the back buffer (episode %d, "
         "flying %ld); render calls this session: %ld on the job, %ld on the game thread; job thread %ld %s "
         "(exit code %lu)", s_rw.frames, s_rw.episodes, s_flightClear, s_callsJob, s_callsGame, s_jobOpenTid,
         rst_thread_state(codeOk, code), (unsigned long)code);
  // The last engine call before this Present: where the frame's work ended, from its own stack (no suspend needed).
  const int n = (int)InterlockedCompareExchange(&s_lastN, 0, 0);
  char stack[1100];
  int len = 0;
  stack[0] = 0;
  for (int i = 0; i < n && i < LAST_FRAMES && len < (int)sizeof(stack) - 2; ++i) {
    if (len) stack[len++] = ' ';
    len += stall_name_addr(stack + len, (int)sizeof(stack) - len, s_lastRets[i]);
  }
  np_log("d3dcap: render watch: the last engine render call (%s) on tid %ld: stack(%d) %s",
         s_lastKind == DTR_CLEAR ? "Clear" : "SetRenderTarget", s_lastTid, n, stack);
  if (sample && codeOk && code == STILL_ACTIVE && (DWORD)s_jobOpenTid != GetCurrentThreadId()) {
    char tag[48];
    _snprintf_s(tag, sizeof tag, _TRUNCATE, "render thread %ld", s_jobOpenTid);
    stall_sample_thread(s_jobThread, (unsigned)s_jobStackBase, tag);
  }
}

// Game thread at Present entry, the job idle (WaitTillNotRendering ran first) and before our own drawing: render target
// 0 back to the back buffer when the engine's frame ended elsewhere, so the next Lock takes the back buffer as its main
// target. Through the real SetRenderTarget: neither the watch nor the trace counts it as the engine's.
static void heal_rt0(IDirect3DDevice9* dev) {
  IDirect3DSurface9* rt = 0;
  IDirect3DSurface9* bb = 0;
  const int haveRt = SUCCEEDED(dev->GetRenderTarget(0, &rt)) && rt;
  const int haveBb = SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb;
  if (rst_heal_rt(haveRt, haveRt && haveBb && rt == bb, haveBb)) {
    const HRESULT hr = s_realSetRt ? s_realSetRt(dev, 0, bb) : dev->SetRenderTarget(0, bb);
    InterlockedIncrement(SUCCEEDED(hr) ? &s_heals : &s_healFails);
    if (++s_healLines <= 16 || s_healLines % 600 == 0)
      np_log("d3dcap: the engine's frame ended on render target %p, not the back buffer %p; set back (hr=0x%08lX, "
             "heal %u, flying %ld)", (void*)rt, (void*)bb, (unsigned long)hr, s_healLines, s_flightClear);
  }
  if (rt) rt->Release();
  if (bb) bb->Release();
}

static HRESULT STDMETHODCALLTYPE present_detour(IDirect3DDevice9* dev, const RECT* src, const RECT* dst,
                                                HWND wnd, const RGNDATA* dirty) {
  const bool ours = dev && dev == s_dev;
  if (ours) tr_present(dev, DTR_EV_PRESENT_ENTRY);   // before our draws: the trace is the engine's frame alone
  if (ours) rec_present(dev, DTR_EV_PRESENT_ENTRY);
  if (ours) render_watch();
  if (ours) rec_drain();
  if (ours) heal_rt0(dev);
  if (ours) InterlockedExchange(&s_ownFrame, 1);
  if (ours) frame_on_present(dev);
  const HRESULT hr = s_realPresent(dev, src, dst, wnd, dirty);
  if (ours) InterlockedExchange(&s_clearsSincePresent, 0);
  if (ours) rtc_present(dev);
  if (ours) {
    IDirect3DSurface9* bb = 0;
    if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) bb->Release();   // the pointer only
    InterlockedExchangePointer((void* volatile*)&s_bbNow, bb);
  }
  if (ours) tr_present(dev, DTR_EV_PRESENT_DONE);
  if (ours) rec_present(dev, DTR_EV_PRESENT_DONE);
  if (ours) frame_after_present();
  if (ours) InterlockedExchange(&s_ownFrame, 0);
  return hr;
}

// The engine's own per-frame Clear (UD3DRenderDevice9::Lock): while flying its first one after Present also clears
// the colour target (freecam.h freecam_clear_flags). Every other call passes unchanged. It stays on through a photo
// capture on purpose: a tile's empty areas get the clear colour, not the previous tile (freecam.md section 1d).
static HRESULT STDMETHODCALLTYPE clear_detour(IDirect3DDevice9* dev, DWORD count, const D3DRECT* rects, DWORD flags,
                                              D3DCOLOR colour, float z, DWORD stencil) {
  if (dev && dev == s_dev) {
    note_engine_call(DTR_CLEAR);
    const int first = InterlockedIncrement(&s_clearsSincePresent) == 1;
    const int flying = InterlockedCompareExchange(&s_flightClear, 0, 0) != 0;
    const DWORD want = (DWORD)freecam_clear_flags(flying, first, count, rects != 0, flags);
    if (first) {   // Lock's clear: did it land on the back buffer (the RendMap 6 latch)?
      IDirect3DSurface9* rt = 0;
      const bool got = SUCCEEDED(dev->GetRenderTarget(0, &rt)) && rt;
      if (rt) rt->Release();   // the pointer only
      InterlockedIncrement(&s_lockFrames);
      if (!got || (const void*)rt != s_bbNow) InterlockedIncrement(&s_lockMisses);
    }
    if (first && flying) {
      InterlockedIncrement(&s_clearFirsts);
      InterlockedExchange(&s_clearLastFlags, (LONG)flags);
      if (flags & D3DCLEAR_TARGET) InterlockedIncrement(&s_clearEngine);
      if (want != flags) InterlockedIncrement(&s_clearAdded);
    }
    flags = want;
    if (rec_on(dev) && !InterlockedCompareExchange(&s_ownFrame, 0, 0)) {
      if (DrecEntry* r = rec_add(DREC_CLEAR, _ReturnAddress())) {
        r->a = s_trCurRt;
        r->idx = count;
        r->flags = flags;
      }
    }
    if ((flags & D3DCLEAR_TARGET) && !count && rtc_on(dev)) rtc_engine_clear(&s_rtc, s_rtcCur);
    if (tr_on(dev)) {
      if (DtrEntry* e = tr_add(DTR_CLEAR)) {
        e->a = s_trCurRt;
        tr_desc((IDirect3DSurface9*)s_trCurRt, e);
        e->flags = flags;
        e->colour = colour;
        e->idx = count;
      }
    }
  }
  return s_realClear(dev, count, rects, flags, colour, z, stencil);
}

// The trace's three other calls: recorded only while a trace records. In flight SetRenderTarget also fills a
// screen-sized target at its first bind of a frame (rtc_bind_surface) before it passes the call on.
static HRESULT STDMETHODCALLTYPE fill_detour(IDirect3DDevice9* dev, IDirect3DSurface9* surf, const RECT* r,
                                             D3DCOLOR colour);

// In flight a screen-sized target's first bind of a frame fills it with 0 first, through our ColorFill detour so the
// frame trace shows it (rtclear.h). The bind itself goes on unchanged.
static void rtc_bind_surface(IDirect3DDevice9* dev, DWORD index, IDirect3DSurface9* surf) {
  if (index == 0) s_rtcCur = surf;
  if (!surf) return;
  D3DSURFACE_DESC d;
  if (FAILED(surf->GetDesc(&d)) || (int)d.Width != s_rtc.w || (int)d.Height != s_rtc.h) return;
  if (!rtc_format_ok((unsigned)d.Format)) { InterlockedIncrement(&s_rtcFormat); return; }
  void* tex = 0;
  if (SUCCEEDED(surf->GetContainer(kIidTexture9, &tex)) && tex) ((IUnknown*)tex)->Release();   // the pointer only
  const int act = rtc_bind(&s_rtc, surf, (int)d.Width, (int)d.Height, tex);
  if (act == RTC_LEARN) {
    InterlockedIncrement(&s_rtcLearned);
    if (RtcSurf* e = rtc_find(&s_rtc, surf)) e->fmt = (unsigned)d.Format;
  } else if (act == RTC_SKIP_HISTORY) InterlockedIncrement(&s_rtcHistory);
  else if (act == RTC_SKIP_ENGINE) InterlockedIncrement(&s_rtcEngine);
  else if (act == RTC_SKIP_FULL) InterlockedIncrement(&s_rtcFull);
  else if (act == RTC_SKIP_KEPT) InterlockedIncrement(&s_rtcKept);
  if (act != RTC_FILL) return;
  LARGE_INTEGER t0, t1, f;
  QueryPerformanceCounter(&t0);
  const HRESULT hr = fill_detour(dev, surf, NULL, 0);
  QueryPerformanceCounter(&t1);
  QueryPerformanceFrequency(&f);
  InterlockedExchangeAdd(&s_rtcFillUs, (LONG)((t1.QuadPart - t0.QuadPart) * 1000000 / f.QuadPart));
  InterlockedIncrement(SUCCEEDED(hr) ? &s_rtcFills : &s_rtcFillFails);
}

static HRESULT STDMETHODCALLTYPE settex_detour(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex) {
  if (tex && rtc_on(dev)) rtc_sampled(&s_rtc, tex);
  if (stage == 0 && rec_on(dev) && !InterlockedCompareExchange(&s_ownFrame, 0, 0))
    if (DrecEntry* r = rec_add(DREC_TEX0, _ReturnAddress())) r->a = tex;
  return s_realSetTex(dev, stage, tex);
}

void d3dcap_rtclear_take(RtcStats* out) {
  out->frames = (unsigned)InterlockedExchange(&s_rtcFrames, 0);
  out->fills = (unsigned)InterlockedExchange(&s_rtcFills, 0);
  out->fillFails = (unsigned)InterlockedExchange(&s_rtcFillFails, 0);
  out->history = (unsigned)InterlockedExchange(&s_rtcHistory, 0);
  out->engine = (unsigned)InterlockedExchange(&s_rtcEngine, 0);
  out->learned = (unsigned)InterlockedExchange(&s_rtcLearned, 0);
  out->fillUs = (unsigned)InterlockedExchange(&s_rtcFillUs, 0);
  out->full = (unsigned)InterlockedExchange(&s_rtcFull, 0);
  out->kept = (unsigned)InterlockedExchange(&s_rtcKept, 0);
  out->format = (unsigned)InterlockedExchange(&s_rtcFormat, 0);
  out->hooked = InterlockedCompareExchange(&s_rtcHooked, 0, 0) != 0;
}

int d3dcap_lock_on_bb_take(void) {
  if (!InterlockedCompareExchange(&s_clearHooked, 0, 0) || !s_bbNow) return -1;
  return rst_lock_verdict(InterlockedExchange(&s_lockFrames, 0), InterlockedExchange(&s_lockMisses, 0));
}

void d3dcap_heal_take(unsigned* heals, unsigned* fails) {
  *heals = (unsigned)InterlockedExchange(&s_heals, 0);
  *fails = (unsigned)InterlockedExchange(&s_healFails, 0);
}

unsigned long d3dcap_render_tid(void) { return (unsigned long)InterlockedCompareExchange(&s_jobTid, 0, 0); }

void d3dcap_rtclear_keep(const void* surf) { InterlockedExchangePointer((void* volatile*)&s_rtcKeptSurf, (void*)surf); }

// At Present (the job done, see above): each target whose verdict changed since its last line, capped for the session.
void d3dcap_rtclear_report(void) {
  static unsigned s_lines;
  if (!s_rtcReady) return;
  for (int i = 0; i < s_rtc.n; ++i) {
    RtcSurf& e = s_rtc.s[i];
    if (e.verdict == e.reported || e.verdict == RTC_LEARN) continue;
    e.reported = e.verdict;
    if (++s_lines > 64) continue;
    np_log("d3dcap: render target %p %ux%u %s(%u): %s", e.surf, e.w, e.h, dtr_fmt_name(e.fmt), e.fmt,
           e.verdict == RTC_FILL ? "filled each first bind" : e.verdict == RTC_SKIP_HISTORY ?
           "kept: sampled before its bind (last frame's data)" : "left to the engine's own clear");
  }
}

static HRESULT STDMETHODCALLTYPE setrt_detour(IDirect3DDevice9* dev, DWORD index, IDirect3DSurface9* surf) {
  if (rec_on(dev) && !InterlockedCompareExchange(&s_ownFrame, 0, 0))
    if (DrecEntry* r = rec_add(DREC_RT, _ReturnAddress())) { r->a = surf; r->idx = index; }
  if (dev && dev == s_dev) {
    note_engine_call(DTR_RT);
    if (rst_engine_bb_bind(index, surf && surf == s_bbNow, InterlockedCompareExchange(&s_ownFrame, 0, 0) != 0))
      InterlockedIncrement(&s_bbBinds);
  }
  if (rtc_on(dev)) rtc_bind_surface(dev, index, surf);
  if (tr_on(dev)) {
    if (index == 0) s_trCurRt = surf;
    if (DtrEntry* e = tr_add(DTR_RT)) {
      e->a = surf;
      e->idx = index;
      tr_desc(surf, e);
    }
  }
  return s_realSetRt(dev, index, surf);
}

static HRESULT STDMETHODCALLTYPE stretch_detour(IDirect3DDevice9* dev, IDirect3DSurface9* src, const RECT* sr,
                                                IDirect3DSurface9* dst, const RECT* dr, D3DTEXTUREFILTERTYPE f) {
  if (rec_on(dev) && !InterlockedCompareExchange(&s_ownFrame, 0, 0))
    if (DrecEntry* r = rec_add(DREC_STRETCH, _ReturnAddress())) { r->a = src; r->b = dst; r->flags = (unsigned)f; }
  if (tr_on(dev)) {
    if (DtrEntry* e = tr_add(DTR_STRETCH)) {
      e->a = src;
      e->b = dst;
      tr_desc(src, e);
      e->flags = (unsigned)f;
      e->idx = (sr ? 1u : 0u) | (dr ? 2u : 0u);
    }
  }
  return s_realStretch(dev, src, sr, dst, dr, f);
}

static HRESULT STDMETHODCALLTYPE fill_detour(IDirect3DDevice9* dev, IDirect3DSurface9* surf, const RECT* r,
                                             D3DCOLOR colour) {
  if (rec_on(dev) && !InterlockedCompareExchange(&s_ownFrame, 0, 0))
    if (DrecEntry* r = rec_add(DREC_FILL, _ReturnAddress())) { r->a = surf; r->flags = colour; }
  if (tr_on(dev)) {
    if (DtrEntry* e = tr_add(DTR_FILL)) {
      e->a = surf;
      tr_desc(surf, e);
      e->colour = colour;
      e->idx = r ? 1u : 0u;
    }
  }
  return s_realFill(dev, surf, r, colour);
}

// Shader binds, recorded only while a frame record runs and only when the pointer changes (d3drec.h).
static HRESULT STDMETHODCALLTYPE setvs_detour(IDirect3DDevice9* dev, IDirect3DVertexShader9* vs) {
  if (rec_on(dev) && !InterlockedCompareExchange(&s_ownFrame, 0, 0) && drec_keep_shader(s_recLastVs, vs)) {
    s_recLastVs = vs;
    if (DrecEntry* r = rec_add(DREC_VS, _ReturnAddress())) r->a = vs;
  }
  return s_realSetVs(dev, vs);
}

static HRESULT STDMETHODCALLTYPE setps_detour(IDirect3DDevice9* dev, IDirect3DPixelShader9* ps) {
  if (rec_on(dev) && !InterlockedCompareExchange(&s_ownFrame, 0, 0) && drec_keep_shader(s_recLastPs, ps)) {
    s_recLastPs = ps;
    if (DrecEntry* r = rec_add(DREC_PS, _ReturnAddress())) r->a = ps;
  }
  return s_realSetPs(dev, ps);
}

int d3dcap_record_flight(void) { return rec_arm(DREC_WHY_FLIGHT); }

void d3dcap_watch_log(const char* when) {
  np_log("d3dcap: render watch at %s: %d frame(s) in a row without an engine back-buffer bind (stall at %d), "
         "%d episode(s) so far", when, s_rw.frames, RST_FIRST, s_rw.episodes);
}

void d3dcap_trace_arm(void) { InterlockedExchange(&s_trState, dtr_step((int)s_trState, DTR_EV_ARM)); }

bool d3dcap_trace_take(const DtrEntry** e, int* n, int* dropped, const void** bb, const void** rt0, unsigned* bbW,
                       unsigned* bbH, unsigned* bbFmt) {
  if (InterlockedCompareExchange(&s_trState, 0, 0) != DTR_READY) return false;
  const int all = (int)s_trN;
  *e = s_tr;
  *n = all < DTR_CAP ? all : DTR_CAP;
  *dropped = all - *n;
  *bb = s_trBb;
  *rt0 = s_trRt0;
  *bbW = s_trBbDesc.Width;
  *bbH = s_trBbDesc.Height;
  *bbFmt = (unsigned)s_trBbDesc.Format;
  return true;
}

void d3dcap_trace_done(void) { InterlockedExchange(&s_trState, dtr_step((int)s_trState, DTR_EV_TAKEN)); }

int d3dcap_trace_hooked(void) { return InterlockedCompareExchange(&s_trHooked, 0, 0) != 0; }

void d3dcap_set_flight_clear(int on) { InterlockedExchange(&s_flightClear, on ? 1 : 0); }

void d3dcap_clear_take(unsigned* firsts, unsigned* added, unsigned* engineTarget, unsigned* lastFlags, int* hooked) {
  *firsts = (unsigned)InterlockedExchange(&s_clearFirsts, 0);
  *added = (unsigned)InterlockedExchange(&s_clearAdded, 0);
  *engineTarget = (unsigned)InterlockedExchange(&s_clearEngine, 0);
  *lastFlags = (unsigned)InterlockedCompareExchange(&s_clearLastFlags, 0, 0);
  *hooked = InterlockedCompareExchange(&s_clearHooked, 0, 0) != 0;
}

static HRESULT STDMETHODCALLTYPE reset_detour(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
  const bool ours = dev && dev == s_dev;
  if (ours) frame_on_reset_before();
  // The engine recreates its D3DPOOL_DEFAULT targets: a new one may land at an old address.
  if (ours) s_rtcReady = 0;
  // Borderless (display.h): d3d9 may get a windowed copy of the engine's fullscreen params.
  D3DPRESENT_PARAMETERS scratch;
  D3DPRESENT_PARAMETERS* use =
      ours ? display_params(pp, &scratch, pp && pp->hDeviceWindow ? pp->hDeviceWindow : s_window, "Reset") : pp;
  HRESULT hr = s_realReset(dev, use);
  if (display_after(pp, use, hr, "Reset")) hr = s_realReset(dev, pp);
  if (ours) frame_on_reset_after(SUCCEEDED(hr));
  if (ours && SUCCEEDED(hr) && pp)
    InterlockedExchange(&s_immediate, pp->PresentationInterval == D3DPRESENT_INTERVAL_IMMEDIATE ? 1 : 0);
  if (ours && SUCCEEDED(hr) && use) InterlockedExchange(&s_windowed, use->Windowed ? 1 : 0);
  np_log("d3dcap: Reset dev=%p ours=%d tid=%lu hr=0x%08lX bb=%ux%u", (void*)dev, ours ? 1 : 0,
         GetCurrentThreadId(), (unsigned long)hr, pp ? pp->BackBufferWidth : 0u,
         pp ? pp->BackBufferHeight : 0u);
  return hr;
}

static void hook_device_bodies(IDirect3DDevice9* dev) {
  void* reset = com_slot(dev, SLOT_DEV_RESET);
  void* present = com_slot(dev, SLOT_DEV_PRESENT);
  if (InterlockedCompareExchange(&s_devClaimed, 1, 0) == 0) {
    s_resetBody = reset;
    s_presentBody = present;
    // Both or neither: drawing from Present without the Reset detour would keep our state block across a Reset.
    const bool resetOk = body_may_hook(D3DM_RESET, "IDirect3DDevice9::Reset", reset);
    const bool presentOk = body_may_hook(D3DM_PRESENT, "IDirect3DDevice9::Present", present);
    if (!resetOk || !presentOk) {
      np_log("d3dcap: device bodies not hooked; nothing is drawn this session");
    } else if (!hook_install_live(reset, "IDirect3DDevice9::Reset", (void*)&reset_detour, (void**)&s_realReset)) {
      np_log("d3dcap: Reset not hooked, so Present is not either; nothing is drawn this session");
    } else {
      hook_install_live(present, "IDirect3DDevice9::Present", (void*)&present_detour, (void**)&s_realPresent);
      // On its own: refused, the free camera's frame clear is off and nothing else changes.
      void* clear = com_slot(dev, SLOT_DEV_CLEAR);
      if (body_may_hook(D3DM_CLEAR, "IDirect3DDevice9::Clear", clear) &&
          hook_install_live(clear, "IDirect3DDevice9::Clear", (void*)&clear_detour, (void**)&s_realClear))
        InterlockedExchange(&s_clearHooked, 1);
      else np_log("d3dcap: Clear not hooked; the free camera cannot clear where nothing draws");
      // The frame trace's three. They record while a trace runs; SetRenderTarget also carries the in-flight fill, which
      // needs SetTexture too (below).
      void* setRt = com_slot(dev, SLOT_DEV_SETRENDERTARGET);
      void* stretch = com_slot(dev, SLOT_DEV_STRETCHRECT);
      void* fill = com_slot(dev, SLOT_DEV_COLORFILL);
      if (body_may_hook(D3DM_TRACE, "IDirect3DDevice9::SetRenderTarget", setRt) &&
          body_may_hook(D3DM_TRACE, "IDirect3DDevice9::StretchRect", stretch) &&
          body_may_hook(D3DM_TRACE, "IDirect3DDevice9::ColorFill", fill) &&
          hook_install_live(setRt, "IDirect3DDevice9::SetRenderTarget", (void*)&setrt_detour, (void**)&s_realSetRt) &&
          hook_install_live(stretch, "IDirect3DDevice9::StretchRect", (void*)&stretch_detour, (void**)&s_realStretch) &&
          hook_install_live(fill, "IDirect3DDevice9::ColorFill", (void*)&fill_detour, (void**)&s_realFill))
        InterlockedExchange(&s_trHooked, 1);
      else np_log("d3dcap: the frame trace's detours are not all in; no frame trace");
      // The smear fix also needs SetTexture: without it a history target could not be told apart, so nothing fills.
      void* setTex = com_slot(dev, SLOT_DEV_SETTEXTURE);
      if (s_trHooked && body_may_hook(D3DM_TRACE, "IDirect3DDevice9::SetTexture", setTex) &&
          hook_install_live(setTex, "IDirect3DDevice9::SetTexture", (void*)&settex_detour, (void**)&s_realSetTex))
        InterlockedExchange(&s_rtcHooked, 1);
      else np_log("d3dcap: SetTexture not hooked; the free camera's render-target fill is off");
      // The frame record's shader binds: refused, the record simply has none.
      void* setVs = com_slot(dev, SLOT_DEV_SETVERTEXSHADER);
      void* setPs = com_slot(dev, SLOT_DEV_SETPIXELSHADER);
      if (!(s_trHooked && body_may_hook(D3DM_TRACE, "IDirect3DDevice9::SetVertexShader", setVs) &&
            body_may_hook(D3DM_TRACE, "IDirect3DDevice9::SetPixelShader", setPs) &&
            hook_install_live(setVs, "IDirect3DDevice9::SetVertexShader", (void*)&setvs_detour, (void**)&s_realSetVs) &&
            hook_install_live(setPs, "IDirect3DDevice9::SetPixelShader", (void*)&setps_detour, (void**)&s_realSetPs)))
        np_log("d3dcap: shader binds not hooked; the frame record has none");
    }
  } else if (reset != s_resetBody || present != s_presentBody) {
    // A second device class (e.g. a different device type) would need its own detours.
    np_log("d3dcap: device %p has other Reset/Present bodies (%p/%p, hooked %p/%p); not hooked",
           (void*)dev, reset, present, s_resetBody, s_presentBody);
  }
}

static HRESULT STDMETHODCALLTYPE create_device_detour(IDirect3D9* self, UINT adapter, D3DDEVTYPE type,
                                                      HWND focus, DWORD flags,
                                                      D3DPRESENT_PARAMETERS* pp,
                                                      IDirect3DDevice9** out) {
  if (s_dev) frame_on_device_replaced();
  // Borderless (display.h): d3d9 may get a windowed copy of the engine's fullscreen params; the engine's own
  // struct is the fallback when d3d9 refuses it.
  D3DPRESENT_PARAMETERS scratch;
  D3DPRESENT_PARAMETERS* use = display_params(pp, &scratch, pp && pp->hDeviceWindow ? pp->hDeviceWindow : focus,
                                              "CreateDevice");
  HRESULT hr = s_realCreateDevice(self, adapter, type, focus, flags, use, out);
  if (display_after(pp, use, hr, "CreateDevice")) {
    use = pp;   // the log below reports the params the device was made with
    hr = s_realCreateDevice(self, adapter, type, focus, flags, pp, out);
  }
  const DWORD tid = GetCurrentThreadId();
  // The out-parameter is read only now that the call that fills it has returned.
  IDirect3DDevice9* dev = (SUCCEEDED(hr) && out) ? *out : 0;
  if (!dev) {
    np_log("d3dcap: CreateDevice failed hr=0x%08lX tid=%lu", (unsigned long)hr, tid);
    return hr;
  }
  np_log("d3dcap: CreateDevice ok dev=%p tid=%lu adapter=%u type=%d focus=%p behavior=0x%08lX "
         "bb=%ux%u windowed=%d devwnd=%p",
         (void*)dev, tid, adapter, (int)type, (void*)focus, (unsigned long)flags,
         pp ? pp->BackBufferWidth : 0u, pp ? pp->BackBufferHeight : 0u,
         use ? (int)use->Windowed : -1, pp ? (void*)pp->hDeviceWindow : (void*)0);
  if (pp) np_log("d3dcap: present interval 0x%08lX (immediate=%d)", (unsigned long)pp->PresentationInterval,
                 pp->PresentationInterval == D3DPRESENT_INTERVAL_IMMEDIATE ? 1 : 0);
  hook_device_bodies(dev);
  s_window = focus ? focus : (pp ? pp->hDeviceWindow : 0);
  InterlockedExchange(&s_createTid, (LONG)tid);
  if (pp) InterlockedExchange(&s_immediate, pp->PresentationInterval == D3DPRESENT_INTERVAL_IMMEDIATE ? 1 : 0);
  if (use) InterlockedExchange(&s_windowed, use->Windowed ? 1 : 0);
  InterlockedExchangePointer((void* volatile*)&s_dev, dev);
  return hr;
}

static IDirect3D9* WINAPI create9_detour(UINT sdk) {
  IDirect3D9* d3d = s_realCreate9(sdk);
  np_log("d3dcap: Direct3DCreate9(sdk=%u) tid=%lu -> %p", sdk, GetCurrentThreadId(), (void*)d3d);
  if (!d3d) return d3d;
  void* body = com_slot(d3d, SLOT_D3D_CREATEDEVICE);
  if (InterlockedCompareExchange(&s_cdClaimed, 1, 0) == 0) {
    s_cdBody = body;
    if (body_may_hook(D3DM_CREATEDEVICE, "IDirect3D9::CreateDevice", body))
      hook_install_live(body, "IDirect3D9::CreateDevice", (void*)&create_device_detour, (void**)&s_realCreateDevice);
  } else if (body != s_cdBody) {
    np_log("d3dcap: IDirect3D9 %p has another CreateDevice body %p (hooked %p); not hooked",
           (void*)d3d, body, s_cdBody);
  }
  return d3d;
}

void d3dcap_install(void) {
  HMODULE self = 0;
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCWSTR)&d3dcap_install, &self) && self) {
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)self;
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)((const char*)self + dos->e_lfanew);
    s_selfBase = (UINT_PTR)self;
    s_selfSize = nt->OptionalHeader.SizeOfImage;
  }
  // D3DDrv9 is loaded by the engine at run time; already here means the device may predate us.
  if (GetModuleHandleW(L"D3DDrv9.dll"))
    np_log("d3dcap: device created before install (D3DDrv9.dll was already loaded)");
  wchar_t path[MAX_PATH];
  const UINT n = GetSystemDirectoryW(path, MAX_PATH);   // SysWOW64 under WOW64 redirection
  if (n == 0 || n + 10 >= MAX_PATH) { np_log("d3dcap: GetSystemDirectoryW failed"); return; }
  wcscat_s(path, MAX_PATH, L"\\d3d9.dll");
  HMODULE d3d9 = LoadLibraryW(path);
  if (!d3d9) { np_log("d3dcap: LoadLibrary d3d9.dll failed (err %lu)", GetLastError()); return; }
  s_d3d9 = d3d9;
  void* create9 = (void*)GetProcAddress(d3d9, "Direct3DCreate9");
  if (!create9) { np_log("d3dcap: d3d9.dll has no Direct3DCreate9"); return; }
  if (!hook_install_live(create9, "Direct3DCreate9", (void*)&create9_detour, (void**)&s_realCreate9)) {
    np_log("d3dcap: Direct3DCreate9 not hooked; no capture this session");
    return;
  }
  np_log("d3dcap: installed on d3d9.dll %p, tid=%lu", (void*)d3d9, GetCurrentThreadId());
}

bool d3dcap_windowed(void) { return InterlockedCompareExchange(&s_windowed, 0, 0) != 0; }

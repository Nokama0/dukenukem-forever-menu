// The dnGame ProcessEvent slot (pehook.h). AActor::ProcessEvent: virtual __thiscall (UFunction*, Parms, Result), `ret
// 0xC`, called as __fastcall (ECX = object). Slot: dnGame.dll's import data (0x10053054, build 244); thunk 0x10047CBA
// is slot 1 of ADukePlayer's vtable 0x10059554. Handlers: game thread; loading thread: only any-thread subscribers.
#include <windows.h>
#include <stdio.h>
#include "pehook.h"
#include "iat.h"
#include "uname.h"
#include "uobj.h"
#include "frame.h"
#include "log.h"

typedef void (__fastcall* ProcessEventFn)(void* self, void* edx, void* func, void* parms, void* result);

static ProcessEventFn s_orig;
static PeSub s_subs[PE_MAX_SUBS];
static PeHandler s_fn[PE_MAX_SUBS];
static const wchar_t* s_names[PE_MAX_SUBS];
static const char* s_who[PE_MAX_SUBS];
static int s_want[PE_MAX_SUBS];   // pehook_enable's value; s_subs[i].on also needs the name resolved
static int s_n;
static volatile LONG s_anyOn;
static int s_tried;
static int s_waitLogged;
static const char* s_refused;
// Counted on the game thread: every event through the slot, and each subscriber's calls; per 600 frames.
static unsigned s_events, s_calls[PE_MAX_SUBS], s_frames;
static volatile LONG s_otherThread, s_otherCalls;

static void __fastcall pe_hook(void* self, void* edx, void* func, void* parms, void* result) {
  (void)edx;
  int name = 0;   // nonzero only on the game thread with a subscriber on: the after phase runs then
  if (s_anyOn && func) {
    // Another thread sees the table as of pehook_frame's InterlockedExchange of s_anyOn (a full barrier after the
    // writes), read here after s_anyOn; aligned ints, so a stale `on` costs one event at most.
    const int game = GetCurrentThreadId() == frame_thread_id();
    if (game) ++s_events;
    else InterlockedIncrement(&s_otherThread);
    // The engine's own UFunction, passed to its own ProcessEvent: its name is read without a guard.
    const int nm = *(const int*)((const char*)func + UO_NAME);
    for (int i = pe_next_call(s_subs, s_n, nm, 0, 0, game); i >= 0; i = pe_next_call(s_subs, s_n, nm, i + 1, 0, game)) {
      if (game) ++s_calls[i];
      else InterlockedIncrement(&s_otherCalls);
      s_fn[i](self, func, parms);
    }
    if (game) name = nm;
  }
  s_orig(self, 0, func, parms, result);
  if (!name) return;
  for (int i = pe_next_phase(s_subs, s_n, name, 0, 1); i >= 0; i = pe_next_phase(s_subs, s_n, name, i + 1, 1)) {
    ++s_calls[i];
    s_fn[i](self, func, parms);
  }
}

static int subscribe(const wchar_t* fnName, PeHandler fn, const char* who, int after, int anyThread) {
  if (s_n >= PE_MAX_SUBS && fnName && fn) {
    static int s_fullLogged;
    if (!s_fullLogged) {
      s_fullLogged = 1;
      np_log("pehook: subscriber table full (%d): %ls for %s not subscribed; later ones neither (not logged again)",
             PE_MAX_SUBS, fnName, who ? who : "?");
    }
  }
  if (s_n >= PE_MAX_SUBS || !fnName || !fn) return -1;
  const int id = s_n;
  s_names[id] = fnName;
  s_fn[id] = fn;
  s_who[id] = who ? who : "?";
  s_subs[id].nameIdx = 0;
  s_subs[id].on = 0;
  s_subs[id].after = after;
  s_subs[id].anyThread = anyThread;
  s_n = id + 1;   // published last: the hook reads s_n
  return id;
}

int pehook_subscribe(const wchar_t* fnName, PeHandler before, const char* who) {
  return subscribe(fnName, before, who, 0, 0);
}

int pehook_subscribe_after(const wchar_t* fnName, PeHandler after, const char* who) {
  return subscribe(fnName, after, who, 1, 0);
}

int pehook_subscribe_any_thread(const wchar_t* fnName, PeHandler before, const char* who) {
  return subscribe(fnName, before, who, 0, 1);
}

void pehook_enable(int id, int on) {
  if (id >= 0 && id < s_n) s_want[id] = on ? 1 : 0;
}

static void install(void) {
  // dnGame.dll can load after our first frame: retry about once a second until it does, then install once.
  static DWORD s_nextTry;
  const DWORD now = GetTickCount();
  if (s_nextTry && (int)(now - s_nextTry) < 0) return;
  const HMODULE dng = GetModuleHandleW(L"dnGame.dll");
  if (!dng) {
    s_nextTry = now + 1000;
    if (!s_waitLogged) { s_waitLogged = 1; np_log("pehook: dnGame.dll is not loaded yet; retrying"); }
    return;
  }
  s_tried = 1;
  if (!iat_patch(dng, "Engine.dll", "?ProcessEvent@AActor@@UAEXPAVUFunction@@PAX1@Z", (void*)&pe_hook,
                 (void**)&s_orig, "pehook")) {
    s_refused = "dnGame.dll's ProcessEvent import slot was not patched";
  } else {
    const void* slot = iat_slot(dng, "Engine.dll", "?ProcessEvent@AActor@@UAEXPAVUFunction@@PAX1@Z");
    np_log("pehook: dnGame.dll %p slot %p (RVA 0x%lx; 0x53054 in build 244)", (void*)dng, slot,
           (unsigned long)((const char*)slot - (const char*)dng));
  }
  if (s_refused) np_log("pehook: not installed: %s", s_refused);
}

void pehook_frame(void) {
  if (!s_tried) install();
  if (s_refused) return;
  int any = 0;
  for (int i = 0; i < s_n; i++) {
    if (s_want[i] && !s_subs[i].nameIdx) {
      s_subs[i].nameIdx = uname_find(s_names[i]);
      if (s_subs[i].nameIdx) np_log("pehook: %s subscribes to %ls (name %d)", s_who[i], s_names[i], s_subs[i].nameIdx);
    }
    s_subs[i].on = s_want[i] && s_subs[i].nameIdx;
    any |= s_subs[i].on;
  }
  InterlockedExchange(&s_anyOn, any);
  if (++s_frames < 600) return;
  s_frames = 0;
  if (!any && !s_events) return;
  char line[2048];   // thirty-two subscribers with long names and 4-digit counts pass 1024
  int len = _snprintf_s(line, sizeof line, _TRUNCATE, "pehook: 600 frames: %u event(s) on the game thread, %ld "
                        "elsewhere (%ld handled)", s_events, InterlockedExchange(&s_otherThread, 0),
                        InterlockedExchange(&s_otherCalls, 0));
  for (int i = 0; i < s_n && len > 0 && len < (int)sizeof line; i++) {
    const int k = _snprintf_s(line + len, sizeof line - len, _TRUNCATE, "; %s %ls %u", s_who[i], s_names[i],
                              s_calls[i]);
    if (k < 0) break;
    len += k;
    s_calls[i] = 0;
  }
  np_log("%s", line);
  s_events = 0;
}

const char* pehook_refused(void) { return s_refused; }

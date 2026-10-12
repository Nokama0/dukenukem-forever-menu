#pragma once
// The dnGame ProcessEvent slot (engine-map.md): dnGame.dll's AActor::ProcessEvent import (slot 0x10053054, thunk
// 0x10047CBA = slot 1 of ADukePlayer's vtable): every engine event on the player. Data slot, no code patched.
// Subscribers match by UFunction name index, run before the native or after it (`after`: out-parameters final).

enum { PE_MAX_SUBS = 32 };
struct PeSub { int nameIdx; int on; int after; int anyThread; };

// The next enabled subscriber at or after `from` for an event named nameIdx, either phase, or -1. NAME_None (0) never
// matches.
inline int pe_next(const PeSub* subs, int n, int nameIdx, int from) {
  if (!nameIdx || !subs) return -1;
  if (n > PE_MAX_SUBS) n = PE_MAX_SUBS;
  for (int i = from < 0 ? 0 : from; i < n; i++)
    if (subs[i].on && subs[i].nameIdx == nameIdx) return i;
  return -1;
}
// The same, in one phase only: after 0 before the engine's call, 1 after it.
inline int pe_next_phase(const PeSub* subs, int n, int nameIdx, int from, int after) {
  for (int i = pe_next(subs, n, nameIdx, from); i >= 0; i = pe_next(subs, n, nameIdx, i + 1))
    if ((subs[i].after != 0) == (after != 0)) return i;
  return -1;
}

// The next subscriber to call for this event, phase and thread. Off the game thread only before-phase subscribers
// that asked for any thread run: the loading thread raises the level-start events (big-package.md section 6).
inline int pe_next_call(const PeSub* subs, int n, int nameIdx, int from, int after, int gameThread) {
  if (!gameThread && after) return -1;
  for (int i = pe_next_phase(subs, n, nameIdx, from, after); i >= 0; i = pe_next_phase(subs, n, nameIdx, i + 1, after))
    if (gameThread || subs[i].anyThread) return i;
  return -1;
}

// Called before (or after) the engine's ProcessEvent; may rewrite Parms. On the game thread, except an any-thread
// subscriber's handler, which also runs on the loading thread.
typedef void (*PeHandler)(void* obj, void* func, void* parms);

// Game thread, at any frame (pehook_frame resolves and enables it on its next pass; the hook sees a new entry only
// once enabled): the handler for events named fnName. -1 when the table is full.
int pehook_subscribe(const wchar_t* fnName, PeHandler before, const char* who);
// The same, called after the engine's ProcessEvent has returned (its out-parameters written).
int pehook_subscribe_after(const wchar_t* fnName, PeHandler after, const char* who);
// A before-phase handler that also runs on other threads (the loading thread). It must touch only what the game itself
// touches on that thread, and read the panel's state only as interlocked scalars.
int pehook_subscribe_any_thread(const wchar_t* fnName, PeHandler before, const char* who);
// Whether a subscriber wants its events this frame (the hook skips disabled ones).
void pehook_enable(int id, int on);
// Game thread, once a frame: installs the slot hook once, resolves names, logs the counts every 600 frames.
void pehook_frame(void);
// Null while installed (or not yet tried); else why the hook is not in place.
const char* pehook_refused(void);

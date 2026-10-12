#pragma once
// Duke's Big Package (docs/re/big-package.md sections 5 and 6): the game's own AChallengeInfo::UnlockPreorderBonuses(2),
// called once at the menu on the game thread, then again before each DukePlayer.PreGameInit, because every map load's
// RescanDLC zeroes the flags first. The rules below are pure; bigpackage.cpp is the engine glue.
#include <stddef.h>
#include <stdint.h>

enum { BP_WAITING, BP_APPLIED, BP_UNAVAILABLE, BP_OFF };
enum { BP_ACT_NONE, BP_ACT_CALL };

// The settle: the engine must stay scriptable for BOTH this many consecutive frames AND this long, so the menu is
// up and RescanDLC has run. Frames alone pass in ~0.1 s on an uncapped menu (1,300 fps), hence the clock floor.
const int BP_SETTLE_FRAMES = 120;
const unsigned BP_SETTLE_MS = 3000;

// scriptableSinceMs: the clock when the current scriptable run began (valid while scriptableFrames > 0).
struct BpGate { int state; int scriptableFrames; bool called; unsigned scriptableSinceMs; };

inline BpGate bp_init(void) { BpGate g = { BP_WAITING, 0, false, 0 }; return g; }

// One frame; nowMs is a millisecond clock (GetTickCount: the unsigned difference survives its wrap). The count runs
// whatever the switch says, so turning it on later acts at once; a level load restarts it. A default object failing its
// checks at the settle point is never called this launch (unknown layout; the game's callers dereference unchecked).
inline int bp_step(BpGate* g, bool on, bool scriptable, bool cdoOk, unsigned nowMs) {
  if (!scriptable) g->scriptableFrames = 0;
  else {
    if (g->scriptableFrames == 0) g->scriptableSinceMs = nowMs;
    if (g->scriptableFrames < BP_SETTLE_FRAMES) g->scriptableFrames++;
  }
  if (g->called || g->state == BP_UNAVAILABLE) return BP_ACT_NONE;
  if (!on) { g->state = BP_OFF; return BP_ACT_NONE; }
  g->state = BP_WAITING;
  if (g->scriptableFrames < BP_SETTLE_FRAMES) return BP_ACT_NONE;
  if (nowMs - g->scriptableSinceMs < BP_SETTLE_MS) return BP_ACT_NONE;
  if (!cdoOk) { g->state = BP_UNAVAILABLE; return BP_ACT_NONE; }
  g->called = true;
  g->state = BP_APPLIED;
  return BP_ACT_CALL;
}

// A VirtualQuery protection that reads without faulting: any readable access, never PAGE_GUARD or PAGE_NOACCESS.
inline bool bp_protect_readable(unsigned long protect) {
  const unsigned long readable = 0x02 | 0x04 | 0x08 | 0x20 | 0x40 | 0x80;   // R, RW, WC, XR, XRW, XWC
  return (protect & readable) != 0 && (protect & (0x100 | 0x01)) == 0;     // not GUARD, not NOACCESS
}

// [p, p+len) lies inside the region [base, base+size), with no wrap.
inline bool bp_span_inside(uintptr_t base, size_t size, uintptr_t p, size_t len) {
  if (p < base || p + len < p) return false;
  return p - base <= size && len <= size - (p - base);
}

// The default object is a ChallengeInfo whose PreorderGroups array is sane (three groups by default; a ceiling of
// eight tolerates a config override while refusing garbage).
inline bool bp_cdo_ok(const void* classField, const void* expected, int groups) {
  return expected && classField == expected && groups >= 1 && groups <= 8;
}

// The default object the PreGameInit re-apply may use: the one the menu call was made on, while the switch is on.
inline const void* bp_armed(bool on, bool called, const void* calledCdo) { return on && called ? calledCdo : 0; }

// DukePlayer.PreGameInit's event UFunction has an Outer that is DukePlayer or a class derived from it (the DLC pawn,
// DukePlayerDLC03, overrides it: big-package.md section 8). Walks US_SUPER up to 8 steps through `read` (a guarded
// read: false on a fault) and returns `dukeClass` when reached, else 0.
typedef bool (*BpReadSuper)(const void* cls, const void** super);
inline const void* bp_match_class(const void* outer, const void* dukeClass, BpReadSuper read) {
  if (!dukeClass) return 0;
  const void* c = outer;
  for (int i = 0; c && i <= 8; i++) {
    if (c == dukeClass) return dukeClass;
    const void* up = 0;
    if (!read(c, &up)) return 0;
    c = up;
  }
  return 0;
}

// Re-apply only when the event's Outer reached DukePlayer (`matchedClass`, 0 if not) and only on the default object
// the menu call checked (GetDefaultObject must still return it).
inline bool bp_reapply_ok(const void* armedCdo, const void* liveCdo, const void* matchedClass) {
  return armedCdo && liveCdo == armedCdo && matchedClass;
}

// The flags line after a re-apply (or a refusal): on the first scriptable frame after g_LoadingOnThread clears, since
// GIsScriptable stays 1 through a map load and the cache is being written on the loading thread until then.
inline bool bp_flags_log_due(bool scriptable, int loading, long re, long loggedRe, long mis, long loggedMis) {
  return scriptable && loading == 0 && (re != loggedRe || mis != loggedMis);
}

// The EXTRAS row's note; none while the switch is off.
inline const char* bp_state_note(int state) {
  switch (state) {
    case BP_APPLIED:     return "Applied";
    case BP_WAITING:     return "Waiting for the menu";
    case BP_UNAVAILABLE: return "Unavailable";
    default:             return NULL;
  }
}

// Engine glue (bigpackage.cpp), game thread only: once per Present, after the panel.
void bigpackage_frame(void);
int  bigpackage_state(void);

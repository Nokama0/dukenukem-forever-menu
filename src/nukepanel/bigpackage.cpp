// Duke's Big Package: the game's own AChallengeInfo::UnlockPreorderBonuses(2) on the ChallengeInfo default object, once
// at the menu (big-package.md 5) and again before each DukePlayer.PreGameInit, since every map load's RescanDLC zeroes
// the flags (section 6). bigpackage_frame runs on the game thread; on_pre_game_init on any loading thread.
#include <windows.h>
#include "bigpackage.h"
#include "engine.h"
#include "config.h"
#include "pehook.h"
#include "frame.h"
#include "uobj.h"
#include "log.h"

// UObject::Class, read by the exported GetClass as [this+0x24] (engine-map.md section 1).
static const size_t CDO_CLASS_OFS = 0x24;
// PreorderGroups is a dnArray {data, Num, Max} at +0x4D0, so Num is +0x4D4 (big-package.md section 2).
static const size_t CDO_GROUPS_NUM_OFS = 0x4D4;
// The span checked readable once per default-object pointer: through PlayerTitles (+0x4E0), past both fields.
static const size_t CDO_READ_SPAN = 0x4E0;
// POG_Gamestop_USA, the first group with bUnlockDBP (big-package.md section 5, point 3).
static const int BP_GROUP = 2;

static BpGate s_gate = { BP_WAITING, 0, false };
static bool s_checked, s_exportsOk;
static void* s_cdoSeen;       // the last default object checked for readability, and the answer
static bool s_cdoReadable;
static void* s_calledCdo;     // the default object the menu call was made on

// Set on the game thread before the subscription is first enabled (pehook_frame's barrier publishes them).
static void* s_ciClass;       // &AChallengeInfo::PrivateStaticClass
static void* s_dukeClass;     // &ADukePlayer::PrivateStaticClass (dnGame.dll export): DukePlayer.PreGameInit's Outer
static int s_sub = -1;
// Shared with the loading thread: interlocked scalars only.
static void* volatile s_armedCdo;
static volatile LONG s_reapplied, s_reappliedOff, s_reMismatch;
static volatile LONG s_loadingAtReapply = -1;   // g_LoadingOnThread at the last re-apply (expected 1)
static void* volatile s_matchedClass;   // the Outer of the last re-applied PreGameInit (logged)
static LONG s_loggedReapplied, s_loggedMismatch;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

// Any thread. RescanDLC has just run UnlockPreorderBonuses' own reset on this same thread (the loading thread on a
// map load), so this repeats the game's write on the thread the game uses for it.
static bool read_super(const void* cls, const void** super) {
  __try {
    *super = *(const void* const*)((const char*)cls + US_SUPER);
    return true;
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

static void on_pre_game_init(void* obj, void* func, void* parms) {
  (void)obj;
  (void)parms;
  // The engine's own UFunction, passed to its own ProcessEvent: read without a guard, as pehook reads its name.
  const void* outer = *(void* const*)((const char*)func + UO_OUTER);
  if (!outer) return;
  // The DLC pawn's override has its own Outer class; accept any class deriving from DukePlayer.
  const void* matched = bp_match_class(outer, s_dukeClass, read_super);
  if (!matched) return;
  void* cdo = InterlockedCompareExchangePointer((void* volatile*)&s_armedCdo, 0, 0);
  if (!cdo) return;
  if (!bp_reapply_ok(cdo, engine_get_default_object(s_ciClass), matched)) {
    InterlockedIncrement(&s_reMismatch);
    return;
  }
  engine_unlock_preorder_bonuses(cdo, BP_GROUP);
  InterlockedExchange(&s_loadingAtReapply, engine_level_loading());
  InterlockedIncrement(&s_reapplied);
  InterlockedExchangePointer((void* volatile*)&s_matchedClass, (void*)outer);
  if (GetCurrentThreadId() != frame_thread_id()) InterlockedIncrement(&s_reappliedOff);
}

// Game thread, never while a level loads: both flags as the game's two readers see them (GetConfigBool, and our
// Progress read's GetInt).
static void log_flags(const char* when) {
  int eb = 0, ei = 0, hb = 0, hi = 0, feb = 0, fei = 0, fhb = 0, fhi = 0, fault = 0;
  __try {
    feb = engine_config_get_bool(L"PreorderBonus", L"BonusEgo", &eb, L"Progress.ini");
    fei = engine_config_get_int(L"PreorderBonus", L"BonusEgo", &ei, L"Progress.ini");
    fhb = engine_config_get_bool(L"PreorderBonus", L"BigHead", &hb, L"Progress.ini");
    fhi = engine_config_get_int(L"PreorderBonus", L"BigHead", &hi, L"Progress.ini");
  } __except (av_only(GetExceptionCode())) {
    fault = 1;
  }
  np_log("bigpackage: %s: [PreorderBonus] BonusEgo bool %d/%d int %d/%d, BigHead bool %d/%d int %d/%d (found/value)%s",
         when, feb, eb, fei, ei, fhb, hb, fhi, hi, fault ? "; the config read faulted" : "");
}

void bigpackage_frame(void) {
  if (!s_checked) {
    s_checked = true;
    const EngineExports* e = engine_exports();
    s_exportsOk = e->gIsScriptable && e->challengeInfoClass && e->getDefaultObject && e->unlockPreorderBonuses;
    if (!s_exportsOk) np_log("bigpackage: unavailable: an Engine.dll export is missing");
    const HMODULE dng = GetModuleHandleW(L"dnGame.dll");
    s_ciClass = e->challengeInfoClass;
    s_dukeClass = dng ? (void*)GetProcAddress(dng, "?PrivateStaticClass@ADukePlayer@@0VUClass@@A") : 0;
    if (s_exportsOk && s_dukeClass) s_sub = pehook_subscribe_any_thread(L"PreGameInit", on_pre_game_init, "bigpackage");
    np_log("bigpackage: DukePlayer class %p; re-apply before its PreGameInit %s", s_dukeClass,
           s_sub >= 0 ? "subscribed" : "UNAVAILABLE (the flags last until the next map load)");
  }
  if (!s_exportsOk) return;

  const EngineExports* e = engine_exports();
  const bool on = g_cfg.bigPackage != 0;
  const bool scriptable = *e->gIsScriptable != 0;
  void* const armed = (void*)bp_armed(on, s_gate.called, s_calledCdo);
  InterlockedExchangePointer((void* volatile*)&s_armedCdo, armed);
  pehook_enable(s_sub, armed != 0);
  // The first frame after the load: what the re-apply left for the player's next reads.
  const LONG re = s_reapplied, mis = s_reMismatch;
  if (bp_flags_log_due(scriptable, engine_level_loading(), re, s_loggedReapplied, mis, s_loggedMismatch)) {
    s_loggedReapplied = re;
    s_loggedMismatch = mis;
    char when[240];
    wsprintfA(when, "re-applied before DukePlayer.PreGameInit %ld time(s), %ld off the game thread, %ld refused; "
              "g_LoadingOnThread at the last re-apply %ld; last Outer class %p (DukePlayer %p)", re, s_reappliedOff, mis,
              s_loadingAtReapply, s_matchedClass, s_dukeClass);
    log_flags(when);
  }
  if (s_gate.called || s_gate.state == BP_UNAVAILABLE) return;   // the menu call: nothing left this launch
  void* cdo = 0;
  const void* cls = 0;
  int groups = -1;
  if (on && scriptable) {
    cdo = engine_get_default_object(e->challengeInfoClass);
    if (cdo != s_cdoSeen) {
      s_cdoSeen = cdo;
      s_cdoReadable = engine_readable(cdo, CDO_READ_SPAN);
    }
    if (cdo && s_cdoReadable) {
      cls = *(void* const*)((const char*)cdo + CDO_CLASS_OFS);
      groups = *(const int*)((const char*)cdo + CDO_GROUPS_NUM_OFS);
    }
  }
  const bool cdoOk = cdo && s_cdoReadable && bp_cdo_ok(cls, e->challengeInfoClass, groups);
  const DWORD nowMs = GetTickCount();
  const bool wasScriptable = s_gate.scriptableFrames > 0;
  const int act = bp_step(&s_gate, on, scriptable, cdoOk, (unsigned)nowMs);
  if (scriptable && !wasScriptable) {
    // Each start of a scriptable run before the call, with the time of day: orders it against the game's own
    // log ("DLC INIT DONE"); the settle clock counts from here.
    SYSTEMTIME st;
    GetLocalTime(&st);
    np_log("bigpackage: scriptable at %lu ms (%02u:%02u:%02u.%03u)", (unsigned long)nowMs, st.wHour, st.wMinute,
           st.wSecond, st.wMilliseconds);
  }
  if (act == BP_ACT_CALL) {
    np_log("bigpackage: cdo=%p groups=%d calling UnlockPreorderBonuses(2)", cdo, groups);
    engine_unlock_preorder_bonuses(cdo, BP_GROUP);
    s_calledCdo = cdo;
    if (!engine_level_loading()) log_flags("applied");
    else np_log("bigpackage: applied (flags not read: a level is loading)");
  } else if (s_gate.state == BP_UNAVAILABLE) {
    np_log("bigpackage: unavailable: cdo=%p readable=%d class=%p (want %p) groups=%d; not called", cdo,
           s_cdoReadable ? 1 : 0, cls, e->challengeInfoClass, groups);
  }
}

int bigpackage_state(void) {
  const bool on = g_cfg.bigPackage != 0;
  if (s_gate.called) return BP_APPLIED;
  if (!on) return BP_OFF;
  if ((s_checked && !s_exportsOk) || s_gate.state == BP_UNAVAILABLE) return BP_UNAVAILABLE;
  return BP_WAITING;
}

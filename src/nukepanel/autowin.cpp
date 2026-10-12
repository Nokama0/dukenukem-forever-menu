// Auto-win (autowin.h; autowin.md). Game thread. What the player uses is Pawn.InteractiveDecoration (set by
// AttachToUsableSomething); pool is a ball hit with Use or a ball or cue carried (Pawn.CarriedActor); Alien Abortion
// starts with Use on its switch, via its map dispatchers. Functions via scall_find, parms by name from the UFunction.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "autowin.h"
#include "world.h"
#include "scall.h"
#include "scallparms.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "config.h"
#include "log.h"
#include "lvnames.h"
#include "mpgate.h"

// UStructProperty::Struct (+0x78, noshake.cpp: UStructProperty::DestroyValue 0x1004ABD0).
static const int PROP_STRUCT = 0x78;

enum { N_INTERACTIVE, N_CARRIED, N_PLAYERUSER, N_USER, N_LASTUSER, N_BLIFT, N_LIFTVAL, N_JUMPPRESSED, N_TABLE,
       N_PLAYERINFO, N_CTRL, N_SCORE, N_HIGHSCORE, N_LEVEL, N_IDLE, N_PLAY, N_BIUSE, N_LOOKHIT, N_TAG, N_DISPATCHING,
       N_INDEX, N_OUTDELAYS, N_CHEATED, N_EGOENABLED, N_COUNT };
static const wchar_t* const NAMES[N_COUNT] = {
  L"InteractiveDecoration", L"CarriedActor", L"PlayerUser", L"User", L"LastUser", L"bLift", L"UserLiftVal",
  L"bIJumpPressed", L"Table", L"PlayerInfo", L"Ctrl", L"Score", L"highscore", L"Level", L"idle", L"Play", L"bIUse",
  L"LookHitActor", L"Tag", L"bDispatching", L"Index", L"OutDelays", L"bHasCheatedThisLevel",
  L"EgoCapAwardsEnabled" };
static int s_n[N_COUNT];

static const wchar_t* const KIND_CLASSES[AW_KIND_COUNT] = { L"dnControl_TapPowered", L"dnControl_Pinball",
                                                            L"dnControl_CoinOp_SlotMachine",
                                                            L"dnControl_AirHockeyPlayer", L"Details_Generic_PoolCue",
                                                            L"Details_Balls_PoolBall", L"dnControl_CoinOp_Poker",
                                                            L"Switches_Generic_PushSwitch_B" };
static const char* const KIND_LABELS[AW_KIND_COUNT] = { "QTE", "pinball", "slots", "air hockey", "pool cue",
                                                        "pool", "video poker", "Alien Abortion" };
static int s_kindName[AW_KIND_COUNT];
static LnKeys s_lnKeys;

// The kinds' class names move across level loads (lvnames.h): looked up again on each new level serial, and kind_of
// retries any still absent. Absent stays 0, never the old index.
static void refresh_kind_names(void) {
  static char buf[AW_KIND_COUNT][64];
  const char* names[AW_KIND_COUNT];
  for (int k = 0; k < AW_KIND_COUNT; k++) {
    _snprintf_s(buf[k], sizeof buf[k], _TRUNCATE, "%ls", KIND_CLASSES[k]);
    names[k] = buf[k];
  }
  static_assert(AW_KIND_COUNT <= LN_MAX, "lvnames.h keeps LN_MAX keys");
  ln_refresh(&s_lnKeys, names, AW_KIND_COUNT, ln_find_ascii, 0);
  memcpy(s_kindName, s_lnKeys.cur, sizeof s_kindName);
  ln_report("autowin", &s_lnKeys, names, AW_KIND_COUNT);
}

// Kinds per class, cleared per level (a class address may be reused by the next level's classes).
struct KindSlot { const void* cls; int kind; };
static KindSlot s_kinds[16];

static unsigned s_level;
static AwMash s_mash;
static unsigned s_mashStartMs;
static int s_mashClassName;
static AwUse s_use;
// Pool, per level: the table found on the first hit (looked up once), the latch, its use, and the probe lines.
static const void* s_poolTable;
static unsigned s_poolTableNextMs;   // the next lookup may run at this tick (a miss retries about once a second)
static int s_poolTableMissLogged;
static const void* s_poolLatch;
static AwUse s_poolUse;
static int s_useWas;
static const void* s_carriedWas;
static int s_poolProbes;
static const int POOL_PROBES = 32;
// Alien Abortion, per level (autowin.md "Alien Abortion"): its dispatchers by Tag (found on the first Start press),
// the game in progress, each pop-up's hit latch, and the game's tallies for its line.
enum { AA_POPUPS = 6, AA_WIN = AA_POPUPS, AA_LOSE, AA_DISPATCHERS };
static const wchar_t* const AA_TAGS[AA_DISPATCHERS] = { L"DisPopUp01", L"DisPopUp02", L"DisPopUp03", L"DisPopUp04",
                                                        L"DisPopUp05", L"DisPopUp06", L"DisExpertScore", L"DisEndGame" };
static const wchar_t* const AA_HITS[AA_POPUPS] = { L"DisPopDown01", L"DisPopDown02", L"DisPopDown03",
                                                   L"DisPopDown04", L"DisPopDown05", L"DisPopDown06" };
static const void* s_aaDisp[AA_DISPATCHERS];
static int s_aaFound = -1;   // dispatchers found this level; -1 not looked for yet
static int s_aaAward;        // this level has the TriggerPlayer tagged AAWinEgoTriggerPlayer (award 29)
static AwDispOfs s_aaOfs;    // Dispatcher's fields, resolved with the dispatchers
static AwAa s_aa;
static int s_aaSpent[AA_POPUPS];
static unsigned s_aaLastMs, s_aaStartMs, s_aaHits, s_aaRefused;

static const char* s_qteWhy;
static const char* s_mgWhy;
static const char* s_gameWhy[AW_KIND_COUNT];
static const char* s_mgNote;

// Counted per 600 frames (and in total): presses sent, QTEs engaged and lifted, minigame wins called, calls the
// engine refused (scall_call 0), and faulted reads.
static unsigned s_frames, s_presses, s_pressesTotal, s_qtes, s_lifts, s_wins, s_winsTotal, s_refusedCalls, s_faults;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static int nm(int k) {
  if (!s_n[k]) s_n[k] = uname_find(NAMES[k]);
  return s_n[k];
}

// p is a live object of this frame's snapshot (p came from the engine: its Index is read).
static bool live(const void* p) {
  if (!p) return false;
  __try {
    return uobj_valid(p);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

static const void* class_of(const void* o) {
  __try {
    return uobj_class(o);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

static int class_name_of(const void* o) {
  __try {
    const void* c = uobj_class(o);
    return c ? uobj_name(c) : 0;
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
}

static UProp prop(const void* owner, int k) {
  UProp none = { 0, 0, UW_OTHER, 0, 0, 0 };
  const int n = nm(k);
  if (!owner || !n) return none;
  __try {
    return uprop_get(owner, n);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return none;
  }
}

// o's property k as an object (validated), int, float or bool; false when it does not resolve or faulted.
static bool rd_obj(const void* o, int k, const void** out) {
  *out = 0;
  const UProp p = prop(class_of(o), k);
  if (!p.ok || p.type != UW_OBJECT) return false;
  __try {
    *out = uw_ptr(o, p.offset);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
  if (*out && !live(*out)) *out = 0;
  return true;
}

static bool rd_int(const void* o, int k, int* out) {
  const UProp p = prop(class_of(o), k);
  if (!p.ok || p.type != UW_INT) return false;
  __try {
    *out = uw_int(o, p.offset);
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

static bool rd_float(const void* o, int k, float* out) {
  const UProp p = prop(class_of(o), k);
  if (!p.ok || p.type != UW_FLOAT) return false;
  __try {
    memcpy(out, (const char*)o + p.offset, 4);
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

// A NameProperty (Actor.Tag): its name index.
static bool rd_name(const void* o, int k, int* out) {
  const UProp p = prop(class_of(o), k);
  if (!p.ok || p.type != UW_OTHER || p.elemSize != 4) return false;
  __try {
    *out = uw_int(o, p.offset);
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

static bool rd_bool(const void* o, int k, int* out) {
  const UProp p = prop(class_of(o), k);
  if (!p.ok || p.type != UW_BOOL || !p.bitmask) return false;
  __try {
    *out = (uw_int(o, p.offset) & (int)p.bitmask) != 0;
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

static int chain_names(const void* cls, int* chain, int cap) {
  int n = 0;
  __try {
    for (const void* c = cls; c && n < cap; c = uw_ptr(c, US_SUPER)) chain[n++] = uobj_name(c);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return -1;
  }
  return n;
}

// o's kind (AW_KIND_*) or -1.
static int kind_of(const void* o) {
  const void* cls = o ? class_of(o) : 0;
  if (!cls) return -1;
  KindSlot& s = s_kinds[((uintptr_t)cls >> 4) & 15];
  if (s.cls == cls) return s.kind;
  bool all = true;
  for (int k = 0; k < AW_KIND_COUNT; k++) {
    if (!s_kindName[k]) s_kindName[k] = uname_find(KIND_CLASSES[k]);
    all = all && s_kindName[k];
  }
  int chain[32];
  const int n = chain_names(cls, chain, 32);
  if (n < 0) return -1;
  const int kind = aw_kind_of_chain(chain, n, s_kindName, AW_KIND_COUNT);
  // A class name not yet in the name table (its package unloaded) may come later: no cached "none" until all exist.
  if (kind >= 0 || all) {
    s.kind = kind;
    s.cls = cls;
  }
  return kind;
}

// GetStateName() on o (Object's native, by ProcessEvent): the state's name index, 0 when not called, -1 when the
// function or its ReturnValue does not resolve.
static int state_name(void* o) {
  const ScallFn f = scall_find(o, L"GetStateName");
  static const void* s_fn;
  static ScallParm s_rv;
  if (!f.ok) return -1;
  if (f.fn != s_fn) {
    ScallParm rv;
    if (!scall_parm(f, L"ReturnValue", L"NameProperty", &rv) || rv.size != 4) return -1;
    s_rv = rv;
    s_fn = f.fn;
  }
  unsigned char parms[64];
  memset(parms, 0, sizeof parms);
  if (f.parmsSize > sizeof parms) return -1;
  if (!scall_call(o, f, parms, sizeof parms)) return 0;
  int n;
  memcpy(&n, parms + s_rv.offset, 4);
  return n;
}

static void refuse_qte(const char* why) {
  if (s_qteWhy) return;
  s_qteWhy = why;
  np_log("autowin: Auto-win QTEs unavailable: %s", why);
}

static void refuse_game(int kind, const char* why) {
  static char s_note[160];
  if (s_gameWhy[kind]) return;
  s_gameWhy[kind] = why;
  _snprintf_s(s_note, sizeof s_note, _TRUNCATE, "No auto-win for %s: %s", KIND_LABELS[kind], why);
  s_mgNote = s_note;
  np_log("autowin: %s skipped: %s", KIND_LABELS[kind], why);
}

// --- QTEs: Jump for the player (autowin.md section 1, lever 1) ----------------------------------------------
static void qte_frame(void* player, const void* ctrl, int kind) {
  const int on = g_cfg.autoQte && !autowin_qte_refused();
  const int tap = on && kind == AW_KIND_TAP;
  int user = 0, bLift = 0;
  float val = 0.f;
  if (tap) {
    const void* pu = 0;
    const unsigned f0 = s_faults;
    if (!rd_obj(ctrl, N_PLAYERUSER, &pu) || !rd_bool(ctrl, N_BLIFT, &bLift) || !rd_float(ctrl, N_LIFTVAL, &val)) {
      if (s_faults != f0) return;   // a fault: counted, tried again next frame; only a miss refuses
      refuse_qte("dnControl_TapPowered's PlayerUser, bLift or UserLiftVal did not resolve; see nukepanel.log");
      return;
    }
    user = pu == player;
  }
  // GetStateName only when it decides a press: the other conditions already hold.
  int idle = 0;
  if (tap && user && bLift && val < 1.f && !(ctrl == s_mash.ctrl && s_mash.lifted)) {
    const int st = state_name((void*)ctrl);
    if (st < 0) {
      refuse_qte("Object.GetStateName did not resolve; see nukepanel.log");
      return;
    }
    idle = st && st == nm(N_IDLE);
    // A press decided by the state alone: logged so a GetStateName that never returns shows (state 0).
    static int s_stateLogs;
    if (!idle && s_stateLogs < 8) {
      s_stateLogs++;
      np_log("autowin: QTE waits on state %d '%ls' (not idle); lift %.2f", st, st ? uname_str(st) : L"", val);
    }
  }
  const AwMash before = s_mash;
  const unsigned now = GetTickCount();
  const AwMashOut o = aw_mash_step(&s_mash, tap ? ctrl : 0, tap, user, idle, bLift, val, on);
  if (o.ev & AW_EV_END) {
    int pending = -1;
    if (!rd_bool(player, N_JUMPPRESSED, &pending)) pending = -1;
    np_log("autowin: QTE %ls ended: %u press(es) over %.2f s, %s; Jump still pending on the player: %d",
           uname_str(s_mashClassName), before.presses, (now - s_mashStartMs) / 1000.0,
           before.lifted ? "lifted" : "not lifted", pending);
  }
  if (o.ev & AW_EV_BEGIN) {
    s_mashStartMs = now;
    s_mashClassName = class_name_of(ctrl);
    ++s_qtes;
    np_log("autowin: QTE %ls (%p) engaged at lift %.2f: pressing Jump each frame", uname_str(s_mashClassName),
           ctrl, val);
  }
  if (o.ev & AW_EV_LIFTED) {
    ++s_lifts;
    np_log("autowin: QTE %ls lifted after %u press(es), %.2f s", uname_str(s_mashClassName), s_mash.presses,
           (now - s_mashStartMs) / 1000.0);
  }
  if (!o.press) return;
  const ScallFn jump = scall_find(player, L"JumpUp");
  if (!jump.ok) {
    refuse_qte("PlayerPawn.JumpUp did not resolve; see nukepanel.log");
    return;
  }
  unsigned char parms[64];
  memset(parms, 0, sizeof parms);
  if (jump.parmsSize > sizeof parms) {
    refuse_qte("PlayerPawn.JumpUp has parameters");
    return;
  }
  if (scall_call(player, jump, parms, sizeof parms)) {
    ++s_presses;
    ++s_pressesTotal;
  } else {
    ++s_refusedCalls;
  }
}

// --- Minigames: the game's own win, once per use (autowin.md section 2) --------------------------------------
// PlayerPawn.bHasCheatedThisLevel for the win lines: while set, NoteDiscoveredEgoAward grants nothing. -1 unresolved.
static int cheated(const void* player) {
  int c = -1;
  if (!rd_bool(player, N_CHEATED, &c)) c = -1;
  return c;
}

// fn(param = v) on obj; param null for none; an IntProperty, or a ByteProperty when byteParm. 1 called, 0 refused by
// scall, -1 not resolved (the game is skipped).
static int call_win(int kind, void* obj, const wchar_t* fn, const wchar_t* param, int v, int byteParm = 0) {
  const ScallFn f = scall_find(obj, fn);
  if (!f.ok) {
    refuse_game(kind, "its win function did not resolve; see nukepanel.log");
    return -1;
  }
  unsigned char parms[256];
  memset(parms, 0, sizeof parms);
  if (f.parmsSize > sizeof parms) {
    refuse_game(kind, "its win function's parameters are larger than expected");
    return -1;
  }
  if (param) {
    ScallParm p;
    const unsigned char b = (unsigned char)v;
    const unsigned size = byteParm ? 1u : 4u;
    if (!scall_parm(f, param, byteParm ? L"ByteProperty" : L"IntProperty", &p) || p.size != size ||
        !scallparms_put(parms, f.parmsSize, p.offset, byteParm ? (const void*)&b : (const void*)&v, size)) {
      refuse_game(kind, "its win function's parameter did not resolve; see nukepanel.log");
      return -1;
    }
  }
  return scall_call(obj, f, parms, sizeof parms);
}

// Air hockey: the player's slot in AirHockeyTable_NativeBase.PlayerInfo[2] (SPlayer.Ctrl), -1 when not seated, -2
// when the layout does not resolve.
static int hockey_index(const void* table, const void* ctrl) {
  const UProp pi = prop(class_of(table), N_PLAYERINFO);
  if (!pi.ok || pi.type != UW_STRUCT || !pi.field || pi.elemSize <= 0) return -2;
  const void* strct = 0;
  int dim = 0;
  __try {
    dim = *(const unsigned short*)((const char*)pi.field + UP_ARRAYDIM);
    strct = uw_ptr(pi.field, PROP_STRUCT);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return -2;
  }
  if (dim < 2 || !live(strct)) return -2;
  const UProp c = prop(strct, N_CTRL);
  if (!c.ok || c.type != UW_OBJECT || c.offset < 0 || c.offset + 4 > pi.elemSize) return -2;
  const void *c0 = 0, *c1 = 0;
  __try {
    c0 = uw_ptr(table, pi.offset + c.offset);
    c1 = uw_ptr(table, pi.offset + pi.elemSize + c.offset);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return -2;
  }
  return aw_hockey_index(c0, c1, ctrl);
}

// Pool: a Furniture_Generic_PoolTable_A of the player's level (TriggerFunc_WinEgoCapIncrease grants to
// Level.GetLocalPlayerPawn(), so any table of this level will do). Two object-table passes, once per level.
static const void* find_pool_table(const void* player) {
  static const wchar_t* const kName[1] = { L"Furniture_Generic_PoolTable_A" };
  const void* cls = 0;
  if (!uobj_find_classes(kName, &cls, 1) || !cls) return 0;
  const void* level = 0;
  if (!rd_obj(player, N_LEVEL, &level) || !level) return 0;
  const UProp lp = prop(cls, N_LEVEL);
  if (!lp.ok || lp.type != UW_OBJECT) return 0;
  const int n = uobj_count();
  __try {
    for (int i = 0; i < n; i++) {
      const void* o = uobj_at(i);
      if (o && uobj_class(o) == cls && uw_ptr(o, lp.offset) == level) return o;
    }
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
  }
  return 0;
}

static void minigame_frame(void* player, const void* ctrl, int kind) {
  const int on = g_cfg.autoMinigames && !autowin_minigames_refused();
  const void* use = 0;
  int useKind = -1;
  // Pinball is left to Pinball autoplay while it is on (pinbot.h): no AwardPoints then.
  if ((kind == AW_KIND_PINBALL && aw_pinball_awards(on, g_cfg.pinballAuto)) ||
      (on && (kind == AW_KIND_SLOTS || kind == AW_KIND_HOCKEY || kind == AW_KIND_POKER))) {
    use = ctrl;
    useKind = kind;
  }
  if (useKind >= 0 && s_gameWhy[useKind]) use = 0;   // that game's call did not resolve
  if (use != s_use.obj && use)
    np_log("autowin: %s: the player uses %ls (%p)", KIND_LABELS[useKind], uname_str(class_name_of(use)), use);
  // Readiness is read only while a win is still owed for this use.
  int ready = 0, hockeyIdx = -1;
  const void* table = 0;
  if (use && !(use == s_use.obj && s_use.won)) {
    const void* who = 0;
    switch (useKind) {
      case AW_KIND_PINBALL:   // AwardPoints grants to DukePlayer(User)
        ready = rd_obj(use, N_USER, &who) && who == player;
        break;
      case AW_KIND_SLOTS:     // Win grants to LastUser, set by PressStartButton
        ready = rd_obj(use, N_LASTUSER, &who) && who == player;
        break;
      case AW_KIND_POKER:     // the grant goes to the player itself: ready once it uses the machine
        ready = 1;
        break;
      case AW_KIND_HOCKEY: {
        const int st = state_name((void*)use);
        if (st < 0) {
          s_mgWhy = "Object.GetStateName did not resolve; see nukepanel.log";
          np_log("autowin: Auto-win minigames unavailable: %s", s_mgWhy);
          return;
        }
        if (st && st == nm(N_PLAY) && rd_obj(use, N_TABLE, &table) && table) {
          hockeyIdx = hockey_index(table, use);
          if (hockeyIdx == -2) {
            refuse_game(AW_KIND_HOCKEY, "AirHockeyTable's PlayerInfo did not resolve; see nukepanel.log");
            return;
          }
          ready = hockeyIdx >= 0;
        }
        break;
      }
      default:
        break;
    }
  }
  if (!aw_use_step(&s_use, use, ready, on)) return;
  int r = -1, v = 0;
  const char* fn = "";
  switch (useKind) {
    case AW_KIND_PINBALL: {
      int score = 0, high = 0;
      if (!rd_int(use, N_SCORE, &score) || !rd_int(use, N_HIGHSCORE, &high)) {
        refuse_game(AW_KIND_PINBALL, "dnControl_Pinball's Score or highscore did not resolve; see nukepanel.log");
        return;
      }
      v = aw_pinball_points(score, high);
      fn = "AwardPoints";
      r = call_win(useKind, (void*)use, L"AwardPoints", L"Points", v);
      break;
    }
    case AW_KIND_SLOTS:
      v = AW_SLOTS_AMOUNT;
      fn = "Win";
      r = call_win(useKind, (void*)use, L"Win", L"amount", v);
      break;
    case AW_KIND_HOCKEY:
      v = hockeyIdx;
      fn = "GameWon";
      r = call_win(useKind, (void*)table, L"GameWon", L"PlayerIndex", v);
      break;
    case AW_KIND_POKER:   // VideoPoker.UncoverCallback's grant on a paying hand: no deterministic win exists
      v = 1;
      fn = "GivePermanentEgoCapAward";
      r = call_win(useKind, player, L"GivePermanentEgoCapAward", L"AwardID", v, 1);
      break;
    default:
      return;
  }
  if (r > 0) {
    ++s_wins;
    ++s_winsTotal;
  } else if (r == 0) {
    ++s_refusedCalls;
  }
  int enabled = -1;   // DukePlayer.EgoCapAwardsEnabled: while clear the native grants nothing
  if (!rd_bool(player, N_EGOENABLED, &enabled)) enabled = -1;
  np_log("autowin: %s won on use (%s): %s(%d) on %p -> %s; EgoCapAwardsEnabled %d, cheated this level %d",
         KIND_LABELS[useKind], aw_path_name(aw_path_of(useKind)), fn, v,
         useKind == AW_KIND_HOCKEY ? table : useKind == AW_KIND_POKER ? (const void*)player : use,
         r > 0 ? "called" : r == 0 ? "refused by scall" : "not resolved", enabled, cheated(player));
}

static const wchar_t* class_str(const void* o) { return o ? uname_str(class_name_of(o)) : L"None"; }

// Pool (autowin.h aw_pool_hit): the first ball hit in a level latches the level's table and wins once. Use presses
// and carried objects are logged (capped per level), so a gate shows what the player did even if the edge is wrong.
// lookKind: the looked-at actor's kind while Use is held (aw_aa_kind applied: a push switch only with the game's Tag).
static void pool_frame(void* player, int useDown, const void* look, int lookKind, const void* carried) {
  if (s_gameWhy[AW_KIND_POOLBALL]) return;
  const int carriedKind = carried ? kind_of(carried) : -1;
  if (useDown && !s_useWas && s_poolProbes < POOL_PROBES) {
    s_poolProbes++;
    np_log("autowin: pool probe: Use pressed at %ls (%s), carrying %ls", class_str(look),
           lookKind >= 0 ? KIND_LABELS[lookKind] : "no minigame", class_str(carried));
  }
  if (carried != s_carriedWas && s_poolProbes < POOL_PROBES) {
    s_poolProbes++;
    np_log("autowin: pool probe: carrying %ls", class_str(carried));
  }
  s_useWas = useDown;
  s_carriedWas = carried;
  const int hit = aw_pool_hit(useDown, lookKind, carriedKind);
  if (hit && !s_poolTable && (int)(GetTickCount() - s_poolTableNextMs) >= 0) {
    s_poolTableNextMs = GetTickCount() + 1000;
    s_poolTable = find_pool_table(player);
    if (s_poolTable || !s_poolTableMissLogged) {
      s_poolTableMissLogged = !s_poolTable;
      np_log("autowin: pool: hit on %ls; %s", class_str(useDown && lookKind == AW_KIND_POOLBALL ? look : carried),
             s_poolTable ? "the level's pool table found" : "no pool table yet (retrying)");
    }
  }
  s_poolLatch = aw_pool_latch(s_poolLatch, hit, s_poolTable);
  if (!aw_use_step(&s_poolUse, s_poolLatch, s_poolLatch && live(s_poolLatch), 1)) return;
  const int r = call_win(AW_KIND_POOLBALL, (void*)s_poolLatch, L"TriggerFunc_WinEgoCapIncrease", 0, 0);
  if (r > 0) {
    ++s_wins;
    ++s_winsTotal;
  } else if (r == 0) {
    ++s_refusedCalls;
  }
  np_log("autowin: pool won on a hit: TriggerFunc_WinEgoCapIncrease on %p -> %s (once this level)", s_poolLatch,
         r > 0 ? "called" : r == 0 ? "refused by scall" : "not resolved");
}

// --- Alien Abortion: the game played by its own hit (autowin.md "Alien Abortion") --------------------------------
// The level's Dispatchers by Tag (AA_TAGS) into s_aaDisp, Dispatcher's field offsets into s_aaOfs, and whether a
// TriggerPlayer tagged AAWinEgoTriggerPlayer is here; one object-table pass. Returns how many dispatchers were found.
static int find_aa_dispatchers(const void* player) {
  memset(s_aaDisp, 0, sizeof s_aaDisp);
  memset(&s_aaOfs, 0, sizeof s_aaOfs);
  s_aaAward = 0;
  int tags[AA_DISPATCHERS];
  for (int k = 0; k < AA_DISPATCHERS; k++) tags[k] = uname_find(AA_TAGS[k]);
  const int awardTag = uname_find(L"AAWinEgoTriggerPlayer");
  static const wchar_t* const kNames[2] = { L"Dispatcher", L"TriggerPlayer" };
  const void* cls[2] = { 0, 0 };
  if (!uobj_find_classes(kNames, cls, 2) || !cls[0]) return 0;
  const UProp dp = prop(cls[0], N_DISPATCHING), ip = prop(cls[0], N_INDEX), op = prop(cls[0], N_OUTDELAYS);
  if (dp.ok && dp.type == UW_BOOL && ip.ok && ip.type == UW_INT && op.ok && op.type == UW_ARRAY) {
    const AwDispOfs o = { dp.offset, dp.bitmask, ip.offset, op.offset };
    s_aaOfs = o;
  }
  if (!aw_disp_ofs_ok(s_aaOfs)) {
    refuse_game(AW_KIND_AASWITCH, "Dispatcher's bDispatching, Index or OutDelays did not resolve; see nukepanel.log");
    return 0;
  }
  const void* level = 0;
  if (!rd_obj(player, N_LEVEL, &level) || !level) return 0;
  const UProp lp = prop(cls[0], N_LEVEL), tp = prop(cls[0], N_TAG);
  if (!lp.ok || lp.type != UW_OBJECT || !tp.ok || tp.type != UW_OTHER || tp.elemSize != 4) return 0;
  // TriggerPlayer's Level and Tag are Actor's, so at the same offsets: checked, not assumed.
  int tpOk = 0;
  if (cls[1]) {
    const UProp l2 = prop(cls[1], N_LEVEL), t2 = prop(cls[1], N_TAG);
    tpOk = l2.ok && t2.ok && l2.offset == lp.offset && t2.offset == tp.offset;
  }
  int found = 0;
  const int n = uobj_count();
  __try {
    for (int i = 0; i < n; i++) {
      const void* o = uobj_at(i);
      if (!o) continue;
      const void* c = uobj_class(o);
      if ((c != cls[0] && !(tpOk && c == cls[1])) || uw_ptr(o, lp.offset) != level) continue;
      const int tag = uw_int(o, tp.offset);
      if (c != cls[0]) {
        s_aaAward |= tag && tag == awardTag;
        continue;
      }
      for (int k = 0; tag && k < AA_DISPATCHERS; k++)
        if (tag == tags[k] && !s_aaDisp[k]) {
          s_aaDisp[k] = o;
          ++found;
        }
    }
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
  }
  return found;
}

// All dispatchers' state in one guarded pass (not a guarded read per object per frame). One that left
// the object table reads as idle; a fault idles them all this frame.
static void read_aa_dispatchers(AwDisp out[AA_DISPATCHERS]) {
  memset(out, 0, sizeof(AwDisp) * AA_DISPATCHERS);
  if (!aw_disp_ofs_ok(s_aaOfs)) return;
  __try {
    for (int k = 0; k < AA_DISPATCHERS; k++)
      if (s_aaDisp[k] && uobj_valid(s_aaDisp[k])) out[k] = aw_disp_read(s_aaDisp[k], s_aaOfs);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    memset(out, 0, sizeof(AwDisp) * AA_DISPATCHERS);
  }
}

// The hit Trigger___35 makes when the player Uses a looked-at alien PopUp0N: GlobalTrigger('DisPopDown0N',
// Other.Instigator, Other) (Engine Trigger.TriggerTarget), here with the player as both. 1 called, 0 refused by
// scall, -1 not resolved.
static int aa_hit(void* player, int slot) {
  const int ev = uname_find(AA_HITS[slot]);
  const ScallFn f = scall_find(player, L"GlobalTrigger");
  if (!ev || !f.ok) return -1;
  unsigned char parms[64];
  memset(parms, 0, sizeof parms);
  ScallParm pe, pi, po;
  if (f.parmsSize > sizeof parms || !scall_parm(f, L"TriggerEvent", L"NameProperty", &pe) || pe.size != 4 ||
      !scall_parm(f, L"ArgInstigator", L"ObjectProperty", &pi) || pi.size != sizeof player ||
      !scall_parm(f, L"Other", L"ObjectProperty", &po) || po.size != sizeof player ||
      !scallparms_put(parms, f.parmsSize, pe.offset, &ev, 4) ||
      !scallparms_put(parms, f.parmsSize, pi.offset, &player, sizeof player) ||
      !scallparms_put(parms, f.parmsSize, po.offset, &player, sizeof player))
    return -1;
  return scall_call(player, f, parms, sizeof parms);
}

// press: Use held on the game's Start switch (sw). Reads the dispatchers only while a game is live or starting.
static void aa_frame(void* player, int press, const void* sw) {
  const unsigned now = GetTickCount();
  const unsigned dt = s_aaLastMs ? now - s_aaLastMs : 0;
  s_aaLastMs = now;
  AwDisp d[AA_DISPATCHERS];
  memset(d, 0, sizeof d);
  int anyUp = 0;
  if (s_aa.live || press) {
    if (s_aaFound < 0) s_aaFound = find_aa_dispatchers(player);
    if (s_gameWhy[AW_KIND_AASWITCH]) return;   // Dispatcher's fields did not resolve: refused once
    read_aa_dispatchers(d);
    for (int k = 0; k < AA_POPUPS; k++) anyUp |= d[k].on;
  }
  const unsigned ev = aw_aa_step(&s_aa, press, anyUp, d[AA_WIN].on, d[AA_LOSE].on, dt);
  if (ev & AW_AA_BEGIN) {
    s_aaStartMs = now;
    s_aaHits = s_aaRefused = 0;
    memset(s_aaSpent, 0, sizeof s_aaSpent);
    int pops = 0;
    for (int k = 0; k < AA_POPUPS; k++) pops += s_aaDisp[k] != 0;
    np_log("autowin: Alien Abortion started at %ls (%p): %s (GlobalTrigger DisPopDown0N once DisPopUp0N waits on its "
           "last event); pop-ups found %d/6, DisExpertScore %s, DisEndGame %s, award 29 TriggerPlayer %s",
           class_str(sw), sw, aw_path_name(AW_PATH_PLAY), pops, s_aaDisp[AA_WIN] ? "found" : "missing",
           s_aaDisp[AA_LOSE] ? "found" : "missing", s_aaAward ? "here" : "not on this map");
  }
  if (ev & (AW_AA_WON | AW_AA_LOST | AW_AA_IDLE)) {
    if (ev & AW_AA_WON) {
      ++s_wins;
      ++s_winsTotal;
    }
    np_log("autowin: Alien Abortion game over (%s): %s; %u hit(s), %u refused, %.1f s; cheated this level %d",
           aw_path_name(AW_PATH_PLAY), aw_aa_result(ev, s_aaAward), s_aaHits, s_aaRefused, (now - s_aaStartMs) / 1000.0, cheated(player));
  }
  if (!s_aa.live) return;
  for (int k = 0; k < AA_POPUPS; k++) {
    if (!aw_whack_step(&s_aaSpent[k], d[k].on, d[k].index, d[k].count)) continue;
    const int r = aa_hit(player, k);
    if (r > 0) {
      ++s_aaHits;
    } else if (r == 0) {
      ++s_aaRefused;
      ++s_refusedCalls;
    } else {
      refuse_game(AW_KIND_AASWITCH, "Actor.GlobalTrigger or a DisPopDown0N name did not resolve; see nukepanel.log");
      return;
    }
  }
}

// The Use inputs pool and Alien Abortion share: Use held, the looked-at actor, the carried one.
static void use_frame(void* player) {
  int useDown = 0;
  const void *look = 0, *carried = 0;
  const unsigned f0 = s_faults;
  if (!rd_bool(player, N_BIUSE, &useDown) || !rd_obj(player, N_LOOKHIT, &look) ||
      !rd_obj(player, N_CARRIED, &carried)) {
    if (s_faults == f0) {   // a fault: tried again next frame; only a miss refuses
      refuse_game(AW_KIND_POOLBALL, "PlayerPawn.bIUse, LookHitActor or Pawn.CarriedActor did not resolve; see "
                                    "nukepanel.log");
      refuse_game(AW_KIND_AASWITCH, "PlayerPawn.bIUse or LookHitActor did not resolve; see nukepanel.log");
    }
    return;
  }
  int lookKind = useDown && look ? kind_of(look) : -1;
  if (lookKind == AW_KIND_AASWITCH) {
    int tag = 0;
    if (!rd_name(look, N_TAG, &tag)) tag = 0;
    lookKind = aw_aa_kind(lookKind, tag, uname_find(L"SwitchAlienAbortReset01"));
  }
  pool_frame(player, useDown, look, lookKind, carried);
  if (!s_gameWhy[AW_KIND_AASWITCH])
    aa_frame(player, lookKind == AW_KIND_AASWITCH, lookKind == AW_KIND_AASWITCH ? look : 0);
}

void autowin_frame(int inLevel) {
  const unsigned level = world_level_serial();
  if (level != s_level) {
    // Pointers of the last level are stale: forget them without reading them.
    if (s_mash.presses)
      np_log("autowin: QTE %ls dropped with the level after %u press(es)", uname_str(s_mashClassName),
             s_mash.presses);
    s_level = level;
    memset(&s_mash, 0, sizeof s_mash);
    memset(&s_use, 0, sizeof s_use);
    memset(s_kinds, 0, sizeof s_kinds);
    refresh_kind_names();
    s_poolTable = 0;
    s_poolTableNextMs = 0;
    s_poolTableMissLogged = 0;
    s_poolLatch = 0;
    memset(&s_poolUse, 0, sizeof s_poolUse);
    s_useWas = 0;
    s_carriedWas = 0;
    s_poolProbes = 0;
    if (s_aa.live)
      np_log("autowin: Alien Abortion game dropped with the level after %u hit(s), %u refused", s_aaHits, s_aaRefused);
    memset(s_aaDisp, 0, sizeof s_aaDisp);
    s_aaFound = -1;
    s_aaAward = 0;
    memset(&s_aa, 0, sizeof s_aa);
    memset(s_aaSpent, 0, sizeof s_aaSpent);
    s_aaLastMs = 0;
  }
  void* player = world_player();
  // Paused (the pause menu), a menu or a load: no reads and no calls; the engagements hold.
  if (inLevel && player && (g_cfg.autoQte || g_cfg.autoMinigames || s_mash.presses || s_use.obj)) {
    // The functions both rows need, checked once a player exists so a missing one shows before any QTE.
    if (g_cfg.autoQte && !s_qteWhy && !scall_find(player, L"JumpUp").ok)
      refuse_qte("PlayerPawn.JumpUp did not resolve; see nukepanel.log");
    const void* ctrl = 0;
    const unsigned f0 = s_faults;
    if (!rd_obj(player, N_INTERACTIVE, &ctrl)) {
      if (s_faults != f0) return;   // a fault: this frame skipped, tried again next frame; only a miss refuses
      if (!s_qteWhy) refuse_qte("Pawn.InteractiveDecoration did not resolve; see nukepanel.log");
      if (!s_mgWhy) {
        s_mgWhy = "Pawn.InteractiveDecoration did not resolve; see nukepanel.log";
        np_log("autowin: Auto-win minigames unavailable: %s", s_mgWhy);
      }
    } else {
      const int kind = ctrl ? kind_of(ctrl) : -1;
      qte_frame(player, ctrl, kind);
      minigame_frame(player, ctrl, kind);
      if (g_cfg.autoMinigames && !autowin_minigames_refused()) use_frame(player);
    }
  }
  uname_dlog_flush();   // uprop's lines, outside every guard
  if (++s_frames < 600) return;
  s_frames = 0;
  if (g_cfg.autoQte || g_cfg.autoMinigames || s_presses || s_wins)
    np_log("autowin: 600 frames: QTEs %s, %u press(es) (total %u), %u engaged, %u lifted; minigames %s, %u win(s) "
           "(total %u); %u call(s) refused, %u fault(s)", g_cfg.autoQte ? (s_qteWhy ? "unavailable" : "on") : "off",
           s_presses, s_pressesTotal, s_qtes, s_lifts,
           g_cfg.autoMinigames ? (s_mgWhy ? "unavailable" : "on") : "off", s_wins, s_winsTotal, s_refusedCalls,
           s_faults);
  s_presses = s_qtes = s_lifts = s_wins = s_refusedCalls = s_faults = 0;
}

const char* autowin_qte_refused(void) { return s_qteWhy ? s_qteWhy : mp_refused(); }
const char* autowin_minigames_refused(void) { return s_mgWhy ? s_mgWhy : mp_refused(); }
const char* autowin_minigames_note(void) { return s_mgNote; }

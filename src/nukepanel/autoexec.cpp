// Auto-execute (autoexec.h; executions.md sections 1-5). Game thread only. Candidates are world.cpp's walked pawns;
// each hostile one in reach is read by name (Pawn.FrozenState, AIActor.AllowFrozenExecution, ControlXParms, its state
// via GetStateName). Calls go via scall: CanPressUse and FastTrace on the player, the victim's CanBeUsedBy and Used.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "autoexec.h"
#include "world.h"
#include "scall.h"
#include "scallparms.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "config.h"
#include "log.h"
#include "mpgate.h"

// UStructProperty::Struct (+0x78, noshake.cpp: UStructProperty::DestroyValue 0x1004ABD0).
static const int PROP_STRUCT = 0x78;
// CanUseOp_Die (dnAI.dll!0x1001B7C0) refuses the finish-me stun while [LevelInfo+0x7BC] bit 0 is set; its name was
// not resolved (executions.md section 1). Read once per level for the log only, with the LevelInfo bool property
// declared at that offset, so the name can be settled.
static const int LI_DIEOP_OFS = 0x7BC;

enum { N_USEDIST, N_GRABBING, N_ATTACHED, N_VIEWMAPPER, N_INTERACTIVE, N_HEALTH, N_LOCATION, N_RADIUS, N_HEIGHT,
       N_LEVEL, N_FROZEN, N_ALLOWFROZEN, N_CXPARMS, N_MYCONTROL, N_ISCONTROLLED, N_CONTROLX, N_CXTIME,
       N_WASEXECUTED, N_USER, N_EGO, N_COUNT };
static const wchar_t* const NAMES[N_COUNT] = {
  L"UseDistance", L"bGrabbing", L"bAttachedToUsableSomething", L"ViewMapper", L"InteractiveDecoration", L"Health",
  L"Location", L"CollisionRadius", L"CollisionHeight", L"Level", L"FrozenState", L"AllowFrozenExecution",
  L"ControlXParms", L"MyControl", L"IsControlled", L"ControlX", L"Time", L"WasExecuted", L"User", L"Ego" };
// From N_CXTIME on, the names feed the log only: a miss there refuses nothing.
static const int N_REQUIRED = N_CXTIME;
static int s_n[N_COUNT];

static const char* s_why;
static unsigned s_level;
static int s_checked;          // this level's resolve check ran (once a player exists)
static int s_levelLogged;      // this level's UseDistance and LevelInfo +0x7BC line
static const void* s_execCls;  // dnControl_Execution, found once per level

static AexLatch s_latch;
// The engagement the latch holds, for its result line.
struct Engagement { const void* victim; int clsName, kind, wasExec, checked; float dist, egoBefore; unsigned startMs; };
static Engagement s_eng;

// Per level: victims whose stun or freeze was logged on first sight, and gate refusals logged per (victim, reason).
static const void* s_seen[16];
static int s_seenN;
static const void* s_gateVictim;
static int s_gateReason, s_gateLogs, s_refuseLogs;
static const int LOGS_PER_LEVEL = 32;

// Counted per 600 frames (and executions in total): Used calls, CanBeUsedBy false, a blocked trace, CanPressUse
// false, frames a gate held an executable enemy back, executions confirmed (WasExecuted), calls scall refused, faults.
static unsigned s_frames, s_calls, s_callsTotal, s_cbuFalse, s_blocked, s_cantUse, s_gated, s_executed,
    s_executedTotal, s_refusedCalls, s_faults;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static int nm(int k) {
  if (!s_n[k]) s_n[k] = uname_find(NAMES[k]);
  return s_n[k];
}

static void refuse(const char* why) {
  if (s_why) return;
  s_why = why;
  np_log("autoexec: Auto-execute unavailable: %s", why);
}

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

static const wchar_t* class_str(const void* o) { return o ? uname_str(class_name_of(o)) : L"None"; }

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

// o's property k as an object (validated), float, bool, byte or vector; false when it does not resolve or faulted
// (a fault counts in s_faults, so a caller tells the two apart).
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

static bool rd_byte(const void* o, int k, int* out) {
  const UProp p = prop(class_of(o), k);
  if (!p.ok || p.type != UW_BYTE) return false;
  __try {
    *out = *((const unsigned char*)o + p.offset);
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

static bool rd_vec(const void* o, int k, float* out3) {
  const UProp p = prop(class_of(o), k);
  if (!p.ok || p.type != UW_STRUCT || p.elemSize < 12) return false;
  __try {
    memcpy(out3, (const char*)o + p.offset, 12);
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

static bool rd_alive(const void* o, int* out) {
  const UProp p = prop(class_of(o), N_HEALTH);
  if (!p.ok || (p.type != UW_FLOAT && p.type != UW_INT)) return false;
  __try {
    *out = uw_health_alive(o, p.offset, p.type);
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

static bool class_isa(const void* cls, const void* target) {
  if (!cls || !target) return false;
  __try {
    return uobj_class_isa(cls, target);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
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

// ControlXParms.MyControl, .IsControlled and .Time through the SControlX struct's own properties. 1 read, 0 the
// layout did not resolve, -1 a fault.
static int rd_cx(const void* o, const void** myControl, int* isControlled, float* time) {
  const UProp pi = prop(class_of(o), N_CXPARMS);
  if (!pi.ok || pi.type != UW_STRUCT || !pi.field || pi.elemSize <= 0) return 0;
  const void* strct = 0;
  __try {
    strct = uw_ptr(pi.field, PROP_STRUCT);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return -1;
  }
  if (!live(strct)) return 0;
  const UProp mc = prop(strct, N_MYCONTROL), ic = prop(strct, N_ISCONTROLLED), tm = prop(strct, N_CXTIME);
  if (!mc.ok || mc.type != UW_OBJECT || mc.offset < 0 || mc.offset + 4 > pi.elemSize) return 0;
  if (!ic.ok || ic.type != UW_BOOL || !ic.bitmask || ic.offset < 0 || ic.offset + 4 > pi.elemSize) return 0;
  *time = -1.f;
  __try {
    *myControl = uw_ptr(o, pi.offset + mc.offset);
    *isControlled = (uw_int(o, pi.offset + ic.offset) & (int)ic.bitmask) != 0;
    if (tm.ok && tm.type == UW_FLOAT && tm.offset >= 0 && tm.offset + 4 <= pi.elemSize)
      memcpy(time, (const char*)o + pi.offset + tm.offset, 4);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return -1;
  }
  if (*myControl && !live(*myControl)) *myControl = 0;
  return 1;
}

// --- The calls ---------------------------------------------------------------------------------------------------
// PlayerPawn.CanPressUse(): 1 true, 0 false or refused by scall, -1 it does not resolve.
static int can_press_use(void* player) {
  const ScallFn f = scall_find(player, L"CanPressUse");
  static const void* s_fn;
  static ScallParm s_rv;
  if (!f.ok) return -1;
  if (f.fn != s_fn) {
    ScallParm rv;
    if (!scall_parm(f, L"ReturnValue", L"BoolProperty", &rv) || rv.size != 4) return -1;
    s_rv = rv;
    s_fn = f.fn;
  }
  unsigned char parms[64];
  memset(parms, 0, sizeof parms);
  if (f.parmsSize > sizeof parms) return -1;
  if (!scall_call(player, f, parms, sizeof parms)) {
    ++s_refusedCalls;
    return 0;
  }
  int r;
  memcpy(&r, parms + s_rv.offset, 4);
  return r != 0;
}

// Actor.FastTrace(TraceEnd, TraceStart) on the player, world geometry only (bCheckActors left false): 1 clear, 0
// blocked or refused by scall, -1 it does not resolve.
static int fast_trace(void* player, const float end[3], const float start[3]) {
  const ScallFn f = scall_find(player, L"FastTrace");
  static const void* s_fn;
  static ScallParm s_end, s_start, s_rv;
  if (!f.ok) return -1;
  if (f.fn != s_fn) {
    ScallParm e, s, rv;
    if (!scall_parm(f, L"TraceEnd", L"StructProperty", &e) || e.size != 12 ||
        !scall_parm(f, L"TraceStart", L"StructProperty", &s) || s.size != 12 ||
        !scall_parm(f, L"ReturnValue", L"BoolProperty", &rv) || rv.size != 4)
      return -1;
    s_end = e;
    s_start = s;
    s_rv = rv;
    s_fn = f.fn;
  }
  unsigned char parms[64];
  memset(parms, 0, sizeof parms);
  if (f.parmsSize > sizeof parms || !scallparms_put(parms, f.parmsSize, s_end.offset, end, 12) ||
      !scallparms_put(parms, f.parmsSize, s_start.offset, start, 12))
    return -1;
  if (!scall_call(player, f, parms, sizeof parms)) {
    ++s_refusedCalls;
    return 0;
  }
  int r;
  memcpy(&r, parms + s_rv.offset, 4);
  return r != 0;
}

// fn as the victim's current state resolves it (scall_find_state), with each of its nObj ObjectProperty parameters the
// player and its bool ReturnValue into *rv when set. Names are not used: the overrides rename them (executions.md
// section 2). 1 called, 0 refused by scall, -1 the function or its parameters did not match.
static int call_use(void* victim, const wchar_t* fn, void* player, int nObj, int* rv) {
  const ScallFn f = scall_find_state(victim, fn);
  if (!f.ok) return -1;
  ScallParm objs[2], ret;
  if (nObj > 2 || scall_parms_of(f, L"ObjectProperty", objs, 2) != nObj) return -1;
  if (rv && (!scall_parm(f, L"ReturnValue", L"BoolProperty", &ret) || ret.size != 4)) return -1;
  unsigned char parms[64];
  memset(parms, 0, sizeof parms);
  if (f.parmsSize > sizeof parms) return -1;
  for (int i = 0; i < nObj; i++)
    if (objs[i].size != 4 || !scallparms_put(parms, f.parmsSize, objs[i].offset, &player, 4)) return -1;
  if (!scall_call(victim, f, parms, sizeof parms)) {
    ++s_refusedCalls;
    return 0;
  }
  if (rv) {
    int r;
    memcpy(&r, parms + ret.offset, 4);
    *rv = r != 0;
  }
  return 1;
}

// --- The player, the victims -------------------------------------------------------------------------------------
struct PlayerRd { float loc[3], radius, height, useDist, ego; int alive, grabbing, attached, viewMapper, control; };

// 1 read, 0 a property did not resolve (refused), -1 a fault (tried again next frame).
static int read_player(const void* player, PlayerRd* r) {
  const unsigned f0 = s_faults;
  const void *vm = 0, *ctrl = 0;
  if (!rd_vec(player, N_LOCATION, r->loc) || !rd_float(player, N_RADIUS, &r->radius) ||
      !rd_float(player, N_HEIGHT, &r->height) || !rd_float(player, N_USEDIST, &r->useDist) ||
      !rd_alive(player, &r->alive) || !rd_bool(player, N_GRABBING, &r->grabbing) ||
      !rd_bool(player, N_ATTACHED, &r->attached) || !rd_obj(player, N_VIEWMAPPER, &vm) ||
      !rd_obj(player, N_INTERACTIVE, &ctrl)) {
    if (s_faults != f0) return -1;
    refuse("a player property (UseDistance, bGrabbing, bAttachedToUsableSomething, ViewMapper, "
           "InteractiveDecoration, Health, Location or the collision size) did not resolve; see nukepanel.log");
    return 0;
  }
  r->viewMapper = vm != 0;
  r->control = ctrl != 0;
  r->ego = -1.f;
  rd_float(player, N_EGO, &r->ego);   // the log only
  return 1;
}

// 1 when v's first-sight line was already logged this level, or the list is full (quiet).
static int seen_before(const void* v) {
  for (int i = 0; i < s_seenN; i++)
    if (s_seen[i] == v) return 1;
  if (s_seenN >= 16) return 1;
  s_seen[s_seenN++] = v;
  return 0;
}

// The candidate's kind (AEX_*), or -1 after a refusal, -2 on a fault (skipped this frame). Frozen is read first; the
// state (a ProcessEvent) only for a pawn that is not.
static int victim_kind(void* o, int alive, float dist) {
  const unsigned f0 = s_faults;
  int frozen = 0, allow = 0;
  if (!rd_byte(o, N_FROZEN, &frozen)) {
    if (s_faults != f0) return -2;
    refuse("Pawn.FrozenState did not resolve; see nukepanel.log");
    return -1;
  }
  const void* cls = class_of(o);
  const UProp af = prop(cls, N_ALLOWFROZEN), cx = prop(cls, N_CXPARMS);
  if (s_faults != f0) return -2;
  if (!af.ok && !cx.ok) return AEX_NONE;   // not an AIActor: no execution
  if (!rd_bool(o, N_ALLOWFROZEN, &allow) || !cx.ok) {
    if (s_faults != f0) return -2;
    refuse("AIActor.AllowFrozenExecution or ControlXParms did not resolve; see nukepanel.log");
    return -1;
  }
  if (frozen) {
    const int k = aex_kind(0, 0, 0, frozen, allow, alive);
    if (!seen_before(o))
      np_log("autoexec: %ls frozen at %.0f units: AllowFrozenExecution %d, alive %d -> %s", class_str(o), dist,
             allow, alive, k == AEX_FROZEN ? "executable" : "not executable");
    return k;
  }
  const int st = state_name(o);
  if (st < 0) {
    refuse("Object.GetStateName did not resolve; see nukepanel.log");
    return -1;
  }
  if (!st || st != nm(N_CONTROLX)) return AEX_NONE;
  const void* myControl = 0;
  int controlled = 0;
  float time = -1.f;
  const int cr = rd_cx(o, &myControl, &controlled, &time);
  if (cr < 0) return -2;
  if (cr == 0) {
    refuse("AIActor.ControlXParms's MyControl or IsControlled did not resolve; see nukepanel.log");
    return -1;
  }
  const int isExec = myControl && class_isa(class_of(myControl), s_execCls);
  const int k = aex_kind(1, isExec, controlled, frozen, allow, alive);
  if (!seen_before(o))
    np_log("autoexec: %ls in ControlX at %.0f units: Time %.1f, MyControl %ls (%s), IsControlled %d -> %s",
           class_str(o), dist, time, class_str(myControl), isExec ? "an execution" : "not an execution",
           controlled, k == AEX_STUN ? "executable" : "not executable");
  return k;
}

// The latched victim is still in the walk (or the walk stopped early and it is still a live object).
static int in_walk(const void* v) {
  if (!live(v)) return 0;
  const WPawn* pw = 0;
  const int n = world_pawns(&pw);
  for (int i = 0; i < n; i++)
    if (pw[i].obj == v) return 1;
  return !world_pawns_complete();
}

static const char* const KIND_NAMES[3] = { "none", "stunned", "frozen" };
static const char* const RELEASE_NAMES[5] = { "", "", "left the walk", "left ControlX or thawed", "timed out" };

// One execution at a time: the latched victim's state each frame, the next-frame check of the attach, and the
// result line when the latch lets go.
static void latch_frame(void* player, const PlayerRd& pr, unsigned now) {
  void* v = (void*)s_latch.victim;
  if (!v) return;
  const int present = in_walk(v);
  int down = 0;
  if (present) {
    int frozen = 0, wasExec = 0;
    rd_byte(v, N_FROZEN, &frozen);
    if (rd_bool(v, N_WASEXECUTED, &wasExec)) s_eng.wasExec = wasExec;
    const int st = frozen ? 0 : state_name(v);
    down = frozen || (st > 0 && st == nm(N_CONTROLX));
  }
  if (!s_eng.checked) {
    s_eng.checked = 1;
    const void *ctrl = 0, *user = 0, *myControl = 0;
    int controlled = 0;
    float time = 0.f;
    rd_obj(player, N_INTERACTIVE, &ctrl);
    if (present && s_eng.kind == AEX_STUN && rd_cx(v, &myControl, &controlled, &time) > 0 && myControl)
      rd_obj(myControl, N_USER, &user);
    np_log("autoexec: next frame: player attached %d, InteractiveDecoration %ls; %s%s", pr.attached,
           class_str(ctrl), s_eng.kind == AEX_STUN ? "MyControl.User is the player: " : "frozen victim",
           s_eng.kind == AEX_STUN ? (user == player ? "yes" : "no") : "");
  }
  const int r = aex_latch_step(&s_latch, present, down, now);
  if (r < AEX_L_GONE) return;
  if (s_eng.wasExec) {
    ++s_executed;
    ++s_executedTotal;
  }
  np_log("autoexec: %ls (%s) %s after %.2f s: WasExecuted %d, ego %.0f -> %.0f", uname_str(s_eng.clsName),
         KIND_NAMES[s_eng.kind], RELEASE_NAMES[r], (now - s_eng.startMs) / 1000.0, s_eng.wasExec, s_eng.egoBefore,
         pr.ego);
}

// A gate holding an executable enemy back: counted each frame, logged when the (victim, reason) pair changes.
static void note_gate(const void* v, int reason) {
  ++s_gated;
  if (v == s_gateVictim && reason == s_gateReason) return;
  s_gateVictim = v;
  s_gateReason = reason;
  if (s_gateLogs >= LOGS_PER_LEVEL) return;
  s_gateLogs++;
  np_log("autoexec: %ls executable in reach, held back: %s", class_str(v), aex_gate_name(reason));
}

static void note_refusal(const void* v, const char* what, float dist, unsigned now) {
  aex_wait(&s_latch, now);
  if (s_refuseLogs >= LOGS_PER_LEVEL) return;
  s_refuseLogs++;
  np_log("autoexec: %ls at %.0f units: %s; nothing called", class_str(v), dist, what);
}

// The resolve check, once per level once a player exists: the names, the player's functions and the control class.
static bool check(void* player) {
  for (int k = 0; k < N_REQUIRED; k++) {
    if (nm(k)) continue;
    refuse("a property or state name is missing from the name table; see nukepanel.log");
    np_log("autoexec: no name %ls", NAMES[k]);
    return false;
  }
  if (!scall_find(player, L"CanPressUse").ok || !scall_find(player, L"FastTrace").ok) {
    refuse("PlayerPawn.CanPressUse or Actor.FastTrace did not resolve; see nukepanel.log");
    return false;
  }
  static const wchar_t* const kExec[1] = { L"dnControl_Execution" };
  const void* cls = 0;
  if (!uobj_find_classes(kExec, &cls, 1) || !cls) {
    refuse("the class dnControl_Execution was not found; see nukepanel.log");
    return false;
  }
  s_execCls = cls;
  return true;
}

// The LevelInfo bool property declared at byte `ofs` with bit mask 1, walking the class's super chain; its name, or 0.
static int bool_prop_at(const void* cls, int ofs) {
  const int boolName = uname_find(L"BoolProperty");
  if (!boolName) return 0;
  __try {
    int d = 0;
    for (const void* c = cls; c && d < 16; c = uw_ptr(c, US_SUPER), d++) {
      int n = 0;
      for (const void* f = uw_ptr(c, US_CHILDREN); f && n < UW_FIELD_CAP; f = uw_ptr(f, UF_NEXT), n++) {
        if (!uobj_valid(f)) break;
        if (uobj_name(uobj_class(f)) != boolName || uw_int(f, UP_OFFSET) != ofs) continue;
        if (*(const unsigned*)((const char*)f + UBP_BITMASK) == 1) return uobj_name(f);
      }
    }
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
  }
  return 0;
}

// Once per level (switch on or off): UseDistance as read by name, and the LevelInfo bit that refuses the stun.
static void log_level(void* player) {
  s_levelLogged = 1;
  float ud = -1.f;
  const UProp up = prop(class_of(player), N_USEDIST);
  rd_float(player, N_USEDIST, &ud);
  const void* level = 0;
  rd_obj(player, N_LEVEL, &level);
  unsigned bits = 0;
  int ok = 0, name = 0;
  if (level) {
    __try {
      bits = (unsigned)uw_int(level, LI_DIEOP_OFS);
      ok = 1;
    } __except (av_only(GetExceptionCode())) {
      ++s_faults;
    }
    name = bool_prop_at(class_of(level), LI_DIEOP_OFS);
  }
  np_log("autoexec: level: Pawn.UseDistance %.1f at +0x%x (the engine's LookHitActor reach reads +0xB18); "
         "LevelInfo +0x7BC %s0x%08x, bit 0 = %d, the bool there: %ls", ud, up.ok ? (unsigned)up.offset : 0u,
         ok ? "" : "unreadable ", bits, (int)(bits & 1), name ? uname_str(name) : L"none found");
}

static void new_level(unsigned level) {
  // Pointers of the last level are stale: forgotten without being read.
  if (s_latch.victim) np_log("autoexec: %ls dropped with the level", uname_str(s_eng.clsName));
  s_level = level;
  memset(&s_latch, 0, sizeof s_latch);
  memset(&s_eng, 0, sizeof s_eng);
  s_checked = 0;
  s_levelLogged = 0;
  s_execCls = 0;
  s_seenN = 0;
  s_gateVictim = 0;
  s_gateReason = 0;
  s_gateLogs = 0;
  s_refuseLogs = 0;
}

static void step(void* player, int panelOpen) {
  const unsigned now = GetTickCount();
  if (!s_checked) {
    s_checked = 1;
    if (!check(player)) return;
  }
  if (!s_execCls) return;
  PlayerRd pr;
  if (read_player(player, &pr) <= 0) return;
  latch_frame(player, pr, now);
  if (s_latch.victim || (s_latch.waiting && (int)(now - s_latch.waitUntil) < 0)) return;
  // canPressUse is a ProcessEvent: assumed here, asked only once a victim is chosen.
  const AexGate g = { 1, panelOpen, 1, pr.alive, pr.grabbing, pr.attached, pr.viewMapper, pr.control, 1 };
  const int gate = aex_gate(g);
  const WPawn* pw = 0;
  const int n = world_pawns(&pw);
  AexCand cand[16];
  int nc = 0;
  for (int i = 0; i < n && nc < 16; i++) {
    const WPawn& w = pw[i];
    if (!w.obj || w.obj == player || !aex_hostile(w.attitude, w.targetsPlayer, w.bossMeter)) continue;
    const float dx = w.x - pr.loc[0], dy = w.y - pr.loc[1], dz = w.z - pr.loc[2];
    if (!aex_in_reach(dx, dy, dz, pr.useDist, pr.radius, w.radius, pr.height, w.height)) continue;
    const float d2 = dx * dx + dy * dy + dz * dz;
    const int k = victim_kind((void*)w.obj, w.alive, sqrtf(d2));
    if (k == -1) return;   // refused
    if (k < 0) continue;
    const AexCand c = { w.obj, d2, 1, k, 1 };
    cand[nc++] = c;
  }
  const int pick = aex_pick(cand, nc);
  if (pick < 0) return;
  void* v = (void*)cand[pick].obj;
  const float dist = sqrtf(cand[pick].dist2);
  if (gate != AEX_GO) {
    note_gate(v, gate);
    return;
  }
  if (aex_choose(&s_latch, gate, cand, nc, now) != pick) return;
  const int cpu = can_press_use(player);
  if (cpu < 0) {
    refuse("PlayerPawn.CanPressUse did not resolve; see nukepanel.log");
    return;
  }
  if (!cpu) {
    ++s_cantUse;
    note_gate(v, AEX_G_CANTUSE);
    aex_wait(&s_latch, now);
    return;
  }
  const WView eye = world_view();
  float vloc[3] = { 0.f, 0.f, 0.f };
  if (!eye.ok || !rd_vec(v, N_LOCATION, vloc)) return;
  const float start[3] = { eye.x, eye.y, eye.z };
  const int ft = fast_trace(player, vloc, start);
  if (ft < 0) {
    refuse("Actor.FastTrace's TraceEnd, TraceStart or ReturnValue did not resolve; see nukepanel.log");
    return;
  }
  if (!ft) {
    ++s_blocked;
    note_refusal(v, "the view to it is blocked (FastTrace)", dist, now);
    return;
  }
  int can = 0;
  const int cb = call_use(v, L"CanBeUsedBy", player, 1, &can);
  if (cb < 0) {
    refuse("CanBeUsedBy (one pawn, a bool return) did not resolve on the enemy; see nukepanel.log");
    return;
  }
  if (cb == 0) {
    aex_wait(&s_latch, now);
    return;
  }
  if (!can) {
    ++s_cbuFalse;
    note_refusal(v, "CanBeUsedBy(player) false", dist, now);
    return;
  }
  const int used = call_use(v, L"Used", player, 2, 0);
  if (used < 0) {
    refuse("Used (two actors) did not resolve on the enemy; see nukepanel.log");
    return;
  }
  if (used == 0) {
    aex_wait(&s_latch, now);
    return;
  }
  aex_latch_set(&s_latch, v, now);
  ++s_calls;
  ++s_callsTotal;
  s_eng.victim = v;
  s_eng.clsName = class_name_of(v);
  s_eng.kind = cand[pick].kind;
  s_eng.wasExec = 0;
  s_eng.checked = 0;
  s_eng.dist = dist;
  s_eng.egoBefore = pr.ego;
  s_eng.startMs = now;
  np_log("autoexec: executing %ls (%s) at %.0f units (UseDistance %.0f): CanBeUsedBy true, Used(player, player) "
         "called; ego %.0f", uname_str(s_eng.clsName), KIND_NAMES[s_eng.kind], dist, pr.useDist, pr.ego);
}

void autoexec_frame(int inLevel, int panelOpen) {
  const unsigned level = world_level_serial();
  if (level != s_level) new_level(level);
  void* player = world_player();
  if (inLevel && player && !s_levelLogged) log_level(player);
  if (!g_cfg.autoExecute || autoexec_refused()) {
    if (s_latch.victim) memset(&s_latch, 0, sizeof s_latch);   // switched off: nothing held
  } else if (inLevel && player) {
    step(player, panelOpen);
  }
  uname_dlog_flush();   // uprop's lines, outside every guard
  if (++s_frames < 600) return;
  s_frames = 0;
  if (g_cfg.autoExecute || s_calls || s_executed)
    np_log("autoexec: 600 frames: %s, %u Used call(s) (total %u), %u executed (total %u); CanBeUsedBy false %u, "
           "trace blocked %u, CanPressUse false %u, gated %u frame(s); %u call(s) refused, %u fault(s)",
           g_cfg.autoExecute ? (s_why ? "unavailable" : "on") : "off", s_calls, s_callsTotal, s_executed,
           s_executedTotal, s_cbuFalse, s_blocked, s_cantUse, s_gated, s_refusedCalls, s_faults);
  s_calls = s_executed = s_cbuFalse = s_blocked = s_cantUse = s_gated = s_refusedCalls = s_faults = 0;
}

const char* autoexec_refused(void) { return s_why ? s_why : mp_refused(); }

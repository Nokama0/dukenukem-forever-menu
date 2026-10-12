// Fire rate and reload speed (handling.md 5-7). Once a frame on the game thread: the held weapon's animation
// controllers and state, then UpdateGlobalAnimRateScale on each whose scale differs from the wanted one, ClearLockout
// when due, and 1.0 back on changed controllers no longer held. Nothing else is written; both calls go by ProcessEvent.
#include <windows.h>
#include <string.h>
#include "handling.h"
#include "scall.h"
#include "scallparms.h"
#include "engine.h"
#include "world.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "config.h"
#include "log.h"
#include "mpgate.h"

enum { N_WEAPON, N_CONTROLLER, N_STATE, N_LOCKOUT, N_WEAPONCONFIG, N_LOCKTIME, N_SCALE, N_LEVEL, N_TIME, N_SECOND,
       N_DUAL, N_SHOTS, N_HEALTH, N_MESHINST, N_FIRESTART, N_LOOPING, N_FIREHELD, N_COUNT,
       N_REQUIRED = N_SECOND };
static const wchar_t* const NAMES[N_COUNT] = { L"Weapon", L"AnimationController", L"WeaponState",
                                               L"bWeaponFireLockout", L"WeaponConfig", L"LockoutFireTime",
                                               L"AnimRateScale", L"Level", L"TimeSeconds", L"SecondPistol",
                                               L"DualPistol", L"AccuracyShots", L"Health", L"MeshInstance",
                                               L"bHasFireStart", L"bHasLoopingFire", L"bIFireServer" };
static int s_n[N_COUNT];

static const char* s_refused;
static bool s_checked;
static unsigned s_level;

// A controller we changed: pointer and object index (a reused address is another object), and our value.
struct Rec { void* ctrl; int index; float ours, lastWant; int stuck; int clsName; };
enum { REC_MAX = 8, STUCK_MAX = 3 };
static Rec s_rec[REC_MAX];
static int s_recN;

// The UpdateGlobalAnimRateScale function and its float parameter, per UFunction.
static ScallFn s_upd;
static ScallParm s_updParm;

// The lockout lever, per held weapon; its measurement (rise time, prev flag) beside it.
static const void* s_lockWeapon;
static HdLock s_lock;
static int s_lkPrev;
static float s_lkT0;
static const void* s_cdoCls;
static float s_lockTime = -1.f;
static int s_lockEager;   // hd_lock_eager of the config class in s_cdoCls
static float s_lastClearT = -1.f;

// GetAnimRate (UMeshInstance, Engine.dll export 0x1030DF40): __thiscall (BYTE channel, const dnName* group),
// `ret 8`, the rate in ST0; null group reads the channel itself (read 2026-10-08). Measurement only.
typedef float (__fastcall* GetAnimRateFn)(void* self, void* edx, unsigned char chan, const int* group);
static GetAnimRateFn s_getRate;
static bool s_getRateTried;

// Measurement: the state-change lines (once per weapon class, state and stock/scaled), the call verdicts (once per
// class, state and value), the shot-interval buckets (once per class and fire %).
struct Seen { int clsName, state, extra; };
enum { SEEN_MAX = 128 };
static Seen s_seen[SEEN_MAX];
static int s_seenN;
struct Pending { int on; const void* mesh; int clsName, state; float oldScale, newScale; float before[4]; };
static Pending s_pend;
struct Bucket { int clsName, firePct, n, lockN, lockOurs, lockEarly; float sum, min, lockSum, lockMin, lockMax; };
static Bucket s_bk;
struct Stock { int clsName; float min; };
enum { STOCK_MAX = 32 };
static Stock s_stock[STOCK_MAX];
static int s_stockN;
static float s_lastShotT;
static int s_lastShots = -1;
static const void* s_shotWeapon;
static int s_prevState = -1;
static const void* s_prevWeapon;
static bool s_firstHeldLogged;

// The fire trace (hd_trace_due): the last state seen, fire held (PlayerPawn.bIFireServer, -1 unreadable), and our
// counters at the last line, so each line says what we did since the one before.
static const void* s_trWeapon;
static int s_trState = -1, s_trHeld = -1, s_trN;
static float s_trT;
static unsigned s_trCalls, s_trClears;

// The 600-frame line.
static unsigned s_frames, s_calls, s_refusedCalls, s_restores, s_adopted, s_stuckSkips, s_clears, s_clearRefused,
    s_lockouts, s_faults;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static bool obj_ok(const void* p) {
  __try {
    return p && uobj_valid(p);
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

static int obj_index(const void* p) {
  __try {
    return uw_int(p, UO_INDEX);
  } __except (av_only(GetExceptionCode())) {
    return -1;
  }
}

static int class_name(const void* o) {
  __try {
    return o && uobj_valid(o) ? uobj_name(uobj_class(o)) : 0;
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
}

static UProp prop(const void* o, int k) {
  UProp none = { 0, 0, UW_OTHER, 0, 0, 0 };
  if (!o || !s_n[k]) return none;
  return uprop_get(uobj_class(o), s_n[k]);
}

// Inside a guard: the object (or class) property k of o, validated.
static void* rd_obj(const void* o, int k) {
  const UProp p = prop(o, k);
  if (!p.ok || (p.type != UW_OBJECT && p.type != UW_CLASS)) return 0;
  void* v = (void*)uw_ptr(o, p.offset);
  return v && uobj_valid(v) ? v : 0;
}
static int rd_float(const void* o, int k, float* out) {
  const UProp p = prop(o, k);
  if (!p.ok || p.type != UW_FLOAT || p.elemSize != 4) return 0;
  memcpy(out, (const char*)o + p.offset, 4);
  return 1;
}

// o's class derives from the class named nameIdx (by name along the super chain).
static bool isa_name(const void* o, int nameIdx) {
  if (!nameIdx) return false;
  const void* c = uobj_class(o);
  for (int i = 0; c && i < 64; i++, c = uw_ptr(c, US_SUPER))
    if (uobj_name(c) == nameIdx) return true;
  return false;
}

// One frame's reading of the player's weapon.
struct View {
  void* weapon; int clsName; void* ctrl[2]; float scale[2]; int nCtrl;
  int state, lock, haveLock; const void* cfg; float now; int haveNow; int shots; int alive; void* mesh; int held;
};

static bool read_view(const void* player, View* v) {
  memset(v, 0, sizeof *v);
  v->state = -1;
  v->shots = -1;
  v->alive = 1;
  v->held = -1;
  __try {
    const void* level = rd_obj(player, N_LEVEL);
    if (level) v->haveNow = rd_float(level, N_TIME, &v->now);
    float hp = 1.f;
    if (s_n[N_HEALTH] && rd_float(player, N_HEALTH, &hp)) v->alive = hp > 0.f;
    if (s_n[N_SHOTS]) {
      const UProp sp = prop(player, N_SHOTS);
      if (sp.ok && sp.type == UW_INT) v->shots = uw_int(player, sp.offset);
    }
    const UProp fh = prop(player, N_FIREHELD);
    if (fh.ok && fh.type == UW_BOOL && fh.bitmask) v->held = (uw_int(player, fh.offset) & (int)fh.bitmask) != 0;
    void* w = rd_obj(player, N_WEAPON);
    if (!w || !v->alive) return true;
    v->weapon = w;
    v->clsName = uobj_name(uobj_class(w));
    const UProp st = prop(w, N_STATE);
    if (st.ok && st.type == UW_BYTE) v->state = *((const unsigned char*)w + st.offset);
    const UProp lk = prop(w, N_LOCKOUT);
    if (lk.ok && lk.type == UW_BOOL && lk.bitmask) {
      v->haveLock = 1;
      v->lock = (uw_int(w, lk.offset) & (int)lk.bitmask) != 0;
    }
    v->cfg = rd_obj(w, N_WEAPONCONFIG);
    if (s_n[N_MESHINST]) v->mesh = rd_obj(w, N_MESHINST);
    void* c = rd_obj(w, N_CONTROLLER);
    if (c && rd_float(c, N_SCALE, &v->scale[0])) v->ctrl[v->nCtrl++] = c;
    // The dual pistols' second gun has its own animation controller: DualPistol.SetWeaponAnimState.
    if (s_n[N_SECOND] && isa_name(w, s_n[N_DUAL])) {
      const void* second = rd_obj(w, N_SECOND);
      void* c2 = second ? rd_obj(second, N_CONTROLLER) : 0;
      if (c2 && c2 != c && rd_float(c2, N_SCALE, &v->scale[v->nCtrl])) v->ctrl[v->nCtrl++] = c2;
    }
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

static int read_scale(const void* ctrl, float* out) {
  __try {
    return uobj_valid(ctrl) && rd_float(ctrl, N_SCALE, out);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

static int read_lock(const void* w) {
  __try {
    const UProp lk = prop(w, N_LOCKOUT);
    return lk.ok && lk.type == UW_BOOL && (uw_int(w, lk.offset) & (int)lk.bitmask) != 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

static int seen_claim(int clsName, int state, int extra);

// A bool of the config class default; -1 when it does not read. Inside a guard.
static int cdo_bool(const void* def, const void* cfg, int k) {
  if (!def || !s_n[k]) return -1;
  const UProp p = uprop_get(cfg, s_n[k]);
  if (!p.ok || p.type != UW_BOOL || !p.bitmask) return -1;
  return (uw_int(def, p.offset) & (int)p.bitmask) != 0;
}

// WeaponConfig.default.LockoutFireTime, as WpnFire reads it, and whether our clear is eager; once per config class.
static float lock_time(const void* cfg) {
  if (cfg == s_cdoCls) return s_lockTime;
  s_cdoCls = cfg;
  s_lockTime = -1.f;
  s_lockEager = 0;
  int start = -1, loop = -1, cfgName = 0;
  __try {
    const void* def = cfg ? engine_get_default_object((void*)cfg) : 0;
    const UProp p = def && s_n[N_LOCKTIME] ? uprop_get(cfg, s_n[N_LOCKTIME]) : UProp{ 0, 0, UW_OTHER, 0, 0, 0 };
    if (p.ok && p.type == UW_FLOAT && p.elemSize == 4) memcpy(&s_lockTime, (const char*)def + p.offset, 4);
    start = cdo_bool(def, cfg, N_FIRESTART);
    loop = cdo_bool(def, cfg, N_LOOPING);
    cfgName = cfg ? uobj_name(cfg) : 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
  }
  s_lockEager = hd_lock_eager(start, loop);
  if (cfgName && s_lockTime > 0.f && seen_claim(cfgName, 300, 0))
    np_log("handling: %ls: LockoutFireTime %.3f s, bHasFireStart %d, bHasLoopingFire %d: above 100 %% our ClearLockout "
           "comes %s", uname_str(cfgName), s_lockTime, start, loop,
           s_lockEager ? "on the frame the lockout is seen" : "after LockoutFireTime / factor");
  return s_lockTime;
}

// Channels 0-3 of the weapon's mesh instance; 0 when the export or the mesh is missing.
static int read_rates(const void* mesh, float* out) {
  for (int i = 0; i < 4; i++) out[i] = -1.f;
  if (!s_getRateTried) {
    s_getRateTried = true;
    const HMODULE eng = GetModuleHandleW(L"Engine.dll");
    s_getRate = eng ? (GetAnimRateFn)GetProcAddress(eng, "?GetAnimRate@UMeshInstance@@QAEMEPBVdnName@@@Z") : 0;
    if (!s_getRate) np_log("handling: UMeshInstance::GetAnimRate is not exported; the rate lines read -1");
  }
  if (!s_getRate || !obj_ok(mesh)) return 0;
  __try {
    for (int i = 0; i < 4; i++) out[i] = s_getRate((void*)mesh, 0, (unsigned char)i, 0);
    return 1;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

static int seen_claim(int clsName, int state, int extra) {
  for (int i = 0; i < s_seenN; i++)
    if (s_seen[i].clsName == clsName && s_seen[i].state == state && s_seen[i].extra == extra) return 0;
  if (s_seenN >= SEEN_MAX) return 0;
  const Seen e = { clsName, state, extra };
  s_seen[s_seenN++] = e;
  return 1;
}

static const char* check(void) {
  for (int k = 0; k < N_COUNT; k++) s_n[k] = uname_find(NAMES[k]);
  for (int k = 0; k < N_REQUIRED; k++)
    if (!s_n[k]) return "a weapon or animation property the rows need is not in the game; see nukepanel.log";
  const EngineExports* e = engine_exports();
  if (!e->findFunction || !e->processEvent) return "FindFunction or ProcessEvent is not exported";
  return 0;
}

static void refuse(const char* why) {
  static const char* s_logged;
  if (s_refused) return;
  s_refused = why;
  if (why == s_logged) return;   // the same refusal again on a later level
  s_logged = why;
  np_log("handling: Fire rate and Reload speed unavailable: %s; nothing is called", why);
}

// UpdateGlobalAnimRateScale(f) on ctrl by ProcessEvent. 1 when called.
static int set_scale(void* ctrl, float f) {
  const ScallFn fn = scall_find(ctrl, L"UpdateGlobalAnimRateScale");
  if (!fn.ok) {
    refuse("AnimationControllerEx.UpdateGlobalAnimRateScale did not resolve");
    return 0;
  }
  if (fn.fn != s_upd.fn) {
    ScallParm p[2];
    if (scall_parms_of(fn, L"FloatProperty", p, 2) != 1 || p[0].size != 4 || fn.parmsSize > 16) {
      refuse("UpdateGlobalAnimRateScale's parameters are not one float");
      return 0;
    }
    s_upd = fn;
    s_updParm = p[0];
    np_log("handling: UpdateGlobalAnimRateScale fn=%p ParmsSize=%u, its float at +%u", fn.fn,
           (unsigned)fn.parmsSize, s_updParm.offset);
  }
  unsigned char parms[16] = { 0 };
  if (!scallparms_put(parms, s_upd.parmsSize, s_updParm.offset, &f, 4)) return 0;
  if (!scall_call(ctrl, s_upd, parms, sizeof parms)) {
    ++s_refusedCalls;
    return 0;
  }
  ++s_calls;
  return 1;
}

static Rec* rec_find(const void* ctrl) {
  for (int i = 0; i < s_recN; i++)
    if (s_rec[i].ctrl == ctrl) return &s_rec[i];
  return 0;
}

static Rec* rec_add(void* ctrl, int clsName, float ours) {
  Rec* r = rec_find(ctrl);
  if (!r) {
    if (s_recN >= REC_MAX) return 0;
    r = &s_rec[s_recN++];
    r->ctrl = ctrl;
    r->index = obj_index(ctrl);
    r->lastWant = -1.f;
    r->stuck = 0;
  }
  r->clsName = clsName;
  r->ours = ours;
  return r;
}

// 1.0 back on each controller we changed that is no longer held (a switch, death, the weapon gone); records go.
static void release_unheld(const View& v) {
  int kept = 0;
  for (int i = 0; i < s_recN; i++) {
    Rec& r = s_rec[i];
    const bool held = r.ctrl == v.ctrl[0] || r.ctrl == v.ctrl[1];
    if (held) {
      s_rec[kept++] = r;
      continue;
    }
    float live = 1.f;
    const int valid = obj_ok(r.ctrl) && obj_index(r.ctrl) == r.index && read_scale(r.ctrl, &live);
    if (hd_restore_due(valid, 0, live, r.ours) && set_scale(r.ctrl, 1.f)) ++s_restores;
  }
  s_recN = kept;
}

static void log_state(const View& v) {
  if (v.weapon == s_prevWeapon && v.state == s_prevState) return;
  s_prevWeapon = v.weapon;
  s_prevState = v.state;
  if (!v.weapon || !v.nCtrl || !seen_claim(v.clsName, v.state, v.scale[0] == 1.f ? 0 : 1)) return;
  float r[4];
  read_rates(v.mesh, r);
  char rel[16];
  weap_reload_text(g_cfg.reloadPct, rel, sizeof rel);
  np_log("handling: %ls state %d: AnimRateScale %.3f, channel rates %.3f %.3f %.3f %.3f (GetAnimRate 0-3; fire %d%%, "
         "reload %s)", uname_str(v.clsName), v.state, v.scale[0], r[0], r[1], r[2], r[3],
         weap_fire_pct_clamped(g_cfg.fireRatePct), rel);
}

// The verdict one frame after a measured call (handling.md section 6).
static void finish_pending(const View& v) {
  if (!s_pend.on) return;
  s_pend.on = 0;
  float after[4];
  if (!read_rates(s_pend.mesh, after)) return;
  const int verdict = hd_rate_verdict(s_pend.before, after, 4, s_pend.oldScale, s_pend.newScale);
  np_log("handling: %ls state %d: UpdateGlobalAnimRateScale %.3f -> %.3f: channel rates %.3f %.3f %.3f %.3f, one frame "
         "later %.3f %.3f %.3f %.3f (state now %d): %s", uname_str(s_pend.clsName), s_pend.state, s_pend.oldScale,
         s_pend.newScale, s_pend.before[0], s_pend.before[1], s_pend.before[2], s_pend.before[3], after[0], after[1],
         after[2], after[3], v.state, hd_verdict_text(verdict));
}

// Actor.GetSequence(0) on the weapon (native, read only): channel 0's sequence name; -1 when not called.
static int anim_seq(void* w) {
  const ScallFn f = scall_find(w, L"GetSequence");
  ScallParm ch, rv;
  if (!f.ok || !scall_parm(f, L"Channel", L"IntProperty", &ch) || ch.size != 4 ||
      !scall_parm(f, L"ReturnValue", L"NameProperty", &rv) || rv.size != 4)
    return -1;
  unsigned char parms[32];
  memset(parms, 0, sizeof parms);   // GroupName 0 (None): the channel itself
  const int zero = 0;
  if (f.parmsSize > sizeof parms || !scallparms_put(parms, f.parmsSize, ch.offset, &zero, 4)) return -1;
  if (!scall_call(w, f, parms, sizeof parms)) return -1;
  int name;
  memcpy(&name, parms + rv.offset, 4);
  return name;
}

// One line per WeaponState change while fire is held: the state this poll finds, against what we did since the line
// before (the one-shot-burst cycle, handling.md section 8). Runs before drive: this poll's calls go in the next line.
static void fire_trace(const View& v) {
  if (v.weapon != s_trWeapon) {
    s_trWeapon = v.weapon;
    s_trState = v.state;
    s_trHeld = v.held;
    return;
  }
  const int due = hd_trace_due(v.held, s_trHeld, s_trState, v.state, s_trN);
  const int prev = s_trState;
  s_trState = v.state;
  s_trHeld = v.held;
  if (!due) return;
  ++s_trN;
  float r[4];
  read_rates(v.mesh, r);
  const int seq = anim_seq(v.weapon);
  const float tMs = v.haveNow ? v.now * 1000.f : 0.f;
  np_log("handling: fire trace %d: frame %u t %.0f ms (+%.0f): %ls state %d -> %d, anim %ls rate %.3f, lockout %d, "
         "shots %d, fire %s; ours since the last line: AnimRateScale %.3f (%u call(s)), ClearLockout %u (the last "
         "%.0f ms ago)%s", s_trN, s_frames, tMs, s_trN > 1 ? tMs - s_trT : 0.f, uname_str(v.clsName), prev, v.state,
         seq > 0 ? uname_str(seq) : seq == 0 ? L"None" : L"?", r[0], v.haveLock ? v.lock : -1, v.shots,
         v.held > 0 ? "held" : v.held == 0 ? "let go" : "?", v.nCtrl ? v.scale[0] : 0.f, s_calls - s_trCalls,
         s_clears - s_trClears, s_lastClearT >= 0.f && v.haveNow ? (v.now - s_lastClearT) * 1000.f : -1.f,
         s_trN == HD_TRACE_MAX ? "; the trace's last line this level" : "");
  s_trT = tMs;
  s_trCalls = s_calls;
  s_trClears = s_clears;
}

static void drive(const View& v, int active) {
  for (int i = 0; i < v.nCtrl; i++) {
    void* c = v.ctrl[i];
    const float live = v.scale[i];
    Rec* r = rec_find(c);
    if (hd_adopt(r != 0, live)) {
      r = rec_add(c, v.clsName, live);
      ++s_adopted;
      np_log("handling: %ls's controller holds AnimRateScale %.3f with no record of ours (a save carried it); "
             "taken as ours", uname_str(v.clsName), live);
    }
    const float want = active ? hd_want(v.state, g_cfg.fireRatePct, g_cfg.reloadPct) : 1.f;
    if (!hd_call_due(active, live, want, r != 0, r ? r->ours : 0.f)) {
      if (r) r->stuck = 0;
      continue;
    }
    // A call the engine did not take (the live value never moves) is tried STUCK_MAX frames, then left for this
    // value: never one call a frame forever.
    if (r && r->lastWant == want && r->stuck >= STUCK_MAX) {
      ++s_stuckSkips;
      continue;
    }
    const bool measure = i == 0 && !s_pend.on && seen_claim(v.clsName, 100 + v.state, (int)(want * 100.f + 0.5f));
    float before[4];
    if (measure) read_rates(v.mesh, before);
    if (!set_scale(c, want)) return;
    if (measure) {
      s_pend.on = 1;
      s_pend.mesh = v.mesh;
      s_pend.clsName = v.clsName;
      s_pend.state = v.state;
      s_pend.oldScale = live;
      s_pend.newScale = want;
      memcpy(s_pend.before, before, sizeof before);
    }
    r = rec_add(c, v.clsName, want);
    if (r) {
      r->stuck = r->lastWant == want ? r->stuck + 1 : 1;
      if (r->stuck == STUCK_MAX)
        np_log("handling: %ls: AnimRateScale stays %.3f after %d calls of %.3f; left until the value changes",
               uname_str(v.clsName), live, STUCK_MAX, want);
      r->lastWant = want;
    }
  }
}

// ClearLockout on the weapon, by the function a timer call reaches now (its state's first). 1 when called.
static int clear_lockout(void* w) {
  const ScallFn has = scall_find(w, L"ClearLockout");
  const ScallFn fn = has.ok ? scall_find_state(w, L"ClearLockout") : has;
  if (!fn.ok || fn.parmsSize) {
    ++s_clearRefused;
    return 0;
  }
  unsigned char parms[4] = { 0, 0, 0, 0 };
  if (!scall_call(w, fn, parms, sizeof parms)) {
    ++s_clearRefused;
    return 0;
  }
  ++s_clears;
  return 1;
}

static void bucket_flush(void) {
  s_bk.n = s_bk.lockN = s_bk.lockOurs = s_bk.lockEarly = 0;
  s_bk.sum = s_bk.lockSum = s_bk.lockMax = 0.f;
  s_bk.min = s_bk.lockMin = 1e9f;
}

// Once per weapon class and fire %, after 8 shot intervals: the cadence and the lockouts, against the stock cadence.
static void bucket_log(float lockTime, float f) {
  if (s_bk.n < 8 || !seen_claim(s_bk.clsName, 200, s_bk.firePct)) return;
  const float avg = s_bk.sum / (float)s_bk.n;
  float stock = 0.f;
  for (int i = 0; i < s_stockN; i++)
    if (s_stock[i].clsName == s_bk.clsName) stock = s_stock[i].min;
  if (s_bk.firePct == WEAP_FIRE_MIN && !stock && s_stockN < STOCK_MAX) {
    const Stock e = { s_bk.clsName, s_bk.min };
    s_stock[s_stockN++] = e;
  }
  const int eff = hd_rate_effect(stock, s_bk.min, f);
  np_log("handling: %ls at fire %d%%: %d shot intervals avg %.3f s min %.3f s (game time; stock min %.3f s: %s); "
         "lockouts %d avg %.3f s min %.3f max %.3f (LockoutFireTime %.3f / %.2f = %.3f), %d cleared by us, %d ended "
         "early by the game", uname_str(s_bk.clsName), s_bk.firePct, s_bk.n, avg, s_bk.min, stock,
         s_bk.firePct == WEAP_FIRE_MIN ? "this is stock" : !stock ? "not measured yet"
         : eff == 1 ? "the fire rate took effect" : eff == 0 ? "NO EFFECT on the cadence" : "unclear",
         s_bk.lockN, s_bk.lockN ? s_bk.lockSum / (float)s_bk.lockN : 0.f, s_bk.lockN ? s_bk.lockMin : 0.f,
         s_bk.lockMax, lockTime, f, f > 0.f ? lockTime / f : 0.f, s_bk.lockOurs, s_bk.lockEarly);
}

static void lockout_and_shots(const View& v, int active) {
  const float f = active ? weap_fire_factor(g_cfg.fireRatePct) : 1.f;
  const float lockTime = v.cfg ? lock_time(v.cfg) : -1.f;
  if (v.weapon != s_lockWeapon) {
    s_lockWeapon = v.weapon;
    s_lock = hd_lock_init();
    s_lkPrev = 0;
  }
  const int firePct = weap_fire_pct_clamped(g_cfg.fireRatePct);
  if (v.clsName != s_bk.clsName || firePct != s_bk.firePct) {
    s_bk.clsName = v.clsName;
    s_bk.firePct = firePct;
    bucket_flush();
  }
  if (!v.weapon || !v.haveNow || !v.haveLock) return;
  int lockNow = v.lock, cleared = 0;
  if (hd_lock_step(&s_lock, lockNow, v.now, lockTime, f, s_lockEager) && clear_lockout(v.weapon)) {
    cleared = 1;
    s_lastClearT = v.now;
    const int again = read_lock(v.weapon);
    hd_lock_after_clear(&s_lock, again, v.now);
    lockNow = again;
  }
  // The lockout's length per shot: a stale engine timer would show as one ended early by the game.
  if (!s_lkPrev && lockNow) {
    s_lkT0 = v.now;
    ++s_lockouts;
  } else if (s_lkPrev && (!lockNow || cleared)) {
    const float len = v.now - s_lkT0;
    s_bk.lockN++;
    s_bk.lockSum += len;
    if (len < s_bk.lockMin) s_bk.lockMin = len;
    if (len > s_bk.lockMax) s_bk.lockMax = len;
    if (cleared) s_bk.lockOurs++;
    else if (lockTime > 0.f && len < lockTime / f * 0.9f - 0.02f) s_bk.lockEarly++;
    if (lockNow) s_lkT0 = v.now;
  }
  s_lkPrev = lockNow;
  // PlayerPawn.AccuracyShots rises once a shot (RecordShot).
  if (v.weapon != s_shotWeapon || v.shots < s_lastShots) {
    s_shotWeapon = v.weapon;
    s_lastShots = v.shots;
    s_lastShotT = -1.f;
  } else if (v.shots > s_lastShots) {
    const float dt = v.now - s_lastShotT;
    if (s_lastShotT >= 0.f && dt > 0.f && dt < 2.f) {
      s_bk.n++;
      s_bk.sum += dt;
      if (dt < s_bk.min) s_bk.min = dt;
    }
    s_lastShots = v.shots;
    s_lastShotT = v.now;
    bucket_log(lockTime, f);
  }
}

static void new_level(unsigned level) {
  s_level = level;
  if (s_recN) np_log("handling: new level: %d controller record(s) dropped (they went with the old level)", s_recN);
  s_recN = 0;
  s_lockWeapon = 0;
  s_lock = hd_lock_init();
  s_lkPrev = 0;
  s_cdoCls = 0;
  s_pend.on = 0;
  s_shotWeapon = 0;
  s_lastShots = -1;
  s_prevWeapon = 0;
  s_prevState = -1;
  s_firstHeldLogged = false;
  s_trWeapon = 0;
  s_trState = -1;
  s_trHeld = -1;
  s_trN = 0;
  s_lastClearT = -1.f;
  s_upd.fn = 0;
  if (s_refused) {   // checked again once per level, logged once
    s_refused = 0;
    s_checked = false;
  }
}

void handling_frame(int live) {
  void* player = world_player();
  const unsigned level = world_level_serial();
  if (level != s_level) new_level(level);
  if (!player || !live) return;
  if (!s_checked) {
    s_checked = true;
    const char* why = check();
    if (why) refuse(why);
  }
  if (s_refused) return;
  View v;
  if (!read_view(player, &v)) return;
  // Outside standalone play the rows read as stock: the controllers are driven back to 1.0 (mpgate.h).
  const int active = !mp_refused() && hd_active(g_cfg.fireRatePct, g_cfg.reloadPct);
  finish_pending(v);
  release_unheld(v);
  if (v.nCtrl && !s_firstHeldLogged) {
    s_firstHeldLogged = true;
    np_log("handling: first held weapon this level: %ls state %d, AnimRateScale %.3f%s (a save made above 100 %% "
           "reads more than 1 here)", uname_str(v.clsName), v.state, v.scale[0],
           v.nCtrl > 1 ? ", and a second controller" : "");
  }
  log_state(v);
  if (active) fire_trace(v);   // an idle panel logs nothing
  // Also with both rows at 100 %: a controller a save left above 1.0 is adopted and put back.
  if (v.nCtrl) drive(v, active);
  if (s_refused) return;
  lockout_and_shots(v, active);
  static unsigned s_lastCalls;
  // Only while a row is up or something happened since the last line: an idle panel logs nothing.
  if (++s_frames % 600 == 0 && (active || s_calls != s_lastCalls)) {
    s_lastCalls = s_calls;
    char rel[16];
    weap_reload_text(g_cfg.reloadPct, rel, sizeof rel);
    np_log("handling: 600 frames: fire %d%% reload %s; held %ls state %d AnimRateScale %.3f; calls %u (refused %u, "
           "stuck skips %u), restores %u, adopted %u, ClearLockout %u (refused %u), lockouts %u; records %d; faults %u",
           weap_fire_pct_clamped(g_cfg.fireRatePct), rel, v.weapon ? uname_str(v.clsName) : L"none", v.state,
           v.nCtrl ? v.scale[0] : 0.f, s_calls, s_refusedCalls, s_stuckSkips, s_restores, s_adopted, s_clears,
           s_clearRefused, s_lockouts, s_recN, s_faults);
  }
}

const char* handling_refused(void) { return s_refused ? s_refused : mp_refused(); }

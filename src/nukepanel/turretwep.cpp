// Fire rate and Perfect accuracy on the mounted gun (turretwep.h; turrets.md 7). Per frame, game thread: the mount
// (aim.cpp's read), then three levers, each put back when firing stops, the row goes off or the player dismounts. Reads
// are access-violation guarded; the calls (GlobalSetProperty, SetAnimRate, UpdateGlobalAnimRateScale) are not.
#include <windows.h>
#include <string.h>
#include "turretwep.h"
#include "accuracy.h"
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

enum { N_HOOK, N_CRSTATE, N_MOUNTPARENT, N_REMAP, N_TURRET, N_FIREDOWN, N_MAXSPREAD, N_MUZZLE, N_CONTROLLER, N_SCALE,
       N_MESHINST, N_LEVEL, N_TIME, N_FIREHELD, N_HEAT, N_OVERHEATED, N_COUNT, N_REQUIRED = N_MAXSPREAD };
static const wchar_t* const NAMES[N_COUNT] = { L"InputHookActor", L"CurrentRemapperState", L"MountParent",
                                               L"dnControlRemapperEx_Turret", L"dnControl_Turret", L"FireDown",
                                               L"MaxSpread", L"MuzzleIndex", L"AnimationController", L"AnimRateScale",
                                               L"MeshInstance", L"Level", L"TimeSeconds", L"bIDnControlFire",
                                               L"TotalHeat", L"bOverheated" };
static int s_n[N_COUNT];
static const char* s_refused;
static bool s_checked;
static unsigned s_level;

// The current state, as world.cpp reads it: UObject.StateFrame +0x0C, FStateFrame.StateNode +0x14 (world.cpp's
// justification: FindObjectField, Engine.dll!0x1003D3D0).
static const int UO_STATEFRAME = 0x0C, SF_STATENODE = 0x14;
enum { CRS_FULLY_ATTACHED = 2 };   // ControlRemapperEx.EControlRemapperState (aim.h AIM_CRS_FULLY_ATTACHED)

// GetAnimRate (UMeshInstance, Engine.dll export 0x1030DF40): __thiscall (BYTE channel, const dnName* group), the
// rate in ST0; a null group reads the channel itself (handling.cpp, read 2026-10-08).
typedef float (__fastcall* GetAnimRateFn)(void* self, void* edx, unsigned char chan, const int* group);
static GetAnimRateFn s_getRate;
static bool s_getRateTried;

static TwSpread s_sp;
static TwAnim s_ga, s_ua;            // the gun's channel 0, the player's controller
static void* s_gaObj;                // the actor s_ga holds a rate on, and its object index
static int s_gaIdx;
static void* s_uaObj;
static int s_uaIdx;

// This mount: the gun, its class and lever set; counters for the dismount line.
static void* s_gun;
static int s_gunName, s_tur, s_logs;
static unsigned s_spApply, s_spRestore, s_gaSets, s_gaBack, s_uaSets, s_uaBack, s_refusedCalls, s_faults;
static int s_gaLogged, s_uaLogged;
static int s_spThisMount, s_spCapLogged;   // MaxSpread applies this mount (tw_apply_allowed)
// The shot count while fire is held (MuzzleIndex, or TotalHeat rises for a gun without one).
static TwRate s_rate;
static int s_prevMuzzle = -9;
static float s_prevHeat = -1.f;
static int s_rateLines, s_rateGun;
enum { LOGS_PER_LEVEL = 60 };

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static bool obj_ok(const void* p, int idx) {
  __try {
    return p && uobj_valid(p) && uw_int(p, UO_INDEX) == idx;
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

static UProp prop(const void* o, int k) {
  UProp none = { 0, 0, UW_OTHER, 0, 0, 0 };
  if (!o || !s_n[k]) return none;
  return uprop_get(uobj_class(o), s_n[k]);
}

// Inside a guard.
static void* rd_obj(const void* o, int k) {
  const UProp p = prop(o, k);
  if (!p.ok || p.type != UW_OBJECT) return 0;
  void* v = (void*)uw_ptr(o, p.offset);
  return v && uobj_valid(v) ? v : 0;
}
static int rd_float(const void* o, int k, float* out) {
  const UProp p = prop(o, k);
  if (!p.ok || p.type != UW_FLOAT || p.elemSize != 4) return 0;
  memcpy(out, (const char*)o + p.offset, 4);
  return 1;
}
static int rd_int(const void* o, int k, int* out) {
  const UProp p = prop(o, k);
  if (!p.ok || p.type != UW_INT || p.elemSize != 4) return 0;
  *out = uw_int(o, p.offset);
  return 1;
}
static int rd_bool(const void* o, int k) {
  const UProp p = prop(o, k);
  if (!p.ok || p.type != UW_BOOL || !p.bitmask) return -1;
  return (uw_int(o, p.offset) & (int)p.bitmask) != 0;
}
static bool isa_name(const void* o, int nameIdx) {
  if (!nameIdx) return false;
  const void* c = uobj_class(o);
  for (int i = 0; c && i < 64; i++, c = uw_ptr(c, US_SUPER))
    if (uobj_name(c) == nameIdx) return true;
  return false;
}

struct Mount { void* gun; int gunName, tur, firing, muzzle, held, haveHeat, overheated; float heat, now; int haveNow; };

static bool read_mount(void* player, Mount* m) {
  memset(m, 0, sizeof *m);
  m->muzzle = -9;
  m->held = -1;
  m->overheated = -1;
  __try {
    const void* level = rd_obj(player, N_LEVEL);
    if (level) m->haveNow = rd_float(level, N_TIME, &m->now);
    m->held = rd_bool(player, N_FIREHELD);
    const void* hook = rd_obj(player, N_HOOK);
    if (!hook || !isa_name(hook, s_n[N_REMAP])) return true;
    const UProp st = prop(hook, N_CRSTATE);
    if (!st.ok || st.type != UW_BYTE || *((const unsigned char*)hook + st.offset) != CRS_FULLY_ATTACHED) return true;
    void* gun = rd_obj(hook, N_MOUNTPARENT);
    if (!gun || !isa_name(gun, s_n[N_TURRET])) return true;
    m->gun = gun;
    m->gunName = uobj_name(uobj_class(gun));
    if (gun == s_gun && m->gunName == s_gunName) {
      m->tur = s_tur;
    } else {
      const wchar_t* chain[16];
      int nn = 0;
      for (const void* c = uobj_class(gun); c && nn < 16 && uobj_valid(c); c = uw_ptr(c, US_SUPER))
        chain[nn++] = uname_str(uobj_name(c));
      m->tur = aim_turret_class(chain, nn);
    }
    const char* frame = (const char*)uw_ptr(gun, UO_STATEFRAME);
    const void* node = frame ? uw_ptr(frame, SF_STATENODE) : 0;
    m->firing = node && uobj_valid(node) && uobj_name(node) == s_n[N_FIREDOWN];
    if (!rd_int(gun, N_MUZZLE, &m->muzzle)) m->muzzle = -9;
    m->haveHeat = rd_float(gun, N_HEAT, &m->heat);
    m->overheated = rd_bool(gun, N_OVERHEATED);
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

// MaxSpread on cls's class default: its property and value. 0 when it does not read as a 4-byte int.
static int read_spread(const void* cls, UProp* p, int* v) {
  __try {
    void* cdo = engine_get_default_object((void*)cls);
    *p = s_n[N_MAXSPREAD] ? uprop_get(cls, s_n[N_MAXSPREAD]) : UProp{ 0, 0, UW_OTHER, 0, 0, 0 };
    if (!cdo || !p->ok || p->type != UW_INT || p->elemSize != 4 || !p->field) return 0;
    *v = uw_int(cdo, p->offset);
    return 1;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

static int read_int_of(const void* o, int k, int* out) {
  __try {
    return o && uobj_valid(o) && rd_int(o, k, out);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

// Channel 0's rate on the actor's mesh instance; 0 when the export, the mesh or the read is missing.
static int read_rate(const void* actor, float* out) {
  if (!s_getRateTried) {
    s_getRateTried = true;
    const HMODULE eng = GetModuleHandleW(L"Engine.dll");
    s_getRate = eng ? (GetAnimRateFn)GetProcAddress(eng, "?GetAnimRate@UMeshInstance@@QAEMEPBVdnName@@@Z") : 0;
    if (!s_getRate) np_log("turret: UMeshInstance::GetAnimRate is not exported; the gun's fire anim is left alone");
  }
  if (!s_getRate) return 0;
  __try {
    void* mesh = actor && uobj_valid(actor) ? rd_obj(actor, N_MESHINST) : 0;
    if (!mesh) return 0;
    *out = s_getRate(mesh, 0, 0, 0);
    return 1;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

// The player's (pawn's) animation controller and its AnimRateScale.
static void* read_ctrl(const void* player, float* scale) {
  __try {
    void* c = rd_obj(player, N_CONTROLLER);
    return c && rd_float(c, N_SCALE, scale) ? c : 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
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

// Actor.SetAnimRate(Channel 0, Rate, GroupName None) by ProcessEvent: the channel the gun's FireDown anim plays on.
static int set_rate(void* actor, float r) {
  const ScallFn f = scall_find(actor, L"SetAnimRate");
  ScallParm ch, rt;
  if (!f.ok || f.parmsSize > 32 || !scall_parm(f, L"Channel", L"IntProperty", &ch) || ch.size != 4 ||
      !scall_parm(f, L"Rate", L"FloatProperty", &rt) || rt.size != 4) {
    ++s_refusedCalls;
    return 0;
  }
  unsigned char parms[32];
  memset(parms, 0, sizeof parms);   // GroupName 0 (None): the channel itself
  const int zero = 0;
  if (!scallparms_put(parms, f.parmsSize, ch.offset, &zero, 4) || !scallparms_put(parms, f.parmsSize, rt.offset, &r, 4) ||
      !scall_call(actor, f, parms, sizeof parms)) {
    ++s_refusedCalls;
    return 0;
  }
  return 1;
}

// AnimationControllerEx.UpdateGlobalAnimRateScale(f) by ProcessEvent (handling.md lever (a)).
static int set_scale(void* ctrl, float v) {
  const ScallFn fn = scall_find(ctrl, L"UpdateGlobalAnimRateScale");
  ScallParm p[2];
  if (!fn.ok || fn.parmsSize > 16 || scall_parms_of(fn, L"FloatProperty", p, 2) != 1 || p[0].size != 4) {
    ++s_refusedCalls;
    return 0;
  }
  unsigned char parms[16] = { 0 };
  if (!scallparms_put(parms, fn.parmsSize, p[0].offset, &v, 4) || !scall_call(ctrl, fn, parms, sizeof parms)) {
    ++s_refusedCalls;
    return 0;
  }
  return 1;
}

static const char* lever_text(int lev) {
  switch (lev) {
    case TW_L_SPREAD | TW_L_GUNANIM:   return "spread, gun fire anim";
    case TW_L_GUNANIM | TW_L_USERANIM: return "gun fire anim, player fire anim";
    case TW_L_USERANIM:                return "player fire anim";
    default:                           return "none";
  }
}

static void log_mount(void* player, const Mount& m) {
  if (s_logs >= LOGS_PER_LEVEL) return;
  s_logs++;
  UProp p;
  int def = -1, inst = -1;
  read_spread(uobj_class(m.gun), &p, &def);
  read_int_of(m.gun, N_MAXSPREAD, &inst);
  float rate = -1.f, scale = -1.f;
  read_rate(m.gun, &rate);
  read_ctrl(player, &scale);
  void* gunCtrl = 0;
  __try {
    gunCtrl = rd_obj(m.gun, N_CONTROLLER);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
  }
  np_log("turret: mounted %ls: levers %s; MaxSpread default %d, instance %d; gun AnimationController %s, channel 0 "
         "rate %.3f; player AnimRateScale %.3f; fire %d%%, accuracy %s", uname_str(m.gunName),
         lever_text(tw_levers(m.tur)), def, inst, gunCtrl ? "set" : "none", rate, scale,
         weap_fire_pct_clamped(g_cfg.fireRatePct), g_cfg.accuracyOn ? "on" : "off");
}

static void log_dismount(void) {
  if (s_logs >= LOGS_PER_LEVEL) return;
  s_logs++;
  np_log("turret: dismounted from %ls: MaxSpread applied %u, restored %u; gun rate sets %u (put back %u); player "
         "scale sets %u (put back %u); refused calls %u, faults %u", uname_str(s_gunName), s_spApply, s_spRestore,
         s_gaSets, s_gaBack, s_uaSets, s_uaBack, s_refusedCalls, s_faults);
  s_spApply = s_spRestore = s_gaSets = s_gaBack = s_uaSets = s_uaBack = 0;
  s_gaLogged = s_uaLogged = 0;
}

// Microseconds since t0 (QueryPerformanceCounter): the object-table walk's cost.
static double elapsed_us(const LARGE_INTEGER& t0) {
  LARGE_INTEGER t1, fq;
  QueryPerformanceCounter(&t1);
  QueryPerformanceFrequency(&fq);
  return fq.QuadPart ? (double)(t1.QuadPart - t0.QuadPart) * 1e6 / (double)fq.QuadPart : -1.0;
}

static void spread_frame(const Mount& m) {
  const int on = g_cfg.accuracyOn && !mp_refused() && m.gun && (tw_levers(m.tur) & TW_L_SPREAD);
  const void* gunCls = m.gun ? uobj_class(m.gun) : 0;
  const void* cls = s_sp.cls ? s_sp.cls : on ? gunCls : 0;
  if (!cls) return;
  const int clsName = s_sp.cls ? s_sp.clsName : m.gunName;
  UProp p;
  int cur = 0;
  const int ok = read_spread(cls, &p, &cur);
  float back = 0.f;
  const int act = tw_spread_step(&s_sp, on, gunCls, m.gunName, ok, (float)cur, &back);
  if (act == TW_SP_APPLY) {
    if (!tw_apply_allowed(s_spThisMount)) {
      if (!s_spCapLogged) {
        s_spCapLogged = 1;
        np_log("turret: %ls.MaxSpread reads %d again after %d applies this mount: left until the next mount",
               uname_str(clsName), cur, s_spThisMount);
      }
      return;
    }
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    engine_set_property_all(L"0", (void*)cls, (void*)p.field, p.offset);
    const double us = elapsed_us(t0);
    ++s_spApply;
    ++s_spThisMount;
    int now = -1;
    read_spread(cls, &p, &now);
    np_log("turret: %ls.MaxSpread %d -> 0 (GlobalSetProperty, the class default and every instance, bNoSave 1; "
           "%.0f us); the default reads %d", uname_str(clsName), cur, us, now);
  } else if (act == TW_SP_RESTORE) {
    wchar_t text[16];
    rk_int_text((int)back, text, 16);
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    engine_set_property_all(text, (void*)cls, (void*)p.field, p.offset);
    const double us = elapsed_us(t0);
    ++s_spRestore;
    np_log("turret: %ls.MaxSpread restored %d (every instance and the default; %.0f us)", uname_str(clsName),
           (int)back, us);
  } else if (act == TW_SP_DROP) {
    np_log("turret: %ls.MaxSpread reads %d, not our 0 (or 0 before ours): left, record dropped", uname_str(clsName),
           cur);
  }
}

// The gun's channel 0 while it is in FireDown (its fire anim carries FireEffects -> PerformFire).
static void gun_anim_frame(const Mount& m, float f) {
  if (s_ga.applied && !obj_ok(s_gaObj, s_gaIdx)) {   // the actor went (level, destroyed): nothing to put back
    s_ga = tw_anim_init();
    s_gaObj = 0;
  }
  void* target = s_ga.applied ? s_gaObj : m.gun;
  const int want = target && target == m.gun && (tw_levers(m.tur) & TW_L_GUNANIM) && m.firing;
  if (!target || (!s_ga.applied && !(want && f > 1.f))) return;
  float live = 0.f, out = 0.f;
  const int ok = read_rate(target, &live);
  const int wasApplied = s_ga.applied;
  if (tw_anim_step(&s_ga, want, f, ok, live, &out) == TW_SET && set_rate(target, out)) {
    if (s_ga.applied) ++s_gaSets;
    else ++s_gaBack;
    if (s_gaLogged < 4 && s_logs < LOGS_PER_LEVEL) {   // the first calls of each mount
      s_gaLogged++;
      s_logs++;
      np_log("turret: %ls gun channel 0 rate %.3f -> %.3f (Actor.SetAnimRate; %s)",
             uname_str(m.gun ? m.gunName : s_gunName), live, out, s_ga.applied ? "FireDown" : "put back");
    }
  }
  if (s_ga.applied && !wasApplied) {
    s_gaObj = target;
    s_gaIdx = obj_index(target);
  }
  if (!s_ga.applied) s_gaObj = 0;
}

// The player's controller while the gun is in FireDown (the launcher's and SuperTurret's refire is its anim end).
static void user_anim_frame(void* player, const Mount& m, float f) {
  if (s_ua.applied && !obj_ok(s_uaObj, s_uaIdx)) {
    s_ua = tw_anim_init();
    s_uaObj = 0;
  }
  const int want = m.gun && (tw_levers(m.tur) & TW_L_USERANIM) && m.firing;
  if (!s_ua.applied && !(want && f > 1.f)) return;
  float live = 0.f;
  void* target = s_ua.applied ? s_uaObj : 0;
  int ok = 0;
  if (target) ok = read_scale(target, &live);
  else {
    target = read_ctrl(player, &live);
    ok = target != 0;
  }
  if (!target) return;
  float out = 0.f;
  const int wasApplied = s_ua.applied;
  if (tw_anim_step(&s_ua, want, f, ok, live, &out) == TW_SET && set_scale(target, out)) {
    if (s_ua.applied) ++s_uaSets;
    else ++s_uaBack;
    if (s_uaLogged < 4 && s_logs < LOGS_PER_LEVEL) {
      s_uaLogged++;
      s_logs++;
      np_log("turret: player AnimRateScale %.3f -> %.3f (UpdateGlobalAnimRateScale; %s)", live, out,
             s_ua.applied ? "FireDown" : "put back");
    }
  }
  if (s_ua.applied && !wasApplied) {
    s_uaObj = target;
    s_uaIdx = obj_index(target);
  }
  if (!s_ua.applied) s_uaObj = 0;
}

// Shots per second while fire is held: MuzzleIndex steps (MG42, launcher), else rises of TotalHeat (one a frame).
static void measure(const Mount& m) {
  int shots = 0;
  const char* by = "MuzzleIndex";
  if (m.muzzle != -9) {
    if (s_prevMuzzle != -9) shots = tw_muzzle_shots(s_prevMuzzle, m.muzzle);
  } else {
    by = "TotalHeat rises";
    if (m.haveHeat && s_prevHeat >= 0.f && m.heat > s_prevHeat) shots = 1;
  }
  s_prevMuzzle = m.muzzle;
  s_prevHeat = m.haveHeat ? m.heat : -1.f;
  if (!m.haveNow) return;
  if (!s_rate.open && shots > 0) s_rateGun = m.gunName;   // the window keeps its gun past a dismount
  int n = 0;
  float secs = 0.f;
  if (!tw_rate_step(&s_rate, m.gun && m.held > 0, m.now, shots, &n, &secs) || s_rateLines >= LOGS_PER_LEVEL) return;
  s_rateLines++;
  np_log("turret: %ls fire held: %d shots over %.3f s = %.2f/s (counted by %s; fire %d%%); TotalHeat %.1f, "
         "bOverheated %d", uname_str(s_rateGun), n, secs, tw_rate_per_s(n, secs), by,
         weap_fire_pct_clamped(g_cfg.fireRatePct), m.haveHeat ? m.heat : -1.f, m.overheated);
}

static void new_level(unsigned level) {
  s_level = level;
  // The class default outlives the level while its class stays loaded: the record stays, the next frame restores.
  if (s_sp.cls) {
    bool keep = false;
    __try {
      keep = uobj_valid(s_sp.cls) && acc_keep_record(1, uobj_name(s_sp.cls), s_sp.clsName);
    } __except (av_only(GetExceptionCode())) {
    }
    if (!keep) {
      np_log("turret: MaxSpread record of %ls dropped with the level (its class unloaded)", uname_str(s_sp.clsName));
      s_sp = tw_spread_init();
    }
  }
  s_ga = tw_anim_init();   // the gun and the pawn went with the level
  s_ua = tw_anim_init();
  s_gaObj = s_uaObj = 0;
  s_gun = 0;
  s_gunName = s_tur = 0;
  s_logs = s_rateLines = 0;
  s_rate = tw_rate_init();
  s_prevMuzzle = -9;
  s_prevHeat = -1.f;
  if (s_refused) {   // checked again once per level
    s_refused = 0;
    s_checked = false;
  }
}

void turretwep_frame(int live) {
  void* player = world_player();
  const unsigned level = world_level_serial();
  if (level != s_level) new_level(level);
  if (!player || !live) return;
  if (!s_checked) {
    s_checked = true;
    for (int k = 0; k < N_COUNT; k++) s_n[k] = uname_find(NAMES[k]);
    for (int k = 0; k < N_REQUIRED; k++)
      if (!s_n[k]) s_refused = "a turret name the levers need is not in the game";
    if (s_refused) np_log("turret: unavailable: %s; nothing is written", s_refused);
  }
  if (s_refused) return;
  Mount m;
  if (!read_mount(player, &m)) return;
  if (m.gun != s_gun) {
    if (s_gun) log_dismount();
    s_gun = m.gun;
    s_gunName = m.gunName;
    s_tur = m.tur;
    s_prevMuzzle = -9;
    s_prevHeat = -1.f;
    s_spThisMount = s_spCapLogged = 0;
    if (m.gun) log_mount(player, m);
  }
  const float f = mp_refused() ? 1.f : weap_fire_factor(g_cfg.fireRatePct);   // stock outside standalone play
  spread_frame(m);
  gun_anim_frame(m, f);
  user_anim_frame(player, m, f);
  measure(m);
}

// Triggerbot (tbot.h; aim.md 4), game thread only. Once a frame: the condition (Crosshair: the game's look trace;
// Locked: aim_target; Aimbot: aimbot_lock), the delay, then FireDown or FireUp on the player through scall (a mounted
// turret: DnControlFireEnable/Disable, turrets.md 4). Gate and weapon: aim.cpp's. Reads are access-violation guarded.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "tbot.h"
#include "aim.h"
#include "aimbot.h"
#include "achhelp.h"   // ah_aim_pass: a hostile candidate holds the fish back
#include "spawnbox.h"
#include "overkill.h"
#include "splash.h"
#include "world.h"
#include "view.h"
#include "espbox.h"
#include "pehook.h"
#include "dimouse.h"
#include "scall.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "engine.h"
#include "config.h"
#include "log.h"

enum { N_LOOKHIT, N_LOOKDIST, N_BIFIRE, N_WEAPON, N_WEAPONCONFIG, N_LOCKOUT, N_INTERRUPT, N_FIRESTART, N_LOOPING,
       N_LOCKTIME, N_ACCSHOTS, N_OWNER, N_MOUNTPARENT, N_WSTATE, N_WSIMPULSE, N_BIDNFIRE, N_AMMOLOADED, N_RELOADCOUNT, N_RELOADCLIP, N_AMMO, N_CHARGE, N_COUNT };
static const wchar_t* const NAMES[N_COUNT] = { L"DirectLookHitActor", L"LookHitDistance", L"bIFire", L"Weapon",
                                               L"WeaponConfig", L"bWeaponFireLockout", L"bFireInterruptFiring",
                                               L"bHasFireStart", L"bHasLoopingFire", L"LockoutFireTime",
                                               L"AccuracyShots", L"Owner", L"MountParent", L"WeaponState",
                                               L"WeaponStateImpulse", L"bIDnControlFire", L"AmmoLoaded", L"ReloadCount",
                                               L"ReloadClipAmmo", L"Ammo", L"Charge" };
static int s_n[N_COUNT];

static const char* s_why;
static int s_sub = -1;
static unsigned s_level;
static int s_checked, s_lookOk;   // this level's check ran; the look-trace properties resolved
static int s_dnOk;                // DnControlFireEnable/Disable and bIDnControlFire resolved: fires on turrets
static int s_pressMounted;        // the mode of the press (or the release owed) in flight: tbot_press_fn
static TbotState s_st;
static TbotPress s_press;
static unsigned s_lastMs;
// The fallback's one cached trace (the pawn under the screen centre), AIM_VIS_MS.
static const void* s_fbObj;
static unsigned s_fbMs;
static int s_fbVis;

static int s_pressLogs;
static const int LOGS_PER_LEVEL = 32;
static int s_holdLogged;   // this hold's first FireDown is logged; a pulse's later ones are not

// The held weapon's cadence (tbot_fire_mode), from its config class default, once per config class.
struct Cadence { const void* cfg; int cfgName, have, mode, clip, insert; };   // clip, insert: ReloadCount, ReloadClipAmmo (-1 unread)
static Cadence s_cad;
// Shots while held: PlayerPawn.AccuracyShots (RecordShot adds one a shot) over the frames the condition held.
static int s_lastShots = -1, s_prevHolds;
static unsigned s_heldShots, s_heldMs, s_pulses;
// Punches while held: the held fists' WeaponStateImpulse moving with WeaponState 10 (tbot_punch_began); a punch never
// reaches RecordShot, so AccuracyShots does not count it.
static int s_lastImpulse = -1;
static unsigned s_heldPunches;
// The look hit that is no walked pawn while one is near the crosshair (the flying troopers, 2026-10-05): logged.
static int s_otherLogs, s_otherLastCls;
static const void* s_otherLastPawn;
// Counted per 600 frames: look hits counted as their pawn through Owner/MountParent; Locked frames that followed the
// aimbot's lock with no enemy under the crosshair.
static unsigned s_xAttached, s_followAimbot;
// Crosshair frames on a hostile ship the held or mounted gun hurts (aircraft.h; no pawn is under the crosshair then),
// and on an achievement target whose row is on (achhelp.h: Pescaphobe's catfish); held back by a hostile candidate.
static unsigned s_xShip, s_xAch, s_xAchHeld;
// Crosshair frames whose pawn spawnbox leaves out (animated away from Location with no render box, teleporting, ...).
static unsigned s_xExcluded, s_xTele, s_xNoDmg, s_xDmg0, s_xNeutral, s_xWrongType;   // the teleporting, bNoDamage, player damage 0, neutral zone and wrong damage type shares
static unsigned s_xExpanded;   // its share fully expanded or popping while the Expander is up (bloat.h)
// Counted per 600 frames: UpdateLookHitActor on the player, frames the condition held, the crosshair on an enemy, on
// an ally, on something else; FireDown and FireUp sent, presses forgotten, calls scall refused, fallback frames.
static unsigned s_frames, s_lookEvents, s_holdFrames, s_xEnemy, s_xAlly, s_xOther, s_downs, s_ups, s_forgets,
    s_refusedCalls, s_fallback, s_faults, s_uncovered, s_waitTurn, s_rayUnres;
// Counted per 600 frames: frames mounted, DnControlFireEnable and DnControlFireDisable sent.
static unsigned s_mountFrames, s_enables, s_disables;
// Overkill (overkill.h): frames fire was held back with a doomed target passed over, and how many times that began.
static unsigned s_savedFrames, s_saved;
// Frames fire was held back during a shell-by-shell reload (tbot_reload_wait), and how many times that began.
static unsigned s_reloadFrames;
static int s_prevReloadWait, s_reloadLogs;
static int s_prevHeldBack;
// Splash (splash.h): frames fire was held because the blast would reach Duke, how many times that began, the bullet
// traces spent judging it; a capped line per hold.
static unsigned s_blastFrames, s_blastHolds, s_blastTraces, s_blastFails, s_blastCapped;
static double s_blastMs, s_blastMaxMs;   // the splash traces' time, and the most in one frame
static int s_prevBlast, s_blastLogs;
static int s_prevBossSkip;   // last frame held off an add by boss priority (counted in aim's boss line)

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
  np_log("tbot: Triggerbot unavailable: %s", why);
}

static UProp prop_of(const void* o, int k) {
  UProp none = { 0, 0, UW_OTHER, 0, 0, 0 };
  const int n = nm(k);
  if (!o || !n) return none;
  __try {
    return uprop_get(uobj_class(o), n);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return none;
  }
}

// The look trace's hit actor (validated) and distance; false when they do not resolve or a read faulted.
static bool read_look(const void* player, const void** hit, float* dist) {
  const UProp a = prop_of(player, N_LOOKHIT), d = prop_of(player, N_LOOKDIST);
  if (!a.ok || a.type != UW_OBJECT || !d.ok || d.type != UW_FLOAT) return false;
  __try {
    *hit = uw_ptr(player, a.offset);
    memcpy(dist, (const char*)player + d.offset, 4);
    if (*hit && !uobj_valid(*hit)) *hit = 0;
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

// Bool k of o, declared on cls's chain (o may be cls's default object). -1 when it does not resolve.
static int rd_bool(const void* o, const void* cls, int k) {
  const int n = nm(k);
  if (!o || !cls || !n) return -1;
  __try {
    const UProp p = uprop_get(cls, n);
    if (!p.ok || p.type != UW_BOOL || !p.bitmask) return -1;
    return (uw_int(o, p.offset) & (int)p.bitmask) != 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return -1;
  }
}

static const void* class_of(const void* o) {
  __try {
    return o ? uobj_class(o) : 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

// PlayerPawn.bIFire, or bIDnControlFire for a turret press: the game is firing (our press or the user's). -1 when it
// does not resolve.
static int read_firing(const void* player, int mounted = 0) {
  return rd_bool(player, class_of(player), mounted ? N_BIDNFIRE : N_BIFIRE);
}

// Object or class property k of o, validated; null when unread.
static const void* rd_obj(const void* o, int k) {
  const UProp p = prop_of(o, k);
  if (!p.ok || (p.type != UW_OBJECT && p.type != UW_CLASS)) return 0;
  __try {
    const void* v = uw_ptr(o, p.offset);
    return v && uobj_valid(v) ? v : 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

static int rd_int(const void* o, int k, int* out) {
  const UProp p = prop_of(o, k);
  if (!p.ok || p.type != UW_INT) return 0;
  __try {
    *out = uw_int(o, p.offset);
    return 1;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

// Int k of o read on cls's chain (o may be cls's default object).
static int rd_int_on(const void* o, const void* cls, int k, int* out) {
  const int n = nm(k);
  if (!o || !cls || !n) return 0;
  __try {
    const UProp p = uprop_get(cls, n);
    if (!p.ok || p.type != UW_INT) return 0;
    *out = uw_int(o, p.offset);
    return 1;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

static int rd_float(const void* o, int k, float* out) {
  const UProp p = prop_of(o, k);
  if (!p.ok || p.type != UW_FLOAT) return 0;
  __try {
    memcpy(out, (const char*)o + p.offset, 4);
    return 1;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

static int rd_byte(const void* o, int k, int* out) {
  const UProp p = prop_of(o, k);
  if (!p.ok || p.type != UW_BYTE) return 0;
  __try {
    *out = *((const unsigned char*)o + p.offset);
    return 1;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

// The held weapon and its cadence: its config class's bFireInterruptFiring, bHasFireStart and bHasLoopingFire from the
// class default (as Weapon.Firing.AttemptFire reads them), once per config class and logged with LockoutFireTime.
// The fists hold whatever their flags say (tbot_fire_mode_kind).
static const void* read_cadence(void* player, int kind) {
  const void* weapon = rd_obj(player, N_WEAPON);
  const void* cfg = weapon ? rd_obj(weapon, N_WEAPONCONFIG) : 0;
  if (!cfg) {
    s_cad.cfg = 0;
    s_cad.mode = TBOT_FIRE_HOLD;
    return weapon;
  }
  int cfgName = 0;
  void* def = 0;
  __try {
    cfgName = uobj_name(cfg);
    def = engine_get_default_object((void*)cfg);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return weapon;
  }
  if (cfg == s_cad.cfg && cfgName == s_cad.cfgName) return weapon;
  const int in = rd_bool(def, cfg, N_INTERRUPT), st = rd_bool(def, cfg, N_FIRESTART), lp = rd_bool(def, cfg, N_LOOPING);
  float lockout = -1.f;
  const int nLock = nm(N_LOCKTIME);
  if (def && nLock) {
    __try {
      const UProp p = uprop_get(cfg, nLock);
      if (p.ok && p.type == UW_FLOAT) memcpy(&lockout, (const char*)def + p.offset, 4);
    } __except (av_only(GetExceptionCode())) {
      ++s_faults;
    }
  }
  int clip = -1, insert = -1;
  if (!rd_int_on(def, cfg, N_RELOADCOUNT, &clip)) clip = -1;
  if (!rd_int_on(def, cfg, N_RELOADCLIP, &insert)) insert = -1;
  const Cadence c = { cfg, cfgName, in >= 0 && st >= 0 && lp >= 0, TBOT_FIRE_HOLD, clip, insert };
  s_cad = c;
  s_cad.mode = tbot_fire_mode_kind(kind, tbot_fire_mode(c.have, in > 0, st > 0, lp > 0));
  np_log("tbot: weapon %ls: bFireInterruptFiring %d, bHasFireStart %d, bHasLoopingFire %d, LockoutFireTime %.2f s "
         "-> %s", uname_str(cfgName), in, st, lp, lockout,
         s_cad.mode == TBOT_FIRE_PULSE ? "pulse (re-pressed when ready)" :
         kind == AIMW_MELEE ? "hold (melee: each punch's end throws the next)" : "hold");
  return weapon;
}

static int class_name_of(const void* o) {
  __try {
    return o && uobj_valid(o) ? uobj_name(uobj_class(o)) : 0;
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
}

// FireDown, FireUp, DnControlFireEnable or DnControlFireDisable (exec, flags 0x20202 alike, no parameters) on the
// player. 1 called.
static int press(void* player, const wchar_t* fn) {
  const ScallFn f = scall_find(player, fn);
  if (!f.ok || f.parmsSize) return 0;
  unsigned char parms[4] = { 0, 0, 0, 0 };
  if (scall_call(player, f, parms, sizeof parms)) return 1;
  ++s_refusedCalls;
  return 0;
}

static void on_look(void* obj, void* func, void* parms) {
  (void)func;
  (void)parms;
  if (obj && obj == world_player()) ++s_lookEvents;
}

// Once per level: the two exec functions and the look trace's properties (aim.md section 4: +0x1D64, +0x1D74).
static bool check(void* player) {
  const ScallFn d = scall_find(player, L"FireDown"), u = scall_find(player, L"FireUp");
  if (!d.ok || !u.ok || d.parmsSize || u.parmsSize) {
    refuse("PlayerPawn.FireDown or FireUp did not resolve as a function without parameters; see nukepanel.log");
    return false;
  }
  if (read_firing(player) < 0) {
    refuse("PlayerPawn.bIFire did not resolve; see nukepanel.log");
    return false;
  }
  // The turret's fire: without it the Triggerbot holds off while mounted, nothing else is refused.
  const ScallFn de = scall_find(player, L"DnControlFireEnable"), dd = scall_find(player, L"DnControlFireDisable");
  s_dnOk = de.ok && dd.ok && !de.parmsSize && !dd.parmsSize && read_firing(player, 1) >= 0;
  np_log("tbot: DnControlFireEnable/Disable and bIDnControlFire %s", s_dnOk ? "resolved: fires mounted guns" :
         "did not resolve: never fires on a mounted gun");
  const UProp a = prop_of(player, N_LOOKHIT), l = prop_of(player, N_LOOKDIST);
  s_lookOk = a.ok && a.type == UW_OBJECT && l.ok && l.type == UW_FLOAT;
  np_log("tbot: DirectLookHitActor %s+0x%x, LookHitDistance %s+0x%x (0x1D64 and 0x1D74 expected): Crosshair %s",
         a.ok ? "" : "unresolved ", a.offset, l.ok ? "" : "unresolved ", l.offset,
         s_lookOk ? "reads the game's look trace" : "falls back to the projected box and LineOfSightTo");
  return true;
}

// The walked pawn p, or -1.
static int walk_index(const WPawn* pw, int n, const void* p) {
  for (int i = 0; p && i < n; i++)
    if (pw[i].obj == p) return i;
  return -1;
}

// Crosshair by our own projection: the nearest walked pawn whose box holds the screen centre; an enemy there is
// checked by one LineOfSightTo (aim_line_of_sight), kept AIM_VIS_MS. A pawn animated away from Location is placed
// and traced at its render box (tbot_pawn_cyl, aim_box_visible), so an empty Location never draws fire.
static int crosshair_fallback(int W, int H, const AimWeapon& wp, const void** target, int* friendly, int* doomed) {
  ++s_fallback;
  const WView v = world_view();
  const WPawn* pw = 0;
  const int n = world_pawns(&pw);
  if (!v.ok) return 0;
  int best = -1;
  float bestD = 0.f;
  for (int i = 0; i < n; i++) {
    if (!pw[i].alive) continue;
    int ok = 0;
    float bc[3], bh[3];
    TbCyl c;
    tbot_pawn_cyl(spawnbox_body(pw[i].obj, bc, bh), bc, bh, pw[i].x, pw[i].y, pw[i].z, pw[i].radius, pw[i].height, &c);
    const EkScreenBox b = esp_pawn_box(v, c.x, c.y, c.z, c.r, c.h, W, H, &ok);
    if (!ok || !tbot_centre_in(b.x, b.y, b.w, b.h, W / 2, H / 2)) continue;
    const float dx = c.x - v.x, dy = c.y - v.y, dz = c.z - v.z;
    const float d = sqrtf(dx * dx + dy * dy + dz * dz);
    if (best < 0 || d < bestD) { best = i; bestD = d; }
  }
  if (best < 0) return 0;
  const WPawn& w = pw[best];
  if (w.ally || w.neutral) {
    *friendly = 1;
    ++s_xAlly;
  }
  float bc[3], bh[3];
  TbCyl cyl;
  const int boxed = tbot_pawn_cyl(spawnbox_body(w.obj, bc, bh), bc, bh, w.x, w.y, w.z, w.radius, w.height, &cyl);
  // The fists' reach is a surface distance: the centre's less the pawn's radius.
  const float xd = wp.kind == AIMW_MELEE ? (bestD > cyl.r ? bestD - cyl.r : 0.f) : bestD;
  if (!tbot_crosshair_enemy(1, w.enemy, w.ally, xd, wp.kind, wp.kind == AIMW_MELEE ? wp.reach : wp.maxTrace))
    return 0;
  if (const int ex = spawnbox_excluded(w.obj)) {
    if (ex == SB_EXCL_DOOMED) {   // the projectiles in flight will kill it
      *doomed = 1;
      return 0;
    }
    ++s_xExcluded;
    if (ex == SB_EXCL_TELEPORT) ++s_xTele;
    else if (ex == SB_EXCL_NODAMAGE) ++s_xNoDmg;
    else if (ex == SB_EXCL_PLAYERDMG0) ++s_xDmg0;
    else if (ex == SB_EXCL_NEUTRAL) ++s_xNeutral;
    else if (ex == SB_EXCL_WRONGTYPE) ++s_xWrongType;
    else if (ex == SB_EXCL_EXPANDED) ++s_xExpanded;
    return 0;
  }
  const unsigned now = GetTickCount();
  if (w.obj != s_fbObj || now - s_fbMs >= AIM_VIS_MS) {
    s_fbObj = w.obj;
    s_fbMs = now;
    s_fbVis = (boxed ? aim_box_visible(w.obj) : aim_line_of_sight(w.obj)) > 0;
  }
  if (!s_fbVis) return 0;
  *target = w.obj;
  return 1;
}

// tbot_attached_pawn's links and test: Owner and MountParent by name, and a walked pawn other than the player.
struct AttachCtx { const WPawn* pw; int n; const void* player; };
static void attach_links(const void* o, const void* out[2], void* ctx) {
  (void)ctx;
  out[0] = rd_obj(o, N_OWNER);
  out[1] = rd_obj(o, N_MOUNTPARENT);
}
static int attach_is_pawn(const void* o, void* ctx) {
  const AttachCtx* c = (const AttachCtx*)ctx;
  return o != c->player && walk_index(c->pw, c->n, o) >= 0;
}
// The walk never goes through the player, and a Projectile (an enemy's rocket in flight, a thrown pipe bomb)
// under the crosshair is not its owner. The class chain is walked under the guard, 16 classes at most.
static int attach_stop(const void* o, void* ctx) {
  if (o == ((const AttachCtx*)ctx)->player) return 1;
  static int s_projName;
  if (!s_projName) s_projName = uname_find(L"Projectile");
  if (!s_projName) return 0;
  __try {
    int k = 0;
    for (const void* c = uobj_class(o); c && k < 16 && uobj_valid(c); c = uw_ptr(c, US_SUPER), k++)
      if (uobj_name(c) == s_projName) return 1;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 1;
  }
  return 0;
}

// A look hit that is no walked pawn while Silent aim's or the aimbot's target is near the crosshair: its class, its
// Owner's and MountParent's, and the pawn the links reached. Capped; a repeat of the last line is skipped.
static void log_other(const void* hit, const void* pawn, int steps) {
  const void* tgt = aim_target_shared();
  if (!tgt) tgt = aimbot_lock();
  if (!tgt || s_otherLogs >= LOGS_PER_LEVEL / 2) return;
  const int cls = class_name_of(hit);
  if (cls == s_otherLastCls && pawn == s_otherLastPawn) return;
  s_otherLastCls = cls;
  s_otherLastPawn = pawn;
  s_otherLogs++;
  const int oc = class_name_of(rd_obj(hit, N_OWNER)), mc = class_name_of(rd_obj(hit, N_MOUNTPARENT));
  const int pc = class_name_of(pawn), nc = class_name_of(tgt);
  char res[48];
  if (pawn) _snprintf(res, sizeof res, "%s pawn after %d link(s)", pawn == tgt ? "that" : "another", steps);
  else _snprintf(res, sizeof res, "no walked pawn within %d links", (int)TBOT_ATTACH_STEPS);
  res[sizeof res - 1] = 0;
  np_log("tbot: look hit %ls (Owner %ls, MountParent %ls) while %ls is near the crosshair: %s (%ls)",
         cls ? uname_str(cls) : L"?", oc ? uname_str(oc) : L"None", mc ? uname_str(mc) : L"None",
         nc ? uname_str(nc) : L"?", res, pc ? uname_str(pc) : L"-");
}

// Crosshair: the look trace's hit is a hostile walked pawn within the gun's reach, or an actor hanging from one
// (tbot_attached_pawn). *target is that pawn; *friendly is set when it is an ally or a neutral pawn (Locked refuses);
// *doomed when it is a hostile the projectiles in flight will kill (overkill.h).
static int crosshair(void* player, int W, int H, const AimWeapon& wp, const void** target, int* friendly, int* doomed) {
  *friendly = 0;
  *doomed = 0;
  if (!s_lookOk) return crosshair_fallback(W, H, wp, target, friendly, doomed);
  const void* hit = 0;
  float dist = -1.f;
  if (!read_look(player, &hit, &dist)) return 0;
  const WPawn* pw = 0;
  const int n = world_pawns(&pw);
  int i = walk_index(pw, n, hit);
  // A ship is no walked pawn: accepted as itself while it is hostile, live, damageable and the gun hurts it.
  if (i < 0 && hit && aim_ship_shootable(hit)) {
    if (!tbot_crosshair_enemy(1, 1, 0, dist, wp.kind, wp.kind == AIMW_MELEE ? wp.reach : wp.maxTrace)) return 0;
    if (overkill_doomed(hit)) {
      *doomed = 1;
      return 0;
    }
    ++s_xShip;
    *target = hit;
    return 1;
  }
  // An achievement target, as a ship: while its row is on, alive, the gun's damage type hurts it and no hostile pawn
  // candidate exists this frame (ah_aim_pass).
  if (i < 0 && hit && aim_ach_shootable(hit)) {
    if (!ah_aim_pass(aim_pawn_cands(), 0)) {
      ++s_xAchHeld;
      return 0;
    }
    if (!tbot_crosshair_enemy(1, 1, 0, dist, wp.kind, wp.kind == AIMW_MELEE ? wp.reach : wp.maxTrace)) return 0;
    if (overkill_doomed(hit)) {
      *doomed = 1;
      return 0;
    }
    ++s_xAch;
    *target = hit;
    return 1;
  }
  if (i < 0 && hit) {
    AttachCtx ctx = { pw, n, player };
    int steps = -1;
    const void* pawn = tbot_attached_pawn(hit, attach_links, attach_is_pawn, attach_stop, &ctx, &steps);
    log_other(hit, pawn, steps);
    i = walk_index(pw, n, pawn);
    if (i >= 0) ++s_xAttached;
  }
  if (i < 0) {
    if (hit) ++s_xOther;
    return 0;
  }
  if (pw[i].ally || pw[i].neutral) {
    *friendly = 1;
    ++s_xAlly;
  }
  if (!tbot_crosshair_enemy(1, pw[i].enemy, pw[i].ally, dist, wp.kind, wp.kind == AIMW_MELEE ? wp.reach : wp.maxTrace))
    return 0;
  if (const int ex = spawnbox_excluded(pw[i].obj)) {   // the hit itself or the pawn it hangs from
    if (ex == SB_EXCL_DOOMED) {
      *doomed = 1;
      return 0;
    }
    ++s_xExcluded;
    if (ex == SB_EXCL_TELEPORT) ++s_xTele;
    else if (ex == SB_EXCL_NODAMAGE) ++s_xNoDmg;
    else if (ex == SB_EXCL_PLAYERDMG0) ++s_xDmg0;
    else if (ex == SB_EXCL_NEUTRAL) ++s_xNeutral;
    else if (ex == SB_EXCL_WRONGTYPE) ++s_xWrongType;
    else if (ex == SB_EXCL_EXPANDED) ++s_xExpanded;
    return 0;
  }
  ++s_xEnemy;
  *target = pw[i].obj;
  return 1;
}

static void new_level(unsigned level) {
  // The old pawn's press ends with it: nothing is called on a pawn of the last level.
  if (s_press.ours) np_log("tbot: a press dropped with the level");
  s_level = level;
  s_press.ours = 0;
  s_press.pendingUp = 0;
  s_press.upOwed = 0;
  s_pressMounted = 0;
  s_st = TbotState();
  s_checked = 0;
  s_dnOk = 0;
  s_lookOk = 0;
  s_fbObj = 0;
  s_pressLogs = 0;
  s_cad = Cadence();
  s_prevReloadWait = s_reloadLogs = 0;
  s_prevBlast = s_blastLogs = 0;
  s_prevBossSkip = 0;
  s_lastShots = -1;
  s_lastImpulse = -1;
  s_prevHolds = 0;
  s_otherLogs = s_otherLastCls = 0;
  s_otherLastPawn = 0;
}

// The held weapon is in a per-shell reload with shells still to load (tbot_reload_wait). State first: the other reads
// happen only in a reload.
static int reload_wait(const void* weapon) {
  int state = -1, loaded = -1;
  if (!weapon || !s_cad.cfg || s_cad.clip <= 0 || s_cad.insert <= 0 || s_cad.insert >= s_cad.clip) return 0;
  if (!rd_byte(weapon, N_WSTATE, &state) || state < TBOT_WSTATE_RELOAD || state > TBOT_WSTATE_RELOAD_STOP) return 0;
  const void* ammo = rd_obj(weapon, N_AMMO);
  float charge = -1.f;
  if (!rd_int(weapon, N_AMMOLOADED, &loaded) || !ammo || !rd_float(ammo, N_CHARGE, &charge)) return 0;
  return tbot_reload_wait(state, loaded, s_cad.clip, s_cad.insert, charge);
}

void tbot_frame(int W, int H, int inLevel, int panelOpen) {
  if (s_sub < 0) s_sub = pehook_subscribe(L"UpdateLookHitActor", on_look, "tbot");
  const unsigned level = world_level_serial();
  if (level != s_level) new_level(level);
  const unsigned now = GetTickCount();
  const int dt = s_lastMs ? (int)(now - s_lastMs) : 0;
  s_lastMs = now;
  void* player = world_player();
  const int on = g_cfg.tbotOn && !tbot_refused();
  if (on && inLevel && player && !s_checked) {
    s_checked = 1;
    check(player);
  }
  const AimWeapon wp = aim_weapon();
  // A press is released in the mode it was made in (tbot_mode_gate); with none in flight the mode follows the mount.
  const int mounted = aim_mounted();
  const int pending = s_press.ours || s_press.pendingUp || s_press.upOwed;
  if (!pending) s_pressMounted = mounted;
  const int mode = s_pressMounted;
  if (mounted) ++s_mountFrames;
  const int gateOpen = tbot_mode_gate(on && !s_why && inLevel && player && !panelOpen && aim_gate_now() == AIM_GO &&
                                      (!mounted || s_dnOk), pending, s_pressMounted, mounted);
  const void* weapon = player && on && inLevel ? read_cadence(player, wp.kind) : 0;
  const void* target = 0;
  int holds = 0, passedDoomed = 0, bossSkip = 0;
  if (gateOpen) {
    const void* xTarget = 0;
    int xFriend = 0;
    const int xEnemy = crosshair(player, W, H, wp, &xTarget, &xFriend, &passedDoomed);
    target = xTarget;
    int covered = 0;
    if (g_cfg.tbotWhen == TBOT_WHEN_LOCKED) {
      // A shot Silent aim redirects follows its target; any other follows the aimbot's lock when it holds
      // one (2026-10-05, as the Aimbot mode), else the crosshair.
      const void* locked = aim_target();
      const void* abLock = aimbot_lock();
      covered = tbot_locked_covered(wp.kind, aim_redirect_ready());
      if (covered) {
        target = locked;
      } else {
        if (locked) ++s_uncovered;
        if (abLock) {
          target = abLock;
          if (!xEnemy && !xFriend) ++s_followAimbot;
        }
      }
      holds = tbot_holds(TBOT_WHEN_LOCKED, xEnemy, xFriend, locked != 0, covered, abLock != 0);
    } else if (g_cfg.tbotWhen == TBOT_WHEN_AIMBOT) {
      // AA's mode: the aimbot's lock this frame, wherever the view is yet; never with an ally under the crosshair.
      const void* locked = aimbot_lock();
      target = locked;
      // The first FireDown came on the lock's frame, before the view had turned (2026-10-09): fire when Silent aim
      // covers the shot or the shot's own ray lands on the lock. The fists keep their rule (the ray is not asked).
      covered = tbot_locked_covered(wp.kind, aim_redirect_ready());
      int ray = -1;
      if (locked && !covered && wp.kind != AIMW_MELEE) {
        ray = tbot_view_ready(wp.kind, wp.kind == AIMW_PROJECTILE ? -1 : aim_view_ray_hits(locked),
                              wp.kind == AIMW_PROJECTILE ? aim_view_lead_deg(locked) : 0.f);
        if (ray == 0) ++s_waitTurn;
        else if (ray < 0) ++s_rayUnres;
      }
      holds = tbot_holds(TBOT_WHEN_AIMBOT, xEnemy, xFriend, locked != 0, covered || wp.kind == AIMW_MELEE, 0, ray);
    } else {
      holds = tbot_holds(TBOT_WHEN_CROSSHAIR, xEnemy, xFriend, 0, 0);
    }
    // Overkill: a doomed target is never fired at (a lock taken before the mark), and the aim's pass-over counts.
    if (holds && target && overkill_doomed(target)) {
      holds = 0;
      passedDoomed = 1;
    }
    if (g_cfg.tbotWhen != TBOT_WHEN_CROSSHAIR && aim_passed_doomed()) passedDoomed = 1;
    // Boss priority (aim.md section 12): while the boss is possible no add is fired at, in every mode.
    if (holds && tbot_boss_skips(aim_boss_on(), target, aim_boss())) {
      holds = 0;
      bossSkip = 1;
    }
  }
  if (bossSkip) aim_boss_skip(!s_prevBossSkip);
  s_prevBossSkip = bossSkip;
  // Splash: an explosive shot whose detonation point lies within its blast's reach of Duke is not fired; the aim goes on.
  int blast = 0;
  if (holds && target) {
    AimSplash sp;
    if (aim_splash(target, &sp) && sp.kind != SPL_NONE) {
      s_blastTraces += (unsigned)sp.traces;
      s_blastMs += sp.ms;
      if (sp.ms > s_blastMaxMs) s_blastMaxMs = sp.ms;
      const SplashVerdict v = splash_judge(sp.kind, sp.radius, sp.duke, sp.dukeR, sp.dukeH, sp.pts);
      if (v.hold) {
        holds = 0;
        blast = 1;
        if (v.fail) ++(v.fail == SPL_FAIL_CAP ? s_blastCapped : s_blastFails);
        const int tc = class_name_of(target);
        const wchar_t* gun = sp.gunName ? uname_str(sp.gunName) : L"?";
        if (!s_prevBlast && s_blastLogs < LOGS_PER_LEVEL / 4 && v.fail) {
          s_blastLogs++;   // fails closed (splash.h): an input was not had
          np_log("tbot: held fire: %ls's blast not judged: %s (%d trace(s)); radius %.0f; target %ls", gun,
                 splash_fail_label(v.fail), sp.traces, splash_radius(sp.kind, sp.radius), tc ? uname_str(tc) : L"?");
        } else if (!s_prevBlast && s_blastLogs < LOGS_PER_LEVEL / 4) {
          s_blastLogs++;
          const float* q = sp.pts.p[v.src];
          const float dx = q[0] - sp.duke[0], dy = q[1] - sp.duke[1], dz = q[2] - sp.duke[2];
          const int bc = sp.blockerCls[v.src];
          np_log("tbot: held fire: %ls's blast would reach Duke: radius %.0f (%s), reach %.0f beyond his collision "
                 "(radius %.0f, half-height %.0f); detonation point %.0f units from it (%.0f from his Location) by %s%s%ls; "
                 "target %ls", gun, splash_radius(sp.kind, sp.radius),
                 sp.kind == SPL_READ ? "DamageRadius" : "DamageRadius unread: the default", v.reach, sp.dukeR, sp.dukeH,
                 v.dist, sqrtf(dx * dx + dy * dy + dz * dz), splash_src_label(v.src),
                 v.src != SPL_PT_TARGET ? ", hit " : "", v.src == SPL_PT_TARGET ? L"" : bc ? uname_str(bc) :
                 L"an unreadable actor", tc ? uname_str(tc) : L"?");
        }
      }
    }
  }
  if (blast) {
    ++s_blastFrames;
    if (!s_prevBlast) ++s_blastHolds;
  }
  s_prevBlast = blast;
  // A shotgun fired mid-reload stops loading (docs/re/handling.md section 2): wait for the reload to finish.
  const int reloadWait = holds && reload_wait(weapon);
  if (reloadWait) {
    holds = 0;
    ++s_reloadFrames;
    if (!s_prevReloadWait && s_reloadLogs < LOGS_PER_LEVEL / 4) {
      s_reloadLogs++;
      np_log("tbot: holding fire while %ls reloads shell by shell", wp.cfgName ? uname_str(wp.cfgName) : L"?");
    }
  }
  s_prevReloadWait = reloadWait;
  const int heldBack = passedDoomed && !holds;
  if (heldBack) {
    ++s_savedFrames;
    if (!s_prevHeldBack) ++s_saved;
  }
  s_prevHeldBack = heldBack;
  if (holds) ++s_holdFrames;
  const int fire = tbot_step(&s_st, holds, target, dt, g_cfg.tbotDelay);
  const int heldMs = s_st.heldMs;
  // The user's own fire: button 0 as the engine's own DInput read received it, so a click the game never
  // saw is never trusted to send FireUp; a key-bound fire is seen through bIFire alone (tbot_act's pendingUp).
  const int userDown = !panelOpen && dimouse_button0();
  const int firing = player ? read_firing(player, mode) : 0;
  const int tap = tbot_mounted_cadence(mode, tbot_taps(wp.cfgName ? uname_str(wp.cfgName) : 0));
  // PULSE needs the weapon's own "ready" (Weapon.CanFire's !bWeaponFireLockout); unread, it holds.
  const int lockout = weapon ? rd_bool(weapon, class_of(weapon), N_LOCKOUT) : -1;
  const int pulse = tbot_mounted_cadence(mode, !tap && s_cad.cfg && s_cad.mode == TBOT_FIRE_PULSE && lockout >= 0);
  if (!holds) s_holdLogged = 0;
  int shots = -1;
  if (player && on && rd_int(player, N_ACCSHOTS, &shots)) {
    const int d = s_lastShots >= 0 ? shots - s_lastShots : 0;
    if ((holds || s_prevHolds) && d > 0 && d < 64) s_heldShots += (unsigned)d;
    s_lastShots = shots;
    // The per-shot probe line. Hitscan only: a projectile lands late, and the shrink and freeze rays hurt nobody's Health.
    // Not mounted: a turret's shot never reaches RecordShot.
    // Pawns only: a ship's shots are gated by the weapon table instead (aircraft.h).
    if (holds && target && wp.kind == AIMW_HITSCAN && !mounted && !aim_is_ship(target)) spawnbox_probe_feed(target, shots);
  }
  int impulse = -1, wstate = -1;
  if (weapon && wp.kind == AIMW_MELEE && rd_int(weapon, N_WSIMPULSE, &impulse) && rd_byte(weapon, N_WSTATE, &wstate)) {
    if ((holds || s_prevHolds) && tbot_punch_began(s_lastImpulse, impulse, wstate)) ++s_heldPunches;
    s_lastImpulse = impulse;
  } else {
    s_lastImpulse = -1;
  }
  if (holds) s_heldMs += (unsigned)(dt < 0 ? 0 : dt > TBOT_FRAME_MS_MAX ? TBOT_FRAME_MS_MAX : dt);
  s_prevHolds = holds;
  if (!player) {
    s_press.ours = s_press.pendingUp = s_press.upOwed = 0;   // no pawn to call: forgotten
  } else {
    const int act = tbot_act(&s_press, gateOpen, fire, tap, userDown, firing > 0, now, pulse, lockout == 0);
    if (act == TB_DOWN) {
      if (press(player, tbot_press_fn(mode, 1))) {
        ++s_downs;
        if (mode) ++s_enables;
        if (pulse) ++s_pulses;
        if (!s_holdLogged && s_pressLogs < LOGS_PER_LEVEL) {
          s_pressLogs++;
          s_holdLogged = 1;
          const int cls = class_name_of(target);
          char offBuf[48] = "";
          if (g_cfg.tbotWhen == TBOT_WHEN_AIMBOT) {
            const float off = aim_view_off_deg(target);
            if (off >= 0.f) _snprintf(offBuf, sizeof offBuf, ", %.1f degrees off the crosshair", off);
            offBuf[sizeof offBuf - 1] = 0;
          }
          np_log("tbot: %ls (%s) on %ls after %d ms held, weapon %ls%s%s", tbot_press_fn(mode, 1),
                 tbot_when_label(g_cfg.tbotWhen),
                 cls ? uname_str(cls) : L"?", heldMs, wp.cfgName ? uname_str(wp.cfgName) : L"?",
                 tap ? ", tapped" : pulse ? ", pulsed" : "", offBuf);
        }
        if (target && !aim_is_ship(target)) spawnbox_helper_event(target, "FireDown", 0);   // capped there
      } else {
        s_press.ours = 0;   // not called: nothing to release
      }
    } else if (act == TB_UP) {
      const int called = press(player, tbot_press_fn(mode, 0));
      tbot_up_result(&s_press, called);
      if (called) ++s_ups;
      if (called && mode) ++s_disables;
    } else if (act == TB_FORGET) {
      ++s_forgets;
    }
  }
  if (s_sub >= 0) pehook_enable(s_sub, on);
  uname_dlog_flush();
  if (++s_frames < 600) return;
  s_frames = 0;
  if (g_cfg.tbotOn || s_downs || s_ups)
    np_log("tbot: 600 frames: %s, %s; UpdateLookHitActor %u; condition held %u frame(s); crosshair on an enemy %u, "
           "an ally or neutral %u, another actor %u, an attachment of a pawn %u, a ship %u, an achievement target %u (held back by a hostile candidate %u), animated away without a render box, teleporting or invulnerable %u (teleporting %u, invulnerable %u, player damage 0 %u, neutral zone %u, wrong damage type %u, expanded or popping %u); FireDown %u, FireUp %u (late %u, "
           "retried %u, owed %d), forgotten %u (open %d); Locked not covered %u frame(s), followed the aimbot %u "
           "frame(s); Aimbot waiting for the turn %u frame(s), ray unresolved %u; mounted %u frame(s), DnControlFireEnable %u, "
           "DnControlFireDisable %u; overkill: fire held back on a doomed target %u time(s) over %u frame(s), marks cleared with the "
           "target alive %u; fallback %u frame(s); shots while held %u in %.2f s (%.1f/s; %u pulse(s), %s); punches while held %u "
           "(WeaponStateImpulse into state 10); fire held during a reload %u frame(s); held fire: blast would reach Duke %u "
           "frame(s) (%u hold(s); not judged %u, trace budget spent %u; %u splash trace(s), %.3f ms, max %.3f ms a frame); "
           "%u call(s) refused, %u fault(s)", g_cfg.tbotOn ? (tbot_refused() ? "unavailable" : "on") : "off",
           tbot_when_label(g_cfg.tbotWhen), s_lookEvents, s_holdFrames, s_xEnemy, s_xAlly, s_xOther, s_xAttached,
           s_xShip, s_xAch, s_xAchHeld, s_xExcluded, s_xTele, s_xNoDmg, s_xDmg0, s_xNeutral, s_xWrongType, s_xExpanded, s_downs, s_ups, s_press.lateUps, s_press.upRetries, s_press.upOwed, s_forgets, s_press.ours, s_uncovered,
           s_followAimbot, s_waitTurn, s_rayUnres, s_mountFrames, s_enables, s_disables, s_saved, s_savedFrames,
           overkill_take_misses(), s_fallback, s_heldShots, s_heldMs / 1000.0, s_heldMs ? s_heldShots * 1000.0 / s_heldMs : 0.0,
           s_pulses,
           !s_cad.cfg ? "no weapon" : s_cad.mode == TBOT_FIRE_PULSE ? "pulse" : "hold", s_heldPunches, s_reloadFrames,
           s_blastFrames, s_blastHolds, s_blastFails, s_blastCapped, s_blastTraces, s_blastMs, s_blastMaxMs,
           s_refusedCalls, s_faults);
  s_lookEvents = s_holdFrames = s_xEnemy = s_xAlly = s_xOther = s_downs = s_ups = s_forgets = s_fallback = 0;
  s_xShip = s_xAch = s_xAchHeld = 0;
  s_heldShots = s_heldMs = s_pulses = s_heldPunches = s_xAttached = s_followAimbot = s_waitTurn = s_rayUnres = s_xExcluded = s_xTele = s_xNoDmg = s_xDmg0 = s_xNeutral = s_xWrongType = s_xExpanded = 0;
  s_refusedCalls = s_faults = s_uncovered = s_mountFrames = s_enables = s_disables = s_saved = s_savedFrames = s_reloadFrames = 0;
  s_press.lateUps = s_press.upRetries = 0;
  s_blastFrames = s_blastHolds = s_blastTraces = s_blastFails = s_blastCapped = 0;
  s_blastMs = s_blastMaxMs = 0.0;
}

const char* tbot_refused(void) { return s_why ? s_why : aim_refused(); }

const void* tbot_target(void) { return s_st.holding ? s_st.target : 0; }

int tbot_pressing(void) { return s_press.ours; }

// Silent aim (aim.h; aim.md sections 1-3, 5). Game thread. Once a frame: the gate, the held weapon and the target,
// visible by the bullet's own trace flags. In the dnGame ProcessEvent slot, on the player only: UpdateWeaponTraceVars
// writes CurrentShake toward the target, RecordShot puts the game's value back; mounted guns likewise (mountaim.h).
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "aim.h"
#include "mountaim.h"
#include "aimbot.h"
#include "aircraft.h"
#include "achhelp.h"   // the achievement targets: the last resort after the ships
#include "splash.h"
#include "spawnbox.h"
#include "overkill.h"
#include "tbot.h"   // tbot_attached_pawn: a point trace that hits the target's own attachment is clear
#include "world.h"
#include "view.h"
#include "pehook.h"
#include "scall.h"
#include "scallparms.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "engine.h"
#include "esp_layout.h"   // ESP_UU_PER_METRE: the slider's metres
#include "config.h"
#include "mpgate.h"
#include "log.h"

// UMesh's BonePartMapper class (+0x7C): UMesh::GetBodyPartForBone (Engine.dll!0x1030CC70, aim.md section 2) reads it
// there; no property names it. Read for the per-class log line only.
static const int UMESH_PARTMAPPER = 0x7C;

enum { N_SHAKE, N_VIEWROT, N_CURFOV, N_DEFFOV, N_VIEWLOC, N_LOCATION, N_HEALTH, N_GRABBING, N_ATTACHED, N_VIEWMAPPER,
       N_INTERACTIVE, N_WEAPON, N_WEAPONCONFIG, N_VELOCITY, N_MAXTRACE, N_PROJCLASS, N_SPEED, N_MAXSPEED, N_INFAMMO,
       N_AMMO, N_CHARGE, N_FROZEN, N_MESH, N_MESHINST, N_EYEBONE, N_LOCKBONE, N_HEAD, N_ACCSHOTS, N_ACCHITS,
       N_LOOKHIT, N_LOOKDIST, N_AMMOEXP, N_OWNER, N_MOUNTPARENT, N_MELEETRACE, N_MELEESHRUNK, N_MELEEEXT,
       N_SHRINKSCALE, N_INPUTHOOK, N_CRSTATE, N_MINPITCH, N_MAXPITCH, N_MINYAW, N_MAXYAW, N_CLAMPYAW, N_ROTPITCH,
       N_ROTYAW, N_TVROT, N_TVLOC, N_SHOTDIST, N_GRENVEL, N_CALCROT, N_VIEWOFFSET, N_ACCEL, N_LIFESPAN, N_MATCHSCALE,
       N_DRAWSCALE, N_PHYSVOL, N_GRAVITY, N_TERMINAL, N_BOUNCE, N_MUZZLENAMES, N_MUZZLEIDX, N_TRACEDT, N_MELEEDT,
       N_DAMAGECLASS, N_BEAMLEN, N_THRESP, N_PENCOUNT, N_PENDIST, N_ROTATION, N_INSTANTAIM,
       N_DMGRADIUS, N_DMGDIRECT, N_GRENRADIUS, N_COLLRADIUS, N_COLLHEIGHT, N_BREAKGLASS, N_UNBREAKABLE, N_PHYSICS, N_UPBOOST,
       N_GRAVSCALE, N_EXPLODEANY, N_EXPDELAY, N_EXPVAR, N_FIRELEFT, N_PROJROT, N_COUNT };
static const wchar_t* const NAMES[N_COUNT] = {
  L"CurrentShake", L"ViewRotation", L"CurrentFOV", L"DefaultFOV", L"CalcedViewLocation", L"Location", L"Health",
  L"bGrabbing", L"bAttachedToUsableSomething", L"ViewMapper", L"InteractiveDecoration", L"Weapon", L"WeaponConfig",
  L"Velocity", L"MaxTraceDistance", L"ProjectileClass", L"Speed", L"MaxSpeed", L"bInfiniteAmmo", L"Ammo", L"Charge",
  L"FrozenState", L"Mesh", L"MeshInstance", L"EyeBone", L"LockOnBone", L"Head", L"AccuracyShots", L"AccuracyHits",
  L"DirectLookHitActor", L"LookHitDistance", L"AmmoExpenditure", L"Owner", L"MountParent", L"MeleeTraceDistance",
  L"MeleeTraceDistanceShrunk", L"MeleeHitExtent", L"ShrinkScale", L"InputHookActor", L"CurrentRemapperState",
  L"MinPitch", L"MaxPitch", L"MinYaw", L"MaxYaw", L"bClampYaw", L"RotatePitch", L"RotateYaw",
  L"TurretViewRotationOffset", L"TurretViewLocationOffset", L"ShotDistance", L"GrenadeVelocity",
  L"CalcedViewRotation", L"ViewOffset", L"AccelerationScaler", L"LifeSpan", L"bMatchInstigatorScale", L"DrawScale",
  L"PhysicsVolume", L"Gravity", L"TerminalVelocity", L"bBounce", L"MuzzleNames", L"MuzzleIndex", L"TraceDamageType",
  L"MeleeDamageType", L"DamageClass", L"BeamMaxLength", L"TraceFireHitResponse", L"MaxTracePenetrationCount",
  L"MaxPenetrationDistance", L"Rotation", L"bUseInstantAimRate", L"DamageRadius", L"bDamageDirectly",
  L"GrenadeDamageRadius", L"CollisionRadius", L"CollisionHeight", L"bBreakGlass", L"bUnBreakable", L"Physics", L"UpwardsBoost",
  L"GravityScale", L"bExplodeOnAnyImpact", L"ExplosionDelay", L"ExplosionDelayVariance", L"bFireLeft",
  L"ProjectileRotation" };
// Names up to N_INTERACTIVE are required (the write and the gate); the rest refuse only what uses them.
static const int N_REQUIRED = N_INTERACTIVE + 1;
static int s_n[N_COUNT];

static const char* s_why;
static int s_subOpen = -1, s_subClose = -1;
static int s_live;                 // the subscriptions are wanted this frame
static unsigned s_level;
static int s_checked;              // this level's resolve check ran
static int s_gate = AIM_G_OFF;     // this frame's gate for Silent aim
static int s_gateNow = AIM_G_OFF;  // the player's gate with the switch on (aim_gate_now)
static int s_ready;                // aim_redirect_ready
static AimBracket s_br;
// The mounted guns' bracket (mountaim.h): FireEffects, BeginState and AnimCallback_AnimEnd, each before and after.
enum { MS_SUBS = 2 * MS_HOOK_COUNT };
static MsBracket s_mb;
static int s_msSub[MS_SUBS] = { -1, -1, -1, -1, -1, -1 };
static int s_msLive, s_msReady, s_msLogs;
// A launcher shot's grenade, read in the next frame's object snapshot (the spawn is not in this frame's).
static struct { int on; const void* gun; unsigned frame; int shot; float dir[3]; } s_msGren;
static AimVisCache s_vis;

// The held weapon's config class: meleeExt: MeleeHitExtent max; hurtsAir: hits aircraft; projName: ProjectileClass
// name; pm, matchScale, spawnP/Y: model, bMatchInstigatorScale, spawn rotator; penCount/penDist: TraceFire's pass
// budget; splash*, breakGlass: blast; kindBy: how the kind was found; projPhys: Physics (-1 unread).
struct WeaponInfo { const void* cfg; int cfgName, kind, kindBy, projPhys; float maxTrace; int ammoAmount, penCount; float penDist;
                    float meleeTrace, meleeShrunk, meleeExt; int hurtsAir, projName; AimProjModel pm;
                    int matchScale, spawnP, spawnY; const void* dmgType; const char* dmgSrc;
                    int splash; float splashR; int splashDoubles, breakGlass;
                    float lobBoost, lobGScale, lobDelay, lobVar; };   // a lob's class defaults (aim_lob_model; -1 unread)
static WeaponInfo s_weap;
// This frame: the held weapon (validated), the fists' reach (aim_melee_reach), IsCompletelyOutOfAmmo's answer.
static const void* s_weapObj;
static float s_reach;
static int s_outOfAmmo = -1;

// The mounted turret this frame (read_mount; docs/re/turrets.md): its remapper and gun, the gun as a weapon, its
// live limits and RotatePitch/RotateYaw (limitsOk: all read). on is aim_turret_mounted. pm: the gun's projectile
// model; muzzle: the launcher's next muzzle bone (its grenade's spawn point), muzzleOk when read and plausible.
struct Mount { int on; const void* remapper; const void* gun; int gunName, tur; AimTurretKind k; AimTurretLimits lim;
               int limitsOk, rotP, rotY, baseOk, base[3], instant; AimProjModel pm; float muzzle[3]; int muzzleOk; float gravity[3];
               const void* dmgType; const char* dmgSrc; int splash; float splashR; int breakGlass; };
static Mount s_mt;
static int s_mountLogs;
static int s_beamName;   // the SuperTurret's mount_beam bone name (super_muzzle): a map's name, looked up again each level
// The last weapon and damage type logged (one line per change, LOGS_PER_LEVEL a level).
static int s_dtLogWeapon, s_dtLogs;
static const void* s_dtLogType;
static AimMountLog s_mlog;   // the debounced attach/dismount log (aim_mount_log_step)
static int s_mlogName;       // the logged gun's class
// Counted per 600 frames: frames mounted, candidates outside the turret's limits.
static unsigned s_mountFrames, s_outOfLimits;

// The held weapon, or the mounted gun (the on-foot weapon is lowered while mounted).
static int w_kind(void) { return s_mt.on ? s_mt.k.kind : s_weap.kind; }
static float w_max(void) { return s_mt.on ? s_mt.k.maxTrace : s_weap.maxTrace; }
static int w_hurts_air(void) { return s_mt.on ? ac_weapon_hurts(0, 0, s_mt.tur) : s_weap.cfg && s_weap.hurtsAir; }
// The player's DrawScale over its class default this frame: a shrunk player's projectiles fly slower
// (Projectile.PostVerifySelf, bMatchInstigatorScale). 1 when unread.
static float s_drawRatio = 1.f;
// A lob's gravity and speed clamp: the player's PhysicsVolume Gravity.Z and TerminalVelocity this frame (0 unread), where
// the spit is spawned; read while a lob is held.
static float s_footGravZ, s_footTerm;
// Counted per 600 frames: intercepts solved (ballistic among them), no solution, candidates passed over out of reach.
static unsigned s_icSolved, s_icBallistic, s_icNone, s_unreachable, s_icCalls;
// The frame counter and the target last counted (one count a frame per target); silent aim's shots out of reach.
static unsigned s_frameNo, s_icFrame, s_shotsOutOfReach;
static const void* s_icTarget;
static double s_icMs, s_icMaxMs;

// The held or mounted gun's projectile model (aim.md section 8); NONE for every other kind.
static AimProjModel w_model(void) {
  if (s_mt.on) return s_mt.pm;
  AimProjModel m = s_weap.pm;
  if (!s_weap.cfg || s_weap.kind != AIMW_PROJECTILE) m.kind = AIM_PM_NONE;
  else if (s_weap.kindBy == AIMW_BY_LOB)
    return aim_lob_model(m.speed, s_weap.lobBoost, s_weap.lobGScale, s_footGravZ, s_footTerm, s_weap.lobDelay,
                         s_weap.lobVar, s_drawRatio);
  if (m.kind == AIM_PM_STRAIGHT && s_weap.matchScale && s_drawRatio > 0.f) {
    m.speed *= s_drawRatio;
    m.maxSpeed *= s_drawRatio;
  }
  return m;
}

// This frame's target; the shot reads its point afresh in the handler.
static const void* s_target;
static int s_targetShip;   // s_target is a ship of the last-resort pass (aircraft.h), not a pawn
static int s_targetAch;    // s_target is an achievement target (achhelp.h), after the ships
static int s_pawnCands = -1;   // this frame's hostile pawn candidates (aim_pawn_cands), -1 unknown
// Boss priority (aim.md section 12): this frame's verdict (AIM_BOSS_*; s_bossEval 0 when not judged) and the pawn.
// s_bossLastCat: the last logged none/on/off (0/1/2); per level s_bossLogs lines. Per 600 frames: frames by verdict,
// the Triggerbot's frames held off an add and how many holds.
static int s_bossEval, s_bossState, s_bossCls, s_bossLastCat, s_bossLogs;
static const void* s_boss;
static const void* s_bossLogged;
static unsigned s_bossFrames[AIM_BOSS_COUNT], s_bossSkipFrames, s_bossSkips;
static int s_targetClsName;
static float s_targetAngle, s_targetDist;
static int s_mkOk;
static float s_mkRaw[3], s_mkLed[3];

// The head bone per pawn class (aim_head_choice), resolved and logged once.
struct HeadRec { const void* cls; int clsName, bone; };
static HeadRec s_head[64];
static int s_headN;

// Per level: log line caps.
static int s_shotLogs, s_targetLogs, s_gateLogs;
static const int LOGS_PER_LEVEL = 32;

// Counted per 600 frames.
static unsigned s_frames, s_uwtv, s_rec, s_redirected, s_noTarget, s_noAmmo, s_gated, s_picked, s_traces,
    s_hidden, s_allyBlocked, s_frozenSkipped, s_boneReads, s_boneFails, s_faults, s_refusedCalls, s_outOfCone;
// Hostile pawns left out (spawnbox_excluded): animated away from Location with no render box; candidate-frames.
// Admitted while in a cut-scene, and aimed at the render box (the body away from Location); box probes, clear ones.
static unsigned s_cutAdmitted, s_boxAimed, s_boxTraces;
static unsigned s_exFar, s_exTele, s_exNoDmg, s_exDmg0, s_exNeutral, s_exWrongType;   // (teleporting; bNoDamage; player damage 0; neutral zone; the held gun's damage type)
static unsigned s_exExpanded;   // fully expanded or popping while the Expander is up (bloat.h)
// Visibility probes and the body point each found (AIM_PT_*; AIM_PT_NONE: none visible); point traces and their cost.
// s_picks[PICK_BOX]: probes clear at the render-box centre; [PICK_HELPER] at a mounted collision actor (mountcol.h),
// whose traces s_helperTraces counts.
enum { PICK_BOX = AIM_PT_COUNT, PICK_HELPER = AIM_PT_COUNT + 1 };
static unsigned s_probes, s_picks[AIM_PT_COUNT + 2], s_ptTraces, s_helperTraces;
// Hidden point traces by what they hit (aim_hit_category), and their capped per-level lines.
static unsigned s_hidHits[AIM_HIT_COUNT];
static int s_hidLogs;
// Clear shot traces the old player-blocking trace called hidden (a blocker bullets pass), and their capped lines.
static unsigned s_skipped;
// Frames a ship was the target; damageable ships left out because the held or mounted gun cannot hurt them.
static unsigned s_shipFrames, s_shipWeaponSkips;
// The ship pass's own rejections (kept apart from the pawn counters): outside the cone, the turret's limits, an ally in
// the line, hidden.
static unsigned s_shipCone, s_shipLimits, s_shipAlly, s_shipHidden;
// Frames the target ship was kept past the gun's strict limits by its hull's slack (ac_limit_slack).
static unsigned s_shipLimitKeeps;
// The achievement pass (pick_ach): frames one was the target, and its own rejections: the gun's damage type, doomed,
// range or screen, the turret's limits, the cone, an ally in the line, hidden. s_achHidLogs: per level.
static unsigned s_achFrames, s_achType, s_achDoomed, s_achRange, s_achLimits, s_achCone, s_achAlly, s_achHidden;
static int s_achHidLogs;
// The previous target ship's own tests this frame (ac_lost_reason) and the values its flip line prints: the pawn pass,
// the gun's limit test (want: the launch rotator; cam: the camera it was asked from) and the cone.
static AcShipVerdict s_sv;
struct ShipFlipVals { int pawnCands, pawnUnknown, hurts, haveLimits, strict, slackOk, wantP, wantY, camP, camY,
                      slackP, slackY; float dist, range, angle; };
static ShipFlipVals s_sf;
static int s_flipLogs;                         // per level
static const int FLIP_LOGS_PER_LEVEL = 64;
static unsigned s_shipLost[AC_LOST_COUNT];     // per 600 frames, by ac_lost_reason
// This frame's gun direction (pitch, yaw) for the turret's limit tests (aim_gun_view), and the frames it came from
// the gun's own rotation or, unread or tilted, the camera (per 600 frames).
static int s_gunView[2], s_gunViewFromGun;
static unsigned s_gunViewGun, s_gunViewCam;
// Doomed candidates passed over (overkill.h): pawns, ships; s_passedDoomed is this frame's (aim_passed_doomed).
static unsigned s_exDoomed, s_shipDoomed;
static int s_passedDoomed;
// Clear bullet traces by what they hit: the target (or an attachment), or nothing (docs/re/aim.md section 3).
static unsigned s_clearTarget, s_clearNoHit;
static int s_skipLogs;
static const int HIDDEN_LOGS_PER_LEVEL = 40;
static double s_ptMs;
static unsigned s_dblTraces;   // the [aim] debug_double_trace re-traces, kept out of s_ptTraces and s_ptMs
static double s_dblMs;
static int s_ptLogs;   // per level: "head hidden -> hips" lines
static double s_traceMs, s_traceMaxMs, s_boneMs, s_handlerMs, s_handlerMaxMs;
static unsigned s_windowStart;
// The first 64 LineOfSightTo calls' QueryPerformanceCounter cost, logged once (aim.md section 6).
static unsigned s_ftFirstN;
static double s_ftFirstSum, s_ftFirstMax;
static LARGE_INTEGER s_qpf;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static double ms_since(LARGE_INTEGER t0) {
  LARGE_INTEGER t1;
  QueryPerformanceCounter(&t1);
  if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
  return (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)s_qpf.QuadPart;
}

static int nm(int k) {
  if (!s_n[k]) s_n[k] = uname_find(NAMES[k]);
  return s_n[k];
}

static void refuse(const char* why) {
  if (s_why) return;
  s_why = why;
  np_log("aim: Silent aim unavailable: %s", why);
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

static int name_of(const void* o) {
  __try {
    return o && uobj_valid(o) ? uobj_name(o) : 0;
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
}

static UProp prop(const void* cls, int k) {
  UProp none = { 0, 0, UW_OTHER, 0, 0, 0 };
  const int n = nm(k);
  if (!cls || !n) return none;
  __try {
    return uprop_get(cls, n);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return none;
  }
}

// o's property k (declared on cls's chain; o may be a class default) as a validated object, a float, an int, a
// bool, a byte, a name or a 12-byte struct. False when it does not resolve or a read faulted.
static bool rd_obj(const void* o, const void* cls, int k, const void** out) {
  *out = 0;
  const UProp p = prop(cls, k);
  if (!p.ok || (p.type != UW_OBJECT && p.type != UW_CLASS)) return false;
  __try {
    *out = uw_ptr(o, p.offset);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
  if (*out && !live(*out)) *out = 0;
  return true;
}

static bool rd_raw(const void* o, const void* cls, int k, int type, int size, void* out) {
  const UProp p = prop(cls, k);
  if (!p.ok || p.type != type || p.elemSize < size) return false;
  __try {
    memcpy(out, (const char*)o + p.offset, (size_t)size);
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

static bool rd_float(const void* o, int k, float* out) { return rd_raw(o, class_of(o), k, UW_FLOAT, 4, out); }
static bool rd_vec(const void* o, int k, float* out3) { return rd_raw(o, class_of(o), k, UW_STRUCT, 12, out3); }
static bool rd_rot(const void* o, int k, int* out3) { return rd_raw(o, class_of(o), k, UW_STRUCT, 12, out3); }
static bool rd_int(const void* o, int k, int* out) { return rd_raw(o, class_of(o), k, UW_INT, 4, out); }

static bool rd_byte(const void* o, int k, int* out) {
  unsigned char b = 0;
  if (!rd_raw(o, class_of(o), k, UW_BYTE, 1, &b)) return false;
  *out = b;
  return true;
}

// A bool of o declared on cls's chain (o may be cls's default object): -1 unread.
static int rd_bool_def(const void* o, const void* cls, int k) {
  const UProp p = prop(cls, k);
  if (!o || !p.ok || p.type != UW_BOOL || !p.bitmask) return -1;
  __try {
    return (uw_int(o, p.offset) & (int)p.bitmask) != 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return -1;
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

// A NameProperty: 4 bytes, the name index (UW_OTHER to the walker, so its property class is checked by name).
static bool rd_name(const void* o, int k, int* out) {
  const UProp p = prop(class_of(o), k);
  static int s_nameProp;
  if (!s_nameProp) s_nameProp = uname_find(L"NameProperty");
  if (!p.ok || p.elemSize != 4 || p.type != UW_OTHER || !p.field || !s_nameProp ||
      name_of(class_of(p.field)) != s_nameProp)
    return false;
  __try {
    *out = uw_int(o, p.offset);
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

static bool wr_rot(void* o, int k, const int* v3) {
  const UProp p = prop(class_of(o), k);
  if (!p.ok || p.type != UW_STRUCT || p.elemSize != 12) return false;
  __try {
    memcpy((char*)o + p.offset, v3, 12);
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

static void* default_of(const void* cls) {
  __try {
    return cls ? engine_get_default_object((void*)cls) : 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

// --- Script calls (scall: ProcessEvent with parms built from each UFunction) ---------------------------------------------------
// Pawn.LineOfSightTo(Other, bCheckFOV=false) (native 904) on the player: the game's sight check, which a closed door
// blocks where FastTrace's world-only flags let Silent aim see through (aim.md 3). 1 visible, 0 not, -1 unresolved.
static int line_of_sight(void* player, const void* pawn) {
  const ScallFn f = scall_find(player, L"LineOfSightTo");
  static const void* s_fn;
  static ScallParm s_other, s_rv;
  if (!f.ok) return -1;
  if (f.fn != s_fn) {
    ScallParm o, fov, rv;
    if (!scall_parm(f, L"Other", L"ObjectProperty", &o) || o.size != 4 ||
        !scall_parm(f, L"bCheckFOV", L"BoolProperty", &fov) || fov.size != 4 ||
        !scall_parm(f, L"ReturnValue", L"BoolProperty", &rv) || rv.size != 4)
      return -1;
    s_other = o;
    s_rv = rv;
    s_fn = f.fn;
  }
  unsigned char parms[64];
  memset(parms, 0, sizeof parms);   // bCheckFOV false
  if (f.parmsSize > sizeof parms || !scallparms_put(parms, f.parmsSize, s_other.offset, &pawn, 4)) return -1;
  LARGE_INTEGER t0;
  QueryPerformanceCounter(&t0);
  const int called = scall_call(player, f, parms, sizeof parms);
  const double ms = ms_since(t0);
  if (!called) {
    ++s_refusedCalls;
    return 0;
  }
  ++s_traces;
  s_traceMs += ms;
  if (ms > s_traceMaxMs) s_traceMaxMs = ms;
  if (s_ftFirstN < 64) {
    s_ftFirstSum += ms;
    if (ms > s_ftFirstMax) s_ftFirstMax = ms;
    if (++s_ftFirstN == 64)
      np_log("aim: LineOfSightTo through ProcessEvent, first 64 calls: avg %.4f ms, max %.4f ms", s_ftFirstSum / 64.0,
             s_ftFirstMax);
  }
  int r;
  memcpy(&r, parms + s_rv.offset, 4);
  return r != 0;
}

// Actor.GetBodyPartForBone(Mesh, BoneName) (static native 570) on the pawn: the EPawnBodyPart, -1 when not called.
static int body_part(void* pawn, const void* mesh, int bone) {
  if (!mesh || !bone) return -1;
  const ScallFn f = scall_find(pawn, L"GetBodyPartForBone");
  if (!f.ok) return -1;
  ScallParm pm, pb, rv;
  if (!scall_parm(f, L"Mesh", L"ObjectProperty", &pm) || pm.size != 4 ||
      !scall_parm(f, L"BoneName", L"NameProperty", &pb) || pb.size != 4 ||
      !scall_parm(f, L"ReturnValue", L"ByteProperty", &rv) || rv.size != 1)
    return -1;
  unsigned char parms[64];
  memset(parms, 0, sizeof parms);
  if (f.parmsSize > sizeof parms || !scallparms_put(parms, f.parmsSize, pm.offset, &mesh, 4) ||
      !scallparms_put(parms, f.parmsSize, pb.offset, &bone, 4))
    return -1;
  if (!scall_call(pawn, f, parms, sizeof parms)) {
    ++s_refusedCalls;
    return -1;
  }
  return parms[rv.offset];
}

// MeshInstance.BoneGetTranslate(BoneName, bAbsolute=true, bDefault=false, Scale=1) (native 533): the bone's world
// position (aim.md section 2). Through ProcessEvent the native reads every parameter from the block, so Scale is
// passed as 1. 1 read, 0 not.
static int bone_world(void* pawn, int bone, float out[3]) {
  const void* mi = 0;
  if (!bone || !rd_obj(pawn, class_of(pawn), N_MESHINST, &mi) || !mi) return 0;
  const ScallFn f = scall_find((void*)mi, L"BoneGetTranslate");
  static const void* s_fn;
  static ScallParm s_bn, s_abs, s_def, s_scale, s_rv;
  if (!f.ok) return 0;
  if (f.fn != s_fn) {
    ScallParm bn, ab, df, sc, rv;
    if (!scall_parm(f, L"BoneName", L"NameProperty", &bn) || bn.size != 4 ||
        !scall_parm(f, L"bAbsolute", L"BoolProperty", &ab) || ab.size != 4 ||
        !scall_parm(f, L"bDefault", L"BoolProperty", &df) || df.size != 4 ||
        !scall_parm(f, L"Scale", L"FloatProperty", &sc) || sc.size != 4 ||
        !scall_parm(f, L"ReturnValue", L"StructProperty", &rv) || rv.size != 12) {
      np_log("aim: MeshInstance.BoneGetTranslate's parameters did not match; heads aim at the body");
      s_fn = f.fn;
      s_rv.size = 0;
      return 0;
    }
    s_bn = bn;
    s_abs = ab;
    s_def = df;
    s_scale = sc;
    s_rv = rv;
    s_fn = f.fn;
  }
  if (s_rv.size != 12) return 0;
  unsigned char parms[64];
  memset(parms, 0, sizeof parms);
  const int one = 1;
  const float scale = 1.f;
  if (f.parmsSize > sizeof parms || !scallparms_put(parms, f.parmsSize, s_bn.offset, &bone, 4) ||
      !scallparms_put(parms, f.parmsSize, s_abs.offset, &one, 4) ||
      !scallparms_put(parms, f.parmsSize, s_scale.offset, &scale, 4))
    return 0;
  LARGE_INTEGER t0;
  QueryPerformanceCounter(&t0);
  const int called = scall_call((void*)mi, f, parms, sizeof parms);
  s_boneMs += ms_since(t0);
  ++s_boneReads;
  if (!called) {
    ++s_refusedCalls;
    return 0;
  }
  memcpy(out, parms + s_rv.offset, 12);
  return 1;
}

// Pawn.GetCameraLocation() (native 872) on the player: where GetWeaponAim starts the shot. 1 read.
static int camera_location(void* player, float out[3]) {
  const ScallFn f = scall_find(player, L"GetCameraLocation");
  ScallParm rv;
  if (!f.ok || !scall_parm(f, L"ReturnValue", L"StructProperty", &rv) || rv.size != 12) return 0;
  unsigned char parms[64];
  memset(parms, 0, sizeof parms);
  if (f.parmsSize > sizeof parms) return 0;
  if (!scall_call(player, f, parms, sizeof parms)) {
    ++s_refusedCalls;
    return 0;
  }
  memcpy(out, parms + rv.offset, 12);
  return 1;
}

// A script function without parameters returning a bool (Pawn.ShrinkingActive, native 921; Weapon.IsCompletelyOutOfAmmo)
// on obj: 1 or 0, -1 when it does not resolve or was refused. The return slot is cached per UFunction.
struct BoolFnCache { const void* fn; ScallParm rv; int ok; };
static int call_bool(void* obj, const wchar_t* name, BoolFnCache* c) {
  const ScallFn f = scall_find(obj, name);
  if (!f.ok) return -1;
  if (f.fn != c->fn) {
    ScallParm rv = { 0, 0 };
    c->fn = f.fn;
    c->ok = scall_parm(f, L"ReturnValue", L"BoolProperty", &rv) && rv.size == 4;
    c->rv = rv;
    if (!c->ok) np_log("aim: %ls's ReturnValue did not match; not called", name);
  }
  if (!c->ok) return -1;
  unsigned char parms[16];
  memset(parms, 0, sizeof parms);
  if (f.parmsSize > sizeof parms || c->rv.offset + 4 > f.parmsSize) return -1;
  if (!scall_call(obj, f, parms, sizeof parms)) {
    ++s_refusedCalls;
    return -1;
  }
  int r;
  memcpy(&r, parms + c->rv.offset, 4);
  return r != 0;
}

// --- The head ----------------------------------------------------------------------------------------------------
// The pawn class's head bone name, or 0 (aim at the body). Resolved on its first pawn and logged with the mesh's
// BonePartMapper and each bone's part (aim.md section 6).
static int head_bone(void* pawn) {
  const void* cls = class_of(pawn);
  const int clsName = name_of(cls);
  if (!cls) return 0;
  for (int i = 0; i < s_headN; i++)
    if (s_head[i].cls == cls && s_head[i].clsName == clsName) return s_head[i].bone;
  const void* mesh = 0;
  rd_obj(pawn, cls, N_MESH, &mesh);
  int eye = 0, lock = 0;
  rd_name(pawn, N_EYEBONE, &eye);
  rd_name(pawn, N_LOCKBONE, &lock);
  const int headPart = body_part(pawn, mesh, nm(N_HEAD));
  const int eyePart = body_part(pawn, mesh, eye), lockPart = body_part(pawn, mesh, lock);
  const int choice = aim_head_choice(headPart, eyePart, lockPart);
  const int bone = choice == AIM_HB_HEAD ? nm(N_HEAD) : choice == AIM_HB_EYE ? eye : choice == AIM_HB_LOCK ? lock : 0;
  const void* mapper = 0;
  if (mesh) {
    __try {
      mapper = uw_ptr(mesh, UMESH_PARTMAPPER);
    } __except (av_only(GetExceptionCode())) {
      ++s_faults;
    }
    if (!live(mapper)) mapper = 0;
  }
  if (s_headN < 64) {
    const HeadRec r = { cls, clsName, bone };
    s_head[s_headN++] = r;
  }
  np_log("aim: %ls: mesh %ls, BonePartMapper %ls; part of Head %d, EyeBone %ls %d, LockOnBone %ls %d -> %ls",
         uname_str(clsName), mesh ? uname_str(name_of(mesh)) : L"None", mapper ? uname_str(name_of(mapper)) : L"None",
         headPart, uname_str(eye), eyePart, uname_str(lock), lockPart, bone ? uname_str(bone) : L"the body");
  return bone;
}

// One body point of a pawn (AIM_PT_*): the head bone when the class has one and it lies on the pawn; chest, hips and
// knees on the cylinder's axis (aim_body_point). AIM_PT_NONE is Aim at's choice (the head, else the chest). 0 when
// the point does not exist (no head) or Location did not read. *bone is the bone used, 0 for the body.
static int point_of(void* pawn, int pt, float out[3], int* bone) {
  *bone = 0;
  float loc[3], r = 0.f, h = 0.f;
  if (!rd_vec(pawn, N_LOCATION, loc)) return 0;
  const WPawn* pw = 0;
  const int n = world_pawns(&pw);
  for (int i = 0; i < n; i++)
    if (pw[i].obj == pawn) { r = pw[i].radius; h = pw[i].height; break; }
  if (pt == AIM_PT_NONE) {
    if (g_cfg.aimPoint == AIM_AT_HEAD && point_of(pawn, AIM_PT_HEAD, out, bone)) return 1;
    pt = AIM_PT_CHEST;
  }
  if (pt != AIM_PT_HEAD) {
    aim_body_point(pt, loc, h, out);
    return 1;
  }
  const int hb = head_bone(pawn);
  float hp[3];
  if (!hb || !bone_world(pawn, hb, hp)) return 0;
  if (!aim_head_plausible(hp[0], hp[1], hp[2], loc[0], loc[1], loc[2], r, h)) {
    ++s_boneFails;
    return 0;
  }
  memcpy(out, hp, sizeof hp);
  *bone = hb;
  return 1;
}

// The point to aim at on a pawn now: the body point its last visibility probe found clear (cached AIM_VIS_MS), else
// Aim at's choice. Silent aim's shot, the aimbot's turn, the marker and the cone all use it.
static int aim_point(void* pawn, float out[3], int* bone) {
  // A ship: its render-box centre, moved with a fresh Location read (no bone, no body points).
  if (const AcShip* s = aircraft_find(pawn)) {
    *bone = 0;
    float loc[3];
    if (!rd_vec(pawn, N_LOCATION, loc)) memcpy(loc, s->loc, sizeof loc);
    for (int k = 0; k < 3; k++) out[k] = loc[k] + s->off[k];
    return 1;
  }
  // An achievement target: its model box centre, as a ship's.
  if (const AhAimTarget* a = achhelp_aim_find(pawn)) {
    *bone = 0;
    float loc[3];
    if (!rd_vec(pawn, N_LOCATION, loc)) memcpy(loc, a->loc, sizeof loc);
    for (int k = 0; k < 3; k++) out[k] = loc[k] + a->off[k];
    return 1;
  }
  *bone = 0;
  // Collision 0 or tiny: the mounted collision the probe found clear (spawnbox_helper_point), not the render box.
  if (spawnbox_helper_point(pawn, out)) return 1;
  float bc[3], bh[3], body[3];
  const int box = spawnbox_body(pawn, bc, bh);
  int have = 0;
  if (!box) {
    int pt = AIM_PT_NONE;
    if (aim_vis_get(&s_vis, pawn, GetTickCount(), 0, &pt) != AIM_VIS_YES) pt = AIM_PT_NONE;
    have = point_of(pawn, pt, body, bone) || (pt != AIM_PT_NONE && point_of(pawn, AIM_PT_NONE, body, bone));
  }
  return aim_pawn_point(box, bc, have, body, out) != AIM_SRC_NONE;
}

// Where to aim for target's point pt seen from eye: pt itself for a hitscan gun; for a projectile gun a point along the
// launch direction aim_intercept solves, the spawn offset taken back. Velocity only with Lead moving targets on. 0: out
// of reach (counted when count is set); targets with no arc solution are skipped for AIM_NOSOLVE_MS.
static AimNoSolve s_nosolve;
static unsigned s_icSkipped;
static int lob_held(void) { return !s_mt.on && s_weap.cfg && s_weap.kindBy == AIMW_BY_LOB; }
static int solve(const void* target, const float pt[3], const float eye[3], float out[3], int count) {
  const AimProjModel m = w_model();
  memcpy(out, pt, 12);
  if (m.kind == AIM_PM_NONE) return 1;
  float vel[3] = { 0.f, 0.f, 0.f }, o[3];
  // A pawn hit through its mounted collision leads by that collision's own motion: its Location stays put.
  if (g_cfg.aimLead && !spawnbox_helper_vel(target, vel)) rd_vec(target, N_VELOCITY, vel);
  if (s_mt.on && s_mt.muzzleOk) memcpy(o, s_mt.muzzle, 12);
  else aim_lob_origin(eye, lob_held(), o);   // the spit leaves below the eye (AIM_LOB_SPAWN_DZ)
  const unsigned nowMs = GetTickCount();
  AimIntercept r = { 0, { 1.f, 0.f, 0.f }, 0.f, { pt[0], pt[1], pt[2] } };
  if (aim_nosolve_recent(&s_nosolve, target, nowMs)) {
    ++s_icSkipped;
  } else {
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    r = aim_intercept(m, o, pt, vel);
    const double ms = ms_since(t0);   // the stepped arc is the costly one: watched
    s_icMs += ms;
    ++s_icCalls;
    if (ms > s_icMaxMs) s_icMaxMs = ms;
    if (!r.ok) aim_nosolve_put(&s_nosolve, target, nowMs);
  }
  if (count && (s_icFrame != s_frameNo || s_icTarget != target)) {   // once a frame per target
    s_icFrame = s_frameNo;
    s_icTarget = target;
    if (!r.ok) ++s_icNone;
    else if (++s_icSolved, m.kind == AIM_PM_BALLISTIC) ++s_icBallistic;
  }
  if (!r.ok) return 0;
  float dir[3] = { r.dir[0], r.dir[1], r.dir[2] };
  const int offP = s_mt.on ? 0 : s_weap.spawnP, offY = s_mt.on ? 0 : s_weap.spawnY;
  if (offP || offY) {
    int p = 0, y = 0;
    aim_rotator_to(dir[0], dir[1], dir[2], &p, &y);
    aim_forward(p - offP, y - offY, dir);
  }
  const float dx = pt[0] - eye[0], dy = pt[1] - eye[1], dz = pt[2] - eye[2];
  float d = sqrtf(dx * dx + dy * dy + dz * dz);
  if (!(d > 1.f)) d = 1.f;
  for (int i = 0; i < 3; i++) out[i] = eye[i] + dir[i] * d;
  return 1;
}
// The rotator the gun must turn to for target's point pt: the solved launch direction (aim_turret_dir_ok's rule),
// the direct line for a hitscan gun. 0 out of reach.
static int launch_rot(const void* target, const float pt[3], const float eye[3], int* wp, int* wy) {
  float out[3];
  if (!solve(target, pt, eye, out, 0)) return 0;
  const float dir[3] = { out[0] - eye[0], out[1] - eye[1], out[2] - eye[2] };
  aim_rotator_to(dir[0], dir[1], dir[2], wp, wy);
  return 1;
}
// A candidate the projectile can reach from here (its centre); counted when not.
static int reach_ok(const void* target, const float pt[3], const float eye[3]) {
  float out[3];
  if (w_model().kind == AIM_PM_NONE || solve(target, pt, eye, out, 0)) return 1;
  ++s_unreachable;
  return 0;
}

// --- The weapon ----------------------------------------------------------------------------------------------------
// cls and its supers, most derived first (16 at most): the names read under the guard, their strings outside it.
static int class_chain(const void* cls, const wchar_t** out, int max) {
  int names[16];
  int n = 0;
  if (max > 16) max = 16;
  __try {
    for (const void* c = cls; c && n < max && uobj_valid(c); c = uw_ptr(c, US_SUPER)) names[n++] = uobj_name(c);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
  }
  for (int i = 0; i < n; i++) out[i] = uname_str(names[i]);
  return n;
}

// The held weapon's config class (Pawn.Weapon -> Weapon.WeaponConfig), and from its class default MaxTraceDistance
// and the projectile's model (ProjectileClass defaults Speed, MaxSpeed, AccelerationScaler, LifeSpan,
// bMatchInstigatorScale). Once per config class, logged.
static void read_weapon(void* player) {
  const void* weapon = 0;
  const void* cfg = 0;
  s_weapObj = 0;
  if (!rd_obj(player, class_of(player), N_WEAPON, &weapon) || !weapon ||
      !rd_obj(weapon, class_of(weapon), N_WEAPONCONFIG, &cfg) || !cfg) {
    s_weap.cfg = 0;
    s_weap.kind = AIMW_NONE;
    return;
  }
  s_weapObj = weapon;
  const int cfgName = name_of(cfg);
  if (cfg == s_weap.cfg && cfgName == s_weap.cfgName) return;
  WeaponInfo w;
  memset(&w, 0, sizeof w);
  w.cfg = cfg;
  w.cfgName = cfgName;
  w.ammoAmount = -1;
  // The class and its supers under the guard, their names outside it (uname.h).
  int names[8];
  int nn = 0;
  __try {
    for (const void* c = cfg; c && nn < 8 && uobj_valid(c); c = uw_ptr(c, US_SUPER)) names[nn++] = uobj_name(c);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
  }
  const wchar_t* chain[8];
  for (int i = 0; i < nn; i++) chain[i] = uname_str(names[i]);
  w.hurtsAir = ac_weapon_hurts(chain, nn, AIM_TUR_NONE);
  const void* def = default_of(cfg);
  float speed = 0.f, maxSpeed = 0.f, accel = 0.f, life = 0.f;
  int match = -1, spdOfs = -1, maxOfs = -1;
  const void* proj = 0;
  const void* traceType = 0;
  w.projPhys = -1;
  int lobs = -1;   // the ProjectileClass's bExplodeOnAnyImpact
  w.lobBoost = w.lobGScale = w.lobDelay = w.lobVar = -1.f;
  if (def) {
    rd_raw(def, cfg, N_MAXTRACE, UW_FLOAT, 4, &w.maxTrace);
    rd_obj(def, cfg, N_PROJCLASS, &proj);
    rd_obj(def, cfg, N_TRACEDT, &traceType);
    unsigned char ph = 0;
    if (const void* pd = proj ? default_of(proj) : 0) {
      if (rd_raw(pd, proj, N_PHYSICS, UW_BYTE, 1, &ph)) w.projPhys = ph;
      lobs = rd_bool_def(pd, proj, N_EXPLODEANY);
      float bz[3];
      if (rd_raw(pd, proj, N_UPBOOST, UW_STRUCT, 12, bz)) w.lobBoost = bz[2];
      rd_raw(pd, proj, N_GRAVSCALE, UW_FLOAT, 4, &w.lobGScale);
      rd_raw(pd, proj, N_EXPDELAY, UW_FLOAT, 4, &w.lobDelay);
      rd_raw(pd, proj, N_EXPVAR, UW_FLOAT, 4, &w.lobVar);
    }
  }
  // The table first (the base guns, verified in game); a config it does not name by what PerformFire does with it.
  const AimKindBy kb = aim_weapon_classify(aim_weapon_kind_chain(chain, nn), proj != 0, w.projPhys, w.maxTrace,
                                           traceType != 0, lobs > 0);
  w.kind = kb.kind;
  w.kindBy = kb.by;
  if (def) {
    // A beam's reach is FreezeRay.BeamMaxLength, on the weapon class (FreezeRay.Tick traces Start + BeamMaxLength x
    // ShrinkScale), not the config; carried in maxTrace so the aim range caps like a hitscan gun's. Unread: uncapped.
    if (w.kind == AIMW_BEAM) {
      w.maxTrace = 0.f;
      const void* wcls = class_of(weapon);
      if (const void* wdef = default_of(wcls)) rd_raw(wdef, wcls, N_BEAMLEN, UW_FLOAT, 4, &w.maxTrace);
      if (!(w.maxTrace > 0.f)) np_log("aim: beam reach %ls: BeamMaxLength unread, the aim range is not capped", uname_str(cfgName));
    }
    rd_raw(def, cfg, N_AMMOEXP, UW_INT, 4, &w.ammoAmount);
    rd_raw(def, cfg, N_PENCOUNT, UW_INT, 4, &w.penCount);
    rd_raw(def, cfg, N_PENDIST, UW_FLOAT, 4, &w.penDist);
    if (w.kind == AIMW_MELEE) {
      float ext[3] = { 0.f, 0.f, 0.f };
      rd_raw(def, cfg, N_MELEETRACE, UW_FLOAT, 4, &w.meleeTrace);
      rd_raw(def, cfg, N_MELEESHRUNK, UW_FLOAT, 4, &w.meleeShrunk);
      rd_raw(def, cfg, N_MELEEEXT, UW_STRUCT, 12, ext);
      for (int i = 0; i < 3; i++)
        if (ext[i] > w.meleeExt) w.meleeExt = ext[i];
      np_log("aim: fists %ls: MeleeTraceDistance %.1f, MeleeTraceDistanceShrunk %.1f, MeleeHitExtent %.1f,%.1f,%.1f",
             uname_str(cfgName), w.meleeTrace, w.meleeShrunk, ext[0], ext[1], ext[2]);
    }
    const void* pdef = proj ? default_of(proj) : 0;
    if (pdef) {
      rd_raw(pdef, proj, N_SPEED, UW_FLOAT, 4, &speed);
      rd_raw(pdef, proj, N_MAXSPEED, UW_FLOAT, 4, &maxSpeed);
      rd_raw(pdef, proj, N_ACCEL, UW_FLOAT, 4, &accel);
      rd_raw(pdef, proj, N_LIFESPAN, UW_FLOAT, 4, &life);
      match = rd_bool_def(pdef, proj, N_MATCHSCALE);
      spdOfs = prop(proj, N_SPEED).offset;
      maxOfs = prop(proj, N_MAXSPEED).offset;
    }
  }
  if (w.kind == AIMW_PROJECTILE && speed > 0.f) {
    const AimProjModel m = { AIM_PM_STRAIGHT, speed, maxSpeed, accel, 0.f, 0.f, life, 0, 0.f };
    w.pm = m;
    if (w.kindBy == AIMW_BY_LOB) {   // the rest per frame (w_model): the volume's gravity, the player's DrawScale
      w.pm.kind = AIM_PM_BALLISTIC;
      w.pm.maxSpeed = w.pm.accel = 0.f;
      if (w.lobBoost < 0.f) w.lobBoost = 0.f;   // unread: no boost (InitializeMotion adds none)
    }
  }
  w.matchScale = match != 0;   // Projectile's default is true
  // The blast (aim.md section 11): Projectile.DoDamage's HurtRadius reaches DamageRadius from the detonation point and
  // spares nobody but the projectile itself; bDamageDirectly damages only the actor hit. The ProjectileClass default.
  if (w.kind == AIMW_PROJECTILE) {
    float radius = -1.f;
    int direct = -1, readR = 0;
    const wchar_t* pchain[16];
    const int pn = proj ? class_chain(proj, pchain, 16) : 0;
    if (const void* pd = proj ? default_of(proj) : 0) {
      readR = rd_raw(pd, proj, N_DMGRADIUS, UW_FLOAT, 4, &radius);
      direct = rd_bool_def(pd, proj, N_DMGDIRECT);
      w.breakGlass = rd_bool_def(pd, proj, N_BREAKGLASS);
    }
    w.splash = splash_kind(1, direct, splash_spares_instigator(pchain, pn), readR, radius);
    w.splashR = splash_radius(w.splash, radius);
    w.splashDoubles = splash_chain_has(pchain, pn, L"dnRocket_Devastator");
    np_log("aim: splash %ls: DamageRadius %.0f%s, bDamageDirectly %d -> %s", proj ? uname_str(name_of(proj)) : L"(no "
           "ProjectileClass)", radius, readR ? "" : " (unread)", direct,
           w.splash == SPL_READ ? "the triggerbot holds fire while the blast would reach Duke" :
           w.splash == SPL_DEFAULT ? "radius unread: fire held inside the default 400" :
           "no blast that reaches Duke (direct damage, or it spares its Instigator)");
  }
  // The type each kind deals: PerformFire's TraceFire passes TraceDamageType (Engine.dll!0x100D0260, cfg+0x54),
  // Projectile.DoDamage the projectile's DamageClass, Weapon.MeleeHit MeleeDamageType. Others: unknown.
  if (def && w.kind == AIMW_HITSCAN) {
    rd_obj(def, cfg, N_TRACEDT, &w.dmgType);
    w.dmgSrc = "WeaponConfig.TraceDamageType";
  } else if (def && w.kind == AIMW_MELEE) {
    rd_obj(def, cfg, N_MELEEDT, &w.dmgType);
    w.dmgSrc = "WeaponConfig.MeleeDamageType";
  } else if (w.kind == AIMW_PROJECTILE && proj) {
    if (const void* pd = default_of(proj)) rd_obj(pd, proj, N_DAMAGECLASS, &w.dmgType);
    w.dmgSrc = "the ProjectileClass's DamageClass";
  } else {
    w.dmgSrc = "not read for this kind";
  }
  aim_spawn_offset(uname_str(cfgName), &w.spawnP, &w.spawnY);
  w.projName = proj ? name_of(proj) : 0;
  s_weap = w;
  static const char* const KINDS[5] = { "not redirected", "hitscan", "projectile", "beam (ice blast only)",
                                        "melee (not redirected)" };
  const int projName = proj ? name_of(proj) : 0;
  // A lob's own values (aim.md section 13). AActor::GetGravity (Engine.dll 0x10257F40) scales by +0x53C under a flag:
  // GravityScale's offset beside it says whether that is the one.
  if (w.kindBy == AIMW_BY_LOB)
    np_log("aim: lob model %ls: PHYS_Meqon, explodes on any impact; Speed %.0f, UpwardsBoost.Z %.0f, GravityScale %.2f "
           "(+0x%x; 0x53c expected), fuse ExplosionDelay %.1f - %.1f s; gravity from the player's PhysicsVolume each frame",
           projName ? uname_str(projName) : L"?", speed, w.lobBoost, w.lobGScale, prop(proj, N_GRAVSCALE).offset,
           w.lobDelay, w.lobVar);
  np_log("aim: weapon %ls: %s (%s; ProjectileClass %ls, Physics %d), AmmoExpenditure %d, %s %.0f, "
         "MaxTracePenetrationCount %d, MaxPenetrationDistance %.0f; hurts aircraft %s", uname_str(cfgName), KINDS[w.kind],
         aim_kind_by_label(w.kindBy), projName ? uname_str(projName) : L"None", w.projPhys, w.ammoAmount,
         w.kind == AIMW_BEAM ? "BeamMaxLength" : "MaxTraceDistance", w.maxTrace, w.penCount, w.penDist,
         w.hurtsAir ? "yes" : "no");
  // The model the intercept uses (aim.md section 8). physProjectile clamps the speed at +0x980/+0x984: Speed and
  // MaxSpeed's offsets beside them confirm the reading.
  if (proj)
    np_log("aim: projectile model %ls: %s, Speed %.0f (+0x%x) rising %.0f/s to MaxSpeed %.0f (+0x%x), LifeSpan %.1f s, "
           "bMatchInstigatorScale %d; spawn rotator offset %d,%d", uname_str(name_of(proj)),
           w.pm.kind == AIM_PM_STRAIGHT ? "straight" : w.pm.kind == AIM_PM_BALLISTIC ? "a lob" : "not aimed", speed,
           spdOfs, accel, maxSpeed, maxOfs, life, match,
           w.spawnP, w.spawnY);
}

// --- The frame -----------------------------------------------------------------------------------------------------
struct PlayerRd { int alive, grabbing, attached; const void* vm; const void* ctrl; };

// 1 read, 0 a property did not resolve (refused), -1 a fault (tried again next frame).
static int read_player(void* player, PlayerRd* r) {
  const unsigned f0 = s_faults;
  const void *vm = 0, *ctrl = 0;
  const void* pc = class_of(player);
  if (!rd_alive(player, &r->alive) || !rd_bool(player, N_GRABBING, &r->grabbing) ||
      !rd_bool(player, N_ATTACHED, &r->attached) || !rd_obj(player, pc, N_VIEWMAPPER, &vm) ||
      !rd_obj(player, pc, N_INTERACTIVE, &ctrl)) {
    if (s_faults != f0) return -1;
    refuse("a player property (Health, bGrabbing, bAttachedToUsableSomething, ViewMapper or InteractiveDecoration) "
           "did not resolve; see nukepanel.log");
    return 0;
  }
  r->vm = vm;
  r->ctrl = ctrl;
  return 1;
}

// The resolve check, once per level once a player exists: the bracket's properties as GetWeaponAim reads them, and
// the crosshair trace's two properties (aim.md section 4: expected at +0x1D64 and +0x1D74).
static bool check(void* player) {
  for (int k = 0; k < N_REQUIRED; k++) {
    if (nm(k)) continue;
    np_log("aim: no name %ls", NAMES[k]);
    refuse("a property name is missing from the name table; see nukepanel.log");
    return false;
  }
  const void* pc = class_of(player);
  const UProp sh = prop(pc, N_SHAKE), vr = prop(pc, N_VIEWROT), cf = prop(pc, N_CURFOV), df = prop(pc, N_DEFFOV);
  const UProp vl = prop(pc, N_VIEWLOC), lh = prop(pc, N_LOOKHIT), ld = prop(pc, N_LOOKDIST);
  np_log("aim: CurrentShake +0x%x, ViewRotation +0x%x, CurrentFOV +0x%x, DefaultFOV +0x%x, CalcedViewLocation +0x%x; "
         "DirectLookHitActor %s+0x%x, LookHitDistance %s+0x%x (0x1D64 and 0x1D74 expected)", sh.offset, vr.offset,
         cf.offset, df.offset, vl.offset, lh.ok ? "" : "unresolved ", lh.offset, ld.ok ? "" : "unresolved ", ld.offset);
  if (!sh.ok || sh.type != UW_STRUCT || sh.elemSize != 12 || !vr.ok || vr.type != UW_STRUCT || vr.elemSize != 12 ||
      !cf.ok || cf.type != UW_FLOAT || !df.ok || df.type != UW_FLOAT || !vl.ok || vl.type != UW_STRUCT) {
    refuse("PlayerPawn.CurrentShake, ViewRotation, CurrentFOV, DefaultFOV or CalcedViewLocation did not resolve; see "
           "nukepanel.log");
    return false;
  }
  if (!scall_find(player, L"TraceEx").ok) {
    refuse("Actor.TraceEx did not resolve; see nukepanel.log");
    return false;
  }
  return true;
}

// tbot_attached_pawn's links and test for a point trace's hit: Owner and MountParent; the target itself.
static void hit_links(const void* o, const void* out[2], void* ctx) {
  (void)ctx;
  rd_obj(o, class_of(o), N_OWNER, &out[0]);
  rd_obj(o, class_of(o), N_MOUNTPARENT, &out[1]);
}
static int hit_is_target(const void* o, void* ctx) { return o == ctx; }

// o's class or one of its supers is named nameIdx (a guarded walk, 16 classes at most).
static int chain_has(const void* o, int nameIdx) {
  if (!o || !nameIdx) return 0;
  __try {
    int k = 0;
    for (const void* c = uobj_class(o); c && k < 16 && uobj_valid(c); c = uw_ptr(c, US_SUPER), k++)
      if (uobj_name(c) == nameIdx) return 1;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
  }
  return 0;
}

static int class_name_idx(const void* o) {
  const void* c = o ? class_of(o) : 0;
  return c ? name_of(c) : 0;
}

static int hit_response(const void* hit);   // below trace_ex: the hit's TraceFireHitResponse

// A point trace that reported hidden (instrumentation only): its hit sorted by aim_hit_category for the
// 600-frame line, and a capped line with the point, the target, the hit actor and its Owner and MountParent, the hit
// distance against the point's, and the trace start against the player's Location.
static void note_hidden(void* player, const float start[3], const float end[3], const void* target, const void* hit,
                        const float hitLoc[3], int pt) {
  static int s_nLevel, s_nPawn, s_nMover;
  if (!s_nLevel) s_nLevel = uname_find(L"LevelInfo");
  if (!s_nPawn) s_nPawn = uname_find(L"Pawn");
  if (!s_nMover) s_nMover = uname_find(L"Mover");
  const void *owner = 0, *mount = 0;
  if (hit) {
    rd_obj(hit, class_of(hit), N_OWNER, &owner);
    rd_obj(hit, class_of(hit), N_MOUNTPARENT, &mount);
  }
  int steps = -1;
  const int self = hit && (hit == player ||
                           tbot_attached_pawn(hit, hit_links, hit_is_target, 0, player, &steps) == player);
  const int hitCls = class_name_idx(hit);
  const wchar_t* hitName = hitCls ? uname_str(hitCls) : L"None";
  const int door = chain_has(hit, s_nMover) || (hitName && wcsstr(hitName, L"Door"));
  const int cat = aim_hit_category(!hit || chain_has(hit, s_nLevel), self, hit && (owner == target || mount == target),
                                   chain_has(hit, s_nPawn), door);
  ++s_hidHits[cat];
  if (s_hidLogs >= HIDDEN_LOGS_PER_LEVEL) return;
  s_hidLogs++;
  float loc[3] = { 0.f, 0.f, 0.f };
  rd_vec(player, N_LOCATION, loc);
  const float hx = hitLoc[0] - start[0], hy = hitLoc[1] - start[1], hz = hitLoc[2] - start[2];
  const float px = end[0] - start[0], py = end[1] - start[1], pz = end[2] - start[2];
  const float cx = start[0] - loc[0], cy = start[1] - loc[1], cz = start[2] - loc[2];
  const int oc = class_name_idx(owner), mc = class_name_idx(mount), tc = class_name_idx(target);
  np_log("aim: point hidden: %s on %ls; hit %ls (Owner %ls, MountParent %ls, TraceFireHitResponse %d) = %s at %.0f of "
         "%.0f units; camera %.0f,%.0f,%.0f, player Location %.0f,%.0f,%.0f (%.0f apart)", aim_pt_label(pt),
         tc ? uname_str(tc) : L"?", hitName, oc ? uname_str(oc) : L"None", mc ? uname_str(mc) : L"None",
         hit_response(hit), aim_hit_label(cat),
         sqrtf(hx * hx + hy * hy + hz * hz), sqrtf(px * px + py * py + pz * pz), start[0], start[1], start[2], loc[0],
         loc[1], loc[2], sqrtf(cx * cx + cy * cy + cz * cz));
}

// Actor.TraceEx(TraceStart, TraceEnd, TraceFlags, out TraceHitResult, Extent, IgnoreActor) (native 736) on the player,
// from start to end with the STraceFlags flags: the first actor hit (validated) and the hit location, read from the
// parms block after the call. 1 called, 0 refused, -1 it does not resolve. Timed.
static int trace_ex(void* player, const float start[3], const float end[3], unsigned flags, const void** hit,
                    float hitLoc[3], const void* ignore = 0) {
  *hit = 0;
  const ScallFn f = scall_find(player, L"TraceEx");
  static const void* s_fn;
  static ScallParm s_start, s_end, s_flags, s_rv, s_hr, s_ign;
  static int s_ignOk;
  if (!f.ok) return -1;
  if (f.fn != s_fn) {
    ScallParm a, b, fl, hr, rv;
    if (!scall_parm(f, L"TraceStart", L"StructProperty", &a) || a.size != 12 ||
        !scall_parm(f, L"TraceEnd", L"StructProperty", &b) || b.size != 12 ||
        !scall_parm(f, L"TraceFlags", L"StructProperty", &fl) || fl.size != 4 ||
        !scall_parm(f, L"TraceHitResult", L"StructProperty", &hr) || hr.size != 56 ||
        !scall_parm(f, L"ReturnValue", L"ObjectProperty", &rv) || rv.size != 4)
      return -1;
    s_start = a;
    s_end = b;
    s_flags = fl;
    s_rv = rv;
    s_hr = hr;
    ScallParm ig = { 0, 0 };
    s_ignOk = scall_parm(f, L"IgnoreActor", L"ObjectProperty", &ig) && ig.size == 4;
    if (!s_ignOk) np_log("aim: Actor.TraceEx has no IgnoreActor parameter; a passed actor is hit again until the cap");
    s_ign = ig;
    s_fn = f.fn;
  }
  unsigned char parms[192];
  memset(parms, 0, sizeof parms);   // Extent 0 (a line), IgnoreActor none unless given
  if (f.parmsSize > sizeof parms || !scallparms_put(parms, f.parmsSize, s_start.offset, start, 12) ||
      !scallparms_put(parms, f.parmsSize, s_end.offset, end, 12) ||
      !scallparms_put(parms, f.parmsSize, s_flags.offset, &flags, 4) ||
      (ignore && s_ignOk && !scallparms_put(parms, f.parmsSize, s_ign.offset, &ignore, 4)))
    return -1;
  LARGE_INTEGER t0;
  QueryPerformanceCounter(&t0);
  const int called = scall_call(player, f, parms, sizeof parms);
  s_ptMs += ms_since(t0);
  ++s_ptTraces;
  if (!called) {
    ++s_refusedCalls;
    return 0;
  }
  memcpy(hit, parms + s_rv.offset, 4);
  memcpy(hitLoc, parms + s_hr.offset + 4, 12);   // STraceHitResult.Location
  return 1;
}

// The hit leaves end in view of target: nothing, the target, or an actor hanging from it (aim_trace_clear).
static int hit_clear(const void* hit, const void* target) {
  int steps = -1;
  const int onTarget = hit && hit != target &&
                       tbot_attached_pawn(hit, hit_links, hit_is_target, 0, (void*)target, &steps) == target;
  return aim_trace_clear(hit != 0, hit == target, onTarget);
}

// A hit actor's TraceFireHitResponse (AIM_THR_*), -1 unread.
static int hit_response(const void* hit) {
  int r = -1;
  return hit && rd_byte(hit, N_THRESP, &r) ? r : -1;
}

// The real bullet's walk (AActor::TraceFire, Engine.dll!0x10282A30): the shot-flag trace, then past each actor the
// bullet would pass, re-traced beyond it, AIM_PASS_MAX times at most. BSP surfaces TraceFire skips by flag are not
// modelled. Same return as trace_ex; *hit and hitLoc are the hit that stops it.
static unsigned s_bulletPassed;   // actors passed, counted per 600 frames
static int bullet_trace(void* player, const float start[3], const float end[3], const void* target, const void** hit,
                        float hitLoc[3], int* passed = 0) {
  static int s_nLevel;
  if (!s_nLevel) s_nLevel = uname_find(L"LevelInfo");
  int pen = s_mt.on ? 0 : s_weap.cfg ? s_weap.penCount : 0;   // the mounted guns' configs are not read: none
  const float penDist = s_mt.on ? 0.f : s_weap.cfg ? s_weap.penDist : 0.f;
  float from[3] = { start[0], start[1], start[2] };
  const void* ignore = 0;
  if (passed) *passed = 0;
  for (int k = 0;; k++) {
    const int called = trace_ex(player, from, end, aim_shot_trace_flags(), hit, hitLoc, ignore);
    if (called <= 0 || k >= AIM_PASS_MAX || !*hit || !live(*hit) || hit_clear(*hit, target) ||
        chain_has(*hit, s_nLevel))
      return called;
    const float dx = hitLoc[0] - start[0], dy = hitLoc[1] - start[1], dz = hitLoc[2] - start[2];
    float next[3];
    if (!aim_bullet_passes(hit_response(*hit), &pen, penDist, sqrtf(dx * dx + dy * dy + dz * dz)) ||
        !aim_step_past(start, end, hitLoc, next))
      return called;
    ++s_bulletPassed;
    if (passed) ++*passed;
    memcpy(from, next, sizeof from);
    ignore = *hit;
  }
}

// One body point of target by the bullet's own trace: TraceFire calls ULevel::MultiLineCheck with flags 0x1080675
// (Actor.TraceEx: bTraceActors | bMeshAccurate | bShotTrace | bTraceTexture). With [aim] debug_double_trace=1 a clear
// result is rechecked with the old player-blocking flags. 1 clear, 0 hidden, -1 unresolved.
static int trace_point(void* player, const float start[3], const float end[3], const void* target, int pt) {
  const void* hit = 0;
  float hitLoc[3] = { 0.f, 0.f, 0.f };
  const int called = bullet_trace(player, start, end, target, &hit, hitLoc);
  if (called <= 0) return called;
  if (hit && !live(hit)) {   // unreadable: never called clear
    ++s_hidHits[AIM_HIT_OTHER];
    return 0;
  }
  const int clear = hit_clear(hit, target);
  spawnbox_note_hit(target, hit ? class_name_idx(hit) : 0, hit == target);
  if (!clear) {
    note_hidden(player, start, end, target, hit, hitLoc, pt);
    return 0;
  }
  if (hit) {
    ++s_clearTarget;
  } else {
    ++s_clearNoHit;   // bullets pass through (or the pawn has no shot-trace collision): the measurement, not yet a rule
    spawnbox_log_nohit(target, aim_pt_label(pt));
  }
  if (!g_cfg.aimDebugDoubleTrace) return 1;
  const void* bhit = 0;
  float bLoc[3];
  // The debug trace is counted apart: the point-trace stats stay the real traces' own.
  const unsigned dt0 = s_ptTraces;
  const double dms0 = s_ptMs;
  const int dr = trace_ex(player, start, end, AIM_TF_BLOCKING, &bhit, bLoc);
  s_dblTraces += s_ptTraces - dt0;
  s_dblMs += s_ptMs - dms0;
  s_ptTraces = dt0;
  s_ptMs = dms0;
  if (dr > 0 && (!bhit || live(bhit)) && aim_blocker_skipped(clear, hit_clear(bhit, target))) {
    ++s_skipped;
    if (s_skipLogs < LOGS_PER_LEVEL / 4) {
      s_skipLogs++;
      const int bc = class_name_idx(bhit);
      np_log("aim: %s: a blocker the bullet passes skipped: %ls", aim_pt_label(pt), bc ? uname_str(bc) : L"None");
    }
  }
  return 1;
}

// One point's first hit by the bullet-flag trace, for the point-geometry log: "label hit <class> (<category>) at A of
// B units (C short)", or clear by the target, an attachment or nothing. The point-trace stats stay the probes' own.
static void ptlog_trace(void* player, const float eye[3], const float p[3], const void* pawn, const char* label,
                        char* out, int cap) {
  const void* hit = 0;
  float hl[3] = { 0.f, 0.f, 0.f };
  const unsigned t0 = s_ptTraces;
  const double ms0 = s_ptMs;
  int passed = 0;
  const int called = bullet_trace(player, eye, p, pawn, &hit, hl, &passed);
  s_ptTraces = t0;
  s_ptMs = ms0;
  const float px = p[0] - eye[0], py = p[1] - eye[1], pz = p[2] - eye[2];
  const float hx = hl[0] - eye[0], hy = hl[1] - eye[1], hz = hl[2] - eye[2];
  const float pd = sqrtf(px * px + py * py + pz * pz), hd = sqrtf(hx * hx + hy * hy + hz * hz);
  if (called <= 0) {
    _snprintf(out, cap, "%s not traced", label);
  } else if (hit && !live(hit)) {
    _snprintf(out, cap, "%s hit an unreadable actor", label);
  } else if (hit_clear(hit, pawn)) {
    _snprintf(out, cap, "%s clear (%s; %d passed)", label,
              !hit ? "nothing hit" : hit == pawn ? "the target" : "an attachment", passed);
  } else {
    static int s_nLevel;
    if (!s_nLevel) s_nLevel = uname_find(L"LevelInfo");
    const int hc = class_name_idx(hit);
    _snprintf(out, cap, "%s hit %ls (%s, response %d) at %.0f of %.0f units (%.0f short; %d passed)", label,
              hc ? uname_str(hc) : L"?", chain_has(hit, s_nLevel) ? "world" : "actor", hit_response(hit), hd, pd,
              pd - hd, passed);
  }
  out[cap - 1] = 0;
}

// The point geometry of a pawn no point of which is clear (egg pods, 2026-10-10: wall and ceiling pods were never
// targeted). Location, the cylinder, the render box and the LockOnBone's world position, then each point's first hit:
// whether the points sit in the geometry or real cover stands between. Once per pawn and distance band.
static AimPtLog s_ptlog;
static void note_points(void* player, const float eye[3], void* pawn) {
  float loc[3];
  if (!rd_vec(pawn, N_LOCATION, loc)) return;
  const float dx = loc[0] - eye[0], dy = loc[1] - eye[1], dz = loc[2] - eye[2];
  const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
  if (!aim_ptlog_take(&s_ptlog, pawn, aim_ptlog_band(dist))) return;
  float r = 0.f, h = 0.f;
  const WPawn* pw = 0;
  const int n = world_pawns(&pw);
  for (int i = 0; i < n; i++)
    if (pw[i].obj == pawn) { r = pw[i].radius; h = pw[i].height; break; }
  float bc[3] = { 0.f, 0.f, 0.f }, bh[3] = { 0.f, 0.f, 0.f }, bp[3] = { 0.f, 0.f, 0.f };
  const int box = spawnbox_render_box(pawn, bc, bh);
  int lock = 0;
  rd_name(pawn, N_LOCKBONE, &lock);
  const int bone = lock && bone_world(pawn, lock, bp) && (bp[0] != 0.f || bp[1] != 0.f || bp[2] != 0.f);
  const float bx = bc[0] - loc[0], by = bc[1] - loc[1], bz = bc[2] - loc[2];
  const float lx = bp[0] - loc[0], ly = bp[1] - loc[1], lz = bp[2] - loc[2];
  const int cls = name_of(class_of(pawn));
  np_log("aim: points of %ls at %.0f units: Location %.0f,%.0f,%.0f, collision radius %.0f height %.0f; render box "
         "%s centre %.0f,%.0f,%.0f half %.0f,%.0f,%.0f (%.0f from Location, dz %.0f); LockOnBone %ls %s at %.0f,%.0f,%.0f "
         "(%.0f from Location, dz %.0f); camera %.0f,%.0f,%.0f", cls ? uname_str(cls) : L"?", dist, loc[0], loc[1],
         loc[2], r, h, box ? "read" : "unread", bc[0], bc[1], bc[2], bh[0], bh[1], bh[2],
         sqrtf(bx * bx + by * by + bz * bz), bz, lock ? uname_str(lock) : L"None", bone ? "read" : "unread", bp[0],
         bp[1], bp[2], sqrtf(lx * lx + ly * ly + lz * lz), lz, eye[0], eye[1], eye[2]);
  char t[5][160];
  static const int PTS[3] = { AIM_PT_CHEST, AIM_PT_HIPS, AIM_PT_KNEES };
  for (int k = 0; k < 3; k++) {
    float p[3];
    aim_body_point(PTS[k], loc, h, p);
    ptlog_trace(player, eye, p, pawn, aim_pt_label(PTS[k]), t[k], sizeof t[k]);
  }
  if (bone) ptlog_trace(player, eye, bp, pawn, "LockOnBone", t[3], sizeof t[3]);
  else _snprintf(t[3], sizeof t[3], "LockOnBone unread");
  if (box) ptlog_trace(player, eye, bc, pawn, "box centre", t[4], sizeof t[4]);
  else _snprintf(t[4], sizeof t[4], "box unread");
  t[3][sizeof t[3] - 1] = t[4][sizeof t[4] - 1] = 0;
  np_log("aim: points of %ls traced: %s; %s; %s; %s; %s", cls ? uname_str(cls) : L"?", t[0], t[1], t[2], t[3], t[4]);
}

// One helper's Location by the bullet-flag trace (trace_point): 1 clear, 0 hidden, -1 refused.
static int trace_helper(void* player, const float eye[3], void* pawn, const McHelper& h) {
  const int clear = trace_point(player, eye, h.c, pawn, AIM_PT_NONE);
  if (clear < 0)
    refuse("Actor.TraceEx's TraceStart, TraceEnd, TraceFlags, TraceHitResult or ReturnValue did not resolve; see "
           "nukepanel.log");
  else
    ++s_helperTraces;
  return clear;
}
// A pawn with mounted collision: the current helper first while it stays clear, unless the first in aim order is better
// (mc_better) and clear too; else the helpers in aim order, AIM_PTS_MAX at most. The first clear one becomes the aim
// point when choose is set. 1 clear, 0 none, -1 no helpers (or the trace does not resolve).
static int probe_helpers(void* player, const float eye[3], void* pawn, int choose) {
  McHelper hs[MC_MAX];
  float centre[3];
  int cur = -1;
  const int n = spawnbox_helpers(pawn, hs, centre, &cur);
  if (!n) return -1;
  if (cur >= 0) {
    const int c = trace_helper(player, eye, pawn, hs[cur]);
    if (c < 0) return -1;
    if (c) {
      int pick = cur;
      if (cur != 0 && mc_better(hs[0], hs[cur], centre)) {
        const int b = trace_helper(player, eye, pawn, hs[0]);
        if (b < 0) return -1;
        if (b) pick = 0;
      }
      if (choose) spawnbox_helper_choose(pawn, hs[pick].obj);
      return 1;
    }
  }
  for (int k = 0, t = 0; k < n && t < AIM_PTS_MAX; k++) {
    if (k == cur) continue;   // hidden above
    ++t;
    const int c = trace_helper(player, eye, pawn, hs[k]);
    if (c < 0) return -1;
    if (!c) continue;
    if (choose) spawnbox_helper_choose(pawn, hs[k].obj);
    return 1;
  }
  return 0;
}

// The visibility probe of one candidate: its body points in priority order (aim_point_order: Aim at's choice, head,
// chest, hips, knees; AIM_PTS_MAX traces at most), the first clear one picked (*pt); and (while Auto-execute is on)
// whether it is frozen. Hidden when none is clear.
static int probe(void* player, const float eye[3], void* pawn, int* frozen, int* pt) {
  *frozen = 0;
  *pt = AIM_PT_NONE;
  ++s_probes;
  if (g_cfg.autoExecute) rd_byte(pawn, N_FROZEN, frozen);
  // Mounted collision (mountcol.h): its helpers in aim order, the first clear one chosen.
  const int hv = probe_helpers(player, eye, pawn, 1);
  if (hv >= 0) {
    ++s_picks[hv ? PICK_HELPER : AIM_PT_NONE];
    return hv ? AIM_VIS_YES : AIM_VIS_NO;
  }
  // The body animated away from Location: one bullet-flag trace to the render-box centre, the point aim_point uses.
  float bc[3], bh[3];
  if (spawnbox_body(pawn, bc, bh)) {
    const int clear = trace_point(player, eye, bc, pawn, AIM_PT_NONE);
    if (clear < 0) {
      refuse("Actor.TraceEx's TraceStart, TraceEnd, TraceFlags, TraceHitResult or ReturnValue did not resolve; see "
             "nukepanel.log");
      return AIM_VIS_NO;
    }
    ++s_boxTraces;
    ++s_picks[clear ? PICK_BOX : AIM_PT_NONE];
    return clear ? AIM_VIS_YES : AIM_VIS_NO;
  }
  const int hb = head_bone(pawn);   // also the per-class BonePartMapper line (aim.md section 6; cached)
  int order[AIM_PTS_MAX];
  const int n = aim_point_order(g_cfg.aimPoint, hb != 0, order);
  for (int k = 0; k < n; k++) {
    float p[3];
    int bone = 0;
    if (!point_of(pawn, order[k], p, &bone)) continue;
    const int clear = trace_point(player, eye, p, pawn, order[k]);
    if (clear < 0) {
      refuse("Actor.TraceEx's TraceStart, TraceEnd, TraceFlags, TraceHitResult or ReturnValue did not resolve; see "
             "nukepanel.log");
      return AIM_VIS_NO;
    }
    if (!clear) continue;
    *pt = order[k];
    ++s_picks[order[k]];
    if (k > 0 && s_ptLogs < LOGS_PER_LEVEL / 2) {
      s_ptLogs++;
      np_log("aim: %ls: %s hidden -> %s", uname_str(name_of(class_of(pawn))), aim_pt_label(order[0]),
             aim_pt_label(order[k]));
    }
    return AIM_VIS_YES;
  }
  ++s_picks[AIM_PT_NONE];
  note_points(player, eye, pawn);   // measurement only (once per pawn and distance band)
  return AIM_VIS_NO;
}

// An ally or a neutral pawn (alive) standing in the line from the camera to the point to (skip: the candidate's walk
// index, -1 for a ship).
static int ally_between_pt(const WPawn* pw, int n, int skip, const float eye[3], const float to[3]) {
  for (int i = 0; i < n; i++) {
    if (i == skip || !(pw[i].ally || pw[i].neutral)) continue;
    if (aim_seg_cyl_hit(eye, to, pw[i].x, pw[i].y, pw[i].z, pw[i].radius, pw[i].height)) return 1;
  }
  return 0;
}
// The pawn's centre for the pick: its body box (spawnbox_body: its mounted collision, or the render box while its body
// is animated away), else the walk's Location. 1 the box.
static int pawn_centre(const WPawn& w, float out[3]) {
  float h[3];
  if (spawnbox_body(w.obj, out, h)) return 1;
  out[0] = w.x;
  out[1] = w.y;
  out[2] = w.z;
  return 0;
}
static int ally_between(const WPawn* pw, int n, int idx, const float eye[3]) {
  float to[3];
  pawn_centre(pw[idx], to);
  return ally_between_pt(pw, n, idx, eye, to);
}

// One candidate's visibility: blocked by an ally, the cache, else a trace while *traces is under the frame's budget
// (traces null: the cache only). A frozen enemy is not a target while Auto-execute is on.
static int cand_vis(void* player, const WView& v, const WPawn* pw, int n, int idx, const float eye[3], unsigned now,
                    int* traces) {
  void* o = (void*)pw[idx].obj;
  if (ally_between(pw, n, idx, eye)) {
    ++s_allyBlocked;
    return AIM_VIS_NO;
  }
  int frozen = 0;
  int vis = aim_vis_get(&s_vis, o, now, &frozen);
  if (vis == AIM_VIS_UNKNOWN && traces && *traces < AIM_TRACES_PER_FRAME && !s_why) {
    ++*traces;
    int pt = AIM_PT_NONE;
    vis = probe(player, eye, o, &frozen, &pt);
    aim_vis_put(&s_vis, o, now, vis, frozen, pt);
    if (vis == AIM_VIS_NO) ++s_hidden;
  }
  if (vis == AIM_VIS_YES && aim_skip_frozen(frozen, g_cfg.autoExecute)) {
    ++s_frozenSkipped;
    return AIM_VIS_NO;
  }
  return vis;
}

// A pawn candidate in the projectile's reach, at its centre (reach_ok).
static int reach_pawn(const WPawn& w, const float eye[3]) {
  float c[3];
  pawn_centre(w, c);
  return reach_ok(w.obj, c, eye);
}

// The cone at the Aim at point, as aim_track keeps it (aim_cone_admits), in either Pick target mode.
static int cone_ok(void* pawn, const float fwd[3], const float eye[3]) {
  float pt[3];
  int bone = 0;
  if (!aim_point(pawn, pt, &bone)) return 0;
  if (aim_cone_admits(g_cfg.aimTarget, fwd, eye, pt, g_cfg.aimCone)) return 1;
  ++s_outOfCone;
  return 0;
}

// Why the previous target lost the pick this frame (the lock churn, 2026-10-05), capped with the target lines.
// prevPos: its position in cand, -1 when it was no candidate; coneOut: its aim point failed the cone; toBoss: the boss
// took the pick by priority; bossOff: prev is the boss and its verdict (AIM_BOSS_*) took it out, 0 otherwise.
static void log_lost(const void* prev, int prevCls, const AimCand* cand, int prevPos, int coneOut, const float fwd[3],
                     const float eye[3], float newKey, int toBoss, int bossOff) {
  if (s_targetLogs >= LOGS_PER_LEVEL) return;
  s_targetLogs++;
  char why[128];
  if (toBoss) {
    _snprintf(why, sizeof why, "the boss is possible (boss priority)");
  } else if (bossOff) {
    _snprintf(why, sizeof why, "the boss is not possible: %s", aim_boss_label(bossOff));
  } else if (prevPos < 0) {
    _snprintf(why, sizeof why, "no candidate (dead, out of range, off screen or outside the cone at its centre)");
  } else if (coneOut) {
    float pt[3];
    int bone = 0;
    const int ok = live(prev) && aim_point((void*)prev, pt, &bone);
    _snprintf(why, sizeof why, "its %s outside the cone (%.1f degrees)", bone ? "head" : "aim point",
              ok ? aim_angle(fwd, pt[0] - eye[0], pt[1] - eye[1], pt[2] - eye[2]) : -1.f);
  } else {
    const int vis = cand[prevPos].vis;
    _snprintf(why, sizeof why, "%s", vis == AIM_VIS_NO ? "hidden" : vis == AIM_VIS_UNKNOWN ? "visibility unknown" :
              "out-ranked");
  }
  why[sizeof why - 1] = 0;
  np_log("aim: %ls lost the pick: %s; its key %.1f, the new target's %.1f", uname_str(prevCls), why,
         prevPos >= 0 ? cand[prevPos].key : -1.f, newKey);
}

// One ship's visibility: an ally or neutral pawn in the line, the cache, else one bullet-flag trace to the render-box
// centre, clear when it hits the ship itself, something hanging from it, or nothing (hit_clear).
static int ship_vis(void* player, const WPawn* pw, int n, const AcShip& s, const float eye[3], unsigned now,
                    int* traces) {
  if (ally_between_pt(pw, n, -1, eye, s.c)) {
    ++s_shipAlly;
    return AIM_VIS_NO;
  }
  int vis = aim_vis_get(&s_vis, s.obj, now, 0);
  if (vis != AIM_VIS_UNKNOWN || !traces || *traces >= AIM_TRACES_PER_FRAME || s_why) return vis;
  ++*traces;
  const void* hit = 0;
  float hitLoc[3];
  const int called = bullet_trace(player, eye, s.c, s.obj, &hit, hitLoc);
  if (called < 0)
    refuse("Actor.TraceEx's TraceStart, TraceEnd, TraceFlags, TraceHitResult or ReturnValue did not resolve; see "
           "nukepanel.log");
  vis = called > 0 && (!hit || live(hit)) && hit_clear(hit, s.obj) ? AIM_VIS_YES : AIM_VIS_NO;
  aim_vis_put(&s_vis, s.obj, now, vis, 0, AIM_PT_NONE);
  if (vis == AIM_VIS_NO) ++s_shipHidden;
  return vis;
}
// A ship candidate in the walk: out of the projectile's reach is hidden (reach_ok), else ship_vis. isTarget: the
// previous target, whose reach goes on its flip line.
static int ship_walk_vis(void* player, const WPawn* pw, int n, const AcShip& s, const float eye[3], unsigned now,
                         int* traces, int isTarget) {
  const int r = reach_ok(s.obj, s.c, eye);
  if (isTarget && !r) s_sv.reach = 0;
  return r ? ship_vis(player, pw, n, s, eye, now, traces) : AIM_VIS_NO;
}

// The last resort (aircraft.h, ac_ship_pass): the hostile, live, damageable ships, ranked and walked as the pawns are,
// at the render-box centre. Range, screen, cone and the turret's limits apply; spawnbox exclusions and Auto-execute do
// not. prev: the previous target when it was a ship. traces null: the cache only. 1 picked (s_target set).
static int pick_ship(void* player, const WView& v, const float fwd[3], int W, int H, const void* prev, int* traces) {
  const int cacheOnly = !traces;
  const AcShip* sh = 0;
  const int ns = aircraft_ships(&sh);
  const WPawn* pw = 0;
  const int n = world_pawns(&pw);
  const float range = aim_range_for(s_mt.on, g_cfg.aimRangeM, ESP_UU_PER_METRE, w_kind(), w_max());
  const int pick = g_cfg.aimTarget;
  const float eye[3] = { v.x, v.y, v.z };
  AimCand cand[AC_MAX];
  float dist[AC_MAX], angle[AC_MAX];
  int nc = 0, prevIdx = -1;
  for (int i = 0; i < ns && i < AC_MAX; i++) {
    const AcShip& s = sh[i];
    const int isTarget = prev && s.obj == prev;
    AcShipVerdict* tv = isTarget ? &s_sv : 0;   // the target's own tests, for its flip line
    if (tv) {
      tv->seen = 1;
      tv->damageable = s.damageable;
    }
    if (!s.damageable) continue;
    if (overkill_doomed(s.obj)) {   // the projectiles in flight will kill it: the aim moves on
      ++s_shipDoomed;
      s_passedDoomed = 1;
      if (tv) tv->doomed = 1;
      continue;
    }
    if (cacheOnly && !isTarget) continue;   // ac_ship_admit: the cache pass never switches
    const float dx = s.c[0] - v.x, dy = s.c[1] - v.y, dz = s.c[2] - v.z;
    const float d = sqrtf(dx * dx + dy * dy + dz * dz);
    float sx = 0.f, sy = 0.f;
    const int onScreen = g_cfg.aimOffscreen ? 1 : aim_on_screen(view_w2s(v, s.c[0], s.c[1], s.c[2], W, H, &sx, &sy),
                                                                   sx, sy, W, H);
    if (tv) {
      tv->inRange = d <= range;
      tv->onScreen = onScreen;
      tv->reach = tv->limits = 1;   // launch_rot and reach_ok below may still refuse it
      s_sf.dist = d;
      s_sf.range = range;
    }
    if (!aim_eligible(1, 0, d, range, onScreen, g_cfg.aimOffscreen)) continue;
    if (s_mt.on && s_mt.limitsOk) {
      int wp = 0, wy = 0;
      if (!launch_rot(s.obj, s.c, eye, &wp, &wy)) {   // the lob, not the direct line
        ++s_unreachable;
        if (tv) tv->reach = 0;
        continue;
      }
      const int strict = aim_turret_reachable(s_mt.lim, s_mt.rotP, s_mt.rotY, s_gunView[0], s_gunView[1], wp, wy);
      const int slackP = isTarget ? ac_limit_slack(s.h[2], d) : 0;
      const int slackY = isTarget ? ac_limit_slack(s.h[0] > s.h[1] ? s.h[0] : s.h[1], d) : 0;
      const int slack = isTarget && ac_turret_reachable(s_mt.lim, s_mt.rotP, s_mt.rotY, s_gunView[0], s_gunView[1],
                                                        wp, wy, slackP, slackY);
      const int admit = ac_ship_admit(isTarget, cacheOnly, strict, slack);
      if (tv) {
        const ShipFlipVals f = { s_sf.pawnCands, s_sf.pawnUnknown, s_sf.hurts, 1, strict, slack, wp, wy,
                                 s_gunView[0], s_gunView[1], slackP, slackY, d, range, 0.f };
        s_sf = f;
        tv->limits = admit;
      }
      if (!admit) {
        ++s_shipLimits;
        continue;
      }
      if (!strict) ++s_shipLimitKeeps;
    }
    angle[i] = aim_angle(fwd, dx, dy, dz);
    const int inCone = aim_cone_admits(pick, fwd, eye, s.c, g_cfg.aimCone);   // the aim point itself
    if (tv) {
      tv->cone = inCone;
      s_sf.angle = angle[i];
    }
    if (!inCone) {
      ++s_shipCone;
      continue;
    }
    dist[i] = d;
    const AimCand c = { i, aim_cand_key(pick, angle[i], d), AIM_VIS_UNKNOWN };
    cand[nc++] = c;
    if (s.obj == prev) prevIdx = i;
  }
  if (!nc) return 0;
  aim_cand_sort(cand, nc);
  const unsigned now = GetTickCount();
  int end = nc, stop = -1;
  for (int k = 0; k < nc; k++) {
    const AcShip& sk = sh[cand[k].idx];   // a projectile gun's arc or range may not reach it
    cand[k].vis = ship_walk_vis(player, pw, n, sk, eye, now, traces, cand[k].idx == prevIdx);
    if (cand[k].vis == AIM_VIS_YES) { end = k; break; }
    if (cand[k].vis == AIM_VIS_UNKNOWN) { stop = end = k; break; }
  }
  for (int k = end + 1; prevIdx >= 0 && k < nc; k++)   // the previous ship past the stop, for the hysteresis
    if (cand[k].idx == prevIdx)
      cand[k].vis = ship_walk_vis(player, pw, n, sh[prevIdx], eye, now, stop >= 0 ? 0 : traces, 1);
  for (int k = 0; prevIdx >= 0 && k < nc; k++)
    if (cand[k].idx == prevIdx) s_sv.vis = cand[k].vis;
  const int at = aim_pick(cand, nc, prevIdx, pick);
  if (at < 0) return 0;
  const AcShip& t = sh[cand[at].idx];
  s_target = t.obj;
  s_targetShip = 1;
  s_targetClsName = t.clsName;
  s_sv.picked = s_target == prev;
  s_targetAngle = angle[cand[at].idx];
  s_targetDist = dist[cand[at].idx];
  if (s_target != prev && s_targetLogs < LOGS_PER_LEVEL) {
    s_targetLogs++;
    np_log("aim: target %ls (aircraft, no pawn picked) at %.0f units, %.1f degrees off the crosshair (%s; %d "
           "candidate(s), Health %.0f)", uname_str(t.clsName), s_targetDist, s_targetAngle, aim_pick_label(pick), nc,
           t.health);
  }
  return 1;
}

// One achievement target's visibility, as a ship's: out of the projectile's reach, an ally or neutral in the line, the
// cache, else one bullet-flag trace to its box centre (bullet_trace passes water: WaterVolume and GeoWater answer 1).
// A hidden one logs its first hit, capped (Map21's fish swim under the dam's water).
static int ach_vis(void* player, const WPawn* pw, int n, const AhAimTarget& t, const float eye[3], unsigned now,
                   int* traces) {
  if (!reach_ok(t.obj, t.c, eye)) return AIM_VIS_NO;
  if (ally_between_pt(pw, n, -1, eye, t.c)) {
    ++s_achAlly;
    return AIM_VIS_NO;
  }
  int vis = aim_vis_get(&s_vis, t.obj, now, 0);
  if (vis != AIM_VIS_UNKNOWN || !traces || *traces >= AIM_TRACES_PER_FRAME || s_why) return vis;
  ++*traces;
  const void* hit = 0;
  float hitLoc[3] = { 0.f, 0.f, 0.f };
  int passed = 0;
  const int called = bullet_trace(player, eye, t.c, t.obj, &hit, hitLoc, &passed);
  if (called < 0)
    refuse("Actor.TraceEx's TraceStart, TraceEnd, TraceFlags, TraceHitResult or ReturnValue did not resolve; see "
           "nukepanel.log");
  vis = called > 0 && (!hit || live(hit)) && hit_clear(hit, t.obj) ? AIM_VIS_YES : AIM_VIS_NO;
  aim_vis_put(&s_vis, t.obj, now, vis, 0, AIM_PT_NONE);
  if (vis == AIM_VIS_NO) {
    ++s_achHidden;
    if (called > 0 && s_achHidLogs < LOGS_PER_LEVEL / 4) {
      s_achHidLogs++;
      const float dx = t.c[0] - eye[0], dy = t.c[1] - eye[1], dz = t.c[2] - eye[2];
      const float hx = hitLoc[0] - eye[0], hy = hitLoc[1] - eye[1], hz = hitLoc[2] - eye[2];
      const int hc = class_name_idx(hit);
      np_log("aim: achievement target hidden: %ls; hit %ls (response %d) at %.0f of %.0f units, %d passed",
             uname_str(t.clsName), hc ? uname_str(hc) : L"an unreadable actor", hit_response(hit),
             sqrtf(hx * hx + hy * hy + hz * hz), sqrtf(dx * dx + dy * dy + dz * dz), passed);
    }
  }
  return vis;
}

// The last resort after the ships (achhelp.h, ah_aim_pass): the achievement targets whose row is on, ranked and
// walked as the ships are, at the model box centre. Range (the fists' reach), the screen, the cone, the turret's
// limits and the held gun's damage type apply. 1 picked.
static int pick_ach(void* player, const WView& v, const float fwd[3], int W, int H, const void* prev, int* traces) {
  const AhAimTarget* at = 0;
  const int na = achhelp_aim_targets(&at);
  if (!na) return 0;
  const WPawn* pw = 0;
  const int n = world_pawns(&pw);
  const float range = aim_range_for(s_mt.on, g_cfg.aimRangeM, ESP_UU_PER_METRE, w_kind(), w_max());
  const int pick = g_cfg.aimTarget;
  const float eye[3] = { v.x, v.y, v.z };
  AimCand cand[AH_AIM_MAX];
  float dist[AH_AIM_MAX], angle[AH_AIM_MAX];
  int nc = 0, prevIdx = -1;
  for (int i = 0; i < na && i < AH_AIM_MAX; i++) {
    const AhAimTarget& t = at[i];
    if (!t.typeOk) {
      ++s_achType;
      continue;
    }
    if (overkill_doomed(t.obj)) {
      ++s_achDoomed;
      s_passedDoomed = 1;
      continue;
    }
    const float dx = t.c[0] - v.x, dy = t.c[1] - v.y, dz = t.c[2] - v.z;
    const float d = sqrtf(dx * dx + dy * dy + dz * dz);
    float sx = 0.f, sy = 0.f;
    const int onScreen = g_cfg.aimOffscreen ? 1 : aim_on_screen(view_w2s(v, t.c[0], t.c[1], t.c[2], W, H, &sx, &sy),
                                                                   sx, sy, W, H);
    const float rangeHere = w_kind() == AIMW_MELEE ? (aim_melee_centre_ok(d, s_reach, t.radius) ? d : -1.f) : range;
    if (!aim_eligible(1, 0, d, rangeHere, onScreen, g_cfg.aimOffscreen)) {
      ++s_achRange;
      continue;
    }
    if (s_mt.on && s_mt.limitsOk) {
      int wp = 0, wy = 0;
      if (!launch_rot(t.obj, t.c, eye, &wp, &wy) ||
          !aim_turret_reachable(s_mt.lim, s_mt.rotP, s_mt.rotY, s_gunView[0], s_gunView[1], wp, wy)) {
        ++s_achLimits;
        continue;
      }
    }
    angle[i] = aim_angle(fwd, dx, dy, dz);
    if (!aim_cone_admits(pick, fwd, eye, t.c, g_cfg.aimCone)) {
      ++s_achCone;
      continue;
    }
    dist[i] = d;
    const AimCand c = { i, aim_cand_key(pick, angle[i], d), AIM_VIS_UNKNOWN };
    cand[nc++] = c;
    if (t.obj == prev) prevIdx = i;
  }
  if (!nc) return 0;
  aim_cand_sort(cand, nc);
  const unsigned now = GetTickCount();
  int end = nc, stop = -1;
  for (int k = 0; k < nc; k++) {
    cand[k].vis = ach_vis(player, pw, n, at[cand[k].idx], eye, now, traces);
    if (cand[k].vis == AIM_VIS_YES) { end = k; break; }
    if (cand[k].vis == AIM_VIS_UNKNOWN) { stop = end = k; break; }
  }
  for (int k = end + 1; prevIdx >= 0 && k < nc; k++)   // the previous target past the stop, for the hysteresis
    if (cand[k].idx == prevIdx) cand[k].vis = ach_vis(player, pw, n, at[prevIdx], eye, now, stop >= 0 ? 0 : traces);
  const int pos = aim_pick(cand, nc, prevIdx, pick);
  if (pos < 0) return 0;
  const AhAimTarget& t = at[cand[pos].idx];
  s_target = t.obj;
  s_targetAch = 1;
  s_targetClsName = t.clsName;
  s_targetAngle = angle[cand[pos].idx];
  s_targetDist = dist[cand[pos].idx];
  if (s_target != prev && s_targetLogs < LOGS_PER_LEVEL) {
    s_targetLogs++;
    const AhAch* a = ah_find(t.id);
    np_log("aim: achievement target %ls (%s, no pawn or ship picked) at %.0f units, %.1f degrees off the crosshair "
           "(%s; %d candidate(s) of %d, Health %.1f, %s box)", uname_str(t.clsName), a ? a->name : "?", s_targetDist,
           s_targetAngle, aim_pick_label(pick), nc, na, t.health, t.boxOk ? "model" : "cylinder");
  }
  return 1;
}

// spawnbox's exclusion as the boss verdict's reason.
static int boss_excl(int ex) {
  return ex == SB_EXCL_NONE ? 0 : ex == SB_EXCL_NODAMAGE ? AIM_BOSS_INVULNERABLE : ex == SB_EXCL_TELEPORT ?
         AIM_BOSS_TELEPORT : ex == SB_EXCL_WRONGTYPE ? AIM_BOSS_WRONGTYPE : ex == SB_EXCL_DOOMED ? AIM_BOSS_DOOMED :
         ex == SB_EXCL_EXPANDED ? AIM_BOSS_EXPANDED : AIM_BOSS_EXCLUDED;
}

// A hostile pawn that enters the pick's candidates: eligible (range, screen, fists' reach), not left out by spawnbox,
// within a mounted gun's limits, and inside the cone's pre-filter at its centre. count: the pick's own call, which
// feeds the counters; *d and *ang its distance and angle there. bt (may be null): the test that refused it.
static int pawn_cand(void* player, const WPawn& w, const WView& v, const float fwd[3], float range, int pick, int W,
                     int H, int count, float* d, float* ang, int cone = 1, AimBossTests* bt = 0) {
  if (!w.obj || w.obj == player) {
    if (bt) bt->eligible = 0;
    return 0;
  }
  float pc[3];   // the render-box centre while the body is away from Location
  const int boxed = pawn_centre(w, pc);
  const float dx = pc[0] - v.x, dy = pc[1] - v.y, dz = pc[2] - v.z;
  *d = sqrtf(dx * dx + dy * dy + dz * dz);
  float sx = 0.f, sy = 0.f;
  const int offscreen = aim_boss_offscreen(cone, g_cfg.aimOffscreen);   // no cone: on screen whatever the setting
  const int onScreen = offscreen ? 1 : aim_on_screen(view_w2s(v, pc[0], pc[1], pc[2], W, H, &sx, &sy), sx, sy, W, H);
  // The fists: only within the melee reach of the pawn's surface, whatever the slider says.
  const float rangeHere = w_kind() == AIMW_MELEE ? (aim_melee_centre_ok(*d, s_reach, w.radius) ? *d : -1.f) : range;
  if (!aim_eligible(w.enemy, w.ally, *d, rangeHere, onScreen, offscreen)) {
    if (bt) bt->eligible = 0;
    return 0;
  }
  // Its body animated away from Location with no render box: no target. A cut-scene alone is one when the exact
  // damage rules allow it; the bullet-flag trace still decides visibility.
  const int ex = spawnbox_excluded(w.obj);
  if (ex) {
    if (bt) bt->excl = boss_excl(ex);
    if (!count) return 0;
    if (ex == SB_EXCL_FAR) ++s_exFar;
    else if (ex == SB_EXCL_TELEPORT) ++s_exTele;
    else if (ex == SB_EXCL_NODAMAGE) ++s_exNoDmg;
    else if (ex == SB_EXCL_PLAYERDMG0) ++s_exDmg0;
    else if (ex == SB_EXCL_NEUTRAL) ++s_exNeutral;
    else if (ex == SB_EXCL_WRONGTYPE) ++s_exWrongType;
    else if (ex == SB_EXCL_EXPANDED) ++s_exExpanded;
    else if (ex == SB_EXCL_DOOMED) {
      ++s_exDoomed;
      s_passedDoomed = 1;
    }
    return 0;
  }
  if (count && spawnbox_in_cutscene(w.obj)) ++s_cutAdmitted;
  if (count && boxed) ++s_boxAimed;
  // Mounted: a target the gun cannot turn to is no candidate (the hook would clamp the turn).
  if (s_mt.on && s_mt.limitsOk) {
    int wp = 0, wy = 0;
    const float at[3] = { v.x, v.y, v.z };
    if (!launch_rot(w.obj, pc, at, &wp, &wy)) {   // the lob, not the direct line
      if (count) ++s_unreachable;
      if (bt) bt->reach = 0;
      return 0;
    }
    if (!aim_turret_reachable(s_mt.lim, s_mt.rotP, s_mt.rotY, s_gunView[0], s_gunView[1], wp, wy)) {
      if (count) ++s_outOfLimits;
      if (bt) bt->limits = 0;
      return 0;
    }
  }
  *ang = aim_angle(fwd, dx, dy, dz);
  if (cone && !aim_cone_prefilter(pick, *ang, *d, w.radius, w.height, g_cfg.aimCone)) {
    if (count) ++s_outOfCone;
    if (bt) bt->cone = 0;
    return 0;
  }
  return 1;
}

// The boss verdict this frame (aim.md section 12): the boss-meter pawn through the pick's own tests, in its order
// (pawn_cand; with cone, the cone at the aim point; the projectile's reach; visibility, probed first so the frame's
// trace budget serves the boss before the adds). *idx: its walk index, -1 without one; *d, *ang as pawn_cand's.
static int boss_eval(void* player, const WView& v, const float fwd[3], float range, int W, int H, int cone,
                     int* traces, int* idx, float* d, float* ang) {
  const WPawn* pw = 0;
  const int n = world_pawns(&pw);
  AimBossTests t = aim_boss_tests_pass();
  *idx = -1;
  *d = *ang = 0.f;
  for (int i = 0; i < n && *idx < 0; i++)
    if (pw[i].bossMeter && pw[i].obj && pw[i].obj != player) *idx = i;
  t.found = *idx >= 0;
  if (!t.found) return aim_boss_verdict(t);
  const WPawn& w = pw[*idx];
  t.alive = w.alive;
  if (!t.alive || !pawn_cand(player, w, v, fwd, range, g_cfg.aimTarget, W, H, 0, d, ang, cone, &t))
    return aim_boss_verdict(t);
  const float eye[3] = { v.x, v.y, v.z };
  float pt[3], c[3], out[3];
  int bone = 0;
  if (cone)
    t.cone = aim_point((void*)w.obj, pt, &bone) && aim_cone_admits(g_cfg.aimTarget, fwd, eye, pt, g_cfg.aimCone);
  if (t.cone) {
    pawn_centre(w, c);
    t.reach = w_model().kind == AIM_PM_NONE || solve(w.obj, c, eye, out, 0);
  }
  if (t.cone && t.reach) t.vis = cand_vis(player, v, pw, n, *idx, eye, GetTickCount(), traces);
  // The Triggerbot's splash guard (aim.md section 11): a boss it would hold fire on is not possible, so the adds may be
  // fired at. One aim_splash call within its trace cap; the Triggerbot reuses it this frame (aim_splash's cache).
  if (t.vis == AIM_VIS_YES && g_cfg.tbotOn) {
    AimSplash sp;
    if (aim_splash(w.obj, &sp) && sp.kind != SPL_NONE)
      t.safe = !splash_judge(sp.kind, sp.radius, sp.duke, sp.dukeR, sp.dukeH, sp.pts).hold;
  }
  return aim_boss_verdict(t);
}

// Takes this frame's verdict: the 600-frame counts and a capped line when it turns on, off or away (none/on/off), or
// the boss changes while on.
static void boss_note(int state, int idx) {
  const WPawn* pw = 0;
  const int n = world_pawns(&pw);
  s_bossEval = 1;
  s_bossState = state;
  s_boss = idx >= 0 && idx < n ? pw[idx].obj : 0;
  s_bossCls = idx >= 0 && idx < n ? pw[idx].nameIdx : 0;
  ++s_bossFrames[state >= 0 && state < AIM_BOSS_COUNT ? state : AIM_BOSS_NONE];
  const int cat = state == AIM_BOSS_NONE ? 0 : state == AIM_BOSS_ON ? 1 : 2;
  if (cat == s_bossLastCat && !(cat == 1 && s_boss != s_bossLogged)) return;
  s_bossLastCat = cat;
  s_bossLogged = cat == 1 ? s_boss : 0;
  if (s_bossLogs >= LOGS_PER_LEVEL) return;
  s_bossLogs++;
  if (cat == 1) np_log("aim: boss priority: on %ls", s_bossCls ? uname_str(s_bossCls) : L"?");
  else np_log("aim: boss priority off: %s%s%ls%s", aim_boss_label(state), s_bossCls ? " (" : "",
              s_bossCls ? uname_str(s_bossCls) : L"", s_bossCls ? ")" : "");
}

// The hostile pawn candidates this frame without a pick (the Triggerbot alone): no trace, no counter.
static int count_pawn_cands(void* player, int W, int H) {
  const WView v = world_view();
  if (!v.ok) return -1;
  const WPawn* pw = 0;
  const int n = world_pawns(&pw);
  float fwd[3];
  aim_forward(v.pitch, v.yaw, fwd);
  const float range = aim_range_for(s_mt.on, g_cfg.aimRangeM, ESP_UU_PER_METRE, w_kind(), w_max());
  int nc = 0;
  for (int i = 0; i < n; i++) {
    float d = 0.f, ang = 0.f;
    nc += pawn_cand(player, pw[i], v, fwd, range, g_cfg.aimTarget, W, H, 0, &d, &ang);
  }
  return nc;
}

static void pick_walk(void* player, int W, int H) {
  const WView v = world_view();
  const WPawn* pw = 0;
  const int n = world_pawns(&pw);
  const void* prev = s_target;
  const int prevCls = s_targetClsName;
  s_passedDoomed = 0;
  const int prevShip = s_targetShip, prevAch = s_targetAch;
  s_target = 0;
  s_targetShip = 0;
  s_targetAch = 0;
  s_sv.viewOk = v.ok;
  if (!v.ok) return;   // no pawns at all still leaves the ships
  float fwd[3];
  aim_forward(v.pitch, v.yaw, fwd);
  const float range = aim_range_for(s_mt.on, g_cfg.aimRangeM, ESP_UU_PER_METRE, w_kind(), w_max());
  const int pick = g_cfg.aimTarget;
  int traces = 0;
  // The boss first (aim.md section 12): while it is possible the adds are not walked or traced.
  int bossIdx = -1;
  float bossD = 0.f, bossAng = 0.f;
  const int boss = boss_eval(player, v, fwd, range, W, H, 1, &traces, &bossIdx, &bossD, &bossAng);
  boss_note(boss, bossIdx);
  static AimCand cand[512];
  static float dist[512], angle[512];
  static unsigned char coneOut[512];   // by walk index: the aim point failed the cone this frame
  int nc = 0, prevIdx = -1;
  for (int i = 0; i < n && nc < 512; i++) {
    const WPawn& w = pw[i];
    float d = 0.f, ang = 0.f;
    if (!pawn_cand(player, w, v, fwd, range, pick, W, H, 1, &d, &ang)) continue;
    const AimCand c = { i, aim_cand_key(pick, ang, d), AIM_VIS_UNKNOWN };
    dist[i] = d;
    angle[i] = ang;
    coneOut[i] = 0;
    cand[nc++] = c;
    if (w.obj == prev) prevIdx = i;
  }
  s_pawnCands = nc;
  aim_cand_sort(cand, nc);
  if (nc > AIM_CAND_MAX) nc = AIM_CAND_MAX;
  const unsigned now = GetTickCount();
  const float eye[3] = { v.x, v.y, v.z };
  const int bossOn = boss == AIM_BOSS_ON;
  int stop = -1;   // where the walk stopped on an unknown
  int end = nc;    // where the walk stopped
  for (int k = 0; k < nc && !bossOn; k++) {
    AimCand& c = cand[k];
    if (c.idx == bossIdx) {   // judged above: not possible, so not walked
      c.vis = AIM_VIS_NO;
      continue;
    }
    const int inCone = cone_ok((void*)pw[c.idx].obj, fwd, eye);
    coneOut[c.idx] = !inCone;
    c.vis = inCone && reach_pawn(pw[c.idx], eye) ? cand_vis(player, v, pw, n, c.idx, eye, now, &traces) : AIM_VIS_NO;
    if (c.vis == AIM_VIS_YES) {   // the first visible: aim_pick returns it, or keeps the previous one
      end = k;
      break;
    }
    if (c.vis == AIM_VIS_UNKNOWN) {   // the traces are spent: aim_pick decides with what it knows
      stop = end = k;
      break;
    }
  }
  // The previous target past where the walk stopped: its visibility (a trace while the budget lasts, the cache once
  // the walk stopped on an unknown), so aim_pick can keep it meanwhile or by hysteresis (aim_challenger_wins).
  int prevPos = -1;
  for (int k = 0; prevIdx >= 0 && k < nc; k++) {
    if (cand[k].idx != prevIdx) continue;
    prevPos = k;
    if (k > end && !bossOn && prevIdx != bossIdx) {
      const int inCone = cone_ok((void*)pw[prevIdx].obj, fwd, eye);
      coneOut[prevIdx] = !inCone;
      cand[k].vis = inCone && reach_pawn(pw[prevIdx], eye) ?
                    cand_vis(player, v, pw, n, prevIdx, eye, now, stop >= 0 ? 0 : &traces) : AIM_VIS_NO;
    }
    break;
  }
  const int tIdx = aim_boss_pick(cand, nc, prevIdx, pick, bossIdx, boss);
  s_sf.pawnCands = nc;
  s_sf.pawnUnknown = stop >= 0;
  float newKey = -1.f;
  for (int k = 0; k < nc; k++)
    if (cand[k].idx == tIdx) newKey = cand[k].key;
  if (prev && !prevShip && !prevAch && (tIdx < 0 || tIdx != prevIdx))
    log_lost(prev, prevCls, cand, prevPos, prevPos >= 0 && coneOut[prevIdx], fwd, eye, newKey, bossOn,
             !bossOn && prevIdx >= 0 && prevIdx == bossIdx ? boss : 0);
  if (tIdx < 0) {
    // No pawn: the ships, last, and only with a gun that hurts them. A pawn whose visibility is
    // still unknown (the frame's traces spent) holds them back, but the previous ship is kept from the cache.
    const int hurts = w_hurts_air();
    const int mode = ac_ship_pass_mode(0, stop >= 0, hurts, prevShip && prev && aircraft_find(prev));
    s_sv.passMode = mode;
    s_sf.hurts = hurts;
    int shipPicked = 0;
    if (mode != AC_PASS_NONE) {
      if (pick_ship(player, v, fwd, W, H, prevShip ? prev : 0, mode == AC_PASS_FULL ? &traces : 0)) {
        ++s_picked;
        ++s_shipFrames;
        shipPicked = 1;
      }
    } else if (!hurts) {
      const AcShip* sh = 0;
      const int ns = aircraft_ships(&sh);
      for (int i = 0; i < ns; i++) s_shipWeaponSkips += (unsigned)sh[i].damageable;
    }
    // Then the achievement targets whose row is on, only with no hostile pawn candidate at all.
    if (ah_aim_pass(nc, shipPicked) && pick_ach(player, v, fwd, W, H, prevAch ? prev : 0, &traces)) {
      ++s_picked;
      ++s_achFrames;
    }
    return;
  }
  const WPawn& t = pw[tIdx];
  s_target = t.obj;
  s_targetClsName = t.nameIdx;
  s_targetAngle = bossOn ? bossAng : angle[tIdx];
  s_targetDist = bossOn ? bossD : dist[tIdx];
  ++s_picked;
  if (s_target != prev) {
    float bc[3], bh[3], hp[3];
    // Capped there; only in a cut-scene, animated away or aimed at its mounted collision.
    spawnbox_log_pick(t.obj, spawnbox_helper_point(t.obj, hp) ? MC_SRC_HELPERS :
                             spawnbox_body(t.obj, bc, bh) ? MC_SRC_BOX : MC_SRC_NONE);
    spawnbox_helper_event(t.obj, "aim pick", 1);
  }
  if (s_target != prev && s_targetLogs < LOGS_PER_LEVEL) {
    s_targetLogs++;
    np_log("aim: target %ls at %.0f units, %.1f degrees off the crosshair (%s; %d candidate(s), %d trace(s) this "
           "frame%s)", uname_str(t.nameIdx), s_targetDist, s_targetAngle, aim_pick_label(pick), nc, traces,
           bossOn ? "; the boss, by priority" : "");
  }
}

// The previous target was a ship and this frame's pick is not it: counted by the first test it failed, and a capped
// line with each pass's verdict, the gun's limit test as the pick asked it, the camera and the remapper's ViewOffset.
static void note_ship_lost(int prevCls) {
  const int r = ac_lost_reason(s_sv);
  ++s_shipLost[r];
  if (s_flipLogs >= FLIP_LOGS_PER_LEVEL) return;
  s_flipLogs++;
  const ShipFlipVals& f = s_sf;
  char lim[192] = "limits not asked";
  if (f.haveLimits) {
    const int p = s_mt.rotP + aim_wrap16(f.wantP - f.camP), y = s_mt.rotY + aim_wrap16(f.wantY - f.camY);
    _snprintf(lim, sizeof lim, "gun to pitch %d of %d..%d (slack %d), yaw %d of %d..%d%s (slack %d): %s", p,
              s_mt.lim.minPitch, s_mt.lim.maxPitch, f.slackP, y, s_mt.lim.minYaw, s_mt.lim.maxYaw,
              s_mt.lim.clampYaw ? "" : " unclamped", f.slackY, f.strict ? "inside" : f.slackOk ? "inside by the slack" :
              "outside");
    lim[sizeof lim - 1] = 0;
  }
  int pend[3];
  aim_mount_view_offset(pend);
  const WView v = world_view();
  np_log("aim: ship flip: %ls lost to %ls: %s; pawn pass %d candidate(s)%s, ship pass %s%s; %.0f units (range %.0f), "
         "%.1f degrees off (cone %d), vis %s; %s; camera %d,%d (the gun %d,%d), RotatePitch/Yaw %d,%d, ViewOffset %d,%d",
         uname_str(prevCls), s_target ? uname_str(s_targetClsName) : L"none", ac_lost_name(r), f.pawnCands,
         f.pawnUnknown ? " (stopped on an unknown)" : "", s_sv.passMode == AC_PASS_FULL ? "full" :
         s_sv.passMode == AC_PASS_CACHE ? "cache only" : "none", f.hurts ? "" : " (the gun cannot hurt ships)", f.dist,
         f.range, f.angle, g_cfg.aimCone, s_sv.vis == AIM_VIS_YES ? "yes" : s_sv.vis == AIM_VIS_NO ? "no" : "unknown",
         lim, v.pitch, v.yaw, s_gunView[0], s_gunView[1], s_mt.rotP, s_mt.rotY, pend[0], pend[1]);
}

// Once a frame before the pick: the mounted gun's own direction (s_gunView), else the camera.
static void gun_view(void) {
  const WView v = world_view();
  s_gunView[0] = v.pitch;
  s_gunView[1] = v.yaw;
  s_gunViewFromGun = 0;
  if (!s_mt.on || !s_mt.limitsOk) return;
  s_gunViewFromGun = aim_gun_view(s_mt.baseOk, s_mt.base, s_mt.rotP, s_mt.rotY, v.pitch, v.yaw, s_gunView);
  ++(s_gunViewFromGun ? s_gunViewGun : s_gunViewCam);
}

static void pick_target(void* player, int W, int H) {
  const void* prev = s_target;
  const int prevShip = s_targetShip, prevCls = s_targetClsName;
  memset(&s_sv, 0, sizeof s_sv);
  memset(&s_sf, 0, sizeof s_sf);
  s_sv.passMode = AC_PASS_NONE;
  s_sv.vis = AIM_VIS_UNKNOWN;
  s_sf.hurts = 1;
  pick_walk(player, W, H);
  if (prev && prevShip && s_target != prev) note_ship_lost(prevCls);
}

// The marker's two points for this frame's target (one bone read a frame while it shows).
static void marker_points(void) {
  s_mkOk = 0;
  if (!aim_marker_shown(g_cfg.silentOn, g_cfg.aimbotOn, g_cfg.aimMarker) || !s_target || !live(s_target)) return;
  int bone = 0;
  if (!aim_point((void*)s_target, s_mkRaw, &bone)) return;
  const WView v = world_view();
  const float eye[3] = { v.x, v.y, v.z };
  if (!solve(s_target, s_mkRaw, eye, s_mkLed, 1)) memcpy(s_mkLed, s_mkRaw, sizeof s_mkLed);   // out of reach
  s_mkOk = 1;
}

// The held weapon can spend this shot's ammo (aim_ammo_ok).
static int ammo_ok(void* player) {
  int infinite = 0;
  rd_bool(player, N_INFAMMO, &infinite);
  float charge = 0.f;
  int haveCharge = 0;
  if (!infinite) {
    const void* weapon = 0;
    const void* ammo = 0;
    if (rd_obj(player, class_of(player), N_WEAPON, &weapon) && weapon &&
        rd_obj(weapon, class_of(weapon), N_AMMO, &ammo) && ammo)
      haveCharge = rd_float(ammo, N_CHARGE, &charge);
  }
  return aim_ammo_ok(infinite, haveCharge, charge, s_weap.ammoAmount);
}

// The handler's pre-checks, once a frame for the Triggerbot's Locked mode (aim_redirect_ready).
static int redirect_ready(void* player) {
  int cur[3], sh[3];
  float cf = 0.f, df = 0.f;
  return ammo_ok(player) && rd_rot(player, N_SHAKE, cur) && rd_float(player, N_CURFOV, &cf) &&
         rd_float(player, N_DEFFOV, &df) && aim_shake_for(0, 0, 0, 0, cf, df, sh);
}

// --- The bracket (the dnGame ProcessEvent slot, before the engine's ProcessEvent) ------------------------------
static void on_trace_vars(void* obj, void* func, void* parms) {
  (void)func;
  (void)parms;
  if (!s_live || !obj || obj != world_player()) return;
  ++s_uwtv;
  if (s_gate != AIM_GO || !s_target || s_weap.kind == AIMW_MELEE || s_mt.on) {   // a punch never passes here; never redirected
    ++s_noTarget;
    return;
  }
  LARGE_INTEGER t0;
  QueryPerformanceCounter(&t0);
  void* tgt = (void*)s_target;
  if (!live(tgt)) {
    ++s_noTarget;
    return;
  }
  // ExpendAmmo must succeed, or RecordShot never comes (aim.md section 1, step 2).
  if (!ammo_ok(obj)) {
    ++s_noAmmo;
    return;
  }
  float pt[3], start[3];
  int bone = 0, vr[3], cur[3];
  float curFov = 0.f, defFov = 0.f;
  if (!aim_point(tgt, pt, &bone)) return;
  if (!camera_location(obj, start) && !rd_vec(obj, N_VIEWLOC, start)) return;
  if (!rd_rot(obj, N_VIEWROT, vr) || !rd_rot(obj, N_SHAKE, cur) || !rd_float(obj, N_CURFOV, &curFov) ||
      !rd_float(obj, N_DEFFOV, &defFov))
    return;
  float d[3] = { pt[0] - start[0], pt[1] - start[1], pt[2] - start[2] };
  const float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  float led[3];
  if (!solve(tgt, pt, start, led, 1)) {   // out of the projectile's reach: the shot goes where the player aimed
    ++s_shotsOutOfReach;
    return;
  }
  for (int i = 0; i < 3; i++) d[i] = led[i] - start[i];
  int wantP, wantY, wr[3];
  aim_rotator_to(d[0], d[1], d[2], &wantP, &wantY);
  if (!aim_shake_for(vr[0], vr[1], wantP, wantY, curFov, defFov, wr)) return;
  aim_bracket_open(&s_br, cur, wr);
  if (!wr_rot(obj, N_SHAKE, wr)) {
    s_br.open = 0;   // nothing written, nothing to put back
    return;
  }
  ++s_redirected;
  const double ms = ms_since(t0);
  s_handlerMs += ms;
  if (ms > s_handlerMaxMs) s_handlerMaxMs = ms;
  if (s_shotLogs >= LOGS_PER_LEVEL) return;
  s_shotLogs++;
  int shots = -1, hits = -1;
  rd_int(obj, N_ACCSHOTS, &shots);
  rd_int(obj, N_ACCHITS, &hits);
  np_log("aim: shot at %ls (%ls) %.0f units, weapon %ls, lead %.0f units; CurrentShake %d,%d,%d -> %d,%d,%d, FOV "
         "%.1f/%.1f; AccuracyShots %d, AccuracyHits %d; %.3f ms", uname_str(s_targetClsName),
         bone ? uname_str(bone) : L"body", dist, uname_str(s_weap.cfgName),
         sqrtf((led[0] - pt[0]) * (led[0] - pt[0]) + (led[1] - pt[1]) * (led[1] - pt[1]) +
               (led[2] - pt[2]) * (led[2] - pt[2])), cur[0], cur[1], cur[2], wr[0], wr[1], wr[2], curFov, defFov,
         shots, hits, ms);
}

static void on_record_shot(void* obj, void* func, void* parms) {
  (void)func;
  (void)parms;
  if (!obj || obj != world_player()) return;
  ++s_rec;
  int back[3];
  if (aim_bracket_close(&s_br, back)) wr_rot(obj, N_SHAKE, back);
}

// A bracket still open at Present: RecordShot never came (aim_bracket_frame_end).
static void frame_end_bracket(void) {
  if (!s_br.open) return;
  void* player = world_player();
  int cur[3], back[3];
  if (!player || !rd_rot(player, N_SHAKE, cur)) {
    s_br.open = 0;
    s_br.leftOpen++;
    return;
  }
  if (aim_bracket_frame_end(&s_br, cur, back)) wr_rot(player, N_SHAKE, back);
}

static void new_level(unsigned level) {
  // Pointers of the last level are stale: forgotten without being read.
  s_level = level;
  s_checked = 0;
  s_target = 0;
  s_targetShip = 0;
  s_targetAch = 0;
  s_mkOk = 0;
  s_br.open = 0;
  s_mb.open = 0;
  s_msGren.on = 0;
  s_msLogs = 0;
  memset(&s_vis, 0, sizeof s_vis);
  memset(&s_weap, 0, sizeof s_weap);
  s_weapObj = 0;
  s_reach = 0.f;
  s_outOfAmmo = -1;
  s_headN = 0;
  memset(&s_mt, 0, sizeof s_mt);
  s_beamName = 0;
  memset(&s_mlog, 0, sizeof s_mlog);
  s_mlogName = 0;
  s_mountLogs = 0;
  s_dtLogWeapon = s_dtLogs = 0;
  s_dtLogType = 0;
  s_shotLogs = s_targetLogs = s_gateLogs = s_ptLogs = s_hidLogs = s_skipLogs = s_flipLogs = s_achHidLogs = 0;
  s_bossEval = s_bossState = s_bossCls = s_bossLastCat = s_bossLogs = 0;
  s_boss = s_bossLogged = 0;
  memset(&s_ptlog, 0, sizeof s_ptlog);
  memset(&s_nosolve, 0, sizeof s_nosolve);
}

// One line per change of the held or mounted gun or its damage type (spawnbox's wrong-damage-type rule reads it).
static void note_dmg_type(void) {
  const int w = s_mt.on ? s_mt.gunName : s_weap.cfg ? s_weap.cfgName : 0;
  const void* t = aim_damage_type();
  if (w == s_dtLogWeapon && t == s_dtLogType) return;
  s_dtLogWeapon = w;
  s_dtLogType = t;
  if (!w || s_dtLogs >= LOGS_PER_LEVEL) return;
  s_dtLogs++;
  const int tn = name_of(t);
  np_log("aim: weapon %ls damage type %ls (%s)", uname_str(w), tn ? uname_str(tn) : L"unknown",
         s_mt.on ? (s_mt.dmgSrc ? s_mt.dmgSrc : "not read for this gun") : s_weap.dmgSrc ? s_weap.dmgSrc : "?");
}

// Frames a closed gate held silent aim back (switched off is not counted); each change logged.
static void note_gate(int gate) {
  if (gate != AIM_GO && gate != AIM_G_OFF) ++s_gated;
  if (gate == s_gate || s_gateLogs >= LOGS_PER_LEVEL) return;
  s_gateLogs++;
  np_log("aim: gate %s", aim_gate_name(gate));
}

// The mounted guns' projectile classes, by name once a level: dnGrenadeLauncherProjectile's bounce (-1 unread),
// dnRocket_SuperTurret's Speed, MaxSpeed, AccelerationScaler and LifeSpan, and each class's DamageRadius and
// bDamageDirectly for the blast (splash.h). found: each class found.
struct MountProj { unsigned level; int done, found[2], bounce; float speed, maxSpeed, accel, life; const void* dmg[2];
                   float radius[2]; int direct[2], breakGlass[2]; const void* cls[2]; };
static MountProj s_mp;
static const MountProj& mount_proj(void) {
  if (s_mp.done && s_mp.level == s_level) return s_mp;
  memset(&s_mp, 0, sizeof s_mp);
  s_mp.level = s_level;
  s_mp.done = 1;
  s_mp.bounce = -1;
  s_mp.speed = s_mp.maxSpeed = s_mp.accel = s_mp.life = -1.f;
  s_mp.radius[0] = s_mp.radius[1] = -1.f;
  s_mp.direct[0] = s_mp.direct[1] = -1;
  s_mp.breakGlass[0] = s_mp.breakGlass[1] = -1;
  static const wchar_t* const kNames[2] = { L"dnGrenadeLauncherProjectile", L"dnRocket_SuperTurret" };
  const void* cls[2] = { 0, 0 };
  uobj_find_classes(kNames, cls, 2);   // one table pass, once a level while mounted
  s_mp.cls[0] = cls[0];
  s_mp.cls[1] = cls[1];
  for (int k = 0; k < 2; k++) {
    const void* def = cls[k] ? default_of(cls[k]) : 0;
    if (!def) continue;
    float f = 0.f;
    if (rd_raw(def, cls[k], N_DMGRADIUS, UW_FLOAT, 4, &f)) s_mp.radius[k] = f;
    s_mp.direct[k] = rd_bool_def(def, cls[k], N_DMGDIRECT);
    s_mp.breakGlass[k] = rd_bool_def(def, cls[k], N_BREAKGLASS);
  }
  if (const void* def = cls[0] ? default_of(cls[0]) : 0) {
    s_mp.found[0] = 1;
    s_mp.bounce = rd_bool_def(def, cls[0], N_BOUNCE);
    rd_obj(def, cls[0], N_DAMAGECLASS, &s_mp.dmg[0]);
  }
  if (const void* def = cls[1] ? default_of(cls[1]) : 0) {
    s_mp.found[1] = 1;
    rd_obj(def, cls[1], N_DAMAGECLASS, &s_mp.dmg[1]);
    float f = 0.f;
    if (rd_raw(def, cls[1], N_SPEED, UW_FLOAT, 4, &f)) s_mp.speed = f;
    if (rd_raw(def, cls[1], N_MAXSPEED, UW_FLOAT, 4, &f)) s_mp.maxSpeed = f;
    if (rd_raw(def, cls[1], N_ACCEL, UW_FLOAT, 4, &f)) s_mp.accel = f;
    if (rd_raw(def, cls[1], N_LIFESPAN, UW_FLOAT, 4, &f)) s_mp.life = f;
  }
  return s_mp;
}

// The launcher's next muzzle: PerformFire spawns at MuzzleNames[MuzzleIndex] after FireEffects stepped MuzzleIndex
// (0..2, -1 before the first shot), so the next shot's bone is MuzzleNames[(MuzzleIndex + 1) % 3]. 1 read and within
// 512 units of the gun (a missing bone reads 0,0,0).
static int next_muzzle_bone(const void* gun) {
  int idx = -1, bone = 0;
  const UProp p = prop(class_of(gun), N_MUZZLENAMES);
  if (!rd_int(gun, N_MUZZLEIDX, &idx) || !p.ok || p.elemSize != 4) return 0;
  const int next = idx < 0 || idx >= 2 ? 0 : idx + 1;
  __try {
    bone = uw_int(gun, p.offset + 4 * next);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
  return bone;
}
static int launcher_muzzle(const void* gun, float out[3]) {
  const int bone = next_muzzle_bone(gun);
  float loc[3];
  if (!bone || !uname_str(bone) || !rd_vec(gun, N_LOCATION, loc) || !bone_world((void*)gun, bone, out)) return 0;
  const float dx = out[0] - loc[0], dy = out[1] - loc[1], dz = out[2] - loc[2];
  return dx * dx + dy * dy + dz * dz < 512.f * 512.f;
}

// The mounted turret (docs/re/turrets.md section 6): PlayerPawn.InputHookActor IsA dnControlRemapperEx_Turret, its
// CurrentRemapperState CRS_FullyAttached and its MountParent IsA dnControl_Turret. The gun's class once per gun; its
// limits, rotation, ShotDistance and GrenadeVelocity every frame (a level script may change them).
static void read_mount(void* player, int attached, Mount* m) {
  memset(m, 0, sizeof *m);
  static int s_nRemap, s_nTurret;
  if (!s_nRemap) s_nRemap = uname_find(L"dnControlRemapperEx_Turret");
  if (!s_nTurret) s_nTurret = uname_find(L"dnControl_Turret");
  const void* hook = 0;
  const void* gun = 0;
  int state = -1;
  if (!attached || !rd_obj(player, class_of(player), N_INPUTHOOK, &hook) || !hook || !chain_has(hook, s_nRemap))
    return;
  rd_byte(hook, N_CRSTATE, &state);
  rd_obj(hook, class_of(hook), N_MOUNTPARENT, &gun);
  m->on = aim_turret_mounted(1, state, gun && chain_has(gun, s_nTurret));
  if (!m->on) return;
  m->remapper = hook;
  m->gun = gun;
  m->gunName = class_name_idx(gun);
  if (gun == s_mt.gun && m->gunName == s_mt.gunName) {
    m->tur = s_mt.tur;
  } else {
    int names[16];
    int nn = 0;
    __try {
      for (const void* c = uobj_class(gun); c && nn < 16 && uobj_valid(c); c = uw_ptr(c, US_SUPER))
        names[nn++] = uobj_name(c);
    } __except (av_only(GetExceptionCode())) {
      ++s_faults;
    }
    const wchar_t* chain[16];
    for (int i = 0; i < nn; i++) chain[i] = uname_str(names[i]);
    m->tur = aim_turret_class(chain, nn);
  }
  float shot = 0.f, grenade = 0.f;
  rd_float(gun, N_SHOTDIST, &shot);
  rd_float(gun, N_GRENVEL, &grenade);
  m->k = aim_turret_kind(m->tur, shot, grenade);
  // The launcher's grenade falls in the gun's PhysicsVolume (physFalling reads the actor's own, +0x208).
  float terminal = 0.f;
  m->gravity[0] = m->gravity[1] = m->gravity[2] = 0.f;
  if (m->tur == AIM_TUR_LAUNCHER) {
    const void* vol = 0;
    if (rd_obj(gun, class_of(gun), N_PHYSVOL, &vol) && vol) {
      rd_vec(vol, N_GRAVITY, m->gravity);
      rd_float(vol, N_TERMINAL, &terminal);
    }
    m->muzzleOk = launcher_muzzle(gun, m->muzzle);
  }
  m->pm = aim_turret_model(m->tur, grenade, m->gravity[2], terminal, -1);
  if (m->tur == AIM_TUR_LAUNCHER || m->tur == AIM_TUR_SUPER) {   // the MG42 fires no projectile: no table pass
    const MountProj& mp = mount_proj();
    m->pm = aim_turret_model(m->tur, grenade, m->gravity[2], terminal, mp.bounce);
    if (m->tur == AIM_TUR_SUPER) m->pm = aim_pm_defaults(m->pm, mp.speed, mp.maxSpeed, mp.accel, mp.life);
  }
  // The gun's damage type, once per gun: the MG42's TraceFire passes its TraceDamageType; the launcher spawns
  // dnGrenadeLauncherProjectile and keeps its DamageClass; the SuperTurret's rocket is dnRocket_SuperTurret.
  if (gun == s_mt.gun && m->gunName == s_mt.gunName) {
    m->dmgType = s_mt.dmgType;
    m->dmgSrc = s_mt.dmgSrc;
  } else if (m->tur == AIM_TUR_MG42) {
    rd_obj(gun, class_of(gun), N_TRACEDT, &m->dmgType);
    m->dmgSrc = "the MG42's TraceDamageType";
  } else if (m->tur == AIM_TUR_LAUNCHER || m->tur == AIM_TUR_SUPER) {
    const int k = m->tur == AIM_TUR_SUPER;
    m->dmgType = mount_proj().dmg[k];
    m->dmgSrc = k ? "dnRocket_SuperTurret's DamageClass" : "dnGrenadeLauncherProjectile's DamageClass";
  }
  // The blast: the launcher writes its live GrenadeDamageRadius on each grenade after the spawn; the SuperTurret's
  // rocket keeps its class default. The MG42 fires none.
  if (m->tur == AIM_TUR_LAUNCHER || m->tur == AIM_TUR_SUPER) {
    const MountProj& mp = mount_proj();
    const int k = m->tur == AIM_TUR_SUPER;
    float radius = mp.radius[k];
    int readR = radius >= 0.f;
    if (!k) readR = rd_float(gun, N_GRENRADIUS, &radius);
    m->splash = splash_kind(1, mp.direct[k], 0, readR, radius);
    m->breakGlass = k ? mp.breakGlass[k] : 0;   // the launcher sets bExplodeOnAnyImpact: its grenade explodes on glass
    m->splashR = splash_radius(m->splash, radius);
  }
  m->limitsOk = rd_int(gun, N_MINPITCH, &m->lim.minPitch) && rd_int(gun, N_MAXPITCH, &m->lim.maxPitch) &&
                rd_int(gun, N_MINYAW, &m->lim.minYaw) && rd_int(gun, N_MAXYAW, &m->lim.maxYaw) &&
                rd_bool(gun, N_CLAMPYAW, &m->lim.clampYaw) && rd_int(gun, N_ROTPITCH, &m->rotP) &&
                rd_int(gun, N_ROTYAW, &m->rotY);
  m->baseOk = rd_rot(gun, N_ROTATION, m->base);   // the turret's own Rotation: the gun's base (aim_gun_view)
  m->instant = rd_bool_def(gun, class_of(gun), N_INSTANTAIM);   // the aimbot's snap or damped step
}

// Takes this frame's mount (read: read_mount ran), logging each attach (the gun, its limits and view offsets, the
// remapper state, whether ViewMapper and InteractiveDecoration are the turret's own) and each dismount, debounced.
// s_mt itself follows every frame: an unread frame is unmounted (the triggerbot releases on a pause).
static void mount_update(void* player, const Mount& m, int read, const void* vm, const void* ctrl) {
  const int ev = aim_mount_log_step(&s_mlog, read, m.on ? m.gun : 0);
  if ((ev == AIM_ML_DISMOUNT || ev == AIM_ML_SWITCH) && s_mountLogs < LOGS_PER_LEVEL) {
    s_mountLogs++;
    np_log("aim: dismounted from %ls", uname_str(s_mlogName));
  }
  s_mt = m;
  if (ev != AIM_ML_ATTACH && ev != AIM_ML_SWITCH) return;
  s_mlogName = m.gunName;
  if (s_mountLogs >= LOGS_PER_LEVEL) return;
  s_mountLogs++;
  int vrot[3] = { 0, 0, 0 }, state = -1;
  float vloc[3] = { 0.f, 0.f, 0.f };
  const int haveRot = rd_rot(m.gun, N_TVROT, vrot), haveLoc = rd_vec(m.gun, N_TVLOC, vloc);
  rd_byte(m.remapper, N_CRSTATE, &state);
  int calc[3] = { 0, 0, 0 };
  rd_rot(player, N_CALCROT, calc);
  static const char* const KINDS[3] = { "not aimed", "hitscan", "projectile" };
  static const char* const MODELS[3] = { "hitscan", "straight", "ballistic" };
  const UProp bb = prop(class_of(m.gun), N_BOUNCE);
  float cam[3] = { 0.f, 0.f, 0.f };
  rd_vec(player, N_VIEWLOC, cam);
  // The projectile model the intercept uses (aim.md section 8). Actor.bBounce's place checks the reading of
  // physFalling's +0x58 & 0x10 (half gravity, no TerminalVelocity clamp).
  np_log("aim: mounted %ls projectile model: %s, speed %.0f, accel %.0f/s to %.0f, gravity %.0f (PhysicsVolume.Gravity "
         "%.0f,%.0f,%.0f), TerminalVelocity %.0f, bBounce %d (%s; Actor.bBounce %s+0x%x mask 0x%x), LifeSpan %.1f (%s); origin "
         "%s, %.0f units from the camera", uname_str(m.gunName),
         m.pm.kind >= 0 && m.pm.kind <= AIM_PM_BALLISTIC ? MODELS[m.pm.kind] : "?", m.pm.speed, m.pm.accel,
         m.pm.maxSpeed, m.pm.gravity, m.gravity[0], m.gravity[1], m.gravity[2], m.pm.terminal, m.pm.bounce,
         s_mp.bounce >= 0 ? "dnGrenadeLauncherProjectile's default" : "unread: Projectile's default assumed",
         bb.ok ? "" : "unresolved ", bb.offset, bb.bitmask, m.pm.maxT,
         m.tur != AIM_TUR_SUPER ? "none" : s_mp.found[1] ? "dnRocket_SuperTurret's defaults" :
         "dnRocket_SuperTurret not found: built-in values",
         m.tur != AIM_TUR_LAUNCHER ? "the camera" : m.muzzleOk ? "the next muzzle bone" : "the camera (muzzle unread)",
         m.muzzleOk ? sqrtf((m.muzzle[0] - cam[0]) * (m.muzzle[0] - cam[0]) + (m.muzzle[1] - cam[1]) *
                            (m.muzzle[1] - cam[1]) + (m.muzzle[2] - cam[2]) * (m.muzzle[2] - cam[2])) : 0.f);
  np_log("aim: mounted %ls (remapper %ls, CurrentRemapperState %d): %s, range %.0f, speed %.0f; limits %spitch "
         "%d..%d, yaw %d..%d, bClampYaw %d; RotatePitch %d, RotateYaw %d, camera pitch %d yaw %d, Rotation %s%d,%d,%d, bUseInstantAimRate %d; "
         "TurretViewRotationOffset %s%d,%d,%d, TurretViewLocationOffset %s%.1f,%.1f,%.1f; ViewMapper %s, "
         "InteractiveDecoration %s", uname_str(m.gunName), uname_str(class_name_idx(m.remapper)), state,
         m.k.kind >= 0 && m.k.kind <= AIMW_PROJECTILE ? KINDS[m.k.kind] : "?",
         aim_range_for(1, 0, 0.f, m.k.kind, m.k.maxTrace), m.k.speed,
         m.limitsOk ? "" : "(unread, not applied) ", m.lim.minPitch, m.lim.maxPitch, m.lim.minYaw, m.lim.maxYaw,
         m.lim.clampYaw, m.rotP, m.rotY, aim_wrap16(calc[0]), aim_wrap16(calc[1]), m.baseOk ? "" : "unread ",
         m.base[0], m.base[1], m.base[2], m.instant, haveRot ? "" : "unread ", vrot[0],
         vrot[1], vrot[2], haveLoc ? "" : "unread ", vloc[0], vloc[1], vloc[2],
         !vm ? "None" : vm == m.remapper ? "the turret's remapper" : "another",
         !ctrl ? "None" : ctrl == m.gun ? "the turret" : "another");
  np_log("aim: mounted %ls splash: %s, radius %.0f", uname_str(m.gunName), m.splash == SPL_READ ? "DamageRadius read" :
         m.splash == SPL_DEFAULT ? "radius unread: the default" : "none", m.splashR);
}

// --- Silent aim on mounted guns (mountaim.h; docs/re/turrets.md section 8) ------------------------------------------------------
// Before the shot event: CalcedViewRotation toward the target (launcher: whole aim, others: pitch) and RotateYaw with
// the gun's UpdateRotations; after it both are put back while still ours. Both values the game writes itself.
static int super_muzzle(float out[3]);   // below, with the splash

// UObject.StateFrame +0x0C -> FStateFrame.StateNode +0x14 (turretwep.cpp's reading): the state's name, 0 unread.
static int state_name_of(const void* o) {
  __try {
    const void* sf = o ? uw_ptr(o, 0x0C) : 0;
    const void* node = sf ? uw_ptr(sf, 0x14) : 0;
    return node && uobj_valid(node) ? uobj_name(node) : 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}
static int outer_name_of(const void* o) {
  __try {
    const void* ou = o ? uobj_outer(o) : 0;
    return ou && uobj_valid(ou) ? uobj_name(ou) : 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

static bool wr_int(void* o, int k, int v) {
  const UProp p = prop(class_of(o), k);
  if (!p.ok || p.type != UW_INT || p.elemSize != 4) return false;
  __try {
    memcpy((char*)o + p.offset, &v, 4);
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

// Health as a float (a float or an int by class). 1 read.
static int health_of(const void* o, float* out) {
  const UProp p = prop(class_of(o), N_HEALTH);
  if (!p.ok || (p.type != UW_FLOAT && p.type != UW_INT)) return 0;
  __try {
    *out = p.type == UW_FLOAT ? *(const float*)((const char*)o + p.offset) : (float)uw_int(o, p.offset);
    return 1;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

// MeshInstance.BoneGetRotate(BoneName, bAbsolute=true, bDefault=false) on the actor's mesh: the bone's world rotation,
// as the turret's PerformFire and FireShot read it. 1 read.
static int bone_rot(const void* actor, int bone, int out[3]) {
  const void* mi = 0;
  if (!bone || !rd_obj(actor, class_of(actor), N_MESHINST, &mi) || !mi) return 0;
  const ScallFn f = scall_find((void*)mi, L"BoneGetRotate");
  static const void* s_fn;
  static ScallParm s_bn, s_abs, s_rv;
  static int s_ok;
  if (!f.ok) return 0;
  if (f.fn != s_fn) {
    s_fn = f.fn;
    ScallParm df;
    s_ok = scall_parm(f, L"BoneName", L"NameProperty", &s_bn) && s_bn.size == 4 &&
           scall_parm(f, L"bAbsolute", L"BoolProperty", &s_abs) && s_abs.size == 4 &&
           scall_parm(f, L"bDefault", L"BoolProperty", &df) && df.size == 4 &&
           scall_parm(f, L"ReturnValue", L"StructProperty", &s_rv) && s_rv.size == 12;
    if (!s_ok) np_log("aim: MeshInstance.BoneGetRotate's parameters did not match; mounted yaw not redirected");
  }
  if (!s_ok) return 0;
  unsigned char parms[64];
  memset(parms, 0, sizeof parms);   // bDefault false
  const int one = 1;
  if (f.parmsSize > sizeof parms || !scallparms_put(parms, f.parmsSize, s_bn.offset, &bone, 4) ||
      !scallparms_put(parms, f.parmsSize, s_abs.offset, &one, 4))
    return 0;
  if (!scall_call((void*)mi, f, parms, sizeof parms)) {
    ++s_refusedCalls;
    return 0;
  }
  memcpy(out, parms + s_rv.offset, 12);
  return 1;
}

// The gun's own dnControl_Turret.UpdateRotations (no parameters): the bones turned to RotateYaw and RotatePitch, as
// every PostUserTick does. 1 called.
static int update_rotations(void* gun) {
  const ScallFn f = scall_find(gun, L"UpdateRotations");
  unsigned char parms[64];
  memset(parms, 0, sizeof parms);
  if (!f.ok || f.parmsSize > sizeof parms) return 0;
  if (!scall_call(gun, f, parms, sizeof parms)) {
    ++s_refusedCalls;
    return 0;
  }
  return 1;
}

// The mounted gun can be bracketed this frame: a known class, the six subscriptions with their event names in the
// name table (pehook resolves them by the same uname_find), CalcedViewRotation; for a bone-yaw gun its limits,
// RotateYaw and UpdateRotations too. Locked mode counts a mounted shot as covered only then.
static const wchar_t* const MS_EVENT_NAMES[MS_HOOK_COUNT] = { L"FireEffects", L"BeginState", L"AnimCallback_AnimEnd" };
static int mount_bracketable(void* player) {
  if (!s_mt.on || s_mt.tur == AIM_TUR_NONE || !live(s_mt.gun)) return 0;
  static int s_evName[MS_HOOK_COUNT];   // a name once in the table stays there
  for (int k = 0; k < MS_SUBS; k++) {
    int& nm = s_evName[k / 2];
    if (!nm) nm = uname_find(MS_EVENT_NAMES[k / 2]);
    if (s_msSub[k] < 0 || !nm) return 0;
  }
  const UProp cr = prop(class_of(player), N_CALCROT);
  if (!cr.ok || cr.type != UW_STRUCT || cr.elemSize != 12 || !prop(class_of(player), N_VIEWLOC).ok) return 0;
  if (!ms_uses_yaw_bone(s_mt.tur)) return 1;
  const UProp ry = prop(class_of(s_mt.gun), N_ROTYAW);
  return s_mt.limitsOk && ry.ok && ry.type == UW_INT && scall_find((void*)s_mt.gun, L"UpdateRotations").ok;
}

// The shot being bracketed, for the after-handler's line; counted per 600 frames.
struct MsShot { int tur, hook, targetCls, bone, wantP, wantY, rot0[3], rot1[3], yaw, rotY0, rotY1, ym0, ym1, steps,
                muzzle0, left0, healthOk; float health0, dist, start[3], dir[3]; double ms; };
static MsShot s_ms;
static const void* s_msTarget;
static unsigned s_msSeen[MS_HOOK_COUNT], s_msOpened[MS_HOOK_COUNT], s_msShots[MS_HOOK_COUNT], s_msNoShot,
    s_msUnknownShot, s_msNoTarget, s_msOutside, s_msNoSolve, s_msUnread, s_msYawSteps, s_msYawResN, s_msHits,
    s_msMisses;
static int s_msYawResMax;
static double s_msYawResSum;
enum { MS_LOGS_PER_LEVEL = 32 };

// The event belongs to the player's mounted gun and this hook brackets that gun: FireEffects and BeginState raised on
// the gun (BeginState of its FireDown state), the anim end on the player while the gun is in FireDown.
static int ms_matches(int hook, const void* obj, const void* func) {
  static int s_nFireDown;
  if (!s_nFireDown) s_nFireDown = uname_find(L"FireDown");
  if (!s_mt.on || !obj || !s_nFireDown || !ms_hook_brackets(hook, s_mt.tur)) return 0;
  if (hook == MS_HOOK_ANIMEND) return obj == world_player() && state_name_of(s_mt.gun) == s_nFireDown;
  if (obj != s_mt.gun) return 0;
  return hook != MS_HOOK_BEGINSTATE || outer_name_of(func) == s_nFireDown;
}

static void ms_before(int hook, void* obj, void* func) {
  if (!s_msLive || !ms_matches(hook, obj, func)) return;
  ++s_msSeen[hook];
  if (s_mb.open) {   // raised inside an open bracket (BeginState inside the anim end): already aimed
    const int z[3] = { 0, 0, 0 };
    ms_open(&s_mb, hook, func, obj, z, z, 0, 0, 0);
    return;
  }
  void* player = world_player();
  void* gun = (void*)s_mt.gun;
  void* tgt = (void*)s_target;
  if (s_gate != AIM_GO || !tgt || !live(tgt) || !player || !live(gun)) {
    ++s_msNoTarget;
    return;
  }
  LARGE_INTEGER t0;
  QueryPerformanceCounter(&t0);
  MsShot r;
  memset(&r, 0, sizeof r);
  r.tur = s_mt.tur;
  r.hook = hook;
  r.targetCls = s_targetClsName;
  r.muzzle0 = -9;
  r.left0 = -1;
  float pt[3], eye[3];
  if (!rd_rot(player, N_CALCROT, r.rot0) || !rd_vec(player, N_VIEWLOC, eye) || !aim_point(tgt, pt, &r.bone)) {
    ++s_msUnread;
    return;
  }
  // The shot's origin: the MG42 traces from the camera, the launcher spawns at its next muzzle, the SuperTurret's
  // rocket at mount_beam (the camera when unread).
  float o[3] = { eye[0], eye[1], eye[2] }, mz[3];
  if (r.tur == AIM_TUR_LAUNCHER && launcher_muzzle(gun, mz)) memcpy(o, mz, sizeof o);
  if (r.tur == AIM_TUR_SUPER && super_muzzle(mz)) memcpy(o, mz, sizeof o);
  float dir[3] = { pt[0] - o[0], pt[1] - o[1], pt[2] - o[2] };
  const AimProjModel m = w_model();
  if (m.kind != AIM_PM_NONE) {
    float vel[3] = { 0.f, 0.f, 0.f };
    if (g_cfg.aimLead && !spawnbox_helper_vel(tgt, vel)) rd_vec(tgt, N_VELOCITY, vel);
    const AimIntercept ic = aim_intercept(m, o, pt, vel);
    if (!ic.ok) {
      ++s_msNoSolve;
      return;
    }
    memcpy(dir, ic.dir, sizeof dir);
  }
  aim_rotator_to(dir[0], dir[1], dir[2], &r.wantP, &r.wantY);
  r.dist = sqrtf((pt[0] - o[0]) * (pt[0] - o[0]) + (pt[1] - o[1]) * (pt[1] - o[1]) + (pt[2] - o[2]) * (pt[2] - o[2]));
  memcpy(r.start, o, sizeof o);
  aim_forward(r.wantP, r.wantY, r.dir);
  r.yaw = ms_uses_yaw_bone(r.tur);
  int bone = 0, mr[3];
  if (r.yaw) {
    if (!s_beamName) s_beamName = uname_find(L"mount_beam");
    bone = r.tur == AIM_TUR_SUPER ? s_beamName : next_muzzle_bone(gun);
    if (!s_mt.limitsOk || !rd_int(gun, N_ROTYAW, &r.rotY0) || !bone_rot(gun, bone, mr)) {
      ++s_msUnread;
      return;
    }
    r.ym0 = r.ym1 = mr[1];
    if (!ms_yaw_plan(r.rotY0, r.ym0, r.wantY, s_mt.lim, &r.rotY1)) {
      ++s_msOutside;
      return;
    }
  }
  ms_rot_write(r.rot0, r.wantP, r.wantY, !r.yaw, r.rot1);
  if (!ms_open(&s_mb, hook, func, gun, r.rot0, r.rot1, r.yaw, r.rotY0, r.rotY1)) return;
  rd_int(gun, N_MUZZLEIDX, &r.muzzle0);
  int fl = 0;
  if (rd_bool(gun, N_FIRELEFT, &fl)) r.left0 = fl;
  r.healthOk = health_of(tgt, &r.health0);
  if (!wr_rot(player, N_CALCROT, r.rot1)) {   // nothing written, nothing to put back
    s_mb.open = 0;
    s_mb.closed++;
    return;
  }
  ++s_msOpened[hook];
  if (r.yaw && !wr_int(gun, N_ROTYAW, r.rotY1)) {
    s_mb.yaw = 0;
  } else if (r.yaw) {
    update_rotations(gun);
    r.steps = 1;
    if (bone_rot(gun, bone, mr)) r.ym1 = mr[1];
    int y2 = 0;
    if (ms_yaw_secant(r.rotY0, r.ym0, r.rotY1, r.ym1, r.wantY, s_mt.lim, &y2) && wr_int(gun, N_ROTYAW, y2)) {
      ms_yaw_wrote(&s_mb, y2);
      r.rotY1 = y2;
      update_rotations(gun);
      r.steps = 2;
      ++s_msYawSteps;
      if (bone_rot(gun, bone, mr)) r.ym1 = mr[1];
    }
    const int res = abs(aim_wrap16(r.wantY - r.ym1));
    s_msYawResSum += res;
    ++s_msYawResN;
    if (res > s_msYawResMax) s_msYawResMax = res;
    aim_forward(r.rot1[0], r.ym1, r.dir);   // the shot's line: the camera's pitch, the muzzle's yaw
  }
  r.ms = ms_since(t0);
  s_ms = r;
  s_msTarget = tgt;
}

static void ms_after(int hook, void* obj, void* func) {
  if (!s_mb.open || s_mb.hook != hook || s_mb.fn != func) return;
  if (hook == MS_HOOK_ANIMEND ? obj != world_player() : obj != s_mb.gun) return;
  void* player = world_player();
  void* gun = (void*)s_mb.gun;
  const MsShot r = s_ms;
  const int gunOk = live(gun);
  int muzzle1 = -9, left1 = -1, fl = 0;
  if (gunOk) {
    rd_int(gun, N_MUZZLEIDX, &muzzle1);
    if (rd_bool(gun, N_FIRELEFT, &fl)) left1 = fl;
  }
  const int shot = ms_shot_seen(r.tur, r.muzzle0, muzzle1, r.left0, left1);
  float h1 = 0.f;
  const int hOk = r.healthOk && s_msTarget && live(s_msTarget) && health_of(s_msTarget, &h1);
  int prot[3] = { 0, 0, 0 };
  const int protOk = r.tur == AIM_TUR_SUPER && gunOk && rd_rot(gun, N_PROJROT, prot);
  int nowRot[3] = { 0, 0, 0 }, nowYaw = 0;
  const int rotRead = player && rd_rot(player, N_CALCROT, nowRot);
  const int yawRead = s_mb.yaw && gunOk && rd_int(gun, N_ROTYAW, &nowYaw);
  MsRestore back;
  if (!ms_close(&s_mb, hook, func, gun, nowRot, rotRead, nowYaw, yawRead, &back)) return;
  if (back.yaw && wr_int(gun, N_ROTYAW, back.yawV)) update_rotations(gun);   // the bones and LastRotateYaw back too
  if (back.rot) wr_rot(player, N_CALCROT, back.rotV);
  ++(shot == 1 ? s_msShots[hook] : shot == 0 ? s_msNoShot : s_msUnknownShot);
  // TraceFire deals its damage inside the shot: the target's Health fell, or it did not.
  if (r.tur == AIM_TUR_MG42 && shot == 1 && hOk) ++(h1 < r.health0 ? s_msHits : s_msMisses);
  if (s_msLogs >= MS_LOGS_PER_LEVEL || shot != 1) return;
  const int n = ++s_msLogs;
  char extra[200];
  if (r.tur == AIM_TUR_MG42) {
    // The redirected line by the bullet's own trace (no spread): from the camera, the camera's pitch, the muzzle's yaw.
    int ray = -1;
    if (player && s_msTarget && live(s_msTarget)) {
      const float reach = s_mt.k.maxTrace > 0.f ? s_mt.k.maxTrace : AIM_TUR_MG42_RANGE;
      const float end[3] = { r.start[0] + r.dir[0] * reach, r.start[1] + r.dir[1] * reach,
                             r.start[2] + r.dir[2] * reach };
      const void* hit = 0;
      float hl[3];
      const unsigned t0 = s_ptTraces;
      const double ms0 = s_ptMs;
      const int called = bullet_trace(player, r.start, end, s_msTarget, &hit, hl);
      s_ptTraces = t0;
      s_ptMs = ms0;
      if (called > 0) ray = hit && live(hit) && (hit == s_msTarget || hit_clear(hit, s_msTarget));
    }
    _snprintf(extra, sizeof extra, "target Health %s%.0f -> %.0f (%s); the shot's line %s", hOk ? "" : "unread ",
              r.health0, h1, !hOk ? "unknown" : h1 < r.health0 ? "hit" : "no damage",
              ray < 0 ? "not traced" : ray ? "hits the target" : "misses the target");
  } else if (r.tur == AIM_TUR_SUPER) {
    _snprintf(extra, sizeof extra, "rocket ProjectileRotation %s%d,%d (%d,%d off the wanted)", protOk ? "" : "unread ",
              aim_wrap16(prot[0]), aim_wrap16(prot[1]), aim_wrap16(prot[0] - r.wantP), aim_wrap16(prot[1] - r.wantY));
  } else {
    _snprintf(extra, sizeof extra, "grenade direction read next frame");
    if (!s_msGren.on) {
      s_msGren.on = 1;
      s_msGren.gun = gun;
      s_msGren.frame = s_frameNo;
      s_msGren.shot = n;
      memcpy(s_msGren.dir, r.dir, sizeof r.dir);
    }
  }
  extra[sizeof extra - 1] = 0;
  np_log("aim: mounted shot %d, %ls via %s: target %ls (%ls) %.0f units from the origin; want pitch %d yaw %d; "
         "CalcedViewRotation %d,%d -> %d,%d; RotateYaw %s%d -> %d, muzzle yaw %d -> %d (%d off, %d step(s)); %s; put "
         "back: CalcedViewRotation %s, RotateYaw %s; %.3f ms", n, uname_str(s_mt.gunName), ms_hook_label(hook),
         uname_str(r.targetCls), r.bone ? uname_str(r.bone) : L"body", r.dist, aim_wrap16(r.wantP), aim_wrap16(r.wantY),
         aim_wrap16(r.rot0[0]), aim_wrap16(r.rot0[1]), aim_wrap16(r.rot1[0]), aim_wrap16(r.rot1[1]),
         r.yaw ? "" : "(not used) ", r.rotY0, r.rotY1, aim_wrap16(r.ym0), aim_wrap16(r.ym1),
         r.yaw ? aim_wrap16(r.wantY - r.ym1) : 0, r.steps, extra, back.rot ? "yes" : "no (not ours)",
         !r.yaw ? "-" : back.yaw ? "yes" : "no (not ours)", r.ms);
}

static void ms_b0(void* o, void* f, void* p) { (void)p; ms_before(MS_HOOK_FIREEFFECTS, o, f); }
static void ms_b1(void* o, void* f, void* p) { (void)p; ms_before(MS_HOOK_BEGINSTATE, o, f); }
static void ms_b2(void* o, void* f, void* p) { (void)p; ms_before(MS_HOOK_ANIMEND, o, f); }
static void ms_a0(void* o, void* f, void* p) { (void)p; ms_after(MS_HOOK_FIREEFFECTS, o, f); }
static void ms_a1(void* o, void* f, void* p) { (void)p; ms_after(MS_HOOK_BEGINSTATE, o, f); }
static void ms_a2(void* o, void* f, void* p) { (void)p; ms_after(MS_HOOK_ANIMEND, o, f); }

static void ms_subscribe(void) {
  const wchar_t* const* kNames = MS_EVENT_NAMES;
  static const PeHandler kBefore[MS_HOOK_COUNT] = { ms_b0, ms_b1, ms_b2 };
  static const PeHandler kAfter[MS_HOOK_COUNT] = { ms_a0, ms_a1, ms_a2 };
  for (int h = 0; h < MS_HOOK_COUNT; h++) {
    if (s_msSub[2 * h] < 0) s_msSub[2 * h] = pehook_subscribe(kNames[h], kBefore[h], "aim mounted");
    if (s_msSub[2 * h + 1] < 0) s_msSub[2 * h + 1] = pehook_subscribe_after(kNames[h], kAfter[h], "aim mounted");
  }
}

// At the frame start: a bracket whose after-handler never ran is closed now, by the same still-ours rule.
static void ms_frame_start_close(void) {
  if (!s_mb.open) return;
  void* player = world_player();
  void* gun = (void*)s_mb.gun;
  const int hook = s_mb.hook, gunOk = gun && live(gun);
  int nowRot[3] = { 0, 0, 0 }, nowYaw = 0;
  const int rotRead = player && rd_rot(player, N_CALCROT, nowRot);
  const int yawRead = s_mb.yaw && gunOk && rd_int(gun, N_ROTYAW, &nowYaw);
  MsRestore back;
  if (!ms_frame_start(&s_mb, nowRot, rotRead, nowYaw, yawRead, &back)) return;
  if (back.yaw && wr_int(gun, N_ROTYAW, back.yawV)) update_rotations(gun);
  if (back.rot) wr_rot(player, N_CALCROT, back.rotV);
  if (s_msLogs >= MS_LOGS_PER_LEVEL) return;
  s_msLogs++;
  np_log("aim: mounted bracket of %s left open to the frame start: put back CalcedViewRotation %s, RotateYaw %s",
         ms_hook_label(hook), back.rot ? "yes" : "no (not ours)", back.yaw ? "yes" : "no or not used");
}

// The launcher's grenade of the last logged shot, in this frame's object snapshot: of the gun's grenades the one
// nearest its muzzle; its Velocity's direction against the one written (physFalling has bent it by a frame).
static void ms_grenade_check(void) {
  if (!s_msGren.on || s_frameNo == s_msGren.frame) return;
  s_msGren.on = 0;
  const void* cls = s_mt.on && s_mt.gun == s_msGren.gun ? mount_proj().cls[0] : 0;
  float muzzle[3];
  if (!cls || !launcher_muzzle(s_msGren.gun, muzzle)) return;
  const void* best = 0;
  float bestD = 3.0e38f;
  int seen = 0;
  const int n = uobj_count();
  for (int i = 0; i < n; i++) {
    const void* o = uobj_at(i);
    if (!o || class_of(o) != cls) continue;
    const void* owner = 0;
    float loc[3];
    if (!rd_obj(o, cls, N_OWNER, &owner) || owner != s_msGren.gun || !rd_vec(o, N_LOCATION, loc)) continue;
    ++seen;
    const float d = (loc[0] - muzzle[0]) * (loc[0] - muzzle[0]) + (loc[1] - muzzle[1]) * (loc[1] - muzzle[1]) +
                    (loc[2] - muzzle[2]) * (loc[2] - muzzle[2]);
    if (d < bestD) {
      bestD = d;
      best = o;
    }
  }
  float v[3];
  int wp = 0, wy = 0, gp = 0, gy = 0;
  aim_rotator_to(s_msGren.dir[0], s_msGren.dir[1], s_msGren.dir[2], &wp, &wy);
  if (!best || !rd_vec(best, N_VELOCITY, v)) {
    np_log("aim: mounted shot %d grenade: not found (%d of the gun's grenades)", s_msGren.shot, seen);
    return;
  }
  aim_rotator_to(v[0], v[1], v[2], &gp, &gy);
  np_log("aim: mounted shot %d grenade: Velocity %.0f,%.0f,%.0f = pitch %d yaw %d; written pitch %d yaw %d (%d,%d off); "
         "%.0f units from the muzzle, %d of the gun's grenades", s_msGren.shot, v[0], v[1], v[2], gp, gy, wp, wy,
         aim_wrap16(gp - wp), aim_wrap16(gy - wy), sqrtf(bestD), seen);
}

// The 600-frame line: per hook the events seen on the mounted gun, the brackets and the shots inside them (whether
// AnimCallback_AnimEnd comes once per SuperTurret shot), and why a shot was not redirected.
static void ms_frame_log(void) {
  static unsigned s_f;
  if (++s_f < 600) return;
  s_f = 0;
  unsigned any = s_mb.opened + s_mb.nested + s_msNoTarget + s_msOutside + s_msNoSolve + s_msUnread;
  for (int h = 0; h < MS_HOOK_COUNT; h++) any += s_msSeen[h];
  if (any)
    np_log("aim: mounted silent aim, 600 frames: seen FireEffects %u, FireDown.BeginState %u, AnimCallback_AnimEnd in "
           "FireDown %u; bracketed %u/%u/%u, a shot inside %u/%u/%u, no shot %u, unknown %u; brackets opened %u closed %u "
           "nested %u left open %u; put back refused (not ours): CalcedViewRotation %u, RotateYaw %u; not redirected: "
           "no target %u, outside the yaw limits %u, no solution %u, unread %u; muzzle yaw off avg %.0f max %d (%u "
           "second step(s)); MG42 target Health fell %u, did not %u", s_msSeen[0], s_msSeen[1], s_msSeen[2],
           s_msOpened[0], s_msOpened[1], s_msOpened[2], s_msShots[0], s_msShots[1], s_msShots[2], s_msNoShot,
           s_msUnknownShot, s_mb.opened, s_mb.closed, s_mb.nested, s_mb.leftOpen, s_mb.rotNotOurs, s_mb.yawNotOurs,
           s_msNoTarget, s_msOutside, s_msNoSolve, s_msUnread, s_msYawResN ? s_msYawResSum / s_msYawResN : 0.0,
           s_msYawResMax, s_msYawSteps, s_msHits, s_msMisses);
  memset(s_msSeen, 0, sizeof s_msSeen);
  memset(s_msOpened, 0, sizeof s_msOpened);
  memset(s_msShots, 0, sizeof s_msShots);
  s_msNoShot = s_msUnknownShot = s_msNoTarget = s_msOutside = s_msNoSolve = s_msUnread = s_msYawSteps = 0;
  s_msYawResN = s_msHits = s_msMisses = 0;
  s_msYawResMax = 0;
  s_msYawResSum = 0.0;
  s_mb.opened = s_mb.closed = s_mb.nested = s_mb.leftOpen = s_mb.rotNotOurs = s_mb.yawNotOurs = 0;
}

void aim_frame(int W, int H, int inLevel, int panelOpen) {
  ++s_frameNo;
  if (s_subOpen < 0) s_subOpen = pehook_subscribe(L"UpdateWeaponTraceVars", on_trace_vars, "aim");
  if (s_subClose < 0) s_subClose = pehook_subscribe(L"RecordShot", on_record_shot, "aim");
  ms_subscribe();
  if (!s_why && (s_subOpen < 0 || s_subClose < 0)) refuse("the ProcessEvent slot's subscriber table is full");
  if (!s_why && pehook_refused()) refuse(pehook_refused());
  // A new level first: a bracket of the old player is dropped with it, never written to the new one.
  const unsigned level = world_level_serial();
  if (level != s_level) new_level(level);
  frame_end_bracket();
  ms_frame_start_close();
  void* player = world_player();
  // Outside standalone play the gate stays off: no pick, no bracket (mpgate.h).
  const char* mp = mp_refused();
  const int want = g_cfg.silentOn && !s_why && !mp;
  // The aimbot's hold rule this frame: with its key up nothing is picked for it (AA: nothing to do, nothing to pay).
  const int aimbotWant = aimbot_engaged();
  // The Triggerbot and the aimbot share the player's gate and the weapon (aim_gate_now, aim_weapon).
  const int need = (g_cfg.silentOn || g_cfg.tbotOn || g_cfg.aimbotOn) && !s_why && !mp;
  int base = AIM_G_OFF;
  Mount mount;
  int mountRead = 0;
  if (need && inLevel && player) {
    if (!s_checked) {
      s_checked = 1;
      check(player);
    }
    PlayerRd pr;
    base = AIM_G_LEVEL;   // a fault this frame: tried again next frame
    if (!s_why && read_player(player, &pr) > 0) {
      read_weapon(player);
      float ds = 0.f, dd = 0.f;
      const void* pc = class_of(player);
      const void* pdef = default_of(pc);
      s_drawRatio = rd_float(player, N_DRAWSCALE, &ds) && pdef && rd_raw(pdef, pc, N_DRAWSCALE, UW_FLOAT, 4, &dd) &&
                    ds > 0.f && dd > 0.f ? ds / dd : 1.f;
      s_footGravZ = s_footTerm = 0.f;
      if (s_weap.cfg && s_weap.kindBy == AIMW_BY_LOB) {   // the spit falls in the volume it spawns in: the player's
        const void* vol = 0;
        float g[3];
        if (rd_obj(player, pc, N_PHYSVOL, &vol) && vol) {
          if (rd_vec(vol, N_GRAVITY, g)) s_footGravZ = g[2];
          rd_float(vol, N_TERMINAL, &s_footTerm);
        }
      }
      read_mount(player, pr.attached, &mount);
      mount_update(player, mount, 1, pr.vm, pr.ctrl);
      mountRead = 1;
      note_dmg_type();
      const AimGate g = { 1, panelOpen, inLevel, pr.alive, pr.grabbing, pr.attached,
                          aim_mount_closes(s_mt.on, pr.vm, s_mt.remapper), aim_mount_closes(s_mt.on, pr.ctrl, s_mt.gun),
                          w_kind(), s_mt.on };
      base = aim_gate(g);
      // The fists' reach this frame (shrunk or not); MightyFoot's Ammo may be None, so never asked about ammo.
      s_reach = 0.f;
      if (base == AIM_GO && w_kind() == AIMW_MELEE) {
        static BoolFnCache s_shrinkFn;
        float scale = 1.f;
        rd_float(player, N_SHRINKSCALE, &scale);
        s_reach = aim_melee_reach(s_weap.meleeTrace, s_weap.meleeShrunk, s_weap.meleeExt,
                                  call_bool(player, L"ShrinkingActive", &s_shrinkFn) > 0, scale);
      }
      // The aimbot's out-of-ammo rule: the game's own answer, once a frame while it could matter.
      s_outOfAmmo = -1;
      if (base == AIM_GO && aimbotWant && !s_mt.on && aim_gun_kind(s_weap.kind) && s_weapObj && live(s_weapObj)) {
        static BoolFnCache s_ammoFn;
        s_outOfAmmo = call_bool((void*)s_weapObj, L"IsCompletelyOutOfAmmo", &s_ammoFn);
      }
    }
  } else if (need) {
    base = panelOpen ? AIM_G_PANEL : AIM_G_LEVEL;
  }
  if (!mountRead) {
    memset(&mount, 0, sizeof mount);
    mount_update(player, mount, 0, 0, 0);
  }
  if (s_mt.on) ++s_mountFrames;
  gun_view();
  if (base != AIM_GO) s_outOfAmmo = -1;
  s_gateNow = s_why || mp ? AIM_G_OFF : base;
  // A mounted gun's shot is bracketed in its own events (mountaim.h; turrets.md section 8).
  s_msReady = mountRead && player ? mount_bracketable(player) : 0;
  const int gate = want ? aim_silent_gate(base, s_mt.on, s_msReady) : AIM_G_OFF;
  note_gate(gate);
  s_gate = gate;
  // An aimbot held back by an empty gun needs no pick of its own.
  const int aimbotPicks = aimbotWant && !aimbot_out_of_ammo(w_kind(), s_outOfAmmo);
  s_pawnCands = -1;
  s_bossEval = 0;
  s_bossState = AIM_BOSS_NONE;
  s_boss = 0;
  s_bossCls = 0;
  if (s_gateNow == AIM_GO && (want || aimbotPicks) && player) pick_target(player, W, H);
  else {
    if (s_gateNow == AIM_GO && g_cfg.tbotOn && player) {
      s_pawnCands = count_pawn_cands(player, W, H);
      // The Triggerbot alone: the boss judged on screen, without the pick's cone (aim.md section 12).
      const WView v = world_view();
      if (v.ok) {
        float fwd[3], d = 0.f, ang = 0.f;
        aim_forward(v.pitch, v.yaw, fwd);
        int traces = 0, idx = -1;
        const float range = aim_range_for(s_mt.on, g_cfg.aimRangeM, ESP_UU_PER_METRE, w_kind(), w_max());
        const int st = boss_eval(player, v, fwd, range, W, H, 0, &traces, &idx, &d, &ang);
        boss_note(st, idx);
      }
    }
    s_target = 0;
    s_targetShip = 0;
    s_targetAch = 0;
    s_passedDoomed = 0;
  }
  s_ready = gate == AIM_GO && player && s_target &&
            (s_mt.on ? s_msReady : s_weap.kind != AIMW_MELEE && redirect_ready(player));
  marker_points();
  s_live = want;
  if (s_subOpen >= 0) pehook_enable(s_subOpen, want);
  if (s_subClose >= 0) pehook_enable(s_subClose, want);
  s_msLive = want && s_mt.on && s_msReady;
  for (int k = 0; k < MS_SUBS; k++)
    if (s_msSub[k] >= 0) pehook_enable(s_msSub[k], s_msLive);
  ms_grenade_check();
  ms_frame_log();
  uname_dlog_flush();   // uprop's lines, outside every guard
  if (!s_windowStart) s_windowStart = GetTickCount();
  if (++s_frames < 600) return;
  s_frames = 0;
  const unsigned now = GetTickCount();
  const double secs = (now - s_windowStart) / 1000.0;
  s_windowStart = now;
  // The target ship lost, by the first test it failed (note_ship_lost).
  char lost[320];
  unsigned lostN = 0;
  int lo = 0;
  lost[0] = 0;
  for (int r = AC_LOST_NONE + 1; r < AC_LOST_COUNT; r++) {
    if (!s_shipLost[r]) continue;
    lostN += s_shipLost[r];
    const int w = _snprintf(lost + lo, sizeof lost - lo, "%s%s %u", lo ? ", " : "", ac_lost_name(r), s_shipLost[r]);
    if (w < 0) break;
    lo += w;
  }
  lost[sizeof lost - 1] = 0;
  memset(s_shipLost, 0, sizeof s_shipLost);
  const unsigned gvGun = s_gunViewGun, gvCam = s_gunViewCam;
  s_gunViewGun = s_gunViewCam = 0;
  if (g_cfg.silentOn || g_cfg.aimbotOn || s_uwtv || s_br.opened)
    np_log("aim: 600 frames: %s, gate %s; UpdateWeaponTraceVars %u, RecordShot %u; shots redirected %u, no target "
           "%u, no ammo %u; brackets opened %u closed %u reopened %u left open %u (restored %u); target frames %u, "
           "gated %u; probes %u: head %u, chest %u, hips %u, knees %u, render box %u, mounted collision %u (%u trace(s)), none %u (point traces %u, avg %.3f ms); hidden "
           "point traces by hit: world %u, player/self %u, target attachment %u, other pawn %u, door/mover %u, "
           "other %u; bullet-transparent blocker skipped %u, passed as the bullet does %u; "
           "triggerbot-fallback LineOfSightTo %u (%.1f/s, avg %.3f ms, max %.3f ms); probes hidden by the bullet trace %u, ally in the way %u, frozen skipped %u; "
           "bone reads %u (avg %.3f ms, off the pawn %u); handler avg %.3f ms max %.3f; %s, out of the cone %u; "
           "excluded: animated away without a render box %u, teleporting %u, invulnerable %u, player damage 0 %u, neutral zone %u, wrong damage type %u, expanded or popping %u, doomed %u; admitted in a cut-scene %u, at the render box %u (box traces %u); intercepts solved %u (ballistic %u), no solution %u (frames x targets; skipped while recent %u), shots out of reach %u, candidates out of reach %u (projectile scale %.2f; %u solve(s), avg %.3f ms, max %.3f ms); bullet trace clear by hitting the target %u, clear with no hit %u; mounted %u frame(s), outside the turret's limits %u, limits asked from the gun's rotation %u frame(s), from the camera %u; aircraft target %u frame(s), skipped for the weapon %u, out of the cone %u, outside the turret's limits %u (kept by the hull's slack %u), ally in the way %u, hidden %u, doomed %u; ship target lost %u (%s); achievement target %u frame(s), skipped for the damage type %u, doomed %u, out of range or off screen %u, outside the turret's limits %u, out of the cone %u, ally in the way %u, hidden %u; %u call(s) refused, %u fault(s)", g_cfg.silentOn ? (s_why ? "unavailable" : "on") : "off",
           aim_gate_name(s_gate), s_uwtv, s_rec,
           s_redirected, s_noTarget, s_noAmmo, s_br.opened, s_br.closed, s_br.reopened, s_br.leftOpen,
           s_br.restoredLate, s_picked, s_gated, s_probes, s_picks[AIM_PT_HEAD], s_picks[AIM_PT_CHEST],
           s_picks[AIM_PT_HIPS], s_picks[AIM_PT_KNEES], s_picks[PICK_BOX], s_picks[PICK_HELPER], s_helperTraces,
           s_picks[AIM_PT_NONE], s_ptTraces,
           s_ptTraces ? s_ptMs / s_ptTraces : 0.0, s_hidHits[AIM_HIT_WORLD], s_hidHits[AIM_HIT_SELF],
           s_hidHits[AIM_HIT_TARGET_ATT], s_hidHits[AIM_HIT_PAWN], s_hidHits[AIM_HIT_DOOR], s_hidHits[AIM_HIT_OTHER],
           s_skipped, s_bulletPassed, s_traces, secs > 0.0 ? s_traces / secs : 0.0,
           s_traces ? s_traceMs / s_traces : 0.0, s_traceMaxMs, s_hidden, s_allyBlocked, s_frozenSkipped, s_boneReads,
           s_boneReads ? s_boneMs / s_boneReads : 0.0, s_boneFails, s_redirected ? s_handlerMs / s_redirected : 0.0,
           s_handlerMaxMs, aim_pick_label(g_cfg.aimTarget), s_outOfCone, s_exFar, s_exTele, s_exNoDmg, s_exDmg0, s_exNeutral, s_exWrongType, s_exExpanded, s_exDoomed, s_cutAdmitted, s_boxAimed, s_boxTraces, s_icSolved, s_icBallistic, s_icNone, s_icSkipped, s_shotsOutOfReach, s_unreachable, s_drawRatio, s_icCalls, s_icCalls ? s_icMs / s_icCalls : 0.0, s_icMaxMs, s_clearTarget, s_clearNoHit, s_mountFrames, s_outOfLimits, gvGun, gvCam, s_shipFrames, s_shipWeaponSkips, s_shipCone, s_shipLimits, s_shipLimitKeeps, s_shipAlly, s_shipHidden, s_shipDoomed, lostN, lost, s_achFrames, s_achType, s_achDoomed, s_achRange, s_achLimits, s_achCone, s_achAlly, s_achHidden, s_refusedCalls, s_faults);
  if (g_cfg.aimDebugDoubleTrace || s_dblTraces)
    np_log("aim: debug double trace: %u old-flag re-trace(s), avg %.3f ms (not in the point-trace stats)", s_dblTraces,
           s_dblTraces ? s_dblMs / s_dblTraces : 0.0);
  s_dblTraces = 0;
  s_dblMs = 0.0;
  s_uwtv = s_rec = s_redirected = s_noTarget = s_noAmmo = s_picked = s_gated = s_traces = s_hidden = 0;
  s_allyBlocked = s_frozenSkipped = s_boneReads = s_boneFails = s_refusedCalls = s_faults = s_outOfCone = 0;
  s_cutAdmitted = s_boxAimed = s_boxTraces = s_helperTraces = 0;
  s_exFar = s_exTele = s_exNoDmg = s_exDmg0 = s_exNeutral = s_exWrongType = s_exExpanded = s_clearTarget = s_clearNoHit = s_probes = s_ptTraces = 0;
  memset(s_hidHits, 0, sizeof s_hidHits);
  s_skipped = s_bulletPassed = s_mountFrames = s_outOfLimits = s_shipFrames = s_shipWeaponSkips = 0;
  s_shipCone = s_shipLimits = s_shipAlly = s_shipHidden = s_shipLimitKeeps = s_exDoomed = s_shipDoomed = 0;
  s_achFrames = s_achType = s_achDoomed = s_achRange = s_achLimits = s_achCone = s_achAlly = s_achHidden = 0;
  // Boss priority (aim.md section 12): only while a boss meter was up.
  unsigned bossSeen = 0;
  for (int k = AIM_BOSS_NONE + 1; k < AIM_BOSS_COUNT; k++) bossSeen += s_bossFrames[k];
  if (bossSeen) {
    char off[512];
    int oo = 0;
    off[0] = 0;
    for (int k = AIM_BOSS_ON + 1; k < AIM_BOSS_COUNT; k++) {
      if (!s_bossFrames[k]) continue;
      const int w = _snprintf(off + oo, sizeof off - oo, "%s%s %u", oo ? ", " : "", aim_boss_label(k), s_bossFrames[k]);
      if (w < 0) break;
      oo += w;
    }
    off[sizeof off - 1] = 0;
    np_log("aim: boss priority, 600 frames: on the boss %u frame(s), not possible %u (%s), no boss meter %u; the "
           "Triggerbot held off an add %u frame(s) (%u time(s))", s_bossFrames[AIM_BOSS_ON],
           bossSeen - s_bossFrames[AIM_BOSS_ON], oo ? off : "-", s_bossFrames[AIM_BOSS_NONE], s_bossSkipFrames,
           s_bossSkips);
  }
  memset(s_bossFrames, 0, sizeof s_bossFrames);
  s_bossSkipFrames = s_bossSkips = 0;
  s_icSolved = s_icBallistic = s_icNone = s_unreachable = s_icCalls = s_shotsOutOfReach = s_icSkipped = 0;
  s_icMs = s_icMaxMs = 0.0;
  memset(s_picks, 0, sizeof s_picks);
  s_ptMs = 0.0;
  s_br.opened = s_br.closed = s_br.reopened = s_br.leftOpen = s_br.restoredLate = 0;
  s_traceMs = s_traceMaxMs = s_boneMs = s_handlerMs = s_handlerMaxMs = 0.0;
}

const char* aim_refused(void) { return s_why ? s_why : mp_refused(); }

const void* aim_target(void) { return s_gate == AIM_GO ? s_target : 0; }

int aim_gate_now(void) { return s_gateNow; }

const void* aim_target_shared(void) { return s_gateNow == AIM_GO ? s_target : 0; }

int aim_track(const void* target, const float eye[3], int pitch, int yaw, float out[3], const char** why) {
  *why = 0;
  // A ship stays a target only while this frame's scan still holds it hostile and live (aircraft.h).
  // An achievement target only while achhelp still offers it (its row on, alive, damageable).
  const int isLive = target && target == s_target && live(target) && (!s_targetShip || aircraft_find(target)) &&
                     (!s_targetAch || achhelp_aim_find(target));
  int alive = 0;
  // Health unread: dropped, never followed blind. A ship's Health > 0 is part of aircraft_find's hostile and live,
  // an achievement target's of ah_aim_target_ok.
  if (isLive && (s_targetShip || s_targetAch)) alive = 1;
  else if (isLive && !rd_alive(target, &alive)) alive = 0;
  float pt[3] = { 0.f, 0.f, 0.f };
  int bone = 0, vis = AIM_VIS_UNKNOWN, inCone = 0;
  const int havePoint = isLive && alive && aim_point((void*)target, pt, &bone);
  if (havePoint) {
    vis = aim_vis_get(&s_vis, target, GetTickCount(), 0);
    float fwd[3];
    aim_forward(pitch, yaw, fwd);
    inCone = aim_cone_admits(g_cfg.aimTarget, fwd, eye, pt, g_cfg.aimCone);   // the pick's own test (cone_ok)
  }
  if (!aimbot_keep(isLive, alive, vis, inCone)) {
    *why = !isLive ? "gone" : !alive ? "dead" : !havePoint ? "no point" : vis == AIM_VIS_NO ? "hidden" :
           "left the cone";
    return 0;
  }
  if (s_targetAch && !aim_ach_shootable(target)) {
    *why = "the held gun's damage type cannot hurt it";
    return 0;
  }
  if (!s_targetShip && !s_targetAch) {   // flagged since the pick: dropped at once (pawns only)
    if (const int ex = spawnbox_excluded(target)) {
      *why = ex == SB_EXCL_DOOMED ? "doomed by the projectiles in flight" :
             ex == SB_EXCL_WRONGTYPE ? "the held gun's damage type cannot hurt it" :
             ex == SB_EXCL_EXPANDED ? "fully expanded or popping (the Expander)" :
             "animated away without a render box, teleporting or invulnerable";
      return 0;
    }
  } else if (overkill_doomed(target)) {
    *why = "doomed by the projectiles in flight";
    return 0;
  }
  if (!solve(target, pt, eye, out, 1)) {
    *why = "out of the projectile's reach";
    return 0;
  }
  return 1;
}

int aim_redirect_ready(void) { return s_ready; }

int aim_is_ship(const void* t) { return t && aircraft_find(t) != 0; }

int aim_ship_shootable(const void* t) {
  const AcShip* s = t ? aircraft_find(t) : 0;
  return s && s->damageable && w_hurts_air();
}

int aim_pawn_cands(void) { return s_pawnCands; }

int aim_boss_on(void) { return s_bossEval && s_bossState == AIM_BOSS_ON && s_boss; }

const void* aim_boss(void) { return aim_boss_on() ? s_boss : 0; }

void aim_boss_skip(int began) {
  ++s_bossSkipFrames;
  if (began) ++s_bossSkips;
}

int aim_is_ach(const void* t) { return t && achhelp_aim_find(t) != 0; }

int aim_ach_shootable(const void* t) {
  const AhAimTarget* a = t ? achhelp_aim_find(t) : 0;
  return a && a->typeOk;
}

int aim_mounted(void) { return s_mt.on; }

const void* aim_mount_gun(void) { return s_mt.on ? s_mt.gun : 0; }

int aim_proj_class_name(void) {
  if (s_mt.on) {   // the launcher's PerformFire spawns dnGame.dnGrenadeLauncherProjectile (turrets.md section 3)
    static int s_grenade;
    if (!s_grenade) s_grenade = uname_find(L"dnGrenadeLauncherProjectile");
    return s_mt.tur == AIM_TUR_LAUNCHER ? s_grenade : 0;
  }
  return s_weap.cfg && s_weap.kind == AIMW_PROJECTILE ? s_weap.projName : 0;
}

int aim_passed_doomed(void) { return s_passedDoomed; }

const void* aim_damage_type(void) {
  if (s_mt.on) return s_mt.dmgType;
  return s_weap.cfg ? s_weap.dmgType : 0;
}

AimProjModel aim_proj_model(void) { return w_model(); }

int aim_mount_gun_view(int out[2]) {
  out[0] = s_gunView[0];
  out[1] = s_gunView[1];
  return s_gunViewFromGun;
}

int aim_mount_instant(void) { return s_mt.on ? s_mt.instant : -1; }

int aim_mount_view_offset(int out[3]) {
  out[0] = out[1] = out[2] = 0;
  if (!s_mt.on || !live(s_mt.remapper) || !rd_rot(s_mt.remapper, N_VIEWOFFSET, out)) {
    out[0] = out[1] = out[2] = 0;   // a partial copy is never used
    return 0;
  }
  return 1;
}

int aim_out_of_ammo(void) { return s_outOfAmmo; }

AimWeapon aim_weapon(void) {
  if (s_mt.on) {   // the mounted gun: cfgName is its class
    const AimWeapon m = { s_mt.k.kind, s_mt.k.maxTrace, s_mt.gunName, 0.f };
    return m;
  }
  const AimWeapon w = { s_weap.cfg ? s_weap.kind : AIMW_NONE, s_weap.maxTrace, s_weap.cfg ? s_weap.cfgName : 0,
                        s_weap.cfg && s_weap.kind == AIMW_MELEE ? s_reach : 0.f };
  return w;
}

void aim_vis_forget(const void* pawn) { aim_vis_drop(&s_vis, pawn); }

int aim_box_visible(const void* pawn) {
  void* player = world_player();
  const WView v = world_view();
  float bc[3], bh[3];
  if (!player || !v.ok || s_why || !pawn || !live(pawn) || !spawnbox_body(pawn, bc, bh)) return -1;
  const float eye[3] = { v.x, v.y, v.z };
  const int hv = probe_helpers(player, eye, (void*)pawn, 0);   // the box is its mounted collision's: trace those
  if (hv >= 0) return hv;
  return trace_point(player, eye, bc, pawn, AIM_PT_NONE);
}

int aim_line_of_sight(const void* pawn) {
  void* player = world_player();
  return player && pawn ? line_of_sight(player, pawn) : 0;
}

// ViewRotation, not the rendered CalcedViewRotation: the aimbot's turn lands in ViewRotation at the tick and the
// next shot leaves along it plus the shake (no recoil: zero), while Calced lags until the next render.
static int view_eye_fwd(void* player, float eye[3], float fwd[3]) {
  int vr[3], sh[3];
  float cur = 0.f, def = 0.f;
  // Mounted: the turret fires from the camera along the camera's pitch (turrets.md section 3), the rendered view.
  if (s_mt.on) {
    if (!rd_vec(player, N_VIEWLOC, eye) || !rd_rot(player, N_CALCROT, vr)) return 0;
    aim_forward(vr[0], vr[1], fwd);
    return 1;
  }
  if (!camera_location(player, eye) && !rd_vec(player, N_VIEWLOC, eye)) return 0;
  if (!rd_rot(player, N_VIEWROT, vr)) return 0;
  // GetWeaponAim adds CurrentShake scaled by CurrentFOV / DefaultFOV; unread, the view alone.
  if (rd_rot(player, N_SHAKE, sh) && rd_float(player, N_CURFOV, &cur) && rd_float(player, N_DEFFOV, &def) &&
      cur > 1.f && cur < 179.f && def > 1.f && def < 179.f) {
    const double k = cur != def ? (double)cur / (double)def : 1.0;
    vr[0] += (int)floor(aim_wrap16(sh[0]) * k + 0.5);
    vr[1] += (int)floor(aim_wrap16(sh[1]) * k + 0.5);
  }
  aim_forward(vr[0], vr[1], fwd);
  return 1;
}

int aim_view_ray_hits(const void* target) {
  void* player = world_player();
  float eye[3], fwd[3];
  if (!player || !target || !live(target) || !view_eye_fwd(player, eye, fwd)) return -1;
  const float reach = w_max() > 0.f ? w_max() : 65536.f;
  const float end[3] = { eye[0] + fwd[0] * reach, eye[1] + fwd[1] * reach, eye[2] + fwd[2] * reach };
  const void* hit = 0;
  float loc[3];
  const unsigned t0 = s_ptTraces;
  const double ms0 = s_ptMs;
  const int called = bullet_trace(player, eye, end, target, &hit, loc);
  s_ptTraces = t0;   // the point-trace stats are the probes' alone
  s_ptMs = ms0;
  if (called <= 0) return -1;
  if (hit && !live(hit)) return -1;
  return hit && (hit == target || hit_clear(hit, target)) ? 1 : 0;   // nothing hit is a miss; hit_clear maps attachments
}

float aim_view_lead_deg(const void* target) {
  void* player = world_player();
  float eye[3], fwd[3], pt[3];
  int vr[3];
  const char* why = 0;
  if (!player || !target || !view_eye_fwd(player, eye, fwd) || !rd_rot(player, s_mt.on ? N_CALCROT : N_VIEWROT, vr) ||
      !aim_track(target, eye, vr[0], vr[1], pt, &why))
    return -1.f;
  return aim_angle(fwd, pt[0] - eye[0], pt[1] - eye[1], pt[2] - eye[2]);
}

float aim_view_off_deg(const void* target) {
  void* player = world_player();
  float eye[3], fwd[3], pt[3];
  int bone = 0;
  if (!player || !target || !live(target) || !view_eye_fwd(player, eye, fwd) || !aim_point((void*)target, pt, &bone))
    return -1.f;
  return aim_angle(fwd, pt[0] - eye[0], pt[1] - eye[1], pt[2] - eye[2]);
}

int aim_marker_points(float raw[3], float led[3]) {
  if (!s_mkOk) return 0;
  memcpy(raw, s_mkRaw, sizeof s_mkRaw);
  memcpy(led, s_mkLed, sizeof s_mkLed);
  return 1;
}

// --- Splash (splash.h; aim.md section 11) ------------------------------------------------------------------------------
// The player, the mounted gun, or an actor hanging from either: the projectile spawns clear of them, so a splash trace
// steps past such a hit instead of calling it the detonation point.
static int splash_self(const void* hit, const void* player) {
  if (!hit) return 0;
  if (hit == player || (s_mt.on && hit == s_mt.gun)) return 1;
  int steps = -1;
  if (tbot_attached_pawn(hit, hit_links, hit_is_target, 0, (void*)player, &steps) == player) return 1;
  return s_mt.on && s_mt.gun && tbot_attached_pawn(hit, hit_links, hit_is_target, 0, (void*)s_mt.gun, &steps) == s_mt.gun;
}

// A projectile explodes on the first thing it hits that it does not fly through: the shot-flag trace from a to b, no
// penetration budget, past the player, the mounted gun, what hangs from them and what splash_passes lets through. 1 a
// blocker short of the target (point in hl, class in *cls), 0 clear, -1 refused, too many passes or budget spent.
enum { AIM_SPLASH_CHORDS = 5, AIM_SPLASH_PASS_MAX = 6 };
static int splash_trace(void* player, SplashBudget* bud, const float a[3], const float b[3], const void* target,
                        float hl[3], int* cls) {
  float from[3] = { a[0], a[1], a[2] };
  const void* ignore = s_mt.on ? s_mt.gun : 0;
  static int s_nLevel, s_nGlass;
  if (!s_nLevel) s_nLevel = uname_find(L"LevelInfo");
  if (!s_nGlass) s_nGlass = uname_find(L"BreakableGlass");
  const int breakGlass = s_mt.on ? s_mt.breakGlass : s_weap.breakGlass;
  for (int k = 0; k <= AIM_SPLASH_PASS_MAX; k++) {
    if (!splash_budget_take(bud)) return -1;
    const void* hit = 0;
    if (trace_ex(player, from, b, aim_shot_trace_flags(), &hit, hl, ignore) <= 0) return -1;
    if (!hit) return 0;
    if (!live(hit)) {
      *cls = 0;
      return 1;
    }
    if (hit_clear(hit, target)) return 0;
    int unbreakable = -1;
    const int glass = chain_has(hit, s_nGlass);
    if (glass) rd_bool(hit, N_UNBREAKABLE, &unbreakable);
    if (chain_has(hit, s_nLevel) ||
        (!splash_self(hit, player) && !splash_passes(hit_response(hit), glass, breakGlass, unbreakable))) {
      *cls = class_name_idx(hit);
      return 1;
    }
    if (!aim_step_past(from, b, hl, from)) return 0;   // the line ends inside what it passed
    ignore = hit;
  }
  return -1;
}

// A falling projectile launched from o along dir at the model's speed, flown T seconds in AIM_SPLASH_CHORDS chords:
// splash_trace's answer for the first chord that is not clear.
static int splash_arc(void* player, SplashBudget* bud, const AimProjModel& m, const float o[3], const float dir[3],
                      float T, const void* target, float hl[3], int* cls) {
  float v0[3];
  aim_launch_vel(m, dir, v0);   // the spit's UpwardsBoost; 0 for the launcher
  float a[3] = { o[0], o[1], o[2] };
  for (int k = 1; k <= AIM_SPLASH_CHORDS; k++) {
    float b[3];
    aim_ballistic_pos(m, o, v0, T * (float)k / (float)AIM_SPLASH_CHORDS, b);
    const int r = splash_trace(player, bud, a, b, target, hl, cls);
    if (r != 0) return r;
    memcpy(a, b, sizeof a);
  }
  return 0;
}

// The SuperTurret's rocket leaves from its mount_beam bone (FireShot, DecoActivity SuperTurret_FireProjectile). 1 read
// and within 2048 units of the gun (a missing bone reads 0,0,0).
static int super_muzzle(float out[3]) {
  if (!s_beamName) s_beamName = uname_find(L"mount_beam");
  float loc[3];
  if (!s_beamName || !s_mt.gun || !rd_vec(s_mt.gun, N_LOCATION, loc) || !bone_world((void*)s_mt.gun, s_beamName, out))
    return 0;
  const float dx = out[0] - loc[0], dy = out[1] - loc[1], dz = out[2] - loc[2];
  return dx * dx + dy * dy + dz * dz < 2048.f * 2048.f;
}

// The candidate points (aim_splash). Fails closed: Duke's Location, the origin or the aim point unread, a trace refused,
// or the budget spent sets o->pts.fail, which holds an explosive shot.
static void splash_points(void* player, const void* target, AimSplash* o, SplashBudget* bud) {
  if (!rd_vec(player, N_LOCATION, o->duke)) {
    o->pts.fail = SPL_FAIL_DUKE;
    return;
  }
  rd_float(player, N_COLLRADIUS, &o->dukeR);   // unread: splash_cyl_dist's defaults
  rd_float(player, N_COLLHEIGHT, &o->dukeH);
  // The origin the projectile really uses: Weapon.ProjectileFire spawns at the camera; the launcher at its next muzzle
  // bone; the SuperTurret at mount_beam. fwd: the gun's current direction (view_eye_fwd: the next shot's).
  float origin[3], fwd[3], sm[3];
  if (!view_eye_fwd(player, origin, fwd)) {
    o->pts.fail = SPL_FAIL_ORIGIN;
    return;
  }
  if (s_mt.on && s_mt.tur == AIM_TUR_LAUNCHER && s_mt.muzzleOk) memcpy(origin, s_mt.muzzle, sizeof origin);
  else if (s_mt.on && s_mt.tur == AIM_TUR_SUPER && super_muzzle(sm)) memcpy(origin, sm, sizeof origin);
  else aim_lob_origin(origin, lob_held(), origin);
  float pt[3];
  int bone = 0;
  if (!aim_point((void*)target, pt, &bone)) {
    o->pts.fail = SPL_FAIL_POINT;
    return;
  }
  // (a) where the target is when the shot arrives (aim_intercept, as solve leads it); a falling grenade unsolved for the
  // moving target is solved for its current point; else where it is now.
  const AimProjModel m = w_model();
  float vel[3] = { 0.f, 0.f, 0.f };
  if (g_cfg.aimLead && !spawnbox_helper_vel(target, vel)) rd_vec(target, N_VELOCITY, vel);
  AimIntercept r = aim_intercept(m, origin, pt, vel);
  if (!r.ok && m.kind == AIM_PM_BALLISTIC) {
    const float still[3] = { 0.f, 0.f, 0.f };
    r = aim_intercept(m, origin, pt, still);
  }
  o->pts.ok[SPL_PT_TARGET] = 1;
  memcpy(o->pts.p[SPL_PT_TARGET], r.ok ? r.hit : pt, sizeof pt);
  const float* a = o->pts.p[SPL_PT_TARGET];
  int res[2] = { 0, 0 };
  int slot[2] = { SPL_PT_BLOCKER, SPL_PT_BLOCKER_GUN };
  if (m.kind == AIM_PM_BALLISTIC) {
    // (c) the arc to (a), and (c') the arc along the gun's current direction for as long (unsolved: the time to cover
    // the horizontal distance to the target at the launch's horizontal speed, 3 s at most).
    slot[0] = SPL_PT_ARC;
    slot[1] = SPL_PT_ARC_GUN;
    if (r.ok) res[0] = splash_arc(player, bud, m, origin, r.dir, r.t, target, o->pts.p[slot[0]], &o->blockerCls[slot[0]]);
    float T = r.t;
    if (!r.ok) {
      const float dx = pt[0] - origin[0], dy = pt[1] - origin[1];
      const float hs = m.speed * sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1]);
      T = hs > 1.f ? sqrtf(dx * dx + dy * dy) / hs : 3.f;
      if (!(T <= 3.f)) T = 3.f;
    }
    if (res[0] >= 0)
      res[1] = splash_arc(player, bud, m, origin, fwd, T, target, o->pts.p[slot[1]], &o->blockerCls[slot[1]]);
  } else {
    // (b) the straight path to (a), and (b') the gun's current line as far as (a) plus the blast's reach.
    res[0] = splash_trace(player, bud, origin, a, target, o->pts.p[slot[0]], &o->blockerCls[slot[0]]);
    const float dx = a[0] - origin[0], dy = a[1] - origin[1], dz = a[2] - origin[2];
    const float len = sqrtf(dx * dx + dy * dy + dz * dz) + SPLASH_MARGIN * o->radius + 64.f;
    const float end[3] = { origin[0] + fwd[0] * len, origin[1] + fwd[1] * len, origin[2] + fwd[2] * len };
    if (res[0] >= 0) res[1] = splash_trace(player, bud, origin, end, target, o->pts.p[slot[1]], &o->blockerCls[slot[1]]);
  }
  for (int i = 0; i < 2; i++) {
    if (res[i] > 0) o->pts.ok[slot[i]] = 1;
    if (res[i] < 0) o->pts.fail = bud->capped ? SPL_FAIL_CAP : SPL_FAIL_TRACE;
  }
}

// One judgement per target a frame: the boss verdict's call (boss_eval) serves the Triggerbot's on the same target, so a
// frame spends the trace cap once for it.
static struct { unsigned frame; const void* target; int ret; AimSplash sp; } s_spMemo;
static int aim_splash_run(const void* target, AimSplash* o);
int aim_splash(const void* target, AimSplash* o) {
  if (s_spMemo.target && s_spMemo.frame == s_frameNo && s_spMemo.target == target) {
    *o = s_spMemo.sp;
    return s_spMemo.ret;
  }
  const int r = aim_splash_run(target, o);
  s_spMemo.frame = s_frameNo;
  s_spMemo.target = target;
  s_spMemo.ret = r;
  s_spMemo.sp = *o;
  return r;
}
static int aim_splash_run(const void* target, AimSplash* o) {
  memset(o, 0, sizeof *o);
  void* player = world_player();
  if (s_gateNow != AIM_GO || !player || !target || !live(target)) return 0;
  o->gunName = s_mt.on ? s_mt.gunName : s_weap.cfg ? s_weap.cfgName : 0;
  o->kind = s_mt.on ? s_mt.splash : s_weap.cfg && s_weap.kind == AIMW_PROJECTILE ? s_weap.splash : SPL_NONE;
  if (o->kind == SPL_NONE) return 1;
  o->radius = s_mt.on ? s_mt.splashR : s_weap.splashR;
  if (!s_mt.on && s_weap.splashDoubles) {   // dnRocket_Devastator.PostVerifySelf: twice the radius while shrunk
    static BoolFnCache s_shrunkFn;
    o->radius = splash_shrunk_radius(o->radius, 1, call_bool(player, L"IsMostlyShrunk", &s_shrunkFn) > 0);
  }
  const unsigned t0 = s_ptTraces;
  const double ms0 = s_ptMs;
  SplashBudget bud = { 0, AIM_SPLASH_TRACE_MAX, 0 };
  splash_points(player, target, o, &bud);
  o->traces = (int)(s_ptTraces - t0);   // reported on the tbot line; the point-trace stats stay the probes' own
  o->ms = s_ptMs - ms0;
  s_ptTraces = t0;
  s_ptMs = ms0;
  return 1;
}

// --- The lob instrument (aim.md section 13) ---------------------------------------------------------------------------
int aim_lob_predict(const void* target, float impact[3], float* pitchDeg, float* t, float eyeOut[3]) {
  const AimProjModel m = w_model();
  void* player = world_player();
  float eye[3], fwd[3], pt[3], vel[3] = { 0.f, 0.f, 0.f };
  int bone = 0;
  if (m.kind != AIM_PM_BALLISTIC || !player || !target || !live(target) || !view_eye_fwd(player, eye, fwd) ||
      !aim_point((void*)target, pt, &bone))
    return 0;
  if (g_cfg.aimLead && !spawnbox_helper_vel(target, vel)) rd_vec(target, N_VELOCITY, vel);
  float o[3];
  if (s_mt.on && s_mt.muzzleOk) memcpy(o, s_mt.muzzle, 12);
  else aim_lob_origin(eye, lob_held(), o);
  memcpy(eyeOut, eye, 12);
  const AimIntercept r = aim_intercept(m, o, pt, vel);
  if (!r.ok) return 0;
  memcpy(impact, r.hit, 12);
  *pitchDeg = (float)(asin(r.dir[2] > 1.f ? 1.f : r.dir[2] < -1.f ? -1.f : r.dir[2]) * 57.29577951308232);
  *t = r.t;
  return 1;
}

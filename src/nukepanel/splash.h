#pragma once
// Splash (docs/re/aim.md section 11): the triggerbot holds fire while an explosive shot's predicted detonation point is
// close enough for the blast to reach Duke. Pure rules for the suite; aim.cpp gathers the points, tbot.cpp holds fire.
#include <math.h>
#include <wchar.h>

// DamageRadius unread: a radius above every one read (the launcher's grenade, 300). Duke's collision when unread:
// Pawn's CollisionRadius 21 and DukePlayer's CollisionHeight 45.
const float SPLASH_DEFAULT_RADIUS = 400.f, SPLASH_MARGIN = 1.1f, SPLASH_DUKE_R = 21.f, SPLASH_DUKE_H = 45.f;

// The shot's splash: none for a hitscan gun, for a projectile that only damages what it hits (bDamageDirectly: the AT
// laser, the ice blast), or for one whose blast spares its Instigator (dnRocket_ShrinkBlast). damageDirectly < 0 unread.
// A radius read as 0 reaches nobody (HurtRadius's VisibleCollidingActors); unread or NaN takes the default.
enum { SPL_NONE = 0, SPL_READ = 1, SPL_DEFAULT = 2 };
inline int splash_kind(int projectile, int damageDirectly, int sparesInstigator, int radiusRead, float radius) {
  if (!projectile || damageDirectly > 0 || sparesInstigator) return SPL_NONE;
  if (!radiusRead || !(radius >= 0.f)) return SPL_DEFAULT;
  return radius > 0.f ? SPL_READ : SPL_NONE;
}
inline float splash_radius(int kind, float radius) {
  return kind == SPL_READ ? radius : kind == SPL_DEFAULT ? SPLASH_DEFAULT_RADIUS : 0.f;
}

// Distance from p to Duke's collision cylinder (0 inside): HurtRadius's VisibleCollidingActors takes an actor whose
// cylinder the sphere touches, so below his feet his height counts, not his radius. r, h unread (<= 0): the defaults.
inline float splash_cyl_dist(const float p[3], const float duke[3], float r, float h) {
  if (!(r > 0.f)) r = SPLASH_DUKE_R;
  if (!(h > 0.f)) h = SPLASH_DUKE_H;
  const float dx = p[0] - duke[0], dy = p[1] - duke[1];
  float hz = sqrtf(dx * dx + dy * dy) - r, vz = fabsf(p[2] - duke[2]) - h;
  if (hz < 0.f) hz = 0.f;
  if (vz < 0.f) vz = 0.f;
  return sqrtf(hz * hz + vz * vz);
}

// The candidate detonation points: (a) the target's intercept, (b) the first blocker on the straight path to it, (c) the
// first hit along a falling projectile's arc to it; (b') and (c') the same along the gun's current direction. The one
// nearest Duke is judged: held while the blast (DamageRadius plus 10 %) reaches his cylinder. src -1: no point.
enum { SPL_PT_TARGET, SPL_PT_BLOCKER, SPL_PT_ARC, SPL_PT_BLOCKER_GUN, SPL_PT_ARC_GUN, SPL_PT_COUNT };
// fail: an input the verdict needs was not had (Duke's Location, the origin, the aim point, a trace refused, or the
// frame's trace budget spent first). Fails closed: an explosive shot is held.
enum { SPL_FAIL_NONE, SPL_FAIL_DUKE, SPL_FAIL_ORIGIN, SPL_FAIL_POINT, SPL_FAIL_TRACE, SPL_FAIL_CAP, SPL_FAIL_COUNT };
struct SplashPts { int ok[SPL_PT_COUNT]; float p[SPL_PT_COUNT][3]; int fail; };
struct SplashVerdict { int hold, src; float dist, reach; int fail; };
inline SplashVerdict splash_judge(int kind, float radius, const float duke[3], float dukeR, float dukeH,
                                  const SplashPts& pts) {
  SplashVerdict v = { 0, -1, 0.f, 0.f, 0 };
  if (kind == SPL_NONE) return v;
  v.reach = splash_radius(kind, radius) * SPLASH_MARGIN;
  if (pts.fail != SPL_FAIL_NONE) {
    v.fail = pts.fail;
    v.hold = 1;
    return v;
  }
  for (int i = 0; i < SPL_PT_COUNT; i++) {
    if (!pts.ok[i]) continue;
    const float d = splash_cyl_dist(pts.p[i], duke, dukeR, dukeH);
    if (v.src < 0 || d < v.dist) {
      v.src = i;
      v.dist = d;
    }
  }
  v.hold = v.src >= 0 && v.dist < v.reach;   // NaN compares false
  return v;
}
// Whether it flies through a hit actor (its TraceFireHitResponse, -1 unread). Only IMMEDIATE (1) actors pass: a trigger
// only gets Touch, volumes/water explode nothing. Glass breaks and it flies on only with bBreakGlass (the SuperTurret
// rocket) and a pane not bUnBreakable; other rockets explode on it. Any other response is the detonation point.
inline int splash_passes(int response, int glass, int breakGlass, int unbreakable) {
  if (response != 1) return 0;
  return !glass || (breakGlass > 0 && unbreakable == 0);
}

// The frame's splash traces, AIM_SPLASH_TRACE_MAX at most: 1 one may run; 0 spent (capped set).
struct SplashBudget { int used, max, capped; };
inline int splash_budget_take(SplashBudget* b) {
  if (b->used >= b->max) {
    b->capped = 1;
    return 0;
  }
  b->used++;
  return 1;
}
inline const char* splash_src_label(int src) {
  return src == SPL_PT_TARGET ? "(a) the target's intercept" : src == SPL_PT_BLOCKER ? "(b) a blocker on the path" :
         src == SPL_PT_ARC ? "(c) the arc's first hit" : src == SPL_PT_BLOCKER_GUN ? "(b') a blocker on the gun's line" :
         src == SPL_PT_ARC_GUN ? "(c') the gun's arc's first hit" : "none";
}
inline const char* splash_fail_label(int f) {
  static const char* const k[SPL_FAIL_COUNT] = { "none", "Duke's Location unread", "the shot's origin unread",
                                                 "the aim point unread", "a trace refused",
                                                 "the frame's trace budget spent" };
  return f >= 0 && f < SPL_FAIL_COUNT ? k[f] : "?";
}

// By the projectile class and its supers: dnRocket_ShrinkBlast.CanHurtRadiusOther refuses its Instigator;
// dnRocket_Devastator.PostVerifySelf doubles DamageRadius when its Instigator is mostly shrunk.
inline int splash_chain_has(const wchar_t* const* names, int n, const wchar_t* want) {
  for (int i = 0; names && i < n; i++)
    if (names[i] && _wcsicmp(names[i], want) == 0) return 1;
  return 0;
}
// CanHurtRadiusOther refuses the Instigator: dnRocket_ShrinkBlast, and dnRocket_ExpandBlast (the Expander, DLC03:
// itself and every non-pawn).
inline int splash_spares_instigator(const wchar_t* const* names, int n) {
  return splash_chain_has(names, n, L"dnRocket_ShrinkBlast") || splash_chain_has(names, n, L"dnRocket_ExpandBlast");
}
inline float splash_shrunk_radius(float radius, int doubles, int shrunk) { return doubles && shrunk ? radius * 2.f : radius; }

// --- The glue (tbot.cpp), game thread only ---------------------------------------------------------------------
// Gathered by aim.cpp for the held or mounted gun: its splash kind and radius, name, Duke's Location and collision, and
// the candidate points for target. traces, ms: shot-flag traces spent and time; blockerCls: per point, the class hit.
enum { AIM_SPLASH_TRACE_MAX = 12 };
struct AimSplash { int kind; float radius; int gunName; float duke[3], dukeR, dukeH; SplashPts pts;
                   int blockerCls[SPL_PT_COUNT], traces; double ms; };
int aim_splash(const void* target, AimSplash* out);

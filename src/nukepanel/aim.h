#pragma once
// Silent aim (aim.md sections 1-3, 5): every single-player shot aims along PlayerPawn.GetWeaponAim = ViewRotation +
// CurrentShake x (CurrentFOV / DefaultFOV). A bracket in our dnGame ProcessEvent slot writes CurrentShake toward the
// target at UpdateWeaponTraceVars and puts it back at RecordShot, so the view never moves.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum { AIM_AT_BODY = 0, AIM_AT_HEAD = 1 };
// DNMP's default (its aim.h and config.cpp): Aim range the maximum. The maximum is 160 m, not DNMP's 64, so the
// railgun's 8192-unit trace is covered (8192 / 52.5 = 156). No Lead amount: the intercept is
// solved exactly.
enum { AIM_RANGE_MIN_M = 1, AIM_RANGE_MAX_M = 160 };
inline const char* aim_point_label(int v) { return v ? "Head" : "Body"; }

// --- The held weapon (aim.md section 1, "Per weapon") ---------------------------------------------------------------------------
// Bracketed: guns whose shots go through PerformFire's GetWeaponAim; thrown, melee, item "weapons" are not. BEAM:
// freeze ray (traced in FreezeRay.Tick; classic ice blast uses PerformFire). MELEE: fists, picked within reach.
enum { AIMW_NONE, AIMW_HITSCAN, AIMW_PROJECTILE, AIMW_BEAM, AIMW_MELEE };
inline int aim_weapon_kind(const wchar_t* cfgName) {
  struct K { const wchar_t* name; int kind; };
  static const K kKinds[] = {
    { L"PistolWeaponConfig", AIMW_HITSCAN }, { L"ShotgunWeaponConfig", AIMW_HITSCAN },
    { L"MachineGunWeaponConfig", AIMW_HITSCAN }, { L"RailgunWeaponConfig", AIMW_HITSCAN },
    { L"MiniGun_BLWeaponConfig", AIMW_HITSCAN }, { L"RPGWeaponConfig", AIMW_PROJECTILE },
    { L"DevastatorWeaponConfig", AIMW_PROJECTILE }, { L"ShrinkRayWeaponConfig", AIMW_PROJECTILE },
    { L"EnforcerGunWeaponConfig", AIMW_PROJECTILE }, { L"ATLaserWeaponConfig", AIMW_PROJECTILE },
    { L"FreezeRayWeaponConfig", AIMW_BEAM }, { L"MightyFootWeaponConfig", AIMW_MELEE },
    // Straight rockets the player can carry, pinned: dnRocket_ATCaptainLaser,
    // dnRocket_Nuke (whose dnNukeExplosion does the damage).
    { L"ATCaptainLaserWeaponConfig", AIMW_PROJECTILE }, { L"NukeWeaponConfig", AIMW_PROJECTILE },
  };
  if (!cfgName) return AIMW_NONE;
  for (int i = 0; i < (int)(sizeof kKinds / sizeof kKinds[0]); i++)
    if (_wcsicmp(cfgName, kKinds[i].name) == 0) return kKinds[i].kind;
  return AIMW_NONE;
}
// The config class and its supers, most derived first: the first one the table knows (DualPistol -> Pistol).
inline int aim_weapon_kind_chain(const wchar_t* const* names, int n) {
  for (int i = 0; names && i < n; i++) {
    const int k = aim_weapon_kind(names[i]);
    if (k != AIMW_NONE) return k;
  }
  return AIMW_NONE;
}

// A config the table does not name (the DLC's; aim.md 13), by PerformFire's handling: a ProjectileClass with default
// Physics PHYS_Projectile (6) flies straight; other physics is thrown, not redirected. No projectile but
// MaxTraceDistance and TraceDamageType: hitscan. Else nothing is fired. byName: the table's answer when it has one.
enum { AIMW_BY_NAME, AIMW_BY_PROJECTILE, AIMW_BY_THROWN, AIMW_BY_TRACE, AIMW_BY_NOTHING, AIMW_BY_LOB };
enum { AIM_PHYS_PROJECTILE = 6 };
struct AimKindBy { int kind, by; };
// lobs: the ProjectileClass default sets bExplodeOnAnyImpact. A PHYS_Meqon (18) projectile that explodes on its first
// impact is a lob the arc solver aims (the Impregnader's spit, aim_lob_model); without it (the pipe bomb) it stays thrown.
enum { AIM_PHYS_MEQON = 18 };
inline AimKindBy aim_weapon_classify(int byName, int hasProj, int projPhysics, float maxTrace, int hasTraceType,
                                     int lobs = 0) {
  AimKindBy r = { byName, AIMW_BY_NAME };
  if (byName != AIMW_NONE) return r;
  if (hasProj) {
    const int straight = projPhysics == AIM_PHYS_PROJECTILE, lob = !straight && lobs && projPhysics == AIM_PHYS_MEQON;
    r.kind = straight || lob ? AIMW_PROJECTILE : AIMW_NONE;
    r.by = straight ? AIMW_BY_PROJECTILE : lob ? AIMW_BY_LOB : AIMW_BY_THROWN;
  } else if (maxTrace > 0.f && hasTraceType) {   // NaN compares false
    r.kind = AIMW_HITSCAN;
    r.by = AIMW_BY_TRACE;
  } else {
    r.by = AIMW_BY_NOTHING;
  }
  return r;
}
inline const char* aim_kind_by_label(int by) {
  static const char* const k[6] = { "by its config's name", "by its ProjectileClass (PHYS_Projectile)",
                                    "its ProjectileClass is thrown or physics-driven", "by MaxTraceDistance and TraceDamageType",
                                    "it fires no projectile and no trace",
                                    "by its ProjectileClass (PHYS_Meqon, explodes on any impact: a lob)" };
  return by >= 0 && by < 6 ? k[by] : "?";
}

// A gun: the kinds that spend ammo (Weapon.IsCompletelyOutOfAmmo reads Ammo, which the fists may not have).
inline int aim_gun_kind(int kind) { return kind == AIMW_HITSCAN || kind == AIMW_PROJECTILE || kind == AIMW_BEAM; }

// Weapon.PerformMelee's reach (aim.md "The fists"): a trace of MeleeTraceDistance (MeleeTraceDistanceShrunk while
// shrunk) from the eye, retried on a miss with the box ShrinkScale x MeleeHitExtent (extent: its largest component).
// A surface distance; 0 when unread, so nothing is in reach.
inline float aim_melee_reach(float trace, float traceShrunk, float extent, int shrunk, float shrinkScale) {
  const float t = shrunk ? traceShrunk : trace;
  if (!(t > 0.f)) return 0.f;   // NaN too
  const float k = shrinkScale > 0.f && shrinkScale <= 1.f ? shrinkScale : 1.f;
  return t + (extent > 0.f ? extent * k : 0.f);
}
// A pick by the distance to the target's centre: within the reach of its surface (its collision radius).
inline int aim_melee_centre_ok(float dist, float reach, float radius) {
  return reach > 0.f && dist <= reach + (radius > 0.f ? radius : 0.f);   // NaN compares false
}

// The range in units: the slider's metres, and for a hitscan gun never past its MaxTraceDistance (the trace ends
// there), nor a beam past FreezeRay.BeamMaxLength (maxTrace carries it; aim.md section 3). maxTrace <= 0 is unread
// and caps nothing.
inline float aim_range_uu(int rangeM, float uuPerM, int kind, float maxTrace) {
  const float r = (float)rangeM * uuPerM;
  return (kind == AIMW_HITSCAN || kind == AIMW_BEAM) && maxTrace > 0.f && maxTrace < r ? maxTrace : r;
}

// Mounted on a turret the slider is ignored (2026-10-10): the range is the gun's own reach, its ShotDistance for a
// hitscan gun; a projectile gun or an unread reach gets a large cap.
enum { AIM_RANGE_MOUNTED_CAP_UU = 65536 };
inline float aim_range_for(int mounted, int rangeM, float uuPerM, int kind, float maxTrace) {
  if (!mounted) return aim_range_uu(rangeM, uuPerM, kind, maxTrace);
  return kind == AIMW_HITSCAN && maxTrace > 0.f ? maxTrace : (float)AIM_RANGE_MOUNTED_CAP_UU;
}

// --- Projectile models and the intercept (aim.md section 8) -----------------------------------------------------------------------
// STRAIGHT: physProjectile adds Acceleration to Velocity per tick, clamped to [Speed, MaxSpeed], no gravity. BALLISTIC:
// physFalling with zone gravity: v + g dt / 2 per step with bBounce (UE1), else v + g dt, capped by TerminalVelocity.
enum { AIM_PM_NONE, AIM_PM_STRAIGHT, AIM_PM_BALLISTIC };
// gravity: downward, units/s^2 (PhysicsVolume.Gravity.Z negated); terminal 0: no clamp; maxT <= 0: AIM_PM_MAXT_DEF;
// bounce: Actor.bBounce (physFalling skips the velocity refinement and the clamp).
// boostZ: a vertical speed the game adds to the launch (dnGrenade.InitializeMotion's UpwardsBoost); 0 for the rest.
struct AimProjModel { int kind; float speed, maxSpeed, accel, gravity, terminal, maxT; int bounce; float boostZ; };
// The launch velocity along the aim direction dir: Speed x dir plus the boost.
inline void aim_launch_vel(const AimProjModel& m, const float dir[3], float out[3]) {
  for (int i = 0; i < 3; i++) out[i] = dir[i] * m.speed;
  out[2] += m.boostZ;
}
// Impregnator spit: dnGrenade.DoThrow sets PHYS_Meqon and KSetLinearVelocity; Meqon uses the zone's Gravity up to
// TerminalVelocity. Gravity x GravityScale is the model (unverified); the fuse (ExplosionDelay - Variance) bounds the
// flight. Unread: GravityScale 0.5, Gravity -950, TerminalVelocity 2500, delay 4 - 2.
inline AimProjModel aim_lob_model(float speed, float boostZ, float gravityScale, float gravityZ, float terminal,
                                  float delay, float variance, float ratio) {
  const float r = ratio > 0.f ? ratio : 1.f, k = r * powf(r, 0.25f);
  const float gs = gravityScale >= 0.f ? gravityScale : 0.5f, gz = gravityZ < 0.f ? -gravityZ : 950.f;
  const float d = delay > 0.f ? delay : 4.f, v = variance >= 0.f && delay > 0.f ? variance : 2.f;
  AimProjModel m = { AIM_PM_BALLISTIC, speed * k, 0.f, 0.f, gs * gz, terminal > 0.f ? terminal : 2500.f,
                     d - v > 0.1f ? d - v : 0.1f, 0, boostZ * k };
  return m;
}
const float AIM_PM_MAXT_DEF = 10.f;
// physFalling's step is the frame (at most 0.05 s); 60 Hz taken. Only bBounce's drift and the clamp depend on it.
const float AIM_PM_STEP = 1.f / 60.f;
inline float aim_pm_maxt(const AimProjModel& m) { return m.maxT > 0.f ? m.maxT : AIM_PM_MAXT_DEF; }
// The gravity the flight feels on average: half with bBounce.
inline double aim_pm_geff(const AimProjModel& m) { return m.bounce ? 0.5 * m.gravity : (double)m.gravity; }

// Distance a straight projectile has flown at t: speed min(Speed + accel t, MaxSpeed) (MaxSpeed <= 0: no cap).
inline float aim_straight_dist(const AimProjModel& m, float t) {
  if (!(t > 0.f)) return 0.f;
  const float cap = m.maxSpeed > 0.f ? m.maxSpeed : 3.0e38f;
  const float s0 = m.speed < cap ? m.speed : cap;
  if (!(m.accel > 0.f) || s0 >= cap) return s0 * t;
  const float tc = (cap - s0) / m.accel;   // MaxSpeed reached
  if (t <= tc) return s0 * t + 0.5f * m.accel * t * t;
  return s0 * tc + 0.5f * m.accel * tc * tc + cap * (t - tc);
}

// One physFalling step of dt on velocity w (z up): the move's velocity in mv; w becomes the next velocity.
inline void aim_fall_step(const AimProjModel& m, double w[3], double dt, double mv[3]) {
  mv[0] = w[0];
  mv[1] = w[1];
  mv[2] = w[2] - 0.5 * m.gravity * dt;
  if (m.bounce) {
    w[2] = mv[2];
    return;
  }
  w[2] -= m.gravity * dt;
  const double sp = sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
  if (m.terminal > 0.f && sp > m.terminal)
    for (int i = 0; i < 3; i++) w[i] *= m.terminal / sp;
}

// A ballistic shot from the origin, in the plane of its horizontal direction: launched at angle th (radians above the
// horizon) with the model's speed, stepped as physFalling does until it has flown D horizontally. 1 with the height
// there (*z, on the step's straight move, as MoveActor goes) and the time (*T); 0 when it never gets there within maxT.
inline int aim_ballistic_reach(const AimProjModel& m, double th, double D, double* z, double* T) {
  double w[3] = { m.speed * cos(th), 0.0, m.speed * sin(th) + m.boostZ }, mv[3], x = 0.0, h = 0.0, t = 0.0;
  const double dt = AIM_PM_STEP, tmax = aim_pm_maxt(m);
  if (D <= 0.0) { *z = 0.0; *T = 0.0; return 1; }
  while (t < tmax) {
    if (!(w[0] > 1e-6)) return 0;
    aim_fall_step(m, w, dt, mv);
    if (x + mv[0] * dt >= D) {
      const double f = (D - x) / (mv[0] * dt);
      *z = h + mv[2] * f * dt;
      *T = t + f * dt;
      return 1;
    }
    x += mv[0] * dt;
    h += mv[2] * dt;
    t += dt;
  }
  return 0;
}
// The position at t of a ballistic projectile now at p with velocity v (overkill's path), stepped as above.
inline void aim_ballistic_pos(const AimProjModel& m, const float p[3], const float v[3], float t, float out[3]) {
  double q[3] = { p[0], p[1], p[2] }, w[3] = { v[0], v[1], v[2] }, mv[3];
  const double dt = AIM_PM_STEP;
  for (double s = 0.0; s < t; s += dt) {
    const double h = t - s < dt ? t - s : dt;
    aim_fall_step(m, w, h, mv);
    for (int i = 0; i < 3; i++) q[i] += mv[i] * h;
  }
  for (int i = 0; i < 3; i++) out[i] = (float)q[i];
}
// The low-arc launch angle hitting (D, H) from the origin: the closed form's low root without the terminal clamp
// (tan = (s^2 - sqrt(s^4 - g(g D^2 + 2 H s^2))) / (g D)) starts a bracket below the arc's peak angle (tan = s^2 / (g D)),
// refined by bisection on the stepped flight. 0 out of reach.
inline int aim_ballistic_angle(const AimProjModel& m, double D, double H, double* th, double* T) {
  const double s = m.speed, g = aim_pm_geff(m), s2 = s * s;
  if (!(s > 0.0) || !(g > 0.0)) return 0;
  if (D < 1.0) {   // straight above or below: no arc to choose
    *th = H >= 0.0 ? 1.5707963 : -1.5707963;
    *T = fabs(H) / s;
    return H <= 0.0 || H <= s2 / (2.0 * g);
  }
  const double disc = s2 * s2 - g * (g * D * D + 2.0 * H * s2);
  const double b = m.boostZ;
  if (disc < 0.0 && !(b > 0.0)) return 0;
  // The angle that lifts the arc highest at D: tan = s^2 / (g D); with a boost b the root of
  // cos + (b / s) sin cos - g D sin / s^2 (bisected; the same root at b = 0).
  double peak = atan(s2 / (g * D));
  if (b != 0.0) {
    double a0 = 0.0, a1 = 1.5707963;
    for (int i = 0; i < 40; i++) {
      const double a = 0.5 * (a0 + a1);
      if (cos(a) + b / s * sin(a) * cos(a) - g * D * sin(a) / s2 > 0.0) a0 = a;
      else a1 = a;
    }
    peak = 0.5 * (a0 + a1);
  }
  const double lowest = -1.55;
  double lo = lowest, hi = peak, z = 0.0, t = 0.0;
  // The closed form's low root (exact without a boost) starts the bracket; a boost only moves the answer.
  double guess = disc >= 0.0 ? atan((s2 - sqrt(disc)) / (g * D)) : peak;
  // The clamp or maxT: out of reach. A short fuse (the Impregnader's 2 s) may not reach D at the peak's slow horizontal
  // speed: the highest lower angle that gets there above H is the bracket's top instead.
  if (!aim_ballistic_reach(m, hi, D, &z, &t) || z < H) {
    double a = hi - 0.03;
    for (; a > lowest; a -= 0.03)
      if (aim_ballistic_reach(m, a, D, &z, &t) && z >= H) break;
    if (!(a > lowest)) return 0;
    hi = a;
  }
  if (guess > hi) guess = hi;
  const double step = 0.03;   // shrink the bracket round the closed form's answer
  if (aim_ballistic_reach(m, guess, D, &z, &t) && z >= H) {
    hi = guess;
    for (double a = guess - step; a > lowest; a -= step) {
      if (!aim_ballistic_reach(m, a, D, &z, &t) || z < H) { lo = a; break; }
      hi = a;
    }
  } else {
    lo = guess;
    for (double a = guess + step; a < hi; a += step) {
      if (aim_ballistic_reach(m, a, D, &z, &t) && z >= H) { hi = a; break; }
      lo = a;
    }
  }
  for (int i = 0; i < 30 && hi - lo > 1e-7; i++) {
    const double mid = 0.5 * (lo + hi);
    if (aim_ballistic_reach(m, mid, D, &z, &t) && z >= H) hi = mid;
    else lo = mid;
  }
  if (!aim_ballistic_reach(m, hi, D, &z, &t)) return 0;
  *th = hi;
  *T = t;
  return 1;
}

// The intercept: the launch direction from o that meets the target at p moving at v (constant), the time of flight and
// the target's point then. STRAIGHT: the first t where the distance flown equals |p + v t - o| (a scan of 64 steps over
// maxT, then bisection); BALLISTIC: the low arc to p + v t, iterated on t. NONE (hitscan): the direction to p, t 0.
struct AimIntercept { int ok; float dir[3]; float t; float hit[3]; };
inline AimIntercept aim_intercept(const AimProjModel& m, const float o[3], const float p[3], const float v[3]) {
  AimIntercept r = { 0, { 1.f, 0.f, 0.f }, 0.f, { p[0], p[1], p[2] } };
  const float tmax = aim_pm_maxt(m);
  double q[3];
  if (m.kind == AIM_PM_STRAIGHT && m.speed > 0.f) {
    double prev = 0.0, t = -1.0;
    const double d0 = sqrt(((double)p[0] - o[0]) * (p[0] - o[0]) + ((double)p[1] - o[1]) * (p[1] - o[1]) +
                           ((double)p[2] - o[2]) * (p[2] - o[2]));
    if (d0 < 1e-3) t = 0.0;   // at the muzzle
    for (int k = 1; k <= 64 && t < 0.0; k++) {
      const double tk = tmax * k / 64.0;
      double d2 = 0.0;
      for (int i = 0; i < 3; i++) { const double e = p[i] + v[i] * tk - o[i]; d2 += e * e; }
      const double f = aim_straight_dist(m, (float)tk) - sqrt(d2);
      if (f >= 0.0) {   // the first crossing: bisect it
        double a = prev, b = tk;
        for (int i = 0; i < 40; i++) {
          const double c = 0.5 * (a + b);
          double e2 = 0.0;
          for (int j = 0; j < 3; j++) { const double e = p[j] + v[j] * c - o[j]; e2 += e * e; }
          if (aim_straight_dist(m, (float)c) - sqrt(e2) >= 0.0) b = c;
          else a = c;
        }
        t = b;
      }
      prev = tk;
    }
    if (t < 0.0) return r;   // never caught within maxT
    r.t = (float)t;
  } else if (m.kind == AIM_PM_BALLISTIC) {
    // Fixed point on t: aim at where the target is at t, take that shot's time. The last step's change in the target's
    // point must be a few units at most, or the target outruns the iteration: no solution.
    const double vs = sqrt((double)v[0] * v[0] + (double)v[1] * v[1] + (double)v[2] * v[2]);
    double t = 0.0, th = 0.0, T = 0.0, moved = 0.0, D = 0.0;
    for (int it = 0; it < 16; it++) {
      for (int i = 0; i < 3; i++) q[i] = p[i] + v[i] * t - o[i];
      D = sqrt(q[0] * q[0] + q[1] * q[1]);
      if (!aim_ballistic_angle(m, D, q[2], &th, &T)) return r;
      moved = fabs(T - t) * vs;
      t = T;
      if (moved < 0.05) break;
    }
    if (moved > 4.0) return r;
    const double hx = D > 1e-6 ? q[0] / D : 1.0, hy = D > 1e-6 ? q[1] / D : 0.0;
    r.dir[0] = (float)(cos(th) * hx);
    r.dir[1] = (float)(cos(th) * hy);
    r.dir[2] = (float)sin(th);
    r.t = (float)t;
    for (int i = 0; i < 3; i++) r.hit[i] = p[i] + v[i] * r.t;
    r.ok = 1;
    return r;
  }
  for (int i = 0; i < 3; i++) { r.hit[i] = p[i] + v[i] * r.t; q[i] = r.hit[i] - o[i]; }
  const double l = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
  if (l > 1e-6)
    for (int i = 0; i < 3; i++) r.dir[i] = (float)(q[i] / l);
  r.ok = 1;
  return r;
}

// The Impregnader's spit leaves about 12 units below the eye: five logged flights back-solved the spawn
//  at 8-13 units below (2026-10-11; Weapon.ProjectileFire's muzzle and PregnatorGun's 5-unit nudge). Only
// the lob (AIMW_BY_LOB) is lowered; the shot line logs the eye and the first-seen offset to keep measuring it.
const float AIM_LOB_SPAWN_DZ = -12.f;
inline void aim_lob_origin(const float eye[3], int lob, float out[3]) {
  for (int i = 0; i < 3; i++) out[i] = eye[i];
  if (lob) out[2] += AIM_LOB_SPAWN_DZ;
}
// Actor.Velocity reads 0 for a Meqon body: the velocity at a's time from two Locations dt apart (game time), the
// vertical put back by the half step of gravity (g downward, u/s^2) the average carries. 0 when dt is not positive.
inline int aim_fd_velocity(const float a[3], const float b[3], float dt, float g, float out[3]) {
  if (!(dt > 0.f)) return 0;
  for (int i = 0; i < 3; i++) out[i] = (b[i] - a[i]) / dt;
  out[2] += 0.5f * g * dt;
  return 1;
}
// A target the arc has no solution for is not solved again for AIM_NOSOLVE_MS (the low-angle scan steps the whole
// flight per angle). In place, else a free slot, else the oldest.
enum { AIM_NOSOLVE_SLOTS = 16 };
const unsigned AIM_NOSOLVE_MS = 100;
struct AimNoSolve { const void* obj[AIM_NOSOLVE_SLOTS]; unsigned ms[AIM_NOSOLVE_SLOTS]; int n; };
inline int aim_nosolve_recent(const AimNoSolve* c, const void* o, unsigned now) {
  for (int i = 0; o && i < c->n; i++)
    if (c->obj[i] == o) return now - c->ms[i] < AIM_NOSOLVE_MS;   // unsigned: safe across the wrap
  return 0;
}
inline void aim_nosolve_put(AimNoSolve* c, const void* o, unsigned now) {
  if (!o) return;
  int at = -1;
  for (int i = 0; i < c->n && at < 0; i++)
    if (c->obj[i] == o) at = i;
  if (at < 0 && c->n < AIM_NOSOLVE_SLOTS) at = c->n++;
  if (at < 0) {
    at = 0;
    for (int i = 1; i < c->n; i++)
      if (now - c->ms[i] > now - c->ms[at]) at = i;
  }
  c->obj[at] = o;
  c->ms[at] = now;
}

// The rotator the game adds to the spawn (AdjustProjectileSpawn): EnforcerGun (200, -200), ATLaser and ATCaptainLaser
// (the same, with a random +-200 the panel cannot know). Subtracted from the solved rotator so the projectile leaves
// along it.
inline void aim_spawn_offset(const wchar_t* cfgName, int* pitch, int* yaw) {
  *pitch = *yaw = 0;
  if (cfgName && (_wcsicmp(cfgName, L"EnforcerGunWeaponConfig") == 0 || _wcsicmp(cfgName, L"ATLaserWeaponConfig") == 0 ||
                  _wcsicmp(cfgName, L"ATCaptainLaserWeaponConfig") == 0)) {
    *pitch = 200;
    *yaw = -200;
  }
}

// --- Choosing the target (aim.md sections 3 and 5) -------------------------------------------------------------
// Hostile by world_classify (an ally or a neutral never), within range, and on screen unless Off-screen targets is on.
inline int aim_eligible(int enemy, int ally, float dist, float range, int onScreen, int offscreenOn) {
  return enemy && !ally && dist <= range && (offscreenOn || onScreen);   // NaN compares false
}
inline int aim_on_screen(int projected, float sx, float sy, int W, int H) {
  return projected && sx >= 0.f && sx <= (float)W && sy >= 0.f && sy <= (float)H;
}
// The view's forward vector from a rotator (view.h's depth column: X forward, Z up, 65536 a turn).
inline void aim_forward(int pitch, int yaw, float out[3]) {
  const double k = 6.283185307179586 / 65536.0;
  const double cp = cos(pitch * k);
  out[0] = (float)(cp * cos(yaw * k));
  out[1] = (float)(cp * sin(yaw * k));
  out[2] = (float)sin(pitch * k);
}
// Degrees between the view's forward and (dx, dy, dz); a zero vector sorts last (180).
inline float aim_angle(const float fwd[3], float dx, float dy, float dz) {
  const double l = sqrt((double)dx * dx + (double)dy * dy + (double)dz * dz);
  if (!(l > 0.0)) return 180.f;
  double c = (fwd[0] * dx + fwd[1] * dy + fwd[2] * dz) / l;
  if (c > 1.0) c = 1.0;
  if (c < -1.0) c = -1.0;
  return (float)(acos(c) * 180.0 / 3.141592653589793);
}

// A frozen enemy belongs to Auto-execute while it is on: a shot may end the frozen execution (aim.md section 5).
inline int aim_skip_frozen(int frozen, int autoExecute) { return frozen && autoExecute; }

// Whether the segment a -> b crosses a pawn's collision cylinder (centre, radius, half-height), approximated by its
// box: an ally or a neutral in the line of fire would take the redirected shot. The slab test.
inline int aim_seg_cyl_hit(const float a[3], const float b[3], float cx, float cy, float cz, float r, float h) {
  if (!(r > 0.f) || !(h > 0.f)) return 0;
  const float lo[3] = { cx - r, cy - r, cz - h }, hi[3] = { cx + r, cy + r, cz + h };
  float t0 = 0.f, t1 = 1.f;
  for (int i = 0; i < 3; i++) {
    const float d = b[i] - a[i];
    if (d == 0.f) {
      if (a[i] < lo[i] || a[i] > hi[i]) return 0;
      continue;
    }
    float ta = (lo[i] - a[i]) / d, tb = (hi[i] - a[i]) / d;
    if (ta > tb) { const float x = ta; ta = tb; tb = x; }
    if (ta > t0) t0 = ta;
    if (tb < t1) t1 = tb;
    if (t0 > t1) return 0;
  }
  return 1;
}

// Visibility, by a trace with the bullet's own flags from the player's eye to the body points (aim.md section 3), at
// most AIM_TRACES_PER_FRAME probes a frame, each kept AIM_VIS_MS. A probe's point trace can re-trace up to AIM_PASS_MAX times (bullet_trace).
enum { AIM_VIS_UNKNOWN, AIM_VIS_YES, AIM_VIS_NO };
enum { AIM_VIS_SLOTS = 64, AIM_TRACES_PER_FRAME = 4, AIM_CAND_MAX = 64 };
const unsigned AIM_VIS_MS = 100;
// pt: the body point found visible (AIM_PT_*), kept with the entry so every user aims at it.
struct AimVisEntry { const void* obj; unsigned stamp; int vis, frozen, pt; };
struct AimVisCache { AimVisEntry e[AIM_VIS_SLOTS]; int n; };
inline int aim_vis_get(const AimVisCache* c, const void* obj, unsigned now, int* frozen, int* pt = 0) {
  if (!obj) return AIM_VIS_UNKNOWN;
  for (int i = 0; i < c->n; i++) {
    if (c->e[i].obj != obj) continue;
    if (now - c->e[i].stamp >= AIM_VIS_MS) return AIM_VIS_UNKNOWN;   // unsigned: safe across the wrap
    if (frozen) *frozen = c->e[i].frozen;
    if (pt) *pt = c->e[i].pt;
    return c->e[i].vis;
  }
  return AIM_VIS_UNKNOWN;
}
// In place for a known object; else a free slot; else the oldest.
inline void aim_vis_put(AimVisCache* c, const void* obj, unsigned now, int vis, int frozen, int pt = 0) {
  if (!obj) return;
  int slot = -1;
  for (int i = 0; i < c->n && slot < 0; i++)
    if (c->e[i].obj == obj) slot = i;
  if (slot < 0 && c->n < AIM_VIS_SLOTS) slot = c->n++;
  if (slot < 0) {
    slot = 0;
    for (int i = 1; i < c->n; i++)
      if (now - c->e[i].stamp > now - c->e[slot].stamp) slot = i;
  }
  const AimVisEntry e = { obj, now, vis, frozen, pt };
  c->e[slot] = e;
}
// Forget obj's entry: its aim point source changed (spawnbox flagged it far, or it came back near), so the cached
// verdict and point no longer describe where the aim goes.
inline void aim_vis_drop(AimVisCache* c, const void* obj) {
  for (int i = 0; obj && i < c->n; i++)
    if (c->e[i].obj == obj) {
      c->e[i] = c->e[--c->n];
      return;
    }
}

// The body points, in priority order (target whatever is visible): Aim at's choice first, then
// head, chest (Location), hips (Location - 0.5 CollisionHeight), knees (Location - 0.8 CollisionHeight); the head
// only when the class has one. At most AIM_PTS_MAX traces a candidate. 0 is AIM_PT_NONE: the Aim at default.
enum { AIM_PT_NONE, AIM_PT_HEAD, AIM_PT_CHEST, AIM_PT_HIPS, AIM_PT_KNEES, AIM_PT_COUNT };
enum { AIM_PTS_MAX = 4 };
inline const char* aim_pt_label(int pt) {
  static const char* const k[AIM_PT_COUNT] = { "default", "head", "chest", "hips", "knees" };
  return pt >= 0 && pt < AIM_PT_COUNT ? k[pt] : "?";
}
inline int aim_point_order(int aimAt, int haveHead, int out[AIM_PTS_MAX]) {
  int n = 0;
  if (aimAt == AIM_AT_HEAD && haveHead) out[n++] = AIM_PT_HEAD;
  out[n++] = AIM_PT_CHEST;
  if (aimAt != AIM_AT_HEAD && haveHead) out[n++] = AIM_PT_HEAD;
  out[n++] = AIM_PT_HIPS;
  if (n < AIM_PTS_MAX) out[n++] = AIM_PT_KNEES;
  return n;
}
// Chest, hips and knees on the cylinder's axis; the head comes from its bone (aim.cpp).
inline void aim_body_point(int pt, const float loc[3], float height, float out[3]) {
  const float down = pt == AIM_PT_HIPS ? 0.5f : pt == AIM_PT_KNEES ? 0.8f : 0.f;
  out[0] = loc[0];
  out[1] = loc[1];
  out[2] = loc[2] - down * height;
}
// A pawn's aim point: its render-box centre while its body is animated away from Location (spawnbox_body, the far
// case; the aircraft pass aims the same way), else the body point (head, chest, ...). AIM_SRC_NONE: neither.
enum { AIM_SRC_NONE = 0, AIM_SRC_BODY = 1, AIM_SRC_BOX = 2 };
inline int aim_pawn_point(int hasBox, const float boxC[3], int haveBody, const float body[3], float out[3]) {
  const float* p = hasBox ? boxC : haveBody ? body : 0;
  if (!p) return AIM_SRC_NONE;
  memcpy(out, p, 12);
  return hasBox ? AIM_SRC_BOX : AIM_SRC_BODY;
}
// A point trace (Actor.TraceEx, blocking actors and the world) is clear when it hit nothing, the target, or an actor
// hanging from the target (its weapon, its jetpack: Owner/MountParent).
inline int aim_trace_clear(int hit, int hitIsTarget, int hitOnTarget) {
  return !hit || hitIsTarget || hitOnTarget;
}
// The point trace's STraceFlags, packed in Engine.u's declaration order (bTraceActors 0, bMeshAccurate 1, bShotTrace 2,
// bUseTrace 3, bTraceTexture 4, ... bOnlyBlocking 11): the bullet's own (a PatchActor
// blocks the player but not bullets). AIM_TF_BLOCKING is the old player-blocking set, kept for the skipped count.
enum { AIM_TF_ACTORS = 0x1, AIM_TF_MESH = 0x2, AIM_TF_SHOT = 0x4, AIM_TF_TEXTURE = 0x10, AIM_TF_ONLY_BLOCKING = 0x800 };
inline unsigned aim_shot_trace_flags(void) { return AIM_TF_ACTORS | AIM_TF_MESH | AIM_TF_SHOT | AIM_TF_TEXTURE; }
const unsigned AIM_TF_BLOCKING = AIM_TF_ACTORS | AIM_TF_ONLY_BLOCKING;
// A clear shot trace whose point the player-blocking trace called hidden: a blocker bullets pass, skipped.
inline int aim_blocker_skipped(int shotClear, int blockingClear) { return shotClear && !blockingClear; }

// The bullet's hit-list walk (AActor::TraceFire, Engine.dll!0x10282A30): each hit actor's TraceFireHitResponse
// (Actor+0x2FE; default 2; GeoWater, Triggers, PhysicsVolume, BreakableGlass 1). 1 passes free; others but Abort spend
// one of MaxTracePenetrationCount (cfg+0x4C) and stop beyond MaxPenetrationDistance (cfg+0x50) if > 0.
enum { AIM_THR_ABORT = 0, AIM_THR_IMMEDIATE = 1, AIM_THR_BACKTRACE = 2, AIM_PASS_MAX = 4 };
inline int aim_bullet_passes(int response, int* penLeft, float maxPenDist, float hitDist) {
  if (response < 0 || response == AIM_THR_ABORT) return 0;
  if (response == AIM_THR_IMMEDIATE) return 1;
  if (*penLeft <= 0 || (maxPenDist > 0.f && maxPenDist < hitDist)) return 0;   // NaN distance: within reach
  --*penLeft;
  return 1;
}
// The next trace's start, one unit past hit along a -> b; 0 when less than two units of the line are left.
inline int aim_step_past(const float a[3], const float b[3], const float hit[3], float out[3]) {
  const float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
  const float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  const float h[3] = { hit[0] - a[0], hit[1] - a[1], hit[2] - a[2] };
  const float at = len > 0.f ? (h[0] * d[0] + h[1] * d[1] + h[2] * d[2]) / len : 0.f;
  if (!(len - at >= 2.f)) return 0;   // NaN compares false
  for (int i = 0; i < 3; i++) out[i] = hit[i] + d[i] / len;
  return 1;
}

// What a point trace that reported hidden hit (nearly every scan said "none" on visible
// enemies), for the 600-frame line. First match: the world (no actor, or the LevelInfo BSP hit), the player or what
// hangs from it, the target's own attachment, another pawn, a door or mover, anything else.
enum { AIM_HIT_WORLD, AIM_HIT_SELF, AIM_HIT_TARGET_ATT, AIM_HIT_PAWN, AIM_HIT_DOOR, AIM_HIT_OTHER, AIM_HIT_COUNT };
inline int aim_hit_category(int world, int self, int onTarget, int pawn, int door) {
  return world ? AIM_HIT_WORLD : self ? AIM_HIT_SELF : onTarget ? AIM_HIT_TARGET_ATT : pawn ? AIM_HIT_PAWN :
         door ? AIM_HIT_DOOR : AIM_HIT_OTHER;
}
inline const char* aim_hit_label(int c) {
  static const char* const k[AIM_HIT_COUNT] = { "world", "player/self", "target attachment", "other pawn",
                                                "door/mover", "other" };
  return c >= 0 && c < AIM_HIT_COUNT ? k[c] : "?";
}

// Pick target and Cone (AA's rows, its aimsolve.h ekm_cand_better): Crosshair ranks by the angle off centre and
// admits only targets inside the cone; Nearest ranks by distance inside the same cone (the Cone
// row must stay adjustable, and Nearest must not whip the view round to an enemy behind).
enum { AIM_PICK_CROSSHAIR = 0, AIM_PICK_NEAREST = 1 };
enum { AIM_CONE_MIN = 1, AIM_CONE_MAX = 90, AIM_CONE_DEF = 30 };
inline const char* aim_pick_label(int v) { return v == AIM_PICK_NEAREST ? "Nearest" : "Crosshair"; }
inline int aim_in_cone(int pick, float angle, int cone) {
  (void)pick;
  return angle <= (float)cone;   // NaN compares false
}
inline float aim_cand_key(int pick, float angle, float dist) { return pick == AIM_PICK_NEAREST ? dist : angle; }

// One eligible pawn: idx its index in this frame's walk, key what it ranks by (aim_cand_key).
struct AimCand { int idx; float key; int vis; };
inline void aim_cand_sort(AimCand* c, int n) {
  for (int i = 1; i < n; i++) {   // insertion sort: at most AIM_CAND_MAX, nearly ordered frame to frame
    const AimCand x = c[i];
    int j = i;
    for (; j > 0 && (c[j - 1].key > x.key || (c[j - 1].key == x.key && c[j - 1].idx > x.idx)); j--)
      c[j] = c[j - 1];
    c[j] = x;
  }
}
// The first visible candidate in key order; one whose visibility is not known yet stops the walk, and the previous
// target (prevIdx) is kept meanwhile while still known visible. -1 for none. Hysteresis: a challenger replaces the
// still-visible previous target only when its key is lower by 10 %, or by 1 degree (Crosshair) / 32 units (Nearest).
enum { AIM_HYST_PCT = 10 };
const float AIM_HYST_DEG = 1.f, AIM_HYST_UU = 32.f;
inline int aim_challenger_wins(int pick, float challenger, float incumbent) {
  const float pct = incumbent * (float)AIM_HYST_PCT / 100.f;
  const float floorM = pick == AIM_PICK_NEAREST ? AIM_HYST_UU : AIM_HYST_DEG;
  return challenger < incumbent - (pct > floorM ? pct : floorM);   // NaN compares false: the target is kept
}
// pick < 0: no hysteresis.
inline int aim_pick(const AimCand* c, int n, int prevIdx, int pick = -1) {
  int prevAt = -1;
  for (int k = 0; prevIdx >= 0 && k < n; k++)
    if (c[k].idx == prevIdx && c[k].vis == AIM_VIS_YES) prevAt = k;
  for (int i = 0; i < n; i++) {
    if (c[i].vis == AIM_VIS_YES) {
      if (pick >= 0 && prevAt > i && !aim_challenger_wins(pick, c[i].key, c[prevAt].key)) return prevAt;
      return i;
    }
    if (c[i].vis != AIM_VIS_UNKNOWN) continue;
    return prevAt;
  }
  return -1;
}

// --- Boss priority (aim.md section 12) -------------------------------------------------------------------------
// The boss is the HUD's boss-meter actor (WPawn.bossMeter). While it is a valid target now (alive, not excluded, in
// range, in front, in reach, visible) nothing else is picked or fired at (no ammo on the adds).
enum { AIM_BOSS_NONE, AIM_BOSS_ON, AIM_BOSS_DEAD, AIM_BOSS_RANGE, AIM_BOSS_INVULNERABLE, AIM_BOSS_TELEPORT,
       AIM_BOSS_WRONGTYPE, AIM_BOSS_DOOMED, AIM_BOSS_EXCLUDED, AIM_BOSS_REACH, AIM_BOSS_LIMITS, AIM_BOSS_CONE,
       AIM_BOSS_HIDDEN, AIM_BOSS_UNKNOWN, AIM_BOSS_BLAST, AIM_BOSS_EXPANDED, AIM_BOSS_COUNT };
inline const char* aim_boss_label(int s) {
  static const char* const k[AIM_BOSS_COUNT] = {
    "no boss meter", "on", "dead or knocked down", "out of range or off screen", "invulnerable", "teleporting",
    "the held gun's damage type cannot hurt it", "doomed by the projectiles in flight",
    "animated away without a render box, player damage 0 or a neutral zone", "out of the projectile's reach",
    "outside the turret's limits", "outside the cone", "hidden, an ally in the way or frozen", "visibility unknown",
    "the blast would reach Duke", "fully expanded or popping (the Expander)" };
  return s >= 0 && s < AIM_BOSS_COUNT ? k[s] : "?";
}
// The tests in the pick's order; one left at its passing value was not reached. excl: an AIM_BOSS_* reason, 0 none.
// safe: the Triggerbot's splash guard lets an explosive shot at it go (splash_judge does not hold).
struct AimBossTests { int found, alive, eligible, excl, reach, limits, cone, vis, safe; };
inline AimBossTests aim_boss_tests_pass(void) {
  const AimBossTests t = { 1, 1, 1, 0, 1, 1, 1, AIM_VIS_YES, 1 };
  return t;
}
// The Off-screen setting as the boss's range test asks it: judged without the pick's cone (the Triggerbot alone), the
// boss must be on screen whatever the setting, or one behind Duke would hold fire on the adds in front.
inline int aim_boss_offscreen(int cone, int offscreenOn) {
  return cone && offscreenOn;
}
// AIM_BOSS_ON, or the first test that failed.
inline int aim_boss_verdict(const AimBossTests& t) {
  if (!t.found) return AIM_BOSS_NONE;
  if (!t.alive) return AIM_BOSS_DEAD;
  if (!t.eligible) return AIM_BOSS_RANGE;
  if (t.excl) return t.excl;
  if (!t.reach) return AIM_BOSS_REACH;
  if (!t.limits) return AIM_BOSS_LIMITS;
  if (!t.cone) return AIM_BOSS_CONE;
  if (t.vis != AIM_VIS_YES) return t.vis == AIM_VIS_NO ? AIM_BOSS_HIDDEN : AIM_BOSS_UNKNOWN;
  return t.safe ? AIM_BOSS_ON : AIM_BOSS_BLAST;
}
// The pick with the boss first: its walk index while it is possible, whatever the adds; else aim_pick over the adds
// with the boss's entry taken out (and no hysteresis toward it). c may be changed. A walk index, -1 for none.
inline int aim_boss_pick(AimCand* c, int n, int prevIdx, int pick, int bossIdx, int state) {
  if (bossIdx >= 0 && state == AIM_BOSS_ON) return bossIdx;
  if (bossIdx >= 0) {
    for (int k = 0; k < n; k++)
      if (c[k].idx == bossIdx) c[k].vis = AIM_VIS_NO;
    if (prevIdx == bossIdx) prevIdx = -1;
  }
  const int at = aim_pick(c, n, prevIdx, pick);
  return at < 0 ? -1 : c[at].idx;
}

// The cone is asked at the Aim at point (head or body) by the pick and by the aimbot's lock alike, so
// a pick is also a keep. Degrees from the view's forward to point, seen from eye.
inline int aim_cone_admits(int pick, const float fwd[3], const float eye[3], const float point[3], int cone) {
  return aim_in_cone(pick, aim_angle(fwd, point[0] - eye[0], point[1] - eye[1], point[2] - eye[2]), cone);
}
// The cheap pre-filter at Location: the Aim at point lies within aim_head_plausible's box, a sphere of radius
// sqrt((r+48)^2 + (h+32)^2) round Location, so it is off centre by at least the Location's angle less that sphere's
// angular radius. Never refuses a pawn aim_cone_admits would take.
inline int aim_cone_prefilter(int pick, float angleLoc, float dist, float r, float h, int cone) {
  const double R = sqrt(((double)r + 48.0) * (r + 48.0) + ((double)h + 32.0) * (h + 32.0));
  if (!((double)dist > R)) return 1;   // the eye inside the sphere: any direction
  const double slack = asin(R / dist) * 180.0 / 3.141592653589793;
  return (double)angleLoc - slack <= (double)cone;   // NaN compares false
}

// --- The head (aim.md section 2) -------------------------------------------------------------------------------
// Per class: bone 'Head' when GetBodyPartForBone(Mesh, 'Head') is Head (1); else EyeBone, then LockOnBone, when
// theirs is; else none (the body). Octababy and Octabrain map no bone to Head.
enum { AIM_PART_HEAD = 1 };
enum { AIM_HB_NONE, AIM_HB_HEAD, AIM_HB_EYE, AIM_HB_LOCK };
inline int aim_head_choice(int headPart, int eyePart, int lockPart) {
  if (headPart == AIM_PART_HEAD) return AIM_HB_HEAD;
  if (eyePart == AIM_PART_HEAD) return AIM_HB_EYE;
  if (lockPart == AIM_PART_HEAD) return AIM_HB_LOCK;
  return AIM_HB_NONE;
}
// BoneGetTranslate returns (0,0,0) for a missing bone: a point outside the pawn's cylinder (with a margin for a
// head above it) is not its head.
inline int aim_head_plausible(float hx, float hy, float hz, float cx, float cy, float cz, float r, float h) {
  const float dx = hx - cx, dy = hy - cy, dz = hz - cz, rr = r + 48.f;
  return dx * dx + dy * dy <= rr * rr && dz <= h + 32.f && dz >= -h;   // NaN compares false
}

// The point-geometry log (aim.md section 3, egg pods 2026-10-10): a pawn whose probe found no point clear is logged
// once per distance band, AIM_PTLOG_PER_BAND pawns a band a level, so far pods behind real cover seen first cannot
// use up the lines a near one needs. Measurement only.
enum { AIM_PTLOG_BANDS = 3, AIM_PTLOG_PER_BAND = 8 };
inline int aim_ptlog_band(float dist) { return dist < 1000.f ? 0 : dist < 2000.f ? 1 : 2; }   // NaN: far
struct AimPtLogKey { const void* pawn; int band; };
struct AimPtLog { AimPtLogKey k[AIM_PTLOG_BANDS * AIM_PTLOG_PER_BAND]; int n, perBand[AIM_PTLOG_BANDS]; };
// 1: log this pawn in this band now (recorded).
inline int aim_ptlog_take(AimPtLog* l, const void* pawn, int band) {
  if (!pawn || band < 0 || band >= AIM_PTLOG_BANDS || l->perBand[band] >= AIM_PTLOG_PER_BAND) return 0;
  for (int i = 0; i < l->n; i++)
    if (l->k[i].pawn == pawn && l->k[i].band == band) return 0;
  const AimPtLogKey e = { pawn, band };
  l->k[l->n++] = e;   // n stays within the array: each band is capped
  l->perBand[band]++;
  return 1;
}

// --- The CurrentShake write (aim.md section 1, "The lever") ----------------------------------------------------
// A rotator difference normalised to -32768..32767 (65536 a turn): the short way round.
inline int aim_wrap16(int d) {
  d &= 0xFFFF;
  return d >= 0x8000 ? d - 0x10000 : d;
}
// rotator(vector): Yaw = atan2(Y, X), Pitch = atan2(Z, |XY|), in 65536 a turn.
inline void aim_rotator_to(float dx, float dy, float dz, int* pitch, int* yaw) {
  const double k = 32768.0 / 3.141592653589793;
  *yaw = (int)floor(atan2((double)dy, (double)dx) * k + 0.5);
  *pitch = (int)floor(atan2((double)dz, sqrt((double)dx * dx + (double)dy * dy)) * k + 0.5);
}
// The shake that makes GetWeaponAim's ViewRotation + ScaledShake equal the wanted rotator. GetWeaponAim scales by
// CurrentFOV / DefaultFOV when they differ, so the difference is divided by that ratio. Roll is 0 (no effect on the
// direction). 0 (nothing to write) when either FOV is unusable.
inline int aim_shake_for(int vrPitch, int vrYaw, int wantPitch, int wantYaw, float curFov, float defFov, int out[3]) {
  if (!(curFov > 1.f && curFov < 179.f) || !(defFov > 1.f && defFov < 179.f)) return 0;
  const double k = curFov != defFov ? (double)curFov / (double)defFov : 1.0;
  out[0] = (int)floor(aim_wrap16(wantPitch - vrPitch) / k + 0.5);
  out[1] = (int)floor(aim_wrap16(wantYaw - vrYaw) / k + 0.5);
  out[2] = 0;
  return 1;
}

// The bracket. PerformFire raises UpdateWeaponTraceVars first and RecordShot last; a refused ExpendAmmo skips
// RecordShot, and UpdateViewShake rebuilds CurrentShake every tick, so a missed restore heals itself.
struct AimBracket { int open; int saved[3]; int wrote[3]; unsigned opened, closed, reopened, leftOpen, restoredLate; };
// At UpdateWeaponTraceVars: cur is CurrentShake now, wr what is about to be written. A bracket still open whose write
// is still in place (a second shot in the same tick after a refused one) keeps the game's value it saved.
inline void aim_bracket_open(AimBracket* b, const int cur[3], const int wr[3]) {
  const int ours = b->open && memcmp(cur, b->wrote, sizeof b->wrote) == 0;
  if (b->open) b->reopened++;
  if (!ours) memcpy(b->saved, cur, sizeof b->saved);
  memcpy(b->wrote, wr, sizeof b->wrote);
  b->open = 1;
  b->opened++;
}
// At RecordShot: 1 with the value to put back, once; whatever became of the target meanwhile.
inline int aim_bracket_close(AimBracket* b, int out[3]) {
  if (!b->open) return 0;
  memcpy(out, b->saved, sizeof b->saved);
  b->open = 0;
  b->closed++;
  return 1;
}
// At the frame's end, still open (no RecordShot came): put back only while our write is still in place (a tick
// without UpdateViewShake); a rebuilt value is the game's own. Closed either way.
inline int aim_bracket_frame_end(AimBracket* b, const int cur[3], int out[3]) {
  if (!b->open) return 0;
  b->open = 0;
  b->leftOpen++;
  if (memcmp(cur, b->wrote, sizeof b->wrote) != 0) return 0;
  memcpy(out, b->saved, sizeof b->saved);
  b->restoredLate++;
  return 1;
}

// --- Gates (aim.md section 5) ---------------------------------------------------------------------------------------------------
// The first reason nothing is redirected, AIM_GO when none (paused, menus, loads: !inLevel). attached: execution,
// turret or control; viewMapper: vehicle/cutscene camera; control: QTE/minigame. AIM_G_MOUNTED is Silent aim's alone.
enum { AIM_GO, AIM_G_OFF, AIM_G_PANEL, AIM_G_LEVEL, AIM_G_DEAD, AIM_G_GRAB, AIM_G_ATTACHED, AIM_G_VIEWMAPPER,
       AIM_G_CONTROL, AIM_G_WEAPON, AIM_G_MOUNTED, AIM_G_COUNT };
// mountedTurret (aim_turret_mounted): the one attachment that opens the gate (docs/re/turrets.md section 6).
struct AimGate { int on, panelOpen, inLevel, playerAlive, grabbing, attached, viewMapper, control, weaponKind,
                 mountedTurret; };
inline int aim_gate(const AimGate& g) {
  if (!g.on) return AIM_G_OFF;
  if (g.panelOpen) return AIM_G_PANEL;
  if (!g.inLevel) return AIM_G_LEVEL;
  if (!g.playerAlive) return AIM_G_DEAD;
  if (g.grabbing) return AIM_G_GRAB;
  if (g.attached && !g.mountedTurret) return AIM_G_ATTACHED;
  if (g.viewMapper) return AIM_G_VIEWMAPPER;
  if (g.control) return AIM_G_CONTROL;
  if (g.weaponKind == AIMW_NONE) return AIM_G_WEAPON;
  return AIM_GO;
}

// --- Mounted guns (docs/re/turrets.md) ---------------------------------------------------------------------------
// ControlRemapperEx.EControlRemapperState (Engine.u, read 2026-10-09): CRS_NoUserAttached 0, CRS_InterpolatingIn 1,
// CRS_FullyAttached 2, CRS_InterpolatingOut 3. Only 2 aims: interpolating in, the hook ignores the turn.
enum { AIM_CRS_FULLY_ATTACHED = 2 };
// PlayerPawn.InputHookActor IsA dnControlRemapperEx_Turret, fully attached, and its MountParent IsA dnControl_Turret.
inline int aim_turret_mounted(int turretRemapper, int state, int parentTurret) {
  return turretRemapper && state == AIM_CRS_FULLY_ATTACHED && parentTurret;
}
// Mounted, the turret's remapper is the ViewMapper (ControlRemapperEx.FinalizeState) and the turret the
// InteractiveDecoration (Pawn.AttachToUsableSomething): those still close the gate when they are anything else.
inline int aim_mount_closes(int mounted, const void* field, const void* own) {
  return field != 0 && !(mounted && field == own);
}
// A mounted gun's shot is redirected by a bracket of its own (mountaim.h; turrets.md section 8); a gun it cannot
// bracket (an unknown class, or a property or function that did not resolve) keeps a gate reason of its own.
inline int aim_silent_gate(int base, int mounted, int bracketable) {
  return base == AIM_GO && mounted && !bracketable ? AIM_G_MOUNTED : base;
}

// The gun by its class chain. The launcher derives from the MG42, so it is looked for first in the whole chain.
enum { AIM_TUR_NONE, AIM_TUR_MG42, AIM_TUR_LAUNCHER, AIM_TUR_SUPER };
inline int aim_turret_class(const wchar_t* const* names, int n) {
  static const wchar_t* const kOrder[3] = { L"dnControl_Turret_MountedGrenadeLauncher", L"dnControl_Turret_MG42",
                                            L"dnControl_Turret_SuperTurret" };
  static const int kTur[3] = { AIM_TUR_LAUNCHER, AIM_TUR_MG42, AIM_TUR_SUPER };
  for (int k = 0; k < 3; k++)
    for (int i = 0; names && i < n; i++)
      if (names[i] && _wcsicmp(names[i], kOrder[k]) == 0) return kTur[k];
  return AIM_TUR_NONE;
}
// The gun as a weapon kind. MG42: hitscan, ShotDistance (class default 50000). SuperTurret: a 16000 u/s rocket, a
// hitscan kind for the gates and overkill, led by its model (aim_turret_model). Launcher: a grenade at GrenadeVelocity
// (default 2500). A live value <= 0 is unread.
const float AIM_TUR_MG42_RANGE = 50000.f, AIM_TUR_GRENADE_SPEED = 2500.f;
struct AimTurretKind { int kind; float maxTrace, speed; };
inline AimTurretKind aim_turret_kind(int tur, float shotDistance, float grenadeVelocity) {
  AimTurretKind k = { AIMW_NONE, 0.f, 0.f };
  if (tur == AIM_TUR_MG42) {
    k.kind = AIMW_HITSCAN;
    k.maxTrace = shotDistance > 0.f ? shotDistance : AIM_TUR_MG42_RANGE;
  } else if (tur == AIM_TUR_SUPER) {
    k.kind = AIMW_HITSCAN;
  } else if (tur == AIM_TUR_LAUNCHER) {
    k.kind = AIMW_PROJECTILE;
    k.speed = grenadeVelocity > 0.f ? grenadeVelocity : AIM_TUR_GRENADE_SPEED;
  }
  return k;
}
// The mounted gun's projectile model (aim.md section 8). Launcher: dnGrenadeLauncherProjectile, PHYS_Falling and
// bBounce at the live GrenadeVelocity, in the gun's PhysicsVolume; no LifeSpan. SuperTurret: dnRocket_SuperTurret's
// defaults, Speed 16000 + AccelerationScaler 100000/s, LifeSpan 6. MG42: hitscan. An unread value takes the default.
inline AimProjModel aim_turret_model(int tur, float grenadeVelocity, float gravityZ, float terminal, int bounce) {
  AimProjModel m = { AIM_PM_NONE, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0 };
  if (tur == AIM_TUR_LAUNCHER) {
    m.kind = AIM_PM_BALLISTIC;
    m.speed = grenadeVelocity > 0.f ? grenadeVelocity : AIM_TUR_GRENADE_SPEED;
    m.gravity = gravityZ < 0.f ? -gravityZ : 950.f;
    m.terminal = terminal > 0.f ? terminal : 2500.f;
    m.bounce = bounce < 0 ? 1 : bounce;
  } else if (tur == AIM_TUR_SUPER) {
    m.kind = AIM_PM_STRAIGHT;
    m.speed = 16000.f;
    m.maxSpeed = 9.0e14f;
    m.accel = 100000.f;
    m.maxT = 6.f;
  }
  return m;
}
// A model with the projectile class default's values where they were read (< 0: unread, the fallback kept):
// Speed, MaxSpeed, AccelerationScaler, LifeSpan.
inline AimProjModel aim_pm_defaults(AimProjModel m, float speed, float maxSpeed, float accel, float life) {
  if (speed > 0.f) m.speed = speed;
  if (maxSpeed > 0.f) m.maxSpeed = maxSpeed;
  if (accel >= 0.f) m.accel = accel;
  if (life >= 0.f) m.maxT = life;
  return m;
}
// dnControl_Turret.InputHook_ViewRotationAdjust clamps RotatePitch/RotateYaw + the turn to [Min, Max] (yaw only with
// bClampYaw), so a target is reachable when the gun's rotation plus the turn from the camera (camP/camY) to it
// (wantP/wantY) lies inside. Relative to the gun, not the camera: TurretViewRotationOffset drops out.
struct AimTurretLimits { int minPitch, maxPitch, minYaw, maxYaw, clampYaw; };
inline int aim_turret_reachable(const AimTurretLimits& l, int rotP, int rotY, int camP, int camY, int wantP, int wantY) {
  const int p = rotP + aim_wrap16(wantP - camP), y = rotY + aim_wrap16(wantY - camY);
  if (p < l.minPitch || p > l.maxPitch) return 0;
  return !l.clampYaw || (y >= l.minYaw && y <= l.maxYaw);
}
// The gun's own direction (pitch, yaw) for the limit tests: dnControl_Turret.UpdateRotations turns the bones to
// Rot(RotatePitch, RotateYaw, 0) >> the turret's Rotation, which is base + Rotate if it has no pitch or roll. The
// camera (free look, aim.md 9) may sit far from it. 1 from the gun; 0 the camera (base unread, tilted or rolled).
inline int aim_gun_view(int baseOk, const int base[3], int rotP, int rotY, int camP, int camY, int out[2]) {
  if (baseOk && abs(aim_wrap16(base[0])) <= 16 && abs(aim_wrap16(base[2])) <= 16) {
    out[0] = aim_wrap16(rotP);
    out[1] = base[1] + rotY;
    return 1;
  }
  out[0] = camP;
  out[1] = camY;
  return 0;
}
// The same test of a launch direction: for a lobbed grenade the gun turns to the solved pitch, not to the
// direct line, so a lob above MaxPitch is out of reach however near the target looks.
inline int aim_turret_dir_ok(const AimTurretLimits& l, int rotP, int rotY, int camP, int camY, const float dir[3]) {
  int p = 0, y = 0;
  aim_rotator_to(dir[0], dir[1], dir[2], &p, &y);
  return aim_turret_reachable(l, rotP, rotY, camP, camY, p, y);
}
inline const char* aim_gate_name(int r) {
  static const char* const kNames[AIM_G_COUNT] = { "go", "off", "panel open", "no level or paused", "player dead",
                                                   "grabbing", "attached to a usable", "view mapper set",
                                                   "using a control", "weapon not redirected",
                                                   "this mounted gun cannot be bracketed" };
  return r >= 0 && r < AIM_G_COUNT ? kNames[r] : "?";
}
// The shot will spend ammo (ExpendAmmo: bInfiniteAmmo, else Ammo.UseAmmo needs Charge >= amount, the config's
// AmmoExpenditure; < 0 unread, taken as 1), so RecordShot will come; an unread charge writes nothing.
inline int aim_ammo_ok(int infinite, int haveCharge, float charge, int amount) {
  return infinite || (haveCharge && charge >= (float)(amount < 0 ? 1 : amount));
}
inline int aim_marker_on(int silentOn, int marker) { return silentOn && marker; }
// The draw gates: Silent aim or the Aimbot (AA's aimon || silenton).
inline int aim_marker_shown(int silentOn, int aimbotOn, int marker) {
  return aim_marker_on(silentOn || aimbotOn, marker);
}

// The attach/dismount log, debounced (each pause logged a dismount and a fresh attach): a frame whose
// mount was not read (pause, menu, a faulted read) changes nothing; a dismount is logged after AIM_MOUNT_OFF_FRAMES
// read frames off, at once when another gun is mounted (SWITCH). gun: null when not mounted. Logs only.
enum { AIM_MOUNT_OFF_FRAMES = 30 };
enum { AIM_ML_NONE = 0, AIM_ML_ATTACH = 1, AIM_ML_DISMOUNT = 2, AIM_ML_SWITCH = 4 };
struct AimMountLog { const void* gun; int offFrames; };
inline int aim_mount_log_step(AimMountLog* s, int read, const void* gun) {
  if (!read) return AIM_ML_NONE;
  if (gun) {
    s->offFrames = 0;
    if (gun == s->gun) return AIM_ML_NONE;
    const int had = s->gun != 0;
    s->gun = gun;
    return had ? AIM_ML_SWITCH : AIM_ML_ATTACH;
  }
  if (!s->gun || ++s->offFrames <= AIM_MOUNT_OFF_FRAMES) return AIM_ML_NONE;
  s->gun = 0;
  s->offFrames = 0;
  return AIM_ML_DISMOUNT;
}

// --- The glue (aim.cpp), game thread only ------------------------------------------------------------------------
// Once a frame after world_frame and before pehook_frame: the gate, the weapon, the target. W, H: the back buffer
// (the off-screen rule).
void aim_frame(int W, int H, int inLevel, int panelOpen);
// Null while usable; else why Silent aim is unavailable.
const char* aim_refused(void);
// The marker's points this frame (raw aim point, led point); 0 without a target or with the marker off.
int aim_marker_points(float raw[3], float led[3]);
// For the Triggerbot (tbot.h). Silent aim's visible target this frame, null unless Silent aim is live and its gate
// open (Locked mode).
const void* aim_target(void);
// The player's gate this frame as aim_gate gives it with the switch on: computed while Silent aim or the Triggerbot
// is on, AIM_G_OFF otherwise.
int aim_gate_now(void);
// The held weapon (its config class, read once per class). reach: the fists' melee reach this frame
// (aim_melee_reach), 0 for any other kind.
struct AimWeapon { int kind; float maxTrace; int cfgName; float reach; };
AimWeapon aim_weapon(void);
// The shared target for the aimbot (aimbot.h): this frame's visible target while the player's gate is open and
// Silent aim or the aimbot asked for one; null otherwise.
const void* aim_target_shared(void);
// For the aimbot's handler, between frames: 1 with the point to turn to (the Aim at point, or for a projectile gun
// along the solved launch direction) seen from eye, while target is still this frame's target and still one
// (aimbot_keep) and in reach of the projectile. 0, with *why, drops the lock at once.
int aim_track(const void* target, const float eye[3], int pitch, int yaw, float out[3], const char** why);
// Weapon.IsCompletelyOutOfAmmo() on the held gun this frame, asked only while the aimbot is engaged with a gun kind
// and the gate open: 1 out, 0 not, -1 not asked or not called.
int aim_out_of_ammo(void);
// Silent aim would redirect a shot now: live, its gate open, and the handler's pre-checks pass (ammo the shot can
// spend, CurrentShake readable, both FOVs usable). Whether the held weapon is bracketed is the caller's (AIMW_*).
int aim_redirect_ready(void);
// The player is on a mounted turret this frame (aim_turret_mounted): aim_weapon is the gun, the view is the camera.
int aim_mounted(void);
// The aim target's kind (aircraft.h): 1 when t is a hostile live ship this frame, not a pawn, so the pawn-only rules
// (the spawnbox exclusions) skip it. aim_ship_shootable adds: damageable, and the held or
// mounted gun hurts it (ac_weapon_hurts).
int aim_is_ship(const void* t);
int aim_ship_shootable(const void* t);
// An achievement target this frame (achhelp.h: its row on, alive, damageable), not a pawn; shootable adds that the held
// or mounted gun's damage type can hurt it (ah_aim_type_ok).
int aim_is_ach(const void* t);
int aim_ach_shootable(const void* t);
// This frame's hostile pawn candidates (in range, on screen, inside the cone's pre-filter; hidden or not), from the
// pick or, for the Triggerbot alone, a count without traces; -1 when neither ran. Any holds the fish back (ah_aim_pass).
int aim_pawn_cands(void);
// The boss this frame (aim.md section 12): 1 while it is possible (AIM_BOSS_ON), so the pick and the Triggerbot take
// nothing else; aim_boss the boss-meter pawn then, else null. Judged by the pick (its cone) when Silent aim or the
// aimbot picks, else for the Triggerbot by the screen alone. aim_boss_skip: the Triggerbot held fire on an add.
int aim_boss_on(void);
const void* aim_boss(void);
void aim_boss_skip(int began);
// The mounted turret's remapper ViewOffset (Pitch, Yaw, Roll): the turn queued but not yet applied. 0 not mounted or
// unread (out zeroed).
int aim_mount_view_offset(int out[3]);
// This frame's mounted gun direction (pitch, yaw: aim_gun_view): 1 from the gun's own rotation, 0 the camera or not
// mounted (out still set, for the log).
int aim_mount_gun_view(int out[2]);
// The mounted gun's bUseInstantAimRate: 1 it takes ViewOffset whole (MG42, launcher), 0 rate-limited (SuperTurret),
// -1 unread or not mounted.
int aim_mount_instant(void);
// The shot's own ray: from the eye along ViewRotation (what GetWeaponAim starts from, after the aimbot's turn) the
// gun's reach with the bullet trace flags. 1 it lands on target (or an attachment of it), 0 not, -1 it does not resolve.
// aim_view_off_deg: degrees between that view and the target's aim point, -1 unresolved.
int aim_view_ray_hits(const void* target);
float aim_view_off_deg(const void* target);
// Degrees between that view and the aimbot's own point for target (the led point aim_track gives), -1 unresolved.
float aim_view_lead_deg(const void* target);
// Pawn.LineOfSightTo(pawn) on the player (aim.cpp's proof): 1 visible, 0 not or refused, -1 it does not resolve.
int aim_line_of_sight(const void* pawn);
// The bullet-flag trace from the view to the pawn's render-box centre (spawnbox_body), as the pick's probe: 1 clear,
// 0 hidden or refused, -1 no box this frame or the trace does not resolve.
int aim_box_visible(const void* pawn);
// spawnbox.cpp: the pawn's aim point source changed (flagged far, or back near); its cached visibility is dropped.
void aim_vis_forget(const void* pawn);
// For overkill.cpp: the mounted gun (null when not mounted); the name index of the class the held or mounted
// projectile gun fires (0: none or unread); whether this frame's pick passed over a doomed candidate.
const void* aim_mount_gun(void);
int aim_proj_class_name(void);
int aim_passed_doomed(void);
// The damage type class the held or mounted gun deals (docs/re/spawns.md section 6), null when unknown: WeaponConfig
// TraceDamageType (hitscan), ProjectileClass's DamageClass, MeleeDamageType; the MG42's TraceDamageType, the
// launcher grenade's and SuperTurret rocket's DamageClass.
const void* aim_damage_type(void);
// The held or mounted gun's projectile model this frame (aim_turret_model, or the weapon's ProjectileClass defaults).
AimProjModel aim_proj_model(void);
// The ballistic instrument (aim.md section 13): the held or mounted ballistic gun's solved shot at target from where it
// leaves now: the predicted impact, the launch pitch in degrees, the flight time and the eye (the camera) it was asked
// from. 0 when no ballistic gun is up or there is no solution.
int aim_lob_predict(const void* target, float impact[3], float* pitchDeg, float* t, float eye[3]);

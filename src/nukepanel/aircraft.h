#pragma once
// Hostile aircraft (docs/re/aircraft.md): the alien dropship and gunship and the drone fighter are dnDecoration actors,
// never in Level.PawnList, so worldscan finds them by class. The ESP draws them with the enemies; the aim takes
// one only when no pawn is a candidate and the weapon can hurt it. Pure rules for the suite; aircraft.cpp reads.
#include <math.h>
#include <wchar.h>
#include "worldscan.h"
#include "aim.h"   // AIM_TUR_*

enum { AC_DROPSHIP, AC_GUNSHIP, AC_FIGHTER, AC_WASP };
// By class name along the super chain, nearest first. The EDF wasps are matched only to be refused: they are friendly
// (bEnemy False, aircraft.md section 3), whatever a map instance says.
struct AcClassRule { const char* name; int kind; };
static const AcClassRule AC_CLASSES[] = {
  { "dnEDFWasp_Low", AC_WASP },
  { "dnEDFWasp", AC_WASP },
  { "dnAlienDropship", AC_DROPSHIP },
  { "dnAlienGunship", AC_GUNSHIP },
  { "dnFlybyPlane_Fighter", AC_FIGHTER },
};
enum { AC_CLASS_COUNT = (int)(sizeof AC_CLASSES / sizeof AC_CLASSES[0]) };
inline int ac_kind_of_rule(int rule) { return rule >= 0 && rule < AC_CLASS_COUNT ? AC_CLASSES[rule].kind : -1; }

inline const char* ac_label(int kind) {
  return kind == AC_DROPSHIP ? "Dropship" : kind == AC_GUNSHIP ? "Gunship" : kind == AC_FIGHTER ? "Fighter" : "";
}

// Hostile by class: never a wasp; the fighter by its class (it shoots Duke, and bEnemy is not set in its defaults,
// aircraft.md section 1) but only while it can be damaged, else it is scenery (the checkpoint-load flyby, 2026-10-09:
// bNoDamage 1); the dropship and gunship by bEnemy (True in their defaults).
inline int ac_enemy(int kind, int bEnemy, int noDamage, int vulnerable) {
  if (kind == AC_FIGHTER) return noDamage <= 0 && vulnerable != 0;
  return (kind == AC_DROPSHIP || kind == AC_GUNSHIP) && bEnemy > 0;
}

// dnDecoration.DecoState: DECOSTATE_Alive 0; 1 and 2 are dead (DecorationIsDead, aircraft.md section 2). -1 unread.
enum { AC_DECO_DEAD = 1 };
// Hostile and live: an enemy, shown (placed ships sit bHidden until their script runs), not being destroyed, not dead.
inline int ac_hostile_live(int kind, int bEnemy, int hidden, int deleteMe, int decoState, float health, int noDamage,
                           int vulnerable) {
  return ac_enemy(kind, bEnemy, noDamage, vulnerable) && !hidden && !deleteMe && decoState < AC_DECO_DEAD && health > 0.f;   // NaN: no
}

// Damage can land: not bNoDamage, HealthPrefab not NeverBreak (0; -1 unread), and the fighter only while
// bIsVulnerable (its TakeDamage returns otherwise). Map11C's dummies (HealthPrefab 0, Health 9999) fail here.
inline int ac_damageable(int kind, int noDamage, int healthPrefab, int vulnerable) {
  if (noDamage > 0 || healthPrefab == 0) return 0;
  return kind != AC_FIGHTER || vulnerable > 0;
}

// The weapons whose damage the dropship and gunship accept (DamageTypesAcceptedOnly = ExplosionDamage,
// LargeCaliberBulletDamage; aircraft.md section 2): the RPG, the Devastator and pipe bombs on foot; every mounted gun
// (MG42 LargeCaliberBulletDamage, the grenade launcher and the Super turret ExplosionDamage). Bullets do nothing.
inline int ac_weapon_hurts(const wchar_t* const* cfgChain, int n, int turret) {
  if (turret == AIM_TUR_MG42 || turret == AIM_TUR_LAUNCHER || turret == AIM_TUR_SUPER) return 1;
  static const wchar_t* const kHurts[] = { L"RPGWeaponConfig", L"DevastatorWeaponConfig", L"PipeBombWeaponConfig" };
  for (int i = 0; cfgChain && i < n; i++)
    for (int k = 0; k < (int)(sizeof kHurts / sizeof kHurts[0]); k++)
      if (cfgChain[i] && _wcsicmp(cfgChain[i], kHurts[k]) == 0) return 1;
  return 0;
}

// The aim's last resort (target them last, if no normal enemy is visible): the ship pass runs only
// when the pawn pass picked nothing, and only with a weapon that hurts them.
inline int ac_ship_pass(int pawnPicked, int weaponHurts) { return !pawnPicked && weaponHurts; }
// How the ship pass runs. A pawn walk stopped on a pawn of unknown visibility (the frame's traces spent) runs none,
// except that a previous ship target still hostile and live is kept from the cache alone (else the aimbot
// and the Triggerbot drop and retake it every few frames among hidden troopers).
enum { AC_PASS_NONE, AC_PASS_FULL, AC_PASS_CACHE };
inline int ac_ship_pass_mode(int pawnPicked, int pawnUnknown, int weaponHurts, int prevShipLive) {
  if (!ac_ship_pass(pawnPicked, weaponHurts)) return AC_PASS_NONE;
  if (!pawnUnknown) return AC_PASS_FULL;
  return prevShipLive ? AC_PASS_CACHE : AC_PASS_NONE;
}

// A ship near a mounted gun's limit: centre passed the strict limit test only every other frame (camera and RotatePitch
// disagree in flight). The target holds through the limits while the gun can still bring its hull on: slack = hull
// angular half extent in rotator units (65536 a turn), capped at AC_LIMIT_SLACK_MAX (2 degrees); 0 when unusable.
enum { AC_LIMIT_SLACK_MAX = 364 };
inline int ac_limit_slack(float halfExtent, float dist) {
  if (!(dist > 0.f) || !(halfExtent > 0.f)) return 0;
  const double a = atan2((double)halfExtent, (double)dist) * 32768.0 / 3.141592653589793;
  return a >= AC_LIMIT_SLACK_MAX ? AC_LIMIT_SLACK_MAX : (int)a;
}
// aim_turret_reachable widened by slackP / slackY at each limit.
inline int ac_turret_reachable(const AimTurretLimits& l, int rotP, int rotY, int camP, int camY, int wantP, int wantY,
                               int slackP, int slackY) {
  const int p = rotP + aim_wrap16(wantP - camP), y = rotY + aim_wrap16(wantY - camY);
  if (p < l.minPitch - slackP || p > l.maxPitch + slackP) return 0;
  return !l.clampYaw || (y >= l.minYaw - slackY && y <= l.maxYaw + slackY);
}
// Whether a ship enters this frame's candidates: the current target by the slack test, any other by the strict one;
// the cache-only pass (AC_PASS_CACHE) admits the current target alone, so it can keep but never switch. A different
// ship then replaces the target only by aim_pick's margin (aim_challenger_wins).
inline int ac_ship_admit(int isTarget, int cacheOnly, int reachStrict, int reachSlack) {
  if (cacheOnly) return isTarget && reachSlack;
  return isTarget ? reachSlack : reachStrict;
}

// Why the target ship left the pick (the flip line, 2026-10-11: a dropship lost and retaken every other frame on the
// SuperTurret): the first test it failed, in the pick's own order. Each flag 1 passed, 0 failed; a test after the
// failing one is never read. vis: AIM_VIS_*; passMode: ac_ship_pass_mode's answer.
enum { AC_LOST_NONE, AC_LOST_NO_VIEW, AC_LOST_NO_PASS, AC_LOST_GONE, AC_LOST_UNDAMAGEABLE, AC_LOST_DOOMED,
       AC_LOST_RANGE, AC_LOST_SCREEN, AC_LOST_REACH, AC_LOST_LIMITS, AC_LOST_CONE, AC_LOST_HIDDEN, AC_LOST_UNKNOWN,
       AC_LOST_OUTRANKED, AC_LOST_COUNT };
struct AcShipVerdict { int viewOk, passMode, seen, damageable, doomed, inRange, onScreen, reach, limits, cone, vis,
                       picked; };
inline int ac_lost_reason(const AcShipVerdict& v) {
  if (!v.viewOk) return AC_LOST_NO_VIEW;
  if (v.passMode == AC_PASS_NONE) return AC_LOST_NO_PASS;
  if (!v.seen) return AC_LOST_GONE;
  if (!v.damageable) return AC_LOST_UNDAMAGEABLE;
  if (v.doomed) return AC_LOST_DOOMED;
  if (!v.inRange) return AC_LOST_RANGE;
  if (!v.onScreen) return AC_LOST_SCREEN;
  if (!v.reach) return AC_LOST_REACH;
  if (!v.limits) return AC_LOST_LIMITS;
  if (!v.cone) return AC_LOST_CONE;
  if (v.vis == AIM_VIS_NO) return AC_LOST_HIDDEN;
  if (v.vis != AIM_VIS_YES) return AC_LOST_UNKNOWN;
  return v.picked ? AC_LOST_NONE : AC_LOST_OUTRANKED;
}
inline const char* ac_lost_name(int r) {
  static const char* const kNames[AC_LOST_COUNT] = {
    "kept", "no view", "no ship pass", "not hostile and live", "not damageable", "doomed", "out of range",
    "off screen", "out of the projectile's reach", "outside the turret's limits", "outside the cone", "hidden",
    "visibility unknown", "out-ranked" };
  return r >= 0 && r < AC_LOST_COUNT ? kNames[r] : "?";
}

// The health bar's cap: the live HealthCap while it holds the most seen, else the most seen (0: unread).
inline float ac_health_cap(float liveCap, float maxSeen) { return liveCap >= maxSeen ? liveCap : maxSeen; }

// A usable render box (GetRenderBoundingBox's mins/maxs): ordered, finite, not the all-zero box of no primitive, and
// no axis past MB_EXTENT_MAX's 8192 (a dropship is about 1000 long).
inline int ac_box_ok(const float mn[3], const float mx[3]) {
  int zero = 1;
  for (int k = 0; k < 3; k++) {
    if (!(mn[k] <= mx[k]) || !(mx[k] - mn[k] <= 8192.f) || mn[k] < -1e30f || mx[k] > 1e30f) return 0;
    if (mn[k] != 0.f || mx[k] != 0.f) zero = 0;
  }
  return !zero;
}

// --- The glue (aircraft.cpp), game thread --------------------------------------------------------------------------
// One hostile, live ship this frame: c and h the render box's centre and half extents (the collision cylinder's when
// boxOk is 0), off its Location by off (centre - Location, so a later read of Location moves the box with it).
enum { AC_MAX = 32 };
struct AcShip { const void* obj; int kind, clsName, damageable, boxOk; float loc[3], off[3], c[3], h[3], health, cap; };
extern const WsClient g_aircraftScan;   // worldscan.cpp's client
// After worldscan_frame: reads the cached ships under one guard, boxes the hostile live ones, logs each ship once.
void aircraft_frame(int inLevel);
// This frame's hostile live ships; valid until the next aircraft_frame.
int aircraft_ships(const AcShip** out);
// The ship this frame holds for obj, or null (not a ship, or not hostile and live).
const AcShip* aircraft_find(const void* obj);
// The ESP's drawn count this frame, for the worldscan line.
void aircraft_drawn(int n);

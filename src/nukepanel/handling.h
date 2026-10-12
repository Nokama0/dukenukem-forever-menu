#pragma once
// Fire rate and reload speed (Weapons > HANDLING; handling.md sections 5-7). The held weapon's animation controller is
// rescaled through UpdateGlobalAnimRateScale (by ProcessEvent): the fire factor in fire states, the reload factor in
// reload states; a click-fired gun's lockout is cleared through ClearLockout once LockoutFireTime / factor has passed.
#include <stdio.h>

// DNMP's rows (its weapons.h): fire 100-400 %, reload 100-1000 % plus a Max stop one step past (stored as 1100).
enum { WEAP_FIRE_MIN = 100, WEAP_FIRE_MAX = 400, WEAP_STEP = 50,
       WEAP_RELOAD_MAX = 1000, WEAP_RELOAD_SLIDER_MAX = 1100 };

inline int weap_round_step(int v, int lo, int hi) {
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  return ((v - lo + WEAP_STEP / 2) / WEAP_STEP) * WEAP_STEP + lo;
}
// A hand-edited ini value lands on the nearest knob stop.
inline int weap_fire_pct_clamped(int v) { return weap_round_step(v, WEAP_FIRE_MIN, WEAP_FIRE_MAX); }
inline int weap_reload_pct_clamped(int v) { return weap_round_step(v, WEAP_FIRE_MIN, WEAP_RELOAD_SLIDER_MAX); }
inline bool weap_reload_is_max(int pct) { return weap_reload_pct_clamped(pct) == WEAP_RELOAD_SLIDER_MAX; }
inline void weap_fire_text(int pct, char* out, int cap) {
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "%d%%", weap_fire_pct_clamped(pct));
}
inline void weap_reload_text(int pct, char* out, int cap) {
  if (weap_reload_is_max(pct)) _snprintf_s(out, (size_t)cap, _TRUNCATE, "Max");
  else _snprintf_s(out, (size_t)cap, _TRUNCATE, "%d%%", weap_reload_pct_clamped(pct));
}

// Max cannot be instant here: the ammo comes from a mid-animation notify (handling.md section 7), so Max is 20x.
const float HD_RELOAD_MAX_FACTOR = 20.f;
inline float weap_fire_factor(int pct) { return (float)weap_fire_pct_clamped(pct) / 100.f; }
inline float weap_reload_factor(int pct) {
  return weap_reload_is_max(pct) ? HD_RELOAD_MAX_FACTOR : (float)weap_reload_pct_clamped(pct) / 100.f;
}

// EWeaponState (Engine.u Weapon): 4-6 fire (Fire, Fire_Start, Fire_Stop), 7-9 reload; everything else (activating,
// deactivating, idle, melee) runs at the stock rate.
enum { HD_WS_FIRE = 4, HD_WS_FIRE_STOP = 6, HD_WS_RELOAD = 7, HD_WS_RELOAD_STOP = 9 };
inline float hd_want(int state, int firePct, int reloadPct) {
  if (state >= HD_WS_FIRE && state <= HD_WS_FIRE_STOP) return weap_fire_factor(firePct);
  if (state >= HD_WS_RELOAD && state <= HD_WS_RELOAD_STOP) return weap_reload_factor(reloadPct);
  return 1.f;
}
// Either row off its stock stop: the poll drives the held controller.
inline int hd_active(int firePct, int reloadPct) {
  return weap_fire_pct_clamped(firePct) != WEAP_FIRE_MIN || weap_reload_pct_clamped(reloadPct) != WEAP_FIRE_MIN;
}

// The held controller this frame. Active: call whenever the live scale is not the wanted one (a state change, a
// save that carried a scale). Inactive: only to put 1.0 back over our own value still in place.
inline int hd_call_due(int active, float live, float want, int hasRec, float recOurs) {
  if (active) return live != want;
  return hasRec && live == recOurs && live != 1.f;
}
// No script writes AnimRateScale (handling.md section 3), so a held controller off 1.0 without a record of ours
// carries our value from a save: it is taken as ours, so the restore rule covers it.
inline int hd_adopt(int hasRec, float live) { return !hasRec && live != 1.f; }
// A controller we changed that is no longer held (a switch, death, the weapon gone): 1.0 back only while our value
// is still in place, the accuracy.md rule.
inline int hd_restore_due(int valid, int held, float live, float ours) {
  return valid && !held && live == ours && live != 1.f;
}

// The lockout lever (c): from the frame bWeaponFireLockout rises, ClearLockout is due once lockTime / f of game time
// has passed (at once when eager); once per lockout, never at f <= 1 or for a gun without a lockout. Game time, so
// game speed composes.
struct HdLock { int was, fired; float t0; };
inline HdLock hd_lock_init(void) { HdLock l = { 0, 0, 0.f }; return l; }
// Eager: due on the frame the rise is seen. A fire-start gun without a looping fire (the Ripper) refires only if the
// lockout is clear when its Fire anim ends, and that anim can end within a frame of LockoutFireTime / f; a lost race
// costs FireStop, idle and FireStart (one-shot bursts, handling.md section 8). A flag not read (-1): not eager.
inline int hd_lock_eager(int hasFireStart, int hasLoopingFire) { return hasFireStart == 1 && hasLoopingFire == 0; }
inline int hd_lock_step(HdLock* l, int lockNow, float now, float lockTime, float f, int eager = 0) {
  if (!lockNow) { l->was = 0; return 0; }
  if (!l->was || now < l->t0) { l->was = 1; l->fired = 0; l->t0 = now; }
  if (l->fired || !(f > 1.f) || !(lockTime > 0.f)) return 0;
  if (!eager && now - l->t0 < lockTime / f) return 0;
  l->fired = 1;
  return 1;
}
// Read again right after our ClearLockout: still set means ClearLockout refired the gun (idle, not interrupting),
// which is a new lockout starting now.
inline void hd_lock_after_clear(HdLock* l, int lockNow, float now) {
  if (lockNow) { l->was = 1; l->fired = 0; l->t0 = now; }
  else l->was = 0;
}

// The fire trace: one line per WeaponState change while fire is held or on the frame it is let go (held -1: the
// flag is not readable, so a change into or out of a fire state), at most HD_TRACE_MAX lines a level.
enum { HD_TRACE_MAX = 120 };
inline int hd_trace_due(int held, int prevHeld, int prevState, int state, int lines) {
  if (lines >= HD_TRACE_MAX || state == prevState) return 0;
  if (held < 0 || prevHeld < 0) {
    const int was = prevState >= HD_WS_FIRE && prevState <= HD_WS_FIRE_STOP;
    return was || (state >= HD_WS_FIRE && state <= HD_WS_FIRE_STOP);
  }
  return held > 0 || prevHeld > 0;
}

// The AnimRate 0.0 question (handling.md section 6): the channel rates before a call and one frame after. Scaled:
// every running channel moved by new/old; unchanged: none moved; zero: no channel runs at a rate at all.
enum { HDV_SCALED, HDV_UNCHANGED, HDV_ZERO, HDV_MIXED };
inline int hd_near(float a, float b) {
  const float d = a > b ? a - b : b - a;
  const float m = (a < 0 ? -a : a) > (b < 0 ? -b : b) ? (a < 0 ? -a : a) : (b < 0 ? -b : b);
  return d <= 0.02f * m + 1e-6f;
}
inline int hd_rate_verdict(const float* before, const float* after, int n, float oldScale, float newScale) {
  int any = 0, scaled = 1, same = 1;
  for (int i = 0; i < n; i++) {
    if (before[i] == 0.f && after[i] == 0.f) continue;
    any = 1;
    const float expect = (oldScale == 0.f || before[i] == 0.f) ? newScale : before[i] * newScale / oldScale;
    if (!hd_near(after[i], expect)) scaled = 0;
    if (!hd_near(after[i], before[i])) same = 0;
  }
  if (!any) return HDV_ZERO;
  if (scaled && !(same && newScale != oldScale)) return HDV_SCALED;
  if (same) return HDV_UNCHANGED;
  return HDV_MIXED;
}
inline const char* hd_verdict_text(int v) {
  switch (v) {
    case HDV_SCALED:    return "the running channels moved with the scale";
    case HDV_UNCHANGED: return "NO EFFECT: the running channels kept their rate";
    case HDV_ZERO:      return "NO EFFECT: every channel reads 0 while animating, the clock is elsewhere";
    default:            return "mixed: some channels moved, some did not";
  }
}

// The shot-interval check: the factor the interval actually shrank by against the one asked for. 1 the lever
// worked (within 25 %), 0 no effect (within 10 % of stock), -1 unclear.
inline int hd_rate_effect(float stockInterval, float interval, float f) {
  if (!(stockInterval > 0.f) || !(interval > 0.f) || !(f > 0.f)) return -1;
  const float got = stockInterval / interval;
  if (got >= f * 0.75f && got <= f * 1.25f) return 1;
  if (got >= 0.9f && got <= 1.1f) return 0;
  return -1;
}

// Game thread, once a frame. live: a level and player exist and nothing pauses the game.
void handling_frame(int live);
// Null while usable (or not yet checked); else why the rows are unavailable.
const char* handling_refused(void);

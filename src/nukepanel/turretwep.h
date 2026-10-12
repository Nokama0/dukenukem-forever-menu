#pragma once
// Fire rate and Perfect accuracy on the mounted gun (docs/re/turrets.md section 7). The pure rules; turretwep.cpp owns
// the engine reads and calls. No new rows: the Weapons rows reach the gun the player is mounted on.
#include "aim.h"
#include "handling.h"
#include "optrestore.h"

// The levers per gun (aim_turret_class). SPREAD: MaxSpread through GlobalSetProperty (MG42 PerformFire is the only
// reader). GUNANIM: the gun's own channel 0 (its FireDown anim carries the FireEffects notify that shoots). USERANIM:
// the player's animation controller; its FireDown anim's end refires the launcher and the SuperTurret.
enum { TW_L_SPREAD = 1, TW_L_GUNANIM = 2, TW_L_USERANIM = 4 };
inline int tw_levers(int tur) {
  if (tur == AIM_TUR_MG42) return TW_L_SPREAD | TW_L_GUNANIM;
  if (tur == AIM_TUR_LAUNCHER) return TW_L_GUNANIM | TW_L_USERANIM;
  if (tur == AIM_TUR_SUPER) return TW_L_USERANIM;
  return 0;
}

// One rate lever (a channel rate or a controller's AnimRateScale): raised to stock x f while the gun is in FireDown,
// the stock value put back when it leaves FireDown, the row goes to 100 % or the player dismounts, and only while our
// value is still in place. A new anim start (the launcher refires through idle) reads as a new stock.
struct TwAnim { int applied; float stock, ours; };
inline TwAnim tw_anim_init(void) { TwAnim a = { 0, 0.f, 0.f }; return a; }
enum { TW_NONE = 0, TW_SET = 1 };
// want: mounted on a gun with this lever and in FireDown. ok: live was read (a faulted read changes nothing).
inline int tw_anim_step(TwAnim* a, int want, float f, int ok, float live, float* out) {
  if (!ok) return TW_NONE;
  if (want && f > 1.f && live > 0.f) {
    const int mine = a->applied && hd_near(live, a->ours);
    if (!mine) a->stock = live;   // first sight, or a new anim start replaced ours
    const float target = a->stock * f;
    if (mine && hd_near(a->ours, target)) return TW_NONE;
    a->applied = 1;
    a->ours = target;
    *out = target;
    return TW_SET;
  }
  if (!a->applied) return TW_NONE;
  a->applied = 0;
  if (!hd_near(live, a->ours)) return TW_NONE;   // the next anim already replaced ours
  *out = a->stock;
  return TW_SET;
}

// The spread lever: one record (the class whose default holds our 0). cur: MaxSpread on the default of the record's
// class when one is held, else of cls. A record for another class, or the lever off, releases first.
struct TwSpread { const void* cls; int clsName; OptRestore r; };
inline TwSpread tw_spread_init(void) { TwSpread s = { 0, 0, optrestore_init() }; return s; }
enum { TW_SP_NONE = 0, TW_SP_APPLY = 1, TW_SP_RESTORE = 2, TW_SP_DROP = 3 };
inline int tw_spread_step(TwSpread* s, int on, const void* cls, int clsName, int curOk, float cur, float* back) {
  if (!curOk) return TW_SP_NONE;
  if (s->cls && (!on || s->cls != cls || s->clsName != clsName)) {
    float b = cur;
    const int mine = optrestore_release(s->r, cur, &b);
    *s = tw_spread_init();
    if (mine && b != cur) {
      *back = b;
      return TW_SP_RESTORE;
    }
    return TW_SP_DROP;   // changed by the game since, or it was 0 before ours
  }
  if (!on || !cls) return TW_SP_NONE;
  if (!s->cls) {
    s->cls = cls;
    s->clsName = clsName;
    s->r = optrestore_init();
  }
  if (opt_step(s->r, 1, cur, 0.f) != OPT_APPLY) return TW_SP_NONE;
  s->r = optrestore_apply(s->r, 0, cur, 0.f);   // level 0 for good: a class default outlives levels
  return cur != 0.f ? TW_SP_APPLY : TW_SP_NONE;
}

// Each apply walks the whole object table (GlobalSetProperty, bAllInstances 1): at most this many per mount, so a
// MaxSpread reset loop cannot walk it every frame. A capped apply writes nothing; its release then drops the record.
enum { TW_SPREAD_APPLY_MAX = 3 };
inline int tw_apply_allowed(int appliesThisMount) { return appliesThisMount < TW_SPREAD_APPLY_MAX; }

// Shots from MuzzleIndex (MG42.FireEffects: ++, past 2 back to 0; -1 before the first shot): 0-2 a frame exactly.
inline int tw_muzzle_shots(int prev, int now) {
  if (prev < -1 || prev > 2 || now < 0 || now > 2) return 0;
  if (prev == -1) return now + 1;
  return (now - prev + 3) % 3;
}

// The measured rate: from the first shot seen while fire is held to the last, closed on release. 1 on the closing
// frame with *n shots over *secs of game time.
struct TwRate { int open, n; float t0, tLast; };
inline TwRate tw_rate_init(void) { TwRate r = { 0, 0, 0.f, 0.f }; return r; }
inline int tw_rate_step(TwRate* r, int held, float now, int shots, int* n, float* secs) {
  if (held) {
    if (shots <= 0) return 0;
    if (!r->open) {
      r->open = 1;
      r->n = 0;
      r->t0 = now;
    }
    r->n += shots;
    r->tLast = now;
    return 0;
  }
  if (!r->open) return 0;
  r->open = 0;
  *n = r->n;
  *secs = r->tLast - r->t0;
  return 1;
}
// Shots per second over a window: the first shot opens it, so n - 1 intervals.
inline float tw_rate_per_s(int n, float secs) { return n > 1 && secs > 0.f ? (float)(n - 1) / secs : 0.f; }

// Game thread, once a frame. live: a level and player exist and nothing pauses the game.
void turretwep_frame(int live);

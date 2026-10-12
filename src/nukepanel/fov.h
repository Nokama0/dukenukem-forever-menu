#pragma once
// Custom field of view (View > FIELD OF VIEW): the rules are duke-dnmp's zoom.h FOV rules; the path is the game's
// own 'ConsoleFOV' scale modifier on the player, never DefaultFOV, a config property the quit path saves
// (docs/re/player-options.md "Field of view").
#include <stdio.h>
#include "optrestore.h"

// The slider holds the game's own FOV degrees (DefaultFOV's unit). 0 stored is the game's own value, which the
// reset arrow writes.
enum { FOV_MIN_DEG = 40, FOV_MAX_DEG = 150, FOV_STOCK = 0 };

inline double fov_want_deg(int cfgDeg, double stockDeg) {
  if (cfgDeg == FOV_STOCK) return stockDeg;
  return (double)(cfgDeg < FOV_MIN_DEG ? FOV_MIN_DEG : cfgDeg > FOV_MAX_DEG ? FOV_MAX_DEG : cfgDeg);
}
// Where the knob sits: the stored value, or the stock rounded to a whole degree.
inline int fov_slider_value(int cfgDeg, double stockDeg) {
  const int v = cfgDeg == FOV_STOCK ? (int)(stockDeg + 0.5) : cfgDeg;
  return v < FOV_MIN_DEG ? FOV_MIN_DEG : v > FOV_MAX_DEG ? FOV_MAX_DEG : v;
}

// The readout fits the panel's readout column (FOV_READOUT_RESERVE); the stock shows to a tenth below 100.
const char* const FOV_READOUT_RESERVE = "99.9";
inline void fov_value_text(int cfgDeg, double stockDeg, int haveStock, char* out, int cap) {
  if (!out || cap <= 0) return;
  if (cfgDeg != FOV_STOCK) _snprintf_s(out, (size_t)cap, _TRUNCATE, "%d", fov_slider_value(cfgDeg, stockDeg));
  else if (!haveStock) _snprintf_s(out, (size_t)cap, _TRUNCATE, "--");
  else if (stockDeg < 99.95) _snprintf_s(out, (size_t)cap, _TRUNCATE, "%.1f", stockDeg);
  else _snprintf_s(out, (size_t)cap, _TRUNCATE, "%.0f", stockDeg);
}

// What the row saves: the reset's 0 as it is, anything else inside the slider's range.
inline int fov_cfg_from_slider(int v) {
  if (v == FOV_STOCK) return FOV_STOCK;
  return v < FOV_MIN_DEG ? FOV_MIN_DEG : v > FOV_MAX_DEG ? FOV_MAX_DEG : v;
}

// The modifier's target: wanted degrees over the game's DefaultFOV (CalculateFOV multiplies its FOV by it), so a
// changed game FOV option is followed on the next apply. 0 when DefaultFOV is outside CalculateFOV's [1, 179].
inline float fov_ratio(double wantDeg, double defaultDeg) {
  if (!(defaultDeg > 1.0 && defaultDeg < 179.0) || !(wantDeg > 0.0)) return 0.f;
  return (float)(wantDeg / defaultDeg);
}

// The 'ConsoleFOV' modifier as one value for optrestore.h: its target while active (HasScaleModifier), else
// FOV_ABSENT, which the release turns into ClearScaleModifier.
const float FOV_ABSENT = 0.f;
inline float fov_current(int active, float target) { return active ? target : FOV_ABSENT; }

// ConsoleFOV is saved with the pawn (Actor.ScaleModifierGroupList is not transient), and its only other writer is
// the cheat-gated FOV exec: a target found in place is ours from before a save, never the game's. So the value off
// goes back to is always FOV_ABSENT, a ClearScaleModifier.
inline OptRestore fov_apply(OptRestore r, unsigned level, float current, float ours) {
  r = optrestore_apply(r, level, current, ours);
  r.saved = FOV_ABSENT;
  return r;
}
// Off at a level start (its first check) with a ConsoleFOV active: a save or checkpoint carried ours; cleared once.
inline int fov_clear_carried(int on, int firstCheckThisLevel, int active) {
  return !on && firstCheckThisLevel && active;
}

// The modifier's state is read (two script calls) on a change, and every FOV_CHECK_FRAMES otherwise: a level's
// TriggerPlayer can clear FOV modifiers it names, and ours is then put back.
const unsigned FOV_CHECK_FRAMES = 30;

// Game thread, once a frame after world_frame.
void fov_frame(void);
// The game's DefaultFOV for the readout and the reset; false until a player has been read.
bool fov_stock(double* deg);
// Null while the path is usable (or not yet checked); else why the row is unavailable.
const char* fov_refused(void);

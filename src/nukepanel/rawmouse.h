#pragma once
// Raw mouse input (Settings > INPUT, qol.md 2): on TriggerViewRotationAdjust, Pitch/Yaw become the DirectInput counts
// since the last event x 0.5 x 7.68 x MouseSensitivity x CurrentFOV/90, the engine's gain below its 26214 counts/s cap,
// with no cap, curve or 1/30 s clamp, fractions carried. Keyboard, gamepad and aim assist turning is replaced.

struct RawCarry { double pitch, yaw; };
// UpdateRotation's `FLD double [0x104CC010]` (at 0x1026ACF9): yaw = trunc(aTurn x dt x 7.68 + adjust).
static const double RAW_ROTATION_GAIN = 7.68;
struct RawIn {
  int dx, dy;          // DIMOUSESTATE lX/lY summed since the last event
  float sens, fov;     // PlayerPawn.MouseSensitivity, CurrentFOV
  int invert, mirror;  // bInvertMouse (flips pitch), LevelInfo.MirrorModeEnabled (flips yaw), as PlayerInput does
  int lockPitch, lockYaw;   // ViewRotationMode 2 / 1: UpdateRotation leaves that axis 0
  int blocked;         // the panel or the console has the mouse: no turn, nothing carried
};

inline RawCarry raw_carry_init(void) {
  RawCarry c = { 0.0, 0.0 };
  return c;
}

// The axis' rotator units this event, the remainder carried toward zero-truncation's lost fraction.
inline int raw_axis(double* carry, double units) {
  const double v = *carry + units;
  const int out = (int)v;   // truncation toward zero; the rest stays in the carry, so nothing is lost
  *carry = v - (double)out;
  return out;
}

inline void raw_turn(RawCarry* c, const RawIn& in, int* pitch, int* yaw) {
  if (in.blocked) {
    *c = raw_carry_init();
    *pitch = *yaw = 0;
    return;
  }
  const double k = 0.5 * RAW_ROTATION_GAIN * (double)in.sens * (double)in.fov / 90.0;
  if (in.lockYaw) { c->yaw = 0.0; *yaw = 0; }
  else *yaw = raw_axis(&c->yaw, (double)in.dx * k * (in.mirror ? -1.0 : 1.0));
  // DirectInput's lY grows toward the player (mouse moved down): looking down, a negative pitch.
  if (in.lockPitch) { c->pitch = 0.0; *pitch = 0; }
  else *pitch = raw_axis(&c->pitch, -(double)in.dy * k * (in.invert ? -1.0 : 1.0));
}

// Game thread, once a frame after input_frame: resolves the player's fields per level, drops counts no event took.
void rawmouse_frame(int inLevel);
// Null while usable; else why the row is unavailable.
const char* rawmouse_refused(void);

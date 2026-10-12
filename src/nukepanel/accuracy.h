#pragma once
// Perfect accuracy (Weapons > PERFECT ACCURACY): the four spreads Weapon.GetShotError reads, zeroed on the class
// default of the WeaponConfig of each weapon the player holds, by GlobalSetProperty(.., bAllInstances 0,
// bNoSave 1); AI weapons use their own config classes (docs/re/player-options.md "Perfect accuracy").
#include <stdio.h>
#include <wchar.h>

enum { ACC_PROPS = 4, ACC_MAX_CLASSES = 32 };
const wchar_t* const ACC_PROP_NAMES[ACC_PROPS] = { L"HorizShotError", L"VertShotError", L"ShrunkHorizShotError",
                                                   L"ShrunkVertShotError" };

// The text ImportText reads back to the same float: nine significant digits round-trip any float.
inline void acc_value_text(float v, wchar_t* out, int cap) {
  if (!out || cap <= 0) return;
  _snwprintf_s(out, (size_t)cap, _TRUNCATE, L"%.9g", (double)v);
}

// Class defaults outlive a level, so a record is kept per class rather than per level: across a level change only
// while its class is still the same object (in the table, the same name); a reloaded class starts a new record.
inline int acc_keep_record(int clsValid, int nameNow, int nameRec) { return clsValid && nameNow == nameRec; }

// No recoil (Weapons > PERFECT ACCURACY, second row): ShakeMagnitude of each WeaponConfig.ViewKicks[i] on the held
// weapon's config class default, zeroed by GlobalSetProperty(.., bNoSave 1) through the ShakeMagnitude IntProperty,
// whose ImportText writes only the address it is given (docs/re/accuracy.md "No recoil").
enum { RK_MAX_KICKS = 8 };
inline int rk_kick_count(int n) { return n < 0 ? 0 : n > RK_MAX_KICKS ? RK_MAX_KICKS : n; }
// GlobalSetProperty writes at default + offset in 32-bit arithmetic, and the kicks live in the array's own block:
// the offset that lands on kick i's field, wrapping when the block sits below the default.
inline int rk_field_offset(unsigned cdo, unsigned data, int i, int elemSize, int fieldOfs) {
  return (int)(data + (unsigned)i * (unsigned)elemSize + (unsigned)fieldOfs - cdo);
}
// The restore text of an int magnitude.
inline void rk_int_text(int v, wchar_t* out, int cap) {
  if (!out || cap <= 0) return;
  _snwprintf_s(out, (size_t)cap, _TRUNCATE, L"%d", v);
}
// A record's values are logged once per level.
inline int acc_log_due(unsigned loggedLevel, unsigned level) { return loggedLevel != level; }
// The record slot for the held weapon's config class: the existing one of the same (class, name), else a new one
// while room is left (*added = 1; the caller fills it); -1 for no held class or a full table.
template <class R>
inline int acc_slot(R* recs, int* n, int cap, const void* held, int heldName, int* added) {
  *added = 0;
  if (!held) return -1;
  for (int i = 0; i < *n; i++)
    if (recs[i].cls == held && recs[i].clsName == heldName) return i;
  if (*n >= cap) return -1;
  recs[*n].cls = held;
  recs[*n].clsName = heldName;
  *added = 1;
  return (*n)++;
}

// Fire probe (accuracy.md): PerformFire ends in PlayerPawn.RecordShot, which adds one to AccuracyShots, so a rise marks
// a shot. The window keeps CurrentShake (the view kick GetWeaponAim adds) at the shot frame and its peak over
// ACC_PROBE_FRAMES frames; a second shot inside it is ignored, a lower count (a new pawn) resets the baseline.
enum { ACC_PROBE_FRAMES = 12 };
struct AccProbe { int haveLast, lastShots, open, frames, firstPitch, firstYaw, peakPitch, peakYaw; };
inline AccProbe acc_probe_init() { AccProbe p = { 0, 0, 0, 0, 0, 0, 0, 0 }; return p; }
// One frame's reading. 1 on the frame the window closes (log it then).
inline int acc_probe_step(AccProbe* p, int shots, int pitch, int yaw) {
  const int shot = p->haveLast && shots > p->lastShots;
  p->haveLast = 1;
  p->lastShots = shots;
  const int ap = pitch < 0 ? -pitch : pitch, ay = yaw < 0 ? -yaw : yaw;
  if (!p->open) {
    if (!shot) return 0;
    p->open = 1;
    p->frames = 1;
    p->firstPitch = pitch;
    p->firstYaw = yaw;
    p->peakPitch = ap;
    p->peakYaw = ay;
  } else {
    p->frames++;
    if (ap > p->peakPitch) p->peakPitch = ap;
    if (ay > p->peakYaw) p->peakYaw = ay;
  }
  if (p->frames < ACC_PROBE_FRAMES) return 0;
  p->open = 0;
  return 1;
}
// Once per class and switch state (accuracy on = 1, + no recoil on = 2): 1 the first time.
inline int acc_probe_claim(unsigned* mask, int state) {
  const unsigned bit = 1u << (state & 3);
  if (*mask & bit) return 0;
  *mask |= bit;
  return 1;
}
// Rotator units to degrees (65536 = 360).
inline double acc_rot_deg(int u) { return u * (360.0 / 65536.0); }

// Game thread, once a frame after world_frame.
void accuracy_frame(void);
// Null while the path is usable (or not yet checked); else why the row is unavailable.
const char* accuracy_refused(void);
// The same for the No recoil row.
const char* recoil_refused(void);

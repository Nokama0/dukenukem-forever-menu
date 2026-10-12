#pragma once
// No screen shake (qol.md 3): every shake is a named PlayerPawn.ActiveShakes entry. Each entry not named *_ViewKick_*
// (weapon recoil is No recoil's) gets Magnitude, FullMagnitude, Limit and Result (ProcessShake's own fields) zeroed at
// TriggerViewRotationAdjust, before UpdateViewShake that tick, and again at Present; switching off restores them.
#include <wchar.h>
#include "cvars.h"   // CVAR_NONE / CVAR_FORCE / CVAR_RESTORE

enum { NS_VALS = 6 };   // Magnitude, FullMagnitude, Limit, Result.Pitch, .Yaw, .Roll
enum { NS_MAX_ENTRIES = 64 };

// A weapon kick, by name: <Gun>_ViewKick_<n> (Weapon.StartWeaponViewKick). Case-insensitive.
inline int ns_is_kick(const wchar_t* name) {
  static const wchar_t kTag[] = L"_viewkick_";
  if (!name) return 0;
  for (int i = 0; name[i] && i < 256; i++) {
    int j = 0;
    while (kTag[j] && name[i + j] && towlower(name[i + j]) == kTag[j]) j++;
    if (!kTag[j]) return 1;
  }
  return 0;
}

inline int ns_zero(const int* v) {
  for (int i = 0; i < NS_VALS; i++)
    if (v[i]) return 0;
  return 1;
}

// One entry's fields, as cvars.h's rule: the game's values kept at the first zeroing and whenever the game writes
// new ones (a re-triggered shake); off, they go back only while our zeros are still in place.
struct ShakeHold { int held; int saved[NS_VALS]; };

inline int ns_step(ShakeHold* h, int force, const int* cur, int* write) {
  if (force) {
    if (!h->held || !ns_zero(cur))
      for (int i = 0; i < NS_VALS; i++) h->saved[i] = cur[i];
    h->held = 1;
    if (ns_zero(cur)) return CVAR_NONE;
    for (int i = 0; i < NS_VALS; i++) write[i] = 0;
    return CVAR_FORCE;
  }
  if (!h->held) return CVAR_NONE;
  h->held = 0;
  if (!ns_zero(cur) || ns_zero(h->saved)) return CVAR_NONE;
  for (int i = 0; i < NS_VALS; i++) write[i] = h->saved[i];
  return CVAR_RESTORE;
}

// Game thread, once a frame: resolves the entry layout per level, zeroes or restores.
void noshake_frame(void);
// Null while usable; else why the row is unavailable.
const char* noshake_refused(void);

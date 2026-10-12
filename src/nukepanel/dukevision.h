#pragma once
// Clean Duke Vision (dukevision.md 5): the scanlines, distortion, tint and vignette are one HUD effect in
// DukeHUD.HUDEffects. Switch on: each Duke Vision effect found there is removed by the game's DukeHUD.RemoveHudEffect;
// the brightening is elsewhere and stays. Off restores nothing: Upgrade_HeatVision adds them again.

// The SHUDEffect names Upgrade_HeatVision adds (dukevision.md section 1): the steady image, the turn-off collapse,
// and the black behind both ramps.
enum { DV_TARGET_COUNT = 3 };
static const wchar_t* const DV_TARGETS[DV_TARGET_COUNT] = { L"DukeVisionHUDEffect", L"DukeVisionHUDEffectRemove",
                                                            L"DukeVisionBlackHack" };
// HUDEffects holds a handful of entries; past this many a pass reads the first DV_MAX_EFFECTS and counts the rest.
enum { DV_MAX_EFFECTS = 64 };
// RemoveHudEffect's parms are one name (4 bytes); a larger block means a layout we do not know.
enum { DV_PARMS_MAX = 64 };

// One call per entry whose name is a target (RemoveHudEffect may remove one per call), in array order, at most
// cap. NAME_None (0) never matches, so a target the name table lacks matches nothing.
inline int dv_plan(const int* names, int count, const int* targets, int* out, int cap) {
  int n = 0;
  for (int i = 0; names && i < count && n < cap; i++) {
    if (!names[i]) continue;
    for (int t = 0; t < DV_TARGET_COUNT; t++)
      if (targets && targets[t] == names[i]) { out[n++] = names[i]; break; }
  }
  return n;
}

// What reflection gave for one DukeHUD class; dv_refusal names the first part missing.
struct DvResolve { int myHud, effects, effectsOfStruct, nameField, removeFn, effectNameParm, parmsSize; };

inline const char* dv_refusal(const DvResolve& r) {
  if (!r.myHud) return "PlayerPawn.MyHUD did not resolve";
  if (!r.effects) return "DukeHUD.HUDEffects did not resolve";
  if (!r.effectsOfStruct) return "HUDEffects is not an array of a struct";
  if (!r.nameField) return "SHUDEffect.Name did not resolve";
  if (!r.removeFn) return "DukeHUD.RemoveHudEffect did not resolve";
  if (!r.effectNameParm) return "RemoveHudEffect's EffectName parameter did not resolve";
  if (r.parmsSize <= 0 || r.parmsSize > DV_PARMS_MAX) return "RemoveHudEffect's parameters are not one name";
  return 0;
}

// The removal runs only when asked, resolved, and not refused outside standalone play (mpgate.h).
inline int dv_on(int want, const char* ownRefusal, const char* mpRefusal) { return want && !ownRefusal && !mpRefusal; }

// Game thread, once a frame, before pehook_frame: inLevel is world_frame's result (0 while paused or loading).
void dukevision_frame(int inLevel);
// Null while usable; else why the row is unavailable.
const char* dukevision_refused(void);

#pragma once
// Post-processing switches (View > POST PROCESSING, docs/re/qol.md section 4): one per effect, each driving one or
// two console variables registered with flags 0 (never saved), forced to the row's values while its switch is off.
// A switch on shows the effect: the game's own values, given back when the switch turns on again.
#include "config.h"
#include "cvars.h"

enum { PFX_BLUR, PFX_MOTIONBLUR, PFX_PIXELMOTIONBLUR, PFX_DOF, PFX_BLOOM, PFX_TONEMAP, PFX_UNDERWATER,
       PFX_FILMGRAIN, PFX_PINKOVISION, PFX_COUNT };
// The game's own option a row depends on (UClient config, set from the game's Video menu only).
enum { PFX_GAME_NONE, PFX_GAME_PIXELMOTIONBLUR, PFX_GAME_FILMGRAIN };
enum { PFX_MAX_VARS = 2 };

// One variable and the dword the row forces it to (a FLOAT's bits when isFloat).
struct PfxVar { const wchar_t* cvar; int isFloat; int forced; };
struct PostFx { const char* label; const char* hint; int Config::* field; int game; int nvars; PfxVar var[PFX_MAX_VARS]; };

// The table: qol.md section 4. Bloom, DOF and grading use the uber pass's levers (Gate 2026-10-04): shipped
// r_TestUberPost 1 skips the passes r_DrawDOF and r_UseToneMapping gate, and the uber bloom ignores r_DrawBlooms.
inline const PostFx& postfx_info(int i) {
  static const PostFx kFx[PFX_COUNT] = {
    { "Screen blur", "Only shows during drunk, pain, underwater and nuke effects: the full-screen blur.",
      &Config::pfxBlur, PFX_GAME_NONE, 1, { { L"r_DrawBlur", 0, 0 } } },
    { "Motion blur", "Only shows during drunk, pain, underwater and nuke effects: the ghosting trail.",
      &Config::pfxMotionBlur, PFX_GAME_NONE, 1, { { L"r_DrawMotionBlur", 0, 0 } } },
    { "Per-pixel motion blur", "Blur from camera movement; needs the game's own Motion blur option.",
      &Config::pfxPixelMotionBlur, PFX_GAME_PIXELMOTIONBLUR, 1, { { L"r_DrawPixelMotionBlur", 0, 0 } } },
    // r_TestDOF makes the uber pass take the DOF values from the variables; a blur amount of 0 leaves it sharp.
    { "Depth of field", "Distance blur in some areas and with vision modes; needs the game's post effects option.",
      &Config::pfxDof, PFX_GAME_NONE, 2, { { L"r_TestDOF", 0, 1 }, { L"r_DOFBlurAmount", 1, 0 } } },
    // Zero brightness into the uber bright pass adds nothing; r_DrawBlooms 0 still removes the star glints.
    { "Bloom", "Glow around bright lights and star glints; needs the game's post effects option.",
      &Config::pfxBloom, PFX_GAME_NONE, 2, { { L"r_BloomBrightnessScale", 1, 0 }, { L"r_DrawBlooms", 0, 0 } } },
    // r_TestToneMapping grades from the scene variables, whose defaults are the neutral grade.
    { "Colour grading", "The per-area colour and desaturation; needs the game's post effects option.",
      &Config::pfxToneMap, PFX_GAME_NONE, 1, { { L"r_TestToneMapping", 0, 1 } } },
    { "Underwater overlay", "The overlay drawn while the view is inside water or another volume.",
      &Config::pfxUnderWater, PFX_GAME_NONE, 1, { { L"r_DrawUnderWater", 0, 0 } } },
    { "Film grain", "Grain and vignette; needs the game's own Film grain option.",
      &Config::pfxFilmGrain, PFX_GAME_FILMGRAIN, 1, { { L"r_AllowFilmgrain", 0, 0 } } },
    { "Pink-o-vision", "The pink screen effect.", &Config::pfxPinkOVision, PFX_GAME_NONE, 1,
      { { L"r_DrawPinkOVision", 0, 0 } } },
  };
  return kFx[i < 0 ? 0 : i >= PFX_COUNT ? PFX_COUNT - 1 : i];
}

// Whether a row with a game option is moot: the option is known to be off, or its variable already reads 0 and
// the 0 is not ours (held). Either way the effect is never drawn, so the row greys out.
inline int pfx_game_off(int optKnown, int optOn, int cvarNow, int held) {
  if (optKnown && !optOn) return 1;
  return cvarNow == 0 && !held;
}

// Game thread, once a frame: finds the variables once, then forces or releases each one.
void postfx_frame(void);
// A photo capturing (photo.cpp): every screen-space effect is forced off as if its switch were, so tiles join without
// seams (freecam.md section 5); colour grading stays, the exposure being frozen by the engine itself.
void postfx_set_photo(int on);
inline int pfx_photo_forces(int i) { return i != PFX_TONEMAP; }
// Null while the row is usable; else why it is unavailable (a variable missing, or the game's option off).
const char* postfx_refused(int i);

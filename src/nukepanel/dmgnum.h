#pragma once
// Damage numbers: the ring and the animation rules, nothing else (ported from duke-dnmp's dmgnum.h, itself AA's).
// Here the events come from the health-delta tracker (dmgtrack.h), not a damage hook. dmgnum.cpp owns the
// engine reads and the draw.
#include <math.h>
#include "canvas.h"     // C_HAZARD, C_RED
#include "dmgtrack.h"   // DmgPt3

// 32 events, oldest overwritten: the draw cost stays fixed whatever the fire rate.
enum { DMG_MAX = 32 };
// Ages are unsigned `now - t0` differences, exact across the millisecond clock's wrap.
const unsigned DMG_LIFE_MS = 750;
// Twice the canvas's text size. The layout (rise, stack, keyline) is scaled by it through the
// canvas transform; the glyphs come from an atlas rasterised at the drawn size (dmg_text_path).
const double DMG_TEXT_SCALE = 2.0;

// `at` is where the victim's box centre was when it was hit; `victim` only groups a stack and is never
// dereferenced. `kill`/`live` are int, not bool: AA lost a bool member of this shape to MSVC /O2.
struct DmgEvent { DmgPt3 at; int damage; int kill; const void* victim; unsigned t0; int live; };
struct DmgRing  { DmgEvent e[DMG_MAX]; int next; };

inline void dmg_reset(DmgRing* r) {
  if (!r) return;
  for (int i = 0; i < DMG_MAX; ++i) {
    DmgEvent& e = r->e[i];
    e.at.x = e.at.y = e.at.z = 0.f;
    e.damage = 0; e.kill = 0; e.victim = 0; e.t0 = 0u; e.live = 0;
  }
  r->next = 0;
}

// A burst past DMG_MAX inside one lifetime keeps the newest numbers, the ones worth seeing.
inline void dmg_push(DmgRing* r, const DmgEvent& ev) {
  if (!r) return;
  r->e[r->next] = ev;
  r->e[r->next].live = 1;
  r->next = (r->next + 1) % DMG_MAX;
}

inline int dmg_is_live(const DmgEvent& ev, unsigned now) {
  if (!ev.live) return 0;
  return (unsigned)(now - ev.t0) < DMG_LIFE_MS ? 1 : 0;
}

inline double dmg_progress(unsigned t0, unsigned now) {
  const unsigned age = now - t0;
  if (age >= DMG_LIFE_MS) return 1.0;
  return (double)age / (double)DMG_LIFE_MS;
}

// Ease-out 1-(1-p)^2 over one text line: knocked loose, then settling.
inline int dmg_rise_px(double p, int textH) {
  if (p < 0.0) p = 0.0;
  if (p > 1.0) p = 1.0;
  const double q = 1.0 - (1.0 - p) * (1.0 - p);
  return (int)floor(q * (double)textH + 0.5);
}

// Opaque for two thirds, then linear to nothing, so the number leaves without a pop.
inline int dmg_alpha(double p) {
  if (p <= 2.0 / 3.0) return 255;
  if (p >= 1.0) return 0;
  return (int)floor(255.0 * (1.0 - p) * 3.0 + 0.5);
}

inline unsigned dmg_colour(int kill) { return kill ? C_RED : C_HAZARD; }

// The drawn size in percent of the 1x font: the UI scale the numbers draw at times DMG_TEXT_SCALE.
inline int dmg_drawn_pct(int uiPct) { return (int)floor((double)ui_scale_clamped(uiPct) * DMG_TEXT_SCALE + 0.5); }
// The pct the numbers' own atlas is rasterised at (canvas_push_text_scale): their drawn size, clamped to what
// fontgen rasterises (UI_SCALE_MIN..UI_SCALE_MAX).
inline int dmg_atlas_pct(int uiPct) { return ui_scale_clamped(dmg_drawn_pct(uiPct)); }
// Which glyphs draw the numbers (2026-10-09: the baked 1x ones scaled 2x read pixelated): the atlas rasterised at
// the drawn size when it was built and is that size, else the baked glyphs through the canvas transform.
enum { DMG_TEXT_BAKED = 0, DMG_TEXT_ATLAS = 1 };
inline int dmg_text_path(int uiPct, bool atlasOk) {
  return atlasOk && dmg_atlas_pct(uiPct) == dmg_drawn_pct(uiPct) ? DMG_TEXT_ATLAS : DMG_TEXT_BAKED;
}

// Live events on the same victim pushed before `idx`, walking from the oldest slot: times one text height, it
// turns a burst into a column instead of one smudge.
inline int dmg_stack_index(const DmgRing& r, int idx, unsigned now) {
  if (idx < 0 || idx >= DMG_MAX) return 0;
  if (!dmg_is_live(r.e[idx], now)) return 0;
  int n = 0;
  for (int k = 0; k < DMG_MAX; ++k) {
    const int s = (r.next + k) % DMG_MAX;
    if (s == idx) break;
    if (dmg_is_live(r.e[s], now) && r.e[s].victim == r.e[idx].victim) n++;
  }
  return n;
}

// Main thread, frame path (frame.cpp): steps the tracker from this frame's enemy pawns, then draws the live numbers
// after the ESP and before the panel. Nothing happens, and the tracker resets, while Damage numbers is off or no
// level is up.
void dmgnum_frame(int W, int H);

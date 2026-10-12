// Ported from duke3d-eduke32-aa/src/ekmod/ui/panel_layout.h (itself Megaton's)
// and kept identical but for PANEL_MIN_W and the height cap (below).
#pragma once
#include "ui.h"
// The panel's layout and drag rules, pure and header-only. PANEL_W/H/INSET/VINSET live here so panel_layout reproduces
// the default rect exactly; menu.cpp includes them. PANEL_W and PANEL_MIN_W grew by 48 with the tab rail (RAIL_W 116 ->
// 164, for "Achievements"), keeping the content column at the width the rules below were measured at.
const int PANEL_W = 668, PANEL_H = 900, PANEL_INSET = 60, PANEL_VINSET = 30;
// PANEL_MIN_H is 790, the panel resizes like the sibling ports' and tabs scroll, so no row depends on it. PANEL_MIN_W =
// 560 is the narrowest panel where the slider track (at TRACK_MIN_W) clears the widest label ("Double jump strength" as
// a sub-row) by TRACK_GAP. At 540 the old track overlapped it by 3px; the uniform readout column needs 20 more.
const int PANEL_MIN_W = 608;
const int PANEL_MIN_H = 790;
const int PANEL_GRIP = 16;
struct PanelBox { int x, y, w, h; };
enum { PDRAG_IDLE = 0, PDRAG_MOVE = 1, PDRAG_RESIZE = 2 };
struct PanelDrag { int phase; int grabDx, grabDy; PanelBox start; };

// Default box for a screen this size: PANEL_INSET anchors the left edge and
// width; the vertical clamp takes PANEL_VINSET on a short screen.
inline PanelBox panel_default_box(int screenW, int screenH) {
  int w = PANEL_W, h = PANEL_H;
  if (h > screenH - 2 * PANEL_VINSET) h = screenH - 2 * PANEL_VINSET;
  if (w > screenW - 2 * PANEL_INSET) w = screenW - 2 * PANEL_INSET;
  PanelBox b = { PANEL_INSET, (screenH - h) / 2, w, h };
  return b;
}
inline int panel_clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
// Where the panel draws: a stored box with no size (w or h <= 0, "unset") falls back to the default box for this
// screen, keeping a stored x/y if >= 0; then clamped, width/height first to [min(PANEL_MIN, max), max], then x/y. The
// height's max is the screen or `hCap`, whichever is lower, applied before x/y so a capped panel is not pinned to y=0.
inline PanelBox panel_layout(PanelBox stored, int screenW, int screenH, int hCap = 0x7fffffff) {
  const int maxH = hCap < screenH ? hCap : screenH;
  if (stored.w <= 0 || stored.h <= 0) {
    PanelBox d = panel_default_box(screenW, screenH);
    if (d.h > maxH) { d.h = maxH; d.y = (screenH - d.h) / 2; }
    if (stored.x >= 0) d.x = stored.x;
    if (stored.y >= 0) d.y = stored.y;
    stored = d;
  }
  PanelBox b = stored;
  const int maxW = screenW;
  b.w = panel_clamp(b.w, PANEL_MIN_W < maxW ? PANEL_MIN_W : maxW, maxW);
  b.h = panel_clamp(b.h, PANEL_MIN_H < maxH ? PANEL_MIN_H : maxH, maxH);
  b.x = panel_clamp(b.x, 0, screenW - b.w);
  b.y = panel_clamp(b.y, 0, screenH - b.h);
  return b;
}
inline UiRect panel_grip_rect(PanelBox b) { UiRect r = { b.x + b.w - PANEL_GRIP, b.y + b.h - PANEL_GRIP, PANEL_GRIP, PANEL_GRIP }; return r; }
inline UiRect panel_title_rect(PanelBox b, int stripH, int statusH) { UiRect r = { b.x, b.y, b.w, 1 + stripH + statusH }; return r; }
// Drag state machine: idle until a press lands on the grip (-> resize) or title band (-> move), follows the mouse until
// the button comes up, then reports idle and sets *ended once (a caller polling every frame acts on the release exactly
// once). *box always goes through panel_layout, so a drag never produces a box the layout rule would itself refuse.
inline PanelDrag panel_drag_step(PanelDrag d, bool pressed, bool down, bool titleHit, bool gripHit,
                                 int mx, int my, PanelBox current, int screenW, int screenH,
                                 PanelBox* box, bool* ended, int hCap = 0x7fffffff) {
  *ended = false; *box = current;
  if (d.phase == PDRAG_IDLE) {
    if (pressed && gripHit) { d.phase = PDRAG_RESIZE; d.start = current; d.grabDx = mx - (current.x + current.w); d.grabDy = my - (current.y + current.h); }
    else if (pressed && titleHit) { d.phase = PDRAG_MOVE; d.start = current; d.grabDx = mx - current.x; d.grabDy = my - current.y; }
    return d;
  }
  if (!down) { d.phase = PDRAG_IDLE; *ended = true; return d; }
  PanelBox b = d.start;
  if (d.phase == PDRAG_MOVE) { b.x = mx - d.grabDx; b.y = my - d.grabDy; }
  else {
    b.w = (mx - d.grabDx) - b.x; b.h = (my - d.grabDy) - b.y;
    // Floored at 1, not zero or negative: panel_layout reads w/h <= 0 as "no stored box, use the default", so a resize
    // dragged past the minimum stops at the minimum instead of jumping to the default box. The floor of 1 changes
    // nothing else: it is below PANEL_MIN_W/H, which panel_layout's own clamp raises anyway.
    if (b.w < 1) b.w = 1;
    if (b.h < 1) b.h = 1;
  }
  *box = panel_layout(b, screenW, screenH, hCap);
  return d;
}

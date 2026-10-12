// Verbatim from Megaton's src/duke3dmod/tooltip.h; kept identical on purpose -- do not edit here.
#pragma once
#include "ui.h"
#include "anim.h"

// Hints as tooltips. A hint is shown beside the cursor once it has RESTED on one row for TOOLTIP_DELAY_MS, hides the
// instant it leaves, and is put away by a press until the cursor leaves and returns. Pure: the panel feeds it the
// hovered row's key each frame and draws what it answers.
struct TooltipState { int rowKey; unsigned restSinceMs; bool shown; bool pressedHide; double alpha; };
// The rest delay is 150 ms: 400 was long enough that a player moving deliberately along a column of rows outran it and
// saw nothing, and 150 is still far longer than a cursor takes to cross a row elsewhere, the only thing the delay
// filters out. Nothing else is timed against it (see menu.cpp's menu_tooltip_note_close).
const unsigned TOOLTIP_DELAY_MS = 150;
const int TOOLTIP_PAD = 8, TOOLTIP_DX = 16, TOOLTIP_DY = 20;

inline TooltipState tooltip_begin() {
  TooltipState s; s.rowKey = -1; s.restSinceMs = 0; s.shown = false; s.pressedHide = false; s.alpha = 0.0;
  return s;
}

// noinline: MSVC 19.50.35728 x86 /O2 constant-folds a chain of calls with literal arguments and loses a byte-sized bool
// member of the struct returned by value; the DLL (runtime values) was never at risk, and int flags, as in entry.h,
// also fix it. `dtMs` IS MILLISECONDS like every clock here (once dtSeconds times 1000, so the fade never ran).
__declspec(noinline) inline TooltipState tooltip_step(TooltipState s, int hoveredRowKey, bool pressed, unsigned nowMs,
                                 int animStyle, double dtMs) {
  if (hoveredRowKey != s.rowKey) {          // arrived, left, or moved to another row
    s.rowKey = hoveredRowKey; s.restSinceMs = nowMs; s.shown = false; s.pressedHide = false; s.alpha = 0.0;
    return s;
  }
  if (pressed) { s.pressedHide = true; s.shown = false; s.alpha = 0.0; return s; }
  if (s.rowKey < 0 || s.pressedHide) { s.shown = false; s.alpha = 0.0; return s; }
  if (!s.shown && nowMs - s.restSinceMs >= TOOLTIP_DELAY_MS) { s.shown = true; s.alpha = 0.0; }
  if (s.shown) {
    if (animStyle == ANIM_OFF) s.alpha = 1.0;
    else {
      s.alpha += dtMs / (double)ANIM_HOVER_IN_MS;
      if (s.alpha > 1.0) s.alpha = 1.0;
    }
  }
  return s;
}

// The box beside the cursor: below and to the right by default, flipped to the
// left of the cursor when it would cross the right edge and above it when it
// would cross the bottom, then clamped to a non-negative origin. Never narrowed.
inline UiRect tooltip_box_place(int mx, int my, int textW, int textH, int screenW, int screenH) {
  UiRect b; b.w = textW + 2 * TOOLTIP_PAD; b.h = textH + 2 * TOOLTIP_PAD;
  b.x = mx + TOOLTIP_DX; b.y = my + TOOLTIP_DY;
  if (b.x + b.w > screenW) b.x = mx - TOOLTIP_DX - b.w;
  if (b.y + b.h > screenH) b.y = my - TOOLTIP_DY - b.h;
  if (b.x < 0) b.x = 0;
  if (b.y < 0) b.y = 0;
  return b;
}

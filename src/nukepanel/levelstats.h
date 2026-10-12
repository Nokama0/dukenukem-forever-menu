#pragma once
// Level stats: this chapter's ego items found/total, top left, in duke-dnmp's
// outlined corner look. Pure rules for the suite; levelstats.cpp draws from progress.h's last read.
#include <stdio.h>

enum { LS_MARGIN = 8, LS_LINE_CAP = 40 };

// "Ego items 2/5" over the table's count; "Ego items -" for a chapter with none, as the Progress tab shows it.
inline void ls_ego_line(char* out, int cap, int found, int total) {
  if (total <= 0) _snprintf_s(out, (size_t)cap, _TRUNCATE, "Ego items -");
  else _snprintf_s(out, (size_t)cap, _TRUNCATE, "Ego items %d/%d", found, total);
}

// Shown with the switch on, in a level whose map is a chapter, with the panel shut (its closing slide included).
// inLevel is world_frame's answer, world_gate's: 0 while paused, which is how the pause menu and every game menu
// over a level show (the ESP's own gate).
inline bool ls_visible(int switchOn, int inLevel, int chapter, int panel) {
  return switchOn && inLevel && chapter >= 0 && !panel;
}

// Frame path, after the ESP and before the panel. inLevel: world_frame's answer this frame.
void levelstats_draw(int W, int H, int inLevel);

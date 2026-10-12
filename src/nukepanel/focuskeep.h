#pragma once
// Keep running when alt-tabbed (Settings > DISPLAY, docs/re/qol.md section 1): the engine's own g_IgnoreFocusChanges
// (what -IGNOREFOCUSCHANGES sets) keeps the focus-loss pause from running, and the audio master volume the focus loss
// zeroes is put back to 1.0. The mouse release and the input stop on focus loss stay the engine's.
#include "cvars.h"

// The flag changes only while the game is in front: flipped between a focus-loss pause and its resume, the resume
// would be skipped and the game would stay paused (qol.md lever a). In front it is 1 while the option is on, and
// the engine's own value (0, or 1 from the command line) when off.
inline int focus_flag_step(CvarHold* h, int on, int front, int current, int* write) {
  if (!front) return CVAR_NONE;
  return cvar_step(h, on, current, 1, write);
}

// Game thread, once a frame after input_frame: applies or releases the flag, resolves the audio subsystem per level.
void focuskeep_frame(void);
// The WndProc, after the engine handled WM_ACTIVATE(inactive) or WM_ACTIVATEAPP(0): the volume back to 1.0 while
// the flag is ours.
void focuskeep_after_focus_loss(void);
// Null while usable; else why the row is unavailable.
const char* focuskeep_refused(void);

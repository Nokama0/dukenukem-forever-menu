#pragma once
// Whether the panel key may open the panel: not while nothing can be drawn, because an open panel swallows the
// game's input and nobody could see it to close it.
#include <string.h>

// What stops drawing, as the name of the call that failed; NULL when the panel can draw. primFailed is prim's
// session-long failure, resetFailed a failed Reset (drawing paused until one succeeds).
inline const char* drawgate_reason(const char* primFailed, bool resetFailed) {
  if (primFailed) return primFailed;
  return resetFailed ? "IDirect3DDevice9::Reset" : 0;
}

// The reason last logged, so each one is logged once per episode however often the key is pressed.
struct DrawGate { const char* logged; };

inline DrawGate drawgate_init(void) { DrawGate g = {0}; return g; }

// Each time the reason is published (every frame, and after a failed Reset). Drawing back ends the episode, so a
// later failure is logged again. True when an open panel must close: it could no longer be seen.
inline bool drawgate_set(DrawGate* g, const char* reason, bool open) {
  if (!reason) g->logged = 0;
  return reason && open;
}

// The panel key: true when the panel may open; *logNow is true when this episode has not logged the reason yet.
inline bool drawgate_may_open(DrawGate* g, const char* reason, bool* logNow) {
  *logNow = false;
  if (!reason) return true;
  if (!g->logged || strcmp(g->logged, reason) != 0) {
    g->logged = reason;
    *logNow = true;
  }
  return false;
}

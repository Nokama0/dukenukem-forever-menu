#pragma once
// Frame-time window and hitch counters, pure. frame.cpp closes a window (logs it, then fs_reset) every 600
// frames; fs_ready (one second of frames) is kept from duke-dnmp for a time-based window.
struct FrameStats { double acc; int n; double last_avg_ms; double min_ms, max_ms; };

inline void fs_reset(FrameStats* s) {
  s->acc = 0.0; s->n = 0; s->last_avg_ms = 0.0; s->min_ms = 1e30; s->max_ms = 0.0;
}
inline int fs_ready(const FrameStats* s) { return s->acc >= 1.0; }
inline void fs_add(FrameStats* s, double dt_s) {
  const double ms = dt_s * 1000.0;
  s->acc += dt_s; s->n++;
  if (ms < s->min_ms) s->min_ms = ms;
  if (ms > s->max_ms) s->max_ms = ms;
  if (s->acc >= 1.0) s->last_avg_ms = s->acc * 1000.0 / s->n;
}
// The window's mean frame time so far, whatever its length; 0 for an empty window.
inline double fs_avg_ms(const FrameStats* s) { return s->n > 0 ? s->acc * 1000.0 / s->n : 0.0; }

// Hitch counters: a frame over 100 ms is logged (frame.cpp) and counted; over 500 ms also counts as a long one.
struct HitchStats { unsigned over100, over500; };
inline int fs_hitch_note(HitchStats* h, double dt_ms) {
  if (dt_ms <= 100.0) return 0;
  h->over100++;
  if (dt_ms > 500.0) h->over500++;
  return 1;
}

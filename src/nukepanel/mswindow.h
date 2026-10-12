#pragma once
// A cost column for the 600-frame log lines: milliseconds summed and maxed over the window (the instruments rule:
// a crowded level answers "what does the panel cost" from the log).
struct MsWindow { double sum, max; unsigned n; };

inline void ms_window_add(MsWindow* w, double ms) {
  w->sum += ms;
  if (ms > w->max) w->max = ms;
  w->n++;
}
inline double ms_window_avg(const MsWindow& w) { return w.n ? w.sum / (double)w.n : 0.0; }
inline void ms_window_reset(MsWindow* w) { w->sum = w->max = 0.0; w->n = 0; }

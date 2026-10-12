#pragma once
// Frame-pacing rules, in QueryPerformanceCounter ticks (the suite uses its own units). The rules above the
// glue section are pure; the glue's declarations follow them.

// Next deadline: period <= 0 means no cap (returns now). Otherwise steps by
// one period; if now has fallen more than a whole period behind (a stall),
// resyncs to now + period instead of bursting to catch up.
inline long long fc_next_deadline(long long prevDeadline, long long now, long long period) {
  if (period <= 0) return now;
  long long next = prevDeadline + period;
  if (now - next > period) next = now + period;
  return next;
}

// Ticks left to wait before `deadline`, 0 once it has passed.
inline long long fc_wait_ticks(long long deadline, long long now) {
  return deadline > now ? deadline - now : 0;
}

// Effective cap fps from the stored config value and the queried monitor Hz
// (0 if unknown). An explicit stored value in [30,240] always wins;
// otherwise follows the monitor when known, else falls back to 60.
inline int fc_effective_fps(int stored, int monitorHz) {
  if (stored >= 30 && stored <= 240) return stored;
  if (monitorHz >= 30 && monitorHz <= 240) return monitorHz;
  return 60;
}

// The Cap slider's hi and the fps actually used, given vsync: a device that waits for the vblank cannot exceed the
// monitor refresh. hi stays 240 when vsync is off or the refresh is unknown; the fallback (fc_effective_fps's 60) is
// never above a known refresh.
struct FcCapClamp { int sliderHi; int effectiveFps; };
inline FcCapClamp fc_cap_clamp(int stored, int refreshHz, int vsyncOn) {
  const bool capped = vsyncOn != 0 && refreshHz >= 30 && refreshHz <= 240;
  FcCapClamp out;
  out.sliderHi = capped ? refreshHz : 240;
  out.effectiveFps = fc_effective_fps(stored, refreshHz);
  if (capped && out.effectiveFps > refreshHz) out.effectiveFps = refreshHz;
  return out;
}

// The glue (framecap.cpp): monitor-refresh query/cache and the wait after the real Present. Declared here so
// callers need only this header.
#include <windows.h>
// The cap in effect: resolves and caches the monitor Hz once `wnd` is known, then applies fc_cap_clamp against
// the stored value and the device's present interval. Never writes g_cfg -- the UI and the pacing both call
// this instead of reading the stored value.
int framecap_effective_fps(HWND wnd);

// Same resolve, plus the Cap slider's hi and whether vsync is the reason the fps is capped (for the hint row).
struct FcCapInfo { int sliderHi; int effectiveFps; int refreshHz; bool vsyncCapped; };
FcCapInfo framecap_cap_info(HWND wnd);

// Waits out the rest of this frame's period; call right after the real Present, on the presenting thread.
void framecap_wait(HWND wnd);

// Drops the cached monitor Hz: a Reset may have changed the display mode.
void framecap_forget_monitor(void);

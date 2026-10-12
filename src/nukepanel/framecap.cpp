// Frame-cap glue: monitor-refresh resolve and the wait after the real Present (rules in framecap.h). Ported from
// DNMP; the wait runs in frame_after_present because the game's window, CreateDevice and every Present share one
// thread (docs/re/engine-map.md section 5), so sleeping there paces the game tick.
#include "framecap.h"
#include "config.h"
#include "d3dcap.h"
#include "log.h"

// Refresh rate (Hz) of the monitor holding `wnd`, 0 if any step fails.
static int monitor_refresh_hz(HWND wnd) {
  if (!wnd) return 0;
  const HMONITOR mon = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
  if (!mon) return 0;
  MONITORINFOEXA mi = {};
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfoA(mon, (MONITORINFO*)&mi)) return 0;
  DEVMODEA dm = {};
  dm.dmSize = sizeof(dm);
  if (!EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) return 0;
  return (int)dm.dmDisplayFrequency;
}

// Cached once resolved; retried every call until wnd is known. Never writes g_cfg: a stored default would ride
// along on the next unrelated config write.
static int s_monitorHz;
static bool s_monitorResolved;
static bool s_saidFallback;

void framecap_forget_monitor(void) { s_monitorResolved = false; }

static int resolve_monitor(HWND wnd) {
  if (!s_monitorResolved) {
    const int hz = monitor_refresh_hz(wnd);
    if (hz >= 30 && hz <= 240) {
      s_monitorHz = hz;
      s_monitorResolved = true;
      np_log("framecap: default %d from monitor", hz);
    } else if (!s_saidFallback) {
      s_saidFallback = true;
      np_log("framecap: default fallback 60, window unknown");
    }
  }
  return s_monitorResolved ? s_monitorHz : 0;
}

FcCapInfo framecap_cap_info(HWND wnd) {
  const int refresh = resolve_monitor(wnd);
  // DNF has no gl_sync: vsync is whether the device presents with an interval other than IMMEDIATE (d3dcap
  // reads it from the CreateDevice/Reset present parameters).
  const bool vsync = d3dcap_vsync_on();
  const FcCapClamp c = fc_cap_clamp(g_cfg.frameCapFps, refresh, vsync ? 1 : 0);
  FcCapInfo info;
  info.sliderHi = c.sliderHi;
  info.effectiveFps = c.effectiveFps;
  info.refreshHz = refresh;
  info.vsyncCapped = vsync && refresh >= 30 && refresh <= 240;
  return info;
}

int framecap_effective_fps(HWND wnd) { return framecap_cap_info(wnd).effectiveFps; }

static LARGE_INTEGER s_qpf;
static long long s_deadline;
static bool s_haveDeadline;
static bool s_loggedOn;

// 1 ms system timer resolution, requested once through our own winmm export (this DLL is the process's winmm),
// so Sleep(1) in the coarse part of the wait really sleeps ~1 ms. Left on for the process's life.
static void begin_period_once(void) {
  static bool done;
  if (done) return;
  done = true;
  typedef UINT (WINAPI* PeriodFn)(UINT);
  const HMODULE self = GetModuleHandleW(L"winmm.dll");
  const PeriodFn fn = self ? (PeriodFn)GetProcAddress(self, "timeBeginPeriod") : 0;
  if (fn) fn(1);
  else np_log("framecap: timeBeginPeriod not found; Sleep granularity is the system default");
}

// Sleep(1) while more than 2 ms remain, then a QPC spin for the tail.
static void wait_for_deadline(void) {
  const long long sleepFloor = s_qpf.QuadPart * 2 / 1000;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  while (s_deadline - now.QuadPart > sleepFloor) {
    Sleep(1);
    QueryPerformanceCounter(&now);
  }
  do { QueryPerformanceCounter(&now); } while (now.QuadPart < s_deadline);
}

void framecap_wait(HWND wnd) {
  if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
  LARGE_INTEGER before;
  QueryPerformanceCounter(&before);

  const bool minimized = wnd && IsIconic(wnd);
  const bool visible = wnd && IsWindowVisible(wnd) != 0;
  const bool foreground = wnd && GetForegroundWindow() == wnd;
  const FcCapInfo info = framecap_cap_info(wnd);
  const int fps = info.effectiveFps;
  // Never wait while minimised or alt-tabbed behind another window: a busy spin there burns a core for nothing.
  if (!g_cfg.frameCapOn || fps <= 0 || minimized || (!foreground && !visible)) {
    s_haveDeadline = false;
    return;
  }
  begin_period_once();
  if (!s_loggedOn) {
    s_loggedOn = true;
    np_log("framecap: on at %d fps (monitor %d Hz, stored %d, vsync %d)", fps, info.refreshHz, g_cfg.frameCapFps,
           info.vsyncCapped ? 1 : 0);
  }

  const long long period = s_qpf.QuadPart / fps;
  if (!s_haveDeadline) {
    s_deadline = before.QuadPart + period;
    s_haveDeadline = true;
    return;
  }
  if (fc_wait_ticks(s_deadline, before.QuadPart) > 0) wait_for_deadline();
  LARGE_INTEGER after;
  QueryPerformanceCounter(&after);
  s_deadline = fc_next_deadline(s_deadline, after.QuadPart, period);
}

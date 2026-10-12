// Borderless window glue (display.h, docs/re/display.md). One thread runs DllMain's first call, CreateDevice,
// Reset and the game window (docs/re/engine-map.md section 5), so the statics below are plain.
#include <windows.h>
#include <d3d9.h>
#include <string.h>
#include "display.h"
#include "config.h"
#include "iat.h"
#include "hook.h"
#include "log.h"
#include "engine.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"

static int s_on;                // [display] borderless at launch; a later toggle waits for the next launch
static int s_active;            // a fullscreen request was turned windowed: the import-slot hooks pin the window
static HWND s_wnd;              // the device window we restyled
static BlRect s_mon;            // the monitor it covers
static HWND s_savedWnd;         // the window s_savedStyle/s_savedEx were taken from, before our first restyle of it
static unsigned s_savedStyle, s_savedEx;
static int s_refused;           // d3d9 refused a windowed device: that call ran exclusive fullscreen
static int s_engineWindowed;    // the engine itself asked for a windowed device while on
static unsigned s_devices, s_styleUndo, s_exUndo, s_posUndo, s_leaves, s_minimises, s_failed;
static int s_overlayLogged;

typedef LONG (WINAPI* SetWindowLongFn)(HWND, int, LONG);
typedef BOOL (WINAPI* SetWindowPosFn)(HWND, HWND, int, int, int, int, UINT);
typedef BOOL (WINAPI* MoveWindowFn)(HWND, int, int, int, int, BOOL);
typedef BOOL (WINAPI* ShowWindowFn)(HWND, int);
static SetWindowLongFn s_realSwlW, s_realSwlA;
static SetWindowPosFn s_realSwp;
static MoveWindowFn s_realMove;
static ShowWindowFn s_realShow;

// The first 16 of each kind, then every 256th, so a fight with the engine shows without flooding the log.
static bool log_due(unsigned n) { return n <= 16 || (n & 255u) == 0; }

struct MonList { BlRect r[16]; int n, primary; };

static BOOL CALLBACK mon_cb(HMONITOR m, HDC, LPRECT, LPARAM p) {
  MonList* ml = (MonList*)p;
  MONITORINFO mi;
  mi.cbSize = sizeof mi;
  if (GetMonitorInfoW(m, &mi)) {
    const BlRect r = { mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom };
    if (mi.dwFlags & MONITORINFOF_PRIMARY) ml->primary = ml->n;
    ml->r[ml->n++] = r;
  }
  return ml->n < 16;
}

static BlRect window_rect(HWND h) {
  RECT wr = { 0, 0, 0, 0 };
  GetWindowRect(h, &wr);
  const BlRect r = { wr.left, wr.top, wr.right, wr.bottom };
  return r;
}

static void log_window(const char* when, HWND h) {
  const BlRect r = window_rect(h);
  RECT cr = { 0, 0, 0, 0 };
  GetClientRect(h, &cr);
  np_log("display: %s: window %p style 0x%08x ex 0x%x at %d,%d %dx%d, client %dx%d, iconic %d", when, (void*)h,
         (unsigned)GetWindowLongW(h, GWL_STYLE), (unsigned)GetWindowLongW(h, GWL_EXSTYLE), r.l, r.t, r.r - r.l,
         r.b - r.t, (int)cr.right, (int)cr.bottom, IsIconic(h) ? 1 : 0);
}

// ViewportWndProc (Engine.dll!0x102BC410) ends fullscreen and minimises on focus loss unless
// SteamUtils()->IsOverlayEnabled() (ISteamUtils slot 17, +0x44). The method is __thiscall with no arguments;
// __fastcall puts `this` in ECX the same way and pushes nothing.
int display_steam_overlay(void) {
  typedef void* (__cdecl* SteamUtilsFn)(void);
  typedef bool (__fastcall* IsOverlayFn)(void* self, void* edx);
  HMODULE sa = GetModuleHandleW(L"steam_api.dll");
  SteamUtilsFn get = sa ? (SteamUtilsFn)GetProcAddress(sa, "SteamUtils") : NULL;
  void* utils = get ? get() : NULL;
  void** vt = utils && hook_readable(utils, sizeof(void*)) ? *(void***)utils : NULL;
  if (!vt || !hook_readable(vt + 17, sizeof(void*))) return -1;
  return ((IsOverlayFn)vt[17])(utils, NULL) ? 1 : 0;
}

// Steam's overlay answers 0 while it is still loading (it had hooked CreateDevice by the 2026-10-04 launch that
// logged 0), so this first answer is not the one the engine sees at a later alt-tab: input.cpp logs that one.
static void log_steam_overlay(void) {
  if (s_overlayLogged) return;
  s_overlayLogged = 1;
  np_log("display: Steam overlay enabled=%d at the first device (asked again at each focus change; 1 there: the "
         "engine keeps fullscreen on focus loss)", display_steam_overlay());
}

// The window becomes our popup over the monitor holding most of it. Our own user32 calls do not pass through
// Engine.dll's import slots; the engine's reaction (its WM_SIZE MoveWindow) does, and is pinned there.
static bool restyle(HWND w) {
  if (!w || !IsWindow(w)) { np_log("display: no device window (%p); borderless not applied", (void*)w); return false; }
  MonList ml;
  memset(&ml, 0, sizeof ml);
  EnumDisplayMonitors(NULL, NULL, mon_cb, (LPARAM)&ml);
  const int pick = bl_pick_monitor(ml.r, ml.n, window_rect(w), ml.primary);
  if (pick < 0) { np_log("display: no monitor found; borderless not applied"); return false; }
  log_window("before", w);
  if (w != s_savedWnd) {   // the engine's own chrome, put back when it leaves fullscreen
    s_savedWnd = w;
    s_savedStyle = (unsigned)GetWindowLongW(w, GWL_STYLE);
    s_savedEx = (unsigned)GetWindowLongW(w, GWL_EXSTYLE);
  }
  s_wnd = w;
  s_mon = ml.r[pick];
  s_active = 1;
  const int mw = s_mon.r - s_mon.l, mh = s_mon.b - s_mon.t;
  SetWindowLongW(w, GWL_STYLE, (LONG)BL_STYLE_POPUP);
  SetWindowLongW(w, GWL_EXSTYLE, (LONG)BL_EXSTYLE_POPUP);
  SetWindowPos(w, NULL, s_mon.l, s_mon.t, mw, mh, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  np_log("display: monitor %d of %d at %d,%d %dx%d", pick, ml.n, s_mon.l, s_mon.t, mw, mh);
  log_window("after", w);
  return true;
}

D3DPRESENT_PARAMETERS* display_params(D3DPRESENT_PARAMETERS* pp, D3DPRESENT_PARAMETERS* scratch, HWND wnd,
                                      const char* what) {
  if (!s_on || !pp) return pp;
  *scratch = *pp;
  if (!bl_rewrite_pp(1, scratch)) {
    if (!s_engineWindowed || s_active)
      np_log("display: %s: the engine asks windowed (bb %ux%u); borderless idles until it asks fullscreen", what,
             pp->BackBufferWidth, pp->BackBufferHeight);
    s_engineWindowed = 1;
    s_active = 0;
    return pp;
  }
  s_engineWindowed = 0;
  log_steam_overlay();
  np_log("display: %s params: bb %ux%u fmt %d count %u windowed %d refresh %u interval 0x%x -> windowed %d refresh %u",
         what, pp->BackBufferWidth, pp->BackBufferHeight, (int)pp->BackBufferFormat, pp->BackBufferCount,
         (int)pp->Windowed, pp->FullScreen_RefreshRateInHz, pp->PresentationInterval, (int)scratch->Windowed,
         scratch->FullScreen_RefreshRateInHz);
  if (!restyle(wnd ? wnd : pp->hDeviceWindow)) return pp;
  return scratch;
}

bool display_after(D3DPRESENT_PARAMETERS* pp, D3DPRESENT_PARAMETERS* used, HRESULT hr, const char* what) {
  if (!pp || used == pp) return false;
  bl_params_back(pp, used);
  if (SUCCEEDED(hr)) {
    s_devices++;
    s_refused = 0;
    np_log("display: %s: windowed device ok (bb %ux%u, #%u)", what, used->BackBufferWidth, used->BackBufferHeight,
           s_devices);
    return false;
  }
  if (!bl_fallback(hr)) {
    if (log_due(++s_failed))
      np_log("display: %s: windowed device failed (hr=0x%08lX, #%u); returned to the engine as it is", what,
             (unsigned long)hr, s_failed);
    return false;
  }
  // The engine's own request is the fallback: exclusive fullscreen for this device, as without the panel.
  s_refused = 1;
  s_active = 0;
  np_log("display: %s: windowed device refused (hr=0x%08lX); retrying the engine's fullscreen params", what,
         (unsigned long)hr);
  return true;
}

static LONG set_style(SetWindowLongFn real, HWND h, int idx, LONG v) {
  if (!s_active || h != s_wnd) return real(h, idx, v);
  if (idx == GWL_STYLE) {
    const int rule = bl_style_rule(1, (unsigned)v);
    if (rule == BL_STYLE_LEAVE) {
      s_active = 0;
      const bool saved = s_savedWnd == h;
      const LONG to = saved ? (LONG)bl_leave_style(s_savedStyle, (unsigned)v) : v;
      np_log("display: the engine leaves fullscreen (style 0x%08x -> 0x%08x, ex -> 0x%x, #%u); borderless idles "
             "until it asks fullscreen", (unsigned)v, (unsigned)to, saved ? s_savedEx : 0u, ++s_leaves);
      if (saved) real(h, GWL_EXSTYLE, (LONG)s_savedEx);
      v = to;
    } else if (rule == BL_STYLE_OURS && (unsigned)v != bl_popup_style((unsigned)v)) {
      const LONG to = (LONG)bl_popup_style((unsigned)v);
      if (log_due(++s_styleUndo))
        np_log("display: engine style 0x%08x -> 0x%08x (#%u)", (unsigned)v, (unsigned)to, s_styleUndo);
      v = to;
    }
  } else if (idx == GWL_EXSTYLE && (unsigned)v != BL_EXSTYLE_POPUP) {
    if (log_due(++s_exUndo)) np_log("display: engine ex style 0x%x -> 0x%x (#%u)", (unsigned)v, BL_EXSTYLE_POPUP, s_exUndo);
    v = (LONG)BL_EXSTYLE_POPUP;
  }
  return real(h, idx, v);
}
static LONG WINAPI swl_w(HWND h, int idx, LONG v) { return set_style(s_realSwlW, h, idx, v); }
static LONG WINAPI swl_a(HWND h, int idx, LONG v) { return set_style(s_realSwlA, h, idx, v); }

static bool pin(const char* api, unsigned flags, int* x, int* y, int* cx, int* cy) {
  BlPos p = { *x, *y, *cx, *cy };
  if (!bl_fix_pos(flags, &p, s_mon)) return false;
  if (log_due(++s_posUndo))
    np_log("display: engine %s %d,%d %dx%d flags 0x%x -> %d,%d %dx%d (#%u)", api, *x, *y, *cx, *cy, flags, p.x, p.y,
           p.cx, p.cy, s_posUndo);
  *x = p.x; *y = p.y; *cx = p.cx; *cy = p.cy;
  return true;
}

static BOOL WINAPI swp_hook(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags) {
  if (s_active && h == s_wnd) pin("SetWindowPos", flags, &x, &y, &cx, &cy);
  return s_realSwp(h, after, x, y, cx, cy, flags);
}

static BOOL WINAPI move_hook(HWND h, int x, int y, int cx, int cy, BOOL repaint) {
  if (s_active && h == s_wnd) pin("MoveWindow", 0, &x, &y, &cx, &cy);
  return s_realMove(h, x, y, cx, cy, repaint);
}

// Measurement only: the engine's own minimise on focus loss (ViewportWndProc's WM_ACTIVATE) is left to run.
static BOOL WINAPI show_hook(HWND h, int cmd) {
  if (h && h == s_wnd && (cmd == SW_MINIMIZE || cmd == SW_SHOWMINIMIZED || cmd == SW_FORCEMINIMIZE) &&
      log_due(++s_minimises))
    np_log("display: engine ShowWindow(%d) on the game window (active %d, #%u)", cmd, s_active, s_minimises);
  return s_realShow(h, cmd);
}

void display_boot(void) {
  const int peek = config_peek_borderless();
  s_on = peek == 1 ? 1 : 0;
  np_log("display: borderless=%d at launch%s", s_on, peek < 0 ? " (nukepanel.ini unreadable: off)" : "");
  if (!s_on) return;
  // Engine.dll's import slots are data CEG does not hash (docs/re/ceg.md); its code is never touched.
  HMODULE eng = GetModuleHandleW(L"Engine.dll");
  if (!eng) { np_log("display: Engine.dll not loaded; the window is restyled at each device only"); return; }
  iat_patch(eng, "USER32.dll", "SetWindowLongW", (void*)&swl_w, (void**)&s_realSwlW, "display");
  iat_patch(eng, "USER32.dll", "SetWindowLongA", (void*)&swl_a, (void**)&s_realSwlA, "display");
  iat_patch(eng, "USER32.dll", "SetWindowPos", (void*)&swp_hook, (void**)&s_realSwp, "display");
  iat_patch(eng, "USER32.dll", "MoveWindow", (void*)&move_hook, (void**)&s_realMove, "display");
  iat_patch(eng, "USER32.dll", "ShowWindow", (void*)&show_hook, (void**)&s_realShow, "display");
}

int display_borderless_on(void) { return s_on; }
int display_borderless_active(void) { return s_on && s_active; }

const char* display_note(void) {
  if ((g_cfg.borderless ? 1 : 0) != s_on) return "Changed: restart the game to apply";
  if (!s_on) return NULL;
  if (s_refused) return "Could not apply; see nukepanel.log";
  if (s_engineWindowed) return "The game is set to windowed: this acts in fullscreen";
  return NULL;
}

// ------------------------------------------------------------------ StartupFullscreen watch
// Measurement only: the flag is read, never written (the game's ini encoding breaks on a rewrite).
static int s_fsOk;
static int s_fsClientOfs, s_fsFlagOfs;
static unsigned s_fsMask, s_fsTries, s_fsFrames, s_fsLines;
static int s_fsLast = -1;   // the value last read, so the frame poll reports only what the messages did not

static int fs_av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static const void* fs_engine(void) {
  void* const* slot = engine_exports()->gEngine;
  return slot ? *(void* const volatile*)slot : 0;
}

// Engine.Client, then the client's StartupFullscreen (a config bool: UWindowsClient::StaticConstructor
// Engine.dll!0x102B7900 declares it at +0xE0), each by name. Names are looked up outside the guard (uname.h).
static const char* fs_resolve(int nClient, int nFlag) {
  const void* eng = fs_engine();
  if (!eng) return "no GEngine";
  __try {
    const UProp c = uprop_get(uobj_class(eng), nClient);
    if (!c.ok || c.type != UW_OBJECT) return "Engine.Client did not resolve";
    const void* client = uw_ptr(eng, c.offset);
    if (!client) return "no client yet";
    const UProp f = uprop_get(uobj_class(client), nFlag);
    if (!f.ok || f.type != UW_BOOL || !f.bitmask) return "StartupFullscreen did not resolve";
    s_fsClientOfs = c.offset;
    s_fsFlagOfs = f.offset;
    s_fsMask = f.bitmask;
    return 0;
  } __except (fs_av_only(GetExceptionCode())) {
    return "faulted";
  }
}

int display_startup_fullscreen(void) {
  if (!s_fsOk) return -1;
  const void* eng = fs_engine();
  if (!eng) return -1;
  __try {
    const void* client = uw_ptr(eng, s_fsClientOfs);
    if (!client) return -1;
    return ((unsigned)uw_int(client, s_fsFlagOfs) & s_fsMask) ? 1 : 0;
  } __except (fs_av_only(GetExceptionCode())) {
    return -1;
  }
}

static const char* fs_change_name(int ch) {
  return ch == FS_ENDED ? "ended (the next launch starts windowed)" : ch == FS_BEGAN ? "begun" :
         ch == FS_UNREAD ? "unreadable on one side" : "unchanged";
}

void display_fs_message(unsigned msg, unsigned w, int before, int after, int front, int iconic) {
  const int ch = fs_change(before, after);
  if (after >= 0) s_fsLast = after;
  if (ch == FS_SAME && !fs_always_log(msg, w)) return;
  if (++s_fsLines > 64 && (s_fsLines & 63u)) return;
  np_log("display: StartupFullscreen %s (%d -> %d) in the engine's %s 0x%x (#%u); in front=%d iconic=%d overlay=%d "
         "borderless active=%d loading=%d", fs_change_name(ch), before, after, fs_msg_name(msg), w, s_fsLines, front,
         iconic, display_steam_overlay(), display_borderless_active(), engine_level_loading());
}

void display_fs_frame(void) {
  if (++s_fsFrames < 60) return;
  s_fsFrames = 0;
  if (!s_fsOk) {
    // Resolved between frames, never inside the WndProc; not while a level loads (the class chains are the load's).
    if (s_fsTries >= 256 || engine_level_loading()) return;
    const int nClient = uname_find(L"Client"), nFlag = uname_find(L"StartupFullscreen");
    const char* why = nClient && nFlag ? fs_resolve(nClient, nFlag) : "names not found";
    ++s_fsTries;
    if (!why) {
      s_fsOk = 1;
      s_fsLast = display_startup_fullscreen();
      np_log("display: StartupFullscreen watch on: Engine.Client +0x%x, StartupFullscreen +0x%x mask 0x%x, now %d",
             s_fsClientOfs, s_fsFlagOfs, s_fsMask, s_fsLast);
    } else if (s_fsTries == 256) {
      np_log("display: StartupFullscreen watch off after 256 tries: %s", why);
    }
    return;
  }
  const int now = display_startup_fullscreen();
  const int ch = fs_change(s_fsLast, now);
  if (now >= 0) {
    if (ch != FS_SAME && (++s_fsLines <= 64 || (s_fsLines & 63u) == 0))
      np_log("display: StartupFullscreen %s (%d -> %d) between window messages (a console command or script) (#%u); "
             "borderless active=%d", fs_change_name(ch), s_fsLast, now, s_fsLines, display_borderless_active());
    s_fsLast = now;
  }
}

void display_fs_exit(void) {
  np_log("display: at window destroy: StartupFullscreen %d (what the next launch starts with); the engine's last "
         "device %s; borderless on=%d active=%d", display_startup_fullscreen(),
         !s_on ? "not tracked (borderless off)" : s_engineWindowed ? "windowed" : s_active ? "fullscreen" : "fullscreen, "
         "borderless idle", s_on, display_borderless_active());
}

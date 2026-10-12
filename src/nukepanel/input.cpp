// D5: the WndProc subclass on the device's focus window and the cursor
// gate on the engine's user32 import slots. The message handling is duke-dnmp's input.cpp less its engine
// facts (inp_*, the console, the freecam); the gate is new (cursorgate.h).
#include <windows.h>
#include <string.h>
#include <intrin.h>
#include "input.h"
#include "stallrule.h"   // stall_fmt_addr
#include "cursorgate.h"
#include "dimouse.h"
#include "clicklock.h"
#include "display.h"
#include "iat.h"
#include "d3dcap.h"
#include "drawgate.h"
#include "frame.h"
#include "log.h"
#include "config.h"
#include "focuskeep.h"
#include "freecam.h"   // freecam_camera_key_now
#include "ui/menu.h"   // menu_keycap_armed, panel_key_effective

static HWND s_wnd;
static WNDPROC s_orig;                  // the proc under ours on s_wnd
static bool s_open;
static int s_key = VK_INSERT;
static HCURSOR s_arrow;
// Game speed key presses, taken by gamespeed_frame. Plain: the WndProc and the frame run on one thread
// (docs/re/engine-map.md section 5; the WndProc logs a THREAD MISMATCH otherwise).
static int s_speed_presses;
static int s_cam_presses, s_copy_presses, s_paste_presses, s_tp_presses, s_photo_presses;
static int s_fly;                        // input_set_flight
static unsigned s_focus_losses;
static unsigned s_fly_swallowed;         // input messages kept from the engine while flying, cumulative

static int s_client_x, s_client_y;       // client-area pixels, from the last mouse message
static int s_cx, s_cy;                   // mapped to back-buffer pixels at the last frame
static bool s_btn[2];
static int s_press[2];                   // button-down messages since the last take
static int s_wheel;
static int s_soft_cursor, s_cursor_checked;
static volatile LONG s_wndproc_tid_logged;
static const char* volatile s_draw_off;   // drawgate_reason, from the frame path
// Written by the frame path (episode end) and the panel key (logged reason): one thread. Were they two,
// a torn view of this pointer only costs one log line more or less.
static DrawGate s_draw_gate;

struct KeyEv { int vk, repeat, mods; };
static KeyEv s_keys[32];
static unsigned s_chars[32];
static int s_key_head, s_key_tail, s_char_head, s_char_tail;   // full rings drop the newest

// The keys and buttons the engine saw go down (passed to it while closed), released to it on open.
static KeyHold s_held;
// What the engine asked of the cursor while open, replayed on close.
static CgState s_cg;
// The clip in force when the panel opened (the engine's), put back on close unless it asked for another meanwhile.
static RECT s_open_clip;
static bool s_open_clipped;

// What reaches the window and what the gate held back while open; logged every 600 open frames and at close.
static unsigned s_u4_frames, s_u4_move, s_u4_input, s_u4_gated[CG_KINDS], s_u4_reasserts, s_u4_getcursorpos;
static unsigned s_u4_lbutton;
static unsigned s_gated_total;

// Engine.dll's cursor calls made while the game window is not in front, and the mouse moves borderless holds
// there (bl_bg_hold, docs/re/display.md "Focus loss"): this window (logged every 600 frames when any, and at each
// focus change), then the session totals. One thread with the WndProc.
enum { BG_SETCURSORPOS = 0, BG_CLIPCURSOR, BG_SETCAPTURE, BG_KINDS };
static unsigned s_bg_calls[BG_KINDS], s_bg_held[BG_KINDS], s_bg_moves;
static unsigned s_bg_calls_total[BG_KINDS], s_bg_held_total[BG_KINDS], s_bg_moves_total;
static unsigned s_bg_frames, s_focus_changes, s_focus_msgs;
static int s_front = -1;   // the game window was the foreground window at the last frame: 1, 0, -1 not seen yet
// The last activation message the engine handled: 1 activated (or none seen yet), 0 deactivated (focuskeep.h).
static int s_activated = 1;

// True while the game window is the foreground window, or before there is a game window to ask about.
static int game_front(void) { return !s_wnd || GetForegroundWindow() == s_wnd; }
static void bg_count(int kind) { s_bg_calls[kind]++; s_bg_calls_total[kind]++; }
static void bg_held(int kind) { s_bg_held[kind]++; s_bg_held_total[kind]++; }

// ------------------------------------------------------------------ the cursor gate
enum { MOD_ENGINE = 0, MOD_DNCOMMON = 1, MOD_COUNT = 2 };
static void* s_real[MOD_COUNT][CG_SLOTS];

typedef BOOL (WINAPI* SetCursorPosFn)(int, int);
typedef BOOL (WINAPI* ClipCursorFn)(const RECT*);
typedef int (WINAPI* ShowCursorFn)(BOOL);
typedef HCURSOR (WINAPI* SetCursorFn)(HCURSOR);
typedef HWND (WINAPI* SetCaptureFn)(HWND);
typedef SHORT (WINAPI* GetKeyStateFn)(int);
typedef BOOL (WINAPI* ReleaseCaptureFn)(void);
typedef BOOL (WINAPI* GetCursorPosFn)(POINT*);

static const char* const kGateNames[CG_SLOTS] = { "SetCursorPos", "ClipCursor", "ShowCursor", "SetCursor",
                                                  "SetCapture", "GetKeyState", "ReleaseCapture", "GetCursorPos" };

// The gates read and write plain statics on the assumption that the engine calls them on the game's main thread
// (docs/re/engine-map.md section 5); a call from another thread is logged once per hook, behaviour unchanged.
static volatile LONG s_gate_tid_logged[CG_SLOTS];
static void gate_thread_check(int kind) {
  const unsigned long ft = frame_thread_id();
  const unsigned long t = GetCurrentThreadId();
  if (ft && t != ft && !InterlockedExchange(&s_gate_tid_logged[kind], 1))
    np_log("input: THREAD MISMATCH: %s on tid=%lu, frame thread %lu", kGateNames[kind], t, ft);
}

// The real display count, unchanged: our own ShowCursor import is not gated.
static int cursor_count(void) {
  ShowCursor(TRUE);
  return ShowCursor(FALSE);
}

template <int M> static BOOL WINAPI gate_SetCursorPos(int x, int y) {
  gate_thread_check(CG_SETCURSORPOS);
  const int front = game_front();
  if (M == MOD_ENGINE && !front) bg_count(BG_SETCURSORPOS);
  if (cursor_gate_passes(CG_SETCURSORPOS, s_open)) {
    if (!bl_bg_hold(display_borderless_active(), front)) return ((SetCursorPosFn)s_real[M][CG_SETCURSORPOS])(x, y);
    bg_held(BG_SETCURSORPOS);
    return TRUE;
  }
  s_u4_gated[CG_SETCURSORPOS]++; s_gated_total++;
  return TRUE;
}
template <int M> static BOOL WINAPI gate_ClipCursor(const RECT* r) {
  gate_thread_check(CG_CLIPCURSOR);
  const int front = game_front();
  if (M == MOD_ENGINE && !front) bg_count(BG_CLIPCURSOR);
  // The engine's own argument to a user32 call, read as user32 would read it.
  if (s_open) s_cg = cg_on_clip(s_cg, true, r == NULL, r ? r->left : 0, r ? r->top : 0, r ? r->right : 0,
                                r ? r->bottom : 0);
  if (cursor_gate_passes_clip(s_open, r == NULL)) {
    if (!bl_bg_hold_clip(display_borderless_active(), front, r == NULL))
      return ((ClipCursorFn)s_real[M][CG_CLIPCURSOR])(r);
    bg_held(BG_CLIPCURSOR);
    return TRUE;
  }
  s_u4_gated[CG_CLIPCURSOR]++; s_gated_total++;
  return TRUE;
}
// Every ShowCursor through the slots, with its caller and the count around it: the first 128 lines, then every
// 64th. The last caller is kept for the hold's watch line; names are cached per return address (a handful of call
// sites), so the loader is asked once per site.
static unsigned s_show_calls, s_show_skipped;
static char s_show_last[96] = "none yet";
struct ShowSite { void* from; char name[96]; };
static ShowSite s_show_sites[8];
static unsigned s_show_site_next;
static void show_caller(void* from) {
  for (int i = 0; i < 8; i++)
    if (s_show_sites[i].from == from) { strcpy_s(s_show_last, sizeof s_show_last, s_show_sites[i].name); return; }
  HMODULE h = 0;
  char path[MAX_PATH];
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCSTR)from, &h) && GetModuleFileNameA(h, path, MAX_PATH))
    stall_fmt_addr(s_show_last, sizeof s_show_last, path, (unsigned)(UINT_PTR)from, (unsigned)(UINT_PTR)h);
  else
    stall_fmt_addr(s_show_last, sizeof s_show_last, 0, (unsigned)(UINT_PTR)from, 0);
  ShowSite& s = s_show_sites[s_show_site_next++ & 7u];
  s.from = from;
  strcpy_s(s.name, sizeof s.name, s_show_last);
}
static void show_log(int show, void* from, int before, int after, const char* note) {
  ++s_show_calls;
  show_caller(from);
  if (s_show_calls > 128 && (s_show_calls & 63u)) return;
  np_log("cursor: ShowCursor(%d) from %s: count %d -> %d%s (call %u, %u not counted)", show, s_show_last, before,
         after, note, s_show_calls, s_show_skipped);
}

template <int M> static int WINAPI gate_ShowCursor(BOOL show) {
  gate_thread_check(CG_SHOWCURSOR);
  void* const from = _ReturnAddress();
  const int on = show ? 1 : 0;
  if (cursor_gate_passes(CG_SHOWCURSOR, s_open)) {
    const int after = ((ShowCursorFn)s_real[M][CG_SHOWCURSOR])(show);
    const int before = on ? after - 1 : after + 1;
    // Engine.dll's redundant step (cg_show_redundant) is undone in the same call, so the cursor never blinks and
    // nothing of ours stays on the count.
    if (M == MOD_ENGINE && cg_show_redundant(on, before)) {
      ((ShowCursorFn)s_real[M][CG_SHOWCURSOR])(on ? FALSE : TRUE);
      s_show_skipped++;
      show_log(on, from, before, before, on ? " (already visible: not counted)" : " (already hidden: not counted)");
      return before;
    }
    show_log(on, from, before, after, "");
    return after;
  }
  const int v = cg_virtual_count(s_cg, cursor_count());
  if (M == MOD_ENGINE && cg_show_redundant(on, v)) {
    s_show_skipped++;
    show_log(on, from, v, v, on ? " (panel open, already visible: not recorded)" :
                                  " (panel open, already hidden: not recorded)");
    return v;
  }
  s_cg = cg_on_showcursor(s_cg, true, on);
  s_u4_gated[CG_SHOWCURSOR]++; s_gated_total++;
  const int nv = cg_virtual_count(s_cg, cursor_count());
  show_log(on, from, v, nv, " (panel open: held for close)");
  return nv;
}
template <int M> static HCURSOR WINAPI gate_SetCursor(HCURSOR c) {
  gate_thread_check(CG_SETCURSOR);
  if (cursor_gate_passes(CG_SETCURSOR, s_open)) return ((SetCursorFn)s_real[M][CG_SETCURSOR])(c);
  s_cg = cg_on_setcursor(s_cg, true, (void*)c);
  s_u4_gated[CG_SETCURSOR]++; s_gated_total++;
  return GetCursor();
}
template <int M> static HWND WINAPI gate_SetCapture(HWND h) {
  gate_thread_check(CG_SETCAPTURE);
  if (M == MOD_ENGINE && !game_front()) bg_count(BG_SETCAPTURE);   // counted only: capture does not pin
  if (cursor_gate_passes(CG_SETCAPTURE, s_open)) return ((SetCaptureFn)s_real[M][CG_SETCAPTURE])(h);
  s_cg = cg_on_capture(s_cg, true, (void*)h);
  s_u4_gated[CG_SETCAPTURE]++; s_gated_total++;
  return GetCapture();
}

template <int M> static SHORT WINAPI gate_GetKeyState(int vk) {
  gate_thread_check(CG_GETKEYSTATE);
  if (keystate_passes(vk, s_open, s_fly != 0, g_cfg.freecamOn ? config_bind_get(BIND_ROW_CAMERA) : 0))
    return ((GetKeyStateFn)s_real[M][CG_GETKEYSTATE])(vk);
  s_u4_gated[CG_GETKEYSTATE]++; s_gated_total++;
  return 0;   // not down, not toggled
}

template <int M> static BOOL WINAPI gate_ReleaseCapture(void) {
  gate_thread_check(CG_RELEASECAPTURE);
  if (cursor_gate_passes(CG_RELEASECAPTURE, s_open)) return ((ReleaseCaptureFn)s_real[M][CG_RELEASECAPTURE])();
  s_cg = cg_on_capture(s_cg, true, NULL);
  s_u4_gated[CG_RELEASECAPTURE]++; s_gated_total++;
  return TRUE;
}
// Never altered: the question is only whether the engine polls the cursor while the panel is open.
template <int M> static BOOL WINAPI count_GetCursorPos(POINT* p) {
  gate_thread_check(CG_GETCURSORPOS);
  if (s_open) s_u4_getcursorpos++;
  return ((GetCursorPosFn)s_real[M][CG_GETCURSORPOS])(p);
}

template <int M> static void* gate_hook(int kind) {
  switch (kind) {
    case CG_SETCURSORPOS: return (void*)gate_SetCursorPos<M>;
    case CG_CLIPCURSOR:   return (void*)gate_ClipCursor<M>;
    case CG_SHOWCURSOR:   return (void*)gate_ShowCursor<M>;
    case CG_SETCURSOR:    return (void*)gate_SetCursor<M>;
    case CG_SETCAPTURE:   return (void*)gate_SetCapture<M>;
    case CG_GETKEYSTATE:  return (void*)gate_GetKeyState<M>;
    case CG_RELEASECAPTURE: return (void*)gate_ReleaseCapture<M>;
    default:              return (void*)count_GetCursorPos<M>;
  }
}

// Engine.dll imports all eight; dnCommon.dll SetCapture and ShowCursor (import tables read 2026-10-04).
static void install_gate(void) {
  const HMODULE eng = GetModuleHandleW(L"Engine.dll"), dnc = GetModuleHandleW(L"dnCommon.dll");
  int n = 0;
  if (!eng) np_log("cursor: Engine.dll not loaded; its slots stay ungated");
  else
    for (int k = 0; k < CG_SLOTS; k++)
      n += iat_patch(eng, "user32.dll", kGateNames[k], gate_hook<MOD_ENGINE>(k), &s_real[MOD_ENGINE][k], "cursor");
  static const int kDnc[2] = { CG_SETCAPTURE, CG_SHOWCURSOR };
  if (!dnc) np_log("cursor: dnCommon.dll not loaded; its slots stay ungated");
  else
    for (int i = 0; i < 2; i++)
      n += iat_patch(dnc, "user32.dll", kGateNames[kDnc[i]], gate_hook<MOD_DNCOMMON>(kDnc[i]),
                     &s_real[MOD_DNCOMMON][kDnc[i]], "cursor");
  np_log("cursor: %d/10 import slots patched (9 gated, 1 counted)", n);
}

static void log_u4(const char* when) {
  unsigned diHeld = 0, diZeroed = 0;
  dimouse_take(&diHeld, &diZeroed);
  const double f = s_u4_frames ? (double)s_u4_frames : 1.0;
  np_log("input: U4 %s frames=%u wm_mousemove=%u wm_input=%u gated setcursorpos=%u clipcursor=%u showcursor=%u "
         "setcursor=%u setcapture=%u releasecapture=%u getkeystate=%u getcursorpos_passed=%u clip_reasserts=%u "
         "di_acquire_held=%u di_getstate_zeroed=%u wm_lbuttondown=%u "
         "per_frame mousemove=%.2f setcursorpos=%.2f getkeystate=%.2f getcursorpos=%.2f total_gated=%u",
         when, s_u4_frames, s_u4_move, s_u4_input, s_u4_gated[CG_SETCURSORPOS], s_u4_gated[CG_CLIPCURSOR],
         s_u4_gated[CG_SHOWCURSOR], s_u4_gated[CG_SETCURSOR], s_u4_gated[CG_SETCAPTURE],
         s_u4_gated[CG_RELEASECAPTURE], s_u4_gated[CG_GETKEYSTATE], s_u4_getcursorpos, s_u4_reasserts,
         diHeld, diZeroed, s_u4_lbutton,
         s_u4_move / f, s_u4_gated[CG_SETCURSORPOS] / f, s_u4_gated[CG_GETKEYSTATE] / f, s_u4_getcursorpos / f,
         s_gated_total);
  s_u4_frames = s_u4_move = s_u4_input = s_u4_reasserts = s_u4_getcursorpos = s_u4_lbutton = 0;
  memset(s_u4_gated, 0, sizeof s_u4_gated);
}

// The foreground window's class and executable name, for the focus lines.
static void describe_window(HWND w, char* cls, int ncls, char* exe, int nexe) {
  cls[0] = 0;
  exe[0] = 0;
  if (!w) return;
  if (!GetClassNameA(w, cls, ncls)) cls[0] = 0;
  DWORD pid = 0;
  GetWindowThreadProcessId(w, &pid);
  const HANDLE p = pid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : NULL;
  if (!p) return;
  char path[MAX_PATH];
  DWORD n = MAX_PATH;
  if (QueryFullProcessImageNameA(p, 0, path, &n)) {
    const char* const b = strrchr(path, '\\');
    strncpy_s(exe, nexe, b ? b + 1 : path, _TRUNCATE);
  }
  CloseHandle(p);
}

static void log_bg(const char* when, HWND fg) {
  char cls[64], exe[64];
  describe_window(fg, cls, sizeof cls, exe, sizeof exe);
  np_log("focus: %s; foreground %p class '%s' process '%s'; engine while not in front: setcursorpos=%u (held %u) "
         "clipcursor=%u (held %u) setcapture=%u mousemoves_held=%u; totals setcursorpos=%u (held %u) clipcursor=%u "
         "(held %u) setcapture=%u mousemoves_held=%u; borderless=%d overlay=%d panel=%d",
         when, (void*)fg, cls, exe, s_bg_calls[BG_SETCURSORPOS], s_bg_held[BG_SETCURSORPOS], s_bg_calls[BG_CLIPCURSOR],
         s_bg_held[BG_CLIPCURSOR], s_bg_calls[BG_SETCAPTURE], s_bg_moves, s_bg_calls_total[BG_SETCURSORPOS],
         s_bg_held_total[BG_SETCURSORPOS], s_bg_calls_total[BG_CLIPCURSOR], s_bg_held_total[BG_CLIPCURSOR],
         s_bg_calls_total[BG_SETCAPTURE], s_bg_moves_total, display_borderless_on(), display_steam_overlay(),
         s_open ? 1 : 0);
  memset(s_bg_calls, 0, sizeof s_bg_calls);
  memset(s_bg_held, 0, sizeof s_bg_held);
  s_bg_moves = 0;
}

// Each change of the foreground window into or out of the game, and every 600 frames a line when the engine made
// cursor calls in the background. Logs the first 64 changes, then every 64th.
static void focus_frame(void) {
  if (!s_wnd) return;
  const HWND fg = GetForegroundWindow();
  const int front = fg == s_wnd ? 1 : 0;
  if (front != s_front) {
    s_front = front;
    if (++s_focus_changes <= 64 || (s_focus_changes & 63u) == 0) {
      char when[64];
      _snprintf_s(when, sizeof when, _TRUNCATE, "game window %s (#%u)", front ? "in front" : "not in front",
                  s_focus_changes);
      log_bg(when, fg);
    }
  }
  if (++s_bg_frames < 600) return;
  s_bg_frames = 0;
  // Capped like the focus lines: the first 64, then every 64th (it repeats while the mouse crosses the game).
  static unsigned s_bg_lines;
  if ((s_bg_calls[BG_SETCURSORPOS] | s_bg_calls[BG_CLIPCURSOR] | s_bg_calls[BG_SETCAPTURE] | s_bg_moves) &&
      (++s_bg_lines <= 64 || (s_bg_lines & 63u) == 0))
    log_bg("600 frames", fg);
}

// ------------------------------------------------------------------ the cursor hold (clicklock.h) The engine mouse is
// shared (dimouse.cpp), so nothing but the engine's one ShowCursor(0) hides the OS cursor in play. Ours: SetCursor and
// SetCursorPos through our own imports only, never ClipCursor or ShowCursor, so nothing is left behind when it lets go.
static bool s_hold;
static unsigned s_hold_ons, s_hold_warps, s_hold_swallowed;
static ClHoldWatch s_hold_watch = cl_watch_init();
static unsigned s_hold_watch_lines;

// Back to the client centre unless already there; each warp is counted.
static void hold_centre(void) {
  RECT rc;
  long x, y;
  if (!s_wnd || !GetClientRect(s_wnd, &rc) || !cl_hold_centre(0, 0, rc.right, rc.bottom, &x, &y)) return;
  POINT sc = { x, y }, now;
  if (!ClientToScreen(s_wnd, &sc)) return;
  if (GetCursorPos(&now) && now.x == sc.x && now.y == sc.y) return;
  SetCursorPos(sc.x, sc.y);
  s_hold_warps++;
}

// Whoever takes the cursor next sets its own shape: the panel its arrow, the engine its release, the window under
// the cursor after a focus loss.
static void hold_set(bool on, const char* why) {
  if (on == s_hold) return;
  s_hold = on;
  if (on) {
    s_hold_ons++;
    hold_centre();
    SetCursor(NULL);
  }
  // Capped like the focus lines; the cursor's own flags show how GetCursorInfo reads our blank shape.
  static unsigned s_holdLogs;
  if (++s_holdLogs > 64) return;
  CURSORINFO ci;
  ci.cbSize = sizeof ci;
  const BOOL got = GetCursorInfo(&ci);
  np_log("input: cursor hold %s (%s); on %u time(s), %u warp(s), %u mouse message(s) kept from the engine so far; "
         "cursor flags 0x%lx shape %p%s", on ? "on" : "off", why, s_hold_ons, s_hold_warps, s_hold_swallowed,
         got ? ci.flags : 0, got ? (void*)ci.hCursor : 0, s_holdLogs == 64 ? " (last hold line)" : "");
}

// Once a frame; the focus messages release it at once, before the next frame. The engine releasing its mouse
// (SetMouseCapture(0): Unacquire, ShowCursor until visible) yields, so its windowed and menu cursor stay its own.
static void hold_frame(void) {
  CURSORINFO ci;
  ci.cbSize = sizeof ci;
  const BOOL gotCi = GetCursorInfo(&ci);
  const int shown = gotCi && (ci.flags & CURSOR_SHOWING) ? 1 : 0;
  const ClHoldIn in = { s_wnd ? 1 : 0, s_wnd && s_activated && GetForegroundWindow() == s_wnd ? 1 : 0,
                        s_wnd && IsIconic(s_wnd) ? 1 : 0, s_open ? 1 : 0, dimouse_engine_wants() ? 0 : 1, shown };
  const int block = cl_hold_block(in);
  // Capped: the first 32 stretches, then every 32nd (a long menu stay is one too).
  if (cl_hold_watch(&s_hold_watch, block, GetTickCount64()) &&
      (++s_hold_watch_lines <= 32 || (s_hold_watch_lines & 31u) == 0))
    np_log("input: cursor hold kept off 5 s in play (%s): engine wants its mouse %d, cursor flags 0x%lx shape %p "
           "count %d; last ShowCursor from %s, %u call(s), %u not counted (stretch %u)", cl_hold_name(block),
           dimouse_engine_wants(), gotCi ? ci.flags : 0ul, gotCi ? (void*)ci.hCursor : 0, cursor_count(),
           s_show_last, s_show_calls, s_show_skipped, s_hold_watch_lines);
  hold_set(block == CL_HOLD_OK, cl_hold_name(block));
  if (s_hold) hold_centre();
}

// ------------------------------------------------------------------ open and close
static void clear_ui_state(void) {
  s_btn[0] = s_btn[1] = false;
  s_press[0] = s_press[1] = 0;
  s_wheel = 0;
  s_key_head = s_key_tail = s_char_head = s_char_tail = 0;
}

static bool is_mouse_code(int vk) { return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON ||
                                           vk == VK_XBUTTON1 || vk == VK_XBUTTON2; }

// Each key and button the engine holds goes up, straight into its own proc, before the panel swallows the
// real release (Review Focus 3).
static void release_held(void) {
  int ups[256];
  const int n = keyhold_take_on_open(&s_held, ups, 256);
  const LPARAM at = MAKELPARAM(s_client_x, s_client_y);
  char list[128];
  int len = 0;
  list[0] = 0;
  for (int i = 0; i < n; i++) {
    const int vk = ups[i];
    if (is_mouse_code(vk)) {
      const UINT m = vk == VK_LBUTTON ? WM_LBUTTONUP : vk == VK_RBUTTON ? WM_RBUTTONUP :
                     vk == VK_MBUTTON ? WM_MBUTTONUP : WM_XBUTTONUP;
      const WPARAM w = m == WM_XBUTTONUP ? MAKEWPARAM(0, vk == VK_XBUTTON1 ? XBUTTON1 : XBUTTON2) : 0;
      CallWindowProcW(s_orig, s_wnd, m, w, at);
    } else {
      const UINT scan = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_VSC);
      const LPARAM l = (LPARAM)(1u | (scan << 16) | (1u << 30) | (1u << 31));
      const bool sys = vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU || vk == VK_F10;
      CallWindowProcW(s_orig, s_wnd, sys ? WM_SYSKEYUP : WM_KEYUP, (WPARAM)vk, l);
    }
    if (len < (int)sizeof list - 6) len += _snprintf_s(list + len, sizeof list - len, _TRUNCATE, " %02X", vk);
  }
  if (n) np_log("input: released %d held key(s)/button(s) to the engine:%s", n, list);
}

static bool clip_is_whole_screen(const RECT& r) {
  const RECT vs = { GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                    GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                    GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) };
  return r.left <= vs.left && r.top <= vs.top && r.right >= vs.right && r.bottom >= vs.bottom;
}

// The game window's client rect on screen, while it is in front: the open panel keeps the cursor inside it,
// so a click cannot land on another monitor and minimise the exclusive-fullscreen game.
static bool window_clip_rect(RECT* out) {
  if (!s_wnd || GetForegroundWindow() != s_wnd || IsIconic(s_wnd) || !GetClientRect(s_wnd, out)) return false;
  POINT a = { out->left, out->top }, b = { out->right, out->bottom };
  if (!ClientToScreen(s_wnd, &a) || !ClientToScreen(s_wnd, &b) || b.x <= a.x || b.y <= a.y) return false;
  out->left = a.x; out->top = a.y; out->right = b.x; out->bottom = b.y;
  return true;
}

static RECT s_our_clip;
static bool s_our_clipped;

// Re-applies our clip when the one in force is not it (the window moved or resized, focus came back); in the
// background lifts it, so the desktop gets a free cursor while the game is not in front.
static void clip_assert(void) {
  RECT want, have;
  const bool front = window_clip_rect(&want);
  const bool got = GetClipCursor(&have) != 0;
  if (front) {
    // A clip already equal to the window (one fullscreen monitor: the whole screen) is the window clip in force.
    if (!(got && clip_rect_same(have.left, have.top, have.right, have.bottom, want.left, want.top, want.right,
                                want.bottom))) {
      ClipCursor(&want);
      s_u4_reasserts++;
    }
    s_our_clip = want;
    s_our_clipped = true;
  } else if (s_our_clipped) {
    if (got && clip_rect_same(have.left, have.top, have.right, have.bottom, s_our_clip.left, s_our_clip.top,
                              s_our_clip.right, s_our_clip.bottom)) ClipCursor(NULL);
    s_our_clipped = false;
  }
}

// Focus went elsewhere (Alt+Tab, a click on another monitor): the frame path may stop running in the background,
// so our clip is lifted here, and the key-ups the other window took are not the engine's to be sent later.
static void on_focus_lost(void) {
  RECT have;
  if (s_our_clipped && GetClipCursor(&have) &&
      clip_rect_same(have.left, have.top, have.right, have.bottom, s_our_clip.left, s_our_clip.top,
                     s_our_clip.right, s_our_clip.bottom))
    ClipCursor(NULL);
  s_our_clipped = false;
  s_held = keyhold_init();
  s_focus_losses++;
}

static void on_open(void) {
  hold_set(false, "panel open");
  POINT pt;
  if (GetCursorPos(&pt) && ScreenToClient(s_wnd, &pt)) { s_client_x = pt.x; s_client_y = pt.y; }
  release_held();
  CURSORINFO ci;
  ci.cbSize = sizeof ci;
  const BOOL gotShape = GetCursorInfo(&ci);
  s_cg = cg_on_open_shape(cg_init(), gotShape ? (void*)ci.hCursor : 0, gotShape ? 1 : 0);
  // The engine's DirectInput mouse is unacquired, so the window gets WM_MOUSE*; before the gate is on.
  dimouse_open();
  s_open_clipped = GetClipCursor(&s_open_clip) && !clip_is_whole_screen(s_open_clip);
  s_open = true;
  // Asked again at every open: the engine may show or hide the OS cursor between sessions.
  s_cursor_checked = 0;
  SetCursor(s_arrow);
  s_cg = cg_note_panel_shape(s_cg);
  memset(s_u4_gated, 0, sizeof s_u4_gated);
  s_u4_frames = s_u4_move = s_u4_input = s_u4_reasserts = s_u4_getcursorpos = s_u4_lbutton = 0;
  s_our_clipped = false;
  clip_assert();
  np_log("input: panel open (engine clip %s, window clip %s, cursor count %d, shape at open %p%s)",
         s_open_clipped ? "held" : "none", s_our_clipped ? "on" : "off: not in front", cursor_count(),
         gotShape ? (void*)ci.hCursor : 0, gotShape ? "" : " (GetCursorInfo failed)");
}

static void on_close(void) {
  s_open = false;
  log_u4("at close");
  const CgState c = s_cg;
  s_cg = cg_init();
  // Bounded: a runaway count would otherwise loop here with the game frozen.
  int steps = cg_close_replay(c), done = 0;
  for (; steps > 0 && done < 256; --steps, ++done) ShowCursor(TRUE);
  for (; steps < 0 && done < 256; ++steps, ++done) ShowCursor(FALSE);
  const int clip = cg_close_clip(c, s_open_clipped ? 1 : 0);
  const RECT engineRect = { c.clipL, c.clipT, c.clipR, c.clipB };
  if (clip == CG_CLIP_ENGINE_RECT) ClipCursor(&engineRect);
  else if (clip == CG_CLIP_AT_OPEN) ClipCursor(&s_open_clip);
  else ClipCursor(NULL);   // the engine's NULL, or ours lifted
  s_our_clipped = false;
  const int capture = cg_close_capture(c);
  if (capture == CG_CAPTURE_SET) SetCapture((HWND)c.capture);
  else if (capture == CG_CAPTURE_RELEASE) ReleaseCapture();
  // With the gate off: the DirectInput mouse is re-acquired if the engine still wants it.
  dimouse_close();
  // After the re-acquire, which leaves no WM_SETCURSOR to undo our arrow: the engine's shape goes back.
  void* shape = 0;
  const int shapeAct = cg_close_shape(c, &shape);
  if (shapeAct != CG_SHAPE_KEEP) {
    SetCursor((HCURSOR)shape);
    np_log("input: cursor shape %p restored (%s)", shape, shapeAct == CG_SHAPE_ENGINE ? "engine's SetCursor" : "at open");
  }
  CURSORINFO ci;
  ci.cbSize = sizeof ci;
  const BOOL got = GetCursorInfo(&ci);
  np_log("input: after close GetCursorInfo ok=%d flags=0x%lx hCursor=%p", got ? 1 : 0, got ? ci.flags : 0ul,
         got ? (void*)ci.hCursor : 0);
  np_log("input: panel closed: %d ShowCursor step(s) replayed%s, clip %s, capture %s, cursor %s; count now %d",
         done, steps ? " (capped at 256)" : "",
         clip == CG_CLIP_ENGINE_RECT ? "engine's rect" : clip == CG_CLIP_ENGINE_NULL ? "engine's NULL" :
         clip == CG_CLIP_AT_OPEN ? "restored from open" : "lifted",
         capture == CG_CAPTURE_SET ? "set" : capture == CG_CAPTURE_RELEASE ? "released" : "untouched", shapeAct == CG_SHAPE_KEEP ? "untouched" : "restored", cursor_count());
  s_open_clipped = false;
}

static void toggle(void) {
  if (s_open) {
    on_close();
  } else {
    // A panel nobody can see would swallow the game's input until the key was pressed blind.
    const char* const why = s_draw_off;
    bool logNow = false;
    if (!drawgate_may_open(&s_draw_gate, why, &logNow)) {
      if (logNow) np_log("input: panel not opened: drawing is disabled (%s failed)", why);
      return;
    }
    on_open();
  }
  clear_ui_state();
}

// ------------------------------------------------------------------ the WndProc
static void push_key(int vk, int repeat) {
  const int next = (s_key_head + 1) % 32;
  if (next == s_key_tail) return;
  // GetKeyState inside the WndProc is the state as of this message, not as of the frame that reads it.
  const int mods = (GetKeyState(VK_SHIFT) < 0 ? INPUT_MOD_SHIFT : 0) | (GetKeyState(VK_CONTROL) < 0 ? INPUT_MOD_CTRL : 0);
  s_keys[s_key_head].vk = vk; s_keys[s_key_head].repeat = repeat; s_keys[s_key_head].mods = mods;
  s_key_head = next;
}

static void push_char(unsigned ch) {
  const int next = (s_char_head + 1) % 32;
  if (next == s_char_tail) return;
  s_chars[s_char_head] = ch;
  s_char_head = next;
}

// Keyboard and mouse messages, the ones swallowed while the panel is open (WM_INPUT apart, below).
static bool is_input_message(UINT m) {
  return (m >= WM_KEYFIRST && m <= WM_KEYLAST) || (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST);
}

static void feed(UINT msg, WPARAM w, LPARAM l) {
  switch (msg) {
    case WM_KEYDOWN: case WM_SYSKEYDOWN: push_key((int)w, (l & (1 << 30)) ? 1 : 0); return;
    case WM_CHAR: push_char((unsigned)w); return;
    case WM_MOUSEWHEEL: s_wheel += GET_WHEEL_DELTA_WPARAM(w); return;   // screen coordinates: no position
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: s_btn[0] = true; s_press[0]++; break;
    case WM_LBUTTONUP:   s_btn[0] = false; break;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: s_btn[1] = true; s_press[1]++; break;
    case WM_RBUTTONUP:   s_btn[1] = false; break;
    case WM_MOUSEMOVE:   break;
    default: return;
  }
  s_client_x = (short)LOWORD(l);
  s_client_y = (short)HIWORD(l);
}

// What the engine is shown while closed, so open can release it.
static void track_held(UINT msg, WPARAM w) {
  switch (msg) {
    case WM_KEYDOWN: case WM_SYSKEYDOWN: s_held = keyhold_down(s_held, (int)w); break;
    case WM_KEYUP: case WM_SYSKEYUP:     s_held = keyhold_up(s_held, (int)w); break;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: s_held = keyhold_down(s_held, VK_LBUTTON); break;
    case WM_LBUTTONUP:   s_held = keyhold_up(s_held, VK_LBUTTON); break;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: s_held = keyhold_down(s_held, VK_RBUTTON); break;
    case WM_RBUTTONUP:   s_held = keyhold_up(s_held, VK_RBUTTON); break;
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: s_held = keyhold_down(s_held, VK_MBUTTON); break;
    case WM_MBUTTONUP:   s_held = keyhold_up(s_held, VK_MBUTTON); break;
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
      s_held = keyhold_down(s_held, GET_XBUTTON_WPARAM(w) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2); break;
    case WM_XBUTTONUP:
      s_held = keyhold_up(s_held, GET_XBUTTON_WPARAM(w) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2); break;
    default: break;
  }
}

// A bindable mouse button's message as its VK (bind.h's codes); 0 for anything else. Mouse 1 is never a bind.
static int speed_mouse_vk(UINT msg, WPARAM w) {
  switch (msg) {
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK: return VK_RBUTTON;
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK: return VK_MBUTTON;
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
      return GET_XBUTTON_WPARAM(w) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
    default: return 0;
  }
}

static LRESULT CALLBACK np_wndproc(HWND h, UINT msg, WPARAM w, LPARAM l) {
  const unsigned long ft = frame_thread_id(), t = GetCurrentThreadId();
  if (ft && t != ft && !InterlockedExchange(&s_wndproc_tid_logged, 1))
    np_log("input: THREAD MISMATCH: WndProc on tid=%lu, frame thread %lu", t, ft);
  const WNDPROC next = s_orig;
  if ((msg == WM_ACTIVATEAPP && !w) || (msg == WM_KILLFOCUS && h == s_wnd)) on_focus_lost();
  if (h == s_wnd && ((msg == WM_ACTIVATEAPP && !w) || msg == WM_KILLFOCUS ||
                     (msg == WM_ACTIVATE && LOWORD(w) == WA_INACTIVE)))
    hold_set(false, "focus lost");   // before the engine's handler: alt-tab or Start gets the cursor at once
  // Before the engine sees it: the overlay state here is the one its focus-loss branch reads.
  if ((msg == WM_ACTIVATEAPP || msg == WM_ACTIVATE) && h == s_wnd &&
      (++s_focus_msgs <= 64 || (s_focus_msgs & 63u) == 0))
    np_log("focus: %s %d (#%u) foreground in front=%d overlay=%d borderless=%d panel=%d",
           msg == WM_ACTIVATE ? "WM_ACTIVATE" : "WM_ACTIVATEAPP", msg == WM_ACTIVATE ? (int)LOWORD(w) : (int)w,
           s_focus_msgs, game_front(), display_steam_overlay(), display_borderless_on(), s_open ? 1 : 0);
  if (msg == WM_MOUSEMOVE) s_u4_move++;
  else if (msg == WM_INPUT) s_u4_input++;
  else if (msg == WM_LBUTTONDOWN) s_u4_lbutton++;

  // An armed bind row gets the panel key too (AA), so the current key can be chosen again.
  if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && (int)w == s_key && h == s_wnd &&
      !(s_open && menu_keycap_armed())) {
    if (!(l & (1 << 30))) toggle();       // bit 30: auto-repeat of a held key
    return 0;                             // never reaches the game
  }

  // Held, the engine gets no mouse message, as under its exclusive mouse: its ViewportWndProc queues button and
  // wheel events from them on top of the DirectInput ones. Before the Game speed key, which never saw them either.
  if (!s_open && h == s_wnd && s_hold) {
    if (msg == WM_SETCURSOR && LOWORD(l) == HTCLIENT) { SetCursor(NULL); return TRUE; }
    if (cl_swallows(msg, 1)) {
      s_hold_swallowed++;
      // Side buttons: DefWindowProc would turn them into browser back/forward, which the exclusive mouse never made.
      if (msg >= WM_XBUTTONDOWN && msg <= WM_XBUTTONDBLCLK) return TRUE;
      return DefWindowProcW(h, msg, w, l);
    }
  }

  // Alt+F4 and Alt+Tab stay the system's (bit 29: Alt held).
  const bool altPass = (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) && (l & (1 << 29)) &&
                       (w == VK_F4 || w == VK_TAB);

  // The Game speed key while the panel is shut (open, the panel owns every key); repeats are swallowed too.
  // Never over Alt+F4/Alt+Tab: a speed key bound to F4 or Tab must not keep the game from closing or switching.
  if (!s_open && !altPass && (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) &&
      hotkey_swallows((int)w, config_bind_get(BIND_ROW_GAMESPEED), true, false, s_open)) {
    if (!(l & (1 << 30))) s_speed_presses++;
    return 0;
  }
  const int mvk = speed_mouse_vk(msg, w);
  if (!s_open && mvk && hotkey_swallows(mvk, config_bind_get(BIND_ROW_GAMESPEED), true, false, s_open)) {
    if (msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN) s_speed_presses++;
    return 0;
  }
  // The free camera's keys with the panel shut: the Camera key while the switch arms it, Copy and Paste view while
  // flying. A mouse-bound one is polled by freecam.cpp instead.
  const bool keyDown = !s_open && !altPass && (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
  if (keyDown && hotkey_swallows((int)w, config_bind_get(BIND_ROW_CAMERA), g_cfg.freecamOn != 0, false, s_open)) {
    if (!(l & (1 << 30))) {
      s_cam_presses++;
      freecam_camera_key_now();   // RendMap back now: the frame path may be the thing that stopped
    }
    return 0;
  }
  if (keyDown && s_fly) {
    if (hotkey_swallows((int)w, config_bind_get(BIND_ROW_CAMCOPY), true, false, s_open)) {
      if (!(l & (1 << 30))) s_copy_presses++;
      return 0;
    }
    if (hotkey_swallows((int)w, config_bind_get(BIND_ROW_CAMPASTE), true, false, s_open)) {
      if (!(l & (1 << 30))) s_paste_presses++;
      return 0;
    }
    if (hotkey_swallows((int)w, config_bind_get(BIND_ROW_TELEPORT), true, false, s_open)) {
      if (!(l & (1 << 30))) s_tp_presses++;
      return 0;
    }
    if (hotkey_swallows((int)w, config_bind_get(BIND_ROW_PHOTO), true, false, s_open)) {
      if (!(l & (1 << 30))) s_photo_presses++;
      return 0;
    }
  }

  if (s_open) {
    if (msg == WM_SETCURSOR && LOWORD(l) == HTCLIENT) { SetCursor(s_arrow); return TRUE; }
    // Swallowed from the engine; DefWindowProc still frees the raw input buffer.
    if (msg == WM_INPUT) return DefWindowProcW(h, msg, w, l);
    if (is_input_message(msg) && !altPass) { feed(msg, w, l); return 0; }
  } else {
    // Flying, every key and button is the camera's: none reaches the engine (Alt cannot open the system menu).
    if (s_fly && is_input_message(msg) && !altPass) {
      s_fly_swallowed++;
      return 0;
    }
    track_held(msg, w);
    // Not in front, the engine's WM_MOUSEMOVE would recentre the cursor and read the offset as mouse look.
    if (msg == WM_MOUSEMOVE && h == s_wnd && bl_bg_hold(display_borderless_active(), game_front())) {
      s_bg_moves++;
      s_bg_moves_total++;
      return DefWindowProcW(h, msg, w, l);
    }
  }

  // The engine's handlers of these persist StartupFullscreen (display.h fs_watch_msg): read around the call.
  const bool fsWatch = h == s_wnd && fs_watch_msg(msg, (unsigned)w);
  const int fsBefore = fsWatch ? display_startup_fullscreen() : -1;
  const LRESULT r = next ? CallWindowProcW(next, h, msg, w, l) : DefWindowProcW(h, msg, w, l);
  if (fsWatch)
    display_fs_message(msg, (unsigned)w, fsBefore, display_startup_fullscreen(), game_front(), IsIconic(h) ? 1 : 0);
  // Focus edges, after the engine's handler: its volume-to-0 has run, so Keep running puts the sound back after it.
  // Active only from WM_ACTIVATE: its handler runs the resume; WM_ACTIVATEAPP(1) arrives before it.
  if (h == s_wnd && (msg == WM_ACTIVATE || msg == WM_ACTIVATEAPP)) {
    const int active = msg == WM_ACTIVATE ? LOWORD(w) != WA_INACTIVE : w != 0;
    if (!active || msg == WM_ACTIVATE) s_activated = active;
    if (!active) focuskeep_after_focus_loss();
  }
  if (msg == WM_NCDESTROY && h == s_wnd) {
    display_fs_exit();
    np_log("input: window %p destroyed; the next device window is subclassed", (void*)h);
    // Closed first: an open panel with no window would hold the engine's cursor calls for good. The DirectInput
    // device is forgotten before, so close calls nothing on a device the engine may already have released.
    dimouse_forget();
    hold_set(false, "window destroyed");
    if (s_open) { on_close(); clear_ui_state(); }
    s_wnd = NULL;
    s_orig = NULL;
  }
  return r;
}

// The device's focus window. A different one (a new device) moves the subclass, putting the old proc back.
static void try_install(HWND h) {
  if (!h || !IsWindow(h)) return;
  hold_set(false, "window replaced");
  if (s_wnd && IsWindow(s_wnd) && (WNDPROC)GetWindowLongPtrW(s_wnd, GWLP_WNDPROC) == np_wndproc)
    SetWindowLongPtrW(s_wnd, GWLP_WNDPROC, (LONG_PTR)s_orig);
  s_wnd = NULL;
  s_orig = NULL;
  const DWORD wndTid = GetWindowThreadProcessId(h, NULL);
  if (wndTid != GetCurrentThreadId())
    np_log("input: THREAD MISMATCH: window %p belongs to tid=%lu, Present runs on tid=%lu", (void*)h, wndTid,
           GetCurrentThreadId());
  SetLastError(0);
  const LONG_PTR prev = GetWindowLongPtrW(h, GWLP_WNDPROC);
  s_orig = (WNDPROC)prev;   // stored before the swap: a message may arrive inside SetWindowLongPtrW
  s_wnd = h;
  if (!SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)np_wndproc) && GetLastError()) {
    np_log("input: SetWindowLongPtrW on %p failed (%lu); no panel input", (void*)h, GetLastError());
    s_wnd = NULL;
    s_orig = NULL;
    return;
  }
  s_activated = 1;   // a new window is active until its own WM_ACTIVATE says otherwise
  np_log("input: subclassed window %p (tid=%lu unicode=%d), previous proc %p", (void*)h, wndTid,
         IsWindowUnicode(h) ? 1 : 0, (void*)prev);
}

// ------------------------------------------------------------------ the frame
void input_frame(int W, int H) {
  static bool gateTried;
  if (!gateTried) {
    gateTried = true;
    s_arrow = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    install_gate();
  }
  const HWND want = d3dcap_window();
  if (want && want != s_wnd) try_install(want);
  focus_frame();
  display_fs_frame();
  dimouse_frame();
  hold_frame();
  if (!s_wnd || !s_open) return;

  clip_assert();
  if (++s_u4_frames >= 600) log_u4("open");
  RECT rc;
  if (GetClientRect(s_wnd, &rc)) {
    s_cx = cursor_scale(s_client_x, rc.right - rc.left, W);
    s_cy = cursor_scale(s_client_y, rc.bottom - rc.top, H);
  }
  if (!s_cursor_checked) {
    s_cursor_checked = 1;
    CURSORINFO ci;
    ci.cbSize = sizeof ci;
    const BOOL ok = GetCursorInfo(&ci);
    const int showing = ok && (ci.flags & CURSOR_SHOWING);
    s_soft_cursor = showing ? 0 : 1;
    np_log("input: GetCursorInfo ok=%d flags=0x%lx showing=%d count=%d -> software_cursor=%d",
           ok ? 1 : 0, ok ? ci.flags : 0ul, showing, cursor_count(), s_soft_cursor);
  }
}

bool input_panel_open(void) { return s_open; }
HWND input_window(void) { return s_wnd; }
int input_game_front(void) { return s_wnd && s_activated && GetForegroundWindow() == s_wnd ? 1 : 0; }
void input_cursor(int* x, int* y) { *x = s_cx; *y = s_cy; }
bool input_mouse_down(int button) { return button >= 0 && button < 2 && s_btn[button]; }
int input_press_take(int button) {
  if (button < 0 || button > 1) return 0;
  const int n = s_press[button];
  s_press[button] = 0;
  return n;
}
int input_gamespeed_take(void) {
  const int n = s_speed_presses;
  s_speed_presses = 0;
  return n;
}
int input_wheel_take(void) { const int w = s_wheel; s_wheel = 0; return w; }
int input_camera_take(void) { const int n = s_cam_presses; s_cam_presses = 0; return n; }
int input_camcopy_take(void) { const int n = s_copy_presses; s_copy_presses = 0; return n; }
int input_campaste_take(void) { const int n = s_paste_presses; s_paste_presses = 0; return n; }
int input_teleport_take(void) { const int n = s_tp_presses; s_tp_presses = 0; return n; }
int input_photo_take(void) { const int n = s_photo_presses; s_photo_presses = 0; return n; }

// DNMP's pair, on this port's cursor: our hold and window clip let go, so the dialog gets a free cursor; the engine's
// own clip is its own and is re-applied when it takes the mouse back.
static int s_modal_shows;
void input_modal_begin(void) {
  hold_set(false, "modal dialog");
  RECT have;
  if (s_our_clipped && GetClipCursor(&have) &&
      clip_rect_same(have.left, have.top, have.right, have.bottom, s_our_clip.left, s_our_clip.top, s_our_clip.right,
                     s_our_clip.bottom))
    ClipCursor(NULL);
  s_our_clipped = false;
  s_modal_shows = 0;
  for (int c = cursor_count(); c < 0 && s_modal_shows < 64; ++s_modal_shows) c = ShowCursor(TRUE);
  SetCursor(s_arrow ? s_arrow : LoadCursor(NULL, IDC_ARROW));
  np_log("input: modal dialog open: %d show step(s)", s_modal_shows);
}

void input_modal_end(void) {
  for (int i = 0; i < s_modal_shows; ++i) ShowCursor(FALSE);
  clear_ui_state();
  POINT pt;
  if (s_wnd && GetCursorPos(&pt) && ScreenToClient(s_wnd, &pt)) { s_client_x = pt.x; s_client_y = pt.y; }
  if (s_open) SetCursor(s_arrow);
  np_log("input: modal dialog closed: %d show step(s) undone", s_modal_shows);
  s_modal_shows = 0;
}
unsigned input_focus_losses(void) { return s_focus_losses; }

// The engine reads its keys by polling GetKeyState (docs/re/input.md, UpdateInput), so the gate's zeros release a
// key held at the start on its next poll; nothing is sent into its window proc from the frame path. The keys it saw
// held are forgotten: their real releases are swallowed while flying.
void input_set_flight(int on) {
  on = on ? 1 : 0;
  if (on == s_fly) return;
  const int held = keyhold_count(s_held);
  if (on) s_held = keyhold_init();
  s_fly = on;
  dimouse_set_flight(on);
  np_log("input: flight gate %s (%d key(s) the engine held, released by its next poll; %u message(s) swallowed in "
         "all flights)", on ? "on" : "off", on ? held : 0, s_fly_swallowed);
}

bool input_key_take(int* vk, int* repeat, int* mods) {
  if (s_key_tail == s_key_head) return false;
  *vk = s_keys[s_key_tail].vk; *repeat = s_keys[s_key_tail].repeat; *mods = s_keys[s_key_tail].mods;
  s_key_tail = (s_key_tail + 1) % 32;
  return true;
}

bool input_char_take(unsigned* ch) {
  if (s_char_tail == s_char_head) return false;
  *ch = s_chars[s_char_tail];
  s_char_tail = (s_char_tail + 1) % 32;
  return true;
}

int input_software_cursor(void) { return s_soft_cursor; }

void input_set_draw_disabled(const char* reason) {
  InterlockedExchangePointer((void* volatile*)&s_draw_off, (void*)reason);
  // Through the normal close edge, so the engine gets its mouse, cursor and clip back.
  if (drawgate_set(&s_draw_gate, reason, s_open)) {
    np_log("input: panel closed: drawing is disabled (%s failed)", reason);
    on_close();
    clear_ui_state();
  }
}

void input_set_panel_key(int vk) {
  const int key = panel_key_effective(vk);
  if (key != s_key) np_log("input: panel key 0x%02X (asked 0x%02X)", key, vk);
  s_key = key;
}

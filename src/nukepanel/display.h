#pragma once
// Borderless window at launch (docs/re/display.md): pure rules, then the glue in display.cpp.
// The engine keeps believing it runs fullscreen; d3d9 gets a windowed device at the game's own back-buffer
// size, and the device window becomes a popup covering its monitor. Read once at launch: a toggle waits a restart.
#include <windows.h>
#include <d3d9.h>

struct BlRect { int l, t, r, b; };
struct BlPos { int x, y, cx, cy; };

enum { BL_SWP_NOSIZE = 0x1, BL_SWP_NOMOVE = 0x2 };
// WS_POPUP|WS_VISIBLE, ex style 0 (DNMP's display.h values).
static const unsigned BL_STYLE_POPUP = 0x90000000u;
static const unsigned BL_EXSTYLE_POPUP = 0u;

// Overlap area of two rects, 0 when disjoint. 64-bit: two 32k-pixel sides overflow an int.
inline long long bl_overlap(BlRect a, BlRect b) {
  const int l = a.l > b.l ? a.l : b.l, r = a.r < b.r ? a.r : b.r;
  const int t = a.t > b.t ? a.t : b.t, bo = a.b < b.b ? a.b : b.b;
  if (r <= l || bo <= t) return 0;
  return (long long)(r - l) * (long long)(bo - t);
}

// The monitor holding most of the window; none overlapping -> primary (when valid), else 0; -1 if n <= 0.
inline int bl_pick_monitor(const BlRect* mons, int n, BlRect win, int primary) {
  if (n <= 0) return -1;
  int best = -1;
  long long bestArea = 0;
  for (int i = 0; i < n; i++) {
    const long long a = bl_overlap(mons[i], win);
    if (a > bestArea) { bestArea = a; best = i; }
  }
  if (best >= 0) return best;
  return primary >= 0 && primary < n ? primary : 0;
}

// D3DDrv9 fills Windowed = !fullscreen (FD3D_FillPresentParams D3DDrv9!0x100057e0): a fullscreen request becomes
// windowed with no refresh rate (d3d9 rejects one when windowed); the back-buffer size and the rest stay the game's.
// An engine that asks for windowed is left as it is. True when pp was changed.
inline bool bl_rewrite_pp(int on, D3DPRESENT_PARAMETERS* pp) {
  if (!on || !pp || pp->Windowed) return false;
  pp->Windowed = TRUE;
  pp->FullScreen_RefreshRateInHz = 0;
  return true;
}

// The engine's struct is never handed to d3d9 rewritten: d3d9 gets our copy, and only the fields d3d9 itself may
// fill in (back-buffer size, format, count) are copied back to the engine's.
inline void bl_params_back(D3DPRESENT_PARAMETERS* eng, const D3DPRESENT_PARAMETERS* ours) {
  eng->BackBufferWidth = ours->BackBufferWidth;
  eng->BackBufferHeight = ours->BackBufferHeight;
  eng->BackBufferFormat = ours->BackBufferFormat;
  eng->BackBufferCount = ours->BackBufferCount;
}

// An engine GWL_STYLE write while borderless is active. ResizeViewport (Engine.dll!0x102BB410) clears WS_CAPTION
// (style & 0xFF3FFFFF) entering fullscreen (ours wins) and ORs it back leaving (bl_leave_style: the engine is going
// windowed, and borderless idles until its next fullscreen device).
enum { BL_STYLE_KEEP = 0, BL_STYLE_OURS = 1, BL_STYLE_LEAVE = 2 };
inline int bl_style_rule(int active, unsigned requested) {
  if (!active) return BL_STYLE_KEEP;
  if ((requested & 0x00C00000u) == 0x00C00000u) return BL_STYLE_LEAVE;   // WS_CAPTION
  return BL_STYLE_OURS;
}
// Our popup style, keeping WS_MINIMIZE so a style write never un-minimises the window behind the shell's back.
inline unsigned bl_popup_style(unsigned requested) { return BL_STYLE_POPUP | (requested & 0x20000000u); }
// Leaving fullscreen: the window's own style from before our restyle (saved), its minimise state as the engine
// has it now, and the caption the engine adds back; never our popup with a caption on it.
inline unsigned bl_leave_style(unsigned saved, unsigned requested) {
  return (saved & ~0x20000000u) | (requested & 0x20000000u) | 0x00C00000u;
}

// A failed windowed CreateDevice/Reset: only a refusal of the parameters falls back to the engine's exclusive
// ones. DEVICELOST (D3DDrv9 retries it every 300 ms, D3DDrv9!0x10002180) and the rest go back to the engine as is.
inline bool bl_fallback(HRESULT hr) { return hr == D3DERR_INVALIDCALL || hr == D3DERR_NOTAVAILABLE; }

// An engine move or size of the game window while active: pinned to the monitor, each part only when the call
// moves or sizes. A minimise (-32000,-32000) is left alone. True when p changed.
inline bool bl_fix_pos(unsigned swpFlags, BlPos* p, BlRect mon) {
  if (p->x == -32000 && p->y == -32000) return false;
  bool changed = false;
  if (!(swpFlags & BL_SWP_NOMOVE) && (p->x != mon.l || p->y != mon.t)) {
    p->x = mon.l; p->y = mon.t; changed = true;
  }
  const int w = mon.r - mon.l, h = mon.b - mon.t;
  if (!(swpFlags & BL_SWP_NOSIZE) && (p->cx != w || p->cy != h)) {
    p->cx = w; p->cy = h; changed = true;
  }
  return changed;
}

// Borderless, game window not in front. With the Steam overlay on, the engine's focus loss keeps its fullscreen flag,
// so its WM_MOUSEMOVE recentres the cursor (Engine.dll!0x102BD7FC) on every move there. Held back then: those mouse
// moves and the engine's cursor pins (SetCursorPos, a ClipCursor rect); its release, ClipCursor(NULL), passes.
inline bool bl_bg_hold(int on, int front) { return on && !front; }
inline bool bl_bg_hold_clip(int on, int front, int rectIsNull) { return bl_bg_hold(on, front) && !rectIsNull; }

// StartupFullscreen watch (docs/re/display.md "Windowed at launch"). Only BeginFullscreen/EndFullscreen
// (Engine.dll!0x102BB370/ 0x102B9ED0) write the flag to system.ini; ViewportWndProc (0x102BC410) reaches them from a
// handful of messages, so the flag is read before and after each. Other input never: the reads stay off the hot path.
inline bool fs_watch_msg(unsigned msg, unsigned w) {
  switch (msg) {
    case WM_ACTIVATE: case WM_ACTIVATEAPP: case WM_DESTROY: case WM_CLOSE: case WM_SIZE: case WM_SYSCOMMAND:
      return true;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: return w == VK_RETURN || w == VK_F4;
    default: return false;
  }
}
// The window closing is logged whatever the flag does: WM_CLOSE goes to DefWindowProc (DestroyWindow), and
// WM_DESTROY ends fullscreen while the viewport is live, so the exit decides what the next launch reads.
inline bool fs_always_log(unsigned msg, unsigned w) {
  return msg == WM_CLOSE || msg == WM_DESTROY || (msg == WM_SYSCOMMAND && (w & 0xFFF0u) == SC_CLOSE);
}
// The flag before and after (1, 0, or -1 unreadable).
enum { FS_SAME = 0, FS_ENDED = 1, FS_BEGAN = 2, FS_UNREAD = 3 };
inline int fs_change(int before, int after) {
  if (before < 0 && after < 0) return FS_SAME;
  if (before < 0 || after < 0) return FS_UNREAD;
  if (before == after) return FS_SAME;
  return after ? FS_BEGAN : FS_ENDED;
}
inline const char* fs_msg_name(unsigned msg) {
  switch (msg) {
    case WM_ACTIVATE: return "WM_ACTIVATE";
    case WM_ACTIVATEAPP: return "WM_ACTIVATEAPP";
    case WM_DESTROY: return "WM_DESTROY";
    case WM_CLOSE: return "WM_CLOSE";
    case WM_SIZE: return "WM_SIZE";
    case WM_SYSCOMMAND: return "WM_SYSCOMMAND";
    case WM_KEYDOWN: return "WM_KEYDOWN";
    case WM_SYSKEYDOWN: return "WM_SYSKEYDOWN";
    default: return "message";
  }
}

// ------------------------------------------------------------------ glue (display.cpp)
// From np_first_call, before d3dcap_install: peeks [display] borderless, logs it, and when on patches
// Engine.dll's USER32 import slots for the game window's style, position and ShowWindow.
void display_boot(void);
// The present parameters d3d9 gets: pp itself, or *scratch rewritten (bl_rewrite_pp) when borderless is on and
// the engine asks fullscreen; then the device window is restyled before the real call. `what` names the call.
D3DPRESENT_PARAMETERS* display_params(D3DPRESENT_PARAMETERS* pp, D3DPRESENT_PARAMETERS* scratch, HWND wnd,
                                      const char* what);
// After the real call with display_params' result: copies d3d9's write-backs to the engine's struct, logs, and
// returns true when the caller must retry with the engine's own pp (d3d9 refused the windowed params: bl_fallback).
bool display_after(D3DPRESENT_PARAMETERS* pp, D3DPRESENT_PARAMETERS* used, HRESULT hr, const char* what);
// Borderless was on at launch (the session's value).
int display_borderless_on(void);
// Borderless is in force now: on at launch and the engine has not left fullscreen (its windowed mode is stock).
int display_borderless_active(void);
// SteamUtils()->IsOverlayEnabled() now: 1, 0, or -1 when it cannot be asked. The engine asks it at each focus loss.
int display_steam_overlay(void);
// A line for the Settings > DISPLAY row, or NULL when there is nothing to say.
const char* display_note(void);
// The live client's StartupFullscreen (GEngine.Client, both by reflection): 1, 0, or -1 when not resolved yet or
// unreadable. Game thread; never resolves (only display_fs_frame does), so it is safe inside the WndProc.
int display_startup_fullscreen(void);
// The WndProc, after the engine handled a watched message (fs_watch_msg): logs a change, or the window closing.
void display_fs_message(unsigned msg, unsigned w, int before, int after, int front, int iconic);
// Once a frame from input_frame: resolves the offsets, then every 60 frames logs a change made between messages
// (a console command or script: Exec's BeginFullscreen/EndFullscreen/ToggleFullscreen).
void display_fs_frame(void);
// The game window's WM_NCDESTROY: the flag the next launch starts with, and the engine's mode at that moment.
void display_fs_exit(void);

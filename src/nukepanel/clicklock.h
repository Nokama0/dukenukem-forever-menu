#pragma once
// Alt-tab click lock (docs/re/input.md "Click lock after alt-tab"): pure rules for dimouse.cpp and input.cpp.
// The sibling DNMP port measured the cause: dinput8's exclusive mouse registers the raw-input row
// NOLEGACY|CAPTUREMOUSE asynchronously, and focus leaving mid-change leaves clicks dead system-wide.
#include <windows.h>
#include <stdio.h>
#include <string.h>

// The engine mouse's cooperative level (DISCL_*): EXCLUSIVE (1) becomes NONEXCLUSIVE (2), FOREGROUND and the rest
// kept, so dinput8 never registers NOLEGACY. Only the classified engine mouse; anything else is passed as asked.
inline unsigned cl_coop_level(int isMouse, unsigned flags) {
  if (!isMouse || !(flags & 1u)) return flags;
  return (flags & ~1u) | 2u;
}

// The cursor hold: with the mouse shared, the OS cursor is hidden over the client (SetCursor(NULL) on WM_SETCURSOR) and
// put back at the client centre every frame, and the window gets no mouse messages, as under the exclusive mouse. It
// yields whenever the engine has released its mouse (SetMouseCapture(0)) or the OS cursor is showing, and to the panel.
struct ClHoldIn { int window, foreground, iconic, panelOpen, engineReleased, cursorShown; };
enum { CL_HOLD_OK = 0, CL_HOLD_NO_WINDOW, CL_HOLD_BACKGROUND, CL_HOLD_ICONIC, CL_HOLD_PANEL, CL_HOLD_ENGINE_RELEASED,
       CL_HOLD_CURSOR_SHOWN };
// The first condition that stops the hold, CL_HOLD_OK when it holds. Focus first, so the log names alt-tab.
inline int cl_hold_block(const ClHoldIn& in) {
  if (!in.window) return CL_HOLD_NO_WINDOW;
  if (!in.foreground) return CL_HOLD_BACKGROUND;
  if (in.iconic) return CL_HOLD_ICONIC;
  if (in.panelOpen) return CL_HOLD_PANEL;
  if (in.engineReleased) return CL_HOLD_ENGINE_RELEASED;
  if (in.cursorShown) return CL_HOLD_CURSOR_SHOWN;
  return CL_HOLD_OK;
}
inline const char* cl_hold_name(int block) {
  static const char* const kN[7] = { "held", "no window", "not in front", "minimised", "panel open",
                                     "engine released its mouse", "OS cursor showing" };
  return block >= 0 && block < 7 ? kN[block] : "?";
}
// The hold kept off in play (in front, panel shut) by the engine's release or a showing cursor: one log line per
// stretch once it has lasted 5 s, so a cursor left showing names its reason.
struct ClHoldWatch { int block; unsigned long long since; int logged; };
inline ClHoldWatch cl_watch_init(void) { ClHoldWatch w = { CL_HOLD_OK, 0, 0 }; return w; }
inline int cl_hold_watch(ClHoldWatch* w, int block, unsigned long long nowMs) {
  const int watched = block == CL_HOLD_ENGINE_RELEASED || block == CL_HOLD_CURSOR_SHOWN;
  if (!watched || block != w->block) {
    w->block = block;
    w->since = nowMs;
    w->logged = 0;
    return 0;
  }
  if (w->logged || nowMs - w->since < 5000) return 0;
  w->logged = 1;
  return 1;
}
// The centre of a rect; false, outputs untouched, when it is empty.
inline bool cl_hold_centre(long l, long t, long r, long b, long* x, long* y) {
  if (r <= l || b <= t) return false;
  *x = l + (r - l) / 2;
  *y = t + (b - t) / 2;
  return true;
}
// While held, every WM_MOUSE* (0x200..0x20E) is kept from the engine: ViewportWndProc queues button and wheel
// events from them on top of the DirectInput ones (a wheel notch would switch weapons twice).
inline bool cl_swallows(unsigned msg, int held) { return held && msg >= 0x200u && msg <= 0x20Eu; }

// Raw-input rows on usage page 1: the mouse (2) and the keyboard (6).
enum { CL_RAW_MOUSE = 2, CL_RAW_KEYBOARD = 6 };
// The row for usage among GetRegisteredRawInputDevices' got rows (-1: the call failed with err), with the flags
// dinput8's table uses named: 0x230 is the exclusive mouse, 0x100 the nonexclusive one (duke-dnmp hotkeys.h).
inline void cl_raw_row(const RAWINPUTDEVICE* rows, int got, unsigned long err, int usage, char* out, size_t cap) {
  if (got < 0) { _snprintf_s(out, cap, _TRUNCATE, "unread (err %lu)", err); return; }
  for (int i = 0; i < got; i++) {
    if (rows[i].usUsagePage != 1 || rows[i].usUsage != usage) continue;
    const unsigned long f = rows[i].dwFlags;
    char names[96] = "";   // every named bit set needs 68 + NUL
    const struct { unsigned long bit; const char* name; } kNames[] = {
      { 0x1, "REMOVE" }, { 0x30, "NOLEGACY" }, { 0x100, "INPUTSINK" },
      { 0x200, usage == CL_RAW_MOUSE ? "CAPTUREMOUSE" : "NOHOTKEYS" }, { 0x400, "APPKEYS" },
      { 0x1000, "EXINPUTSINK" }, { 0x2000, "DEVNOTIFY" } };
    for (size_t k = 0; k < sizeof kNames / sizeof kNames[0]; k++) {
      if ((f & kNames[k].bit) != kNames[k].bit) continue;
      if (names[0]) strcat_s(names, sizeof names, "|");
      strcat_s(names, sizeof names, kNames[k].name);
    }
    _snprintf_s(out, cap, _TRUNCATE, "flags 0x%lx%s%s target %p", f, names[0] ? " " : "", names,
                (void*)rows[i].hwndTarget);
    return;
  }
  _snprintf_s(out, cap, _TRUNCATE, "none registered (%d rows)", got);
}

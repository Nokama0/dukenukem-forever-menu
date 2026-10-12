#pragma once
// Pure rules for the cursor import-slot gate (input.cpp) and the keys the engine saw held. Windows-free so the
// suite pins them. Flags are int, not bool: MSVC 19.50 /O2 folds byte-sized bool members of structs returned
// by value from inline chains (an MSVC 19.50 /O2 constant-folding bug).

// The engine's cursor and key-state calls, by the user32 import they come through (Engine.dll and dnCommon.dll).
// The kinds below CG_KINDS are gated; GetCursorPos only counts its calls (CG_SLOTS covers both).
enum { CG_SETCURSORPOS = 0, CG_CLIPCURSOR, CG_SHOWCURSOR, CG_SETCURSOR, CG_SETCAPTURE, CG_GETKEYSTATE,
       CG_RELEASECAPTURE, CG_KINDS, CG_GETCURSORPOS = CG_KINDS, CG_SLOTS };

// Closed, every call goes through. Open, the cursor is ours (no recentring, clipping, hiding or capture) and
// GetKeyState reads 0 for every key, so a held key moves nothing behind the panel. GetCursorPos always passes.
inline bool cursor_gate_passes(int kind, bool open) { return kind == CG_GETCURSORPOS || !open; }
// Open, the panel clips the cursor to the game window itself: even the engine's ClipCursor(NULL) is held back,
// recorded for close.
inline bool cursor_gate_passes_clip(bool open, bool rectIsNull) { (void)rectIsNull; return !open; }
// GetKeyState also reads 0 for every key while the free camera flies (Duke stands, no binding fires), and for the
// armed Camera key always: the engine polls all 256 keys, so a swallowed WM_KEYDOWN alone would still reach it.
inline bool keystate_passes(int vk, bool open, bool flying, int hiddenVk) {
  return !open && !flying && !(hiddenVk && vk == hiddenVk);
}

// What the engine asked for while the panel held its calls back, for close to replay.
struct CgState {
  int pendingShow;                              // net ShowCursor(TRUE) minus ShowCursor(FALSE) held back
  int clipCalls, clipNull, clipL, clipT, clipR, clipB;   // the last ClipCursor while open
  int captureCalls; void* capture;              // the last SetCapture (NULL: ReleaseCapture) while open
  int cursorCalls; void* cursor;                // the last SetCursor while open
  int shapeKnown; void* shapeAtOpen;            // GetCursorInfo's hCursor at open, when it answered
  int shapeChanged;                             // the panel set its own arrow while open
};
inline CgState cg_init(void) { CgState s = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; return s; }

inline CgState cg_on_showcursor(CgState s, bool open, int show) {
  if (open) s.pendingShow += show ? 1 : -1;
  return s;
}
// The display count a gated ShowCursor answers: the one the engine would have reached, so a
// `while (ShowCursor(FALSE) >= 0)` loop ends instead of spinning against an unchanged count.
inline int cg_virtual_count(CgState s, int realCount) { return realCount + s.pendingShow; }
// ShowCursor steps close applies (positive: TRUE calls), leaving the counter where the engine expects it.
inline int cg_close_replay(CgState s) { return s.pendingShow; }
// Engine.dll's ShowCursor(TRUE) calls are all "until visible" loops (SetMouseCapture(0)), so a release with the cursor
// already visible raised the count (an alt-tab left it at 2). Such a call (count before it >= 0) is answered without
// counting; so is a hide with count < 0, which floors at -1 so ShutdownAfterError's ShowCursor(TRUE) shows it.
inline int cg_show_redundant(int show, int countBefore) { return show ? countBefore >= 0 : countBefore < 0; }

inline CgState cg_on_clip(CgState s, bool open, bool rectIsNull, int l, int t, int r, int b) {
  if (!open) return s;
  s.clipCalls++;
  s.clipNull = rectIsNull ? 1 : 0;
  s.clipL = l; s.clipT = t; s.clipR = r; s.clipB = b;
  return s;
}
// What close does with the clip: the engine's last gated call, else the clip it had in force at open, else
// nothing of the engine's to keep, so the panel's own window clip is lifted.
enum { CG_CLIP_ENGINE_RECT = 0, CG_CLIP_ENGINE_NULL, CG_CLIP_AT_OPEN, CG_CLIP_LIFT };
inline int cg_close_clip(CgState s, int clippedAtOpen) {
  if (s.clipCalls) return s.clipNull ? CG_CLIP_ENGINE_NULL : CG_CLIP_ENGINE_RECT;
  return clippedAtOpen ? CG_CLIP_AT_OPEN : CG_CLIP_LIFT;
}
// The panel re-clips only when the clip in force is not its own window rect (moved, resized, focus back).
inline bool clip_rect_same(int l, int t, int r, int b, int l2, int t2, int r2, int b2) {
  return l == l2 && t == t2 && r == r2 && b == b2;
}

inline CgState cg_on_capture(CgState s, bool open, void* wnd) {
  if (open) { s.captureCalls++; s.capture = wnd; }
  return s;
}
// What close does with capture: nothing asked while open, the engine's last SetCapture, or its ReleaseCapture.
enum { CG_CAPTURE_UNTOUCHED = 0, CG_CAPTURE_SET, CG_CAPTURE_RELEASE };
inline int cg_close_capture(CgState s) {
  if (!s.captureCalls) return CG_CAPTURE_UNTOUCHED;
  return s.capture ? CG_CAPTURE_SET : CG_CAPTURE_RELEASE;
}
// The engine hides the cursor by shape: SetCursor(0) with the display count >= 0 (input.md). Open records the shape in
// force; once the panel has set its arrow, close puts a shape back (the engine's own SetCursor held back while open,
// else the one recorded at open), since the re-acquired DirectInput mouse sends no WM_SETCURSOR to undo the arrow.
enum { CG_SHAPE_KEEP = 0, CG_SHAPE_ENGINE, CG_SHAPE_AT_OPEN };
inline CgState cg_on_open_shape(CgState s, void* shape, int known) {
  s.shapeKnown = known ? 1 : 0;
  s.shapeAtOpen = known ? shape : 0;
  return s;
}
inline CgState cg_note_panel_shape(CgState s) { s.shapeChanged = 1; return s; }
inline int cg_close_shape(CgState s, void** shape) {
  if (s.cursorCalls) { *shape = s.cursor; return CG_SHAPE_ENGINE; }
  if (s.shapeChanged && s.shapeKnown) { *shape = s.shapeAtOpen; return CG_SHAPE_AT_OPEN; }
  return CG_SHAPE_KEEP;
}

inline CgState cg_on_setcursor(CgState s, bool open, void* cur) {
  if (open) { s.cursorCalls++; s.cursor = cur; }
  return s;
}

// Virtual keys (and mouse-button codes 1..6) the engine has seen go down and not yet up. Opening the panel
// releases them to the engine, so nothing it held stays held while the panel swallows the key-up.
struct KeyHold { unsigned bits[8]; };
inline KeyHold keyhold_init(void) { KeyHold k = { { 0 } }; return k; }
inline KeyHold keyhold_down(KeyHold k, int vk) {
  if (vk >= 1 && vk <= 255) k.bits[vk >> 5] |= 1u << (vk & 31);
  return k;
}
inline KeyHold keyhold_up(KeyHold k, int vk) {
  if (vk >= 1 && vk <= 255) k.bits[vk >> 5] &= ~(1u << (vk & 31));
  return k;
}
inline int keyhold_count(KeyHold k) {
  int n = 0;
  for (int w = 0; w < 8; w++)
    for (unsigned v = k.bits[w]; v; v &= v - 1) n++;
  return n;
}
// Up to `cap` held codes in ascending order into `ups`, each cleared as it is taken; returns how many.
inline int keyhold_take_on_open(KeyHold* k, int* ups, int cap) {
  int n = 0;
  for (int vk = 1; vk <= 255 && n < cap; vk++) {
    const unsigned bit = 1u << (vk & 31);
    if (!(k->bits[vk >> 5] & bit)) continue;
    k->bits[vk >> 5] &= ~bit;
    ups[n++] = vk;
  }
  return n;
}

// duke-dnmp's ownership.h rule: a client-area coordinate to the back buffer's pixels, clamped inside it.
inline int cursor_scale(int c, int client, int render) {
  if (client <= 0 || render <= 0) return 0;
  const long long v = (long long)c * render / client;
  if (v < 0) return 0;
  if (v > render - 1) return render - 1;
  return (int)v;
}

// The suite: runs before the DLL links, fails the build on any red check. Only PURE rules are tested; anything reading
// live memory or calling the real winmm is read, not run. CHECK/CHECK_MSG (ported from duke3d-eduke32-aa) are two named
// macros, because MSVC's traditional preprocessor does not expand __VA_ARGS__ into another macro's argument list.
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>
#include "../logfmt.h"
#include "../hostgate.h"
#include "../paths.h"
#include "../stockgate.h"
#include "../hookrule.h"
#include "../d3dres.h"
#include "../primbatch.h"
#include "../cursorgate.h"
#include "../canvas_clip.h"
#include "../canvas.h"
#include "../font.h"
#include "../fontpack.h"
#include "../config.h"
#include "../ini.h"
#include "../ui/panel_layout.h"
#include "../ui/bind.h"
#include "../ui/anim.h"
#include "../ui/menu.h"
#include "../ui/entry.h"
#include "../ui/tipwrap.h"
#include "../framestats.h"
#include "../stallrule.h"
#include "../slowopen.h"
#include "../framecap.h"
#include "../bigpackage.h"
#include "../atlaswhite.h"
#include "../dimouse.h"
#include "../crashrule.h"
#include "../cfgretry.h"
#include "../drawgate.h"
#include "../d3dbody.h"
#include "../uwalk.h"
#include "../view.h"
#include "../world.h"
#include "../espbox.h"
#include "../esp_layout.h"
#include "../espnames.h"
#include "../dmgtrack.h"
#include "../dmgnum.h"
#include "../mswindow.h"
#include "../scallparms.h"
#include "../optrestore.h"
#include "../fov.h"
#include "../gamespeed.h"
#include "../accuracy.h"
#include "../display.h"
#include "../cvars.h"
#include "../focuskeep.h"
#include "../postfx.h"
#include "../progress.h"
#include "../levelstats.h"
#include "../egoesp.h"
#include "../pehook.h"
#include "../rawmouse.h"
#include "../noshake.h"
#include "../clicklock.h"
#include "../autowin.h"
#include "../autoexec.h"
#include "../worldscan.h"
#include "../items.h"
#include "../breakables.h"
#include "../modelbox.h"
#include "../dukevision.h"
#include "../aim.h"
#include "../mountaim.h"
#include "../tbot.h"
#include "../aimbot.h"
#include "../spawnbox.h"
#include "../freecam.h"
#include "../teleport.h"
#include "../photo.h"
#include "../photofolder.h"
#include "../d3dtrace.h"
#include "../rtclear.h"
#include "../rstall.h"
#include "../d3drec.h"
#include "../handling.h"
#include "../turretwep.h"
#include "../collect.h"
#include "../achreset.h"
#include "../aircraft.h"
#include "../overkill.h"
#include "../mountcol.h"
#include "../achhelp.h"
#include "../lvnames.h"
#include "../pbtrace.h"
#include "../arctrace.h"
#include "../pinbot.h"
#include "../arcbot.h"
#include "../fblift.h"
#include "../splash.h"
#include "../bloat.h"
#include "../mpgate.h"
#include <initializer_list>
#include <string>
#include <math.h>
#include <windows.h>   // the config tests' temp files, and VK_ and BOOL names

static int passed = 0, failed = 0;

// config.cpp logs through np_log; the suite has no log file.
void np_log(const char*, ...) {}

#define CHECK_MSG(cond, what)                                                 \
  do {                                                                        \
    if (cond) { passed++; }                                                   \
    else { failed++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, what); }  \
  } while (0)
#define CHECK(cond) CHECK_MSG(cond, #cond)

static int fmt_into(char* out, int cap, const char* fmt, ...) {
  va_list ap; va_start(ap, fmt); int n = logfmt_line(out, cap, fmt, ap); va_end(ap); return n;
}
static void tb_0001_logfmt_short_line_ends_in_newline() {
  char b[64]; int n = fmt_into(b, sizeof b, "x=%d", 7);
  CHECK(n == 4); CHECK(memcmp(b, "x=7\n", 4) == 0);
}
static void tb_0002_logfmt_overlong_line_truncates_without_nul() {
  char big[2000]; memset(big, 'a', sizeof big - 1); big[sizeof big - 1] = 0;
  char b[1024]; int n = fmt_into(b, sizeof b, "%s", big);
  CHECK(n == 1023); CHECK(b[n - 1] == '\n');
  CHECK(memchr(b, 0, (size_t)n) == NULL);
}
static void tb_0003_logfmt_tiny_cap_still_newline() {
  char b[2]; int n = fmt_into(b, sizeof b, "%s", "hello");
  CHECK(n == 1); CHECK(b[0] == '\n');
}

static void tb_0004_hostgate_only_the_game_exe() {
  CHECK(host_is_game(L"x\\System\\DukeForever.exe"));
  CHECK(host_is_game(L"x\\system\\dukeforever.EXE"));
  CHECK(!host_is_game(L"x\\System\\DukeForeverEditor.exe"));
  CHECK(!host_is_game(L"DukeForever.exe.bak"));
  CHECK(!host_is_game(L""));
}
static void tb_0005_paths_join_beside_module() {
  wchar_t p[260];
  CHECK(paths_join(L"g\\System\\winmm.dll", L"NukePanel\\", p, 260) && wcscmp(p, L"g\\System\\NukePanel\\") == 0);
  CHECK(!paths_join(L"winmm.dll", L"NukePanel\\", p, 260));   // no directory part
  CHECK(!paths_join(L"g\\System\\winmm.dll", L"NukePanel\\", p, 10));   // too small
}

static void tb_0006_stock_digest_hex_compare() {
  unsigned char d[32]; for (int i = 0; i < 32; i++) d[i] = (unsigned char)i;
  CHECK(stock_digest_matches(d, "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"));
  CHECK(stock_digest_matches(d, "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F"));
  CHECK(!stock_digest_matches(d, "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1e"));
  CHECK(!stock_digest_matches(d, "0001"));
}
static void tb_0007_hook_prologue_accepts_foreign_jmp() {
  const unsigned char fp[8]  = {0x8B,0xFF,0x55,0x8B,0xEC,0x83,0xEC,0x10};
  const unsigned char jmp[8] = {0xE9,0x11,0x22,0x33,0x44,0x83,0xEC,0x10};
  const unsigned char bad[8] = {0xE9,0x11,0x22,0x33,0x44,0x90,0xEC,0x10};
  CHECK(hook_prologue_ok(fp, fp, 8, 5));
  CHECK(hook_prologue_ok(jmp, fp, 8, 5));
  CHECK(!hook_prologue_ok(bad, fp, 8, 5));
}
static void tb_0008_hook_prologue_len_whole_instructions() {
  const unsigned char hot[16] = {0x8B,0xFF,0x55,0x8B,0xEC,0x83,0xEC,0x10};
  const unsigned char frame[16] = {0x55,0x8B,0xEC,0x83,0xEC,0x10};
  const unsigned char call[16] = {0x55,0xE8,0,0,0,0};
  CHECK(hook_prologue_len(hot, 16) == 5);
  CHECK(hook_prologue_len(frame, 16) == 6);
  CHECK(hook_prologue_len(call, 16) == 0);   // a relative call must not be copied raw
}
static void tb_0009_d3dres_reset_lifetime() {
  D3dRes r = d3dres_init();
  r = d3dres_on_created(r);                 CHECK(r.stateBlock && r.texture);
  r = d3dres_on_reset_before(r);            CHECK(!r.stateBlock && r.texture);
  r = d3dres_on_reset_after(r, false);      CHECK(!r.stateBlock);
  r = d3dres_on_reset_after(r, true);       CHECK(r.stateBlock && r.texture);
}
static void tb_0010_primbatch_flush_on_texture_or_full() {
  PrimBatch b; primbatch_init(&b, 6);
  CHECK(primbatch_add_tri(&b, (void*)1) == PB_QUEUED);
  CHECK(primbatch_add_tri(&b, (void*)2) == PB_FLUSH_THEN_QUEUE);
  CHECK(primbatch_add_tri(&b, (void*)2) == PB_QUEUED);
  CHECK(primbatch_add_tri(&b, (void*)2) == PB_FLUSH_THEN_QUEUE);
  CHECK(b.count == 3);
}
static void tb_0011_d3dres_device_replaced_releases_all() {
  D3dRes r = d3dres_on_created(d3dres_init());
  r = d3dres_on_device_replaced(r);         CHECK(!r.stateBlock && !r.texture);
  r = d3dres_on_created(r);                 CHECK(r.stateBlock && r.texture);
}

// cursorgate.h: which engine cursor calls pass while the panel is open, what close replays, and the keys the
// engine saw held, released to it when the panel opens (Review Focus 3).
static void tb_0012_cursor_gate_and_keyhold() {
  CHECK(cursor_gate_passes(CG_SETCURSORPOS, false));
  CHECK(!cursor_gate_passes(CG_SETCURSORPOS, true));
  CHECK(!cursor_gate_passes(CG_CLIPCURSOR, true));      // the panel's own clip to the window holds
  CHECK(!cursor_gate_passes_clip(true, /*rectIsNull*/ true));   // even ClipCursor(NULL): recorded for close
  CHECK(!cursor_gate_passes_clip(true, false));
  CHECK(!cursor_gate_passes(CG_GETKEYSTATE, true));     // open: every key reads up and untoggled
  CHECK(cursor_gate_passes(CG_GETKEYSTATE, false));
  CHECK(!cursor_gate_passes(CG_SHOWCURSOR, true));      // ShowCursor counts are replayed on close
  CHECK(!cursor_gate_passes(CG_SETCAPTURE, true));
  CHECK(cursor_gate_passes(CG_SETCURSOR, false));
  // Close replays the engine's last ShowCursor intent so its display counter ends where it expects.
  CgState s = cg_init();
  s = cg_on_showcursor(s, true, FALSE);  CHECK(s.pendingShow == -1);   // gated: recorded, not applied
  s = cg_on_showcursor(s, true, TRUE);   CHECK(s.pendingShow == 0);
  s = cg_on_showcursor(s, true, FALSE);  CHECK(cg_close_replay(s) == -1);
  // Review Focus 3: keys held when the panel opens are released to the engine as key-ups on open.
  KeyHold k = keyhold_init();
  k = keyhold_down(k, 'W'); k = keyhold_down(k, VK_SHIFT);
  int ups[8]; int n = keyhold_take_on_open(&k, ups, 8);
  CHECK(n == 2 && keyhold_count(k) == 0);
}
// The rest of the gate: every kind passes while closed, a closed call records nothing, the count the engine is
// answered while gated is the one it would have seen, and the last clip/capture/cursor intent is kept for close.
static void tb_0013_cursor_gate_intents() {
  for (int kind = 0; kind < CG_KINDS; ++kind) {
    CHECK(cursor_gate_passes(kind, false));
    CHECK(!cursor_gate_passes(kind, true));
  }
  CHECK(cursor_gate_passes_clip(false, false) && cursor_gate_passes_clip(false, true));
  CgState s = cg_on_showcursor(cg_init(), false, FALSE);
  CHECK_MSG(s.pendingShow == 0, "a closed call goes through and is not replayed");
  s = cg_on_showcursor(s, true, FALSE);
  CHECK_MSG(cg_virtual_count(s, 0) == -1, "a gated hide answers the count the engine expects");
  // A `while (ShowCursor(FALSE) >= 0)` loop ends against the virtual count, at any starting count.
  CgState loop = cg_init();
  int calls = 0;
  while (cg_virtual_count(loop, 3) >= 0 && calls < 64) { loop = cg_on_showcursor(loop, true, FALSE); calls++; }
  CHECK(calls == 4 && cg_close_replay(loop) == -4);
  CgState c = cg_on_clip(cg_init(), false, false, 1, 2, 3, 4);
  CHECK_MSG(c.clipCalls == 0, "closed: not recorded");
  c = cg_on_clip(c, true, false, 10, 20, 30, 40);
  CHECK(c.clipCalls == 1 && c.clipNull == 0 && c.clipL == 10 && c.clipT == 20 && c.clipR == 30 && c.clipB == 40);
  c = cg_on_clip(c, true, true, 0, 0, 0, 0);
  CHECK_MSG(c.clipCalls == 2 && c.clipNull == 1, "a later ClipCursor(NULL) is the intent close keeps");
  // Close puts back the engine's own clip state: its last gated call, else the clip in force at open, else none.
  CHECK(cg_close_clip(c, 1) == CG_CLIP_ENGINE_NULL);
  CHECK(cg_close_clip(cg_on_clip(c, true, false, 1, 2, 3, 4), 0) == CG_CLIP_ENGINE_RECT);
  CHECK(cg_close_clip(cg_init(), 1) == CG_CLIP_AT_OPEN);
  CHECK_MSG(cg_close_clip(cg_init(), 0) == CG_CLIP_LIFT, "no engine clip at all: ours is lifted");
  CHECK(clip_rect_same(1, 2, 3, 4, 1, 2, 3, 4) && !clip_rect_same(1, 2, 3, 4, 1, 2, 3, 5));
  static int objs_[2];  // distinct storage: an optimiser may fold equal unwritten locals
  int& wnd = objs_[0]; int& cur = objs_[1];
  c = cg_on_capture(c, true, &wnd);
  c = cg_on_setcursor(c, true, &cur);
  CHECK(c.captureCalls == 1 && c.capture == &wnd && c.cursorCalls == 1 && c.cursor == &cur);
  CHECK(cg_on_capture(cg_init(), false, &wnd).captureCalls == 0);
  // ReleaseCapture is held back too (a NULL capture intent), so close replays the engine's last word on capture.
  CHECK(!cursor_gate_passes(CG_RELEASECAPTURE, true) && cursor_gate_passes(CG_RELEASECAPTURE, false));
  CHECK(cg_close_capture(cg_init()) == CG_CAPTURE_UNTOUCHED);
  CHECK(cg_close_capture(c) == CG_CAPTURE_SET);
  c = cg_on_capture(c, true, 0);
  CHECK_MSG(c.captureCalls == 2 && cg_close_capture(c) == CG_CAPTURE_RELEASE, "SetCapture then ReleaseCapture: released");
  CHECK(cg_close_capture(cg_on_capture(c, true, &wnd)) == CG_CAPTURE_SET);
  // GetCursorPos is counted and never held back.
  CHECK(cursor_gate_passes(CG_GETCURSORPOS, true) && cursor_gate_passes(CG_GETCURSORPOS, false));
  CHECK(CG_GETCURSORPOS >= CG_KINDS && CG_GETCURSORPOS < CG_SLOTS);
  KeyHold k = keyhold_down(keyhold_down(keyhold_init(), 'A'), 'D');
  k = keyhold_up(k, 'A');
  CHECK(keyhold_count(k) == 1);
  k = keyhold_down(keyhold_down(k, 0), 300);
  CHECK_MSG(keyhold_count(k) == 1, "codes outside 1..255 are ignored");
  int ups[1];
  CHECK_MSG(keyhold_take_on_open(&k, ups, 1) == 1 && ups[0] == 'D' && keyhold_count(k) == 0, "taken in order");
  CHECK(keyhold_take_on_open(&k, ups, 1) == 0);
}
// duke-dnmp's ownership.h rule: the client-area cursor to the back buffer's pixels.
static void tb_0014_cursor_scale_maps_client_to_render() {
  CHECK(cursor_scale(640, 1280, 3440) == 1720);      // window smaller than the render size
  CHECK(cursor_scale(1720, 3440, 1280) == 640);      // window larger
  CHECK(cursor_scale(-5, 1280, 3440) == 0);
  CHECK(cursor_scale(1280, 1280, 3440) == 3439);
  CHECK(cursor_scale(10, 0, 3440) == 0);             // minimised: no client area
}

// --- The panel shell's suites, ported from duke-dnmp's test_host.cpp ----------------------------------------
static bool near_eq(float a, float b) { return fabsf(a - b) < 1e-4f; }

// Temp files for the config tests: written, read back, deleted.
static void temp_ini(char* out, int cap, const char* name) {
  char dir[MAX_PATH];
  const DWORD n = GetTempPathA(MAX_PATH, dir);
  _snprintf_s(out, cap, _TRUNCATE, "%s%s_%lu.ini", (n && n < MAX_PATH) ? dir : ".\\", name,
              GetCurrentProcessId());
}
static void write_text(const char* path, const char* text) {
  FILE* f = NULL;
  if (fopen_s(&f, path, "wb") == 0 && f) { fputs(text, f); fclose(f); }
}
static bool defaults_equal(const Config& c) {
  return c.menukey == 0x2D && c.panelx == -1 && c.panely == -1 && c.panelw == 0 && c.panelh == 0 &&
         c.animations == 1 && c.uiScale == UI_SCALE_DEFAULT && c.frameCapOn == 1 && c.frameCapFps == 0 &&
         c.bigPackage == 1;
}

static EntryState entry_type(EntryState s, const char* keys) {
  for (int i = 0; keys[i]; i++) s = entry_step(s, ENTRY_EV_CHAR, keys[i], 0, EK_NONE, 0, 0, 0).state;
  return s;
}

// Every slider row in the panel, as menu.cpp draws it: its tab, label, indent, the widest readout it can show and
// whether it has a reset glyph (a named slider has none).
static int tw(const char* s, int font) {
  int w = 0;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++)
    if (*p >= 0x20 && *p <= 0x7F) w += font_glyphs[font][*p - 0x20].adv;
  return w;
}
static const char* cap_widest() {
  static char a[24], b[24];
  menu_cap_readout(a, sizeof a, 240, 0);
  menu_cap_readout(b, sizeof b, 240, 1);
  return tw(a, FONT_MONO) >= tw(b, FONT_MONO) ? a : b;
}
struct SliderInv { int tab; const char* label; int indent; char reserve[24]; int hasReset; };
static int slider_inventory(SliderInv* out) {
  struct Fixed { int tab; const char* label; int indent; const char* reserve; int hasReset; };
  const Fixed fixed[] = {
    { TAB_SETTINGS, "Cap", SUB_INDENT, cap_widest(), 1 },
    { TAB_SETTINGS, "UI scale", 0, "200%", 1 },
  };
  int n = 0;
  for (unsigned i = 0; i < sizeof fixed / sizeof fixed[0]; i++) {
    SliderInv& r = out[n++];
    r.tab = fixed[i].tab; r.label = fixed[i].label; r.indent = fixed[i].indent; r.hasReset = fixed[i].hasReset;
    strncpy_s(r.reserve, sizeof r.reserve, fixed[i].reserve, _TRUNCATE);
  }
  return n;
}

static int tip_test_measure(const char* s, int n, void* ctx) { (void)s; return n * *(int*)ctx; }

static void tb_0015_clip_rect_inside_is_unchanged() {
  const ClipRect c = { 0.f, 0.f, 200.f, 200.f };
  float x0 = 10, y0 = 20, x1 = 30, y1 = 40, u0 = 0, v0 = 0, u1 = 1, v1 = 1;
  CHECK(clip_rect_uv(&c, &x0, &y0, &x1, &y1, &u0, &v0, &u1, &v1) == 1);
  CHECK(x0 == 10.f && y0 == 20.f && x1 == 30.f && y1 == 40.f);
  CHECK(u0 == 0.f && v0 == 0.f && u1 == 1.f && v1 == 1.f);
}
static void tb_0016_clip_rect_fully_outside_returns_zero() {
  const ClipRect c = { 0.f, 0.f, 100.f, 100.f };
  float x0 = 150, y0 = 10, x1 = 180, y1 = 20, u0 = 0, v0 = 0, u1 = 1, v1 = 1;
  CHECK(clip_rect_uv(&c, &x0, &y0, &x1, &y1, &u0, &v0, &u1, &v1) == 0);
  float a0 = 10, b0 = -50, a1 = 20, b1 = 0, s0 = 0, t0 = 0, s1 = 1, t1 = 1;   // touching the top edge only
  CHECK(clip_rect_uv(&c, &a0, &b0, &a1, &b1, &s0, &t0, &s1, &t1) == 0);
}
static void tb_0017_clip_rect_half_left_moves_u_halfway() {
  const ClipRect c = { 50.f, 0.f, 300.f, 300.f };
  float x0 = 0, y0 = 0, x1 = 100, y1 = 10, u0 = 0.2f, v0 = 0, u1 = 0.4f, v1 = 1;
  CHECK(clip_rect_uv(&c, &x0, &y0, &x1, &y1, &u0, &v0, &u1, &v1) == 1);
  CHECK(x0 == 50.f && x1 == 100.f);
  CHECK(near_eq(u0, 0.3f) && near_eq(u1, 0.4f));
  CHECK(v0 == 0.f && v1 == 1.f);
}
static void tb_0018_clip_rect_all_four_sides() {
  const ClipRect c = { 10.f, 20.f, 90.f, 60.f };
  float x0 = 0, y0 = 0, x1 = 100, y1 = 100, u0 = 0, v0 = 0, u1 = 1, v1 = 1;
  CHECK(clip_rect_uv(&c, &x0, &y0, &x1, &y1, &u0, &v0, &u1, &v1) == 1);
  CHECK(x0 == 10.f && y0 == 20.f && x1 == 90.f && y1 == 60.f);
  CHECK(near_eq(u0, 0.1f) && near_eq(u1, 0.9f) && near_eq(v0, 0.2f) && near_eq(v1, 0.6f));
}
static void tb_0019_clip_rect_zero_area_clip_returns_zero() {
  const ClipRect c = { 50.f, 0.f, 50.f, 100.f };
  float x0 = 0, y0 = 0, x1 = 100, y1 = 100, u0 = 0, v0 = 0, u1 = 1, v1 = 1;
  CHECK(clip_rect_uv(&c, &x0, &y0, &x1, &y1, &u0, &v0, &u1, &v1) == 0);
}
static void tb_0020_clip_poly_triangle_half_clipped() {
  const ClipRect c = { 0.f, 0.f, 50.f, 200.f };
  const ClipVert tri[3] = { { 0, 0, 0, 0 }, { 100, 0, 1, 0 }, { 0, 100, 0, 1 } };
  ClipVert out[CLIP_POLY_MAX];
  const int n = clip_poly(&c, tri, 3, out);
  CHECK(n == 4);
  bool inside = true, mid = false;
  for (int i = 0; i < n; ++i) {
    if (out[i].x > 50.f + 1e-4f) inside = false;
    if (near_eq(out[i].x, 50.f) && near_eq(out[i].y, 0.f) && near_eq(out[i].u, 0.5f)) mid = true;
  }
  CHECK(inside); CHECK(mid);
  const ClipRect away = { 200.f, 200.f, 300.f, 300.f };
  CHECK(clip_poly(&away, tri, 3, out) == 0);
}
// Ported from AA tb_0012_panel_layout (duke3d-eduke32-aa tests/test_host.cpp:2375).
static void tb_0021_panel_layout() {
  PanelBox unset = {-1, -1, 0, 0};
  const PanelBox d = panel_layout(unset, 3440, 1440);
  CHECK_MSG(d.w >= PANEL_MIN_W && d.h >= PANEL_MIN_H,
            "an unset panel comes back at least the minimum size");
  CHECK_MSG(d.x >= 0 && d.y >= 0, "and on screen");

  PanelBox huge = {0, 0, 99999, 99999};
  const PanelBox c = panel_layout(huge, 1280, 720);
  CHECK_MSG(c.w <= 1280 && c.h <= 720, "a panel larger than the screen is clamped to it");

  PanelBox b = {100, 200, 600, 800};
  const UiRect g = panel_grip_rect(b);
  CHECK_MSG(g.x + g.w == b.x + b.w && g.y + g.h == b.y + b.h,
            "the grip is flush with the bottom-right corner");
  CHECK_MSG(ui_hit(g, b.x + b.w - 1, b.y + b.h - 1), "the corner pixel is in the grip");
  CHECK_MSG(!ui_hit(g, b.x, b.y), "the opposite corner is not");
}
// The two resolutions that matter: the box stays inside the screen.
static void tb_0022_panel_box_clamps_1280x720_and_3440x1440() {
  const PanelBox unset = {-1, -1, 0, 0};
  const PanelBox a = panel_layout(unset, 1280, 720);
  CHECK(a.x == PANEL_INSET && a.w == PANEL_W);
  CHECK(a.h == 720 && a.y == 0);   // the 660 inset box is under AA's 790 floor, which clamps to the screen
  const PanelBox b = panel_layout(unset, 3440, 1440);
  CHECK(b.w == PANEL_W && b.h == PANEL_H && b.y == (1440 - PANEL_H) / 2);
  // A box stored on a big screen and loaded on a small one comes back on it.
  const PanelBox away = {3000, 1300, 620, 900};
  const PanelBox c = panel_layout(away, 1280, 720);
  CHECK(c.x + c.w <= 1280 && c.y + c.h <= 720 && c.x >= 0 && c.y >= 0);
  CHECK(c.h == 720);
  const PanelBox tiny = {10, 10, 50, 50};
  const PanelBox t = panel_layout(tiny, 3440, 1440);
  CHECK(t.w == PANEL_MIN_W && t.h == PANEL_MIN_H);
}
// Ported from AA tb_0317_ui_panel_max_h_for (test_host.cpp:22838).
static void tb_0023_ui_panel_max_h_for() {
  const int screens[] = { 480, 600, 768, 1080, 1200, 1440 };
  const int pcts[]    = { 75, 80, 90, 100, 110, 125, 130, 150, 175, 200 };
  for (unsigned si = 0; si < sizeof(screens) / sizeof(screens[0]); ++si) {
    for (unsigned pi = 0; pi < sizeof(pcts) / sizeof(pcts[0]); ++pi) {
      const int screenH = screens[si], pct = pcts[pi];
      const int h = ui_panel_max_h_for(screenH, pct, PANEL_VINSET);
      if (h == 0x7fffffff) {
        CHECK_MSG(pct <= UI_SCALE_MIN, "only the floor scale has no boundary");
        continue;
      }
      CHECK_MSG(ui_scale_max_for(screenH, h, PANEL_VINSET) >= pct,
                "the answer itself must still clear pct");
      CHECK_MSG(ui_scale_max_for(screenH, h + 1, PANEL_VINSET) < pct,
                "one taller must not");
    }
  }
  const int h1440 = ui_panel_max_h_for(1440, 130, PANEL_VINSET);
  CHECK_MSG(ui_scale_max_for(1440, h1440, PANEL_VINSET) >= 130,
            "1440p at 130%: the reported case must clear its own scale");
  CHECK_MSG(ui_scale_max_for(1440, h1440 + 1, PANEL_VINSET) < 130,
            "and one logical pixel taller must drop it");
  CHECK_MSG(ui_panel_max_h_for(1440, UI_SCALE_MIN, PANEL_VINSET) == 0x7fffffff,
            "at the floor scale nothing needs capping");
}
// Ported from AA tb_0250..0252 (test_host.cpp:18849-18960). The byte sweeps step
// by 4 (64 values): the suite caps loops at 64.
static void tb_0024_bind_key_is_live_matches_label() {
  CHECK_MSG(bind_key_is_live(0x1B) == false, "Escape is not a bindable key");
  CHECK_MSG(bind_key_is_live(0) == false, "0 is the unbound sentinel");
  CHECK(bind_key_is_live(VK_BIND_RBUTTON)  == true);
  CHECK(bind_key_is_live(VK_BIND_MBUTTON)  == true);
  CHECK(bind_key_is_live(VK_BIND_XBUTTON1) == true);
  CHECK(bind_key_is_live(VK_BIND_XBUTTON2) == true);
  CHECK_MSG(bind_key_is_live(0x42) == true, "an ordinary key (B) is live");
  int mismatches = 0;
  for (int vk = 0; vk <= 255; vk += 4) {
    const bool live = bind_key_is_live(vk);
    const bool labelled = strcmp(bind_label(vk), "Unbound") != 0;
    if (live != labelled) mismatches++;
  }
  CHECK_MSG(mismatches == 0, "live iff the label is not Unbound");
}
static void tb_0025_bind_hold_is_active_requires_live_key() {
  CHECK(bind_hold_is_active(0, true) == false);
  CHECK(bind_hold_is_active(0x1B, true) == false);
  CHECK(bind_hold_is_active(0x42, true)  == true);
  CHECK(bind_hold_is_active(0x42, false) == false);
  int mismatches = 0;
  for (int vk = 0; vk <= 255; vk += 4)
    if (!bind_key_is_live(vk) && bind_hold_is_active(vk, true)) mismatches++;
  CHECK_MSG(mismatches == 0, "a non-live vk never reports held");
}
static void tb_0026_bind_menu_key_effective_matches_live() {
  CHECK(bind_menu_key_effective(0x11) == 0x2D);   // VK_CONTROL: unmappable, Insert instead
  CHECK(bind_menu_key_effective(0x10) == 0x2D);   // VK_SHIFT
  CHECK(bind_menu_key_effective(0x42) == 0x42);
  CHECK(bind_menu_key_effective(VK_BIND_RBUTTON)  == VK_BIND_RBUTTON);
  CHECK(bind_menu_key_effective(VK_BIND_MBUTTON)  == VK_BIND_MBUTTON);
  CHECK(bind_menu_key_effective(VK_BIND_XBUTTON1) == VK_BIND_XBUTTON1);
  CHECK(bind_menu_key_effective(VK_BIND_XBUTTON2) == VK_BIND_XBUTTON2);
  int mismatches = 0;
  for (int vk = 0; vk <= 255; vk += 4)
    if (!bind_key_is_live(bind_menu_key_effective(vk))) mismatches++;
  CHECK_MSG(mismatches == 0, "the effective key is always live");
}
static void tb_0027_bind_swallows_requires_live_key_and_window() {
  CHECK(bind_swallows(0x42, 0x42, true) == true);
  CHECK(bind_swallows(0x42, 0x42, false) == false);
  CHECK(bind_swallows(0x1B, 0x1B, true) == false);
  CHECK(bind_swallows(0x42, 0, true) == false);
  CHECK(bind_swallows(0x43, 0x42, true) == false);
}
// anim.h's core, from Megaton's tb_0099 (tests/test_phase8.cpp:570); spring runs
// capped at 64 steps of 16 ms, a second, past both pairs' settling time.
static void tb_0028_anim_core() {
  CHECK(anim_ease_out(0.0) == 0.0 && anim_ease_out(1.0) == 1.0);
  CHECK(anim_ease_out(0.5) > 0.8);
  CHECK(anim_ease_out(-1.0) == 0.0 && anim_ease_out(2.0) == 1.0);
  CHECK(anim_progress(1000u, 1000u, 150) == 0.0);
  CHECK(fabs(anim_progress(1000u, 1075u, 150) - 0.5) < 1e-9);
  CHECK(anim_progress(1000u, 9000u, 150) == 1.0);
  CHECK(anim_progress(0xFFFFFFF0u, 0x0000000Au, 100) > 0.25 && anim_progress(0xFFFFFFF0u, 0x0000000Au, 100) < 0.27);
  CHECK(anim_progress(0u, 5u, 0) == 1.0);
  AnimTween tw = { 10.0, 20.0, 1000u, 100 };
  CHECK(anim_tween_value(tw, 1000u) == 10.0 && anim_tween_value(tw, 1100u) == 20.0);
  CHECK(anim_tween_value(tw, 1050u) > 15.0);
  CHECK(anim_dt_clamped(1000u, 1016u) == 16.0);
  CHECK(anim_dt_clamped(1000u, 3000u) == 50.0);
  CHECK(anim_dt_clamped(1000u,  999u) == 0.0);
  AnimSpring one = { 0.0, 0.0 };
  anim_spring_step(&one, 1.0, 400.0, 40.0, 16.0);
  CHECK(one.x > 0.05);   // semi-implicit Euler moves on the first step
  AnimSpring s = { 0.0, 0.0 }; bool crossed = false;
  for (int i = 0; i < 64; i++) { anim_spring_step(&s, 1.0, 400.0, 40.0, 16.0); if (s.x > 1.0 + 1e-6) crossed = true; }
  CHECK(!crossed && anim_spring_settled(s, 1.0, 1e-3));
  AnimSpring g = { 0.0, 0.0 }; bool over = false;
  for (int i = 0; i < 64; i++) { anim_spring_step(&g, 1.0, 300.0, 22.0, 16.0); if (g.x > 1.02) over = true; }
  CHECK(over && anim_spring_settled(g, 1.0, 1e-3));
  CHECK(anim_blend_colour(0x80102030u, 0xFFFFFFFFu, 0.0) == 0x80102030u);
  CHECK(anim_blend_colour(0x80102030u, 0xFFA0B0C0u, 1.0) == 0x80A0B0C0u);   // alpha kept
}
// Ported from AA tb_0208_menu_scroll_rules (test_host.cpp:14627).
static void tb_0029_menu_scroll_rules() {
  CHECK(menu_scroll_max(800, 825) == 0);
  CHECK(menu_scroll_max(825, 825) == 0);
  CHECK(menu_scroll_max(866, 825) == 41);
  CHECK(menu_scroll_clamp(40, 800, 825) == 0);
  CHECK(menu_scroll_clamp(-5, 866, 825) == 0);
  CHECK(menu_scroll_clamp(999, 866, 825) == 41);
  CHECK(menu_scroll_after_wheel(0, 1.0, 866, 825) == 0);
  CHECK(menu_scroll_after_wheel(41, -9.0, 866, 825) == 41);
  CHECK(menu_scroll_after_wheel(0, -1.0, 4000, 825) == MENU_SCROLL_STEP);
  CHECK(menu_scroll_after_wheel(MENU_SCROLL_STEP, 1.0, 4000, 825) == 0);
  CHECK(menu_scroll_after_wheel(400, 0.0, 866, 825) == 41);
  CHECK(menu_scroll_thumb_h(825, 866, 825) == (825 * 825) / 866);
  CHECK(menu_scroll_thumb_h(825, 100000, 825) == MENU_SCROLLBAR_MIN_THUMB);
  CHECK(menu_scroll_thumb_h(825, 800, 825) == 825);
  const int th = menu_scroll_thumb_h(825, 866, 825);
  CHECK(menu_scroll_thumb_y(825, th, 0, 866, 825) == 0);
  CHECK(menu_scroll_thumb_y(825, th, 41, 866, 825) == 825 - th);
  CHECK(MENU_MOUSE_NOWHERE < PAD - MENU_CONTENT_BUDGET);
}
// Our WndProc toggles on key-downs only, and a DInput-exclusive mouse may send it
// no button messages, so a mouse panel key would leave the panel unreachable.
static void tb_0030_panel_key_effective_is_a_key() {
  CHECK(panel_key_effective(0x2D) == 0x2D);
  CHECK(panel_key_effective(0x70) == 0x70);             // F1
  CHECK(panel_key_effective(VK_BIND_RBUTTON) == 0x2D);
  CHECK(panel_key_effective(VK_BIND_XBUTTON2) == 0x2D);
  CHECK(panel_key_effective(0x10) == 0x2D);             // VK_SHIFT: unmappable
  CHECK(panel_key_effective(0) == 0x2D);
  CHECK(panel_key_effective(-5) == 0x2D && panel_key_effective(4096) == 0x2D);
}
static void tb_0031_config_missing_file_gives_defaults() {
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_missing");
  DeleteFileA(path);
  g_cfg.menukey = 99; g_cfg.animations = 0;
  CHECK(config_load_from(path) == false);
  CHECK(defaults_equal(g_cfg));
  CHECK(g_cfgDirty == 0);
}
static void tb_0032_config_garbage_value_gives_default() {
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_garbage");
  write_text(path, "[panel]\r\nkey=banana\r\nx=12\r\ny=7q\r\nw= 700 \r\nanimations=7\r\n"
                   "[extras]\r\nbigpackage=\r\n");
  CHECK(config_load_from(path) == true);
  CHECK(g_cfg.menukey == 0x2D);       // not a number
  CHECK(g_cfg.panelx == 12);
  CHECK(g_cfg.panely == -1);          // trailing junk
  CHECK(g_cfg.panelw == 700);         // spaces around a number are fine
  CHECK(g_cfg.animations == 1);       // out of range
  CHECK(g_cfg.bigPackage == 1);       // empty
  // A key of the right name in the wrong section is not ours.
  write_text(path, "[extras]\r\nkey=70\r\n[panel]\r\nbigpackage=0\r\n");
  config_load_from(path);
  CHECK(g_cfg.menukey == 0x2D && g_cfg.bigPackage == 1);
  DeleteFileA(path);
}
static void tb_0033_config_unknown_key_survives_write() {
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_unknown");
  write_text(path, "; the player's note\r\n[panel]\r\nfuture=42\r\nkey=112\r\n[other]\r\nkeep=me\r\n");
  config_load_from(path);
  CHECK(g_cfg.menukey == 112);
  g_cfg.animations = 0; g_cfg.panelx = 300; g_cfgDirty = 1;
  config_write_changed();
  CHECK(g_cfgDirty == 0);
  char text[1024] = { 0 };
  FILE* f = NULL;
  if (fopen_s(&f, path, "rb") == 0 && f) { fread(text, 1, sizeof text - 1, f); fclose(f); }
  CHECK(strstr(text, "; the player's note") != NULL);
  CHECK(strstr(text, "future=42") != NULL);
  CHECK(strstr(text, "keep=me") != NULL);
  CHECK(strstr(text, "key=112") != NULL);            // unchanged keys are not rewritten
  CHECK(strstr(text, "animations=0") != NULL);
  CHECK(strstr(text, "x=300") != NULL);
  CHECK(strstr(text, "bigpackage") == NULL);         // unchanged: never written
  config_load_from(path);
  CHECK(g_cfg.animations == 0 && g_cfg.panelx == 300 && g_cfg.menukey == 112);
  DeleteFileA(path);
}
// AA's panel_layout.h:27 floor, copied rather than re-measured on our shorter
// tabs: a resize must stop where AA's does.
static void tb_0034_panel_min_h_is_aas() {
  CHECK(PANEL_MIN_H == 790);
  const PanelBox tiny = {10, 10, 50, 50};
  CHECK(panel_layout(tiny, 3440, 1440).h == 790);
  CHECK(panel_layout(tiny, 1280, 720).h == 720);   // a screen shorter than the floor still clamps to it
}
// menu.h: a tab is shown once it has a row of ours; all six do now (Combat since Auto-execute, 2026-10-05).
static void tb_0035_menu_tab_rail_shows_tabs_with_rows() {
  for (int t = 0; t < TAB_COUNT; ++t)
    CHECK(menu_tab_visible(t) == (t == TAB_SETTINGS || t == TAB_VISUALS || t == TAB_WEAPONS || t == TAB_VIEW ||
                                  t == TAB_PROGRESS || t == TAB_COMBAT || t == TAB_ACHIEVEMENTS || t == TAB_GAME));
  CHECK(!menu_tab_visible(-1));
}
// Slider typing (AA's entry.h, keyed by VK here) and "Also bound in the game".
static void tb_0036_entry_vk_events() {
  int ch = 0;
  CHECK(entry_char_of_vk(0x30) == '0' && entry_char_of_vk(0x39) == '9');
  CHECK_MSG(entry_char_of_vk(0x60) == '0' && entry_char_of_vk(0x69) == '9', "the keypad digits");
  CHECK_MSG(entry_char_of_vk(0xBD) == '-' && entry_char_of_vk(0x6D) == '-', "both minus keys");
  CHECK_MSG(entry_char_of_vk(0x41) == 0 && entry_char_of_vk(0x20) == 0, "letters and space are not typed");
  CHECK(entry_event_of_vk(0x35, false, false, &ch) == ENTRY_EV_CHAR && ch == '5');
  CHECK(entry_event_of_vk(0x0D, false, false, &ch) == ENTRY_EV_ENTER);
  CHECK(entry_event_of_vk(0x1B, false, false, &ch) == ENTRY_EV_ESCAPE);
  CHECK(entry_event_of_vk(0x08, false, false, &ch) == ENTRY_EV_BACKSPACE);
  CHECK(entry_event_of_vk(0x2E, false, false, &ch) == ENTRY_EV_DELETE);
  CHECK_MSG(entry_event_of_vk(0x25, true, false, &ch) == ENTRY_EV_LEFT && ch == 1, "Shift+Left extends");
  CHECK(entry_event_of_vk(0x23, false, false, &ch) == ENTRY_EV_END && ch == 0);
  CHECK(entry_event_of_vk(ENTRY_VK_A, false, true, &ch) == ENTRY_EV_SELECT_ALL);
  CHECK(entry_event_of_vk(ENTRY_VK_C, false, true, &ch) == ENTRY_EV_COPY);
  CHECK(entry_event_of_vk(ENTRY_VK_X, false, true, &ch) == ENTRY_EV_CUT);
  CHECK(entry_event_of_vk(ENTRY_VK_V, false, true, &ch) == ENTRY_EV_PASTE);
  CHECK_MSG(entry_event_of_vk(0x35, false, true, &ch) == -1, "Ctrl+5 is not a digit");
  CHECK_MSG(entry_event_of_vk(0x41, false, false, &ch) == -1, "a key the field does not take");
  CHECK_MSG(strcmp(MENU_GAMECFG_NOTE, "Also bound in the game") == 0, "AA's note text");
}
static void tb_0037_entry_commit_and_clamp() {
  EntryState s = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 4007, EK_INT, 1, 40, "8").state;
  CHECK(s.open == 1 && s.rowKey == 4007 && strcmp(s.text, "8") == 0 && s.caret == 1);
  EntryResult r = entry_step(s, ENTRY_EV_ENTER, 0, 0, EK_NONE, 0, 0, 0);
  CHECK_MSG(r.commit == 0 && r.state.open == 0, "Enter on the untouched prefill closes without a commit");
  s = entry_step(s, ENTRY_EV_BACKSPACE, 0, 0, EK_NONE, 0, 0, 0).state;
  s = entry_type(s, "99");
  r = entry_step(s, ENTRY_EV_ENTER, 0, 0, EK_NONE, 0, 0, 0);
  CHECK_MSG(r.commit == 1 && r.value == 40, "99 commits clamped to the row's 40");
  s = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 1, EK_INT, 1, 40, "8").state;
  s = entry_type(entry_step(s, ENTRY_EV_SELECT_ALL, 0, 0, EK_NONE, 0, 0, 0).state, "0");
  r = entry_step(s, ENTRY_EV_ENTER, 0, 0, EK_NONE, 0, 0, 0);
  CHECK_MSG(r.commit == 1 && r.value == 1, "0 commits clamped up to 1");
  s = entry_type(entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 1, EK_INT, 1, 40, "8").state, "5");
  r = entry_step(s, ENTRY_EV_ESCAPE, 0, 0, EK_NONE, 0, 0, 0);
  CHECK_MSG(r.commit == 0 && r.state.open == 0, "Escape cancels");
  s = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 1, EK_INT, 1, 40, "8").state;
  s = entry_step(s, ENTRY_EV_BACKSPACE, 0, 0, EK_NONE, 0, 0, 0).state;
  r = entry_step(s, ENTRY_EV_ENTER, 0, 0, EK_NONE, 0, 0, 0);
  CHECK_MSG(r.commit == 0 && r.state.open == 0, "an emptied field closes and commits nothing");
  s = entry_type(entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 1, EK_INT, 1, 40, "").state, "-3");
  CHECK_MSG(strcmp(s.text, "3") == 0, "no minus on a row that cannot go below zero");
  s = entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 1, EK_INT, 1, 40, "").state;
  s = entry_step(s, ENTRY_EV_PASTE, 0, 0, EK_NONE, 0, 0, "FOV: 12").state;
  r = entry_step(s, ENTRY_EV_CLICKAWAY, 0, 0, EK_NONE, 0, 0, 0);
  CHECK_MSG(r.commit == 1 && r.value == 12, "a pasted \"FOV: 12\" delivers 12, and clicking away commits");
  CHECK_MSG(entry_step(entry_begin(), ENTRY_EV_OPEN, 0, 1, EK_NONE, 0, 0, "x").state.open == 0,
            "a row with no number opens nothing");
  bool ok = true;
  CHECK(entry_parse("", EK_INT, 0, 10, &ok) == 0 && !ok);
}
// config.cpp reads only the rows this port has: DNMP's other keys are not read.
static void tb_0038_config_keys_are_this_ports() {
  static const char* const kGone[][2] = {
    { "debug", "timing" }, { "freecam", "hudstash" },
    { "saves", "fast" }, { "framerate", "doublejump" } };
  int known = 0;
  for (int i = 0; i < (int)(sizeof kGone / sizeof kGone[0]); ++i) known += config_key_known(kGone[i][0], kGone[i][1]);
  CHECK_MSG(known == 0, "DNMP's other rows' keys are not read");
  CHECK(config_key_known("panel", "key") && config_key_known("panel", "uiscale") && config_key_known("panel", "x"));
  CHECK(config_key_known("framerate", "cap") && config_key_known("framerate", "capfps"));
  CHECK(config_key_known("extras", "bigpackage") && config_key_known("Extras", "BigPackage"));
  CHECK_MSG(config_key_known("display", "borderless"), "Borderless window is this port's row now (2026-10-04)");
}
// menu.h: a collapsed sub-row block lays out nothing but keeps its row ordinals, so the rows below keep their
// animation slots and row keys whether it shows or not.
static void tb_0039_menu_collapse_keeps_ordinals() {
  const MenuCollapse off = menu_collapse_begin(0, 10, 6);
  CHECK(!off.shown && menu_collapse_end(off, 10) == 16);
  const MenuCollapse on = menu_collapse_begin(1, 10, 6);
  CHECK(on.shown && menu_collapse_end(on, 16) == 16);
  CHECK_MSG(menu_collapse_end(on, 13) == 16, "a block that drew fewer rows still ends at its reserve");
  CHECK_MSG(menu_collapse_end(on, 18) == 18, "never moves an ordinal backwards");
}
// ini_peek_int leaves what config_load_from leaves, from the same text.
static void tb_0040_ini_peek_int() {
  static const char* const kTexts[] = {
    "[extras]\r\nbigpackage=0\r\n",
    "[Extras]\r\n  BigPackage = 0 \r\n",
    "[extras]\r\nbigpackage=7\r\n",
    "[extras]\r\nbigpackage=0\r\nbigpackage=x\r\n",
    "[extras]\r\nbigpackage=0\r\nbigpackage=1\r\n",
    "[panel]\r\nbigpackage=0\r\n",
    "[extras]\r\n; bigpackage=0\r\n# bigpackage=0\r\n",
    "[extras]\r\n[panel\r\nbigpackage=0\r\n",
    "[extras]\nbigpackage=0x0",
    "[extras]\r\nbigpackage\r\n=0\r\n",
    "",
  };
  static const int kWant[] = { 0, 0, 1, 0, 1, 1, 1, 0, 0, 1, 1 };
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_peek");
  for (int i = 0; i < (int)(sizeof kTexts / sizeof kTexts[0]); i++) {
    const int peek = ini_peek_int(kTexts[i], (int)strlen(kTexts[i]), "extras", "bigpackage", 0, 1, 1);
    write_text(path, kTexts[i]);
    config_load_from(path);
    if (peek != kWant[i] || g_cfg.bigPackage != kWant[i])
      printf("  text %d: peek %d, config_load_from %d, want %d\n", i, peek, g_cfg.bigPackage, kWant[i]);
    CHECK_MSG(peek == kWant[i] && g_cfg.bigPackage == kWant[i], "peek and loader agree");
  }
  DeleteFileA(path);
  const char* cut = "[extras]\r\nbigpackage=0\r\n";
  CHECK_MSG(ini_peek_int(cut, 12, "extras", "bigpackage", 0, 1, 1) == 1, "only n bytes are read");
}
// menu.h: the Items tab is gone; Progress joins before Settings, Achievements
// between Progress and Settings.
static void tb_0041_menu_tab_rail() {
  CHECK(TAB_COUNT == 8);
  CHECK(TAB_VISUALS == 0 && TAB_COMBAT == 1 && TAB_WEAPONS == 2);
  CHECK(TAB_VIEW == 3 && TAB_GAME == 4 && TAB_PROGRESS == 5 && TAB_ACHIEVEMENTS == 6 && TAB_SETTINGS == 7);
  CHECK(!menu_tab_visible(8));
  CHECK(strcmp(menu_tab_name(TAB_GAME), "Game") == 0);
}
// menu.h: the eased fold. Opens and shuts over MENU_FOLD_MS with ease-out, turns back from
// where it is, pops under animations off, and only an open or opening fold is laid out and hit-tested.
static void tb_0042_menu_fold_ease() {
  const unsigned t = 1000;
  MenuFold f = menu_fold_init(0);
  CHECK(menu_fold_frac(f, t) == 0.0 && !menu_fold_laid_out(f, t) && !menu_fold_animating(f, t));
  f = menu_fold_step(f, 1, t, 1);
  CHECK(f.open == 1 && menu_fold_frac(f, t) == 0.0 && menu_fold_laid_out(f, t) && menu_fold_animating(f, t));
  const double mid = menu_fold_frac(f, t + MENU_FOLD_MS / 2);
  CHECK_MSG(mid > 0.5 && mid < 1.0, "ease-out: past half at half time");
  CHECK(menu_fold_frac(f, t + MENU_FOLD_MS) == 1.0 && !menu_fold_animating(f, t + MENU_FOLD_MS));
  CHECK_MSG(menu_fold_step(f, 1, t + 10, 1).t0 == t, "the same parent value does not restart it");
  MenuFold g = menu_fold_step(f, 0, t + MENU_FOLD_MS / 2, 1);
  CHECK_MSG(menu_fold_frac(g, t + MENU_FOLD_MS / 2) == mid, "turns back from where it was");
  CHECK(menu_fold_laid_out(g, t + MENU_FOLD_MS / 2));
  CHECK(menu_fold_frac(g, t + MENU_FOLD_MS / 2 + MENU_FOLD_MS) == 0.0);
  CHECK(!menu_fold_laid_out(g, t + MENU_FOLD_MS / 2 + MENU_FOLD_MS));
  CHECK_MSG(menu_fold_frac(menu_fold_step(menu_fold_init(0), 1, t, 0), t) == 1.0, "animations off pops open");
  CHECK_MSG(menu_fold_frac(menu_fold_step(menu_fold_init(1), 0, t, 0), t) == 0.0, "and pops shut");
  const MenuFold w = menu_fold_step(menu_fold_init(0), 1, 0xFFFFFFF0u, 1);
  CHECK_MSG(menu_fold_frac(w, 0xFFFFFFF0u + MENU_FOLD_MS) == 1.0, "wrap-safe clock");
  CHECK(menu_fold_height(340, 0.0) == 0 && menu_fold_height(340, 1.0) == 340);
  CHECK(menu_fold_height(340, 0.5) == 170 && menu_fold_height(341, 0.5) == 171);
  CHECK(menu_fold_height(340, 1.5) == 340 && menu_fold_height(340, -1.0) == 0 && menu_fold_height(-5, 0.5) == 0);
  CHECK_MSG(menu_fold_hit_live(f, t + MENU_FOLD_MS, false), "settled open: every row, as before");
  CHECK_MSG(menu_fold_hit_live(f, t + 10, true) && !menu_fold_hit_live(f, t + 10, false),
            "opening: only the part already drawn");
  CHECK_MSG(!menu_fold_hit_live(g, t + MENU_FOLD_MS / 2 + 10, true), "shutting: nothing");
}
// menu.h: the scroll cue. Drawn only on overflow; the thumb is the visible share of the
// track and follows the offset; the bottom fade is full while a fade's height or more lies below, gone at the end.
static void tb_0043_menu_scroll_cue() {
  CHECK(!menu_scroll_cue(600, 0, 600, 600).shown && !menu_scroll_cue(600, 0, 400, 600).shown);
  const MenuScrollCue top = menu_scroll_cue(600, 0, 1200, 600);
  CHECK(top.shown && top.thumbH == 300 && top.thumbY == 0 && top.fade == 1.0);
  const MenuScrollCue end = menu_scroll_cue(600, 600, 1200, 600);
  CHECK(end.thumbY == 300 && end.fade == 0.0);
  CHECK(menu_scroll_cue(600, 300, 1200, 600).thumbY == 150);
  CHECK_MSG(menu_scroll_cue(600, 600 - MENU_SCROLL_FADE_H / 2, 1200, 600).fade == 0.5, "eases out over the end");
  CHECK_MSG(menu_scroll_cue(600, 9999, 1200, 600).thumbY == 300, "an offset past the end is clamped");
  CHECK(menu_scroll_cue(600, 0, 100000, 600).thumbH == MENU_SCROLLBAR_MIN_THUMB);
  CHECK(!menu_scroll_cue(0, 0, 1200, 600).shown);
}
// --- UI scale ---------------------------------------
// AA's tb_0155 packing rules, its slot cases dropped (one panel atlas here), plus the white block this port packs
// last for the flat fills.
static void tb_0044_fontpack_rules() {
  for (int i = 1; i < 6; i++) {
    const long long prev = (long long)FONTPACK_LADDER[i-1].w * FONTPACK_LADDER[i-1].h;
    const long long cur  = (long long)FONTPACK_LADDER[i].w   * FONTPACK_LADDER[i].h;
    CHECK(cur > prev);
  }
  PackBox boxes[4] = { {16,16}, {16,16}, {32,8}, {8,32} };
  PackPos pos[4];
  CHECK(fontpack_shelf(boxes, 4, 512, 256, 1, pos) == true);
  for (int i = 0; i < 4; i++)
    CHECK(pos[i].x >= 0 && pos[i].y >= 0 && pos[i].x + boxes[i].w <= 512 && pos[i].y + boxes[i].h <= 256);
  for (int a = 0; a < 4; a++)
    for (int b = a + 1; b < 4; b++) {
      const int ax0 = pos[a].x, ax1 = pos[a].x + boxes[a].w, ay0 = pos[a].y, ay1 = pos[a].y + boxes[a].h;
      const int bx0 = pos[b].x, bx1 = pos[b].x + boxes[b].w, by0 = pos[b].y, by1 = pos[b].y + boxes[b].h;
      CHECK(ax1 <= bx0 || bx1 <= ax0 || ay1 <= by0 || by1 <= ay0);
    }
  PackBox huge[1] = { {4096, 4096} };
  PackPos hp[1];
  PackBox chosen = { 0, 0 };
  CHECK(fontpack_choose(huge, 1, 1, hp, &chosen) == -1);
  // Every baked glyph doubled plus 4 px each way (GDI's hinting at 2x is not an exact doubling), and the 4x4
  // white block last: 200% still packs into rung 2 (1024x512), well under the 2048 ceiling.
  static PackBox big[FONT_COUNT * 96 + 1];
  static PackPos bp[FONT_COUNT * 96 + 1];
  for (int f = 0; f < FONT_COUNT; f++)
    for (int g = 0; g < 96; g++) {
      const Glyph& gl = font_glyphs[f][g];
      big[f * 96 + g].w = gl.w > 0 ? 2 * gl.w + 4 : 0;
      big[f * 96 + g].h = gl.h > 0 ? 2 * gl.h + 4 : 0;
    }
  big[FONT_COUNT * 96].w = 4; big[FONT_COUNT * 96].h = 4;
  const int rung = fontpack_choose(big, FONT_COUNT * 96 + 1, 1, bp, &chosen);
  CHECK_MSG(rung >= 0 && rung <= 2, "200% and the white block fit rung 2");
  const PackPos wp = bp[FONT_COUNT * 96];
  CHECK(wp.x >= 1 && wp.y >= 1 && wp.x + 4 <= chosen.w && wp.y + 4 <= chosen.h);
}
static void tb_0045_canvas_atlas_step() {
  CHECK(canvas_atlas_step(100, 0, 0) == CANVAS_ATLAS_BAKED);
  CHECK(canvas_atlas_step(100, 130, 130) == CANVAS_ATLAS_BAKED);
  CHECK(canvas_atlas_step(130, 0, 0) == CANVAS_ATLAS_BUILD);
  CHECK(canvas_atlas_step(130, 130, 0) == CANVAS_ATLAS_HELD);
  CHECK(canvas_atlas_step(130, 150, 0) == CANVAS_ATLAS_BUILD);
  CHECK_MSG(canvas_atlas_step(130, 150, 130) == CANVAS_ATLAS_REFUSED, "a refused scale is not retried");
  CHECK(canvas_atlas_step(150, 0, 130) == CANVAS_ATLAS_BUILD);
  CHECK(canvas_atlas_step(250, 200, 0) == CANVAS_ATLAS_HELD);
  CHECK(canvas_atlas_step(60, 0, 0) == CANVAS_ATLAS_BUILD);
  CHECK(canvas_atlas_step(60, 75, 0) == CANVAS_ATLAS_HELD);
}
static void tb_0046_ui_scale_settled_and_px() {
  CHECK(ui_scale_settled(130, 140) == 130);
  CHECK(ui_scale_settled(150, 140) == 140);
  CHECK(ui_scale_settled(300, 200) == 200);
  CHECK(ui_scale_settled(50, 200) == 75);
  CHECK(ui_scale_settled(130, 75) == 75);
  CHECK(ui_scale_settled(100, ui_scale_max_for(1440, PANEL_H, PANEL_VINSET)) == 100);
  // The faces fontgen asks GDI for: Bahnschrift 21/16 and Cascadia Mono 18 at the scale.
  CHECK(ui_scale_px(21, 130) == 27 && ui_scale_px(16, 130) == 21 && ui_scale_px(18, 130) == 23);
  CHECK(ui_scale_px(21, 200) == 42 && ui_scale_px(16, 200) == 32 && ui_scale_px(18, 200) == 36);
  CHECK(ui_scale_px(21, 75) == 16 && ui_scale_px(16, 75) == 12 && ui_scale_px(18, 75) == 14);
  CHECK(ui_unscale_px(ui_scale_px(400, 130), 130) == 400);
  CHECK_MSG(config_key_known("panel", "uiscale"), "AA's key, read and written");
}
// The UI scale reaches 200% on every screen at least 950 px tall. The ceiling is the largest scale at which the least
// panel fits: the chrome, PANEL_CHROME_MIN_H tall, and PANEL_MIN_W wide. A taller stored panel draws capped at the
// applied scale, its content scrolling; the stored box is not written.
static void tb_0047_ui_scale_ceiling() {
  CHECK_MSG(ui_scale_max_for(1440, 1300, PANEL_VINSET) == 105, "the reported case under the stored-height rule");
  CHECK(PANEL_CHROME_MIN_H == 415);
  CHECK_MSG(PANEL_CHROME_MIN_H - (2 + STRIP_H + STATUS_H) >= TAB_COUNT * MENU_RAIL_ENTRY_H, "the whole rail fits");
  CHECK_MSG((PANEL_CHROME_MIN_H - (2 + STRIP_H + STATUS_H) - 2 * PAD) / (ROW_H + ROW_GAP) >= 3,
            "the body beside the rail holds at least three rows");
  CHECK(ui_scale_ceiling(3440, 1440) == 200);
  CHECK(ui_scale_ceiling(2560, 1440) == 200);
  CHECK(ui_scale_ceiling(1920, 1080) == 200);
  CHECK_MSG(ui_scale_ceiling(1280, 720) == 150, "720 px is now short of 200%");
  CHECK_MSG(ui_scale_ceiling(1920, 950) == 200, "the shortest screen that reaches 200%");
  CHECK(ui_scale_ceiling(1920, 949) == 195);
  CHECK_MSG(ui_scale_ceiling(1024, 1080) == 165, "a narrow screen: PANEL_MIN_W 608 decides");
  CHECK_MSG(ui_scale_ceiling(1024, 768) == 160, "1024x768: the eight-tab height decides (165 by the width)");
  CHECK_MSG(ui_scale_ceiling(1920, 240) == UI_SCALE_MIN, "a short screen floors at the minimum");
  CHECK(ui_panel_h_cap(1440, 200) == 660);
  CHECK(ui_panel_h_cap(1080, 200) == 480);
  CHECK_MSG(ui_panel_h_cap(600, 200) == PANEL_CHROME_MIN_H, "past the ceiling the chrome floor holds");
  const int pcts[] = { 75, 100, 130, 165, 200 };
  for (int i = 0; i < 5; i++) {
    const int cap = ui_panel_h_cap(1440, pcts[i]);
    CHECK_MSG(cap >= PANEL_CHROME_MIN_H, "the cap never squeezes the panel under the chrome");
    CHECK_MSG(cap == 0x7fffffff || ui_scale_max_for(1440, cap, PANEL_VINSET) >= pcts[i],
              "a panel at its cap fits the screen at that scale");
  }
  // A tall stored panel at 200% on 3440x1440 (1720x720 logical): drawn 660 tall, as near its stored place as fits.
  const PanelBox stored = { 60, 70, 620, 1300 };
  const PanelBox d = menu_panel_drawn(stored, ui_unscale_px(3440, 200), ui_unscale_px(1440, 200),
                                      ui_panel_h_cap(1440, 200));
  CHECK(d.h == 660 && d.y == 60 && d.x == 60 && d.w == 620);
  const PanelBox d1080 = menu_panel_drawn(stored, 960, 540, ui_panel_h_cap(1080, 200));
  CHECK(d1080.h == 480 && d1080.y + d1080.h <= 540);
  // Width: at a narrow screen's ceiling the panel is no wider than the logical screen and no narrower than the floor.
  const int pctN = ui_scale_ceiling(1024, 768);
  const PanelBox unset = { -1, -1, 0, 0 };
  const PanelBox dn = menu_panel_drawn(unset, ui_unscale_px(1024, pctN), ui_unscale_px(768, pctN),
                                       ui_panel_h_cap(768, pctN));
  CHECK(dn.w >= PANEL_MIN_W && dn.x + dn.w <= ui_unscale_px(1024, pctN));
  const PanelBox d100 = menu_panel_drawn(unset, 2560, 1440, ui_panel_h_cap(1440, 100));
  CHECK_MSG(d100.h == PANEL_H && d100.w == PANEL_W, "at 100% the default box is unchanged");
  CHECK(ui_scale_step(137) == 135 && ui_scale_step(138) == 140 && ui_scale_step(199) == 200);
  CHECK(ui_scale_step(76) == 75 && ui_scale_step(300) == 200 && ui_scale_step(-5) == 75);
  CHECK_MSG(ui_scale_settled(137, 200) == 135, "an old stored value settles on a step");
}
// One track left edge and one track width for every slider row on every tab, sub-rows included, at the default
// panel, the narrowest and a narrow screen's at its ceiling; every label clears the track.
static void tb_0048_slider_geometry_uniform() {
  SliderInv inv[32];
  const int n = slider_inventory(inv);
  CHECK(n == 2);
  const int panelWs[] = { PANEL_W, PANEL_MIN_W, ui_unscale_px(1024, ui_scale_ceiling(1024, 768)) };
  for (int pi = 0; pi < 3; pi++) {
    const int rowW = panelWs[pi] - RAIL_W - 2 * PAD - 2;
    MenuSliderGeom first = { 0, 0, 0, 0, 0 };
    for (int i = 0; i < n; i++) {
      const MenuSliderGeom g = menu_slider_geom(rowW, tw(MENU_READOUT_RESERVE, FONT_MONO));
      if (i == 0) first = g;
      char msg[160];
      sprintf_s(msg, "panel %d, tab %d %s: x=%d w=%d readout=%d+%d (first row x=%d w=%d)", panelWs[pi], inv[i].tab,
                inv[i].label, g.trackX, g.trackW, g.readoutX, g.readoutW, first.trackX, first.trackW);
      CHECK_MSG(g.trackX == first.trackX && g.trackW == first.trackW && g.readoutX == first.readoutX, msg);
      CHECK_MSG(tw(inv[i].reserve, FONT_MONO) <= g.readoutW, msg);
      CHECK_MSG(inv[i].indent + tw(inv[i].label, FONT_UI) + TRACK_GAP <= g.trackX, msg);
    }
  }
  // The default panel: one 100 px track at x=233, the readout column 99 wide (nine mono cells) right after it.
  const MenuSliderGeom d = menu_slider_geom(PANEL_W - RAIL_W - 2 * PAD - 2, tw(MENU_READOUT_RESERVE, FONT_MONO));
  CHECK(d.trackX == 233 && d.trackW == 100 && d.readoutX == 365 && d.readoutW == 99);
  CHECK_MSG(d.glyphX >= d.trackX + d.trackW && d.glyphX + RESET_W <= d.readoutX, "the glyph sits between them");
  // The narrowest panel still gives the minimum track.
  CHECK(menu_slider_geom(PANEL_MIN_W - RAIL_W - 2 * PAD - 2, tw(MENU_READOUT_RESERVE, FONT_MONO)).trackW == TRACK_MIN_W);
  // A wider readout cell (a scaled atlas rounds the mono advance) moves every row's track together.
  CHECK(menu_slider_geom(474, 108).trackX == 233 && menu_slider_geom(474, 108).trackW == 91);
  char cap[24];
  menu_cap_readout(cap, sizeof cap, 144, 1);
  CHECK_MSG(strcmp(cap, "144 fps") == 0, "following the monitor reads fps too");
  menu_cap_readout(cap, sizeof cap, 60, 0);
  CHECK(strcmp(cap, "60 fps") == 0);
}
// The capped panel moves, rests and opens where the cap puts it (the drag lays out
// with the cap, so a press and a click that moves nothing leave the box where it is, and an unset box centres.
static void tb_0049_capped_panel_drag() {
  const int W = 1720, H = 720, cap = 660;   // 3440x1440 at 200%
  const PanelBox stored = { 60, 60, 620, 1300 };
  const PanelBox laid = menu_panel_drawn(stored, W, H, cap);
  CHECK(laid.y == 60 && laid.h == 660);
  PanelDrag d = { PDRAG_IDLE, 0, 0, { 0, 0, 0, 0 } };
  PanelBox box; bool ended = false;
  d = panel_drag_step(d, true, true, true, false, 300, 70, laid, W, H, &box, &ended, cap);
  CHECK(d.phase == PDRAG_MOVE && box.y == 60);
  d = panel_drag_step(d, false, true, true, false, 300, 70, laid, W, H, &box, &ended, cap);
  CHECK_MSG(box.x == laid.x && box.y == laid.y && box.w == laid.w && box.h == laid.h,
            "a held press that moves nothing leaves the box as laid, so the click stores nothing");
  d = panel_drag_step(d, false, true, true, false, 300, 40, laid, W, H, &box, &ended, cap);
  CHECK_MSG(box.y == 30 && box.h == 660, "a move at the cap follows the mouse");
  d = panel_drag_step(d, false, false, true, false, 300, 40, laid, W, H, &box, &ended, cap);
  CHECK(ended && d.phase == PDRAG_IDLE);
  // A resize under a cap below PANEL_MIN_H changes the width only, in place.
  d.phase = PDRAG_IDLE;
  const PanelBox g = laid;
  d = panel_drag_step(d, true, true, false, true, g.x + g.w - 1, g.y + g.h - 1, laid, W, H, &box, &ended, cap);
  d = panel_drag_step(d, false, true, false, true, g.x + g.w + 39, g.y + g.h - 101, laid, W, H, &box, &ended, cap);
  CHECK_MSG(box.h == 660 && box.y == 60 && box.w == 660, "a resize at the cap keeps the height and the place");
  // An unset box at the cap is centred.
  const PanelBox unset = { -1, -1, 0, 0 };
  const PanelBox c = menu_panel_drawn(unset, W, H, cap);
  CHECK(c.h == 660 && c.y == 30);
  const PanelBox c1080 = menu_panel_drawn(unset, 960, 540, ui_panel_h_cap(1080, 200));
  CHECK(c1080.h == 480 && c1080.y == 30);
  // The entry field covers the readout column, the glyph slot and the margin, never the track.
  const int fieldW = tw(MENU_ENTRY_WIDEST, FONT_MONO) + 2 * FIELD_PAD;
  const int rowWs[] = { PANEL_W - RAIL_W - 2 * PAD - 2, PANEL_MIN_W - RAIL_W - 2 * PAD - 2 };
  for (int i = 0; i < 2; i++) {
    const MenuSliderGeom s = menu_slider_geom(rowWs[i], tw(MENU_READOUT_RESERVE, FONT_MONO));
    CHECK(rowWs[i] - fieldW >= s.trackX + s.trackW);
  }
}
// Hints wrap to a width at spaces; a word too wide is cut.
static void tb_0050_tip_wrap() {
  int px = 6, st[TIP_LINES_MAX], ln[TIP_LINES_MAX], widest = -1;
  const char* s = "aaa bbb ccc";
  int n = tip_wrap(s, 42, tip_test_measure, &px, st, ln, TIP_LINES_MAX, &widest);
  CHECK_MSG(n == 2 && st[0] == 0 && ln[0] == 7 && st[1] == 8 && ln[1] == 3 && widest == 42, "two words fit, one wraps");
  n = tip_wrap(s, 1000, tip_test_measure, &px, st, ln, TIP_LINES_MAX, &widest);
  CHECK_MSG(n == 1 && ln[0] == 11 && widest == 66, "short text stays on one line");
  n = tip_wrap("aa   bb", 24, tip_test_measure, &px, st, ln, TIP_LINES_MAX, &widest);
  CHECK_MSG(n == 2 && ln[0] == 2 && st[1] == 5 && ln[1] == 2, "the spaces at a break are dropped");
  n = tip_wrap("abcdefghij", 30, tip_test_measure, &px, st, ln, TIP_LINES_MAX, &widest);
  CHECK_MSG(n == 2 && ln[0] == 5 && st[1] == 5 && ln[1] == 5, "a word wider than the line is cut");
  n = tip_wrap("a b c d e", 6, tip_test_measure, &px, st, ln, 3, &widest);
  CHECK_MSG(n == 3 && st[2] == 4, "at most cap lines");
  n = tip_wrap("", 100, tip_test_measure, &px, st, ln, TIP_LINES_MAX, &widest);
  CHECK_MSG(n == 0 && widest == 0, "empty: no lines");
}
// The wrapped box's width limit, and the box staying on screen at the right and bottom edges.
static void tb_0051_tip_place() {
  CHECK_MSG(tip_max_w(600, 1920) == TIP_MAX_W, "420 on a wide panel");
  CHECK_MSG(tip_max_w(300, 1920) == 300, "the panel's width when narrower");
  CHECK_MSG(tip_max_w(600, 380) == 380 - 2 * TOOLTIP_PAD, "the screen less the padding");
  CHECK_MSG(tip_max_w(20, 1920) == TIP_MIN_W, "never below the floor");
  int px = 7, st[TIP_LINES_MAX], ln[TIP_LINES_MAX], widest = 0;
  const char* s = "Secrets: every area's, matching the game's tally. Nukes: the game's tally; 7 of them lie in the "
                  "areas' pickups, crates and barrels.";
  const int maxW = tip_max_w(600, 1280);
  const int n = tip_wrap(s, maxW, tip_test_measure, &px, st, ln, TIP_LINES_MAX, &widest);
  CHECK_MSG(n >= 2 && widest <= maxW, "the long hint wraps within the limit");
  const int h = n * 12 + (n - 1) * TIP_LINE_GAP;
  UiRect b = tooltip_box_place(1270, 700, widest, h, 1280, 720);
  CHECK_MSG(b.x >= 0 && b.x + b.w <= 1280 && b.y >= 0 && b.y + b.h <= 720, "near the bottom right: flipped on screen");
  b = tooltip_box_place(10, 10, widest, h, 1280, 720);
  CHECK_MSG(b.x == 10 + TOOLTIP_DX && b.y == 10 + TOOLTIP_DY, "away from the edges: below right of the cursor");
}

static void tb_0052_framestats_reports_after_one_second() {
  FrameStats fs; fs_reset(&fs);
  for (int i = 0; i < 64; ++i) fs_add(&fs, 0.0057);   // 64 * 5.7 ms = 0.365 s
  CHECK(!fs_ready(&fs));
  fs_reset(&fs);
  for (int i = 0; i < 64; ++i) fs_add(&fs, 1.0 / 60.0);  // 1.067 s
  CHECK(fs_ready(&fs));
  CHECK(fs.last_avg_ms > 16.6 && fs.last_avg_ms < 16.7);
  CHECK(fs.min_ms > 16.6 && fs.max_ms < 16.7);
}

static void tb_0053_hitch_thresholds() {
  HitchStats h = {0, 0};
  CHECK(!fs_hitch_note(&h, 100.0)); CHECK(!fs_hitch_note(&h, 16.7));
  CHECK(fs_hitch_note(&h, 100.5)); CHECK(fs_hitch_note(&h, 500.0)); CHECK(fs_hitch_note(&h, 500.5));
  CHECK(h.over100 == 3 && h.over500 == 1);
}

// The 600-frame window is far shorter than a second on the menu (~1300 fps): its mean needs no full second.
static void tb_0054_framestats_avg_any_window() {
  FrameStats fs; fs_reset(&fs);
  CHECK_MSG(fs_avg_ms(&fs) == 0.0, "an empty window has no mean");
  fs_add(&fs, 0.001); fs_add(&fs, 0.002); fs_add(&fs, 0.0006);
  CHECK(!fs_ready(&fs));
  CHECK(fs_avg_ms(&fs) > 1.199 && fs_avg_ms(&fs) < 1.201);
  CHECK(fs.min_ms > 0.599 && fs.min_ms < 0.601 && fs.max_ms > 1.999 && fs.max_ms < 2.001);
}

static void tb_0055_stall_policy() {
  CHECK(stall_decide(299, 0, 0, 0) == STALL_SKIP); CHECK(stall_decide(300, 0, 0, 0) == STALL_SAMPLE);
  CHECK_MSG(stall_decide(799, 1, 0, 1) == STALL_SKIP, "second sample waits for 800 ms");
  CHECK(stall_decide(800, 1, 0, 1) == STALL_SAMPLE);
  CHECK_MSG(stall_decide(2999, 2, 0, 1) == STALL_SKIP, "two samples per stall");
  CHECK_MSG(stall_decide(300, 0, 0, 39) == STALL_SAMPLE, "stall 40 is sampled");
  CHECK_MSG(stall_decide(300, 0, 0, 40) == STALL_SKIP, "no stall 41");
  CHECK_MSG(stall_decide(800, 1, 0, 40) == STALL_SAMPLE, "stall 40 still gets its second sample");
}

static void tb_0056_stall_long_frames() {
  CHECK(stall_decide(3000, 2, 0, 1) == STALL_SAMPLE_LONG);
  CHECK_MSG(stall_decide(3000, 0, 0, 40) == STALL_SAMPLE_LONG, "a long frame is sampled once past the cap");
  CHECK_MSG(stall_decide(9000, 2, 1, 1) == STALL_SKIP, "only once");
  CHECK_MSG(stall_decide(3000, 1, 1, 1) == STALL_SKIP, "nothing after the long sample");
  CHECK_MSG(stall_count_after(5, STALL_SAMPLE, 0) == 6, "the first sample counts the stall");
  CHECK_MSG(stall_count_after(6, STALL_SAMPLE, 1) == 6, "the second does not");
  CHECK_MSG(stall_count_after(6, STALL_SAMPLE_LONG, 2) == 5, "a long frame gives its count back");
  CHECK_MSG(stall_count_after(6, STALL_SAMPLE_LONG, 0) == 6, "an uncounted long frame gives nothing back");
}

// Fake stack: 32 words from 0x1000; unaligned or outside reads fail.
struct StallMem { unsigned w[32]; };
static int stall_mem_read(unsigned addr, unsigned* out, void* ctx) {
  if (addr < 0x1000 || addr >= 0x1000 + 32 * 4 || (addr & 3)) return 0;
  *out = ((StallMem*)ctx)->w[(addr - 0x1000) / 4];
  return 1;
}
static void stall_mem_set(StallMem* m, unsigned addr, unsigned next, unsigned ret) {
  m->w[(addr - 0x1000) / 4] = next; m->w[(addr - 0x1000) / 4 + 1] = ret;
}
static void tb_0057_stall_walk() {
  StallMem m = {};
  stall_mem_set(&m, 0x1010, 0x1020, 0x401000);
  stall_mem_set(&m, 0x1020, 0x1040, 0x402000);
  stall_mem_set(&m, 0x1040, 0, 0x403000);
  unsigned r[8] = {};
  CHECK(stall_walk(0x1010, 0x1000, 0x1080, stall_mem_read, &m, r, 8) == 3);
  CHECK(r[0] == 0x401000 && r[1] == 0x402000 && r[2] == 0x403000);
  CHECK_MSG(stall_walk(0x1010, 0x1000, 0x1080, stall_mem_read, &m, r, 2) == 2, "max caps the walk");
  CHECK_MSG(stall_walk(0x1010, 0x1000, 0x1030, stall_mem_read, &m, r, 8) == 2, "a caller frame past hi ends it");
  CHECK_MSG(stall_walk(0x1012, 0x1000, 0x1080, stall_mem_read, &m, r, 8) == 0, "unaligned ebp");
  CHECK_MSG(stall_walk(0x1010, 0x1014, 0x1080, stall_mem_read, &m, r, 8) == 0, "ebp below lo");
  CHECK_MSG(stall_walk(0x107C, 0x1000, 0x1080, stall_mem_read, &m, r, 8) == 0, "frame without room for two words");
  stall_mem_set(&m, 0x1020, 0x1020, 0x402000);
  CHECK_MSG(stall_walk(0x1010, 0x1000, 0x1080, stall_mem_read, &m, r, 8) == 2, "a frame pointing at itself ends it");
  stall_mem_set(&m, 0x1020, 0x1010, 0x402000);
  CHECK_MSG(stall_walk(0x1010, 0x1000, 0x1080, stall_mem_read, &m, r, 8) == 2, "a frame pointing down ends it");
  stall_mem_set(&m, 0x1020, 0x1040, 0);
  CHECK_MSG(stall_walk(0x1010, 0x1000, 0x1080, stall_mem_read, &m, r, 8) == 1, "a zero return address ends it");
  stall_mem_set(&m, 0x1020, 0x2000, 0x402000);
  CHECK_MSG(stall_walk(0x1010, 0x1000, 0x3000, stall_mem_read, &m, r, 8) == 2, "an unreadable frame ends it");
}

static void tb_0058_stall_fmt_addr() {
  char b[64];
  CHECK(stall_fmt_addr(b, 64, "Games\\DNF\\System\\Engine.dll", 0x10025c6e, 0x10000000) == 18);
  CHECK(strcmp(b, "Engine.dll+0x25c6e") == 0);
  stall_fmt_addr(b, 64, "Windows/SysWOW64/ntdll.dll", 0x77001234, 0x77000000);
  CHECK(strcmp(b, "ntdll.dll+0x1234") == 0);
  CHECK(stall_fmt_addr(b, 64, 0, 0x12abcd, 0) == 10 && strcmp(b, "0x0012abcd") == 0);
  CHECK_MSG(stall_fmt_addr(b, 8, 0, 0x12abcd, 0) == 7 && strcmp(b, "0x0012a") == 0, "truncated, terminated");
}

static void tb_0059_stall_find_module() {
  const StallMod m[2] = { { 0x400000u, 0x1000u, "DukeForever.exe" }, { 0x10000000u, 0x20000u, "Engine.dll" } };
  CHECK(stall_find_module(m, 2, 0x400000u) == 0);
  CHECK(stall_find_module(m, 2, 0x400FFFu) == 0);
  CHECK_MSG(stall_find_module(m, 2, 0x401000u) == -1, "one past the end is outside");
  CHECK(stall_find_module(m, 2, 0x1001F000u) == 1);
  CHECK_MSG(stall_find_module(m, 2, 0x3FFFFFu) == -1, "below every base");
  CHECK_MSG(stall_find_module(m, 0, 0x400000u) == -1, "an empty table names nothing");
}

static void tb_0060_slowopen_rule() {
  SlowOpen so = slowopen_init();
  CHECK(!slowopen_note(&so, L"a.u", 49.9, 0));
  CHECK(slowopen_note(&so, L"a.u", 50.0, 0));
  CHECK(!slowopen_note(&so, L"a.u", 900.0, 5000));      // same path within 10 s: counted, not logged
  CHECK(slowopen_note(&so, L"a.u", 60.0, 10001));
  CHECK(so.total == 3);
}

static void tb_0061_slowopen_paths() {
  SlowOpen so = slowopen_init();
  CHECK(slowopen_note(&so, L"Game\\System\\Engine.u", 80.0, 100));
  CHECK_MSG(!slowopen_note(&so, L"game/system/ENGINE.U", 80.0, 200), "case and slashes name the same file");
  CHECK_MSG(slowopen_note(&so, L"Game\\System\\Core.u", 80.0, 300), "another path is logged at once");
  CHECK_MSG(!slowopen_note(&so, L"Game\\System\\Core.u", 80.0, 300 + 9999), "9.999 s later: not yet");
  CHECK_MSG(slowopen_note(&so, 0, 80.0, 400), "no path is still logged once");
  CHECK_MSG(slowopen_note(&so, L"x.u", 80.0, 0xFFFFFF00u) && slowopen_note(&so, L"x.u", 80.0, 0x2800u),
            "the clock wraps: 10.2 s later across the wrap is logged");
  // A full table takes a new path by evicting the one logged longest ago.
  so = slowopen_init();
  wchar_t p[16];
  for (int i = 0; i < SLOWOPEN_PATHS; ++i) {
    swprintf(p, 16, L"f%d.u", i);
    CHECK(slowopen_note(&so, p, 80.0, (unsigned)i));
  }
  CHECK_MSG(slowopen_note(&so, L"new.u", 80.0, 1000), "a path past the table is still logged");
  CHECK_MSG(slowopen_note(&so, L"f0.u", 80.0, 1001), "the evicted oldest path is new again");
  CHECK_MSG(!slowopen_note(&so, L"f5.u", 80.0, 1002), "a path still in the table is not");
  CHECK(so.total == SLOWOPEN_PATHS + 3);
}

// framecap.h's pure rules.
static void tb_0062_fc_next_deadline_cases() {
  // On schedule: returns prev + period.
  CHECK(fc_next_deadline(1000, 1050, 100) == 1100);
  // now more than a period late (a stall): resyncs to now + period rather
  // than bursting -- 1250 is 150 past next=1100, more than period=100.
  CHECK(fc_next_deadline(1000, 1250, 100) == 1350);
  // Exactly one period late is still "on schedule", not a resync.
  CHECK(fc_next_deadline(1000, 1200, 100) == 1100);
  // period 0 means no cap: always now.
  CHECK(fc_next_deadline(1000, 1234, 0) == 1234);
}
static void tb_0063_fc_wait_ticks_zero_when_past() {
  CHECK(fc_wait_ticks(1100, 1050) == 50);
  CHECK(fc_wait_ticks(1100, 1100) == 0);
  CHECK(fc_wait_ticks(1100, 1150) == 0);
}
// 64 simulated frames at 175 Hz with a 5.8 ms render: every frame's total (render + wait) is within 0.1 ms of 1/175 s
// after the first. Ticks chosen as 1/175,000,000 s so both the period (1e6 ticks) and the 5.8 ms render (1,015,000
// ticks) are exact integers.
static void tb_0064_fc_pacing_64_frames_at_175hz() {
  const long long freq = 175000000;
  const long long period = freq / 175;              // 1,000,000 ticks
  const long long renderTicks = 1015000;             // 5.8 ms
  const long long tolTicks = (long long)(0.0001 * freq);   // 0.1 ms = 17,500 ticks
  long long now = 0;
  long long deadline = now + period;                 // first frame's schedule
  for (int i = 0; i < 64; ++i) {
    const long long frameStart = now;
    now += renderTicks;                              // the render
    const long long wait = fc_wait_ticks(deadline, now);
    now += wait;                                      // the pacing wait
    if (i > 0) {
      const long long total = now - frameStart;
      const long long diff = total > period ? total - period : period - total;
      CHECK(diff <= tolTicks);
    }
    deadline = fc_next_deadline(deadline, now, period);
  }
}

// The sentinel-to-effective rule, pulled out pure so a computed default is never written into g_cfg (it would be diffed
// and persisted by config_write_changed on any unrelated change): framecap.cpp never writes g_cfg.frameCapFps; it only
// calls this against the stored value read fresh each frame.
static void tb_0065_fc_effective_fps_sentinel_rule() {
  CHECK(fc_effective_fps(0, 175) == 175);    // stored 0 (unset): follow the monitor
  CHECK(fc_effective_fps(0, 0) == 60);       // stored 0, monitor not yet known: fallback
  CHECK(fc_effective_fps(120, 175) == 120);  // an explicit player choice always wins
  CHECK(fc_effective_fps(30, 0) == 30);      // explicit, at the slider's floor
  CHECK(fc_effective_fps(240, 60) == 240);   // explicit, at the slider's ceiling
  CHECK(fc_effective_fps(-1, 0) == 60);      // any stale/garbage stored value: fallback too
}

// vsync forces at the monitor refresh, so the
// Cap slider's hi and the effective fps must both fall to it.
static void tb_0066_fc_cap_clamp_vsync_cases() {
  FcCapClamp c = fc_cap_clamp(0, 175, 1);
  CHECK(c.sliderHi == 175 && c.effectiveFps == 175);
  c = fc_cap_clamp(200, 175, 1);
  CHECK(c.sliderHi == 175 && c.effectiveFps == 175);
  c = fc_cap_clamp(120, 175, 1);
  CHECK(c.sliderHi == 175 && c.effectiveFps == 120);
  c = fc_cap_clamp(200, 175, 0);
  CHECK(c.sliderHi == 240 && c.effectiveFps == 200);
  c = fc_cap_clamp(0, 0, 1);
  CHECK(c.sliderHi == 240 && c.effectiveFps == 60);
}

// Apply once per launch, only when enabled, scriptable and the default object checks out, and only after the
// engine has been scriptable for a settle period (the menu is up; RescanDLC has run). The cases, with a
// clock at 30 ms a frame so the frame count is the binding limit (120 frames span 3570 ms).
static void tb_0067_bp_gate() {
  unsigned t = 1000;
  BpGate g = bp_init();
  CHECK(bp_step(&g, /*on*/false, /*scriptable*/true,  /*cdoOk*/true, t += 30) == BP_ACT_NONE);
  CHECK(bp_step(&g, true, false, true, t += 30) == BP_ACT_NONE);           // not scriptable yet
  for (int i = 0; i < BP_SETTLE_FRAMES - 1; i++) CHECK(bp_step(&g, true, true, true, t += 30) == BP_ACT_NONE);
  CHECK(bp_step(&g, true, true, true, t += 30) == BP_ACT_CALL);            // settle period reached
  CHECK(bp_step(&g, true, true, true, t += 30) == BP_ACT_NONE);            // once per launch
  CHECK(g.state == BP_APPLIED);
  BpGate h = bp_init();
  for (int i = 0; i < BP_SETTLE_FRAMES; i++) bp_step(&h, true, true, false, t += 30);
  CHECK(bp_step(&h, true, true, false, t += 30) == BP_ACT_NONE && h.state == BP_UNAVAILABLE);  // bad object
  CHECK(bp_step(&h, true, true, true, t += 30) == BP_ACT_NONE && h.state == BP_UNAVAILABLE);
  BpGate k = bp_init();
  for (int i = 0; i < 10; i++) bp_step(&k, true, true, true, t += 30);
  bp_step(&k, true, false, true, t += 30);                                 // scriptable dropped: restart count
  CHECK(k.scriptableFrames == 0);
  CHECK(k.state == BP_WAITING);
  BpGate o = bp_init();
  bp_step(&o, false, true, true, t += 30);
  CHECK(o.state == BP_OFF);
}

// The wall-clock floor: 120 frames inside a second never call; the call comes on the first frame at or after
// BP_SETTLE_MS since scriptable began; a drop restarts the clock; the unsigned difference survives a wrap.
static void tb_0069_bp_gate_clock() {
  BpGate g = bp_init();
  const unsigned s = 5000;
  for (int i = 0; i < BP_SETTLE_FRAMES; i++) CHECK(bp_step(&g, true, true, true, s + 8u * i) == BP_ACT_NONE);
  CHECK(g.scriptableFrames == BP_SETTLE_FRAMES && g.scriptableSinceMs == s);
  CHECK(bp_step(&g, true, true, true, s + 2999) == BP_ACT_NONE);
  CHECK(bp_step(&g, true, true, true, s + 3000) == BP_ACT_CALL);
  BpGate d = bp_init();
  for (int i = 0; i < BP_SETTLE_FRAMES; i++) bp_step(&d, true, true, true, s + 8u * i);
  bp_step(&d, true, false, true, s + 2000);                                // level load: clock restarts
  const unsigned s2 = s + 2900;
  for (int i = 0; i < BP_SETTLE_FRAMES; i++) bp_step(&d, true, true, true, s2 + (unsigned)i);
  CHECK(d.scriptableSinceMs == s2);
  CHECK(bp_step(&d, true, true, true, s + 3000) == BP_ACT_NONE);           // the old run's deadline is gone
  CHECK(bp_step(&d, true, true, true, s2 + 2999) == BP_ACT_NONE);
  CHECK(bp_step(&d, true, true, true, s2 + 3000) == BP_ACT_CALL);
  BpGate w = bp_init();
  const unsigned sw = 0xFFFFFF00u;
  for (int i = 0; i < BP_SETTLE_FRAMES; i++) bp_step(&w, true, true, true, sw + (unsigned)i);
  CHECK(bp_step(&w, true, true, true, sw + 2999u) == BP_ACT_NONE);
  CHECK(bp_step(&w, true, true, true, sw + 3000u) == BP_ACT_CALL);
}

// The default-object checks: a readable span, the Class field and PreorderGroups.Num 1..8; the row's note.
static void tb_0068_bp_cdo_checks() {
  CHECK(bp_protect_readable(0x04) && bp_protect_readable(0x20) && bp_protect_readable(0x02));
  CHECK(!bp_protect_readable(0x01) && !bp_protect_readable(0x104) && !bp_protect_readable(0x10));
  CHECK(bp_span_inside(0x1000, 0x2000, 0x1000, 0x2000));
  CHECK(!bp_span_inside(0x1000, 0x2000, 0x1000, 0x2001));
  CHECK(!bp_span_inside(0x1000, 0x2000, 0x0FFF, 0x10));
  CHECK(!bp_span_inside(0x1000, 0x2000, (uintptr_t)-0x10, 0x20));   // wraps
  static int objs_[2];  // distinct storage: an optimiser may fold equal unwritten locals
  int& cls = objs_[0]; int& other = objs_[1];
  CHECK(bp_cdo_ok(&cls, &cls, 3));
  CHECK(bp_cdo_ok(&cls, &cls, 1) && bp_cdo_ok(&cls, &cls, 8));
  CHECK(!bp_cdo_ok(&other, &cls, 3) && !bp_cdo_ok(&cls, &cls, 0) && !bp_cdo_ok(&cls, &cls, 9));
  CHECK(!bp_cdo_ok(NULL, NULL, 3));
  CHECK(strcmp(bp_state_note(BP_APPLIED), "Applied") == 0);
  CHECK(strcmp(bp_state_note(BP_WAITING), "Waiting for the menu") == 0);
  CHECK(strcmp(bp_state_note(BP_UNAVAILABLE), "Unavailable") == 0);
  CHECK(bp_state_note(BP_OFF) == NULL);
}

// The baked atlas gets its white block on a copy: 16 texels opaque, the rest as the source, the source untouched.
static unsigned char s_atlas_copy[512 * 256];
static void tb_0070_atlas_white_block() {
  CHECK(FONT_ATLAS_W == 512 && FONT_ATLAS_H == 256);
  CHECK(font_atlas[254 * 512 + 510] == 0);   // the measured bug: transparent where the fills sample
  atlas_add_white_block(s_atlas_copy, font_atlas, 512, 256, 508, 252);
  int opaque = 0;
  for (int y = 252; y < 256; y++)
    for (int x = 508; x < 512; x++) opaque += s_atlas_copy[y * 512 + x] == 255;
  CHECK(opaque == 16);
  CHECK(memcmp(s_atlas_copy, font_atlas, 512 * 252) == 0);   // every row above the block
  CHECK(s_atlas_copy[252 * 512 + 507] == font_atlas[252 * 512 + 507]);
  CHECK(s_atlas_copy[255 * 512 + 0] == font_atlas[255 * 512 + 0]);
  CHECK(s_atlas_copy[253 * 512 + 300] == font_atlas[253 * 512 + 300]);
  CHECK(font_atlas[254 * 512 + 510] == 0 && font_atlas[252 * 512 + 508] == 0);
  unsigned char tiny[4] = { 1, 2, 3, 4 }, out[4];   // a block past the edge is clipped, not written outside
  atlas_add_white_block(out, tiny, 2, 2, 1, 1);
  CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3 && out[3] == 255);
}

// The DirectInput mouse: closed passes and records the engine's intent; open releases it through one real
// Unacquire only when the engine holds it; held calls while open; close re-acquires; a new device replaces the old.
static void tb_0071_dimouse_rule() {
  int d1 = 0, d2 = 0, other = 0;
  DmState s = dm_init();
  CHECK_MSG(dm_on_open(&s, DM_SEEN_UNKNOWN) == DM_CALL_NONE && dm_on_close(&s) == DM_CALL_NONE, "no device: no calls");
  dm_on_created(&s, &d1);
  CHECK(s.device == &d1 && s.kind == DM_KIND_UNKNOWN && s.wants == 0);
  CHECK_MSG(dm_on_engine_acquire(&s, &d1, 1) == 1 && s.wants == 1, "closed: passes, intent recorded unclassified");
  CHECK(dm_needs_kind(&s, &d1) && !dm_needs_kind(&s, &other) && !dm_needs_kind(&s, 0));
  CHECK(dm_set_kind(&s, &d1, DM_KIND_MOUSE) == DM_CALL_NONE && s.kind == DM_KIND_MOUSE && !dm_needs_kind(&s, &d1));
  CHECK(dm_on_getstate(&s, &d1) == 1);
  CHECK_MSG(dm_on_engine_acquire(&s, &other, 0) == 1 && s.wants == 1, "another device: passes, not recorded");
  CHECK_MSG(dm_on_open(&s, DM_SEEN_UNKNOWN) == DM_CALL_UNACQUIRE && s.open == 1, "open, wants 1: one real Unacquire");
  CHECK_MSG(dm_on_getstate(&s, &d1) == 0, "open: the mouse reads zeros");
  CHECK(dm_on_getstate(&s, &other) == 1);
  CHECK_MSG(dm_on_engine_acquire(&s, &d1, 0) == 0 && s.wants == 0, "open: Unacquire held, recorded");
  CHECK_MSG(dm_on_engine_acquire(&s, &d1, 1) == 0 && s.wants == 1, "open: Acquire held, recorded");
  CHECK_MSG(dm_on_close(&s) == DM_CALL_ACQUIRE && s.open == 0, "close, wants 1: one real Acquire");
  CHECK(dm_on_getstate(&s, &d1) == 1);
  CHECK(dm_on_engine_acquire(&s, &d1, 0) == 1 && s.wants == 0);
  CHECK_MSG(dm_on_open(&s, DM_SEEN_UNKNOWN) == DM_CALL_NONE && dm_on_close(&s) == DM_CALL_NONE, "wants 0: no real calls");
  dm_on_engine_acquire(&s, &d1, 1);
  dm_on_created(&s, &d2);
  CHECK_MSG(s.device == &d2 && s.kind == DM_KIND_UNKNOWN && s.wants == 0, "a second device replaces the first");
  CHECK_MSG(dm_on_engine_acquire(&s, &d1, 1) == 1 && s.wants == 0, "the old device is forgotten");
  CHECK_MSG(dm_on_open(&s, DM_SEEN_UNKNOWN) == DM_CALL_NONE, "unclassified at open: nothing called");
  CHECK(dm_on_engine_acquire(&s, &d2, 1) == 1 && s.wants == 1 && dm_on_getstate(&s, &d2) == 1);
  CHECK_MSG(dm_set_kind(&s, &d2, DM_KIND_MOUSE) == DM_CALL_UNACQUIRE, "classified the mouse while open: release now");
  CHECK_MSG(dm_set_kind(&s, &d2, DM_KIND_OTHER) == DM_CALL_NONE && s.kind == DM_KIND_MOUSE, "classified once");
  CHECK(dm_on_close(&s) == DM_CALL_ACQUIRE);
  dm_forget(&s);
  CHECK_MSG(s.device == 0 && dm_on_open(&s, DM_SEEN_UNKNOWN) == DM_CALL_NONE && dm_on_close(&s) == DM_CALL_NONE, "forgotten");
  dm_on_created(&s, &d1);
  dm_set_kind(&s, &d1, DM_KIND_OTHER);
  dm_on_engine_acquire(&s, &d1, 1);
  CHECK_MSG(dm_on_open(&s, DM_SEEN_UNKNOWN) == DM_CALL_NONE && dm_on_engine_acquire(&s, &d1, 1) == 1 && dm_on_getstate(&s, &d1) == 1,
            "not a mouse: everything passes while open");
  CHECK(dm_on_close(&s) == DM_CALL_NONE);
  dm_on_created(&s, &d2);
  dm_set_kind(&s, &d2, DM_KIND_MOUSE);
  dm_on_engine_acquire(&s, &d2, 1);
  CHECK_MSG(dm_on_released(&s, &d2, 1) == 0 && s.device == &d2, "a reference left: still tracked");
  CHECK_MSG(dm_on_released(&s, &other, 0) == 0 && s.device == &d2, "another object's last Release: ignored");
  CHECK_MSG(dm_on_released(&s, &d2, 0) == 1 && s.device == 0, "the last Release forgets the device");
  CHECK_MSG(dm_on_open(&s, DM_SEEN_UNKNOWN) == DM_CALL_NONE && dm_on_close(&s) == DM_CALL_NONE, "released: no real calls");
}

// At open the device's own answer beats a stale intent: not acquired means nothing to release or re-acquire,
// unless the engine acquires while open; acquired means release and re-acquire.
static void tb_0077_dimouse_seen_at_open() {
  CHECK(dm_seen_from_hr(0) == DM_SEEN_ACQUIRED);
  CHECK_MSG(dm_seen_from_hr((long)0x8007000CL) == DM_SEEN_RELEASED, "DIERR_NOTACQUIRED");
  CHECK_MSG(dm_seen_from_hr((long)0x8007001EL) == DM_SEEN_RELEASED, "DIERR_INPUTLOST");
  CHECK_MSG(dm_seen_from_hr((long)0x80070057L) == DM_SEEN_UNKNOWN && dm_seen_from_hr(1) == DM_SEEN_UNKNOWN, "other");
  int d = 0;
  DmState s = dm_init();
  dm_on_created(&s, &d);
  dm_set_kind(&s, &d, DM_KIND_MOUSE);
  dm_on_engine_acquire(&s, &d, 1);
  CHECK_MSG(dm_on_open(&s, DM_SEEN_RELEASED) == DM_CALL_NONE && s.wants == 0, "stale wants 1: no Unacquire");
  CHECK_MSG(dm_on_close(&s) == DM_CALL_NONE, "stale wants 1: no Acquire at close");
  CHECK(dm_on_open(&s, DM_SEEN_RELEASED) == DM_CALL_NONE);
  CHECK(dm_on_engine_acquire(&s, &d, 1) == 0);
  CHECK_MSG(dm_on_close(&s) == DM_CALL_ACQUIRE, "engine Acquire while open still re-acquires at close");
  dm_on_engine_acquire(&s, &d, 0);
  CHECK_MSG(dm_on_open(&s, DM_SEEN_ACQUIRED) == DM_CALL_UNACQUIRE && s.wants == 1, "acquired: released at open");
  CHECK(dm_on_close(&s) == DM_CALL_ACQUIRE);
  CHECK_MSG(dm_on_open(&s, DM_SEEN_UNKNOWN) == DM_CALL_UNACQUIRE, "unknown: the recorded intent");
  dm_on_close(&s);
  DmState u = dm_init();
  dm_on_created(&u, &d);
  CHECK_MSG(dm_on_open(&u, DM_SEEN_ACQUIRED) == DM_CALL_NONE && u.wants == 0, "unclassified: no answer applied");
}

// Mouse classification by GetDeviceState's size and GetDeviceInfo's dwDevType; the held read's zero fill.
static void tb_0076_dimouse_kind_and_zero() {
  CHECK(dm_kind_by_size(0x14) == DM_KIND_MOUSE && dm_kind_by_size(0x10) == DM_KIND_MOUSE);
  CHECK_MSG(dm_kind_by_size(256) == DM_KIND_OTHER && dm_kind_by_size(0) == DM_KIND_OTHER, "keyboard, nothing");
  CHECK_MSG(dm_kind_by_devtype(0x112) == DM_KIND_MOUSE && dm_kind_by_devtype(0x12) == DM_KIND_MOUSE, "low byte 0x12");
  CHECK_MSG(dm_kind_by_devtype(0x413) == DM_KIND_OTHER && dm_kind_by_devtype(0x1200) == DM_KIND_OTHER, "not a mouse");
  unsigned char buf[0x15];
  memset(buf, 0xAB, sizeof buf);
  CHECK_MSG(dm_zero_state(buf, 0x14) == 0, "DI_OK");
  int zeros = 0;
  for (int i = 0; i < 0x14; i++) zeros += buf[i] == 0;
  CHECK(zeros == 0x14 && buf[0x14] == 0xAB);
  CHECK_MSG(dm_zero_state(0, 0x14) == (long)0x80070057L, "no buffer: DIERR_INVALIDPARAM");
  CHECK(dm_zero_state(buf, 0) == (long)0x80070057L && dm_zero_state(buf, 0x401) == (long)0x80070057L);
}

static void tb_0072_crash_skip_codes() {
  CHECK_MSG(crash_skip_code(0x406D1388ul), "thread naming is a signal");
  CHECK_MSG(crash_skip_code(0x40010006ul), "OutputDebugStringA is a signal");
  CHECK_MSG(crash_skip_code(0x4001000Aul), "OutputDebugStringW is a signal");
  CHECK(!crash_skip_code(0xC0000005ul));
  CHECK_MSG(!crash_skip_code(0xE06D7363ul), "a C++ throw is logged");
  CHECK(!crash_skip_code(0x80000003ul));
}

static void tb_0073_crash_log_cap() {
  CHECK_MSG(!crash_take_slot(0), "the count is 1-based");
  CHECK(crash_take_slot(1));
  CHECK(crash_take_slot(CRASH_MAX_LOGGED));
  CHECK_MSG(!crash_take_slot(CRASH_MAX_LOGGED + 1), "the 17th is counted, not logged");
  CHECK(!crash_take_slot(-5));
}

static void tb_0074_crash_av_kind() {
  CHECK(strcmp(crash_av_kind(0), "read") == 0);
  CHECK(strcmp(crash_av_kind(1), "write") == 0);
  CHECK_MSG(strcmp(crash_av_kind(8), "exec") == 0, "DEP: 8 is execute");
  CHECK(strcmp(crash_av_kind(2), "?") == 0);
}

static void tb_0075_crash_fmt_where() {
  const StallMod m[2] = { { 0x400000u, 0x1000u, "DukeForever.exe" }, { 0x10000000u, 0x200000u, "Engine.dll" } };
  char b[64];
  CHECK(crash_fmt_where(b, 64, m, 2, 0x100B9460u) == 18 && strcmp(b, "Engine.dll+0xb9460") == 0);
  CHECK(crash_fmt_where(b, 64, m, 2, 0x400010u) > 0 && strcmp(b, "DukeForever.exe+0x10") == 0);
  CHECK_MSG(crash_fmt_where(b, 64, m, 2, 0x7FFE0000u) == 10 && strcmp(b, "0x7ffe0000") == 0, "unnamed: bare address");
  CHECK_MSG(crash_fmt_where(b, 64, m, 0, 0x400010u) == 10 && strcmp(b, "0x00400010") == 0, "empty table");
}

// cursorgate.h: the engine hides the cursor by shape (SetCursor(0), the count stays >= 0), so close puts back the
// shape in force at open once the panel has set its arrow; an engine SetCursor held back while open wins.
static void tb_0078_cursor_shape_restored_on_close() {
  void* shape = (void*)1;
  CgState s = cg_on_open_shape(cg_init(), 0, 1);
  CHECK_MSG(cg_close_shape(s, &shape) == CG_SHAPE_KEEP, "the panel never changed the shape: nothing to restore");
  s = cg_note_panel_shape(s);
  CHECK_MSG(cg_close_shape(s, &shape) == CG_SHAPE_AT_OPEN && shape == 0, "the NULL shape at open comes back");
  static int objs_[2];  // distinct storage: an optimiser may fold equal unwritten locals
  int& arrow = objs_[0]; int& engine = objs_[1];
  s = cg_note_panel_shape(cg_on_open_shape(cg_init(), &arrow, 1));
  CHECK(cg_close_shape(s, &shape) == CG_SHAPE_AT_OPEN && shape == &arrow);
  s = cg_on_setcursor(s, true, &engine);
  CHECK_MSG(cg_close_shape(s, &shape) == CG_SHAPE_ENGINE && shape == &engine, "the engine's later SetCursor wins");
  s = cg_on_setcursor(cg_note_panel_shape(cg_on_open_shape(cg_init(), &arrow, 1)), true, 0);
  CHECK_MSG(cg_close_shape(s, &shape) == CG_SHAPE_ENGINE && shape == 0, "including the engine's SetCursor(0)");
  s = cg_note_panel_shape(cg_on_open_shape(cg_init(), &arrow, 0));
  CHECK_MSG(cg_close_shape(s, &shape) == CG_SHAPE_KEEP, "no GetCursorInfo at open: no shape to put back");
}

// cfgretry.h: a failed ini write is retried after 1 s, doubling to 30 s, and logged at the first failure and at the
// ceiling; the success that ends a streak reports its count once.
static void tb_0079_config_write_backoff() {
  CfgRetry r = cfgretry_init();
  CHECK_MSG(cfgretry_due(&r, 5), "no failure yet: a change is written at once");
  const unsigned kDelay[8] = {1000, 2000, 4000, 8000, 16000, 30000, 30000, 30000};
  unsigned now = 1000;
  int logs = 0, firstAt = 0, ceilingAt = 0;
  bool delaysOk = true;
  for (int i = 0; i < 8; i++) {
    const int act = cfgretry_on_fail(&r, now);
    if (act != CFGR_LOG_NONE) logs++;
    if (act == CFGR_LOG_FIRST) firstAt = (int)r.fails;
    if (act == CFGR_LOG_CEILING) ceilingAt = (int)r.fails;
    if (r.delayMs != kDelay[i] || cfgretry_due(&r, now + kDelay[i] - 1) || !cfgretry_due(&r, now + kDelay[i]))
      delaysOk = false;
    now += kDelay[i];
  }
  CHECK_MSG(delaysOk, "1, 2, 4, 8, 16 s, then 30 s for good");
  CHECK_MSG(logs == 2 && firstAt == 1 && ceilingAt == 6, "two lines per streak: the first failure, the ceiling");
  CHECK_MSG(cfgretry_on_success(&r) == 8, "the success reports the streak's count");
  CHECK_MSG(r.fails == 0 && cfgretry_due(&r, now), "and starts over: the next change is written at once");
  CHECK_MSG(cfgretry_on_success(&r) == 0, "a success with no streak logs nothing");
  CHECK_MSG(cfgretry_on_fail(&r, now) == CFGR_LOG_FIRST, "a new streak logs its first failure again");
  r = cfgretry_init();
  cfgretry_on_fail(&r, 0xFFFFFC18u);   // 1 s before GetTickCount wraps
  CHECK_MSG(!cfgretry_due(&r, 0xFFFFFFFFu) && cfgretry_due(&r, 0u), "the wait survives the tick wrap");
}

// drawgate.h: the panel key opens nothing while drawing is disabled, and each reason is logged once.
static void tb_0080_panel_open_refused_while_drawing_disabled() {
  CHECK(drawgate_reason(0, false) == 0);
  CHECK(strcmp(drawgate_reason("DrawPrimitiveUP", false), "DrawPrimitiveUP") == 0);
  CHECK_MSG(strcmp(drawgate_reason("DrawPrimitiveUP", true), "DrawPrimitiveUP") == 0, "the session-long one first");
  CHECK(strcmp(drawgate_reason(0, true), "IDirect3DDevice9::Reset") == 0);
  DrawGate g = drawgate_init();
  bool logNow = true;
  CHECK_MSG(drawgate_may_open(&g, 0, &logNow) && !logNow, "drawing works: the panel opens");
  CHECK_MSG(!drawgate_may_open(&g, "DrawPrimitiveUP", &logNow) && logNow, "refused, and logged");
  CHECK_MSG(!drawgate_may_open(&g, "DrawPrimitiveUP", &logNow) && !logNow, "refused again, not logged again");
  CHECK_MSG(!drawgate_may_open(&g, "IDirect3DDevice9::Reset", &logNow) && logNow, "another reason is logged");
  CHECK_MSG(drawgate_may_open(&g, 0, &logNow) && !logNow, "drawing back: the panel opens");
  // An episode ends when drawing comes back, whether or not the key was pressed meanwhile.
  g = drawgate_init();
  CHECK(!drawgate_may_open(&g, "IDirect3DDevice9::Reset", &logNow) && logNow);
  CHECK_MSG(!drawgate_set(&g, "IDirect3DDevice9::Reset", false), "closed panel: nothing to close");
  CHECK(!drawgate_may_open(&g, "IDirect3DDevice9::Reset", &logNow) && !logNow);
  CHECK_MSG(!drawgate_set(&g, 0, true), "drawing works: an open panel stays open");
  CHECK_MSG(!drawgate_may_open(&g, "IDirect3DDevice9::Reset", &logNow) && logNow, "a later failure logs again");
  CHECK_MSG(drawgate_set(&g, "DrawPrimitiveUP", true), "drawing lost while open: the panel closes");
}

// d3dbody.h, with the 2026-10-04 log's shape: the IDirect3D9 CreateDevice slot points outside d3d9.dll (an
// overlay's replacement) while the device's Reset and Present lie inside it.
static void tb_0081_d3d_body_verdict() {
  CHECK_MSG(d3dbody_verdict(D3DM_CREATEDEVICE, true, false) == D3DB_HOOK_LOGGED, "the overlay's CreateDevice: hooked, logged");
  CHECK(d3dbody_verdict(D3DM_RESET, true, true) == D3DB_HOOK);
  CHECK(d3dbody_verdict(D3DM_PRESENT, true, true) == D3DB_HOOK);
  CHECK(d3dbody_verdict(D3DM_CREATEDEVICE, true, true) == D3DB_HOOK);
  CHECK_MSG(d3dbody_verdict(D3DM_RESET, true, false) == D3DB_REFUSE, "Reset outside d3d9.dll: refused");
  CHECK_MSG(d3dbody_verdict(D3DM_PRESENT, true, false) == D3DB_REFUSE, "Present outside d3d9.dll: refused");
  CHECK_MSG(d3dbody_verdict(D3DM_CREATEDEVICE, false, false) == D3DB_REFUSE, "an unreadable slot is never hooked");
}

// Synthetic class and property records for the uwalk rules: the walks read them through a layout of offsets,
// exactly as they read the engine's UStruct/UField/UProperty through engine-map.md's offsets.
struct UW_FakeProp;
struct UW_FakeClass { const UW_FakeClass* super; const UW_FakeProp* children; };
struct UW_FakeProp { int name; int offset; const UW_FakeProp* next; };
static const UwLayout UW_FAKE_LAYOUT = { (int)offsetof(UW_FakeClass, super), (int)offsetof(UW_FakeClass, children),
                                         (int)offsetof(UW_FakeProp, next), (int)offsetof(UW_FakeProp, name),
                                         (int)offsetof(UW_FakeProp, offset), 0, 0 };
static UW_FakeClass uw_class(const UW_FakeClass* super) { UW_FakeClass c = { super, 0 }; return c; }
static UW_FakeProp uw_prop(int name, int offset) { UW_FakeProp p = { name, offset, 0 }; return p; }
static UW_FakeClass uw_unrelated = { 0, 0 };
static bool uw_isa(const UW_FakeClass* c, const UW_FakeClass* target, int cap) {
  return uw_isa_l(&UW_FAKE_LAYOUT, c, target, cap) != 0;
}
static UWProp uw_find_prop(const UW_FakeClass* c, int name) { return uw_find_prop_l(&UW_FAKE_LAYOUT, c, name, 32); }
// A pawn list given as the sequence of pointers the walk would follow; valid = in the snapshot's first n slots.
struct UwFakeList { const uintptr_t* seq; int len; int pos; const void* const* snap; int snapN; };
static const void* uw_fake_next(void* ctx, const void*) {
  UwFakeList* f = (UwFakeList*)ctx;
  return ++f->pos < f->len ? (const void*)f->seq[f->pos] : 0;
}
static int uw_fake_valid(void* ctx, const void* p) {
  const UwFakeList* f = (const UwFakeList*)ctx;
  for (int i = 0; i < f->snapN; i++) if (f->snap[i] == p) return 1;
  return 0;
}
static UWList uw_list_test(std::initializer_list<uintptr_t> seq, const void* const* snap, int snapN, int cap = 512) {
  UwFakeList f = { seq.begin(), (int)seq.size(), 0, snap, snapN };
  const UwListOps ops = { uw_fake_next, uw_fake_valid, &f };
  const void* out[8];
  return uw_list(seq.size() ? (const void*)*seq.begin() : 0, &ops, out, cap < 8 ? cap : 8);
}
static void tb_0082_uw_isa_depth_cap_and_cycle() {
  UW_FakeClass a = uw_class(0), b = uw_class(&a), c = uw_class(&b);
  CHECK(uw_isa(&c, &a, 32)); CHECK(!uw_isa(&a, &c, 32));
  CHECK(uw_isa(&c, &c, 32));                                            // a class is itself
  CHECK(!uw_isa(&c, &a, 1));                                            // the cap ends the walk
  a.super = &c; CHECK(!uw_isa(&a, &uw_unrelated, 32));                  // cycle: false, terminates
}
static void tb_0083_uw_find_prop_children_then_super() {
  UW_FakeProp h = uw_prop(10, 0x2A0), l = uw_prop(11, 0x150); h.next = &l;
  UW_FakeClass base = uw_class(0); base.children = &h;
  UW_FakeClass derived = uw_class(&base);
  UWProp p = uw_find_prop(&derived, 11); CHECK(p.ok && p.offset == 0x150);
  CHECK(!uw_find_prop(&derived, 99).ok);
  UW_FakeProp shadow = uw_prop(11, 0x10); derived.children = &shadow;   // the first match wins
  p = uw_find_prop(&derived, 11); CHECK(p.ok && p.offset == 0x10);
  CHECK(!uw_find_prop(&derived, 0).ok);                                 // NAME_None never matches
}
static void tb_0084_uw_pawn_list_stops() {
  const void* snap[4] = { (void*)0x1000, (void*)0x2000, (void*)0x3000, 0 };
  UWList l = uw_list_test({0x1000, 0x2000, 0x9999}, snap, 3); CHECK(l.count == 2 && l.stop == UW_STOP_FOREIGN);
  l = uw_list_test({0x1000, 0x2000, 0x1000}, snap, 3);       CHECK(l.count == 2 && l.stop == UW_STOP_CYCLE);
  l = uw_list_test({0x1000, 0}, snap, 3);                    CHECK(l.count == 1 && l.stop == UW_STOP_END);
  l = uw_list_test({0x1000, 0x2000, 0x3000}, snap, 3, 2);    CHECK(l.count == 2 && l.stop == UW_STOP_CAP);
  l = uw_list_test({0}, snap, 3);                            CHECK(l.count == 0 && l.stop == UW_STOP_END);
  CHECK(strcmp(uw_stop_name(UW_STOP_FOREIGN), "foreign") == 0);
}
static void tb_0085_view_w2s() {
  WView v = { 0, 0, 0, 0, 0, 0, 90.f, 1 }; float sx, sy;
  CHECK(view_w2s(v, 1000, 0, 0, 1920, 1080, &sx, &sy) && fabsf(sx - 960) < 0.5f && fabsf(sy - 540) < 0.5f);
  CHECK(!view_w2s(v, -1000, 0, 0, 1920, 1080, &sx, &sy));
  CHECK(view_w2s(v, 1000, 1000, 0, 1440, 1080, &sx, &sy) && fabsf(sx - 1440) < 1.f);   // edge at 45 deg (4:3)
  CHECK(view_w2s(v, 1000, 0, 100, 1920, 1080, &sx, &sy) && sy < 540);                  // +Z is up the screen
  WView yawRight = { 0, 0, 0, 0, 16384, 0, 90.f, 1 };                                   // a quarter turn: +Y ahead
  CHECK(view_w2s(yawRight, 0, 1000, 0, 1920, 1080, &sx, &sy) && fabsf(sx - 960) < 0.5f);
  CHECK(!view_w2s(yawRight, 1000, 0, 0, 1920, 1080, &sx, &sy));                         // old forward is now beside
  WView pitchUp = { 0, 0, 0, 8192, 0, 0, 90.f, 1 };                                     // 45 deg up
  CHECK(view_w2s(pitchUp, 1000, 0, 1000, 1920, 1080, &sx, &sy) && fabsf(sy - 540) < 0.5f);
  WView bad = v; bad.ok = 0;
  CHECK(!view_w2s(bad, 1000, 0, 0, 1920, 1080, &sx, &sy));
}
static void tb_0086_uw_read_health_typed() {
  unsigned char obj[8] = {}; int i = 75; float f = 42.9f; memcpy(obj, &i, 4); memcpy(obj + 4, &f, 4);
  CHECK(uw_read_health(obj, 0, UW_INT) == 75); CHECK(uw_read_health(obj, 4, UW_FLOAT) == 42);
  f = 1e20f; memcpy(obj + 4, &f, 4); CHECK(uw_read_health(obj, 4, UW_FLOAT) == 0x7FFFFFFF);   // clamped, no UB
  CHECK(uw_read_health(obj, 0, UW_OTHER) == 0);
}
static void tb_0087_world_gate() {
  CHECK(!world_gate(0, (void*)1, 1, 0)); CHECK(!world_gate((void*)1, 0, 1, 0));
  CHECK(!world_gate((void*)1, (void*)1, 0, 0)); CHECK(world_gate((void*)1, (void*)1, 1, 0));
}
static void tb_0120_uw_alive_on_raw_float() {
  unsigned char obj[8] = {}; float f = 0.5f; int i = 1; memcpy(obj, &f, 4); memcpy(obj + 4, &i, 4);
  CHECK(uw_health_alive(obj, 0, UW_FLOAT)); CHECK(uw_read_health(obj, 0, UW_FLOAT) == 0);   // alive though it reads 0
  f = 0.f; memcpy(obj, &f, 4); CHECK(!uw_health_alive(obj, 0, UW_FLOAT));
  f = -3.f; memcpy(obj, &f, 4); CHECK(!uw_health_alive(obj, 0, UW_FLOAT));
  const unsigned nanBits = 0x7FC00000u; memcpy(obj, &nanBits, 4); CHECK(!uw_health_alive(obj, 0, UW_FLOAT));
  CHECK(uw_health_alive(obj, 4, UW_INT)); i = 0; memcpy(obj + 4, &i, 4); CHECK(!uw_health_alive(obj, 4, UW_INT));
  CHECK(!uw_health_alive(obj, 4, UW_OTHER));
}
static void tb_0122_world_scan_backoff() {
  unsigned v = WORLD_SCAN_MIN; const unsigned want[] = { 60, 120, 240, 480, 600, 600 };
  for (int i = 0; i < 6; i++) { v = world_scan_backoff(v); CHECK(v == want[i]); }
  CHECK(world_scan_backoff(0) == WORLD_SCAN_MIN);
}

// ---- ESP stage: espbox.h, esp_layout.h, espnames.h, dmgtrack.h, dmgnum.h (ported from duke-dnmp's suites) ----
static void set_corners(ScreenPt c[8], float x0, float y0, float x1, float y1) {
  for (int i = 0; i < 8; ++i) { c[i].x = (i & 1) ? x1 : x0; c[i].y = (i & 2) ? y1 : y0; c[i].ok = 1; }
}
static void tb_0088_esp_box_from_corners() {
  ScreenPt c[8];
  int ok = 0;
  set_corners(c, 20.f, 30.f, 60.f, 80.f);
  EkScreenBox b = esp_box_from_corners(c, 100, 100, &ok);
  CHECK(ok && b.x == 20 && b.y == 30 && b.w == 40 && b.h == 50);
  set_corners(c, 20.f, 40.f, 40.f, 60.f);   // far corners behind: sides past the centre run to the edges
  for (int i = 4; i < 8; ++i) { c[i].x = c[i].y = 0.f; c[i].ok = 0; }
  b = esp_box_from_corners(c, 100, 100, &ok);
  CHECK(ok && b.x == 0 && b.y == 0 && b.w == 40 && b.h == 100);
  set_corners(c, 120.f, 20.f, 150.f, 60.f);
  esp_box_from_corners(c, 100, 100, &ok);
  CHECK(ok == 0);
  set_corners(c, 50.f, 50.f, 50.f, 50.f);
  b = esp_box_from_corners(c, 100, 100, &ok);
  CHECK(ok && b.x == 50 && b.y == 50 && b.w == 1 && b.h == 1);
  for (int i = 0; i < 8; ++i) c[i].ok = 0;
  esp_box_from_corners(c, 100, 100, &ok);
  CHECK(ok == 0);
}
// A view at the origin looking down +X (yaw 0), FOV 90, on a 100x100 screen: right = +Y, up = +Z.
static WView esp_test_view() { WView v = { 0.f, 0.f, 0.f, 0, 0, 0, 90.f, 1 }; return v; }
static void tb_0089_esp_pawn_box() {
  const WView v = esp_test_view();
  int ok = 0;
  EkScreenBox b = esp_pawn_box(v, 200.f, 0.f, 0.f, 10.f, 10.f, 100, 100, &ok);   // corners at 50 -+ 10/190*50 .. 10/210*50
  CHECK(ok && b.x == 47 && b.y == 47 && b.w == 6 && b.h == 6);
  esp_pawn_box(v, -200.f, 0.f, 0.f, 10.f, 10.f, 100, 100, &ok);
  CHECK_MSG(ok == 0, "behind the camera: no box, never mirrored");
  esp_pawn_box(v, 200.f, 5000.f, 0.f, 10.f, 10.f, 100, 100, &ok);
  CHECK_MSG(ok == 0, "wholly off screen");
  b = esp_pawn_box(v, 5.f, 0.f, 0.f, 10.f, 10.f, 100, 100, &ok);   // near face behind: runs to the edges
  CHECK(ok && b.x == 0 && b.y == 0 && b.w == 100 && b.h == 100);
  WView bad = v;
  bad.ok = 0;
  esp_pawn_box(bad, 200.f, 0.f, 0.f, 10.f, 10.f, 100, 100, &ok);
  CHECK(ok == 0);
  EkScreenBox e = esp_pawn_box(v, 200.f, 0.f, 0.f, 0.f, 0.f, 100, 100, &ok);   // zero-size collision: a point box
  CHECK(ok && e.w >= 1 && e.h >= 1);
  esp_pawn_box(v, 0.5f, 0.f, 0.f, 10.f, 10.f, 100, 100, &ok);   // centre behind the near plane, corners in front
  CHECK_MSG(ok == 0, "a pawn whose centre is not in front never draws");
}
static void tb_0090_esp_markers() {
  for (int dim = 2; dim <= 128; dim += 2) CHECK_MSG(esp_bracket_arm(dim) * 2 < dim, "two opposing arms never meet");
  CHECK(esp_bracket_arm(0) == 0 && esp_bracket_arm(-5) == 0 && esp_bracket_arm(30) == 10);
  CHECK(esp_bracket_thickness(0, 10) == 0);
  CHECK(esp_bar_x(100) < 100 && esp_bar_x(100) + ESP_BAR_W <= 100);
  CHECK(esp_label_x(100, 80, 40) == 120);
  CHECK(esp_name_label_y(200) < 200 && esp_dist_label_y(200, 80) > 280);
}
static void tb_0091_esp_health_label() {
  char buf[ESP_HEALTH_NUM_BUFSIZE];
  CHECK(esp_health_number(buf, 40, 0) && strcmp(buf, "40") == 0);
  CHECK(esp_health_number(buf, 40, 100) && strcmp(buf, "40/100") == 0);
  CHECK(esp_health_number(buf, 4600, 4500) && strcmp(buf, "4600/4500") == 0);
  CHECK(!esp_health_number(buf, 0, 100) && !esp_health_number(buf, -5, 100));
  const EspLabelRuns r = esp_label_runs(100, 80, 40, 30);
  CHECK(r.totalW == 40 + ESP_LABEL_RUN_GAP + 30 && r.numX == r.nameX + 40 + ESP_LABEL_RUN_GAP);
  const EspLabelRuns n = esp_label_runs(100, 80, 0, 30);
  CHECK(n.totalW == 30 && n.numX == n.nameX);
  CHECK(esp_label_mono_y(200, 17, 12) == 200 + 17 - 12);
  CHECK(canvas_bar_fill(40, 25, 100) == 10 && canvas_bar_fill(50, 500, 100) == 50 && canvas_bar_fill(50, 0, 100) == 0);
  CHECK(canvas_health_colour(80, 100) == C_GREEN && canvas_health_colour(40, 100) == C_HAZARD &&
        canvas_health_colour(10, 100) == C_RED);
}
static void tb_0092_marker_pipeline() {
  EkMarker m[8];
  memset(m, 0, sizeof(m));
  const int d[8] = { 500, 100, 300, 900, 300, 50, 700, 200 };
  for (int i = 0; i < 8; i++) { m[i].kind = EK_MARKER_ENEMY; m[i].index = i; m[i].seq = i; m[i].dist2d = d[i]; }
  EkMarker k[8];
  memcpy(k, m, sizeof(m));
  ekm_keep_nearest(k, 8, 3);
  CHECK(k[0].dist2d == 50 && k[1].dist2d == 100 && k[2].dist2d == 200);
  memcpy(k, m, sizeof(m));
  ekm_keep_nearest(k, 8, 8);
  for (int i = 0; i < 8; i++) CHECK(k[i].dist2d == d[i]);
  ekm_keep_nearest(0, 8, 3);
  EkMarker s[8];
  memcpy(s, m, sizeof(m));
  ekm_sort_far_to_near(s, 8);
  for (int i = 1; i < 8; i++) CHECK(s[i - 1].dist2d >= s[i].dist2d);
  int first = -1, second = -1;
  for (int i = 0; i < 8; i++)
    if (s[i].dist2d == 300) { if (first < 0) first = s[i].seq; else second = s[i].seq; }
  CHECK_MSG(first == 2 && second == 4, "equal distances keep collection order");
  ekm_sort_far_to_near(0, 8);
}
static void tb_0093_esp_distance() {
  CHECK(fabsf(esp_dist_m(315.f, 420.f) - 10.f) < 1e-4f);   // 3-4-5 at 525 units, 52.5 per metre
  CHECK(esp_dist_m(0.f, 0.f) == 0.f);
}
static void tb_0094_esp_label_outline() {
  int dx[8], dy[8];
  CHECK(esp_outline_offsets(ESP_LABEL_OUTLINE_R, dx, dy) == 8);
  bool noCentre = true, distinct = true;
  for (int i = 0; i < 8; i++) {
    if (dx[i] == 0 && dy[i] == 0) noCentre = false;
    for (int j = 0; j < i; j++) if (dx[i] == dx[j] && dy[i] == dy[j]) distinct = false;
  }
  CHECK(noCentre && distinct);
  CHECK(esp_outline_offsets(0, dx, dy) == 0);
  CHECK(esp_keyline_colour(0xFFE8E4DAu) == 0xFF000000u && esp_keyline_colour(0x80E8E4DAu) == 0x80000000u);
}
static void tb_0095_esp_names() {
  char out[48];
  esp_display_name("Pig_Cop", out, sizeof out);
  CHECK_MSG(strcmp(out, "Pig Cop") == 0, "no table entry: the class name, underscores as spaces");
  esp_display_name("", out, sizeof out);
  CHECK(out[0] == 0);
  esp_display_name("A_Very_Long_Class_Name_That_Overruns", out, 8);
  CHECK(strlen(out) == 7 && strncmp(out, "A Very ", 7) == 0);
  CHECK(esp_wide_to_ascii(L"Grunt", out, sizeof out) && strcmp(out, "Grunt") == 0);
  CHECK(esp_wide_to_ascii(L"G\x00e9", out, sizeof out) && strcmp(out, "G?") == 0);
}
static void tb_0096_esp_config_defaults() {
  config_defaults();
  CHECK(g_cfg.espOn == 1 && g_cfg.espBoundingBoxes == 1 && g_cfg.espHealthBars == 1 && g_cfg.espNames == 1 &&
        g_cfg.espDistance == 1);
  CHECK_MSG(g_cfg.espAllies == 0, "Allies are off by default");
  CHECK(g_cfg.dmgNumbers == 1);
  CHECK(config_key_known("esp", "enemies") && config_key_known("esp", "allies") && config_key_known("esp", "dmgnumbers"));
  CHECK(menu_tab_visible(TAB_VISUALS));
}
static void tb_0097_dmg_progress_rise_alpha() {
  CHECK(fabs(dmg_progress(1000u, 1250u) - 1.0 / 3.0) < 1e-9);
  CHECK(dmg_progress(1000u, 1750u) == 1.0);
  CHECK(fabs(dmg_progress(0xFFFFFF00u, 0xFFFFFF00u + 375u) - 0.5) < 1e-9);   // across the clock's wrap
  CHECK(dmg_rise_px(0.0, 30) == 0 && dmg_rise_px(1.0 / 3.0, 30) == 17 && dmg_rise_px(1.0, 30) == 30);
  CHECK(dmg_alpha(0.0) == 255 && dmg_alpha(2.0 / 3.0) == 255 && dmg_alpha(0.8) == 153 && dmg_alpha(1.0) == 0);
  CHECK(dmg_colour(0) == C_HAZARD && dmg_colour(1) == C_RED);
}
static DmgEvent dmg_ev(const void* victim, int damage, unsigned t0) {
  DmgEvent e = { { 0.f, 0.f, 0.f }, damage, 0, victim, t0, 1 };
  return e;
}
static void tb_0098_dmg_ring() {
  static DmgRing r;
  int a = 0, b = 0;
  dmg_reset(&r);
  dmg_push(&r, dmg_ev(&a, 1, 100u));
  dmg_push(&r, dmg_ev(&a, 2, 100u));
  CHECK(dmg_stack_index(r, 0, 200u) == 0 && dmg_stack_index(r, 1, 200u) == 1);
  dmg_push(&r, dmg_ev(&b, 3, 100u));
  dmg_push(&r, dmg_ev(&a, 4, 100u));
  CHECK(dmg_stack_index(r, 2, 200u) == 0 && dmg_stack_index(r, 3, 200u) == 2);
  CHECK(dmg_stack_index(r, 3, 100u + DMG_LIFE_MS) == 0);
  dmg_reset(&r);
  for (int i = 0; i <= DMG_MAX; ++i) dmg_push(&r, dmg_ev(&a, i, 0u));
  CHECK(r.e[0].damage == DMG_MAX && r.e[1].damage == 1 && r.next == 1);
  CHECK(dmg_is_live(r.e[0], 10u) == 1 && dmg_is_live(r.e[0], DMG_LIFE_MS) == 0);
}
// dmgtrack.h: health drops become events; heals and new pawns do not; a vanished or zeroed pawn is a kill.
static void tb_0099_dmgtrack_steps() {
  static DmgTrack t;
  t = dmgtrack_init();
  const void* A = (void*)0x100;
  const void* B = (void*)0x200;
  DmgDelta d[8];
  int n;
  { DmgSeen s[] = { {A, 100}, {B, 50} };  n = dmgtrack_step(&t, s, 2, d, 8);  CHECK(n == 0); }
  { DmgSeen s[] = { {A, 70}, {B, 60} };   n = dmgtrack_step(&t, s, 2, d, 8);
    CHECK(n == 1 && d[0].who == A && d[0].damage == 30 && !d[0].kill); }
  { DmgSeen s[] = { {A, 0}, {B, 60} };    n = dmgtrack_step(&t, s, 2, d, 8);
    CHECK(n == 1 && d[0].damage == 70 && d[0].kill); }
  { DmgSeen s[] = { {B, 20} };            n = dmgtrack_step(&t, s, 1, d, 8);
    CHECK(n == 1 && d[0].who == B && d[0].damage == 40); }   // A gone after death: no second kill
  n = dmgtrack_step(&t, 0, 0, d, 8);
  CHECK(n == 1 && d[0].who == B && d[0].kill && d[0].damage == 20);   // vanished alive: a kill
  n = dmgtrack_step(&t, 0, 0, d, 8);
  CHECK(n == 0);
}
static void tb_0100_dmgtrack_bounds() {
  static DmgTrack t;
  t = dmgtrack_init();
  static DmgSeen s[40];
  DmgDelta d[8];
  for (int i = 0; i < 40; ++i) {
    s[i].who = (const void*)(size_t)(0x1000 + i * 16);
    s[i].health = 100;
    s[i].x = (float)i; s[i].y = 0.f; s[i].z = 0.f;
  }
  CHECK(dmgtrack_step(&t, s, 40, d, 8) == 0);
  for (int i = 0; i < 40; ++i) s[i].health = 90 - i;   // 40 drops into 8 output slots: the newest 8 survive
  const int n = dmgtrack_step(&t, s, 40, d, 8);
  CHECK(n == 8 && d[7].who == s[39].who && d[0].who == s[32].who);
  CHECK_MSG(d[7].at.x == 39.f, "an event carries the pawn's last position");
  // More than DMGTRACK_MAX distinct new pawns are not tracked and never fault.
  static DmgSeen big[DMGTRACK_MAX + 8];
  for (int i = 0; i < DMGTRACK_MAX + 8; ++i) {
    big[i].who = (const void*)(size_t)(0x100000 + i * 16);
    big[i].health = 10;
    big[i].x = big[i].y = big[i].z = 0.f;
  }
  t = dmgtrack_init();
  CHECK(dmgtrack_step(&t, big, DMGTRACK_MAX + 8, d, 8) == 0 && t.n == DMGTRACK_MAX);
  CHECK(dmgtrack_step(&t, 0, 0, d, 0) == 0);   // zero output capacity is survivable
  CHECK(dmgtrack_step(0, big, 1, d, 8) == 0);
}
static void tb_0123_dmgtrack_incomplete_and_bulk() {
  static DmgTrack t;
  t = dmgtrack_init();
  const void* A = (void*)0x100;
  const void* B = (void*)0x200;
  DmgDelta d[8];
  DmgSeen both[] = { {A, 100}, {B, 50} };
  DmgSeen onlyA[] = { {A, 100} };
  CHECK(dmgtrack_step_if(&t, 1, both, 2, d, 8) == 0 && t.n == 2);
  CHECK_MSG(dmgtrack_step_if(&t, 0, onlyA, 1, d, 8) == 0 && t.n == 2, "an incomplete frame: no events, state kept");
  DmgSeen hurt[] = { {A, 100}, {B, 30} };
  const int n = dmgtrack_step_if(&t, 1, hurt, 2, d, 8);
  CHECK_MSG(n == 1 && d[0].who == B && d[0].damage == 20 && !d[0].kill, "B was held, not killed, by the cut walk");
  CHECK(!dmg_is_bulk_unload(DMGTRACK_BULK_KILLS - 1) && dmg_is_bulk_unload(DMGTRACK_BULK_KILLS));
}

// ---- ESP gate 1 fixes: Hor+ FOV, pause gate, attitude, damage-number size, draw distance ----
// FSceneNode::GetFOV (Engine.dll!0x10212630): wider than 4:3 widens the horizontal FOV, 4:3 and narrower keep it.
static void tb_0124_view_effective_hfov() {
  CHECK(fabsf(view_effective_hfov(90.f, 1024, 768, 1.f) - 90.f) < 0.01f);
  CHECK(fabsf(view_effective_hfov(90.f, 1280, 1024, 1.f) - 90.f) < 0.01f);    // 5:4, narrower: unchanged
  CHECK(fabsf(view_effective_hfov(90.f, 3440, 1440, 1.f) - 121.6f) < 0.1f);
  CHECK(fabsf(view_effective_hfov(90.f, 1920, 1080, 1.f) - 106.3f) < 0.1f);
  CHECK_MSG(fabsf(view_effective_hfov(90.f, 1024, 768, 1.1f) - 99.f) < 0.01f, "r_AspectScale multiplies first");
  // A point at the widened half-angle sits on the right edge.
  WView v = { 0, 0, 0, 0, 0, 0, 90.f, 1 }; float sx, sy;
  const double half = view_effective_hfov(90.f, 3440, 1440, 1.f) * 3.141592653589793 / 360.0;
  CHECK(view_w2s(v, (float)(1000 * cos(half)), (float)(1000 * sin(half)), 0, 3440, 1440, &sx, &sy) &&
        fabsf(sx - 3440) < 1.f && fabsf(sy - 720) < 0.5f);
}
// Paused = a non-empty LevelInfo.Pauser or LevelInfo.bPaused; a missing property never pauses.
static void tb_0125_world_paused_gate() {
  CHECK(!world_paused(1, 0, 1, 0)); CHECK(world_paused(1, 7, 1, 0)); CHECK(world_paused(1, 0, 1, 1));
  CHECK_MSG(!world_paused(1, 1, 1, 0), "a terminator-only string is empty");
  CHECK(!world_paused(0, 9, 0, 1));   // neither property resolved: keep drawing
  CHECK(world_paused(0, 0, 1, 1) && world_paused(1, 5, 0, 0));
  CHECK(!world_gate((void*)1, (void*)1, 1, 1)); CHECK(world_gate((void*)1, (void*)1, 1, 0));
}
// dnAI.dll FindRelationship (0x1003a9c0): an exact class first, else the most derived ancestor entry; no entry is
// neutral. Hostile below AIATT_Neutral, friendly above (WeightForAttitude's sign, 0x1003a790).
static const void* const kFacBase = (const void*)0x100;   // AIFaction
static const void* const kFacHuman = (const void*)0x200;  // AIFactionHuman : AIFaction
static const void* const kFacDuke = (const void*)0x300;   // AIFactionDuke : AIFactionHuman
static int fake_isa(void*, const void* cls, const void* anc) {
  for (const void* c = cls; c; c = c == kFacDuke ? kFacHuman : c == kFacHuman ? kFacBase : (const void*)0)
    if (c == anc) return 1;
  return 0;
}
static void tb_0126_world_attitude() {
  const WRel enforcer[] = { { (void*)0x900, 1 }, { kFacDuke, 2 }, { kFacHuman, 2 }, { (void*)0x901, 5 } };
  CHECK(world_attitude(enforcer, 4, kFacDuke, fake_isa, 0) == 2);
  const WRel byAncestor[] = { { kFacBase, 6 }, { kFacHuman, 1 } };
  CHECK_MSG(world_attitude(byAncestor, 2, kFacDuke, fake_isa, 0) == 1, "the most derived ancestor wins");
  const WRel twice[] = { { kFacDuke, 6 }, { kFacDuke, 2 } };
  CHECK_MSG(world_attitude(twice, 2, kFacDuke, fake_isa, 0) == 6, "the first exact entry wins");
  CHECK(world_attitude(enforcer, 4, (const void*)0x777, fake_isa, 0) == -1);   // no relationship
  CHECK_MSG(world_attitude(enforcer, 4, 0, fake_isa, 0) == -1, "player faction None matches nothing");
  int en, al, ne;
  world_classify(1, 2, 0, &en, &al, &ne); CHECK(en && !al && !ne);    // Hate: hostile
  world_classify(1, 3, 0, &en, &al, &ne); CHECK(en && !al && !ne);    // Dislike: still a negative weight
  world_classify(1, 6, 0, &en, &al, &ne); CHECK(!en && al && !ne);    // Love: friendly (EDF, civilians)
  world_classify(1, 4, 0, &en, &al, &ne); CHECK(!en && !al && ne);    // Neutral
  world_classify(1, -1, 0, &en, &al, &ne); CHECK(!en && !al && ne);   // missing relationship: neutral
  world_classify(0, 2, 0, &en, &al, &ne); CHECK(!en && !al && !ne);   // dead: nothing
  CHECK(strcmp(world_attitude_name(2), "Hate") == 0 && strcmp(world_attitude_name(-1), "none") == 0 &&
        strcmp(world_attitude_name(9), "?") == 0);
}
// Damage numbers draw at twice the canvas's text size by default.
static void tb_0127_dmg_text_scale() {
  CHECK(DMG_TEXT_SCALE == 2.0);
}
// The numbers' own atlas is rasterised at their drawn size, the UI scale times DMG_TEXT_SCALE, clamped to what
// fontgen rasterises (UI_SCALE_MIN..UI_SCALE_MAX).
static void tb_0418_dmg_atlas_pct() {
  CHECK(dmg_atlas_pct(100) == 200 && dmg_atlas_pct(75) == 150);
  CHECK(dmg_atlas_pct(150) == UI_SCALE_MAX && dmg_atlas_pct(0) == 150);   // 0 is clamped to 75 first
}
// Glyphs at the drawn size only from a built atlas of exactly that size; otherwise the scaled baked glyphs.
static void tb_0419_dmg_text_path() {
  CHECK(dmg_text_path(100, true) == DMG_TEXT_ATLAS && dmg_text_path(75, true) == DMG_TEXT_ATLAS);
  CHECK(dmg_text_path(100, false) == DMG_TEXT_BAKED);
  CHECK(dmg_text_path(150, true) == DMG_TEXT_BAKED);   // 300% is past fontgen: a clamped atlas would draw small
}
// AA's draw-distance cull (aimsolve.h ekm_within_draw_distance) in metres; Unlimited is its own term.
static void tb_0128_esp_draw_distance() {
  CHECK(ekm_within_draw_distance(10, 256, 0) && ekm_within_draw_distance(256, 256, 0));
  CHECK(!ekm_within_draw_distance(257, 256, 0) && ekm_within_draw_distance(100000, 1, 1));
  CHECK(!ekm_within_draw_distance(-1, 256, 0));
  CHECK(ESP_DRAWDIST_MIN == 1 && ESP_DRAWDIST_MAX == 256 && ESP_DRAWDIST_DEFAULT == ESP_DRAWDIST_MAX);
  config_defaults();
  CHECK(g_cfg.espDrawDist == ESP_DRAWDIST_DEFAULT && g_cfg.espNoLimit == 0);
  CHECK(config_key_known("esp", "drawdist") && config_key_known("esp", "nolimit"));
}

// The silhouette width of an off-axis pawn is the cylinder's, not the world-axis box's: at 50 degrees off the axis it
// matches f*(tan(theta+a)-tan(theta-a)) and is narrower than the 8-corner box was; straight ahead the two agree.
static void tb_0130_esp_pawn_silhouette() {
  const int W = 1000, H = 1000;
  WView v = { 0.f, 0.f, 0.f, 0, 0, 0, 170.f, 1, 0.f };
  const double pi = 3.141592653589793, f = (W * 0.5) / tan(view_effective_hfov(170.f, W, H, 1.f) * pi / 360.0);
  const float r = 50.f, h = 50.f, d = 500.f;
  const double theta = 50.0 * pi / 180.0, a = asin(r / d);
  const float px = (float)(d * cos(theta)), py = (float)(d * sin(theta));
  EspExtent e;
  CHECK(esp_pawn_extent(v, px, py, 0.f, r, h, W, H, &e));
  const double want = f * (tan(theta + a) - tan(theta - a));
  CHECK_MSG(fabs((e.x1 - e.x0) - want) < 0.02 * want, "off-axis width follows the cylinder's silhouette");
  float lo = 1e9f, hi = -1e9f, sx, sy;   // the old box: 8 world-axis corners
  for (int i = 0; i < 8; ++i) {
    CHECK(view_w2s(v, px + ((i & 1) ? r : -r), py + ((i & 2) ? r : -r), (i & 4) ? h : -h, W, H, &sx, &sy));
    if (sx < lo) lo = sx;
    if (sx > hi) hi = sx;
  }
  CHECK_MSG(e.x1 - e.x0 < hi - lo, "narrower than the 8-corner width for the same pawn");
  int ok = 0;
  const EkScreenBox b = esp_pawn_box(v, d, 0.f, 0.f, r, h, W, H, &ok);   // straight ahead: the old corners' box +-1 px
  float c0 = 1e9f, c1 = -1e9f;
  for (int i = 0; i < 8; ++i) {
    view_w2s(v, d + ((i & 1) ? r : -r), ((i & 2) ? r : -r), (i & 4) ? h : -h, W, H, &sx, &sy);
    if (sx < c0) c0 = sx;
    if (sx > c1) c1 = sx;
  }
  CHECK(ok && abs(b.x - (int)floorf(c0)) <= 1 && abs(b.x + b.w - (int)ceilf(c1)) <= 1);
  EspExtent up;
  CHECK_MSG(esp_pawn_extent(v, 0.f, 0.f, 300.f, r, h, W, H, &up) == 0 || up.x1 >= up.x0, "straight overhead is survivable");
}

// ---- ESP final-review fixes ----
// The runtime list (AIActor.Relations, what FindRelationship walks) serves when it has entries and a 12-byte
// element; otherwise the class defaults (Relationships); otherwise nothing.
static void tb_0131_world_rel_source() {
  CHECK(world_rel_layout_ok(1, 12) && !world_rel_layout_ok(1, 16) && !world_rel_layout_ok(0, 12));
  CHECK(world_rel_source(1, 3, 1, 4) == WREL_SRC_RUNTIME);
  CHECK_MSG(world_rel_source(1, 0, 1, 4) == WREL_SRC_DEFAULTS, "an empty runtime list falls back");
  CHECK(world_rel_source(0, 3, 1, 4) == WREL_SRC_DEFAULTS);   // unresolved or wrong layout
  CHECK(world_rel_source(1, 0, 1, 0) == WREL_SRC_NONE && world_rel_source(0, 0, 0, 0) == WREL_SRC_NONE);
  CHECK(world_rel_source(1, 2, 0, 0) == WREL_SRC_RUNTIME);
}
// The effective-FOV line: at once for a new scale, size or FOV source; a zoom logs once, after 30 still frames.
static void tb_0132_world_fov_log() {
  WFovLog s = {};
  CHECK(world_fov_log_step(&s, 1.f, 3440, 1440, 1, 90.f));    // first frame
  CHECK(!world_fov_log_step(&s, 1.f, 3440, 1440, 1, 90.f));
  int lines = 0;
  for (int i = 0; i < 33; i++) lines += world_fov_log_step(&s, 1.f, 3440, 1440, 1, 90.f - 1.f * (i + 1));
  CHECK_MSG(lines == 0, "no line per zoom frame");
  for (int i = 0; i < 29; i++) lines += world_fov_log_step(&s, 1.f, 3440, 1440, 1, 57.f);
  CHECK(lines == 0);
  CHECK_MSG(world_fov_log_step(&s, 1.f, 3440, 1440, 1, 57.f), "logged once the FOV held for 30 frames");
  for (int i = 0; i < 40; i++) lines += world_fov_log_step(&s, 1.f, 3440, 1440, 1, 57.f);
  CHECK(lines == 0);
  CHECK(world_fov_log_step(&s, 1.f, 1920, 1080, 1, 57.f));   // back buffer changed
  CHECK(world_fov_log_step(&s, 1.1f, 1920, 1080, 1, 57.f));  // r_AspectScale changed
  CHECK(world_fov_log_step(&s, 1.1f, 1920, 1080, 2, 57.f));  // FOV source changed
  for (int i = 0; i < 40; i++) lines += world_fov_log_step(&s, 1.1f, 1920, 1080, 2, 57.f);
  CHECK_MSG(lines == 0, "a still FOV equal to the logged one logs nothing");
}
// Pawns and relationships are read only for a feature that uses them.
static void tb_0133_world_wants_pawns() {
  CHECK(!world_wants_pawns(0, 0) && world_wants_pawns(1, 0) && world_wants_pawns(0, 1) && world_wants_pawns(1, 1));
}
// The 600-frame cost columns: average and maximum milliseconds over the window, reset per window.
static void tb_0134_ms_window() {
  MsWindow w = {};
  CHECK(ms_window_avg(w) == 0.0 && w.max == 0.0);
  ms_window_add(&w, 1.0); ms_window_add(&w, 3.0); ms_window_add(&w, 2.0);
  CHECK(fabs(ms_window_avg(w) - 2.0) < 1e-12 && w.max == 3.0 && w.n == 3);
  ms_window_reset(&w);
  CHECK(w.n == 0 && w.max == 0.0 && w.sum == 0.0);
}

// A script call's parameters go in at reflection's offsets; one that would run past ParmsSize is refused.
static void tb_0135_scallparms_put() {
  unsigned char p[16] = {};
  float fov = 110.f; int i = 7;
  CHECK(scallparms_put(p, 16, 0, &fov, 4) == 1 && memcmp(p, &fov, 4) == 0);
  CHECK(scallparms_put(p, 16, 12, &i, 4) == 1 && memcmp(p + 12, &i, 4) == 0);
  CHECK(scallparms_put(p, 16, 13, &i, 4) == 0);   // runs past ParmsSize: refused
  CHECK(scallparms_put(p, 0, 0, &i, 4) == 0);
  CHECK(scallparms_put(p, 16, 0xFFFFFFFCu, &i, 8) == 0);   // offset + size wraps: refused
  CHECK(p[12] == 7 && p[13] == 0 && p[4] == 0);   // the refused calls wrote nothing
}

// optrestore.h: on remembers the game's value, off restores it only while ours is in place, a level forgets.
static void tb_0136_optrestore() {
  OptRestore r = optrestore_init();
  r = optrestore_apply(r, /*level*/1, /*gameValue*/90.f, /*ours*/110.f);  CHECK(r.saved == 90.f && r.applied);
  float back; CHECK(optrestore_release(r, /*current*/110.f, &back) && back == 90.f);   // ours still there: restore
  CHECK(!optrestore_release(r, /*current*/75.f, &back));                              // game changed it: leave it
  r = optrestore_level(r, 2);                     CHECK(!r.applied);                  // new level: re-apply fresh
  CHECK(gs_clamp_step(3) == 10 && gs_clamp_step(402) == 400 && gs_clamp_step(52) == 50);
  OptRestore s = optrestore_apply(optrestore_init(), 1, 90.f, 110.f);
  s = optrestore_apply(s, 1, 110.f, 120.f);
  CHECK_MSG(s.saved == 90.f && s.ours == 120.f, "a slider move keeps the game's value");
  s = optrestore_apply(s, 1, 75.f, 120.f);
  CHECK_MSG(s.saved == 75.f, "the game changed it under ours: its newer value is the one restored");
  s = optrestore_apply(s, 2, 120.f, 120.f);
  CHECK_MSG(s.saved == 120.f, "another level: what is there now is the game's");
  CHECK(optrestore_level(s, 2).applied == 1);
  CHECK(!optrestore_release(optrestore_init(), 0.f, &back));
}
static void tb_0137_opt_step() {
  OptRestore r = optrestore_init();
  CHECK(opt_step(r, 0, 1.f, 0.5f) == OPT_NONE);
  CHECK(opt_step(r, 1, 1.f, 0.5f) == OPT_APPLY);
  r = optrestore_apply(r, 1, 1.f, 0.5f);
  CHECK_MSG(opt_step(r, 1, 0.5f, 0.5f) == OPT_NONE, "ours in place: nothing every frame");
  CHECK_MSG(opt_step(r, 1, 1.f, 0.5f) == OPT_APPLY, "the game put its value back (a level start): re-apply");
  CHECK_MSG(opt_step(r, 1, 0.5f, 0.75f) == OPT_APPLY, "a new slider value");
  CHECK(opt_step(r, 0, 0.5f, 0.5f) == OPT_RELEASE && opt_step(optrestore_level(r, 2), 0, 1.f, 0.5f) == OPT_NONE);
}
// duke-dnmp's FOV rules (zoom.h), the stock being DefaultFOV here, and the modifier's ratio.
static void tb_0138_fov_rules() {
  CHECK_MSG(fabs(fov_want_deg(FOV_STOCK, 85.0) - 85.0) < 1e-9, "0 is the game's own value, exactly");
  CHECK(fov_want_deg(90, 85.0) == 90.0);
  CHECK(fov_want_deg(20, 85.0) == (double)FOV_MIN_DEG && fov_want_deg(400, 85.0) == (double)FOV_MAX_DEG);
  CHECK(fov_slider_value(FOV_STOCK, 79.7553) == 80 && fov_slider_value(FOV_STOCK, 0.0) == FOV_MIN_DEG);
  CHECK(fov_slider_value(120, 79.7553) == 120 && fov_slider_value(7, 79.7553) == FOV_MIN_DEG);
  CHECK(fov_cfg_from_slider(FOV_STOCK) == FOV_STOCK && fov_cfg_from_slider(10) == FOV_MIN_DEG);
  CHECK(fov_cfg_from_slider(999) == FOV_MAX_DEG && fov_cfg_from_slider(95) == 95);
  char b[16];
  fov_value_text(FOV_STOCK, 79.7553, 1, b, sizeof b);
  CHECK_MSG(strcmp(b, "79.8") == 0, b);
  fov_value_text(FOV_STOCK, 106.6, 1, b, sizeof b);
  CHECK(strcmp(b, "107") == 0);
  fov_value_text(101, 79.7553, 1, b, sizeof b);
  CHECK(strcmp(b, "101") == 0);
  fov_value_text(FOV_STOCK, 0.0, 0, b, sizeof b);
  CHECK(strcmp(b, "--") == 0);
  bool fits = true;
  for (int v = FOV_MIN_DEG; v <= FOV_MAX_DEG; v += 7) {
    fov_value_text(v, 0.0, 1, b, sizeof b);
    if (strlen(b) > strlen(FOV_READOUT_RESERVE)) fits = false;
  }
  CHECK_MSG(fits, "every readout fits the reserve");
  CHECK(fabs(fov_ratio(110.0, 90.0) - 110.0 / 90.0) < 1e-6 && fov_ratio(90.0, 90.0) == 1.f);
  CHECK_MSG(fov_ratio(110.0, 0.0) == 0.f && fov_ratio(110.0, 180.0) == 0.f, "no usable DefaultFOV: no ratio");
  CHECK(fov_current(0, 1.2f) == FOV_ABSENT && fov_current(1, 1.2f) == 1.2f);
  // The game's own FOV option changed while ours is on: the next apply follows it, and off clears the modifier.
  OptRestore r = optrestore_apply(optrestore_init(), 1, fov_current(0, 0.f), fov_ratio(110.0, 90.0));
  const float ours2 = fov_ratio(110.0, 100.0);
  CHECK(opt_step(r, 1, r.ours, ours2) == OPT_APPLY);
  r = optrestore_apply(r, 1, r.ours, ours2);
  float back = 1.f;
  CHECK_MSG(optrestore_release(r, ours2, &back) && back == FOV_ABSENT, "off: the modifier the game never had goes");
}
// duke-dnmp's speedrule.h settings and DebugSlomo's range.
static void tb_0139_gs_settings() {
  CHECK(gs_clamp_step(50) == 50 && gs_clamp_step(53) == 55 && gs_clamp_step(398) == 400);
  CHECK(gs_clamp_step(-40) == 10 && gs_clamp_step(999) == 400 && gs_clamp_step(GS_PCT_DEF) == 50);
  CHECK(GS_PCT_MIN == 10 && GS_PCT_MAX == 400 && GS_PCT_STEP == 5);
  CHECK(gs_slomo(10) == 0.1f && gs_slomo(400) == 4.f && gs_slomo(100) == 1.f && gs_slomo(3) == 0.1f);
  config_defaults();
  CHECK(g_cfg.gameSpeedPct == 50 && g_cfg.gameSpeedKey == 0 && config_bind_get(BIND_ROW_GAMESPEED) == 0);
  CHECK(config_key_known("game", "speed") && config_key_known("game", "speedkey"));
  CHECK_MSG(!config_key_known("game", "on"), "the switch is not saved: off at every launch");
  config_bind_set(BIND_ROW_GAMESPEED, 0x75);
  CHECK(g_cfg.gameSpeedKey == 0x75 && config_bind_get(BIND_ROW_GAMESPEED) == 0x75);
  config_defaults();
  g_cfgDirty = 0;
}
// The accuracy restore text round-trips each float; a record survives a level only on the same class object.
static void tb_0140_accuracy_rules() {
  static const float kV[] = { 0.f, 0.1f, 0.035f, 1.f / 3.f, 123.456f, 1e-7f, 2.5e6f };
  bool trip = true;
  wchar_t t[32];
  for (int i = 0; i < (int)(sizeof kV / sizeof kV[0]); i++) {
    acc_value_text(kV[i], t, 32);
    if ((float)wcstod(t, 0) != kV[i]) trip = false;
  }
  CHECK_MSG(trip, "%.9g reads back to the same float");
  acc_value_text(0.f, t, 32);
  CHECK(wcscmp(t, L"0") == 0);
  CHECK(acc_keep_record(1, 77, 77) && !acc_keep_record(0, 77, 77) && !acc_keep_record(1, 78, 77));
  CHECK(wcscmp(ACC_PROP_NAMES[0], L"HorizShotError") == 0 && wcscmp(ACC_PROP_NAMES[3], L"ShrunkVertShotError") == 0);
  config_defaults();
  CHECK(g_cfg.fovOn == 0 && g_cfg.fovDeg == FOV_STOCK && g_cfg.accuracyOn == 0);
  CHECK(config_key_known("view", "fov") && config_key_known("view", "fovdeg") &&
        config_key_known("weapons", "accuracy"));
}

// Review C1: a checkpoint reload brings back a pawn already carrying our ConsoleFOV; off still clears it.
static void tb_0141_fov_release_after_reload() {
  const float ours = fov_ratio(110.0, 90.0);
  OptRestore r = fov_apply(optrestore_init(), 1, FOV_ABSENT, ours);
  r = optrestore_level(r, 2);   // death: the checkpoint's pawn, a new level serial
  CHECK(opt_step(r, 1, ours, ours) == OPT_APPLY);
  r = fov_apply(r, 2, ours, ours);
  float back = 1.f;
  CHECK_MSG(optrestore_release(r, ours, &back) && back == FOV_ABSENT, "off clears: never our old target back");
  r = fov_apply(r, 2, 1.5f, ours);   // a target that is not ours, found while on
  CHECK_MSG(r.saved == FOV_ABSENT, "whatever was in place, the release value stays ABSENT");
  CHECK(!optrestore_release(r, 1.5f, &back));   // not ours at release: left
}
// With Custom FOV off, a ConsoleFOV active at a level's first check is cleared, once.
static void tb_0142_fov_clear_carried() {
  CHECK(fov_clear_carried(0, 1, 1));
  CHECK_MSG(!fov_clear_carried(1, 1, 1), "on: our apply takes it over");
  CHECK_MSG(!fov_clear_carried(0, 0, 1), "once per level, not every check");
  CHECK(!fov_clear_carried(0, 1, 0));
}
// The status line counts each gameplay switch while on and available (menu clean-up).
static void tb_0143_menu_live_count() {
  const MenuLive a[] = { { "Perfect accuracy", 0, 0 }, { "Game speed", 0, 0 } };
  CHECK(menu_live_count(a, 2) == 0);
  const MenuLive b[] = { { "Perfect accuracy", 1, 0 }, { "Game speed", 1, 0 } };
  CHECK(menu_live_count(b, 2) == 2);
  const MenuLive c[] = { { "Perfect accuracy", 1, 1 }, { "Game speed", 1, 1 } };
  CHECK_MSG(menu_live_count(c, 2) == 0, "a refused switch does not count");
}
// The fire probe: a rise of PlayerPawn.AccuracyShots opens a window that keeps the first and peak view kick and
// closes after ACC_PROBE_FRAMES frames; each (class, on/off) is logged once.
static void tb_0144_accuracy_probe() {
  AccProbe p = acc_probe_init();
  CHECK_MSG(!acc_probe_step(&p, 5, 0, 0) && !p.open, "the first reading only sets the baseline");
  CHECK(!acc_probe_step(&p, 5, 300, 0) && !p.open);
  CHECK(!acc_probe_step(&p, 6, -120, 40) && p.open);
  int closed = 0, n = 1;
  for (int k = 0; k < 64 && !closed; k++, n++) closed = acc_probe_step(&p, 7, k == 0 ? -350 : 10, k == 1 ? -90 : 0);
  CHECK_MSG(closed && n == ACC_PROBE_FRAMES, "closes on the window's last frame; a second shot inside it is ignored");
  CHECK(p.firstPitch == -120 && p.firstYaw == 40 && p.peakPitch == 350 && p.peakYaw == 90 && !p.open);
  CHECK_MSG(!acc_probe_step(&p, 3, 0, 0) && !p.open, "a lower count (a new pawn) is a new baseline, not a shot");
  CHECK(!acc_probe_step(&p, 4, 0, 0) && p.open);
  unsigned mask = 0;
  CHECK(acc_probe_claim(&mask, 0) && !acc_probe_claim(&mask, 0) && acc_probe_claim(&mask, 1) && !acc_probe_claim(&mask, 1));
  CHECK(acc_rot_deg(16384) == 90.0 && acc_rot_deg(-8192) == -45.0);
}
// No recoil: each kick's ShakeMagnitude is restored exactly, re-applied when the game rewrites it, logged once per
// level, and only the held weapon's class gets a record.
struct TbRec { const void* cls; int clsName; };
static void tb_0145_recoil_rules() {
  static const int kM[] = { 0, 800, 400, 2048, -1500, 65535, 2147483647 };
  bool trip = true;
  wchar_t t[32];
  for (int i = 0; i < (int)(sizeof kM / sizeof kM[0]); i++) {
    rk_int_text(kM[i], t, 32);
    if (_wtoi(t) != kM[i]) trip = false;
  }
  CHECK_MSG(trip, "the restore text reads back to the same int");
  OptRestore r = optrestore_init();
  CHECK(opt_step(r, 1, 800.f, 0.f) == OPT_APPLY);
  r = optrestore_apply(r, 0, 800.f, 0.f);
  CHECK_MSG(opt_step(r, 1, 0.f, 0.f) == OPT_NONE, "ours in place: no write");
  CHECK_MSG(opt_step(r, 1, 800.f, 0.f) == OPT_APPLY, "the game wrote it back (a reload): applied again");
  r = optrestore_apply(r, 0, 800.f, 0.f);
  float back = 0.f;
  CHECK(opt_step(r, 0, 0.f, 0.f) == OPT_RELEASE && optrestore_release(r, 0.f, &back) && (int)back == 800);
  CHECK_MSG(!optrestore_release(r, 500.f, &back), "changed by the game since: left");
  CHECK(acc_log_due(0, 1) && !acc_log_due(3, 3) && acc_log_due(3, 4));
  CHECK(rk_kick_count(-1) == 0 && rk_kick_count(2) == 2 && rk_kick_count(1000) == RK_MAX_KICKS);
  // GlobalSetProperty writes at default + offset (32-bit): the offset lands on kick i's field either side.
  const unsigned cdo = 0x20000000u, below = 0x10000000u, above = 0x30000000u;
  CHECK(cdo + (unsigned)rk_field_offset(cdo, below, 1, 0x48, 0x18) == below + 0x48 + 0x18);
  CHECK(cdo + (unsigned)rk_field_offset(cdo, above, 0, 0x48, 0x18) == above + 0x18);
  TbRec recs[2];
  int n = 0, added = 0;
  const int a = 1, b = 2, c = 3;
  CHECK_MSG(acc_slot(recs, &n, 2, (const void*)0, 0, &added) == -1 && n == 0, "no held weapon: no record");
  CHECK(acc_slot(recs, &n, 2, &a, 11, &added) == 0 && added && n == 1);
  CHECK(acc_slot(recs, &n, 2, &a, 11, &added) == 0 && !added && n == 1);
  CHECK(acc_slot(recs, &n, 2, &b, 12, &added) == 1 && added && n == 2);
  CHECK_MSG(acc_slot(recs, &n, 2, &c, 13, &added) == -1 && n == 2, "full: left alone");
  CHECK_MSG(recs[0].cls == &a && recs[1].cls == &b, "only the classes the player held");
  const MenuLive rk[] = { { "No recoil", 1, 0 } }, rkRefused[] = { { "No recoil", 1, 1 } };
  CHECK(menu_live_count(rk, 1) == 1 && menu_live_count(rkRefused, 1) == 0);
  config_defaults();
  CHECK(g_cfg.recoilOn == 0 && config_key_known("weapons", "norecoil"));
  unsigned mask = 0;
  CHECK(acc_probe_claim(&mask, 3) && !acc_probe_claim(&mask, 3) && acc_probe_claim(&mask, 2));
}

// display.h: the monitor holding most of the window; none -> primary; no monitors -> -1.
static void tb_0160_bl_pick_monitor() {
  const BlRect mons[2] = { { 0, 0, 3440, 1440 }, { 3440, 0, 5360, 1080 } };
  const BlRect onRight = { 3000, 100, 4400, 900 };   // 440 px on 0, 960 px on 1
  CHECK(bl_pick_monitor(mons, 2, onRight, 0) == 1);
  const BlRect onLeft = { 100, 100, 1124, 868 };
  CHECK(bl_pick_monitor(mons, 2, onLeft, 1) == 0);
  const BlRect nowhere = { -5000, -5000, -4000, -4000 };
  CHECK_MSG(bl_pick_monitor(mons, 2, nowhere, 1) == 1, "none overlapping: the primary");
  CHECK_MSG(bl_pick_monitor(mons, 2, nowhere, 7) == 0, "an invalid primary: the first");
  CHECK(bl_pick_monitor(mons, 0, onLeft, 0) == -1);
  const BlRect big[2] = { { 0, 0, 40000, 40000 }, { 40000, 0, 80000, 40000 } };
  const BlRect span = { 30000, 0, 80000, 40000 };
  CHECK_MSG(bl_pick_monitor(big, 2, span, 0) == 1, "areas past 2^31 compare in 64 bits");
}
// display.h: fullscreen in -> windowed out, size kept, refresh 0; off or already windowed -> untouched.
static void tb_0161_bl_rewrite_pp() {
  D3DPRESENT_PARAMETERS fs;
  memset(&fs, 0, sizeof fs);
  fs.BackBufferWidth = 3440; fs.BackBufferHeight = 1440; fs.BackBufferFormat = D3DFMT_A8R8G8B8;
  fs.BackBufferCount = 1; fs.SwapEffect = D3DSWAPEFFECT_DISCARD; fs.Windowed = FALSE;
  fs.FullScreen_RefreshRateInHz = 144; fs.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
  D3DPRESENT_PARAMETERS p = fs;
  CHECK(bl_rewrite_pp(1, &p));
  CHECK(p.Windowed == TRUE && p.FullScreen_RefreshRateInHz == 0);
  CHECK_MSG(p.BackBufferWidth == 3440 && p.BackBufferHeight == 1440 && p.BackBufferFormat == D3DFMT_A8R8G8B8 &&
            p.BackBufferCount == 1 && p.PresentationInterval == D3DPRESENT_INTERVAL_IMMEDIATE &&
            p.SwapEffect == D3DSWAPEFFECT_DISCARD, "everything else kept");
  p = fs;
  CHECK(!bl_rewrite_pp(0, &p) && memcmp(&p, &fs, sizeof p) == 0);
  D3DPRESENT_PARAMETERS w = fs;
  w.Windowed = TRUE; w.FullScreen_RefreshRateInHz = 0;
  p = w;
  CHECK_MSG(!bl_rewrite_pp(1, &p) && memcmp(&p, &w, sizeof p) == 0, "a windowed request is the engine's own");
  CHECK(!bl_rewrite_pp(1, NULL));
  // d3d9's write-backs reach the engine's struct; our Windowed and refresh never do.
  D3DPRESENT_PARAMETERS eng = fs, ours = fs;
  bl_rewrite_pp(1, &ours);
  ours.BackBufferCount = 2; ours.BackBufferFormat = D3DFMT_X8R8G8B8; ours.BackBufferWidth = 1; ours.BackBufferHeight = 2;
  bl_params_back(&eng, &ours);
  CHECK(eng.BackBufferCount == 2 && eng.BackBufferFormat == D3DFMT_X8R8G8B8 && eng.BackBufferWidth == 1 &&
        eng.BackBufferHeight == 2);
  CHECK(eng.Windowed == FALSE && eng.FullScreen_RefreshRateInHz == 144);
}
// display.h: the engine's style writes while borderless is active; its window moves pinned to the monitor.
static void tb_0162_bl_style_and_pos() {
  CHECK(bl_style_rule(0, 0x10CB0000u) == BL_STYLE_KEEP);
  CHECK_MSG(bl_style_rule(1, 0x100B0000u) == BL_STYLE_OURS, "entering fullscreen strips the caption: ours");
  CHECK_MSG(bl_style_rule(1, 0x10CB0000u) == BL_STYLE_LEAVE, "a caption added back: the engine leaves fullscreen");
  CHECK(bl_style_rule(1, 0x90000000u) == BL_STYLE_OURS);
  CHECK(bl_popup_style(0x100B0000u) == BL_STYLE_POPUP);
  CHECK_MSG(bl_popup_style(0x300B0000u) == (BL_STYLE_POPUP | 0x20000000u), "a minimised window stays minimised");
  const BlRect mon = { 3440, 0, 5360, 1080 };
  BlPos p = { 100, 100, 1024, 768 };
  CHECK(bl_fix_pos(0, &p, mon) && p.x == 3440 && p.y == 0 && p.cx == 1920 && p.cy == 1080);
  CHECK_MSG(!bl_fix_pos(0, &p, mon), "already there: unchanged");
  BlPos q = { 7, 8, 9, 10 };
  CHECK(bl_fix_pos(BL_SWP_NOMOVE | BL_SWP_NOSIZE, &q, mon) == false && q.x == 7 && q.cx == 9);
  CHECK(bl_fix_pos(BL_SWP_NOSIZE, &q, mon) && q.x == 3440 && q.y == 0 && q.cx == 9 && q.cy == 10);
  BlPos m = { -32000, -32000, 160, 28 };
  CHECK_MSG(!bl_fix_pos(0, &m, mon) && m.x == -32000, "a minimise is left alone");
  // Leaving fullscreen puts the engine's own chrome back, not our popup with a caption.
  CHECK(bl_leave_style(0x100B0000u, 0x90C00000u) == 0x10CB0000u);
  CHECK_MSG(bl_leave_style(0x100B0000u, 0xB0C00000u) == 0x30CB0000u, "minimised now: stays minimised");
  CHECK_MSG(bl_leave_style(0x300B0000u, 0x90C00000u) == 0x10CB0000u, "minimised when saved: not now");
  // Only a refusal of the windowed params falls back to exclusive; a lost device goes back to the engine.
  CHECK(bl_fallback(D3DERR_INVALIDCALL) && bl_fallback(D3DERR_NOTAVAILABLE));
  CHECK(!bl_fallback(D3DERR_DEVICELOST) && !bl_fallback(D3DERR_OUTOFVIDEOMEMORY) && !bl_fallback(D3D_OK));
}
// display.h: the window messages whose engine handler can persist StartupFullscreen are bracketed; the window
// closing is logged whatever the flag does; a change reads as ended or begun, one unreadable side as unread.
static void tb_0464_fs_watch() {
  CHECK(fs_watch_msg(WM_ACTIVATE, WA_INACTIVE) && fs_watch_msg(WM_ACTIVATEAPP, 0) && fs_watch_msg(WM_DESTROY, 0));
  CHECK(fs_watch_msg(WM_CLOSE, 0) && fs_watch_msg(WM_SIZE, SIZE_RESTORED) && fs_watch_msg(WM_SYSCOMMAND, SC_MAXIMIZE));
  CHECK_MSG(fs_watch_msg(WM_SYSKEYDOWN, VK_RETURN) && fs_watch_msg(WM_KEYDOWN, VK_RETURN), "Alt+Enter");
  CHECK_MSG(fs_watch_msg(WM_SYSKEYDOWN, VK_F4), "Alt+F4: DestroyWindow during a load");
  CHECK_MSG(!fs_watch_msg(WM_KEYDOWN, 'W') && !fs_watch_msg(WM_MOUSEMOVE, 0) && !fs_watch_msg(WM_SYSKEYUP, VK_RETURN),
            "other input is never bracketed");
  CHECK(fs_always_log(WM_CLOSE, 0) && fs_always_log(WM_DESTROY, 0));
  CHECK_MSG(fs_always_log(WM_SYSCOMMAND, SC_CLOSE | 3), "SC_CLOSE, low bits ignored");
  CHECK(!fs_always_log(WM_SYSCOMMAND, SC_MINIMIZE) && !fs_always_log(WM_ACTIVATE, 0));
  CHECK(fs_change(1, 0) == FS_ENDED && fs_change(0, 1) == FS_BEGAN);
  CHECK(fs_change(1, 1) == FS_SAME && fs_change(0, 0) == FS_SAME);
  CHECK_MSG(fs_change(-1, -1) == FS_SAME, "never resolved: nothing known changed");
  CHECK_MSG(fs_change(1, -1) == FS_UNREAD && fs_change(-1, 0) == FS_UNREAD, "one side unreadable");
  CHECK(strcmp(fs_msg_name(WM_ACTIVATEAPP), "WM_ACTIVATEAPP") == 0 && strcmp(fs_msg_name(WM_CLOSE), "WM_CLOSE") == 0);
  CHECK(strcmp(fs_msg_name(0x7FFF), "message") == 0);
}

// config.h: the launch peek of [display] borderless leaves what config_load_from leaves.
static void tb_0163_borderless_peek() {
  static const char* const kTexts[] = {
    "[display]\r\nborderless=1\r\n",
    "[Display]\r\n  Borderless = 1 \r\n",
    "[display]\r\nborderless=2\r\n",
    "[display]\r\nborderless=1\r\nborderless=x\r\n",
    "[panel]\r\nborderless=1\r\n",
    "[display]\r\n; borderless=1\r\n",
    "",
  };
  static const int kWant[] = { 1, 1, 0, 1, 0, 0, 0 };
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_blpeek");
  for (int i = 0; i < (int)(sizeof kTexts / sizeof kTexts[0]); i++) {
    const int peek = config_peek_borderless_text(kTexts[i], (int)strlen(kTexts[i]));
    write_text(path, kTexts[i]);
    config_load_from(path);
    if (peek != kWant[i] || g_cfg.borderless != kWant[i])
      printf("  text %d: peek %d, config_load_from %d, want %d\n", i, peek, g_cfg.borderless, kWant[i]);
    CHECK_MSG(peek == kWant[i] && g_cfg.borderless == kWant[i], "peek and loader agree");
  }
  DeleteFileA(path);
  config_defaults();
  CHECK_MSG(g_cfg.borderless == 0 && config_key_known("display", "borderless"), "default off");
}

// display.h: borderless, game window not in front: the engine's mouse moves and cursor pins are held; a clip
// release (NULL) always passes; in front or borderless off nothing changes.
static void tb_0164_bl_bg_hold() {
  CHECK_MSG(bl_bg_hold(1, 0), "borderless, another window in front: held");
  CHECK_MSG(!bl_bg_hold(1, 1), "in front: the engine's own");
  CHECK_MSG(!bl_bg_hold(0, 0), "borderless off: untouched");
  CHECK(bl_bg_hold_clip(1, 0, 0));
  CHECK_MSG(!bl_bg_hold_clip(1, 0, 1), "ClipCursor(NULL), the engine's release, passes");
  CHECK(!bl_bg_hold_clip(1, 1, 0) && !bl_bg_hold_clip(0, 0, 0));
}

// cvars.h: the game's value is remembered at the first force and given back at release; a value the game writes
// while forced is the one given back; released, a changed game value is followed (nothing stale is restored).
static void tb_0180_cvar_hold() {
  CvarHold h = cvar_hold_init();
  int w = -1;
  CHECK_MSG(cvar_step(&h, 1, 1, 0, &w) == CVAR_FORCE && w == 0 && h.held && h.saved == 1, "first force: 1 kept, 0 set");
  CHECK_MSG(cvar_step(&h, 1, 0, 0, &w) == CVAR_NONE, "ours in place: nothing");
  w = -1;
  CHECK_MSG(cvar_step(&h, 1, 2, 0, &w) == CVAR_FORCE && w == 0 && h.saved == 2, "the game wrote 2: kept, forced again");
  w = -1;
  CHECK_MSG(cvar_step(&h, 0, 0, 0, &w) == CVAR_RESTORE && w == 2 && !h.held, "release: the game's latest back");
  CHECK_MSG(cvar_step(&h, 0, 3, 0, &w) == CVAR_NONE && !h.held, "released: the game's own changes are left");
  CHECK_MSG(cvar_step(&h, 1, 3, 0, &w) == CVAR_FORCE && h.saved == 3, "forced again: the followed value is kept");
  CHECK_MSG(cvar_step(&h, 0, 5, 0, &w) == CVAR_NONE && !h.held, "changed by the game since: left at release");
  CvarHold z = cvar_hold_init();
  CHECK_MSG(cvar_step(&z, 1, 0, 0, &w) == CVAR_NONE && z.held, "already off: held, nothing written");
  CHECK_MSG(cvar_step(&z, 0, 0, 0, &w) == CVAR_NONE && !z.held, "nothing to undo");
}

// focuskeep.h: the flag is 1 only once the game is in front with the option on, and changes only in front.
static void tb_0181_focus_flag() {
  CvarHold h = cvar_hold_init();
  int w = -1;
  CHECK_MSG(focus_flag_step(&h, 1, 0, 0, &w) == CVAR_NONE && !h.held, "on, not in front: unchanged");
  CHECK_MSG(focus_flag_step(&h, 1, 1, 0, &w) == CVAR_FORCE && w == 1, "on, in front: 1");
  CHECK_MSG(focus_flag_step(&h, 1, 0, 1, &w) == CVAR_NONE, "alt-tabbed: held");
  CHECK_MSG(focus_flag_step(&h, 0, 0, 1, &w) == CVAR_NONE && h.held, "off while away: waits for the front");
  w = -1;
  CHECK_MSG(focus_flag_step(&h, 0, 1, 1, &w) == CVAR_RESTORE && w == 0 && !h.held, "off in front: the engine's 0");
  CvarHold c = cvar_hold_init();   // launched with -IGNOREFOCUSCHANGES: the engine's 1 stays
  CHECK(focus_flag_step(&c, 1, 1, 1, &w) == CVAR_NONE && focus_flag_step(&c, 0, 1, 1, &w) == CVAR_NONE);
}

// postfx.h / config: every switch shows its effect by default and names its own variable; Keep running is off.
static void tb_0182_postfx_config() {
  config_defaults();
  CHECK(g_cfg.keepRunning == 0 && config_key_known("display", "keeprunning"));
  int on = 0, distinct = 1;
  for (int i = 0; i < PFX_COUNT; i++) {
    on += g_cfg.*(postfx_info(i).field) == 1;
    for (int j = 0; j < i; j++)
      if (wcscmp(postfx_info(i).var[0].cvar, postfx_info(j).var[0].cvar) == 0 ||
          postfx_info(i).field == postfx_info(j).field)
        distinct = 0;
  }
  CHECK_MSG(on == PFX_COUNT && distinct, "all shown by default, one variable and one key each");
  CHECK(config_key_known("postfx", "blur") && config_key_known("postfx", "pinkovision"));
}

// progress.h: the built-in chapter table is progress.md's (24 chapters, 65 entries, 34 distinct ids, 3 in none),
// the cap awards sum to 60 and the part masks hold exactly the B and C maps.
static void tb_0170_pg_builtin_table() {
  PgTable t;
  pg_table_builtin(&t);
  int entries = 0, seen[PG_AWARDS] = {}, distinct = 0;
  for (int i = 0; i < t.n; ++i) {
    entries += t.awardN[i];
    for (int j = 0; j < t.awardN[i]; ++j)
      if (t.awards[i][j] < PG_AWARDS && !seen[t.awards[i][j]]++) distinct++;
  }
  CHECK(t.n == 24 && entries == 65 && distinct == 34 && !seen[3]);
  CHECK(strcmp(t.url[4], "map04") == 0 && strcmp(t.title[23], "Press Conference") == 0 && t.awardN[9] == 14);
  float sum = 0.f;
  for (int i = 0; i < PG_AWARDS; ++i) sum += PG_CAP_AWARDS[i];
  CHECK(sum == 60.f && PG_CAP_BASE + sum == PG_CAP_MAX);
  static const int kB[] = { 4, 6, 7, 11, 12, 14, 16, 17, 19, 20, 21 }, kC[] = { 4, 11 };
  unsigned b = 0, c = 0;
  for (int x : kB) b |= 1u << x;
  for (int x : kC) c |= 1u << x;
  CHECK(PG_PART_MASK[0] == 0x7FFFFFu && PG_PART_MASK[1] == b && PG_PART_MASK[2] == c);
  CHECK(strcmp(PG_AWARD_NAMES[0], "Toilet") == 0 && strcmp(PG_AWARD_NAMES[17], "SmokeCigar") == 0 &&
        strcmp(PG_AWARD_NAMES[34], "DefeatLeech") == 0);
}

// progress.h: a chapter's N/M counts the table's entries whose award is found, over Awards.Num(): an id listed
// twice counts twice, one the game never grants (poker, 1) stays unfound, an id past the enum never counts found.
static void tb_0171_pg_chapter_rows() {
  PgTable t;
  pg_table_builtin(&t);
  PgFlags f;
  memset(&f, 0, sizeof f);
  f.award[21] = 1;   // Mirror
  f.award[0] = 1;    // Toilet
  PgChapter c[PG_CHAPTERS_MAX];
  CHECK(pg_build_chapters(t, f, c) == 24);
  CHECK_MSG(c[0].egoFound == 2 && c[0].egoTotal == 3, "Duke Lives 2/3");
  CHECK_MSG(c[9].egoFound == 2 && c[9].egoTotal == 14, "Titty City: 21 and 0 of 14; poker unfound");
  CHECK_MSG(c[3].egoFound == 0 && c[3].egoTotal == 0, "Mothership Battle has none");
  CHECK(strcmp(c[1].title, "Damn! It's Late...") == 0 && strcmp(c[1].url, "map01") == 0);
  t.awardN[3] = 3;
  t.awards[3][0] = 21; t.awards[3][1] = 21; t.awards[3][2] = 200;
  pg_build_chapters(t, f, c);
  CHECK_MSG(c[3].egoFound == 2 && c[3].egoTotal == 3, "duplicate counted twice, id 200 never found");
  t.awardN[3] = 40;   // more entries than are kept: the total stays the game's
  for (int j = 3; j < PG_CH_AWARDS_MAX; ++j) t.awards[3][j] = 200;
  pg_build_chapters(t, f, c);
  CHECK(c[3].egoTotal == 40 && c[3].egoFound == 2);
  CHECK(pg_award_found(f, 21) && !pg_award_found(f, 1) && !pg_award_found(f, -1) && !pg_award_found(f, 35));
}

// progress.h: HasCompletionForLevel. Every part whose mask has the chapter's bit must be set in that difficulty's
// word; chapter 23 and difficulties outside 0..3 are never complete.
static void tb_0172_pg_completion_bits() {
  PgFlags f;
  memset(&f, 0, sizeof f);
  f.completion[1][0] = 1u << 4;
  CHECK_MSG(!pg_level_done(f, 1, 4), "Lady Killer needs map04, map04b and map04c");
  f.completion[1][1] = 1u << 4;
  CHECK(!pg_level_done(f, 1, 4));
  f.completion[1][2] = 1u << 4;
  CHECK(pg_level_done(f, 1, 4) && !pg_level_done(f, 0, 4) && !pg_level_done(f, 2, 4));
  f.completion[3][0] = 1u << 0;
  CHECK_MSG(pg_level_done(f, 3, 0), "Duke Lives has one part");
  f.completion[2][0] = 1u << 6;
  CHECK_MSG(!pg_level_done(f, 2, 6), "Duke Dome needs map06b too");
  f.completion[2][1] = 1u << 6;
  CHECK(pg_level_done(f, 2, 6));
  for (int d = 0; d < PG_DIFFS; ++d) f.completion[d][0] = f.completion[d][1] = f.completion[d][2] = 0xFFFFFFFFu;
  CHECK_MSG(!pg_level_done(f, 0, 23) && !pg_level_done(f, 0, 24), "levels 0..22 only");
  CHECK(!pg_level_done(f, -1, 0) && !pg_level_done(f, 4, 0) && pg_level_done(f, 0, 22));
  PgTable t;
  pg_table_builtin(&t);
  PgChapter c[PG_CHAPTERS_MAX];
  pg_build_chapters(t, f, c);
  CHECK(c[22].done[0] && c[22].done[3] && !c[23].done[0] && !c[23].done[3]);
}

// progress.h: totals. Permanent cap = base + found awards' caps (+ the preorder bonus), never above the maximum.
static void tb_0173_pg_totals() {
  PgFlags f;
  memset(&f, 0, sizeof f);
  PgTotals t = pg_totals(f, PG_CAP_AWARDS, PG_CAP_BASE, PG_CAP_MAX, PG_PREORDER_EGO, 0, 0.f);
  CHECK(t.run.awardsFound == 0 && t.run.awardsTotal == 35 && t.run.capPermanent == 40 && t.run.capMax == 100 &&
        !t.run.capNowOk);
  f.award[28] = 1;   // Alien Queen, +4
  f.award[5] = 1;    // air hockey, +3
  f.helmet[1] = 1;
  f.phone[0] = f.phone[11] = 1;
  t = pg_totals(f, PG_CAP_AWARDS, PG_CAP_BASE, PG_CAP_MAX, PG_PREORDER_EGO, 1, 51.6f);
  CHECK(t.run.awardsFound == 2 && t.run.capPermanent == 47 && t.run.capNowOk && t.run.capNow == 52 &&
        t.profile.helmets == 1 && t.profile.phones == 2);
  f.bonusEgo = 1;
  t = pg_totals(f, PG_CAP_AWARDS, PG_CAP_BASE, PG_CAP_MAX, PG_PREORDER_EGO, 0, 0.f);
  CHECK_MSG(t.run.capPermanent == 52 && t.run.capMax == 105, "the preorder code adds 5 to both");
  for (int i = 0; i < PG_AWARDS; ++i) f.award[i] = 1;
  t = pg_totals(f, PG_CAP_AWARDS, PG_CAP_BASE, PG_CAP_MAX, PG_PREORDER_EGO, 0, 0.f);
  CHECK(t.run.awardsFound == 35 && t.run.capPermanent == 105 && t.run.capMax == 105);
  const float big[PG_AWARDS] = { 90.f };
  t = pg_totals(f, big, PG_CAP_BASE, PG_CAP_MAX, PG_PREORDER_EGO, 0, 0.f);
  CHECK_MSG(t.run.capPermanent == t.run.capMax, "clamped to the maximum");
}

// progress.h: the 1 Hz rule (forced on the tab's opening, at once before any read, across the tick wrap) and the
// slow-Get probe.
static void tb_0174_pg_refresh_rule() {
  CHECK(pg_refresh_due(0, 0, 5, 0));
  CHECK(!pg_refresh_due(1, 1000, 1999, 0) && pg_refresh_due(1, 1000, 2000, 0));
  CHECK(pg_refresh_due(1, 1000, 1001, 1));
  CHECK_MSG(pg_refresh_due(1, 0xFFFFFF00u, 0x000002F0u, 0) && !pg_refresh_due(1, 0xFFFFFF00u, 0x10u, 0), "wrap");
  CHECK(pg_probe_fast(0.02) && pg_probe_fast(4.9) && !pg_probe_fast(5.0) && !pg_probe_fast(1000.0));
}

// progress.h: a key the cache lacks reads 0 whatever the out-parameter holds; with no keys every row is 0/N and
// no chapter is finished.
static void tb_0175_pg_missing_keys() {
  CHECK(pg_bool_from_get(0, 1) == 0 && pg_bool_from_get(1, 0) == 0 && pg_bool_from_get(1, 1) == 1 &&
        pg_bool_from_get(1, -1) == 1);
  CHECK(pg_int_from_get(0, 0x7FFFFF) == 0 && pg_int_from_get(1, 0x810) == 0x810u);
  PgFlags f;
  memset(&f, 0, sizeof f);
  PgTable t;
  pg_table_builtin(&t);
  PgChapter c[PG_CHAPTERS_MAX];
  const int n = pg_build_chapters(t, f, c);
  int found = 0, done = 0;
  for (int i = 0; i < n; ++i) {
    found += c[i].egoFound;
    for (int d = 0; d < PG_DIFFS; ++d) done += c[i].done[d];
  }
  CHECK(n == 24 && found == 0 && done == 0 && c[2].egoTotal == 7);
  PgTable empty;
  memset(&empty, 0, sizeof empty);
  CHECK(pg_build_chapters(empty, f, c) == 0);
}

// progress.h / levelstats.h: the map's chapter (SPSetEgoAwardsForLevel's 5-character match), engine strings to
// ASCII, the readouts and the overlay's line and gate; Level stats defaults off.
static void tb_0176_pg_map_and_text() {
  PgTable t;
  pg_table_builtin(&t);
  CHECK(pg_chapter_of_map(t, "Map04B") == 4 && pg_chapter_of_map(t, "map00") == 0 && pg_chapter_of_map(t, "MAP23") == 23);
  CHECK(pg_chapter_of_map(t, "Map_mydigs") == -1 && pg_chapter_of_map(t, "map0") == -1 && pg_chapter_of_map(t, 0) == -1);
  CHECK(pg_chapter_of_map(t, "Entry") == -1);
  char b[48];
  pg_ascii(L"Damn! It\x2019s", 64, b, sizeof b);
  CHECK(strcmp(b, "Damn! It?s") == 0);
  pg_ascii(L"abcdef", 3, b, sizeof b);
  CHECK(strcmp(b, "abc") == 0);
  pg_ascii(0, 5, b, sizeof b);
  CHECK(b[0] == 0);
  pg_ratio(b, sizeof b, 2, 5);
  CHECK(strcmp(b, "2/5") == 0);
  pg_ratio(b, sizeof b, 0, 0);
  CHECK(strcmp(b, "-") == 0);
  // The difficulty column is as wide as its caption or its pips, whichever is wider, and never narrower than the pips.
  CHECK(pg_diff_col_w(40, 70) == 70 && pg_diff_col_w(40, 30) == 40 && pg_diff_col_w(40, 40) == 40);
  CHECK(strstr(PG_DIFF_TIP, "Piece of Cake") && strstr(PG_DIFF_TIP, "Damn I'm Good"));
  PgChapter c;
  memset(&c, 0, sizeof c);
  pg_done_text(c, 4, PG_DIFF_NAMES, b, sizeof b);
  CHECK(strcmp(b, "not finished yet") == 0);
  c.done[0] = c.done[2] = c.done[3] = 1;
  char long_[96];
  pg_done_text(c, 4, PG_DIFF_NAMES, long_, sizeof long_);
  CHECK(strcmp(long_, "finished on Piece of Cake, Come Get Some and Damn, I'm Good") == 0);
  pg_done_text(c, 23, PG_DIFF_NAMES, b, sizeof b);
  CHECK(strncmp(b, "the game keeps no", 17) == 0);
  ls_ego_line(b, sizeof b, 1, 3);
  CHECK(strcmp(b, "Ego items 1/3") == 0);
  ls_ego_line(b, sizeof b, 0, 0);
  CHECK_MSG(strcmp(b, "Ego items -") == 0, "a chapter with no awards reads as the tab does");
  CHECK_MSG(!ls_visible(1, world_gate((void*)1, (void*)1, 1, 1) ? 1 : 0, 4, 0), "paused: hidden");
  CHECK(ls_visible(1, 1, 4, 0) && !ls_visible(0, 1, 4, 0) && !ls_visible(1, 0, 4, 0) && !ls_visible(1, 1, -1, 0) &&
        !ls_visible(1, 1, 4, 1));
  config_defaults();
  CHECK(g_cfg.levelStats == 0 && config_key_known("esp", "levelstats"));
}

// progress.h: the game's difficulty labels with "Difficulty: " stripped (any language's prefix up to ':').
static void tb_0177_pg_diff_labels() {
  char b[40];
  CHECK(pg_diff_label("Difficulty: Damn, I'm Good", b, sizeof b) && strcmp(b, "Damn, I'm Good") == 0);
  CHECK(pg_diff_label("Schwierigkeit:  Kinderspiel ", b, sizeof b) && strcmp(b, "Kinderspiel") == 0);
  CHECK(pg_diff_label("Let's Rock", b, sizeof b) && strcmp(b, "Let's Rock") == 0);
  CHECK(!pg_diff_label("Difficulty: ", b, sizeof b) && !pg_diff_label(0, b, sizeof b) && b[0] == 0);
  CHECK(strcmp(PG_DIFF_NAMES[0], "Piece of Cake") == 0 && strcmp(PG_DIFF_NAMES[3], "Damn, I'm Good") == 0);
}

// progress.h: until a Get has returned found, the keys are read once per level (a probe), not each second; one
// found key opens reads for good.
static void tb_0178_pg_found_gate() {
  PgGate g;
  memset(&g, 0, sizeof g);
  CHECK_MSG(pg_gate_read(&g, 0), "the first read probes");
  pg_gate_note(&g, 0);
  CHECK_MSG(!pg_gate_read(&g, 0) && !pg_gate_read(&g, 0), "nothing found: no read again on the same level");
  CHECK_MSG(pg_gate_read(&g, 1) && !pg_gate_read(&g, 1), "a new level probes once");
  pg_gate_note(&g, 3);
  CHECK_MSG(pg_gate_read(&g, 1) && pg_gate_read(&g, 1) && pg_gate_read(&g, 7), "found once: every refresh reads");
  pg_gate_note(&g, 0);
  CHECK_MSG(pg_gate_read(&g, 7), "a later empty read does not close it");
}

// progress.h: the class scan's back-off (5 s doubling to 5 min) and the property span check.
static void tb_0179_pg_scan_backoff_and_span() {
  CHECK(pg_scan_backoff(0) == 5000 && pg_scan_backoff(5000) == 10000 && pg_scan_backoff(80000) == 160000);
  CHECK(pg_scan_backoff(160000) == 300000 && pg_scan_backoff(300000) == 300000);
  unsigned ms = 0;
  int steps = 0;
  while (ms < PG_SCAN_MAX_MS && steps < 64) { ms = pg_scan_backoff(ms); steps++; }
  CHECK(ms == PG_SCAN_MAX_MS && steps == 7);   // 5, 10, 20, 40, 80, 160 s, then 5 min
  CHECK(pg_field_in(0x24, 4) && pg_field_in(0xFFFC, 4) && !pg_field_in(0xFFFD, 4) && !pg_field_in(-4, 4) &&
        !pg_field_in(0x24, 0) && !pg_field_in(0x7FFFFFFF, 4));
}

static int eg_rule(const char* name) {
  for (int i = 0; i < EG_CLASS_COUNT; ++i)
    if (strcmp(EG_CLASSES[i].name, name) == 0) return i;
  return -1;
}

// egoesp.h: the giver filter. Class -> award id(s); a found award hides its marker (the bench while either of its
// two is unfound); event awards and TriggerPlayer never mark; boosters take the instance id while they give;
// monitors only while they can switch screens; TriggerPlayer and the cigar in Duke's mouth are not givers.
static void tb_0230_eg_giver_filter() {
  int ids[EG_IDS];
  const int toilet = eg_rule("dnControl_Bathroom_UrinationReceptacle"), bench = eg_rule("dnControl_WorkoutBench");
  const int booster = eg_rule("Settings_Workout_PunchKick_EgoBooster");
  const int monitor = eg_rule("Electronics_Generic_Computer_Monitor");
  CHECK(toilet >= 0 && bench >= 0 && booster >= 0 && monitor >= 0);
  CHECK(eg_rule("TriggerPlayer") < 0 && eg_rule("CigarAttachment") < 0);
  CHECK_MSG(eg_rule("Details_Generic_CigarBox") < 0 && eg_rule("CigarPickup") >= 0, "the cigar box gives no cigar");
  CHECK(eg_giver_ids(EG_CLASSES[toilet], 0, 0, 0, 0, ids) == 1 && ids[0] == 0);
  CHECK(eg_giver_ids(EG_CLASSES[bench], 0, 0, 0, 0, ids) == 2 && ids[0] == 3 && ids[1] == 4);
  CHECK_MSG(eg_giver_ids(EG_CLASSES[booster], 1, 19, 0, 0, ids) == 1 && ids[0] == 19, "speed bag: instance id");
  CHECK_MSG(eg_giver_ids(EG_CLASSES[booster], 0, 18, 0, 0, ids) == 0, "a booster that gives nothing");
  CHECK_MSG(eg_giver_ids(EG_CLASSES[booster], 1, 28, 0, 0, ids) == 0, "an event id is never a marker");
  CHECK_MSG(eg_giver_ids(EG_CLASSES[booster], 1, 200, 0, 0, ids) == 0, "past the enum");
  CHECK(eg_giver_ids(EG_CLASSES[monitor], 0, 0, 1, 2, ids) == 1 && ids[0] == 25);
  CHECK_MSG(eg_giver_ids(EG_CLASSES[monitor], 0, 0, 1, 0, ids) == 0, "not wired to a CPU with a keyboard");
  CHECK(eg_giver_ids(EG_CLASSES[monitor], 0, 0, 0, 3, ids) == 0);
  for (int i = 0; i < EG_CLASS_COUNT; ++i)
    CHECK_MSG(!eg_event_award(EG_CLASSES[i].id0) && !eg_event_award(EG_CLASSES[i].id1), "no event award by class");
  CHECK(eg_event_award(26) && eg_event_award(34) && !eg_event_award(25) && !eg_event_award(35));
  unsigned char found[PG_AWARDS] = {};
  int b[EG_IDS] = { 3, 4 };
  CHECK(eg_unfound(b, 2, found) == 3);
  found[3] = 1;
  CHECK_MSG(eg_unfound(b, 2, found) == 4, "the bench stays while the full lift is unfound");
  found[4] = 1;
  CHECK_MSG(eg_unfound(b, 2, found) == -1, "found: hidden");
  CHECK(eg_unfound(b, 0, found) == -1);
  char l[40];
  eg_label(9, l, sizeof l);
  CHECK(strcmp(l, "Pinball High Score") == 0);
  eg_label(0, l, sizeof l);
  CHECK(strcmp(l, "Toilet") == 0);
  eg_label(35, l, sizeof l);
  CHECK(l[0] == 0);
  eg_label(4, l, 8);
  CHECK(strcmp(l, "Workout") == 0);
  CHECK(eg_clamp_extent(0.f) == 8.f && eg_clamp_extent(20.f) == 20.f && eg_clamp_extent(4000.f) == 128.f);
  config_defaults();
  CHECK(g_cfg.espEgo == 1 && config_key_known("esp", "egoboosts"));
}

// egoesp.h: the poker machine grants PokerWin (VideoPoker.UncoverCallback on a paying hand) and the cigar pickup
// SmokeCigar (Map20 has no cigar box): both are fixed givers.
static void tb_0436_eg_poker_cigar_givers() {
  int ids[EG_IDS];
  const int poker = eg_rule("dnControl_CoinOp_Poker"), cigar = eg_rule("CigarPickup");
  CHECK(poker >= 0 && cigar >= 0);
  CHECK(eg_giver_ids(EG_CLASSES[poker], 0, 0, 0, 0, ids) == 1 && ids[0] == 1);
  CHECK(eg_giver_ids(EG_CLASSES[cigar], 0, 0, 0, 0, ids) == 1 && ids[0] == 17);
}

static int eg_event_index(int id, const char* tag) {
  for (int k = 0; k < EG_EVENT_COUNT; ++k)
    if (EG_EVENTS[k].id == id && strcmp(EG_EVENTS[k].tag, tag) == 0) return k;
  return -1;
}

// egoesp.h: an event award is marked on the actor the player uses, matched by class on the super chain AND Tag;
// only while a TriggerPlayer in the level lists the award (Map_mydigs has the same Alien Abortion switch and none).
static void tb_0437_eg_event_rules() {
  const int sw = eg_event_index(29, "SwitchAlienAbortReset01"), dvd = eg_event_index(30, "PornoDVDPlayer");
  const int dance = eg_event_index(31, "ShakeItBabyLookInfo"), book = eg_event_index(26, "dnControl_BookSigning");
  CHECK(sw >= 0 && dvd >= 0 && dance >= 0 && book >= 0 && eg_event_index(26, "dnControl_BookSigning_Mounted") >= 0);
  for (int k = 0; k < EG_EVENT_COUNT; ++k) CHECK_MSG(eg_event_award(EG_EVENTS[k].id), "event ids only");
  CHECK(eg_rule_event(EG_CLASS_COUNT + sw) == sw && eg_rule_event(0) == -1 &&
        eg_rule_event(EG_CLASS_COUNT + EG_EVENT_COUNT) == -1);
  // Resolved names: class k -> 100 + k, tag k -> 200 + k; the switch class is also a superclass name (7).
  int cls[EG_EVENT_COUNT], tag[EG_EVENT_COUNT];
  for (int k = 0; k < EG_EVENT_COUNT; ++k) { cls[k] = 100 + k; tag[k] = 200 + k; }
  const int swChain[] = { 50, 100 + sw, 7 };
  const unsigned m = eg_event_mask(swChain, 3, cls);
  CHECK(m == (1u << sw));
  CHECK(eg_event_pick(m, 200 + sw, tag) == sw);
  CHECK_MSG(eg_event_pick(m, 200 + dvd, tag) == -1, "the DVD player's tag on a switch: not the listed actor");
  CHECK_MSG(eg_event_pick(1u << dvd, 200 + sw, tag) == -1, "the switch's tag on another class");
  CHECK_MSG(eg_event_pick(m, 0, tag) == -1, "no Tag");
  const int other[] = { 50, 7 };
  CHECK_MSG(eg_event_mask(other, 2, cls) == 0, "class not on the chain");
  tag[sw] = 0;
  CHECK_MSG(eg_event_pick(m, 0, tag) == -1, "a tag name not loaded never matches");
  cls[sw] = 0;
  CHECK(eg_event_mask(swChain, 3, cls) == 0);
  unsigned char listed[PG_AWARDS] = {};
  const unsigned char tp[] = { 29, 200, 30 };
  eg_note_listed(listed, tp, 3);
  CHECK(listed[29] && listed[30] && !listed[31] && !listed[26]);
  CHECK(eg_event_id(EG_EVENTS[sw], listed, 0) == 29 && eg_event_id(EG_EVENTS[dvd], listed, 0) == 30);
  CHECK_MSG(eg_event_id(EG_EVENTS[dance], listed, 0) == -1, "no TriggerPlayer lists 31 in this level");
  listed[26] = 1;
  CHECK(eg_event_id(EG_EVENTS[book], listed, 0) == 26);
  CHECK_MSG(eg_event_id(EG_EVENTS[book], listed, 1) == -1, "the signing table before the kid hands the book over");
  listed[31] = 1;
  CHECK_MSG(eg_event_id(EG_EVENTS[dance], listed, 1) == 31, "the look info is marked hidden or not");
}

struct EgAliveCtx { const void* gone; };
static bool eg_alive_stub(void* ctx, int, const void* obj) { return obj != ((EgAliveCtx*)ctx)->gone; }

// egoesp.h: the per-level cache. Built once per level serial (in a level only), rebuilt when the serial moves;
// entries whose slot no longer holds them are dropped in order; past EG_MAX nothing is added and it is counted.
static void tb_0231_eg_cache_rule() {
  static EgCache c;
  memset(&c, 0, sizeof c);
  CHECK(!eg_cache_due(c, 1, 0) && eg_cache_due(c, 1, 1));
  eg_cache_reset(&c, 1);
  CHECK(!eg_cache_due(c, 1, 1) && eg_cache_due(c, 2, 1) && !eg_cache_due(c, 2, 0));
  static int objs[3];
  for (int i = 0; i < 3; ++i) CHECK(eg_cache_add(&c, &objs[i], 10 + i, i));
  EgAliveCtx ctx = { &objs[1] };
  CHECK(eg_cache_prune(&c, eg_alive_stub, &ctx) == 1);
  CHECK(c.n == 2 && c.e[0].obj == &objs[0] && c.e[1].obj == &objs[2] && c.e[1].slot == 12 && c.e[1].rule == 2);
  CHECK(eg_cache_prune(&c, eg_alive_stub, &ctx) == 0 && c.n == 2);
  for (int i = c.n; i < EG_MAX; ++i) eg_cache_add(&c, &objs[0], i, 0);
  CHECK(c.n == EG_MAX && !eg_cache_add(&c, &objs[0], 99, 0) && c.overflow == 1);
  eg_cache_reset(&c, 2);
  CHECK(c.n == 0 && c.serial == 2 && c.built && !c.overflow);
}

// rawmouse.h: counts x 0.5 x 7.68 x sensitivity x FOV/90 (UpdateRotation's 7.68), fractions carried; invert and
// mirror as PlayerInput; a locked axis and an open panel turn nothing and carry nothing.
static void tb_0183_raw_turn() {
  RawCarry c = raw_carry_init();
  RawIn in = { 3, 4, 1.f, 90.f, 0, 0, 0, 0, 0 };
  int p = 99, y = 99;
  raw_turn(&c, in, &p, &y);
  CHECK_MSG(y == 11 && p == -15 && fabs(c.yaw - 0.52) < 1e-9 && fabs(c.pitch + 0.36) < 1e-9,
            "3 counts at 3.84 a count: 11 now, 0.52 carried; mouse down looks down");
  in.dx = 1; in.dy = 0;
  raw_turn(&c, in, &p, &y);
  CHECK_MSG(y == 4 && fabs(c.yaw - 0.36) < 1e-9, "the carried 0.52 adds to the next count");
  RawCarry b = raw_carry_init();
  in.dx = -3; in.dy = -4;
  raw_turn(&b, in, &p, &y);
  CHECK_MSG(y == -11 && p == 15 && fabs(b.yaw + 0.52) < 1e-9, "left and up: the same magnitudes, carried toward zero");
  RawCarry d = raw_carry_init();
  RawIn g = { 1000, 0, 2.f, 45.f, 0, 0, 0, 0, 0 };
  raw_turn(&d, g, &p, &y);
  CHECK_MSG(y == 3840 || y == 3839, "no cap: 1000 counts x 0.5 x 7.68 x 2 x 45/90");
  RawCarry h = raw_carry_init();
  RawIn inv = { 2, 2, 1.f, 90.f, 1, 1, 0, 0, 0 };
  raw_turn(&h, inv, &p, &y);
  CHECK_MSG(p == 7 && y == -7, "invert flips pitch, mirror flips yaw");
  RawIn lock = { 4, 4, 1.f, 90.f, 0, 0, 1, 1, 0 };
  RawCarry e = { 0.5, 0.5 };
  raw_turn(&e, lock, &p, &y);
  CHECK_MSG(p == 0 && y == 0 && e.pitch == 0.0 && e.yaw == 0.0, "locked axes: nothing turns or carries");
  RawIn open = { 50, 50, 1.f, 90.f, 0, 0, 0, 0, 1 };
  RawCarry f = { 0.5, -0.5 };
  raw_turn(&f, open, &p, &y);
  CHECK_MSG(p == 0 && y == 0 && f.pitch == 0.0 && f.yaw == 0.0, "panel open: zero, nothing carried");
}

// noshake.h: weapon kicks are kept by name; other entries are zeroed and their values given back when off, unless
// the game wrote new ones since.
static void tb_0184_shake_filter() {
  CHECK(ns_is_kick(L"Pistol_ViewKick_1") && ns_is_kick(L"shotgun_viewkick_2"));
  CHECK_MSG(!ns_is_kick(L"DamageShake_Pitch") && !ns_is_kick(L"Nuke_Shake") && !ns_is_kick(L"ViewKick") &&
            !ns_is_kick(0), "damage, explosions and a bare ViewKick are not weapon kicks");
  ShakeHold h = {};
  const int live[NS_VALS] = { 300, 600, 450, 10, -20, 5 };
  const int zero[NS_VALS] = {};
  int w[NS_VALS] = { 7, 7, 7, 7, 7, 7 };
  CHECK_MSG(ns_step(&h, 1, live, w) == CVAR_FORCE && ns_zero(w) && h.saved[1] == 600, "zeroed, values kept");
  CHECK_MSG(ns_step(&h, 1, zero, w) == CVAR_NONE, "still ours: nothing");
  const int again[NS_VALS] = { 900, 900, 0, 0, 0, 0 };
  CHECK_MSG(ns_step(&h, 1, again, w) == CVAR_FORCE && h.saved[0] == 900, "re-triggered: the new values kept");
  CHECK_MSG(ns_step(&h, 0, zero, w) == CVAR_RESTORE && w[0] == 900 && w[1] == 900 && !h.held, "off: given back");
  ShakeHold k = {};
  ns_step(&k, 1, live, w);
  CHECK_MSG(ns_step(&k, 0, again, w) == CVAR_NONE && !k.held, "the game's own values since: left");
}

// pehook.h: no subscriber, a disabled one and NAME_None pass straight through; enabled ones are found in order.
static void tb_0185_pe_dispatch() {
  CHECK_MSG(pe_next(0, 0, 77, 0) == -1, "no subscribers: the engine's call only");
  PeSub s[3] = { { 77, 0 }, { 12, 1 }, { 77, 1 } };
  CHECK_MSG(pe_next(s, 3, 77, 0) == 2 && pe_next(s, 3, 77, 3) == -1, "a disabled subscriber is skipped");
  CHECK(pe_next(s, 3, 12, 0) == 1 && pe_next(s, 3, 13, 0) == -1);
  PeSub none[1] = { { 0, 1 } };
  CHECK_MSG(pe_next(none, 1, 0, 0) == -1, "an unresolved name never matches");
  config_defaults();
  CHECK(g_cfg.rawMouse == 0 && g_cfg.noShake == 0 && config_key_known("input", "rawmouse") &&
        config_key_known("view", "noshake"));
}

// clicklock.h: the engine mouse goes shared (EXCLUSIVE -> NONEXCLUSIVE), FOREGROUND and the rest kept; other
// devices and levels already shared are untouched.
static void tb_0186_cl_coop_level() {
  CHECK_MSG(cl_coop_level(1, 0x5) == 0x6, "the engine's EXCLUSIVE|FOREGROUND becomes NONEXCLUSIVE|FOREGROUND");
  CHECK(cl_coop_level(1, 0x9) == 0xA && cl_coop_level(1, 0x15) == 0x16);
  CHECK_MSG(cl_coop_level(1, 0x6) == 0x6 && cl_coop_level(1, 0xA) == 0xA, "already shared: unchanged");
  CHECK_MSG(cl_coop_level(0, 0x5) == 0x5, "not the classified mouse: unchanged");
}

// clicklock.h: the first condition that stops the hold, focus first; the centre of the client.
static void tb_0187_cl_hold_rule() {
  const ClHoldIn play = { 1, 1, 0, 0, 0, 0 };
  CHECK_MSG(cl_hold_block(play) == CL_HOLD_OK, "in front, the engine holds the mouse: held");
  ClHoldIn c = play; c.window = 0; CHECK(cl_hold_block(c) == CL_HOLD_NO_WINDOW);
  c = play; c.foreground = 0; CHECK_MSG(cl_hold_block(c) == CL_HOLD_BACKGROUND, "alt-tab");
  c = play; c.iconic = 1; CHECK(cl_hold_block(c) == CL_HOLD_ICONIC);
  c = play; c.panelOpen = 1; CHECK(cl_hold_block(c) == CL_HOLD_PANEL);
  c = play; c.engineReleased = 1; CHECK_MSG(cl_hold_block(c) == CL_HOLD_ENGINE_RELEASED, "SetMouseCapture(0)");
  c = play; c.cursorShown = 1; CHECK_MSG(cl_hold_block(c) == CL_HOLD_CURSOR_SHOWN, "the engine showed the cursor");
  c = play; c.foreground = 0; c.panelOpen = 1; CHECK(cl_hold_block(c) == CL_HOLD_BACKGROUND);
  for (int b = CL_HOLD_OK; b <= CL_HOLD_CURSOR_SHOWN; b++) CHECK(cl_hold_name(b)[0] != '?');
  CHECK(strcmp(cl_hold_name(99), "?") == 0);
  long x = 7, y = 7;
  CHECK(cl_hold_centre(0, 0, 3440, 1440, &x, &y) && x == 1720 && y == 720);
  CHECK_MSG(cl_hold_centre(10, 20, 13, 25, &x, &y) && x == 11 && y == 22, "odd sizes round down");
  x = 7; y = 7;
  CHECK_MSG(!cl_hold_centre(0, 0, 0, 1440, &x, &y) && x == 7 && y == 7, "empty: false, outputs untouched");
  CHECK_MSG(cl_swallows(0x201, 1) && cl_swallows(0x20A, 1) && cl_swallows(0x200, 1) && cl_swallows(0x20E, 1),
            "held: buttons, wheel and moves are kept from the engine, as under the exclusive mouse");
  CHECK(!cl_swallows(0x201, 0) && !cl_swallows(0x20, 1) && !cl_swallows(0xFF, 1) && !cl_swallows(0x100, 1));
}

// cursorgate.h: the engine's "ShowCursor(TRUE) until visible" loop no longer counts past visible. The alt-tab of
// 2026-10-10 as the engine ran it (5 releases, 2 captures) left the count at 2; it now ends hidden.
static int tb465_true(int* count) {   // the Engine.dll slot, closed: the real call, undone when redundant
  const int after = ++*count;
  if (cg_show_redundant(1, after - 1)) { --*count; return after - 1; }
  return after;
}
static int tb465_false(int* count) {
  const int after = --*count;
  if (cg_show_redundant(0, after + 1)) { ++*count; return after + 1; }
  return after;
}
static void tb_0465_cg_show_redundant() {
  CHECK_MSG(cg_show_redundant(1, 0) && cg_show_redundant(1, 3), "already visible: not counted");
  CHECK_MSG(!cg_show_redundant(1, -1) && !cg_show_redundant(1, -5), "hidden: the engine's call counts");
  CHECK_MSG(!cg_show_redundant(0, 0) && !cg_show_redundant(0, 2), "a hide from visible counts");
  CHECK_MSG(cg_show_redundant(0, -1) && cg_show_redundant(0, -3), "already hidden: the hide is not counted");
  int count = -1;
  for (int rel = 0; rel < 5; rel++) {   // SetMouseCapture(0): do ShowCursor(1) while (< 0)
    int r, calls = 0;
    do { r = tb465_true(&count); } while (r < 0 && ++calls < 64);
    CHECK(r >= 0);
  }
  CHECK_MSG(count == 0, "five releases leave the cursor just visible");
  tb465_false(&count); tb465_false(&count);   // two SetMouseCapture(1): one ShowCursor(0) each
  CHECK_MSG(count == -1, "after the return the cursor is hidden again, the count floored at -1");
  int menu, calls = 0;                  // a later menu still shows it
  do { menu = tb465_true(&count); } while (menu < 0 && ++calls < 64);
  CHECK(count == 0 && menu == 0);
  tb465_false(&count); tb465_false(&count);
  CHECK_MSG(tb465_true(&count) == 0, "ShutdownAfterError's single ShowCursor(1) shows the crash dialog's cursor");
  // Open: the gate answers the virtual count and records nothing for a redundant show.
  CgState s = cg_init();
  const int v = cg_virtual_count(s, 1);
  if (!cg_show_redundant(1, v)) s = cg_on_showcursor(s, true, TRUE);
  CHECK_MSG(cg_close_replay(s) == 0, "open, visible: close replays no extra show");
}

// clicklock.h: one line per stretch the hold is kept off in play for 5 s; focus, panel and window reasons are not
// watched, and a change of reason starts a new stretch.
static void tb_0466_cl_hold_watch() {
  ClHoldWatch w = cl_watch_init();
  CHECK(!cl_hold_watch(&w, CL_HOLD_CURSOR_SHOWN, 1000));
  CHECK(!cl_hold_watch(&w, CL_HOLD_CURSOR_SHOWN, 5999));
  CHECK_MSG(cl_hold_watch(&w, CL_HOLD_CURSOR_SHOWN, 6000), "5 s blocked in play: logged");
  CHECK_MSG(!cl_hold_watch(&w, CL_HOLD_CURSOR_SHOWN, 60000), "once per stretch");
  CHECK(!cl_hold_watch(&w, CL_HOLD_ENGINE_RELEASED, 61000));
  CHECK_MSG(cl_hold_watch(&w, CL_HOLD_ENGINE_RELEASED, 66000), "a new reason is a new stretch");
  CHECK(!cl_hold_watch(&w, CL_HOLD_OK, 67000));
  CHECK(!cl_hold_watch(&w, CL_HOLD_CURSOR_SHOWN, 68000) && cl_hold_watch(&w, CL_HOLD_CURSOR_SHOWN, 73000));
  const int unwatched[4] = { CL_HOLD_NO_WINDOW, CL_HOLD_BACKGROUND, CL_HOLD_ICONIC, CL_HOLD_PANEL };
  for (int i = 0; i < 4; i++) {
    ClHoldWatch u = cl_watch_init();
    CHECK(!cl_hold_watch(&u, unwatched[i], 0) && !cl_hold_watch(&u, unwatched[i], 100000));
  }
}

// clicklock.h: a raw-input row named the way dinput8's table sets it.
static void tb_0188_cl_raw_row() {
  RAWINPUTDEVICE d[2] = {};
  d[0].usUsagePage = 1; d[0].usUsage = 6; d[0].dwFlags = 0x100;
  d[1].usUsagePage = 1; d[1].usUsage = 2; d[1].dwFlags = 0x230;
  char m[96], k[96];
  cl_raw_row(d, 2, 0, CL_RAW_MOUSE, m, sizeof m);
  cl_raw_row(d, 2, 0, CL_RAW_KEYBOARD, k, sizeof k);
  CHECK_MSG(strncmp(m, "flags 0x230 NOLEGACY|CAPTUREMOUSE target", 40) == 0, m);
  CHECK_MSG(strncmp(k, "flags 0x100 INPUTSINK target", 28) == 0, k);
  cl_raw_row(d, 1, 0, CL_RAW_MOUSE, m, sizeof m);
  CHECK_MSG(strcmp(m, "none registered (1 rows)") == 0, m);
  cl_raw_row(d, -1, 122, CL_RAW_MOUSE, m, sizeof m);
  CHECK_MSG(strcmp(m, "unread (err 122)") == 0, m);
}

// world.h: hostile by attitude or by target. Map00's Cycloid idolizes Duke and attacks him through a scripted
// forced target (AIActor.PrimaryTargetActor), so a targeting pawn is an enemy whatever its attitude.
static void tb_0200_world_classify_by_target() {
  int en, al, ne;
  world_classify(1, 7, 1, &en, &al, &ne); CHECK_MSG(en && !al && !ne, "Idolize but targeting the player: enemy");
  world_classify(1, 7, 0, &en, &al, &ne); CHECK(!en && al && !ne);
  world_classify(1, 4, 1, &en, &al, &ne); CHECK(en && !al && !ne);
  world_classify(1, -1, 1, &en, &al, &ne); CHECK_MSG(en && !al && !ne, "no relationship, targeting: enemy");
  world_classify(1, 2, 1, &en, &al, &ne); CHECK(en && !al && !ne);
  world_classify(0, 7, 1, &en, &al, &ne); CHECK_MSG(!en && !al && !ne, "dead: nothing, target or not");
}
// world.h: the attitude line is logged once per (class, attitude, targets) and the list is capped.
static void tb_0201_world_att_log_key() {
  WAttKey k[3];
  int n = 0;
  const void* C = (void*)0x100;
  CHECK(world_att_log_new(k, &n, 3, C, 2, 0) == 1 && n == 1);
  CHECK_MSG(world_att_log_new(k, &n, 3, C, 2, 0) == 0, "the same class and attitude again: not logged");
  CHECK_MSG(world_att_log_new(k, &n, 3, C, 7, 0) == 1, "a second instance's override attitude is logged");
  CHECK_MSG(world_att_log_new(k, &n, 3, C, 7, 1) == 1 && n == 3, "turning to target the player is logged");
  CHECK_MSG(world_att_log_new(k, &n, 3, (void*)0x200, 2, 0) == 0 && n == 3, "full: nothing more");
}
// dmgtrack.h: a knockdown (Health 0 in state ControlX) is damage, not a kill; a stage reset raising Health is no
// event; a tracked pawn that turns non-hostile alive leaves quietly, never as a kill.
static void tb_0202b_world_downed() {
  static WDownSet d;
  memset(&d, 0, sizeof d);
  int p[20];
  // Admission: only a pawn that was hostile while alive and whose Health just reached 0.
  CHECK_MSG(world_down_admit(0, 1, 1) && !world_down_admit(0, 1, 0) && !world_down_admit(1, 1, 1) &&
                !world_down_admit(0, 0, 1),
            "a candidate is a pawn hostile while alive whose Health crossed to 0");
  wdown_add(&d, &p[0], 100);
  wdown_add(&d, &p[0], 100);
  CHECK_MSG(d.n == 1 && wdown_has(&d, &p[0]) && !wdown_has(&d, &p[1]), "a candidate is noted once");
  // Downed: Health 0 and its MyControl is a finishing move. Revived (Health > 0) or MyControl cleared: not.
  CHECK_MSG(world_downed(0, 1) && !world_downed(1, 1) && !world_downed(0, 0), "downed: Health 0 with a finishing move");
  // Kept: while downed; or before MyControl was ever seen, for the grace frames; never once alive again.
  CHECK_MSG(world_down_keep(0, 1, 500, 1) && !world_down_keep(1, 1, 0, 1), "kept while downed, gone when revived");
  CHECK_MSG(world_down_keep(0, 0, WDOWN_GRACE - 1, 0) && !world_down_keep(0, 0, WDOWN_GRACE, 0),
            "MyControl not yet seen: kept for the grace frames only");
  CHECK_MSG(!world_down_keep(0, 0, 0, 1), "MyControl seen and now cleared: removed at once");
  wdown_see(&d, &p[0]);
  CHECK_MSG(wdown_seen(&d, &p[0]) && d.last[0] == 100, "seen flag set; last visited is the add frame");
  wdown_touch(&d, &p[0], 7);
  wdown_add(&d, &p[1], 7);
  wdown_add(&d, &p[2], 3);
  wdown_prune(&d, 7);
  CHECK_MSG(d.n == 2 && wdown_has(&d, &p[0]) && wdown_has(&d, &p[1]) && !wdown_has(&d, &p[2]),
            "a pawn not visited this walk is dropped");
  wdown_remove(&d, &p[0]);
  CHECK_MSG(d.n == 1 && !wdown_has(&d, &p[0]) && wdown_has(&d, &p[1]) && !wdown_seen(&d, &p[1]),
            "removal keeps the others and their flags");
  for (int i = 0; i < 20; i++) wdown_add(&d, &p[i], 9);
  CHECK_MSG(d.n == WDOWN_MAX && wdown_has(&d, &p[19]) && !wdown_has(&d, &p[0]), "full: the oldest goes");
}

static void tb_0202_dmgtrack_knockdown_and_left() {
  static DmgTrack t;
  t = dmgtrack_init();
  const void* A = (void*)0x100;
  const void* B = (void*)0x200;
  const void* C = (void*)0x300;
  DmgDelta d[8];
  int n;
  { DmgSeen s[] = { {A, 100}, {B, 50} };  n = dmgtrack_step(&t, s, 2, d, 8);  CHECK(n == 0); }
  { DmgSeen s[] = { {A, 0, 0, 0, 0, DMGSEEN_KNOCKDOWN}, {B, 50} };  n = dmgtrack_step(&t, s, 2, d, 8);
    CHECK_MSG(n == 1 && d[0].who == A && d[0].damage == 100 && !d[0].kill, "knockdown: damage, no kill marker"); }
  CHECK(t.n == 1);   // A retired at 0
  { DmgSeen s[] = { {A, 3000}, {B, 50} };  n = dmgtrack_step(&t, s, 2, d, 8);
    CHECK_MSG(n == 0 && t.n == 2, "the recovery is a first sighting"); }
  { DmgSeen s[] = { {A, 3200}, {B, 50} };  n = dmgtrack_step(&t, s, 2, d, 8);
    CHECK_MSG(n == 0, "Health rising (a stage reset): no event, never negative damage"); }
  { DmgSeen s[] = { {A, 3150}, {B, 50, 0, 0, 0, DMGSEEN_LEFT}, {C, 10, 0, 0, 0, DMGSEEN_LEFT} };
    n = dmgtrack_step(&t, s, 3, d, 8);
    CHECK(n == 1 && d[0].who == A && d[0].damage == 50 && !d[0].kill);
    CHECK_MSG(t.n == 1, "B left quietly; C, never hostile, is not tracked"); }
  { DmgSeen s[] = { {A, 3150} };  n = dmgtrack_step(&t, s, 1, d, 8);
    CHECK_MSG(n == 0, "B's absence after leaving is no kill"); }
  { DmgSeen s[] = { {A, 0, 0, 0, 0, DMGSEEN_KNOCKDOWN}, {C, 0, 0, 0, 0, DMGSEEN_KNOCKDOWN} };
    n = dmgtrack_step(&t, s, 2, d, 8);
    CHECK_MSG(n == 1 && d[0].who == A && t.n == 0, "an untracked knockdown is not tracked"); }
}
// world.h: the previous frame's living pawns, so a pawn's state is read only on the frame its Health reaches 0.
static void tb_0203_world_alive_set() {
  static WAliveSet s;
  walive_clear(&s);
  CHECK(!walive_has(&s, (void*)0x100) && !walive_has(&s, 0));
  CHECK(walive_add(&s, (void*)0x100) && walive_has(&s, (void*)0x100));
  CHECK_MSG(!walive_add(&s, 0) && !walive_has(&s, 0), "null is never a member");
  CHECK(walive_add(&s, (void*)(0x100 + WALIVE_SLOTS * 16)) && walive_has(&s, (void*)(0x100 + WALIVE_SLOTS * 16)));
  walive_clear(&s);
  CHECK(!walive_has(&s, (void*)0x100));
  CHECK(world_knock_probe(0, 1) && !world_knock_probe(1, 1) && !world_knock_probe(0, 0) && !world_knock_probe(1, 0));
}

// postfx.h: the uber pass's levers (qol.md section 4 Gate): bloom, DOF and grading drive their Test/brightness
// variables; a row with two variables holds each one on its own, floats by their bits.
static void tb_0190_postfx_uber_levers() {
  const PostFx& bl = postfx_info(PFX_BLOOM);
  CHECK(bl.nvars == 2 && wcscmp(bl.var[0].cvar, L"r_BloomBrightnessScale") == 0 && bl.var[0].isFloat &&
        bl.var[0].forced == cvar_bits(0.f) && wcscmp(bl.var[1].cvar, L"r_DrawBlooms") == 0 && bl.var[1].forced == 0);
  const PostFx& dof = postfx_info(PFX_DOF);
  CHECK(dof.nvars == 2 && wcscmp(dof.var[0].cvar, L"r_TestDOF") == 0 && dof.var[0].forced == 1 &&
        !dof.var[0].isFloat && wcscmp(dof.var[1].cvar, L"r_DOFBlurAmount") == 0 && dof.var[1].isFloat &&
        dof.var[1].forced == cvar_bits(0.f));
  const PostFx& tm = postfx_info(PFX_TONEMAP);
  CHECK(tm.nvars == 1 && wcscmp(tm.var[0].cvar, L"r_TestToneMapping") == 0 && tm.var[0].forced == 1);
  int unique = 1;
  for (int i = 0; i < PFX_COUNT; i++)
    for (int a = 0; a < postfx_info(i).nvars; a++)
      for (int j = 0; j <= i; j++)
        for (int b = 0; b < postfx_info(j).nvars; b++)
          if ((j < i || b < a) && wcscmp(postfx_info(i).var[a].cvar, postfx_info(j).var[b].cvar) == 0) unique = 0;
  CHECK_MSG(unique, "no variable is driven by two rows");
  CHECK_MSG(strstr(postfx_info(PFX_BLUR).hint, "drunk") && strstr(postfx_info(PFX_MOTIONBLUR).hint, "drunk"),
            "the two blur rows say when they show");
  CHECK(cvar_float(cvar_bits(0.6f)) == 0.6f);
  CvarHold t = cvar_hold_init(), a = cvar_hold_init();
  const int six = cvar_bits(0.6f);
  int w = -1;
  CHECK_MSG(cvar_step(&t, 1, 0, 1, &w) == CVAR_FORCE && w == 1, "r_TestDOF 0 -> 1");
  CHECK_MSG(cvar_step(&a, 1, six, 0, &w) == CVAR_FORCE && w == 0 && a.saved == six, "r_DOFBlurAmount 0.6 -> 0.0");
  CHECK_MSG(cvar_step(&t, 1, 0, 1, &w) == CVAR_FORCE && cvar_step(&a, 1, 0, 0, &w) == CVAR_NONE,
            "the game reset one: only that one is forced again");
  w = -1;
  CHECK_MSG(cvar_step(&t, 0, 1, 1, &w) == CVAR_RESTORE && w == 0, "released: r_TestDOF back to 0");
  w = -1;
  CHECK_MSG(cvar_step(&a, 0, 0, 0, &w) == CVAR_RESTORE && w == six, "released: r_DOFBlurAmount back to 0.6");
}

// postfx.h: Per-pixel motion blur and Film grain grey out while the game's own option is off, or while their
// variable reads 0 and we did not set it.
static void tb_0191_postfx_game_off() {
  CHECK(postfx_info(PFX_PIXELMOTIONBLUR).game == PFX_GAME_PIXELMOTIONBLUR &&
        postfx_info(PFX_FILMGRAIN).game == PFX_GAME_FILMGRAIN && postfx_info(PFX_BLOOM).game == PFX_GAME_NONE);
  CHECK_MSG(pfx_game_off(1, 0, 1, 0) && pfx_game_off(1, 0, 0, 1), "the game's option off: greyed");
  CHECK_MSG(!pfx_game_off(1, 1, 1, 0) && !pfx_game_off(0, 0, 1, 0), "option on, or not known: live");
  CHECK_MSG(pfx_game_off(0, 0, 0, 0), "the variable already 0 and not ours: greyed");
  CHECK_MSG(!pfx_game_off(1, 1, 0, 1), "0 because we hold it: live");
}

// gamespeed.h: the audio rate is divided by our speed factor, never past Miles' range nor past the input.
static void tb_0193_gs_audio_rate() {
  int c = 0;
  CHECK(gs_audio_rate(22050, 1.f, &c) == 22050 && !c);
  CHECK_MSG(gs_audio_rate(11025, 0.5f, &c) == 22050 && !c, "50 %: the dilated rate doubled back");
  CHECK_MSG(gs_audio_rate(88200, 2.f, &c) == 44100 && !c, "200 %: halved back");
  CHECK_MSG(gs_audio_rate(50000, 0.1f, &c) == GS_RATE_MAX && c, "clamped at the top");
  c = 0;
  CHECK_MSG(gs_audio_rate(GS_RATE_MAX + 1000, 0.5f, &c) == GS_RATE_MAX + 1000 && c, "never past the input");
  c = 0;
  CHECK_MSG(gs_audio_rate(GS_RATE_MIN * 2, 4.f, &c) == GS_RATE_MIN && c, "clamped at the bottom");
  c = 0;
  CHECK(gs_audio_rate(GS_RATE_MIN / 2, 4.f, &c) == GS_RATE_MIN / 2 && c);
  c = 0;
  CHECK(gs_audio_rate(0, 0.5f, &c) == 0 && gs_audio_rate(22050, 0.f, &c) == 22050 && !c);
}

// gamespeed.h: only the two dilated call sites are divided, and only for a sound or stream that follows dilation.
static void tb_0194_gs_audio_site() {
  CHECK(gs_rate_site(0xD462) == GS_SITE_SAMPLE && gs_rate_site(0xB183) == GS_SITE_STREAM &&
        gs_rate_site(0xC1B6) == GS_SITE_START && gs_rate_site(0x5B95) == GS_SITE_VOIP &&
        gs_rate_site(0x1234) == GS_SITE_OTHER);
  CHECK(gs_rate_divides(GS_SITE_SAMPLE, 0) && !gs_rate_divides(GS_SITE_SAMPLE, 1));
  CHECK(gs_rate_divides(GS_SITE_STREAM, 0) && !gs_rate_divides(GS_SITE_STREAM, 1));
  CHECK_MSG(!gs_rate_divides(GS_SITE_START, 0) && !gs_rate_divides(GS_SITE_VOIP, 0) &&
            !gs_rate_divides(GS_SITE_OTHER, 0), "undilated sites pass through");
}

// progress.h: the totals split by scope. A New Game clears [EgoAwards] only (progress.md section
// 5): the playthrough block drops to 0 awards and the base cap, the profile block and the chapter ticks keep.
static void tb_0210_pg_scope_split() {
  PgFlags f;
  memset(&f, 0, sizeof f);
  f.award[28] = f.award[5] = 1;
  f.helmet[1] = 1;
  f.phone[0] = f.phone[11] = 1;
  f.completion[1][0] = 1u << 0;   // Duke Lives on Let's Rock (one part)
  PgTable tb;
  pg_table_builtin(&tb);
  PgChapter before[PG_CHAPTERS_MAX], after[PG_CHAPTERS_MAX];
  const int n = pg_build_chapters(tb, f, before);
  const PgRunTotals r0 = pg_run_totals(f, PG_CAP_AWARDS, PG_CAP_BASE, PG_CAP_MAX, PG_PREORDER_EGO, 1, 51.f);
  const PgProfileTotals p0 = pg_profile_totals(f);
  CHECK(r0.awardsFound == 2 && r0.capPermanent == 47 && r0.capNowOk && r0.capNow == 51);
  CHECK(p0.helmets == 1 && p0.phones == 2);
  memset(f.award, 0, sizeof f.award);   // DukePlayer.ClearEgoAwards
  const PgRunTotals r1 = pg_run_totals(f, PG_CAP_AWARDS, PG_CAP_BASE, PG_CAP_MAX, PG_PREORDER_EGO, 1, 40.f);
  const PgProfileTotals p1 = pg_profile_totals(f);
  CHECK_MSG(r1.awardsFound == 0 && r1.capPermanent == 40 && r1.capMax == 100, "the playthrough block starts over");
  CHECK_MSG(memcmp(&p0, &p1, sizeof p0) == 0, "the profile block keeps its values");
  pg_build_chapters(tb, f, after);
  int egoAfter = 0, sameTicks = 1;
  for (int i = 0; i < n; ++i) {
    egoAfter += after[i].egoFound;
    sameTicks &= memcmp(before[i].done, after[i].done, sizeof before[i].done) == 0;
  }
  CHECK_MSG(egoAfter == 0 && sameTicks && after[0].done[1], "per-chapter ego to 0, the ticks kept");
  const PgTotals t = pg_totals(f, PG_CAP_AWARDS, PG_CAP_BASE, PG_CAP_MAX, PG_PREORDER_EGO, 0, 0.f);
  CHECK_MSG(t.run.awardsFound == 0 && t.profile.helmets == 1, "pg_totals is both blocks");
}

// autowin.h: the mash rule. Presses only while the player uses a TapPowered control in idle that is still lifting,
// stops for good once a lift it pressed for completes (no press pending after Finish), and a new control (or none)
// ends the engagement.
static void tb_0211_aw_mash_rule() {
  AwMash m = {};
  int c1 = 1, c2 = 2;
  AwMashOut o = aw_mash_step(&m, &c1, 0, 1, 1, 1, -1.f, 1);
  CHECK_MSG(!o.press && !o.ev, "not a TapPowered control: nothing");
  o = aw_mash_step(&m, &c1, 1, 1, 1, 1, -1.f, 1);
  CHECK_MSG(o.press && o.ev == AW_EV_BEGIN && m.presses == 1, "the first press begins the engagement");
  o = aw_mash_step(&m, &c1, 1, 1, 1, 1, 0.2f, 1);
  CHECK(o.press && !o.ev && m.presses == 2);
  CHECK_MSG(!aw_mash_step(&m, &c1, 1, 1, 1, 1, 0.3f, 0).press, "off or paused: no press");
  CHECK_MSG(!aw_mash_step(&m, &c1, 1, 0, 1, 1, 0.3f, 1).press, "someone else's control: no press");
  CHECK_MSG(!aw_mash_step(&m, &c1, 1, 1, 0, 1, 0.3f, 1).press, "out of idle (Fail, Finish): no press");
  CHECK_MSG(!aw_mash_step(&m, &c1, 1, 1, 1, 1, nanf(""), 1).press, "a NaN lift never presses");
  o = aw_mash_step(&m, &c1, 1, 1, 1, 0, 1.f, 1);
  CHECK_MSG(!o.press && o.ev == AW_EV_LIFTED && m.lifted, "FullyLifted cleared bLift: done, no press");
  o = aw_mash_step(&m, &c1, 1, 1, 1, 1, -1.f, 1);
  CHECK_MSG(!o.press && !o.ev, "relaxed again on the same control (the bench): no second lift");
  o = aw_mash_step(&m, 0, 0, 0, 0, 0, 0.f, 1);
  CHECK_MSG(!o.press && o.ev == AW_EV_END && !m.presses && !m.lifted, "detached: the engagement ends");
  o = aw_mash_step(&m, &c1, 1, 1, 1, 1, -1.f, 1);
  CHECK_MSG(o.press && o.ev == AW_EV_BEGIN, "the same control used again is a new engagement");
  o = aw_mash_step(&m, &c2, 1, 1, 1, 1, 0.99f, 1);
  CHECK_MSG(o.press && o.ev == (AW_EV_END | AW_EV_BEGIN), "another control: one ends, the next begins");
  CHECK_MSG(!aw_mash_step(&m, &c2, 1, 1, 1, 1, 1.f, 1).press, "at 1 nothing is pressed");
  AwMash q = {};
  CHECK_MSG(!aw_mash_step(&q, &c1, 1, 1, 1, 0, -1.f, 1).ev, "a lift we never pressed for is not ours");
}

// autowin.h: the minigame rule. One win per use, keyed on the used object changing; it waits for ready and the
// switch, and is spent once made.
static void tb_0212_aw_use_rule() {
  AwUse u = {};
  int a = 1, b = 2;
  CHECK_MSG(!aw_use_step(&u, &a, 0, 1), "not ready (no start pressed, not in Play): no win");
  CHECK_MSG(aw_use_step(&u, &a, 1, 1), "ready: the win");
  CHECK_MSG(!aw_use_step(&u, &a, 1, 1) && !aw_use_step(&u, &a, 1, 1), "once per use");
  CHECK_MSG(!aw_use_step(&u, 0, 1, 1), "left the game: nothing");
  CHECK_MSG(aw_use_step(&u, &a, 1, 1), "used again: one more win");
  CHECK_MSG(!aw_use_step(&u, &b, 1, 0), "switch off: no win");
  CHECK_MSG(aw_use_step(&u, &b, 1, 1), "switched on mid-use: the win comes then");
  int wins = 0;
  for (int i = 0; i < 64; i++) wins += aw_use_step(&u, (i & 8) ? &a : &b, 1, 1);
  CHECK_MSG(wins == 7, "alternating every 8 frames: one win per new use");
}

// autowin.h: the class kinds by name chain, the air hockey index (never a guess) and the pinball points.
static void tb_0213_aw_helpers() {
  const int kinds[AW_KIND_COUNT] = { 501, 502, 503, 504, 505 };
  const int cycloid[] = { 900, 501, 400, 300 };
  CHECK_MSG(aw_kind_of_chain(cycloid, 4, kinds, AW_KIND_COUNT) == AW_KIND_TAP, "a TapPowered subclass");
  const int slots[] = { 503, 450 };
  CHECK(aw_kind_of_chain(slots, 2, kinds, AW_KIND_COUNT) == AW_KIND_SLOTS);
  const int other[] = { 0, 77, 400 };
  CHECK_MSG(aw_kind_of_chain(other, 3, kinds, AW_KIND_COUNT) == -1, "no kind, None never matches");
  const int unresolved[AW_KIND_COUNT] = { 0, 0, 0, 0, 0 };
  CHECK(aw_kind_of_chain(other, 3, unresolved, AW_KIND_COUNT) == -1);
  int c0 = 0, c1 = 0, me = 0;
  CHECK(aw_hockey_index(&c0, &c1, &c0) == 0 && aw_hockey_index(&c0, &c1, &c1) == 1);
  CHECK_MSG(aw_hockey_index(&c0, &c1, &me) == -1 && aw_hockey_index(&c0, &c0, &c0) == -1 &&
            aw_hockey_index(0, 0, 0) == -1, "not seated, or both slots alike: no index");
  CHECK(aw_pinball_points(0, 0) == 1 && aw_pinball_points(0, 50000) == 50001 && aw_pinball_points(900, 100) == 1);
  CHECK_MSG(aw_pinball_points(-2000000000, 2000000000) == 0x7FFFFFFF, "clamped, no overflow");
}

// config: both switches off by default, under [autowin].
static void tb_0214_aw_config() {
  config_defaults();
  CHECK(g_cfg.autoQte == 0 && g_cfg.autoMinigames == 0);
  CHECK(config_key_known("autowin", "qte") && config_key_known("autowin", "minigames"));
}

// world.h: the boss meter's actor (DukeHUD.BossMeterActor while bDrawBossMeter) is an enemy whatever its attitude
// or target; the watch line covers a non-hostile pawn with a target or the meter; the meter marks a knockdown.
static void tb_0220_world_boss_meter() {
  int en, al, ne;
  world_classify(1, 7, 0, 1, &en, &al, &ne); CHECK_MSG(en && !al && !ne, "Idolize, no target, the boss meter: enemy");
  world_classify(1, 7, 0, 0, &en, &al, &ne); CHECK(!en && al && !ne);
  world_classify(1, -1, 0, 1, &en, &al, &ne); CHECK(en && !al && !ne);
  world_classify(0, 7, 1, 1, &en, &al, &ne); CHECK_MSG(!en && !al && !ne, "dead: nothing");
  CHECK_MSG(world_watch(7, 1, 0) && world_watch(7, 0, 1) && world_watch(-1, 1, 0), "not hostile, target or meter");
  CHECK_MSG(!world_watch(2, 1, 1) && !world_watch(7, 0, 0), "hostile, or nothing set: no line");
  CHECK_MSG(world_knock_rule(1, 0) && world_knock_rule(0, 1) && !world_knock_rule(0, 0),
            "ControlX or the boss meter's actor: a knockdown");
}

// egoesp.h: markers only while the player can earn awards (DukePlayer.EgoCapAwardsEnabled; Map00 switches it off
// before the Cycloid fight, where its urinals grant nothing). An unresolved flag shows them, as before.
static void tb_0222_eg_awards_open() {
  CHECK(eg_awards_open(1, 1, 0) && !eg_awards_open(1, 0, 0));
  CHECK_MSG(eg_awards_open(0, 0, 0), "flag not found: markers stay");
  // PlayerPawn.bHasCheatedThisLevel (-1 unread counts as not cheated) closes the awards whatever the level says.
  CHECK(!eg_awards_open(1, 1, 1) && !eg_awards_open(0, 0, 1) && !eg_awards_open(1, 0, 1));
  CHECK(eg_awards_open(1, 1, -1) && eg_awards_open(0, 0, -1) && !eg_awards_open(1, 0, -1));
}

// autowin.h: pool is played by using a ball ("Hit Ball"); the first hit latches the level's table and wins once.
static void tb_0221_aw_pool_rule() {
  CHECK_MSG(aw_pool_hit(1, AW_KIND_POOLBALL, -1), "Use on a ball");
  CHECK_MSG(!aw_pool_hit(0, AW_KIND_POOLBALL, -1), "looking at a ball without Use");
  CHECK(!aw_pool_hit(1, -1, -1) && !aw_pool_hit(1, AW_KIND_TAP, -1));
  CHECK_MSG(aw_pool_hit(0, -1, AW_KIND_POOLBALL) && aw_pool_hit(0, -1, AW_KIND_POOLCUE), "a ball or the cue carried");
  static int objs_[2];  // distinct storage: an optimiser may fold equal unwritten locals
  int& t1 = objs_[0]; int& t2 = objs_[1];
  CHECK(aw_pool_latch(0, 0, &t1) == 0);
  CHECK_MSG(aw_pool_latch(0, 1, 0) == 0, "no table in the level: nothing latched");
  CHECK(aw_pool_latch(0, 1, &t1) == &t1);
  CHECK_MSG(aw_pool_latch(&t1, 0, &t2) == &t1 && aw_pool_latch(&t1, 1, &t2) == &t1, "latched for the level");
  AwUse u = {};
  const void* latch = 0;
  int wins = 0;
  for (int i = 0; i < 64; i++) {
    latch = aw_pool_latch(latch, i % 5 == 3, &t1);
    wins += aw_use_step(&u, latch, 1, 1);
  }
  CHECK_MSG(wins == 1, "one win per level, however many balls are hit");
}

// worldscan.h: a pass on entering a level (serial or LevelInfo moved) and every WS_RESCAN_MS after, none outside a
// level; a level change is seen before any read, but a load with no player is not a change by itself.
static void tb_0240_ws_rescan_rule() {
  static int lvA, lvB;
  WsClock c;
  ws_clock_reset(&c);
  CHECK(!ws_scan_due(c, 1, &lvA, 0, 1000) && ws_scan_due(c, 1, &lvA, 1, 1000) && !ws_scan_due(c, 1, 0, 1, 1000));
  CHECK_MSG(!ws_level_moved(c, 1, &lvA), "nothing built yet: nothing to clear");
  ws_scan_done(&c, 1, &lvA, 1000);
  CHECK(!ws_scan_due(c, 1, &lvA, 1, 1000 + WS_RESCAN_MS - 1) && ws_scan_due(c, 1, &lvA, 1, 1000 + WS_RESCAN_MS));
  CHECK_MSG(WS_RESCAN_MS >= 250 && WS_RESCAN_MS <= 500, "2-4 Hz (items.md section 4)");
  CHECK_MSG(ws_scan_due(c, 2, &lvA, 1, 1001) && ws_scan_due(c, 1, &lvB, 1, 1001), "a new level: at once");
  CHECK(ws_level_moved(c, 2, &lvA) && ws_level_moved(c, 1, &lvB) && !ws_level_moved(c, 1, &lvA));
  CHECK_MSG(!ws_level_moved(c, 1, 0), "no player during a load: not a move by itself");
  CHECK_MSG(ws_level_moved(c, 2, 0), "a new serial with no player yet: cleared");
  ws_scan_done(&c, 1, &lvA, 0xFFFFFF00u);
  CHECK_MSG(!ws_scan_due(c, 1, &lvA, 1, 0xFFFFFF00u + 100) && ws_scan_due(c, 1, &lvA, 1, 0xFFFFFF00u + 400),
            "across the tick wrap");
  CHECK(ws_period_due(0, 0, 5, IT_ASK_MS) && !ws_period_due(100, 1, 100 + IT_ASK_MS - 1, IT_ASK_MS) &&
        ws_period_due(100, 1, 100 + IT_ASK_MS, IT_ASK_MS));
}

struct WsAliveCtx { const void* gone; };
static bool ws_alive_stub(void* ctx, int, const void* obj) { return obj != ((WsAliveCtx*)ctx)->gone; }

// worldscan.h: the per-row cache. Entries whose slot no longer holds them go, in order (a taken pickup is destroyed);
// past WS_MAX nothing is added and the overflow is counted; a clear (each pass) empties both.
static void tb_0241_ws_cache() {
  static WsCache c;
  ws_cache_clear(&c);
  static int objs[3];
  for (int i = 0; i < 3; ++i) CHECK(ws_cache_add(&c, &objs[i], 10 + i, i));
  WsAliveCtx ctx = { &objs[1] };
  CHECK(ws_cache_prune(&c, ws_alive_stub, &ctx) == 1);
  CHECK(c.n == 2 && c.e[0].obj == &objs[0] && c.e[1].obj == &objs[2] && c.e[1].slot == 12 && c.e[1].rule == 2);
  CHECK(ws_cache_prune(&c, ws_alive_stub, &ctx) == 0 && c.n == 2);
  CHECK_MSG(WS_MAX >= 128, "78 explodables and 34 pickups fit (items.md section 3)");
  c.n = WS_MAX - 1;
  CHECK(ws_cache_add(&c, &objs[0], 1, 0) && c.n == WS_MAX && c.overflow == 0);
  CHECK(!ws_cache_add(&c, &objs[0], 2, 0) && !ws_cache_add(&c, &objs[0], 3, 0) && c.n == WS_MAX && c.overflow == 2);
  ws_cache_clear(&c);
  CHECK(c.n == 0 && c.overflow == 0);
}

// items.h: the class filter (nearest class on the super chain wins), presence, and the labels.
static void tb_0242_it_filter() {
  int names[IT_CLASS_COUNT];
  for (int i = 0; i < IT_CLASS_COUNT; ++i) names[i] = 100 + i;
  int pickup = -1, battery = -1, crate = -1, ammo = -1, pile = -1, gas = -1, cell = -1;
  for (int i = 0; i < IT_CLASS_COUNT; ++i) {
    const char* n = IT_CLASSES[i].name;
    if (!strcmp(n, "Pickup")) pickup = i;
    if (!strcmp(n, "BatteryPickup")) battery = i;
    if (!strcmp(n, "GasCanPickup")) gas = i;
    if (!strcmp(n, "EnergyCellPickup")) cell = i;
    if (!strcmp(n, "Containers_Crates_ItemCrate")) crate = i;
    if (!strcmp(n, "Containers_Crates_InfiniteAmmo")) ammo = i;
    if (!strcmp(n, "Containers_Pile_InfiniteAmmo")) pile = i;
  }
  CHECK(pickup >= 0 && battery >= 0 && gas >= 0 && cell >= 0 && crate >= 0 && ammo >= 0 && pile >= 0);
  const int shotgun[4] = { 7, 8, names[pickup], 9 };   // ShotgunPickup < WeaponPickup < Pickup < ...
  const int bat[3] = { names[battery], names[pickup], 9 };
  const int box[4] = { names[crate], 11, 12, 13 };
  const int other[3] = { 7, 8, 9 };
  CHECK(it_rule_of_chain(shotgun, 4, names, IT_CLASS_COUNT) == pickup);
  CHECK_MSG(it_rule_of_chain(bat, 3, names, IT_CLASS_COUNT) == battery, "the quest class before Engine.Pickup");
  CHECK(it_rule_of_chain(box, 4, names, IT_CLASS_COUNT) == crate && it_rule_of_chain(other, 3, names, IT_CLASS_COUNT) < 0);
  names[pickup] = 0;
  const int none[2] = { 0, 9 };
  CHECK_MSG(it_rule_of_chain(none, 2, names, IT_CLASS_COUNT) < 0, "NAME_None never matches");
  CHECK(it_row_of_kind(IT_CLASSES[pickup].kind) == IT_ROW_ITEMS && it_row_of_kind(IT_CLASSES[crate].kind) == IT_ROW_ITEMS);
  CHECK(it_row_of_kind(IT_CLASSES[ammo].kind) == IT_ROW_AMMO && it_row_of_kind(IT_CLASSES[pile].kind) == IT_ROW_AMMO);
  CHECK(it_row_of_kind(IT_CLASSES[battery].kind) == IT_ROW_QUEST && it_row_of_kind(IT_CLASSES[gas].kind) == IT_ROW_QUEST &&
        it_row_of_kind(IT_CLASSES[cell].kind) == IT_ROW_QUEST && it_row_of_kind(-1) < 0);
  CHECK(it_present(IT_QUEST, 0, 0, 0, 0) && it_present(IT_PICKUP, 0, 0, 0, 0) && it_present(IT_CRATE, 0, 0, 0, 0));
  CHECK_MSG(!it_present(IT_QUEST, 0, 0, 1, 0), "a quest item in the player's hands");
  CHECK_MSG(!it_present(IT_PICKUP, 1, 0, 0, 0) && !it_present(IT_PICKUP, 0, 1, 0, 0), "destroyed, or placed hidden");
  CHECK_MSG(!it_present(IT_CRATE, 0, 0, 0, 1) && !it_present(IT_CRATE, 0, 0, 0, 2), "a broken crate");
  CHECK_MSG(it_present(IT_PICKUP, 0, 0, 0, 2), "DecoState is a decoration's, not a pickup's");
  char l[48];
  it_class_label("PipeBombPickup", l, sizeof l);
  CHECK(strcmp(l, "Pipe Bomb") == 0);
  it_class_label("PistolAmmo", l, sizeof l);
  CHECK(strcmp(l, "Pistol Ammo") == 0);
  it_class_label("gascan", l, sizeof l);
  CHECK(strcmp(l, "Gas can") == 0);
  it_class_label("Pistol_Gold", l, sizeof l);
  CHECK(strcmp(l, "Pistol Gold") == 0);
  it_class_label("Pickup", l, sizeof l);
  CHECK_MSG(strcmp(l, "Pickup") == 0, "never stripped to nothing");
  it_class_label("EnergyCell", l, 7);
  CHECK(strcmp(l, "Energy") == 0);
  const char* two[2] = { "Pipebomb", "Pipebomb" };
  it_crate_label(two, 2, l, sizeof l);
  CHECK(strcmp(l, "Crate: Pipebomb x2") == 0);
  const char* mix[3] = { "Trip Mine", "40oz Beer", "Trip Mine" };
  it_crate_label(mix, 3, l, sizeof l);
  CHECK(strcmp(l, "Crate: Trip Mine x2, 40oz Beer") == 0);
  it_crate_label(mix, 0, l, sizeof l);
  CHECK_MSG(strcmp(l, "Crate") == 0, "contents unread: still a crate");
  it_crate_label(mix, 3, l, 12);
  CHECK(strlen(l) == 11 && strncmp(l, "Crate: Trip", 11) == 0);
}

// items.h: Hide what you can't take. CanPickupClass's refusal hides, a failed or unmade call shows; a crate hides
// only when every one of its contents is refused. Answers are kept per inventory class, per level.
static void tb_0243_it_hide_rule() {
  CHECK(it_answer(0, 1) == IT_ANS_UNKNOWN && it_answer(1, 1) == IT_ANS_TAKE && it_answer(1, 0) == IT_ANS_REFUSED);
  CHECK(it_hide_pickup(1, IT_ANS_REFUSED) && !it_hide_pickup(0, IT_ANS_REFUSED) && !it_hide_pickup(1, IT_ANS_TAKE));
  CHECK_MSG(!it_hide_pickup(1, IT_ANS_UNKNOWN), "a call failure shows the item");
  const int all[2] = { IT_ANS_REFUSED, IT_ANS_REFUSED }, some[2] = { IT_ANS_REFUSED, IT_ANS_TAKE };
  const int unk[2] = { IT_ANS_REFUSED, IT_ANS_UNKNOWN };
  CHECK(it_hide_crate(1, all, 2) && !it_hide_crate(0, all, 2) && !it_hide_crate(1, some, 2));
  CHECK_MSG(!it_hide_crate(1, unk, 2), "one content unanswered: shown");
  CHECK_MSG(!it_hide_crate(1, all, 0), "no contents read: shown");
  static ItAnswers t;
  it_answers_reset(&t);
  static int a, b, c;
  CHECK(it_answer_of(&t, &a) == IT_ANS_UNKNOWN && t.n == 1 && it_answer_of(&t, &a) == IT_ANS_UNKNOWN && t.n == 1);
  CHECK(it_answer_of(&t, 0) == IT_ANS_UNKNOWN && t.n == 1);
  it_answer_put(&t, &a, IT_ANS_REFUSED);
  it_answer_put(&t, &b, IT_ANS_TAKE);
  CHECK_MSG(it_answer_of(&t, &a) == IT_ANS_REFUSED && t.n == 1, "an answer for an unregistered class is dropped");
  t.n = IT_ANS_MAX;
  CHECK(it_answer_of(&t, &c) == IT_ANS_UNKNOWN && t.n == IT_ANS_MAX && t.overflow == 1);
  it_answers_reset(&t);
  CHECK(t.n == 0 && t.overflow == 0 && it_answer_of(&t, &a) == IT_ANS_UNKNOWN);
}

// config.cpp: the WORLD rows' keys and defaults (spec: all off but Items and pickups; DNMP's hidecanttake off).
static void tb_0244_esp2_config() {
  config_defaults();
  CHECK(g_cfg.espItems == 1 && g_cfg.espHideTaken == 0 && g_cfg.espAmmoCrates == 0 && g_cfg.espQuest == 0);
  CHECK(config_key_known("esp", "items") && config_key_known("esp", "hidecanttake") &&
        config_key_known("esp", "ammocrates") && config_key_known("esp", "questitems"));
}

static int bk_class(const char* name) {
  for (int i = 0; i < BK_CLASS_COUNT; ++i)
    if (strcmp(BK_CLASSES[i].name, name) == 0) return i;
  return -1;
}
static int bk_stash(const char* event) {
  for (int i = 0; i < BK_STASH_COUNT; ++i)
    if (!strcmp(BK_STASHES[i].event, event)) return i;
  return -1;
}

// breakables.h: the purposeful filter. Padlocks always, the known stashes by Event, anything else only
// with an Event; a pillar or board without one never. The padlock and gas pod classes match before dnDecoration.
static void tb_0245_bk_filter() {
  int names[BK_CLASS_COUNT];
  for (int i = 0; i < BK_CLASS_COUNT; ++i) names[i] = 200 + i;
  const int pad = bk_class("Details_Padlock"), deco = bk_class("dnDecoration"), pod = bk_class("Biology_Generic_GasPod");
  const int bld = bk_class("dnDestructibleBuilding");
  CHECK(pad >= 0 && deco >= 0 && pod >= 0 && bld >= 0);
  const int padChain[4] = { 7, names[pad], 8, names[deco] }, podChain[3] = { names[pod], 9, names[deco] };
  const int pillar[3] = { 7, 8, names[deco] }, other[2] = { 7, 8 };
  CHECK(it_rule_of_chain(padChain, 4, names, BK_CLASS_COUNT) == pad && it_rule_of_chain(podChain, 3, names, BK_CLASS_COUNT) == pod);
  CHECK(it_rule_of_chain(pillar, 3, names, BK_CLASS_COUNT) == deco && it_rule_of_chain(other, 2, names, BK_CLASS_COUNT) < 0);
  CHECK_MSG(bk_purposeful(BK_PADLOCK, 0, -1), "a padlock, Event or not");
  CHECK_MSG(bk_purposeful(BK_DECO, 1, -1) && bk_purposeful(BK_BUILDING, 1, -1), "an Event set");
  CHECK_MSG(!bk_purposeful(BK_DECO, 0, -1) && !bk_purposeful(BK_GASPOD, 0, -1) && !bk_purposeful(BK_BUILDING, 0, -1),
            "a pillar, board or pallet without an Event");
  CHECK_MSG(bk_purposeful(BK_DECO, 0, 0), "a known stash");
  CHECK(bk_stash("TreasureChest_Dispatcher") >= 0 && bk_stash("TriggerKarmaState_RailgunPickup_Mining_02") >= 0);
  CHECK(bk_stash("SecretSteroids_RoundRobin") >= 0 && bk_stash("Floor5_Jumppad1_Open_Relay") >= 0);
  CHECK_MSG(bk_stash("Trailer_Door1_Unlock") < 0, "a route breakable is not in the table");
  int events[BK_STASH_COUNT];
  for (int i = 0; i < BK_STASH_COUNT; ++i) events[i] = 400 + i;
  CHECK(bk_stash_of(400, events, BK_STASH_COUNT) == 0 && bk_stash_of(401, events, BK_STASH_COUNT) == 1);
  CHECK_MSG(bk_stash_of(399, events, BK_STASH_COUNT) < 0, "an Event outside the table");
  events[2] = 0;
  CHECK_MSG(bk_stash_of(0, events, BK_STASH_COUNT) < 0, "NAME_None and unresolved names never match");
  CHECK(bk_rule_pack(BK_DECO, 3) != bk_rule_pack(BK_DECO, -1) && bk_rule_kind(bk_rule_pack(BK_GASPOD, 5)) == BK_GASPOD &&
        bk_rule_stash(bk_rule_pack(BK_GASPOD, 5)) == 5 && bk_rule_stash(bk_rule_pack(BK_PADLOCK, -1)) == -1);
}

// breakables.h: alive means intact and damageable; a building while it has parts; unresolved fields count as alive.
static void tb_0246_bk_alive() {
  CHECK(bk_alive(BK_DECO, 0, 0, 0, 3, 0, 0, -1) && bk_alive(BK_PADLOCK, 0, 0, 0, -1, 0, 0, -1));
  CHECK_MSG(!bk_alive(BK_DECO, 1, 0, 0, 3, 0, 0, -1) && !bk_alive(BK_DECO, 0, 1, 0, 3, 0, 0, -1), "destroyed or hidden");
  CHECK_MSG(!bk_alive(BK_DECO, 0, 0, 1, 3, 0, 0, -1) && !bk_alive(BK_DECO, 0, 0, 0, 0, 0, 0, -1), "bNoDamage, NeverBreak");
  CHECK_MSG(!bk_alive(BK_PADLOCK, 0, 0, 0, 3, 1, 0, -1) && !bk_alive(BK_DECO, 0, 0, 0, 3, 2, 0, -1), "DecoState dead");
  CHECK_MSG(!bk_alive(BK_GASPOD, 0, 0, 0, 3, 0, 1, -1) && bk_alive(BK_GASPOD, 0, 0, 0, 3, 0, 0, -1), "a popped pod");
  CHECK_MSG(bk_alive(BK_BUILDING, 0, 0, 0, -1, 0, 0, 4) && !bk_alive(BK_BUILDING, 0, 0, 0, -1, 0, 0, 0), "Parts emptied");
  CHECK(bk_alive(BK_BUILDING, 0, 0, 0, -1, 0, 0, -1) && !bk_alive(BK_BUILDING, 1, 0, 0, -1, 0, 0, 4));
}

// breakables.h: "Stash: <what it opens>", "Padlock", else the class's short name.
static void tb_0247_bk_label() {
  char l[48];
  const int chest = bk_stash("TreasureChest_Dispatcher");
  bk_label(BK_PADLOCK, chest, "Details_Padlock_Base", l, sizeof l);
  CHECK(strncmp(l, "Stash: ", 7) == 0 && strcmp(l + 7, BK_STASHES[chest].opens) == 0);
  bk_label(BK_PADLOCK, -1, "Details_Padlock_Base", l, sizeof l);
  CHECK(strcmp(l, "Padlock") == 0);
  bk_label(BK_DECO, -1, "Barrels_Generic_Explosive01", l, sizeof l);
  CHECK(strcmp(l, "Barrels Explosive01") == 0);
  bk_label(BK_DECO, -1, "Cannister_Generic_Red", l, sizeof l);
  CHECK(strcmp(l, "Cannister Red") == 0);
  bk_label(BK_BUILDING, -1, "dnDestructibleBuilding", l, sizeof l);
  CHECK(strcmp(l, "DestructibleBuilding") == 0);
  bk_label(BK_DECO, -1, "Vents_Generic_BreakableGrate_B", l, 12);
  CHECK(strcmp(l, "Vents Break") == 0);
  bk_label(BK_DECO, -1, "", l, sizeof l);
  CHECK(strcmp(l, "Breakable") == 0);
}

// breakables.h (2026-10-11): a checkpoint loads the level from a package named cpNN, so the map name never
// matches; the identity is the Event name alone. A route breakable reads "Path: ..." and is not drawn as a stash.
static void tb_0521_bk_path() {
  char l[48];
  const int pod = bk_stash("Floor5_Jumppad1_Open_Relay"), chest = bk_stash("TreasureChest_Dispatcher");
  CHECK(pod >= 0 && chest >= 0);
  CHECK(bk_stash_is_path(pod) && !bk_stash_is_path(chest) && !bk_stash_is_path(-1) && !bk_stash_is_path(BK_STASH_COUNT));
  bk_label(BK_GASPOD, pod, "Biology_Generic_GasPod", l, sizeof l);
  CHECK(strncmp(l, "Path: ", 6) == 0 && strcmp(l + 6, BK_STASHES[pod].opens) == 0);
  bk_label(BK_PADLOCK, chest, "Details_Padlock_Base", l, sizeof l);
  CHECK(strncmp(l, "Stash: ", 7) == 0);
  int events[BK_STASH_COUNT];
  for (int i = 0; i < BK_STASH_COUNT; ++i) events[i] = 500 + i;
  CHECK_MSG(bk_stash_of(500 + pod, events, BK_STASH_COUNT) == pod, "the same Event in a cpNN package");
  for (int i = 0; i < BK_STASH_COUNT; ++i)
    CHECK_MSG(BK_STASHES[i].event && BK_STASHES[i].event[0] && BK_STASHES[i].opens[0], "every row names its Event");
}

// breakables.h: a decoration with an Event shows only in a breakable family (items.md section 5); lights,
// switches, cups and fish fire Events too and are noise.
static void tb_0249_bk_family() {
  CHECK(bk_family_ok("Barrels_Generic_Explosive01") && bk_family_ok("Cannister_Generic_Red"));
  CHECK(bk_family_ok("Containers_Crates_Destructible") && bk_family_ok("Vents_Generic_BreakableGrate_B"));
  CHECK(bk_family_ok("Details_Generic_DestructibleWoodenBoard_A") && bk_family_ok("dnDestructibleBuilding"));
  CHECK_MSG(!bk_family_ok("Lights_Generic_Lamp") && !bk_family_ok("Details_Generic_Cup") && !bk_family_ok(""),
            "non-breakables with an Event are left out");
  CHECK(!bk_family_ok(0));
}

// breakables.h: a crate (Containers_Crates_) is marked only when it holds items and is not an item crate (the Items
// row names those by contents). An Event or a stash alone no longer marks a crate; other families are unchanged.
static void tb_0470_bk_crate_rule() {
  CHECK(bk_crate_class("Containers_Crates_Destructible") == BK_CR_CRATE);
  CHECK(bk_crate_class("Containers_Crates_ItemCrate") == BK_CR_ITEM);
  CHECK(bk_crate_class("Barrels_Generic_Explosive01") == BK_CR_NONE && bk_crate_class("Containers_Pots") == BK_CR_NONE);
  CHECK(bk_crate_class(0) == BK_CR_NONE);
  CHECK_MSG(!bk_crate_shown(BK_CR_CRATE, 0) && bk_purposeful(BK_DECO, 1, -1), "an empty crate with an Event is not marked");
  CHECK_MSG(!bk_crate_shown(BK_CR_CRATE, 0) && bk_purposeful(BK_DECO, 0, 0), "an empty stash crate is not marked");
  CHECK_MSG(!bk_crate_shown(BK_CR_ITEM, 1) && !bk_crate_shown(BK_CR_ITEM, 0), "an item crate is never marked here");
  CHECK_MSG(bk_crate_shown(BK_CR_CRATE, 1), "a crate that spawns a pickup is marked");
  CHECK_MSG(bk_crate_shown(BK_CR_NONE, 0) && bk_crate_shown(BK_CR_NONE, 1), "other families are unchanged");
  CHECK_MSG(bk_crate_holds(0, -1) == 0 && bk_crate_holds(1, -1) && bk_crate_holds(0, 1), "a stash leads to a pickup");
  CHECK_MSG(bk_crate_shown(BK_CR_CRATE, bk_crate_holds(0, 1)) && !bk_crate_shown(BK_CR_ITEM, bk_crate_holds(0, 2)),
            "a stash crate stays marked; a stash item crate is the Items row's");
  char l[48];
  bk_crate_label(l, sizeof l);
  CHECK(strcmp(l, "Crate: items") == 0);
}

// aim.h: the point-geometry log (egg pods, 2026-10-10) takes a pawn once per distance band, with a cap per band, so
// far pods seen first (behind real cover) cannot use up the lines a near pod needs.
static void tb_0471_aim_ptlog() {
  CHECK_MSG(aim_ptlog_band(999.f) == 0 && aim_ptlog_band(1000.f) == 1 && aim_ptlog_band(1999.f) == 1 &&
            aim_ptlog_band(2000.f) == 2, "bands: under 1000, under 2000, beyond");
  const float nan = sqrtf(-1.f);
  CHECK_MSG(aim_ptlog_band(nan) == 2, "NaN is the far band");
  AimPtLog l;
  memset(&l, 0, sizeof l);
  static int objs_[2];  // distinct storage: an optimiser may fold equal unwritten locals
  int& a = objs_[0]; int& b = objs_[1];
  CHECK_MSG(aim_ptlog_take(&l, &a, 2) && !aim_ptlog_take(&l, &a, 2), "once per pawn and band");
  CHECK_MSG(aim_ptlog_take(&l, &a, 0) && aim_ptlog_take(&l, &b, 2), "a new band or pawn is taken");
  CHECK(!aim_ptlog_take(&l, 0, 0) && !aim_ptlog_take(&l, &a, 3) && !aim_ptlog_take(&l, &a, -1));
  int pods[AIM_PTLOG_PER_BAND + 2];
  int took = 0;
  for (int i = 0; i < AIM_PTLOG_PER_BAND + 2; i++) took += aim_ptlog_take(&l, &pods[i], 2);
  CHECK_MSG(took == AIM_PTLOG_PER_BAND - 2, "the far band is capped (a and b took two)");
  CHECK_MSG(aim_ptlog_take(&l, &b, 0) && aim_ptlog_take(&l, &b, 1), "a full far band leaves the near bands free");
}

// aim.h: TraceFire's pass rule (Engine.dll!0x10282A30): Immediate passes free, Backtrace spends a penetration and
// stops past MaxPenetrationDistance, Abort and an unread response stop. The re-trace starts just past the hit.
static void tb_0472_aim_bullet_passes() {
  int pen = 0;
  CHECK_MSG(aim_bullet_passes(AIM_THR_IMMEDIATE, &pen, 256.f, 900.f) && pen == 0, "Immediate passes, spends nothing");
  CHECK_MSG(!aim_bullet_passes(AIM_THR_BACKTRACE, &pen, 256.f, 10.f), "Backtrace with no penetration left stops");
  CHECK_MSG(!aim_bullet_passes(AIM_THR_ABORT, &pen, 0.f, 10.f) && !aim_bullet_passes(-1, &pen, 0.f, 10.f),
            "Abort and unread stop");
  pen = 2;
  CHECK_MSG(aim_bullet_passes(AIM_THR_BACKTRACE, &pen, 128.f, 100.f) && pen == 1, "Backtrace within reach spends one");
  CHECK_MSG(!aim_bullet_passes(AIM_THR_BACKTRACE, &pen, 128.f, 129.f) && pen == 1, "past MaxPenetrationDistance stops");
  CHECK_MSG(aim_bullet_passes(AIM_THR_BACKTRACE, &pen, 0.f, 5000.f) && pen == 0, "no distance cap when 0");
  pen = 1;
  CHECK_MSG(aim_bullet_passes(3, &pen, 0.f, 1.f) && pen == 0, "any other response is Backtrace's branch");
  const float a[3] = { 0.f, 0.f, 0.f }, b[3] = { 100.f, 0.f, 0.f }, h[3] = { 40.f, 0.f, 0.f };
  float o[3];
  CHECK_MSG(aim_step_past(a, b, h, o) && o[0] > 40.f && o[0] < 42.f && o[1] == 0.f, "one unit past the hit");
  const float h2[3] = { 99.5f, 0.f, 0.f };
  CHECK_MSG(!aim_step_past(a, b, h2, o), "nothing left of the line");
  CHECK(!aim_step_past(a, a, a, o));
}

// ---- Mounted collision (mountcol.h; docs/re/spawns.md section 8) ----
static McHelper mc_test_helper(float x, float r, float h, int taker) {
  McHelper m = {};
  m.c[0] = x;
  m.r = r;
  m.h = h;
  m.taker = taker;
  return m;
}
static void tb_0473_mc_helper_choice() {
  CHECK_MSG(mc_tiny(0.f, 88.f) && mc_tiny(34.f, 0.f) && mc_tiny(34.f, 3.f), "collision 0 or tiny: its helpers decide");
  CHECK_MSG(!mc_tiny(34.f, 88.f) && !mc_tiny(4.f, 4.f), "a real cylinder keeps today's behaviour");
  CHECK_MSG(mc_is_helper(1, 60.f, 90.f, 0), "a colliding mounted cylinder is a hit shape");
  CHECK_MSG(!mc_is_helper(0, 60.f, 90.f, 0) && !mc_is_helper(-1, 60.f, 90.f, 0), "no collision, or unread: not one");
  CHECK_MSG(!mc_is_helper(1, 0.f, 90.f, 0) && !mc_is_helper(1, 60.f, 0.f, 0) && !mc_is_helper(1, 60.f, 90.f, 1),
            "a zero cylinder or a walked pawn: not one");
  const McHelper a[3] = { mc_test_helper(0.f, 10.f, 10.f, 0), mc_test_helper(1.f, 40.f, 60.f, 0),
                          mc_test_helper(2.f, 5.f, 5.f, 1) };
  int o[MC_MAX];
  CHECK_MSG(mc_order(a, 3, 0, o) == 3 && o[0] == 2 && o[1] == 1 && o[2] == 0, "the damage taker first, then the largest");
  CHECK_MSG(mc_order(a, 2, 0, o) == 2 && o[0] == 1 && o[1] == 0, "no taker: the largest (the _Body) first");
  const McHelper t[3] = { mc_test_helper(0.f, 10.f, 10.f, 0), mc_test_helper(1.f, 10.f, 10.f, 0),
                          mc_test_helper(2.f, 10.f, 10.f, 0) };
  CHECK_MSG(mc_order(t, 3, 0, o) == 3 && o[0] == 0 && o[1] == 1 && o[2] == 2, "a tie keeps the list's order");
}
static void tb_0474_mc_union_box() {
  const McHelper a[2] = { mc_test_helper(0.f, 10.f, 20.f, 0), mc_test_helper(100.f, 5.f, 5.f, 0) };
  McHelper b[2] = { a[0], a[1] };
  b[1].c[2] = 50.f;
  float c[3], h[3];
  CHECK_MSG(mc_union(b, 2, c, h) && c[0] == 47.5f && c[1] == 0.f && c[2] == 17.5f && h[0] == 57.5f && h[1] == 10.f &&
            h[2] == 37.5f, "the box round both cylinders");
  CHECK_MSG(mc_union(a, 1, c, h) && c[0] == 0.f && h[0] == 10.f && h[1] == 10.f && h[2] == 20.f, "one: its cylinder");
  CHECK_MSG(!mc_union(a, 0, c, h), "none: no box");
}
static void tb_0475_mc_velocity() {
  McVel v = {};
  int x = 0, y = 0;
  float out[3];
  const float p0[3] = { 0.f, 0.f, 0.f }, p1[3] = { 10.f, 0.f, 0.f }, p2[3] = { 30.f, 0.f, 0.f };
  CHECK_MSG(mc_vel_step(&v, &x, p0, 0.1f, out) == 0 && out[0] == 0.f, "the first sample: no velocity yet");
  CHECK_MSG(mc_vel_step(&v, &x, p1, 0.1f, out) == 1 && fabsf(out[0] - 100.f) < 0.01f, "the delta over the frame time");
  CHECK_MSG(mc_vel_step(&v, &x, p2, 0.1f, out) == 2 && fabsf(out[0] - 150.f) < 0.01f, "smoothed: the mean of the frames");
  CHECK_MSG(mc_vel_step(&v, &x, p2, 0.f, out) == 2 && fabsf(out[0] - 150.f) < 0.01f, "no frame time: no new sample");
  float px[3] = { 30.f, 0.f, 0.f };
  for (int i = 0; i < 4; i++) {
    px[0] += 5.f;
    mc_vel_step(&v, &x, px, 0.1f, out);
  }
  CHECK_MSG(v.n == MC_VEL_N && fabsf(out[0] - 50.f) < 0.01f, "only the last frames count");
  CHECK_MSG(mc_vel_step(&v, &y, p1, 0.1f, out) == 0 && out[0] == 0.f && v.obj == &y, "another helper: restarted");
  CHECK_MSG(mc_vel_step(&v, &y, p2, 0.1f, out) == 1 && fabsf(out[0] - 200.f) < 0.01f, "from the new point only");
  CHECK_MSG(mc_vel_step(&v, &y, p0, 1.f, out) == 0 && out[0] == 0.f, "a pause restarts it");
  McVel w = {};
  for (int i = 0; i < 4; i++) mc_vel_step(&w, &x, p0, 0.1f, out);
  CHECK_MSG(mc_vel_step(&w, &x, p1, 0.1f, out) == 0, "the first move after standing still only re-anchors");
  CHECK_MSG(mc_vel_step(&w, &x, p2, 0.1f, out) == 1 && fabsf(out[0] - 200.f) < 0.01f, "then moves count at full speed");
}
static void tb_0476_mc_body_without_helpers() {
  const float hc[3] = { 1.f, 2.f, 3.f }, hh[3] = { 4.f, 5.f, 6.f }, bc[3] = { 7.f, 8.f, 9.f }, bh[3] = { 10.f, 11.f, 12.f };
  float c[3] = { -1.f, -1.f, -1.f }, h[3] = { -1.f, -1.f, -1.f };
  CHECK_MSG(mc_body(0, hc, hh, 0, bc, bh, c, h) == MC_SRC_NONE && c[0] == -1.f && h[0] == -1.f,
            "no helpers, no render box: the Location cylinder stays");
  CHECK_MSG(mc_body(0, hc, hh, 1, bc, bh, c, h) == MC_SRC_BOX && c[0] == 7.f && h[2] == 12.f,
            "no helpers: an animated-away pawn keeps its render box");
  CHECK_MSG(mc_body(1, hc, hh, 1, bc, bh, c, h) == MC_SRC_HELPERS && c[0] == 1.f && h[2] == 6.f,
            "helpers replace the render box");
}

static void tb_0477_mc_tiebreak() {
  const McHelper a[3] = { mc_test_helper(0.f, 64.f, 64.f, 1), mc_test_helper(100.f, 64.f, 64.f, 1),
                          mc_test_helper(200.f, 64.f, 64.f, 1) };
  const float centre[3] = { 180.f, 0.f, 0.f };
  int o[MC_MAX];
  CHECK_MSG(mc_order(a, 3, centre, o) == 3 && o[0] == 2 && o[1] == 1 && o[2] == 0,
            "equal rank: the one nearest the middle of the body first (not the tail base first in the list)");
  McHelper b[3] = { a[0], a[1], a[2] };
  b[0].r = 80.f;
  CHECK_MSG(mc_order(b, 3, centre, o) == 3 && o[0] == 0, "a larger cylinder still outranks nearness");
  b[0].r = 64.f;
  b[0].taker = 1;
  b[1].taker = b[2].taker = 0;
  CHECK_MSG(mc_order(b, 3, centre, o) == 3 && o[0] == 0 && o[1] == 2, "a taker still outranks nearness");
}
static void tb_0478_mc_hysteresis() {
  const float centre[3] = { 0.f, 0.f, 0.f };
  const McHelper cur = mc_test_helper(100.f, 64.f, 64.f, 1);
  CHECK_MSG(!mc_better(mc_test_helper(80.f, 64.f, 64.f, 1), cur, centre), "20 % nearer: the current one stays");
  CHECK_MSG(mc_better(mc_test_helper(60.f, 64.f, 64.f, 1), cur, centre), "40 % nearer: switch");
  CHECK_MSG(!mc_better(mc_test_helper(-71.f, 64.f, 64.f, 1), cur, centre), "29 % nearer on the other side: stays");
  CHECK_MSG(mc_better(mc_test_helper(500.f, 64.f, 64.f, 1), mc_test_helper(10.f, 64.f, 64.f, 0), centre),
            "a taker over a non-taker, however far");
  CHECK_MSG(!mc_better(mc_test_helper(1.f, 64.f, 64.f, 0), cur, centre), "a lower rank never wins on nearness");
  CHECK_MSG(!mc_better(cur, cur, centre), "itself: no switch");
}
static void tb_0479_mc_vel_position_changes() {
  McVel v = {};
  int x = 0;
  float out[3];
  const float p0[3] = { 0.f, 0.f, 0.f }, p1[3] = { 12.f, 0.f, 0.f };
  mc_vel_step(&v, &x, p0, 0.006f, out);
  CHECK_MSG(mc_vel_step(&v, &x, p0, 0.006f, out) == 0 && out[0] == 0.f, "our frame saw the same position: no sample");
  mc_vel_step(&v, &x, p0, 0.006f, out);
  CHECK_MSG(mc_vel_step(&v, &x, p1, 0.006f, out) == 1 && fabsf(out[0] - 12.f / 0.018f) < 0.5f,
            "the move over the time since the position last changed, not over one frame");
  float px[3] = { 12.f, 0.f, 0.f };
  int n = 1;
  for (int i = 0; i < 6; i++) n = mc_vel_step(&v, &x, px, 0.05f, out);
  CHECK_MSG(n == 0 && out[0] == 0.f, "no change for longer than the gap: still, no velocity");
}

static void tb_0248_bk_config() {
  config_defaults();
  CHECK(g_cfg.espBreakables == 0 && config_key_known("esp", "breakables"));
}

// ---- Marker boxes from the model (modelbox.h) and the wired-monitor rule (egoesp.h) ----
static MbPlace mb_test_place(float x) {
  MbPlace t = {};
  t.loc[0] = x;
  t.drawScale = 1.f;
  t.scale3d[0] = t.scale3d[1] = t.scale3d[2] = 1.f;
  return t;
}
static bool mb_near(const float* p, float x, float y, float z) {
  return fabsf(p[0] - x) < 0.01f && fabsf(p[1] - y) < 0.01f && fabsf(p[2] - z) < 0.01f;
}
static void tb_0255_mb_local_ok() {
  MbLocal b = { { -10.f, -5.f, -2.f }, { 10.f, 5.f, 2.f }, 1 };
  CHECK(mb_local_ok(b));
  MbLocal flat = { { -10.f, -5.f, 0.f }, { 10.f, 5.f, 0.f }, 1 };
  CHECK_MSG(mb_local_ok(flat), "a flat model (a poster) has a box");
  MbLocal x = b;
  x.valid = 0;
  CHECK_MSG(!mb_local_ok(x), "IsValid clear: no bounds");
  x = b;
  x.min[0] = 20.f;
  CHECK(!mb_local_ok(x));
  x = b;
  x.max[2] = sqrtf(-1.f);
  CHECK(!mb_local_ok(x));
  x = b;
  x.max[1] = 9000.f;
  CHECK_MSG(!mb_local_ok(x), "longer than any decoration: garbage");
  MbLocal pt = { { 0.f, 0.f, 0.f }, { 0.f, 0.f, 0.f }, 1 };
  CHECK_MSG(!mb_local_ok(pt), "a point is not a model");
}
// LocalToWorldCoords' bLowerByCollision: as PrePivot the model drops by CollisionHeight in scaled local Z, else in
// world Z through PostPivot.
static void tb_0256_mb_pivots() {
  MbPlace t = mb_test_place(0.f);
  t.scale3d[2] = 2.f;
  t.prePivot[0] = 1.f; t.prePivot[1] = 2.f; t.prePivot[2] = 3.f;
  t.postPivot[0] = 4.f; t.postPivot[1] = 5.f; t.postPivot[2] = 6.f;
  t.collisionHeight = 10.f;
  float pre[3], post[3];
  CHECK(mb_pivots(t, pre, post) && mb_near(pre, 1.f, 2.f, 3.f) && mb_near(post, 4.f, 5.f, 6.f));
  t.lowerAsPrePivot = 1;
  CHECK_MSG(mb_pivots(t, pre, post) && mb_near(pre, 1.f, 2.f, 3.f), "AsPrePivot alone lowers nothing");
  t.lowerByCollision = 1;
  CHECK(mb_pivots(t, pre, post) && mb_near(pre, 1.f, 2.f, 8.f) && mb_near(post, 4.f, 5.f, 6.f));
  t.lowerAsPrePivot = 0;
  CHECK(mb_pivots(t, pre, post) && mb_near(pre, 1.f, 2.f, 3.f) && mb_near(post, 4.f, 5.f, -4.f));
  t.lowerAsPrePivot = 1;
  t.scale3d[2] = 0.f;
  CHECK_MSG(!mb_pivots(t, pre, post), "a zero Z scale cannot divide");
}
// Location + PostPivot + R * (DrawScale * DrawScale3D * (local - PrePivot)); corner bit 0 = Max.X, 1 = Max.Y, 2 = Max.Z.
static void tb_0257_mb_corners() {
  const MbLocal b = { { -10.f, -5.f, -2.f }, { 10.f, 5.f, 2.f }, 1 };
  MbPlace t = mb_test_place(100.f);
  MbCorners c = mb_corners(t, b);
  CHECK(c.ok && mb_near(c.p[0], 90.f, -5.f, -2.f) && mb_near(c.p[7], 110.f, 5.f, 2.f) && mb_near(c.p[1], 110.f, -5.f, -2.f));
  t.rot[1] = 16384;   // yaw a quarter turn: local X along world +Y
  c = mb_corners(t, b);
  CHECK_MSG(c.ok && mb_near(c.p[1], 105.f, 10.f, -2.f), "yaw turns the box with the model");
  t.rot[1] = 0;
  t.rot[0] = 16384;   // pitch a quarter turn: local X straight up
  c = mb_corners(t, b);
  CHECK(c.ok && mb_near(c.p[1], 102.f, -5.f, 10.f));
  t = mb_test_place(100.f);
  t.drawScale = 2.f;
  c = mb_corners(t, b);
  CHECK(c.ok && mb_near(c.p[7], 120.f, 10.f, 4.f));
  t.prePivot[0] = 10.f;
  c = mb_corners(t, b);
  CHECK_MSG(c.ok && mb_near(c.p[0], 60.f, -10.f, -4.f), "PrePivot shifts before the scale");
  t = mb_test_place(100.f);
  t.lowerByCollision = 1;
  t.collisionHeight = 10.f;
  c = mb_corners(t, b);
  CHECK(c.ok && mb_near(c.p[0], 90.f, -5.f, -12.f));
  MbLocal bad = b;
  bad.valid = 0;
  CHECK(!mb_corners(mb_test_place(100.f), bad).ok);
  t = mb_test_place(100.f);
  t.drawScale = 0.f;
  CHECK_MSG(!mb_corners(t, b).ok, "a zero scale: the cylinder instead");
}
// A shotgun pickup (ShotGun_Static 26.5 x 2.3 x 8.3 UU, cylinder r 32) 200 UU ahead on the 100x100 FOV 90 view.
static void tb_0258_mb_screen_box() {
  const WView v = esp_test_view();
  const MbLocal gun = { { -13.26f, -1.16f, -4.16f }, { 13.26f, 1.16f, 4.16f }, 1 };
  MbPlace t = mb_test_place(200.f);
  int ok = 0, okc = 0;
  EkScreenBox m = mb_screen_box(v, mb_corners(t, gun), 100, 100, &ok);
  const EkScreenBox cyl = esp_pawn_box(v, 200.f, 0.f, 0.f, 32.f, 8.f, 100, 100, &okc);
  CHECK(ok && okc && m.w * 4 <= cyl.w && m.h <= cyl.h);
  t.rot[1] = 16384;   // lying across the view: as wide as the gun is long
  m = mb_screen_box(v, mb_corners(t, gun), 100, 100, &ok);
  CHECK(ok && m.w >= 6 && m.w <= 8 && m.x >= 45 && m.x + m.w <= 55);
  t = mb_test_place(5.f);   // the camera inside the model: some corners behind, no box
  mb_screen_box(v, mb_corners(t, gun), 100, 100, &ok);
  CHECK_MSG(ok == 0, "a corner behind the camera: no box, never edge-to-edge");
  t = mb_test_place(200.f);
  t.loc[1] = 5000.f;
  mb_screen_box(v, mb_corners(t, gun), 100, 100, &ok);
  CHECK(ok == 0);
  MbCorners none = {};
  mb_screen_box(v, none, 100, 100, &ok);
  CHECK(ok == 0);
}
static void tb_0259_mb_marker_box() {
  const WView v = esp_test_view();
  MbCorners none = {};
  int ok = 0;
  EkScreenBox b = mb_marker_box(v, none, 200.f, 0.f, 0.f, 10.f, 10.f, 100, 100, &ok);
  CHECK_MSG(ok && b.x == 47 && b.y == 47 && b.w == 6 && b.h == 6, "unresolved bounds: the cylinder's box");
  const MbLocal cube = { { -20.f, -20.f, -20.f }, { 20.f, 20.f, 20.f }, 1 };
  const MbCorners c = mb_corners(mb_test_place(200.f), cube);
  int ok2 = 0;
  const EkScreenBox want = mb_screen_box(v, c, 100, 100, &ok2);
  b = mb_marker_box(v, c, 200.f, 0.f, 0.f, 10.f, 10.f, 100, 100, &ok);
  CHECK(ok && ok2 && b.x == want.x && b.y == want.y && b.w == want.w && b.h == want.h && b.w > 6);
}
// Electronics_Generic_Computer_Monitor.SwitchScreens grants 25 and only Keyboard.KeyPress reaches it, through the
// CPU whose FindComponents set the monitor's CPU; Map02's five wall monitors have no CPU or keyboard in the map.
static void tb_0260_eg_monitor_wired() {
  CHECK(eg_monitor_wired(1, 1, 0) == 1);
  CHECK_MSG(eg_monitor_wired(0, 1, 0) == 0, "no CPU: Map02's wall monitors");
  CHECK_MSG(eg_monitor_wired(1, 0, 0) == 0, "a CPU with no keyboard");
  CHECK_MSG(eg_monitor_wired(1, 2, 1) == 0, "hidden");
  int ids[EG_IDS];
  const int monitor = eg_rule("Electronics_Generic_Computer_Monitor");
  CHECK(eg_giver_ids(EG_CLASSES[monitor], 0, 0, 1, eg_monitor_wired(1, 1, 0), ids) == 1 && ids[0] == 25);
  CHECK(eg_giver_ids(EG_CLASSES[monitor], 0, 0, 1, eg_monitor_wired(0, 0, 0), ids) == 0);
  CHECK_MSG(eg_giver_ids(EG_CLASSES[monitor], 0, 0, 0, eg_monitor_wired(1, 1, 0), ids) == 0, "switched a moment ago");
}

// dukevision.h: one RemoveHudEffect call per Duke Vision entry, in order; other effects and NAME_None are left.
static void tb_0265_dv_plan() {
  CHECK(wcscmp(DV_TARGETS[0], L"DukeVisionHUDEffect") == 0 && wcscmp(DV_TARGETS[1], L"DukeVisionHUDEffectRemove") == 0 &&
        wcscmp(DV_TARGETS[2], L"DukeVisionBlackHack") == 0);
  const int targets[DV_TARGET_COUNT] = { 11, 12, 13 };
  const int names[6] = { 40, 13, 11, 0, 12, 13 };
  int out[8] = {};
  CHECK_MSG(dv_plan(names, 6, targets, out, 8) == 4 && out[0] == 13 && out[1] == 11 && out[2] == 12 && out[3] == 13,
            "each target entry once, a repeated name twice");
  CHECK_MSG(dv_plan(names, 6, targets, out, 2) == 2, "capped");
  const int none[DV_TARGET_COUNT] = { 0, 0, 0 };
  CHECK_MSG(dv_plan(names, 6, none, out, 8) == 0, "an unresolved target matches nothing, NAME_None included");
  CHECK(dv_plan(0, 6, targets, out, 8) == 0 && dv_plan(names, 0, targets, out, 8) == 0);
}

static void tb_0266_dv_config() {
  config_defaults();
  CHECK_MSG(g_cfg.cleanDukeVision == 0 && config_key_known("view", "cleandukevision"), "off by default, saved");
}

// The first missing part names the refusal; a full resolve refuses nothing.
static void tb_0267_dv_refusal() {
  DvResolve r = { 1, 1, 1, 1, 1, 1, 4 };
  CHECK(dv_refusal(r) == 0);
  DvResolve a = r; a.myHud = 0; a.removeFn = 0;
  CHECK(dv_refusal(a) && strstr(dv_refusal(a), "MyHUD"));
  DvResolve b = r; b.effectsOfStruct = 0;
  CHECK(dv_refusal(b) && strstr(dv_refusal(b), "array of a struct"));
  DvResolve c = r; c.effectNameParm = 0;
  CHECK(dv_refusal(c) && strstr(dv_refusal(c), "EffectName"));
  DvResolve d = r; d.parmsSize = DV_PARMS_MAX + 1;
  DvResolve e = r; e.parmsSize = 0;
  CHECK_MSG(dv_refusal(d) && dv_refusal(e), "a parms block we do not know is refused");
}

// autoexec.h: hostile without "alive" (a kneeling enemy has Health 0), executable as stunned in an execution
// control or frozen where allowed, within UseDistance plus both radii; the nearest eligible enemy wins.
static void tb_0270_aex_target() {
  CHECK_MSG(aex_hostile(2, 0, 0) && aex_hostile(7, 1, 0) && aex_hostile(7, 0, 1), "hostile, targeting or boss");
  CHECK_MSG(!aex_hostile(4, 0, 0) && !aex_hostile(6, 0, 0) && !aex_hostile(-1, 0, 0), "neutral, ally, none: no");
  CHECK_MSG(aex_kind(1, 1, 0, 0, 0, 0) == AEX_STUN, "ControlX, an execution control, Health 0: stun");
  CHECK_MSG(aex_kind(1, 0, 0, 0, 0, 0) == AEX_NONE, "a boss's finish-me control: not an execution");
  CHECK_MSG(aex_kind(1, 1, 1, 0, 0, 0) == AEX_NONE, "already controlled: no");
  CHECK_MSG(aex_kind(0, 1, 0, 0, 0, 1) == AEX_NONE, "out of ControlX: no");
  CHECK_MSG(aex_kind(0, 0, 0, 1, 1, 1) == AEX_FROZEN, "frozen and allowed: frozen");
  CHECK_MSG(aex_kind(0, 0, 0, 1, 0, 1) == AEX_NONE && aex_kind(0, 0, 0, 1, 1, 0) == AEX_NONE,
            "frozen but not allowed, or a frozen corpse: no");
  CHECK(aex_in_reach(100.f, 0.f, 0.f, 72.f, 20.f, 20.f, 39.f, 39.f));
  CHECK(aex_in_reach(112.f, 0.f, 0.f, 72.f, 20.f, 20.f, 39.f, 39.f));
  CHECK_MSG(!aex_in_reach(113.f, 0.f, 0.f, 72.f, 20.f, 20.f, 39.f, 39.f), "past UseDistance plus both radii");
  CHECK_MSG(!aex_in_reach(80.f, 80.f, 0.f, 72.f, 20.f, 20.f, 39.f, 39.f), "horizontal, not per axis");
  CHECK(aex_in_reach(0.f, 0.f, 150.f, 72.f, 20.f, 20.f, 39.f, 39.f));
  CHECK_MSG(!aex_in_reach(0.f, 0.f, 151.f, 72.f, 20.f, 20.f, 39.f, 39.f) &&
            !aex_in_reach(0.f, 0.f, -151.f, 72.f, 20.f, 20.f, 39.f, 39.f), "a floor above or below");
  const float nan = sqrtf(-1.f);
  CHECK_MSG(!aex_in_reach(nan, 0.f, 0.f, 72.f, 20.f, 20.f, 39.f, 39.f) &&
            !aex_in_reach(0.f, 0.f, 0.f, 0.f, 20.f, 20.f, 39.f, 39.f) &&
            !aex_in_reach(0.f, 0.f, 0.f, nan, 20.f, 20.f, 39.f, 39.f), "NaN or no UseDistance: never");
  int x[6];
  const AexCand c[6] = { { &x[0], 900.f, 1, AEX_STUN, 1 }, { &x[1], 100.f, 0, AEX_STUN, 1 },
                         { &x[2], 100.f, 1, AEX_NONE, 1 }, { &x[3], 400.f, 1, AEX_FROZEN, 1 },
                         { &x[4], 50.f, 1, AEX_STUN, 0 }, { &x[5], 400.f, 1, AEX_STUN, 1 } };
  CHECK_MSG(aex_pick(c, 6) == 3, "the nearest eligible, the first on a tie");
  CHECK(aex_pick(c, 1) == 0);
  CHECK(aex_pick(c + 1, 4) == 2);
  CHECK_MSG(aex_pick(c + 1, 1) == -1 && aex_pick(c, 0) == -1, "none eligible: -1");
  const AexGate go = { 1, 0, 1, 1, 0, 0, 0, 0, 1 };
  CHECK(aex_gate(go) == AEX_GO);
  AexGate g = go; g.on = 0; g.panelOpen = 1;
  CHECK_MSG(aex_gate(g) == AEX_G_OFF, "off first");
  g = go; g.panelOpen = 1; CHECK(aex_gate(g) == AEX_G_PANEL);
  g = go; g.inLevel = 0; CHECK(aex_gate(g) == AEX_G_LEVEL);
  g = go; g.playerAlive = 0; CHECK(aex_gate(g) == AEX_G_DEAD);
  g = go; g.grabbing = 1; CHECK(aex_gate(g) == AEX_G_GRAB);
  g = go; g.attached = 1; CHECK(aex_gate(g) == AEX_G_ATTACHED);
  g = go; g.viewMapper = 1; CHECK(aex_gate(g) == AEX_G_VIEWMAPPER);
  g = go; g.control = 1; CHECK(aex_gate(g) == AEX_G_CONTROL);
  g = go; g.canPressUse = 0; CHECK(aex_gate(g) == AEX_G_CANTUSE);
  CHECK(strcmp(aex_gate_name(AEX_G_PANEL), "panel open") == 0 && strcmp(aex_gate_name(99), "?") == 0);
}

// One execution at a time: the latched victim blocks every other until it leaves the walk, leaves ControlX or
// thaws, or the latch times out; a closed gate or a running retry wait calls nobody.
static void tb_0271_aex_one_at_a_time() {
  int a, b;
  const AexCand two[2] = { { &a, 400.f, 1, AEX_STUN, 1 }, { &b, 900.f, 1, AEX_STUN, 1 } };
  AexLatch l = { 0, 0, 0, 0 };
  CHECK(aex_latch_step(&l, 0, 0, 1000) == AEX_L_FREE);
  CHECK(aex_choose(&l, AEX_GO, two, 2, 1000) == 0);
  aex_latch_set(&l, &a, 1000);
  CHECK(aex_latch_step(&l, 1, 1, 1016) == AEX_L_HELD);
  CHECK_MSG(aex_choose(&l, AEX_GO, two, 2, 1016) == -1, "a second enemy in reach waits");
  CHECK(aex_latch_step(&l, 1, 1, 1000 + AEX_LATCH_MS - 1) == AEX_L_HELD);
  CHECK_MSG(aex_latch_step(&l, 0, 1, 1100) == AEX_L_GONE, "the victim left the walk");
  CHECK(aex_latch_step(&l, 0, 0, 1116) == AEX_L_FREE);
  CHECK_MSG(aex_choose(&l, AEX_GO, two + 1, 1, 1116) == 0, "then the other");
  aex_latch_set(&l, &a, 2000);
  CHECK_MSG(aex_latch_step(&l, 1, 0, 2016) == AEX_L_LEFT, "left ControlX or thawed");
  aex_latch_set(&l, &a, 3000);
  CHECK_MSG(aex_latch_step(&l, 1, 1, 3000 + AEX_LATCH_MS) == AEX_L_TIMEOUT, "a Used that did not take");
  CHECK_MSG(aex_choose(&l, AEX_G_ATTACHED, two, 2, 20000) == -1, "the gate closed (mid-animation): nobody");
  aex_wait(&l, 5000);
  CHECK_MSG(aex_choose(&l, AEX_GO, two, 2, 5000 + AEX_RETRY_MS - 1) == -1, "a refusal waits");
  CHECK(aex_choose(&l, AEX_GO, two, 2, 5000 + AEX_RETRY_MS) == 0);
  aex_wait(&l, 0xFFFFFF00u);
  CHECK_MSG(aex_choose(&l, AEX_GO, two, 2, 0xFFFFFF00u + 100u) == -1 &&
            aex_choose(&l, AEX_GO, two, 2, 0x10u) == 0, "across the tick count's wrap");
}

static void tb_0272_aex_config() {
  config_defaults();
  CHECK_MSG(g_cfg.autoExecute == 0 && config_key_known("autowin", "execute"), "off by default, saved");
}

// Silent aim's target rule (aim.h): hostile, in range, the off-screen switch, nearest the crosshair, visible.
// The freeze ray's beam reach (FreezeRay.BeamMaxLength) caps the aim range like a hitscan gun's MaxTraceDistance.
static void tb_0467_beam_reach_cap() {
  CHECK_MSG(aim_range_uu(64, 52.5f, AIMW_BEAM, 256.f) == 256.f, "beam: capped by its reach");
  CHECK_MSG(aim_range_uu(2, 52.5f, AIMW_BEAM, 256.f) == 105.f, "under the reach: the slider");
  CHECK_MSG(aim_range_uu(64, 52.5f, AIMW_BEAM, 0.f) == 3360.f, "an unread reach stays uncapped");
  CHECK_MSG(aim_range_uu(64, 52.5f, AIMW_HITSCAN, 3000.f) == 3000.f &&
            aim_range_uu(64, 52.5f, AIMW_PROJECTILE, 256.f) == 3360.f, "hitscan unchanged, projectiles uncapped");
  CHECK_MSG(aim_range_for(0, 64, 52.5f, AIMW_BEAM, 256.f) == 256.f, "on foot: the beam's reach");
  CHECK_MSG(tbot_crosshair_enemy(1, 1, 0, 256.f, AIMW_BEAM, 256.f) && !tbot_crosshair_enemy(1, 1, 0, 257.f, AIMW_BEAM, 256.f),
            "the triggerbot holds fire past the beam's reach");
  CHECK_MSG(tbot_crosshair_enemy(1, 1, 0, 9000.f, AIMW_BEAM, 0.f), "an unread reach: no cap");
}

// Enemies in a scripted animation are targets when damageable (Map16B: an MG42 gunner never locked).
static void tb_0468_spawnbox_cutscene_target() {
  CHECK_MSG(spawnbox_exclusion(0, 1, 0) == SB_EXCL_NONE, "a cut-scene alone never excludes");
  CHECK_MSG(spawnbox_exclusion(1, 0, 0, 0, 0, 0, 0, 0, 1) == SB_EXCL_NONE &&
            spawnbox_exclusion(1, 1, 0, 0, 0, 0, 0, 0, 1) == SB_EXCL_NONE, "far with a body box: aimed at the box");
  CHECK_MSG(spawnbox_exclusion(1, 0, 0) == SB_EXCL_FAR && spawnbox_exclusion(1, 1, 0, 0, 0, 0, 0, 0, 0) == SB_EXCL_FAR,
            "far without a body box: left out");
  CHECK_MSG(spawnbox_exclusion(0, 1, 1, 0, 0, 0, 0, 0, 1) == SB_EXCL_TELEPORT &&
            spawnbox_exclusion(0, 1, 0, 1, 0, 0, 0, 0, 1) == SB_EXCL_NODAMAGE &&
            spawnbox_exclusion(1, 1, 0, 0, 1, 0, 0, 0, 1) == SB_EXCL_PLAYERDMG0 &&
            spawnbox_exclusion(1, 1, 0, 0, 0, 1, 0, 0, 1) == SB_EXCL_NEUTRAL &&
            spawnbox_exclusion(0, 1, 0, 0, 0, 0, 0, 1, 1) == SB_EXCL_WRONGTYPE &&
            spawnbox_exclusion(1, 1, 0, 0, 0, 0, 1, 0, 1) == SB_EXCL_DOOMED, "the exact rules still exclude");
  const float box[3] = { 10.f, 20.f, 30.f }, body[3] = { 1.f, 2.f, 3.f };
  float out[3] = { 0.f, 0.f, 0.f };
  CHECK_MSG(aim_pawn_point(1, box, 1, body, out) == AIM_SRC_BOX && out[0] == 10.f && out[2] == 30.f,
            "body away from Location: the render-box centre");
  CHECK_MSG(aim_pawn_point(1, box, 0, body, out) == AIM_SRC_BOX && out[1] == 20.f, "the box needs no body point");
  CHECK_MSG(aim_pawn_point(0, box, 1, body, out) == AIM_SRC_BODY && out[0] == 1.f && out[2] == 3.f,
            "near: the body point (head, chest, ...)");
  CHECK_MSG(aim_pawn_point(0, box, 0, body, out) == AIM_SRC_NONE, "neither: no point");
}

// Crosshair's projected fallback places a far pawn at its render box, as the aim and the ESP do;
// a pawn's cached visibility is dropped when its aim point source changes (flagged or back near).
static void tb_0469_tbot_box_and_vis_drop() {
  const float c[3] = { 100.f, 200.f, 300.f }, half[3] = { 20.f, 30.f, 40.f };
  TbCyl k;
  CHECK_MSG(tbot_pawn_cyl(1, c, half, 1.f, 2.f, 3.f, 17.f, 39.f, &k) == 1 && k.x == 100.f && k.y == 200.f &&
            k.z == 300.f && k.r == 30.f && k.h == 40.f, "far with a box: its centre, the larger half width, its half height");
  CHECK_MSG(tbot_pawn_cyl(0, c, half, 1.f, 2.f, 3.f, 17.f, 39.f, &k) == 0 && k.x == 1.f && k.z == 3.f && k.r == 17.f &&
            k.h == 39.f, "no box: Location and the collision cylinder");
  static AimVisCache vc;
  memset(&vc, 0, sizeof vc);
  int a[3];
  aim_vis_put(&vc, &a[0], 1000, AIM_VIS_YES, 0, AIM_PT_HEAD);
  aim_vis_put(&vc, &a[1], 1000, AIM_VIS_NO, 0);
  aim_vis_put(&vc, &a[2], 1000, AIM_VIS_YES, 0);
  aim_vis_drop(&vc, &a[0]);
  CHECK_MSG(aim_vis_get(&vc, &a[0], 1010, 0) == AIM_VIS_UNKNOWN && aim_vis_get(&vc, &a[1], 1010, 0) == AIM_VIS_NO &&
            aim_vis_get(&vc, &a[2], 1010, 0) == AIM_VIS_YES && vc.n == 2, "dropped: unknown, the others kept");
  aim_vis_drop(&vc, &a[0]);
  aim_vis_drop(&vc, 0);
  CHECK_MSG(vc.n == 2, "dropping an absent entry or null changes nothing");
}

static void tb_0280_aim_target_rule() {
  const float nan = sqrtf(-1.f);
  CHECK_MSG(aim_eligible(1, 0, 100.f, 3360.f, 1, 0), "an enemy on screen in range");
  CHECK_MSG(!aim_eligible(0, 1, 100.f, 3360.f, 1, 1), "an ally: never");
  CHECK_MSG(!aim_eligible(1, 1, 100.f, 3360.f, 1, 1), "flagged both: never");
  CHECK_MSG(!aim_eligible(1, 0, 3361.f, 3360.f, 1, 1) && aim_eligible(1, 0, 3360.f, 3360.f, 1, 1), "range edge");
  CHECK_MSG(!aim_eligible(1, 0, nan, 3360.f, 1, 1), "NaN distance: never");
  CHECK_MSG(!aim_eligible(1, 0, 100.f, 3360.f, 0, 0) && aim_eligible(1, 0, 100.f, 3360.f, 0, 1),
            "off screen only with the switch on");
  CHECK_MSG(aim_range_uu(64, 52.5f, AIMW_HITSCAN, 3000.f) == 3000.f, "hitscan: capped by MaxTraceDistance");
  CHECK_MSG(aim_range_uu(10, 52.5f, AIMW_HITSCAN, 3000.f) == 525.f, "under the trace: the slider");
  CHECK_MSG(aim_range_uu(64, 52.5f, AIMW_PROJECTILE, 3000.f) == 3360.f, "projectiles: no trace cap");
  CHECK_MSG(aim_range_uu(64, 52.5f, AIMW_HITSCAN, 0.f) == 3360.f, "an unread trace distance caps nothing");
  CHECK_MSG(aim_range_for(0, 10, 52.5f, AIMW_HITSCAN, 3000.f) == 525.f, "on foot: the slider");
  CHECK_MSG(aim_range_for(0, 64, 52.5f, AIMW_HITSCAN, 3000.f) == 3000.f, "on foot: capped by the trace");
  CHECK_MSG(aim_range_for(1, 10, 52.5f, AIMW_HITSCAN, 50000.f) == 50000.f, "mounted hitscan: its own reach");
  CHECK_MSG(aim_range_for(1, 10, 52.5f, AIMW_PROJECTILE, 50000.f) == AIM_RANGE_MOUNTED_CAP_UU, "mounted projectile: the cap");
  CHECK_MSG(aim_range_for(1, 10, 52.5f, AIMW_HITSCAN, 0.f) == AIM_RANGE_MOUNTED_CAP_UU, "mounted, unread reach: the cap");
  float f[3];
  aim_forward(0, 16384, f);
  CHECK_MSG(fabsf(f[0]) < 1e-4f && fabsf(f[1] - 1.f) < 1e-4f && fabsf(f[2]) < 1e-4f, "yaw 16384 faces +Y");
  aim_forward(16384, 0, f);
  CHECK_MSG(fabsf(f[2] - 1.f) < 1e-4f, "pitch 16384 faces up");
  const float fw[3] = { 1.f, 0.f, 0.f };
  CHECK(fabsf(aim_angle(fw, 5.f, 0.f, 0.f)) < 1e-3f);
  CHECK(fabsf(aim_angle(fw, 0.f, 3.f, 0.f) - 90.f) < 1e-3f);
  CHECK(fabsf(aim_angle(fw, -2.f, 0.f, 0.f) - 180.f) < 1e-3f);
  CHECK_MSG(aim_angle(fw, 0.f, 0.f, 0.f) == 180.f, "a zero vector sorts last");
  CHECK(aim_on_screen(1, 0.f, 0.f, 800, 600) && aim_on_screen(1, 800.f, 600.f, 800, 600));
  CHECK_MSG(!aim_on_screen(0, 10.f, 10.f, 800, 600) && !aim_on_screen(1, -1.f, 10.f, 800, 600) &&
            !aim_on_screen(1, 10.f, 601.f, 800, 600) && !aim_on_screen(1, nan, 10.f, 800, 600), "off screen");
  // Nearest the crosshair first; equal angles by walk order.
  AimCand c[4] = { { 3, 40.f, AIM_VIS_YES }, { 1, 10.f, AIM_VIS_NO }, { 2, 10.f, AIM_VIS_YES },
                   { 0, 25.f, AIM_VIS_UNKNOWN } };
  aim_cand_sort(c, 4);
  CHECK_MSG(c[0].idx == 1 && c[1].idx == 2 && c[2].idx == 0 && c[3].idx == 3, "by angle, then index");
  CHECK_MSG(aim_pick(c, 4, -1) == 1, "the first visible in order");
  c[1].vis = AIM_VIS_NO;
  CHECK_MSG(aim_pick(c, 4, -1) == -1, "an unknown before any visible: no pick yet");
  CHECK_MSG(aim_pick(c, 4, 3) == 3, "the previous target, still visible, is kept meanwhile");
  CHECK_MSG(aim_pick(c, 4, 1) == -1, "a previous target that is not visible is not kept");
  c[2].vis = AIM_VIS_NO;
  CHECK_MSG(aim_pick(c, 4, -1) == 3 && aim_pick(c, 0, -1) == -1, "past the hidden ones");
  CHECK_MSG(aim_skip_frozen(1, 1) && !aim_skip_frozen(1, 0) && !aim_skip_frozen(0, 1),
            "a frozen enemy is Auto-execute's while it is on");
  // An ally standing in the line of fire blocks the candidate.
  const float o[3] = { 0.f, 0.f, 0.f }, t[3] = { 100.f, 0.f, 0.f };
  CHECK_MSG(aim_seg_cyl_hit(o, t, 50.f, 0.f, 0.f, 10.f, 30.f), "an ally between");
  CHECK_MSG(!aim_seg_cyl_hit(o, t, 50.f, 25.f, 0.f, 10.f, 30.f), "an ally beside the line");
  CHECK_MSG(!aim_seg_cyl_hit(o, t, 150.f, 0.f, 0.f, 10.f, 30.f), "an ally behind the target");
  CHECK_MSG(!aim_seg_cyl_hit(o, t, 50.f, 0.f, 50.f, 10.f, 30.f), "an ally on a floor above");
  CHECK_MSG(!aim_seg_cyl_hit(o, t, 50.f, 0.f, 0.f, 0.f, 0.f), "a pawn without a size blocks nothing");
  // The visibility cache: 100 ms, the oldest replaced when full.
  static AimVisCache vc;
  memset(&vc, 0, sizeof vc);
  int objs[AIM_VIS_SLOTS + 1];
  CHECK(aim_vis_get(&vc, &objs[0], 1000, 0) == AIM_VIS_UNKNOWN);
  aim_vis_put(&vc, &objs[0], 1000, AIM_VIS_YES, 1);
  int frozen = 0;
  CHECK(aim_vis_get(&vc, &objs[0], 1000 + AIM_VIS_MS - 1, &frozen) == AIM_VIS_YES && frozen == 1);
  CHECK_MSG(aim_vis_get(&vc, &objs[0], 1000 + AIM_VIS_MS, 0) == AIM_VIS_UNKNOWN, "stale after 100 ms");
  aim_vis_put(&vc, &objs[0], 1100, AIM_VIS_NO, 0);
  CHECK_MSG(aim_vis_get(&vc, &objs[0], 1150, 0) == AIM_VIS_NO && vc.n == 1, "refreshed in place");
  for (int i = 1; i < AIM_VIS_SLOTS; i++) aim_vis_put(&vc, &objs[i], 1200 + i, AIM_VIS_YES, 0);
  aim_vis_put(&vc, &objs[AIM_VIS_SLOTS], 1290, AIM_VIS_YES, 0);
  CHECK_MSG(vc.n == AIM_VIS_SLOTS && aim_vis_get(&vc, &objs[0], 1290, 0) == AIM_VIS_UNKNOWN &&
            aim_vis_get(&vc, &objs[AIM_VIS_SLOTS], 1290, 0) == AIM_VIS_YES, "full: the oldest goes");
  CHECK(aim_vis_get(&vc, 0, 1290, 0) == AIM_VIS_UNKNOWN);
  // The head bone (aim.md section 2): Head, else EyeBone, else LockOnBone, whose part is Head (1).
  CHECK(aim_head_choice(1, 1, 1) == AIM_HB_HEAD);
  CHECK(aim_head_choice(0, 1, 1) == AIM_HB_EYE);
  CHECK(aim_head_choice(0, 19, 1) == AIM_HB_LOCK);
  CHECK_MSG(aim_head_choice(0, 0, 0) == AIM_HB_NONE && aim_head_choice(-1, -1, -1) == AIM_HB_NONE,
            "Octababy, Octabrain: the body");
  CHECK_MSG(aim_head_plausible(10.f, 0.f, 40.f, 0.f, 0.f, 0.f, 20.f, 50.f), "a head inside its cylinder");
  CHECK_MSG(!aim_head_plausible(0.f, 0.f, 0.f, 3000.f, 400.f, 20.f, 20.f, 50.f), "a missing bone's (0,0,0)");
  CHECK_MSG(!aim_head_plausible(nan, 0.f, 40.f, 0.f, 0.f, 0.f, 20.f, 50.f), "NaN");
}

// The CurrentShake that turns GetWeaponAim's ViewRotation + ScaledShake onto the target.
static void tb_0281_aim_shake_delta() {
  CHECK(aim_wrap16(70000) == 4464 && aim_wrap16(-40000) == 25536 && aim_wrap16(32768) == -32768 &&
        aim_wrap16(-32768) == -32768 && aim_wrap16(100) == 100);
  int p = 9, y = 9;
  aim_rotator_to(5.f, 0.f, 0.f, &p, &y);
  CHECK(p == 0 && y == 0);
  aim_rotator_to(0.f, 5.f, 0.f, &p, &y);
  CHECK(p == 0 && y == 16384);
  aim_rotator_to(0.f, -5.f, 0.f, &p, &y);
  CHECK(y == -16384);
  aim_rotator_to(5.f, 0.f, 5.f, &p, &y);
  CHECK(p == 8192 && y == 0);
  int s[3] = { 7, 7, 7 };
  CHECK_MSG(aim_shake_for(0, 65000, 0, 500, 90.f, 90.f, s) && s[0] == 0 && s[1] == 1036 && s[2] == 0,
            "the short way across the yaw wrap");
  CHECK_MSG(aim_shake_for(60000, 0, -1000, 0, 90.f, 90.f, s) && s[0] == 4536, "pitch stored below zero");
  CHECK_MSG(aim_shake_for(0, 0, 0, 1000, 45.f, 90.f, s) && s[1] == 2000, "zoomed: divided by the FOV ratio");
  CHECK_MSG(aim_shake_for(0, 0, 0, 1000, 100.f, 90.f, s) && s[1] == 900, "a wider FOV than the default");
  CHECK_MSG(!aim_shake_for(0, 0, 0, 1000, 45.f, 0.f, s) && !aim_shake_for(0, 0, 0, 1000, 0.f, 90.f, s) &&
            !aim_shake_for(0, 0, 0, 1000, sqrtf(-1.f), 90.f, s), "an unusable FOV writes nothing");
  // GetWeaponAim's own arithmetic: ViewRotation + (int)(shake * ratio) lands within a unit of the target.
  const float fovs[4] = { 90.f, 45.f, 20.f, 110.f };
  int worst = 0;
  for (int i = 0; i < 64; i++) {
    const int vrP = (i * 7919) & 0xFFFF, vrY = (i * 104729) & 0xFFFF;
    const int wP = (i * 1237) % 16000 - 8000, wY = (i * 50021) & 0xFFFF;
    const float cur = fovs[i & 3], def = 90.f;
    int sh[3];
    if (!aim_shake_for(vrP, vrY, wP, wY, cur, def, sh)) { worst = 99; break; }
    const float k = cur != def ? cur / def : 1.f;
    const int gotP = vrP + (int)((float)sh[0] * k), gotY = vrY + (int)((float)sh[1] * k);
    const int eP = abs(aim_wrap16(gotP - wP)), eY = abs(aim_wrap16(gotY - wY));
    if (eP > worst) worst = eP;
    if (eY > worst) worst = eY;
  }
  CHECK_MSG(worst <= 1, "64 views and zooms: on target within one unit");
}

// The bracket: saved at UpdateWeaponTraceVars, put back exactly once at RecordShot, whatever the target did.
static void tb_0282_aim_bracket() {
  AimBracket b;
  memset(&b, 0, sizeof b);
  int out[3] = { 0, 0, 0 };
  CHECK_MSG(!aim_bracket_close(&b, out), "a RecordShot with nothing open writes nothing");
  const int cur[3] = { 10, -20, 3 }, wr[3] = { 400, 900, 0 };
  aim_bracket_open(&b, cur, wr);
  CHECK(b.open && b.opened == 1);
  // A shotgun's ten pellets read the one shake in between: nothing to step.
  CHECK_MSG(aim_bracket_close(&b, out) && out[0] == 10 && out[1] == -20 && out[2] == 3, "restored");
  CHECK_MSG(!aim_bracket_close(&b, out) && b.closed == 1, "exactly once");
  // Refused ammo skips RecordShot: the next UpdateWeaponTraceVars of the same tick still sees our write.
  aim_bracket_open(&b, cur, wr);
  aim_bracket_open(&b, wr, wr);
  CHECK_MSG(b.reopened == 1 && aim_bracket_close(&b, out) && out[1] == -20, "the game's value is kept");
  // A later tick: UpdateViewShake rebuilt the shake, so the new value is the one to keep.
  const int rebuilt[3] = { 1, 2, 0 };
  aim_bracket_open(&b, cur, wr);
  aim_bracket_open(&b, rebuilt, wr);
  CHECK_MSG(aim_bracket_close(&b, out) && out[0] == 1 && out[1] == 2, "rebuilt: the new value");
  // Frame end: still open means no RecordShot came.
  CHECK_MSG(!aim_bracket_frame_end(&b, cur, out), "nothing open");
  aim_bracket_open(&b, cur, wr);
  CHECK_MSG(aim_bracket_frame_end(&b, wr, out) && out[1] == -20 && !b.open, "our write still there: restored");
  aim_bracket_open(&b, cur, wr);
  CHECK_MSG(!aim_bracket_frame_end(&b, rebuilt, out) && !b.open && b.leftOpen == 2, "rebuilt: forgotten");
}

// The lead: projectile weapons only, scaled by the amount.
static void tb_0283_aim_lead() {
  const float p[3] = { 1000.f, 0.f, 0.f }, v[3] = { 0.f, 300.f, 0.f };
  float o[3];
  const float o0[3] = { 0.f, 0.f, 0.f };
  const AimProjModel hs = { AIM_PM_NONE, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0 };
  const AimIntercept r = aim_intercept(hs, o0, p, v);
  CHECK_MSG(r.ok && r.t == 0.f && r.dir[0] == 1.f && r.dir[1] == 0.f, "hitscan never leads");
  (void)o;
  CHECK(aim_weapon_kind(L"PistolWeaponConfig") == AIMW_HITSCAN && aim_weapon_kind(L"ShotgunWeaponConfig") == AIMW_HITSCAN &&
        aim_weapon_kind(L"MachineGunWeaponConfig") == AIMW_HITSCAN &&
        aim_weapon_kind(L"RailgunWeaponConfig") == AIMW_HITSCAN && aim_weapon_kind(L"MiniGun_BLWeaponConfig") == AIMW_HITSCAN);
  CHECK(aim_weapon_kind(L"rpgweaponconfig") == AIMW_PROJECTILE &&
        aim_weapon_kind(L"DevastatorWeaponConfig") == AIMW_PROJECTILE &&
        aim_weapon_kind(L"ShrinkRayWeaponConfig") == AIMW_PROJECTILE &&
        aim_weapon_kind(L"EnforcerGunWeaponConfig") == AIMW_PROJECTILE &&
        aim_weapon_kind(L"ATLaserWeaponConfig") == AIMW_PROJECTILE);
  CHECK_MSG(aim_weapon_kind(L"PipeBombWeaponConfig") == AIMW_NONE && aim_weapon_kind(L"TripMineWeaponConfig") == AIMW_NONE &&
            aim_weapon_kind(L"WeaponConfig") == AIMW_NONE &&
            aim_weapon_kind(0) == AIMW_NONE, "thrown and the rest: not redirected");
  const wchar_t* dual[3] = { L"DualPistolWeaponConfig", L"PistolWeaponConfig", L"WeaponConfig" };
  const wchar_t* mp[2] = { L"MP_RPGWeaponConfig", L"MP_WeaponConfig" };
  CHECK_MSG(aim_weapon_kind_chain(dual, 3) == AIMW_HITSCAN && aim_weapon_kind_chain(mp, 2) == AIMW_NONE &&
            aim_weapon_kind_chain(dual, 0) == AIMW_NONE, "a subclass by its supers");
}

// Nothing is redirected while a gate is closed; the shot also needs ammo it will spend.
static void tb_0284_aim_gates() {
  const AimGate go = { 1, 0, 1, 1, 0, 0, 0, 0, AIMW_HITSCAN };
  CHECK(aim_gate(go) == AIM_GO);
  AimGate g = go; g.on = 0; g.panelOpen = 1;
  CHECK_MSG(aim_gate(g) == AIM_G_OFF, "off first");
  g = go; g.panelOpen = 1; CHECK(aim_gate(g) == AIM_G_PANEL);
  g = go; g.inLevel = 0; CHECK(aim_gate(g) == AIM_G_LEVEL);
  g = go; g.playerAlive = 0; CHECK(aim_gate(g) == AIM_G_DEAD);
  g = go; g.grabbing = 1; CHECK(aim_gate(g) == AIM_G_GRAB);
  g = go; g.attached = 1; CHECK_MSG(aim_gate(g) == AIM_G_ATTACHED, "an execution, a turret, a control");
  g = go; g.viewMapper = 1; CHECK_MSG(aim_gate(g) == AIM_G_VIEWMAPPER, "a vehicle or a cutscene camera");
  g = go; g.control = 1; CHECK_MSG(aim_gate(g) == AIM_G_CONTROL, "a QTE or a minigame");
  g = go; g.weaponKind = AIMW_NONE; CHECK_MSG(aim_gate(g) == AIM_G_WEAPON, "a weapon that is not redirected");
  g = go; g.weaponKind = AIMW_PROJECTILE; CHECK(aim_gate(g) == AIM_GO);
  CHECK(strcmp(aim_gate_name(AIM_G_PANEL), "panel open") == 0 && strcmp(aim_gate_name(99), "?") == 0);
  CHECK_MSG(aim_ammo_ok(1, 0, 0.f, 1) && aim_ammo_ok(0, 1, 1.f, 1) && aim_ammo_ok(0, 1, 30.f, 1), "infinite, or a round");
  CHECK_MSG(!aim_ammo_ok(0, 1, 0.5f, 1) && !aim_ammo_ok(0, 0, 50.f, 1) && !aim_ammo_ok(0, 1, sqrtf(-1.f), 1),
            "empty or unread: no write, so no bracket can leak");
  CHECK(aim_marker_on(1, 1) && !aim_marker_on(0, 1) && !aim_marker_on(1, 0));
}

static void tb_0285_aim_config() {
  config_defaults();
  CHECK_MSG(g_cfg.silentOn == 0 && g_cfg.aimMarker == 0, "the switch and the marker off by default");
  // The sub-rows follow DNMP (same as Manhattan Project): Lead on; the range at its maximum.
  CHECK_MSG(g_cfg.aimPoint == AIM_AT_BODY && g_cfg.aimRangeM == AIM_RANGE_MAX_M && g_cfg.aimOffscreen == 1 &&
            g_cfg.aimLead == 1, "DNMP's values");
  // 160 m covers the railgun's 8192-unit trace at 52.5 units a metre.
  CHECK(AIM_RANGE_MIN_M == 1 && AIM_RANGE_MAX_M == 160);
  CHECK(aim_range_uu(AIM_RANGE_MAX_M, 52.5f, AIMW_HITSCAN, 8192.f) == 8192.f);
  CHECK(config_key_known("aim", "silent") && config_key_known("aim", "point") && config_key_known("aim", "range") &&
        config_key_known("aim", "offscreen") && config_key_known("aim", "lead") &&
        config_key_known("aim", "marker"));
  CHECK(strcmp(aim_point_label(AIM_AT_BODY), "Body") == 0 && strcmp(aim_point_label(AIM_AT_HEAD), "Head") == 0);
}

// The triggerbot's condition: Crosshair (the game's look trace on a hostile pawn) or Locked, held for the delay.
static void tb_0286_tbot_condition() {
  CHECK(tbot_when_holds(TBOT_WHEN_CROSSHAIR, 1, 0) && !tbot_when_holds(TBOT_WHEN_CROSSHAIR, 0, 1));
  CHECK(tbot_when_holds(TBOT_WHEN_LOCKED, 0, 1) && !tbot_when_holds(TBOT_WHEN_LOCKED, 1, 0));
  CHECK_MSG(tbot_crosshair_enemy(1, 1, 0, 500.f, AIMW_HITSCAN, 3000.f), "an enemy within the gun's trace");
  CHECK_MSG(!tbot_crosshair_enemy(1, 1, 0, 3001.f, AIMW_HITSCAN, 3000.f), "past MaxTraceDistance: the shot ends short");
  CHECK_MSG(tbot_crosshair_enemy(1, 1, 0, 9000.f, AIMW_PROJECTILE, 0.f) &&
            tbot_crosshair_enemy(1, 1, 0, 9000.f, AIMW_HITSCAN, 0.f), "a projectile, or an unread trace: no cap");
  CHECK_MSG(!tbot_crosshair_enemy(0, 1, 0, 100.f, AIMW_HITSCAN, 3000.f), "no pawn under the crosshair (a wall)");
  CHECK_MSG(!tbot_crosshair_enemy(1, 0, 0, 100.f, AIMW_HITSCAN, 3000.f), "a neutral");
  CHECK_MSG(!tbot_crosshair_enemy(1, 1, 0, sqrtf(-1.f), AIMW_HITSCAN, 3000.f), "NaN distance");
  CHECK(tbot_centre_in(390, 290, 20, 20, 400, 300) && !tbot_centre_in(401, 290, 20, 20, 400, 300) &&
        !tbot_centre_in(390, 290, 0, 20, 400, 300));
  int x, y;
  TbotState s = { 0, 0, 0 };
  CHECK_MSG(tbot_step(&s, 1, &x, 16, 0), "no delay: at once");
  s = TbotState();
  CHECK(!tbot_step(&s, 1, &x, 16, 100));
  for (int i = 0; i < 6; i++) tbot_step(&s, 1, &x, 16, 100);
  CHECK_MSG(!tbot_step(&s, 1, &x, 3, 100) && tbot_step(&s, 1, &x, 1, 100), "100 ms held: fires");
  CHECK_MSG(!tbot_step(&s, 1, &y, 16, 100), "a new target starts again");
  tbot_step(&s, 1, &y, 16, 100);
  CHECK_MSG(!tbot_step(&s, 0, &y, 16, 100) && s.heldMs == 0 && !tbot_step(&s, 1, &y, 16, 100), "a break resets");
  s = TbotState();
  tbot_step(&s, 1, &x, 16, 500);
  CHECK_MSG(!tbot_step(&s, 1, &x, 5000, 500) && s.heldMs == TBOT_FRAME_MS_MAX, "a long gap is a pause, not a hold");
  CHECK(tbot_taps(L"RPGWeaponConfig") && tbot_taps(L"railgunweaponconfig") && !tbot_taps(L"PistolWeaponConfig") &&
        !tbot_taps(0));
}

// FireDown and FireUp in pairs: every press ends in one FireUp, or is forgotten when someone else released it.
static void tb_0287_tbot_press_pairing() {
  TbotPress p;
  memset(&p, 0, sizeof p);
  CHECK(tbot_act(&p, 1, 0, 0, 0, 0, 1000) == TB_NONE);
  CHECK_MSG(tbot_act(&p, 1, 1, 0, 0, 0, 1000) == TB_DOWN && p.ours, "pressed");
  CHECK_MSG(tbot_act(&p, 1, 1, 0, 0, 1, 1016) == TB_NONE, "held: the gun refires itself");
  CHECK_MSG(tbot_act(&p, 1, 0, 0, 0, 1, 1032) == TB_UP && !p.ours, "the condition ends: released");
  // The gate closes while held (the panel opens, a QTE starts): FireUp at once, the user's button or not.
  tbot_act(&p, 1, 1, 0, 0, 0, 2000);
  CHECK_MSG(tbot_act(&p, 0, 1, 0, 1, 1, 2016) == TB_UP && !p.ours, "a closing gate releases at once");
  CHECK_MSG(tbot_act(&p, 0, 1, 0, 0, 0, 2032) == TB_NONE, "and presses nothing while closed");
  // The user's own fire.
  CHECK_MSG(tbot_act(&p, 1, 1, 0, 1, 0, 3000) == TB_NONE, "the user's button down: no press");
  CHECK_MSG(tbot_act(&p, 1, 1, 0, 0, 1, 3016) == TB_NONE, "the game already firing: not ours to press");
  tbot_act(&p, 1, 1, 0, 0, 0, 3032);
  CHECK_MSG(tbot_act(&p, 1, 0, 0, 1, 1, 3048) == TB_FORGET && !p.ours, "never FireUp under the user's own fire");
  tbot_act(&p, 1, 1, 0, 0, 0, 3064);
  CHECK_MSG(tbot_act(&p, 1, 1, 0, 0, 0, 3080) == TB_FORGET && !p.ours, "released by the game: forgotten");
  CHECK_MSG(tbot_act(&p, 1, 1, 0, 0, 0, 3096) == TB_DOWN, "then pressed again");
  tbot_act(&p, 0, 0, 0, 0, 1, 3112);
  // RPG and railgun: one press a shot, the release on the next frame, a gap before the next.
  CHECK(tbot_act(&p, 1, 1, 1, 0, 0, 4000) == TB_DOWN);
  CHECK_MSG(tbot_act(&p, 1, 1, 1, 0, 1, 4016) == TB_UP, "a tap ends the next frame");
  CHECK_MSG(tbot_act(&p, 1, 1, 1, 0, 0, 4016 + TBOT_TAP_GAP_MS - 1) == TB_NONE, "the gap");
  CHECK(tbot_act(&p, 1, 1, 1, 0, 0, 4016 + TBOT_TAP_GAP_MS) == TB_DOWN);
  tbot_act(&p, 0, 0, 0, 0, 1, 5000);
  CHECK_MSG(p.downs == p.ups + p.forgets && !p.ours, "every press accounted for");
  // A long run of random inputs never leaves a press behind once the gate closes.
  unsigned r = 12345;
  for (int i = 0; i < 64; i++) {
    r = r * 1103515245u + 12345u;
    tbot_act(&p, (r >> 16) & 1, (r >> 17) & 1, (r >> 18) & 1, (r >> 19) & 1, (r >> 20) & 1, 6000u + 16u * i);
  }
  tbot_act(&p, 0, 0, 0, 0, 1, 7100);
  CHECK_MSG(!p.ours && p.downs == p.ups + p.forgets, "64 random frames: paired");
}

// An ally under the crosshair never fires, whatever the delay.
static void tb_0288_tbot_ally() {
  CHECK_MSG(!tbot_crosshair_enemy(1, 0, 1, 100.f, AIMW_HITSCAN, 3000.f), "an ally");
  CHECK_MSG(!tbot_crosshair_enemy(1, 1, 1, 100.f, AIMW_HITSCAN, 3000.f), "flagged both: never");
  TbotState s = { 0, 0, 0 };
  TbotPress p;
  memset(&p, 0, sizeof p);
  int ally;
  for (int i = 0; i < 64; i++) {
    const int holds = tbot_when_holds(TBOT_WHEN_CROSSHAIR, tbot_crosshair_enemy(1, 0, 1, 100.f, AIMW_HITSCAN, 3000.f), 0);
    const int fire = tbot_step(&s, holds, &ally, 16, 0);
    CHECK_MSG(tbot_act(&p, 1, fire, 0, 0, 0, 1000u + 16u * i) == TB_NONE, "no press on an ally");
  }
}

static void tb_0289_tbot_config() {
  config_defaults();
  CHECK_MSG(g_cfg.tbotOn == 0 && g_cfg.tbotWhen == TBOT_WHEN_CROSSHAIR && g_cfg.tbotDelay == TBOT_DELAY_MS_DEF &&
            TBOT_DELAY_MS_DEF == 0, "off by default; DNMP's mode and delay");
  CHECK(TBOT_DELAY_MS_MIN == 0 && TBOT_DELAY_MS_MAX == 500);
  CHECK(config_key_known("aim", "triggerbot") && config_key_known("aim", "triggerwhen") &&
        config_key_known("aim", "triggerdelay"));
  CHECK(strcmp(tbot_when_label(TBOT_WHEN_CROSSHAIR), "Crosshair") == 0 &&
        strcmp(tbot_when_label(TBOT_WHEN_LOCKED), "Locked") == 0);
}

// Locked fires only on a shot the bracket redirects; otherwise the Crosshair checks decide.
static void tb_0290_tbot_locked_cover() {
  CHECK_MSG(aim_weapon_kind(L"FreezeRayWeaponConfig") == AIMW_BEAM, "the freeze ray's beam has its own trace");
  const AimGate g = { 1, 0, 1, 1, 0, 0, 0, 0, AIMW_BEAM };
  CHECK_MSG(aim_gate(g) == AIM_GO, "silent aim still brackets its classic-mode ice blast");
  const float p[3] = { 0.f, 0.f, 0.f }, v[3] = { 0.f, 300.f, 0.f };
  float o[3];
  (void)p; (void)v; (void)o;
  CHECK(tbot_locked_covered(AIMW_HITSCAN, 1) && tbot_locked_covered(AIMW_PROJECTILE, 1));
  CHECK_MSG(!tbot_locked_covered(AIMW_BEAM, 1) && !tbot_locked_covered(AIMW_NONE, 1), "not bracketed");
  CHECK_MSG(!tbot_locked_covered(AIMW_HITSCAN, 0), "the redirect's pre-checks failed this frame");
  CHECK(tbot_holds(TBOT_WHEN_LOCKED, 0, 0, 1, 1));
  CHECK_MSG(!tbot_holds(TBOT_WHEN_LOCKED, 0, 1, 1, 1), "Locked: never with an ally under the crosshair");
  CHECK_MSG(!tbot_holds(TBOT_WHEN_LOCKED, 0, 0, 1, 0), "the beam held: a wall under the crosshair, no fire");
  CHECK_MSG(tbot_holds(TBOT_WHEN_LOCKED, 1, 0, 1, 0) && tbot_holds(TBOT_WHEN_LOCKED, 1, 0, 0, 0),
            "not covered: an enemy under the crosshair, as Crosshair mode");
  CHECK_MSG(!tbot_holds(TBOT_WHEN_LOCKED, 1, 1, 1, 0), "flagged both: never");
  CHECK_MSG(!tbot_holds(TBOT_WHEN_LOCKED, 0, 0, 0, 1), "covered but nothing locked");
  CHECK(tbot_holds(TBOT_WHEN_CROSSHAIR, 1, 0, 0, 0) && !tbot_holds(TBOT_WHEN_CROSSHAIR, 0, 0, 1, 1));
  // ExpendAmmo spends WeaponConfig.AmmoExpenditure, not always 1.
  CHECK_MSG(!aim_ammo_ok(0, 1, 2.f, 3) && aim_ammo_ok(0, 1, 3.f, 3), "the charge must cover the amount");
  CHECK_MSG(aim_ammo_ok(0, 1, 1.f, -1) && !aim_ammo_ok(0, 1, 0.5f, -1), "an unread amount counts as 1");
  CHECK(aim_ammo_ok(0, 1, 0.f, 0) && aim_ammo_ok(1, 0, 0.f, 5));
}

// The user's fire is the button the engine's own DInput read received; and a press forgotten under the
// user's button still ends in FireUp once the button is up while the game is still firing.
static void tb_0291_tbot_release_backstop() {
  unsigned char st[0x14];
  memset(st, 0, sizeof st);
  CHECK(!dm_button0(st, sizeof st));
  st[12] = 0x80;
  CHECK(dm_button0(st, sizeof st) && dm_button0(st, 0x10));
  CHECK_MSG(!dm_button0(st, 12) && !dm_button0(0, 0x14), "a short or missing buffer");
  TbotPress p;
  memset(&p, 0, sizeof p);
  CHECK(tbot_act(&p, 1, 1, 0, 0, 0, 1000) == TB_DOWN);
  CHECK(tbot_act(&p, 1, 0, 0, 1, 1, 1016) == TB_FORGET);
  CHECK_MSG(tbot_act(&p, 1, 0, 0, 1, 1, 1032) == TB_NONE, "still the user's");
  CHECK_MSG(tbot_act(&p, 1, 0, 0, 0, 1, 1048) == TB_UP && p.lateUps == 1, "button up, fire still set: FireUp");
  CHECK(tbot_act(&p, 1, 0, 0, 0, 1, 1064) == TB_NONE);
  tbot_act(&p, 1, 1, 0, 0, 0, 2000);
  tbot_act(&p, 1, 0, 0, 1, 1, 2016);
  CHECK_MSG(tbot_act(&p, 1, 0, 0, 0, 0, 2032) == TB_NONE && p.lateUps == 1, "the user's release ended it");
  tbot_act(&p, 1, 1, 0, 0, 0, 3000);
  tbot_act(&p, 1, 0, 0, 1, 1, 3016);
  CHECK_MSG(tbot_act(&p, 0, 0, 0, 1, 1, 3032) == TB_UP && p.lateUps == 2, "a closing gate ends it too");
  CHECK(tbot_act(&p, 0, 0, 0, 0, 1, 3048) == TB_NONE && p.downs == p.ups + p.forgets);
}

// The aimbot's turn (aimbot.h): the delta from the rendered view to the target, added to the event's adjust, the
// short way round in yaw. The three views used below: no shake, and the stock-like limits 16000 up, 49152 down.
static AimbotView aimbot_view(int vrPitch, int calcPitch, int calcYaw) {
  const AimbotView v = { vrPitch, calcPitch, calcYaw, 16000, 49152, 0, 0 };
  return v;
}
static void tb_0300_aimbot_yaw_wrap() {
  int dp = 7, dy = 7;
  aimbot_turn(aimbot_view(0, 0, 0), 0, 0, 1000, 2000, 0, AIMBOT_SECONDS_PER_TIC, &dp, &dy);
  CHECK_MSG(dp == 1000 && dy == 2000, "a snap at 0 %");
  aimbot_turn(aimbot_view(0, 0, 65000), 0, 0, 0, 500, 0, AIMBOT_SECONDS_PER_TIC, &dp, &dy);
  CHECK_MSG(dy == 1036, "the short way across the wrap");
  aimbot_turn(aimbot_view(0, 0, 500), 0, 0, 0, 65000, 0, AIMBOT_SECONDS_PER_TIC, &dp, &dy);
  CHECK_MSG(dy == -1036, "and back");
  aimbot_turn(aimbot_view(0, 0, -536), 0, 0, 0, 500, 0, AIMBOT_SECONDS_PER_TIC, &dp, &dy);
  CHECK_MSG(dy == 1036, "an unnormalised view yaw");
  aimbot_turn(aimbot_view(0, 0, 0), 0, 0, 0, 32768 + 100, 0, AIMBOT_SECONDS_PER_TIC, &dp, &dy);
  CHECK_MSG(dy == -32668, "past half a turn: the other way");
  // The mouse's turn (the game's own or raw mouse's) is already in the adjust: the sum lands on the target.
  aimbot_turn(aimbot_view(0, 0, 0), 120, 300, 1000, 2000, 0, AIMBOT_SECONDS_PER_TIC, &dp, &dy);
  CHECK_MSG(dp == 880 && dy == 1700, "composes with the adjust already there");
  // The rendered view leads ViewRotation by its shake: the delta is the rendered view's.
  aimbot_turn(aimbot_view(0, 200, 0), 0, 0, 1000, 0, 0, AIMBOT_SECONDS_PER_TIC, &dp, &dy);
  CHECK_MSG(dp == 800, "CalcedViewRotation above ViewRotation");
  aimbot_turn(aimbot_view(60000, 60000, 0), 0, 0, -5000, 0, 0, AIMBOT_SECONDS_PER_TIC, &dp, &dy);
  CHECK_MSG(dp == 536, "a pitch stored below zero");
  // rotator(vector) to the turn: a target straight to the left and up.
  int wp, wy;
  aim_rotator_to(0.f, 100.f, 100.f, &wp, &wy);
  aimbot_turn(aimbot_view(0, 0, 0), 0, 0, wp, wy, 0, AIMBOT_SECONDS_PER_TIC, &dp, &dy);
  CHECK(dp == 8192 && dy == 16384);
  // 64 views and adjusts: after a snap the rendered view faces the target (mod 65536).
  int bad = 0;
  for (int i = 0; i < 64; i++) {
    const int vrP = ((i * 977) % 30000) - 15000, calcP = vrP + (i % 7) * 13, calcY = (i * 104729) & 0xFFFF;
    const int aP = (i % 5) * 31 - 60, aY = (i % 9) * 57 - 200;
    const int wantP = (i * 1237) % 14000 - 7000, wantY = (i * 50021) & 0xFFFF;
    aimbot_turn(aimbot_view(vrP & 0xFFFF, calcP, calcY), aP, aY, wantP, wantY, 0, AIMBOT_SECONDS_PER_TIC, &dp, &dy);
    if (aim_wrap16(calcP + aP + dp - wantP) != 0 || aim_wrap16(calcY + aY + dy - wantY) != 0) bad++;
  }
  CHECK_MSG(bad == 0, "64 snaps on target");
}

// The game's pitch limits (UpdateRotation 0x1026ACA0: MaxUp +0x1FD4 < Pitch < MinDown +0x1FD8 is clamped).
static void tb_0301_aimbot_pitch_clamp() {
  CHECK_MSG(aimbot_clamp_pitch(15000, 16000, 49152) == 15000 && aimbot_clamp_pitch(-16384, 16000, 49152) == -16384,
            "inside the limits, and on the lower one");
  CHECK_MSG(aimbot_clamp_pitch(16001, 16000, 49152) == 16000 && aimbot_clamp_pitch(16384, 16000, 49152) == 16000,
            "past the upper limit");
  CHECK_MSG(aimbot_clamp_pitch(-16385, 16000, 49152) == -16384 && aimbot_clamp_pitch(49000, 16000, 49152) == -16384,
            "past the lower limit, either form");
  CHECK_MSG(aimbot_clamp_pitch(33000, 16000, 49152) == -16384 && aimbot_clamp_pitch(30000, 16000, 49152) == 16000,
            "deep in the band: the nearer limit");
  CHECK_MSG(aimbot_clamp_pitch(17000, 0, 0) == 17000, "unread limits clamp nothing (the engine's test fails too)");
  int dp, dy;
  aimbot_turn(aimbot_view(0, 0, 0), 0, 0, 16384, 0, 0, AIMBOT_SECONDS_PER_TIC, &dp, &dy);
  CHECK_MSG(dp == 16000, "a target overhead: up to the limit only");
  aimbot_turn(aimbot_view(0, 0, 0), 0, 0, -16384, 0, 0, AIMBOT_SECONDS_PER_TIC, &dp, &dy);
  CHECK_MSG(dp == -16384, "straight down is the lower limit itself");
  aimbot_turn(aimbot_view(0, 300, 0), 0, 0, 16384, 0, 0, AIMBOT_SECONDS_PER_TIC, &dp, &dy);
  CHECK_MSG(dp == 16000, "the limit is ViewRotation's, whatever the shake");
  // The mouse already pushed past the limit: ours never leaves the sum inside the band, where the engine's clamp
  // would pick a side by aLookUp's sign.
  aimbot_turn(aimbot_view(15900, 15900, 0), 500, 0, 16000, 0, 50, AIMBOT_SECONDS_PER_TIC, &dp, &dy);
  CHECK_MSG(aim_wrap16(15900 + 500 + dp) == 16000, "a smoothed step held at the limit");
  int bad = 0;
  for (int i = 0; i < 64; i++) {
    const int vr = (i * 4099) % 32000 - 16000, a = (i % 11) * 300 - 1500, want = (i * 3001) % 32768 - 16384;
    aimbot_turn(aimbot_view(vr & 0xFFFF, vr, 0), a, 0, want, 0, (i * 13) % 100, AIMBOT_SECONDS_PER_TIC, &dp, &dy);
    const unsigned u = (unsigned)(vr + a + dp) & 0xFFFF;
    if (16000 < (int)u && (int)u < 49152) bad++;
  }
  CHECK_MSG(bad == 0, "64 turns: never inside the band");
  AimbotView v = aimbot_view(0, 0, 0);
  v.lockPitch = 1;
  aimbot_turn(v, 0, 0, 1000, 2000, 0, AIMBOT_SECONDS_PER_TIC, &dp, &dy);
  CHECK_MSG(dp == 0 && dy == 2000, "ViewRotationMode 2: pitch left alone");
  v = aimbot_view(0, 0, 0);
  v.lockYaw = 1;
  aimbot_turn(v, 0, 0, 1000, 2000, 0, AIMBOT_SECONDS_PER_TIC, &dp, &dy);
  CHECK_MSG(dp == 1000 && dy == 0, "ViewRotationMode 1: yaw left alone");
}

// AA's smoothing step: (100 - Smoothing) % of the remaining delta per event, with a forced unit step.
static void tb_0302_aimbot_smoothing() {
  CHECK_MSG(aimbot_smooth_step(1000, 0) == 1000 && aimbot_smooth_step(-1000, 0) == -1000, "0 %: a snap");
  CHECK(aimbot_smooth_step(1000, 50) == 500 && aimbot_smooth_step(-1000, 50) == -500);
  CHECK_MSG(aimbot_smooth_step(1000, 100) == 0 && aimbot_smooth_step(-7, 100) == 0, "100 %: holds");
  CHECK_MSG(aimbot_smooth_step(1, 50) == 1 && aimbot_smooth_step(-1, 99) == -1 && aimbot_smooth_step(3, 90) == 1,
            "the forced unit step");
  CHECK(aimbot_smooth_step(0, 50) == 0 && aimbot_smooth_step(-3, 50) == -1);
  int d = 1000, steps = 0;
  for (; d != 0 && steps < 64; steps++) d -= aimbot_smooth_step(d, 80);
  CHECK_MSG(d == 0, "80 %: converges, never parked a unit short");
  d = 1000;
  for (int i = 0; i < 64; i++) d -= aimbot_smooth_step(d, 100);
  CHECK_MSG(d == 1000, "100 %: never moves");
  int dp, dy;
  aimbot_turn(aimbot_view(0, 0, 0), 0, 0, 1000, -2000, 50, AIMBOT_SECONDS_PER_TIC, &dp, &dy);
  CHECK_MSG(dp == 500 && dy == -1000, "the turn steps both axes");
  // Time-based (AA's ekm_smooth_step_tics): the event's DeltaTime in AA's 30 Hz tics, so the glide takes the same
  // wall time at any frame rate. One tic is exactly the per-tic step above.
  CHECK(AIMBOT_SECONDS_PER_TIC == 1.0 / 30.0 && AIMBOT_TICS_MAX == 4);
  int same = 1;
  for (int i = 0; i < 64; i++) {
    const int delta = (i * 7919) % 20000 - 10000, pct = (i * 37) % 101;
    if (aimbot_smooth_step_dt(delta, pct, AIMBOT_SECONDS_PER_TIC) != aimbot_smooth_step(delta, pct)) same = 0;
  }
  CHECK_MSG(same, "one tic of DeltaTime: AA's per-tic step");
  CHECK_MSG(aimbot_smooth_step_dt(1000, 0, 0.0) == 1000 && aimbot_smooth_step_dt(-900, 0, 1.0 / 240) == -900,
            "0 %: a snap at any frame rate");
  CHECK_MSG(aimbot_smooth_step_dt(1000, 100, 1.0 / 30) == 0 && aimbot_smooth_step_dt(1000, 50, 0.0) == 0,
            "100 % holds; no time, no step");
  CHECK_MSG(aimbot_smooth_step_dt(1, 99, 1.0 / 240) == 1 && aimbot_smooth_step_dt(-1, 50, 1.0 / 240) == -1,
            "the forced unit step");
  CHECK_MSG(aimbot_smooth_step_dt(1000, 50, 1.0) == 937, "a long gap counts four tics at most (a pause, not a snap)");
  // 0.2 s at 50 %: 6 events at 30 fps and 24 at 120 fps leave about the same distance, and still gliding.
  int d30 = 1000, d120 = 1000;
  for (int i = 0; i < 6; i++) d30 -= aimbot_smooth_step_dt(d30, 50, 1.0 / 30);
  for (int i = 0; i < 24; i++) d120 -= aimbot_smooth_step_dt(d120, 50, 1.0 / 120);
  CHECK_MSG(d120 > 0 && d30 > 0 && abs(d30 - d120) <= 8, "the same glide at 30 and 120 fps");
  aimbot_turn(aimbot_view(0, 0, 0), 0, 0, 1000, -2000, 50, 1.0 / 120, &dp, &dy);
  CHECK_MSG(dp == 159 && dy == -318, "the turn steps by DeltaTime");
}

// Pick target and Cone (AA's): Crosshair ranks by the angle off centre inside the cone; Nearest by distance alone.
static void tb_0303_aim_pick_cone() {
  CHECK(strcmp(aim_pick_label(AIM_PICK_CROSSHAIR), "Crosshair") == 0 &&
        strcmp(aim_pick_label(AIM_PICK_NEAREST), "Nearest") == 0);
  CHECK_MSG(aim_in_cone(AIM_PICK_CROSSHAIR, 30.f, 30) && !aim_in_cone(AIM_PICK_CROSSHAIR, 30.01f, 30), "the edge");
  CHECK_MSG(!aim_in_cone(AIM_PICK_CROSSHAIR, sqrtf(-1.f), 90), "NaN: never");
  CHECK_MSG(!aim_in_cone(AIM_PICK_NEAREST, 170.f, 30) && aim_in_cone(AIM_PICK_NEAREST, 29.f, 30),
            "Nearest keeps to the cone too");
  CHECK(aim_cand_key(AIM_PICK_CROSSHAIR, 12.f, 900.f) == 12.f && aim_cand_key(AIM_PICK_NEAREST, 12.f, 900.f) == 900.f);
  // Nearest: the candidates sorted by distance, the near one first though farther off centre.
  const float ang[3] = { 5.f, 40.f, 20.f }, dist[3] = { 2000.f, 300.f, 900.f };
  AimCand c[3];
  for (int i = 0; i < 3; i++) {
    const AimCand x = { i, aim_cand_key(AIM_PICK_NEAREST, ang[i], dist[i]), AIM_VIS_YES };
    c[i] = x;
  }
  aim_cand_sort(c, 3);
  CHECK_MSG(c[0].idx == 1 && c[1].idx == 2 && c[2].idx == 0 && aim_pick(c, 3, -1) == 0, "Nearest: by distance");
  for (int i = 0; i < 3; i++) {
    const AimCand x = { i, aim_cand_key(AIM_PICK_CROSSHAIR, ang[i], dist[i]), AIM_VIS_YES };
    c[i] = x;
  }
  aim_cand_sort(c, 3);
  CHECK_MSG(c[0].idx == 0 && c[1].idx == 2, "Crosshair: by angle");
  CHECK(AIM_CONE_MIN == 1 && AIM_CONE_MAX == 90 && AIM_CONE_DEF == 30);
}

// The hold rule: Hold key off tracks continuously; on, only while a live binding is down and the game is in front.
// The lock is kept only while the target lives, is not known hidden and stays in the cone.
static void tb_0304_aimbot_hold() {
  CHECK_MSG(aimbot_key_held(0, 0, 0, 0), "Hold key off: always");
  CHECK(aimbot_key_held(1, VK_BIND_XBUTTON1, 1, 1));
  CHECK_MSG(!aimbot_key_held(1, VK_BIND_XBUTTON1, 0, 1), "the game not in front: the key is not read");
  CHECK(!aimbot_key_held(1, VK_BIND_XBUTTON1, 1, 0));
  CHECK_MSG(!aimbot_key_held(1, 0, 1, 1) && !aimbot_key_held(1, 0x10, 1, 1), "unbound or unmappable: never held");
  CHECK(aimbot_key_held(1, 0x46, 1, 1));   // F, a keyboard key
  // A mouse binding is also read from the engine's own DirectInput read (the exclusive mouse sends no messages).
  CHECK(aimbot_vk_di_button(VK_BIND_RBUTTON) == 1 && aimbot_vk_di_button(VK_BIND_MBUTTON) == 2 &&
        aimbot_vk_di_button(VK_BIND_XBUTTON1) == 3 && aimbot_vk_di_button(VK_BIND_XBUTTON2) == 4);
  CHECK_MSG(aimbot_vk_di_button(VK_BIND_LBUTTON) == -1 && aimbot_vk_di_button(0x46) == -1 &&
            aimbot_vk_di_button(0) == -1, "fire and keys: none");
  unsigned char st[0x14];
  memset(st, 0, sizeof st);
  st[15] = 0x80;
  CHECK_MSG(dm_buttons(st, 0x10) == 8u && dm_buttons(st, 0x14) == 8u, "button 3 (X1) in either format");
  st[19] = 0x80;
  CHECK_MSG(dm_buttons(st, 0x10) == 8u && dm_buttons(st, 0x14) == 0x88u, "DIMOUSESTATE has four buttons");
  CHECK(dm_buttons(st, 12) == 0u && dm_buttons(0, 0x14) == 0u);
  CHECK(aimbot_keep(1, 1, AIM_VIS_YES, 1) && aimbot_keep(1, 1, AIM_VIS_UNKNOWN, 1));
  CHECK_MSG(!aimbot_keep(0, 1, AIM_VIS_YES, 1), "gone");
  CHECK_MSG(!aimbot_keep(1, 0, AIM_VIS_YES, 1), "dead");
  CHECK_MSG(!aimbot_keep(1, 1, AIM_VIS_NO, 1), "a wall between");
  CHECK_MSG(!aimbot_keep(1, 1, AIM_VIS_YES, 0), "left the cone");
}

// The Triggerbot's third mode (AA's): it fires while the aimbot holds a visible target, never on an ally.
static void tb_0305_tbot_aimbot_mode() {
  CHECK(TBOT_WHEN_AIMBOT == 2 && strcmp(tbot_when_label(TBOT_WHEN_AIMBOT), "Aimbot") == 0);
  CHECK(tbot_when_holds(TBOT_WHEN_AIMBOT, 0, 1) && !tbot_when_holds(TBOT_WHEN_AIMBOT, 1, 0));
  CHECK_MSG(tbot_holds(TBOT_WHEN_AIMBOT, 0, 0, 1, 1), "locked and covered: fires wherever the crosshair is");
  CHECK_MSG(!tbot_holds(TBOT_WHEN_AIMBOT, 1, 0, 0, 1), "no lock: no fire, even on an enemy");
  CHECK_MSG(!tbot_holds(TBOT_WHEN_AIMBOT, 0, 1, 1, 1), "never with an ally under the crosshair");
  // The ray gate (2026-10-09): the first FireDown came on the lock's frame, before the view had turned.
  CHECK_MSG(!tbot_holds(TBOT_WHEN_AIMBOT, 0, 0, 1, 0, 0, 0), "lock, no cover, the ray misses: wait for the turn");
  CHECK_MSG(tbot_holds(TBOT_WHEN_AIMBOT, 0, 0, 1, 0, 0, 1), "lock and the ray lands: fire");
  CHECK_MSG(tbot_holds(TBOT_WHEN_AIMBOT, 0, 0, 1, 1, 0, 0), "silent aim covers the shot: fire whatever the ray says");
  CHECK_MSG(tbot_holds(TBOT_WHEN_AIMBOT, 0, 0, 1, 0, 0, -1), "the ray does not resolve: today's fire on the lock");
  CHECK_MSG(!tbot_holds(TBOT_WHEN_AIMBOT, 0, 0, 0, 1, 0, 1), "no lock: nothing, whatever the ray");
  CHECK_MSG(!tbot_holds(TBOT_WHEN_AIMBOT, 0, 1, 1, 0, 0, 1), "an ally under the crosshair: never");
  // A projectile gun is led, so the view points ahead of the pawn: the angle to the aimbot's own point decides.
  CHECK_MSG(tbot_view_ready(AIMW_HITSCAN, 1, 30.f) == 1 && tbot_view_ready(AIMW_HITSCAN, 0, 0.f) == 0 &&
                tbot_view_ready(AIMW_BEAM, 0, 0.f) == 0, "hitscan and beam follow the ray");
  CHECK_MSG(tbot_view_ready(AIMW_PROJECTILE, 0, 1.9f) == 1 && tbot_view_ready(AIMW_PROJECTILE, 1, 2.1f) == 0,
            "projectile: under 2 degrees to the led point holds, whatever the ray");
  CHECK_MSG(tbot_view_ready(AIMW_PROJECTILE, 0, -1.f) == -1 && tbot_view_ready(AIMW_HITSCAN, -1, 0.f) == -1,
            "unresolved stays unresolved");
  CHECK(strcmp(tbot_when_label(TBOT_WHEN_LOCKED), "Locked") == 0);
}

static void tb_0306_aimbot_config() {
  config_defaults();
  CHECK_MSG(g_cfg.aimbotOn == 0 && g_cfg.aimHold == 1 && g_cfg.aimSmooth == 0, "AA's defaults: off, Hold key on, snap");
  CHECK(g_cfg.aimTarget == AIM_PICK_CROSSHAIR && g_cfg.aimCone == AIM_CONE_DEF);
  CHECK_MSG(BIND_ROW_AIMBOT == 2, "appended after Game speed");
  CHECK_MSG(g_cfg.aimbotKey == VK_BIND_XBUTTON1 && config_bind_get(BIND_ROW_AIMBOT) == VK_BIND_XBUTTON1 &&
            strcmp(bind_label(config_bind_get(BIND_ROW_AIMBOT)), "Mouse 4") == 0, "mouse button 4");
  config_bind_set(BIND_ROW_AIMBOT, 0x46);
  CHECK(g_cfg.aimbotKey == 0x46);
  CHECK(config_key_known("aim", "aimon") && config_key_known("aim", "aimhold") &&
        config_key_known("aim", "aimsmooth") && config_key_known("aim", "aimtarget") &&
        config_key_known("aim", "aimcone") && config_key_known("aim", "bindaim"));
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_aimbot");
  write_text(path, "[aim]\r\ntriggerwhen=2\r\naimcone=91\r\naimsmooth=100\r\naimtarget=1\r\n");
  config_load_from(path);
  CHECK_MSG(g_cfg.tbotWhen == TBOT_WHEN_AIMBOT && g_cfg.aimCone == AIM_CONE_DEF && g_cfg.aimSmooth == 100 &&
            g_cfg.aimTarget == AIM_PICK_NEAREST, "the ranges");
  DeleteFileA(path);
  config_defaults();
}

// The pick admits by the cone at the same point the lock keeps by (the Aim at point, head or body), so a
// pick is also a keep; the cheap pre-filter at Location never refuses a pawn whose aim point is inside.
static void tb_0307_aim_cone_pick_keep() {
  const float eye[3] = { 0.f, 0.f, 0.f };
  float fwd[3];
  aim_forward(0, 0, fwd);
  const float loc[3] = { 200.f, 100.f, 0.f }, head[3] = { 200.f, 100.f, 60.f };
  CHECK_MSG(aim_cone_admits(AIM_PICK_CROSSHAIR, fwd, eye, loc, 30) &&
            !aim_cone_admits(AIM_PICK_CROSSHAIR, fwd, eye, head, 30), "centre inside Cone 30, head outside");
  // Pick and keep both ask aim_cone_admits at the head: neither takes it, so no lock-and-drop churn.
  const int picked = aim_cone_admits(AIM_PICK_CROSSHAIR, fwd, eye, head, 30);
  CHECK_MSG(!picked && !aimbot_keep(1, 1, AIM_VIS_YES, aim_cone_admits(AIM_PICK_CROSSHAIR, fwd, eye, head, 30)),
            "the head outside: not picked, so never locked and dropped");
  CHECK_MSG(aim_cone_admits(AIM_PICK_NEAREST, fwd, eye, head, 1) == aim_cone_admits(AIM_PICK_CROSSHAIR, fwd, eye, head, 1),
            "Nearest: the same cone as Crosshair");
  CHECK_MSG(aim_cone_prefilter(AIM_PICK_CROSSHAIR, aim_angle(fwd, 200.f, 100.f, 0.f), 223.6f, 20.f, 50.f, 30),
            "the pre-filter lets it through to the exact test");
  int bad = 0, admitted = 0;
  unsigned r = 777;
  for (int i = 0; i < 64; i++) {
    r = r * 1103515245u + 12345u;
    const float lx = 60.f + (float)((r >> 8) % 3000), ly = (float)((int)((r >> 12) % 2400) - 1200);
    const float lz = (float)((int)((r >> 4) % 400) - 200), cr = 18.f + (float)(r % 30), ch = 30.f + (float)((r >> 20) % 40);
    // A head somewhere aim_head_plausible accepts.
    const float hp[3] = { lx + (cr + 48.f) * 0.7f * ((r >> 3) & 1 ? 1.f : -1.f),
                          ly + (cr + 48.f) * 0.7f * ((r >> 5) & 1 ? 1.f : -1.f), lz + ch + 31.f };
    if (!aim_head_plausible(hp[0], hp[1], hp[2], lx, ly, lz, cr, ch)) { bad++; continue; }
    const float d = sqrtf(lx * lx + ly * ly + lz * lz);
    const int cone = 1 + (int)((r >> 16) % 90);
    if (!aim_cone_admits(AIM_PICK_CROSSHAIR, fwd, eye, hp, cone)) continue;
    admitted++;
    if (!aim_cone_prefilter(AIM_PICK_CROSSHAIR, aim_angle(fwd, lx, ly, lz), d, cr, ch, cone)) bad++;
  }
  CHECK_MSG(bad == 0 && admitted > 0, "64 pawns: a head inside the cone always passes the pre-filter");
}

// The calibration marker shows with Silent aim or the Aimbot (AA's aimon || silenton).
static void tb_0308_aim_marker_aimbot() {
  CHECK(aim_marker_shown(0, 1, 1) && aim_marker_shown(1, 0, 1) && aim_marker_shown(1, 1, 1));
  CHECK(!aim_marker_shown(0, 0, 1) && !aim_marker_shown(1, 1, 0));
}

// Limits wider than half a turn (UE1's 18000/49152): a smoothed pitch goes through level, never the band.
static void tb_0309_aimbot_wide_limits() {
  const AimbotView v = { (-16000) & 0xFFFF, -16000, 0, 18000, 49152, 0, 0 };
  int dp, dy;
  aimbot_turn(v, 0, 0, 17000, 0, 50, AIMBOT_SECONDS_PER_TIC, &dp, &dy);
  CHECK_MSG(aim_wrap16(-16000 + dp) == 500, "half way up through level");
  aimbot_turn(v, 0, 0, 17000, 0, 0, AIMBOT_SECONDS_PER_TIC, &dp, &dy);
  CHECK_MSG(aim_wrap16(-16000 + dp) == 17000, "and a snap");
}

// The cadence per weapon config (dnGame.u class defaults, read 2026-10-05): only an interrupting gun without a
// fire-start or looping animation is re-pressed.
static void tb_0320_tbot_fire_mode() {
  CHECK_MSG(tbot_fire_mode(1, 1, 0, 0) == TBOT_FIRE_PULSE, "Pistol (0.15 s) and MiniGun_BL (0.06 s): interrupt");
  CHECK_MSG(tbot_fire_mode(1, 0, 1, 1) == TBOT_FIRE_HOLD, "DualPistol: looping fire");
  CHECK_MSG(tbot_fire_mode(1, 1, 1, 0) == TBOT_FIRE_HOLD, "Ripper: a fire-start animation");
  CHECK_MSG(tbot_fire_mode(1, 1, 1, 1) == TBOT_FIRE_HOLD, "freeze ray: looping");
  CHECK_MSG(tbot_fire_mode(1, 0, 0, 0) == TBOT_FIRE_HOLD, "shotgun, shrink ray: ClearLockout refires a held one");
  CHECK_MSG(tbot_fire_mode(0, 1, 0, 0) == TBOT_FIRE_HOLD, "flags unread: hold");
}

// PULSE: pressed when the weapon is ready (bWeaponFireLockout clear), released the next frame; HOLD ignores ready.
static void tb_0321_tbot_pulse_press() {
  TbotPress p;
  memset(&p, 0, sizeof p);
  CHECK_MSG(tbot_act(&p, 1, 1, 0, 0, 0, 1000, 1, 0) == TB_NONE, "pulse: not ready, no press");
  CHECK(tbot_act(&p, 1, 1, 0, 0, 0, 1006, 1, 1) == TB_DOWN && p.ours);
  CHECK_MSG(tbot_act(&p, 1, 1, 0, 0, 1, 1012, 1, 0) == TB_UP && !p.ours, "released the next frame, still wanted");
  CHECK_MSG(tbot_act(&p, 1, 1, 0, 0, 0, 1018, 1, 0) == TB_NONE, "the lockout runs");
  CHECK_MSG(tbot_act(&p, 1, 1, 0, 0, 0, 1160, 1, 1) == TB_DOWN, "ready again: pressed at once, no tap gap");
  CHECK_MSG(tbot_act(&p, 1, 1, 0, 1, 1, 1166, 1, 0) == TB_FORGET && p.pendingUp, "never FireUp under the user's fire");
  tbot_act(&p, 0, 0, 0, 0, 0, 1200, 1, 1);
  CHECK_MSG(tbot_act(&p, 1, 1, 0, 0, 0, 2000, 0, 0) == TB_DOWN, "hold: ready does not gate the press");
  CHECK_MSG(tbot_act(&p, 1, 1, 0, 0, 1, 2006, 0, 1) == TB_NONE && p.ours, "hold: kept down");
  tbot_act(&p, 0, 0, 0, 0, 1, 2012, 0, 1);
  CHECK_MSG(!p.ours && p.downs == p.ups + p.forgets, "paired");
}

// A small actor graph for tbot_attached_pawn: node i is &g_tbNodes[i]; Owner and MountParent as indices (-1 none).
static char g_tbNodes[8];
struct TbGraph { int owner[8], mount[8], pawn[8], stop[8]; };
static int tb_node_index(const void* o) {
  const int i = (int)((const char*)o - g_tbNodes);
  return o && i >= 0 && i < 8 ? i : -1;
}
static void tb_graph_links(const void* o, const void* out[2], void* ctx) {
  const TbGraph* g = (const TbGraph*)ctx;
  const int i = tb_node_index(o);
  out[0] = out[1] = 0;
  if (i < 0) return;
  if (g->owner[i] >= 0) out[0] = &g_tbNodes[g->owner[i]];
  if (g->mount[i] >= 0) out[1] = &g_tbNodes[g->mount[i]];
}
static int tb_graph_pawn(const void* o, void* ctx) {
  const int i = tb_node_index(o);
  return i >= 0 && ((const TbGraph*)ctx)->pawn[i];
}
static int tb_graph_stop(const void* o, void* ctx) {
  const int i = tb_node_index(o);
  return i >= 0 && ((const TbGraph*)ctx)->stop[i];
}
static const void* tb_node(const TbGraph&, int i) { return &g_tbNodes[i]; }

// An actor the look trace hits counts as the walked pawn it hangs from, up Owner and MountParent, 4 links at most.
static void tb_0322_tbot_attached_pawn() {
  TbGraph g;
  for (int i = 0; i < 8; i++) g.owner[i] = g.mount[i] = -1, g.pawn[i] = g.stop[i] = 0;
  g.pawn[0] = 1;
  int steps = -1;
  CHECK_MSG(tbot_attached_pawn(tb_node(g, 0), tb_graph_links, tb_graph_pawn, tb_graph_stop, &g, &steps) == tb_node(g, 0) &&
            steps == 0, "the pawn itself");
  g.owner[1] = 0;   // a held weapon
  CHECK_MSG(tbot_attached_pawn(tb_node(g, 1), tb_graph_links, tb_graph_pawn, tb_graph_stop, &g, &steps) == tb_node(g, 0) &&
            steps == 1, "a weapon owned by the pawn");
  g.mount[2] = 3, g.owner[3] = 7, g.mount[3] = 0;   // an effect mounted on a mount on the pawn, owned elsewhere
  CHECK_MSG(tbot_attached_pawn(tb_node(g, 2), tb_graph_links, tb_graph_pawn, tb_graph_stop, &g, &steps) == tb_node(g, 0) &&
            steps == 2, "through MountParent when Owner leads nowhere");
  for (int i = 0; i < 8; i++) g.owner[i] = g.mount[i] = -1;
  for (int i = 1; i < 5; i++) g.mount[i] = i - 1;   // 4 -> 3 -> 2 -> 1 -> 0
  CHECK_MSG(tbot_attached_pawn(tb_node(g, 4), tb_graph_links, tb_graph_pawn, tb_graph_stop, &g, &steps) == tb_node(g, 0) &&
            steps == 4, "four links: found");
  g.mount[5] = 4;
  CHECK_MSG(!tbot_attached_pawn(tb_node(g, 5), tb_graph_links, tb_graph_pawn, tb_graph_stop, &g, &steps), "five links: not");
  g.owner[6] = 7, g.owner[7] = 6;
  CHECK_MSG(!tbot_attached_pawn(tb_node(g, 6), tb_graph_links, tb_graph_pawn, tb_graph_stop, &g, &steps), "a cycle ends");
  CHECK(!tbot_attached_pawn(0, tb_graph_links, tb_graph_pawn, tb_graph_stop, &g, &steps));
  // The walk never goes through the player, and a projectile is never its owner's.
  for (int i = 0; i < 8; i++) g.owner[i] = g.mount[i] = -1, g.pawn[i] = g.stop[i] = 0;
  g.pawn[0] = 1;
  g.mount[1] = 2, g.stop[2] = 1, g.owner[2] = 0;   // mounted on the player, the player owned by a pawn
  CHECK_MSG(!tbot_attached_pawn(tb_node(g, 1), tb_graph_links, tb_graph_pawn, tb_graph_stop, &g, &steps),
            "the walk stops at the player");
  g.owner[3] = 0, g.stop[3] = 1;   // an enemy's rocket in flight
  CHECK_MSG(!tbot_attached_pawn(tb_node(g, 3), tb_graph_links, tb_graph_pawn, tb_graph_stop, &g, &steps),
            "a projectile under the crosshair is not its owner");
  g.stop[0] = 1;
  CHECK_MSG(tbot_attached_pawn(tb_node(g, 0), tb_graph_links, tb_graph_pawn, tb_graph_stop, &g, &steps) ==
            tb_node(g, 0), "a pawn hit itself still counts");
  g.stop[3] = 0;
  CHECK_MSG(tbot_attached_pawn(tb_node(g, 3), tb_graph_links, tb_graph_pawn, 0, &g, &steps) == tb_node(g, 0),
            "no stop test: followed");
}

// A refused FireUp is owed and sent again each frame until it goes through; under the user's own fire it
// waits for the button; a game that already released it owes nothing.
static void tb_0327_tbot_up_retry() {
  TbotPress p;
  memset(&p, 0, sizeof p);
  tbot_act(&p, 1, 1, 0, 0, 0, 1000);
  CHECK(tbot_act(&p, 1, 0, 0, 0, 1, 1016) == TB_UP);
  tbot_up_result(&p, 0);
  CHECK_MSG(p.upOwed && tbot_act(&p, 1, 1, 0, 0, 1, 1032) == TB_UP && p.upRetries == 1, "refused: sent again");
  tbot_up_result(&p, 0);
  CHECK_MSG(tbot_act(&p, 0, 0, 0, 0, 1, 1048) == TB_UP && p.upRetries == 2, "with the gate closed too");
  tbot_up_result(&p, 1);
  CHECK_MSG(!p.upOwed && tbot_act(&p, 1, 0, 0, 0, 0, 1064) == TB_NONE, "through: nothing owed");
  tbot_act(&p, 1, 1, 0, 0, 0, 2000);
  tbot_act(&p, 1, 0, 0, 0, 1, 2016);
  tbot_up_result(&p, 0);
  CHECK_MSG(tbot_act(&p, 1, 0, 0, 1, 1, 2032) == TB_NONE && !p.upOwed && p.pendingUp, "the user's button: waits");
  CHECK_MSG(tbot_act(&p, 1, 0, 0, 0, 1, 2048) == TB_UP, "then sent once it is up");
  tbot_up_result(&p, 1);
  tbot_act(&p, 1, 1, 0, 0, 0, 3000);
  tbot_act(&p, 1, 0, 0, 0, 1, 3016);
  tbot_up_result(&p, 0);
  CHECK_MSG(tbot_act(&p, 1, 0, 0, 0, 0, 3032) == TB_NONE && !p.upOwed, "released by the game meanwhile: not owed");
  CHECK_MSG(p.downs == p.ups + p.forgets && !p.ours, "paired");
}

// Locked without a redirected shot follows the aimbot's lock (as the Aimbot mode does), never onto an ally.
static void tb_0323_tbot_locked_follows_aimbot() {
  CHECK_MSG(tbot_holds(TBOT_WHEN_LOCKED, 0, 0, 0, 0, 1), "Silent aim off, the aimbot locked: fires");
  CHECK_MSG(!tbot_holds(TBOT_WHEN_LOCKED, 0, 1, 0, 0, 1), "an ally under the crosshair: never");
  CHECK_MSG(tbot_holds(TBOT_WHEN_LOCKED, 1, 0, 0, 0, 0), "no lock: the crosshair still");
  CHECK_MSG(!tbot_holds(TBOT_WHEN_LOCKED, 0, 0, 0, 0, 0), "neither: no");
  CHECK_MSG(tbot_holds(TBOT_WHEN_LOCKED, 0, 0, 1, 1, 0), "covered: Silent aim's target as before");
  CHECK_MSG(!tbot_holds(TBOT_WHEN_CROSSHAIR, 0, 0, 0, 0, 1), "Crosshair ignores the aimbot");
}

// Hysteresis: a visible challenger replaces the visible previous target only when clearly ahead (10 %, or 1 degree /
// 32 units, whichever is larger).
static void tb_0324_aim_pick_hysteresis() {
  CHECK_MSG(!aim_challenger_wins(AIM_PICK_CROSSHAIR, 2.0f, 2.3f) && aim_challenger_wins(AIM_PICK_CROSSHAIR, 1.2f, 2.3f),
            "Crosshair: by a degree near the centre");
  CHECK_MSG(!aim_challenger_wins(AIM_PICK_CROSSHAIR, 18.5f, 20.f) && aim_challenger_wins(AIM_PICK_CROSSHAIR, 17.9f, 20.f),
            "Crosshair: by 10 % farther out");
  CHECK_MSG(!aim_challenger_wins(AIM_PICK_NEAREST, 480.f, 513.f) && aim_challenger_wins(AIM_PICK_NEAREST, 460.f, 513.f),
            "Nearest: by 10 %");
  CHECK_MSG(!aim_challenger_wins(AIM_PICK_NEAREST, 100.f, 120.f) && aim_challenger_wins(AIM_PICK_NEAREST, 87.f, 120.f),
            "Nearest: by 32 units up close");
  CHECK_MSG(!aim_challenger_wins(AIM_PICK_NEAREST, 500.f, 500.f), "a tie keeps the target");
  AimCand c[3] = { { 0, 500.f, AIM_VIS_YES }, { 1, 513.f, AIM_VIS_YES }, { 2, 900.f, AIM_VIS_YES } };
  CHECK_MSG(aim_pick(c, 3, 1, AIM_PICK_NEAREST) == 1, "near tie: the previous target kept");
  CHECK_MSG(aim_pick(c, 3, -1, AIM_PICK_NEAREST) == 0 && aim_pick(c, 3, 1) == 0, "no previous, or no rule: the first");
  c[0].key = 400.f;
  CHECK_MSG(aim_pick(c, 3, 1, AIM_PICK_NEAREST) == 0, "clearly nearer: the challenger");
  c[0].key = 500.f;
  c[1].vis = AIM_VIS_NO;
  CHECK_MSG(aim_pick(c, 3, 1, AIM_PICK_NEAREST) == 0, "the previous one hidden: the challenger");
  c[1].vis = AIM_VIS_YES;
  c[0].vis = AIM_VIS_UNKNOWN;
  CHECK_MSG(aim_pick(c, 3, 1, AIM_PICK_NEAREST) == 1, "an unknown ahead: kept meanwhile, as before");
}

// spawns.md section 4: the rendered body's centre more than 64 units from Location is "away from Location".
static void tb_0325_spawnbox_offset() {
  const float loc[3] = { 100.f, 0.f, 50.f };
  const float mn[3] = { 80.f, -20.f, 0.f }, mx[3] = { 120.f, 20.f, 100.f };
  float d = -1.f, dz = -1.f;
  CHECK_MSG(spawnbox_offset(mn, mx, loc, &d, &dz) && d == 0.f && dz == 0.f, "the body on Location");
  const float mnUp[3] = { 80.f, -20.f, 300.f }, mxUp[3] = { 120.f, 20.f, 400.f };
  CHECK_MSG(spawnbox_offset(mnUp, mxUp, loc, &d, &dz) && dz == 300.f && d == 300.f && spawnbox_far(d),
            "the body 300 units above: far");
  CHECK_MSG(spawnbox_far(64.5f) && !spawnbox_far(64.f) && !spawnbox_far(sqrtf(-1.f)), "the threshold");
  const float z[3] = { 0.f, 0.f, 0.f };
  CHECK_MSG(!spawnbox_offset(z, z, loc, &d, &dz), "the zero box: no primitive");
  CHECK_MSG(!spawnbox_offset(mx, mn, loc, &d, &dz), "inverted: unusable");
  const float nan3[3] = { sqrtf(-1.f), 0.f, 0.f };
  CHECK_MSG(!spawnbox_offset(nan3, mx, loc, &d, &dz), "NaN: unusable");
  CHECK_MSG(spawnbox_skipped(1, 0, SB_PHYS_FALLING, 0) && spawnbox_skipped(1, 0, 18, 1), "dropping, not hostile");
  CHECK_MSG(!spawnbox_skipped(1, 1, SB_PHYS_FALLING, 1) && !spawnbox_skipped(0, 0, SB_PHYS_FALLING, 1) &&
            !spawnbox_skipped(1, 0, 18, 0), "hostile, dead, or not dropping: no");
}

// Once per (pawn, state, kind) a level, and 8 lines a level at most.
static void tb_0326_spawnbox_rate() {
  SbLog l;
  memset(&l, 0, sizeof l);
  static int objs_[2];  // distinct storage: an optimiser may fold equal unwritten locals
  int& a = objs_[0]; int& b = objs_[1];
  CHECK(spawnbox_log_new(&l, &a, 5, SB_KIND_FAR));
  CHECK_MSG(!spawnbox_log_new(&l, &a, 5, SB_KIND_FAR), "the same pawn and state: once");
  CHECK_MSG(spawnbox_log_new(&l, &a, 6, SB_KIND_FAR) && spawnbox_log_new(&l, &a, 6, SB_KIND_SKIPPED),
            "a new state or kind: again");
  int logged = 3;
  for (int s = 0; s < 64; s++) logged += spawnbox_log_new(&l, &b, s, SB_KIND_FAR);
  CHECK_MSG(logged == SPAWNBOX_LINES_PER_LEVEL && l.lines == SPAWNBOX_LINES_PER_LEVEL, "8 lines a level");
}

// spawns.md step 2: a far pawn stays flagged while far, 5 s at most, 8 pawns at most.
static void tb_0328_spawnbox_flags() {
  SbFlags s;
  memset(&s, 0, sizeof s);
  int p[10];
  const int i = spawnbox_flag_set(&s, &p[0], 1000);
  CHECK_MSG(i == 0 && s.n == 1 && s.f[0].pawn == &p[0] && s.f[0].since == 1000, "set");
  CHECK_MSG(spawnbox_flag_set(&s, &p[0], 2000) == 0 && s.n == 1 && s.f[0].since == 1000, "set again: the same flag");
  CHECK_MSG(spawnbox_flag_find(&s, &p[0]) == 0 && spawnbox_flag_find(&s, &p[1]) == -1, "found");
  CHECK_MSG(spawnbox_flag_keep(s.f[0], 1, 1, 1000 + SPAWNBOX_FLAG_MS - 1), "kept while far");
  CHECK_MSG(!spawnbox_flag_keep(s.f[0], 1, 0, 1100), "cleared back within 64 units");
  CHECK_MSG(!spawnbox_flag_keep(s.f[0], 0, 1, 1100), "cleared when the box is unusable");
  CHECK_MSG(!spawnbox_flag_keep(s.f[0], 1, 1, 1000 + SPAWNBOX_FLAG_MS), "cleared after 5 s");
  for (int k = 1; k < SPAWNBOX_FLAGS; k++) spawnbox_flag_set(&s, &p[k], 1000);
  CHECK_MSG(s.n == SPAWNBOX_FLAGS && spawnbox_flag_set(&s, &p[8], 1000) == -1, "the cap: 8");
  spawnbox_flag_drop(&s, 0);
  CHECK_MSG(s.n == SPAWNBOX_FLAGS - 1 && spawnbox_flag_find(&s, &p[0]) == -1 && spawnbox_flag_find(&s, &p[7]) >= 0,
            "dropped, the others kept");
  CHECK_MSG(spawnbox_flag_set(&s, &p[8], 1200) >= 0, "a slot again");
}

// The cut-scene note's table; a pawn animated away from Location without a body box is no target.
static void tb_0329_spawnbox_exclusion() {
  SbCuts s;
  memset(&s, 0, sizeof s);
  int p[40];
  spawnbox_cut_note(&s, &p[0], 1, 1000);
  CHECK_MSG(spawnbox_cut_has(&s, &p[0], 1500) && !spawnbox_cut_has(&s, &p[1], 1500), "noted");
  CHECK_MSG(!spawnbox_cut_has(&s, &p[0], 1000 + SPAWNBOX_CUT_MS), "not read again for a second: no longer counts");
  spawnbox_cut_note(&s, &p[0], 1, 1900);
  CHECK_MSG(spawnbox_cut_has(&s, &p[0], 2000) && s.n == 1, "refreshed, one entry");
  spawnbox_cut_note(&s, &p[0], 0, 2000);
  CHECK_MSG(!spawnbox_cut_has(&s, &p[0], 2000) && s.n == 0, "out of the cut-scene: removed");
  for (int k = 0; k < SPAWNBOX_CUTS; k++) spawnbox_cut_note(&s, &p[k], 1, 3000u + (unsigned)k);
  spawnbox_cut_note(&s, &p[39], 1, 3100);
  CHECK_MSG(s.n == SPAWNBOX_CUTS && spawnbox_cut_has(&s, &p[39], 3100) && !spawnbox_cut_has(&s, &p[0], 3100) &&
            spawnbox_cut_has(&s, &p[1], 3100), "full: the oldest replaced");
  CHECK(spawnbox_exclusion(0, 0, 0) == SB_EXCL_NONE && spawnbox_exclusion(1, 0, 0) == SB_EXCL_FAR &&
        spawnbox_exclusion(0, 1, 0) == SB_EXCL_NONE && spawnbox_exclusion(1, 1, 0) == SB_EXCL_FAR);
  CHECK_MSG(spawnbox_exclusion(0, 0, 1) == SB_EXCL_TELEPORT && spawnbox_exclusion(0, 1, 1) == SB_EXCL_TELEPORT,
            "teleporting alone excludes");
}

// An enemy mid AI teleport (CurrentOp 138, bTeleporting, bHidden) is no target; held from first to last frame read.
static void tb_0329g_spawnbox_invincible() {
  CHECK_MSG(spawnbox_health_locked(1, 0, 0, 0) && spawnbox_health_locked(0, 1, 0, 0) && spawnbox_health_locked(0, 0, 1, 0) &&
            spawnbox_health_locked(0, 0, 0, 1),
            "AIActor.Invulnerable, bGodMode, bGhostMode or bProtectedEgoRecharge: Pawn.ModifyHealthActivity keeps Health");
  CHECK_MSG(!spawnbox_health_locked(0, 0, 0, 0) && !spawnbox_health_locked(-1, -1, -1, -1) && !spawnbox_health_locked(-1, 0, -1, 0),
            "all clear or unreadable: damageable");
}

static void tb_0329f_spawnbox_health_floor() {
  CHECK_MSG(spawnbox_health_floored(75.f, 75.f) && spawnbox_health_floored(20.f, 20.f) && spawnbox_health_floored(10.f, 20.f),
            "health at or under its floor (RenderActor.SetHealth clamps to HealthMin): damage cannot lower it");
  CHECK_MSG(!spawnbox_health_floored(75.f, 0.f) && !spawnbox_health_floored(75.f, 20.f) && !spawnbox_health_floored(0.f, 0.f) &&
            !spawnbox_health_floored(75.f, -1.f), "no floor, or health above it: damageable");
}

static void tb_0329b_spawnbox_teleport() {
  CHECK_MSG(spawnbox_tele_signals(138, 0, 0) == SB_TELE_OP && spawnbox_tele_signals(0, 0, 0) == 0 &&
            spawnbox_tele_signals(137, 0, 0) == 0 && spawnbox_tele_signals(-1, -1, -1) == 0, "op 138 only");
  CHECK_MSG(spawnbox_tele_signals(0, 1, 0) == SB_TELE_FLAG && spawnbox_tele_signals(0, 0, 1) == SB_TELE_HIDDEN &&
            spawnbox_tele_signals(138, 1, 1) == (SB_TELE_OP | SB_TELE_FLAG | SB_TELE_HIDDEN), "flags and bits");
  SbTeles s;
  memset(&s, 0, sizeof s);
  int p[40];
  unsigned held = 0;
  CHECK_MSG(spawnbox_tele_note(&s, &p[0], SB_TELE_OP, 1000, &held) == SB_TELE_EV_START && spawnbox_tele_has(&s, &p[0], 1010),
            "started");
  CHECK_MSG(spawnbox_tele_note(&s, &p[0], SB_TELE_OP, 1500, &held) == SB_TELE_EV_NONE, "refreshed, no second start");
  CHECK_MSG(!spawnbox_tele_has(&s, &p[0], 1500 + SPAWNBOX_CUT_MS) && !spawnbox_tele_has(&s, &p[1], 1500), "stale ends");
  CHECK_MSG(spawnbox_tele_note(&s, &p[0], 0, 1700, &held) == SB_TELE_EV_END && held == 500 && s.n == 0, "ended, held 500");
  CHECK(spawnbox_tele_note(&s, &p[0], 0, 1800, &held) == SB_TELE_EV_NONE);
  for (int k = 0; k < SPAWNBOX_CUTS; k++) spawnbox_tele_note(&s, &p[k], SB_TELE_FLAG, 2000, &held);
  CHECK_MSG(spawnbox_tele_note(&s, &p[39], SB_TELE_FLAG, 2100, &held) == SB_TELE_EV_NONE && !spawnbox_tele_has(&s, &p[39], 2100),
            "full of fresh entries: the new one waits");
  CHECK_MSG(spawnbox_tele_note(&s, &p[39], SB_TELE_FLAG, 2000 + SPAWNBOX_CUT_MS, &held) == SB_TELE_EV_START, "full: a stale slot");
}

// Pawn.TakeDamage returns at once under Role < ROLE_Authority (4); the per-shot probe is capped per level and per pawn.
static void tb_0329d_spawnbox_role_probe() {
  CHECK_MSG(spawnbox_role_blocks(0) && spawnbox_role_blocks(3) && !spawnbox_role_blocks(4) && !spawnbox_role_blocks(-1) &&
            !spawnbox_role_blocks(5), "role < 4 only; -1 is unreadable");
  CHECK_MSG(spawnbox_tele_signals(0, 0, 0, spawnbox_role_blocks(2)) == SB_TELE_NODAMAGE, "same bit as bNoDamage");
  SbProbeCap c;
  memset(&c, 0, sizeof c);
  int p[40];
  int taken = 0;
  for (int k = 0; k < 10; k++) taken += spawnbox_probe_take(&c, &p[0]);
  CHECK_MSG(taken == SPAWNBOX_PROBE_PER_PAWN, "6 for one pawn");
  for (int k = 1; k < 40; k++) taken += spawnbox_probe_take(&c, &p[k]);
  CHECK_MSG(taken == SPAWNBOX_PROBE_PER_LEVEL && !spawnbox_probe_take(&c, &p[1]) && !spawnbox_probe_take(&c, 0), "24 a level");
}

// bNoDamage is a target exclusion after the teleport; the watch logs one line per change of the tuple, capped.
static void tb_0329c_spawnbox_nodamage_watch() {
  CHECK_MSG(spawnbox_tele_signals(0, 0, 0, 1) == SB_TELE_NODAMAGE && (SB_TELE_NODAMAGE & SB_TELE_MOVING) == 0, "own bit");
  CHECK_MSG(spawnbox_exclusion(0, 0, 0, 1) == SB_EXCL_NODAMAGE && spawnbox_exclusion(0, 0, 1, 1) == SB_EXCL_TELEPORT &&
            spawnbox_exclusion(0, 1, 0, 1) == SB_EXCL_NODAMAGE && spawnbox_exclusion(1, 0, 0, 1) == SB_EXCL_FAR,
            "no-damage last in precedence");
  SbTeles t;
  memset(&t, 0, sizeof t);
  int p[2];
  unsigned held = 0;
  spawnbox_tele_note(&t, &p[0], SB_TELE_NODAMAGE, 100, &held);
  CHECK_MSG((spawnbox_tele_sig(&t, &p[0], 150) & SB_TELE_NODAMAGE) && !spawnbox_tele_sig(&t, &p[1], 150), "sig read back");
  static SbWatches w;
  memset(&w, 0, sizeof w);
  SbTuple a = { 5, 0, 0, 0, 0, 0, 100 };
  unsigned ms = 0;
  int isNew = 0;
  int at = spawnbox_watch_begin(&w, &p[0], 1000, 0, &isNew);
  CHECK_MSG(at >= 0 && isNew && spawnbox_watch_change(&w, at, isNew, a, 1000, &ms) == 1 && ms == 0, "first sight logs");
  at = spawnbox_watch_begin(&w, &p[0], 1100, 0, &isNew);
  CHECK_MSG(!isNew && spawnbox_watch_change(&w, at, isNew, a, 1100, &ms) == 0, "same tuple: no line");
  a.nodamage = 1;
  CHECK_MSG(spawnbox_watch_change(&w, at, 0, a, 1200, &ms) == 1 && ms == 200, "a change logs, ms since first sight");
  CHECK_MSG(spawnbox_watch_begin(&w, &p[0], 7000, 0, &isNew) == -1, "6 s after first sight: closed");
  CHECK_MSG(spawnbox_watch_begin(&w, &p[0], 7100, 1, &isNew) >= 0, "an exclusion reopens it");
  CHECK_MSG(spawnbox_watch_begin(&w, &p[0], 13099, 0, &isNew) >= 0 && spawnbox_watch_begin(&w, &p[0], 13100, 0, &isNew) == -1,
            "6 s after the last exclusion");
  memset(&w, 0, sizeof w);
  at = spawnbox_watch_begin(&w, &p[1], 0, 0, &isNew);
  int lines = 0;
  for (int k = 0; k < 40; k++) {
    SbTuple b = { 5, 0, 0, 0, 0, 0, k };
    lines += spawnbox_watch_change(&w, at, k == 0, b, (unsigned)k, &ms);
  }
  CHECK_MSG(lines == SPAWNBOX_WATCH_LINES_PER_PAWN && w.lines == lines, "12 lines a pawn");
  w.lines = SPAWNBOX_WATCH_LINES_PER_LEVEL;
  w.w[at].lines = 0;
  SbTuple c = { 6, 0, 0, 0, 0, 0, 1 };
  CHECK_MSG(spawnbox_watch_change(&w, at, 0, c, 50, &ms) == 0, "60 lines a level");
}

// The player-damage nullifiers: DamageScaleInfo walked last to first, the neutral zone, their place in the precedence.
static void tb_0329h_spawnbox_dmgscale() {
  const SbDsEntry none[1] = { { 0, 0, 0, 0.f } };
  CHECK_MSG(spawnbox_dscale_verdict(none, 0) == SB_DS_OK && spawnbox_dscale_verdict(none, 1) == SB_DS_OK, "empty or no match: damageable");
  const SbDsEntry zero[2] = { { 1, 0, 0, 1.f }, { 1, 0, 0, 0.f } };
  CHECK_MSG(spawnbox_dscale_verdict(zero, 2) == SB_DS_ZERO, "the last full match decides: scale 0");
  const SbDsEntry last[2] = { { 1, 0, 0, 0.f }, { 1, 0, 0, 0.5f } };
  CHECK_MSG(spawnbox_dscale_verdict(last, 2) == SB_DS_OK, "an earlier zero is shadowed by a later match");
  const SbDsEntry skip[2] = { { 1, 0, 0, 0.f }, { 0, 0, 0, 1.f } };
  CHECK_MSG(spawnbox_dscale_verdict(skip, 2) == SB_DS_ZERO, "a class mismatch is walked past");
  const SbDsEntry typed[2] = { { 1, 0, 0, 0.f }, { 1, 2, 0, 0.f } };
  CHECK_MSG(spawnbox_dscale_verdict(typed, 2) == SB_DS_UNKNOWN, "a type-qualified match stops the walk, unknown");
  const SbDsEntry nott[1] = { { 1, 0, 1, 0.f } };
  CHECK_MSG(spawnbox_dscale_verdict(nott, 1) == SB_DS_UNKNOWN, "a not-type list counts too");
  const SbDsEntry neg[1] = { { 1, 0, 0, -1.f } };
  CHECK_MSG(spawnbox_dscale_verdict(neg, 1) == SB_DS_ZERO, "scale <= 0");
  CHECK_MSG(spawnbox_exclusion(0, 0, 0, 0, 1, 0) == SB_EXCL_PLAYERDMG0 && spawnbox_exclusion(0, 0, 0, 0, 0, 1) == SB_EXCL_NEUTRAL &&
            spawnbox_exclusion(0, 0, 0, 0, 1, 1) == SB_EXCL_PLAYERDMG0 && spawnbox_exclusion(0, 0, 0, 1, 1, 1) == SB_EXCL_NODAMAGE &&
            spawnbox_exclusion(0, 0, 1, 0, 1, 1) == SB_EXCL_TELEPORT && spawnbox_exclusion(0, 0, 0, 0, 0, 0) == SB_EXCL_NONE,
            "precedence: after bNoDamage, damage 0 before neutral");
  static SbDmgs d;
  memset(&d, 0, sizeof d);
  int a[2], due = 0;
  const int i = spawnbox_dmg_slot(&d, &a[0], 1000, 5, &due);
  CHECK_MSG(i >= 0 && due, "a new pawn is due");
  d.d[i].verdict = SB_DS_ZERO;
  d.d[i].neutral = 1;
  CHECK_MSG(spawnbox_dmg_slot(&d, &a[0], 1010, 6, &due) == i && !due, "not due the next frame");
  CHECK_MSG(spawnbox_dmg_slot(&d, &a[0], 1020, 5 + SPAWNBOX_DMG_REREAD, &due) == i && due, "due again after 30 frames");
  int v = -9, nz = -9;
  CHECK_MSG(spawnbox_dmg_get(&d, &a[0], 1030, &v, &nz) && v == SB_DS_ZERO && nz == 1 && !spawnbox_dmg_get(&d, &a[1], 1030, &v, &nz) &&
            !spawnbox_dmg_get(&d, &a[0], 1020 + SPAWNBOX_CUT_MS, &v, &nz), "read back while fresh");
}

// The body-point priority list and the point trace's clear rule (2026-10-05: a grunt showing its lower half only).
// The measured no-effect guard is gone (2026-10-10): it excluded damageable enemies. The exact rules rank as before.
static void tb_0329e_spawnbox_noeffect() {
  CHECK_MSG(spawnbox_exclusion(0, 0, 0, 0, 0, 1) == SB_EXCL_NEUTRAL && spawnbox_exclusion(0, 0, 0, 0, 1, 0) == SB_EXCL_PLAYERDMG0 &&
            spawnbox_exclusion(0, 0, 0, 1, 0, 0) == SB_EXCL_NODAMAGE && spawnbox_exclusion(0, 0, 1, 0, 0, 0) == SB_EXCL_TELEPORT &&
            spawnbox_exclusion(1, 0, 0, 0, 0, 0) == SB_EXCL_FAR && SB_EXCL_DOOMED == 7,
            "precedence without the no-effect reason; doomed is 7");
}

static void tb_0330_aim_point_order() {
  int o[AIM_PTS_MAX];
  CHECK_MSG(aim_point_order(AIM_AT_HEAD, 1, o) == 4 && o[0] == AIM_PT_HEAD && o[1] == AIM_PT_CHEST &&
            o[2] == AIM_PT_HIPS && o[3] == AIM_PT_KNEES, "Head: head, chest, hips, knees");
  CHECK_MSG(aim_point_order(AIM_AT_BODY, 1, o) == 4 && o[0] == AIM_PT_CHEST && o[1] == AIM_PT_HEAD &&
            o[2] == AIM_PT_HIPS && o[3] == AIM_PT_KNEES, "Body: chest first, then head, hips, knees");
  CHECK_MSG(aim_point_order(AIM_AT_HEAD, 0, o) == 3 && o[0] == AIM_PT_CHEST && o[1] == AIM_PT_HIPS &&
            o[2] == AIM_PT_KNEES, "no head bone: chest, hips, knees");
  CHECK(aim_point_order(AIM_AT_BODY, 0, o) == 3 && o[0] == AIM_PT_CHEST);
  const float loc[3] = { 10.f, 20.f, 100.f };
  float p[3];
  aim_body_point(AIM_PT_CHEST, loc, 50.f, p);
  CHECK_MSG(p[0] == 10.f && p[1] == 20.f && p[2] == 100.f, "chest: Location");
  aim_body_point(AIM_PT_HIPS, loc, 50.f, p);
  CHECK_MSG(p[0] == 10.f && p[2] == 75.f, "hips: half the height down");
  aim_body_point(AIM_PT_KNEES, loc, 50.f, p);
  CHECK_MSG(p[1] == 20.f && p[2] == 60.f, "knees: 0.8 of the height down");
  CHECK_MSG(aim_trace_clear(0, 0, 0) && aim_trace_clear(1, 1, 0) && aim_trace_clear(1, 0, 1), "clear");
  CHECK_MSG(!aim_trace_clear(1, 0, 0), "a door, a wall actor or another pawn: hidden");
  AimVisCache c;
  memset(&c, 0, sizeof c);
  int x = 0, pt = -1;
  aim_vis_put(&c, &x, 1000, AIM_VIS_YES, 0, AIM_PT_HIPS);
  CHECK_MSG(aim_vis_get(&c, &x, 1050, 0, &pt) == AIM_VIS_YES && pt == AIM_PT_HIPS, "the visible point is cached");
  CHECK(!strcmp(aim_pt_label(AIM_PT_HIPS), "hips"));
}

// The hidden point traces sorted by what they hit, first match wins.
static void tb_0331_aim_hit_category() {
  CHECK(aim_hit_category(1, 1, 1, 1, 1) == AIM_HIT_WORLD);
  CHECK(aim_hit_category(0, 1, 1, 1, 1) == AIM_HIT_SELF);
  CHECK(aim_hit_category(0, 0, 1, 1, 1) == AIM_HIT_TARGET_ATT);
  CHECK(aim_hit_category(0, 0, 0, 1, 1) == AIM_HIT_PAWN);
  CHECK(aim_hit_category(0, 0, 0, 0, 1) == AIM_HIT_DOOR);
  CHECK(aim_hit_category(0, 0, 0, 0, 0) == AIM_HIT_OTHER);
}

// The point trace uses the bullet's flags (TraceFire's 0x1080675 = STraceFlags 0x17 through
// Engine.dll!0x1027B730); a player-only blocker the bullet passes is counted as skipped.
static void tb_0332_aim_shot_trace() {
  CHECK_MSG(aim_shot_trace_flags() == 0x17u, "bTraceActors | bMeshAccurate | bShotTrace | bTraceTexture");
  CHECK_MSG(!(aim_shot_trace_flags() & AIM_TF_ONLY_BLOCKING), "not the player-blocking filter");
  CHECK_MSG(aim_blocker_skipped(1, 0) && !aim_blocker_skipped(1, 1) && !aim_blocker_skipped(0, 0) &&
            !aim_blocker_skipped(0, 1), "skipped: clear for the bullet, hidden for the old trace");
}

// freecam.h: the flight in Unreal axes (X forward at yaw 0, Y right, Z up). 100 % flies 8 m/s, Shift 4 times
// faster, Alt 4 times slower, both cancel; forward follows the pitch, strafe stays level, Space/Ctrl are world Z.
// 64 frames of 1/64 s are one second.
static bool fc_at(const FreecamPose& p, float x, float y, float z) {
  return fabsf(p.eye[0] - x) < 0.05f && fabsf(p.eye[1] - y) < 0.05f && fabsf(p.eye[2] - z) < 0.05f;
}
static FreecamPose fc_pose(float yaw, float pitch) {
  FreecamPose p = { { 0.f, 0.f, 0.f }, yaw, pitch };
  return p;
}
static void tb_0340_freecam_flight() {
  const float v = freecam_speed_uu(100);
  CHECK_MSG(fabsf(v - 8.f * 52.5f) < 1e-3f, "100 % is 8 m/s at 52.5 UU a metre");
  CHECK_MSG(freecam_speed_uu(0) == freecam_speed_uu(FREECAM_SPEED_MIN_PCT) &&
            freecam_speed_uu(9999) == freecam_speed_uu(FREECAM_SPEED_MAX_PCT), "clamped to the row's range");
  FreecamPose p = fc_pose(0.f, 0.f);
  for (int i = 0; i < 64; ++i) freecam_move(&p, FCK_FWD, v, 1.f / 64.f);
  CHECK_MSG(fc_at(p, 420.f, 0.f, 0.f), "64 steps of 1/64 s forward at yaw 0: 420 UU along +X");
  FreecamPose f = fc_pose(0.f, 0.f), s = fc_pose(0.f, 0.f), b = fc_pose(0.f, 0.f), o = fc_pose(0.f, 0.f);
  freecam_move(&f, FCK_FWD | FCK_FAST, v, 1.f);
  freecam_move(&s, FCK_FWD | FCK_SLOW, v, 1.f);
  freecam_move(&b, FCK_FWD | FCK_FAST | FCK_SLOW, v, 1.f);
  freecam_move(&o, FCK_FWD | FCK_BACK | FCK_LEFT | FCK_RIGHT | FCK_UP | FCK_DOWN, v, 1.f);
  CHECK_MSG(fc_at(f, 1680.f, 0.f, 0.f) && fc_at(s, 105.f, 0.f, 0.f), "Shift 4 times faster, Alt 4 times slower");
  CHECK_MSG(fc_at(b, 420.f, 0.f, 0.f), "both held cancel out");
  CHECK_MSG(fc_at(o, 0.f, 0.f, 0.f), "opposing keys cancel");
  FreecamPose y = fc_pose(90.f, 0.f), r = fc_pose(0.f, 0.f);
  freecam_move(&y, FCK_FWD, v, 1.f);
  freecam_move(&r, FCK_RIGHT, v, 1.f);
  CHECK_MSG(fc_at(y, 0.f, 420.f, 0.f), "yaw 90 looks down +Y");
  CHECK_MSG(fc_at(r, 0.f, 420.f, 0.f), "strafe right at yaw 0 is +Y");
  FreecamPose pf = fc_pose(0.f, 45.f), pr = fc_pose(0.f, 45.f), pu = fc_pose(0.f, 45.f);
  freecam_move(&pf, FCK_FWD, v, 1.f);
  freecam_move(&pr, FCK_RIGHT, v, 1.f);
  freecam_move(&pu, FCK_UP, v, 1.f);
  const float d = 420.f * 0.70710678f;
  CHECK_MSG(fc_at(pf, d, 0.f, d), "forward follows the pitch");
  CHECK_MSG(fc_at(pr, 0.f, 420.f, 0.f), "strafe stays level under a pitch");
  CHECK_MSG(fc_at(pu, 0.f, 0.f, 420.f), "Space rises along world Z whatever the pitch");
  CHECK_MSG(freecam_dt(0.5) == FREECAM_DT_MAX && freecam_dt(-1.0) == 0.f && fabsf(freecam_dt(0.016) - 0.016f) < 1e-6f,
            "a stall moves the camera at most a quarter second");
}

// freecam.h: DirectInput counts turn the camera (right turns right, up looks up), the pitch stops short of
// vertical, the yaw wraps; the engine's rotator units (65536 a turn) round-trip through the pose, roll dropped.
static void tb_0341_freecam_look_rotator() {
  FreecamPose p = fc_pose(170.f, 0.f);
  freecam_look(&p, 200, 0, FREECAM_SENS_DEG);
  CHECK_MSG(fabsf(p.yaw + 170.f) < 1e-3f, "the yaw wraps past 180");
  freecam_look(&p, 0, -2000, FREECAM_SENS_DEG);
  CHECK_MSG(p.pitch == FREECAM_PITCH_MAX, "the mouse moved up looks up, stopping short of vertical");
  freecam_look(&p, 0, 5000, FREECAM_SENS_DEG);
  CHECK(p.pitch == -FREECAM_PITCH_MAX);
  CHECK(fabsf(freecam_deg_from_rot(16384) - 90.f) < 1e-4f && fabsf(freecam_deg_from_rot(49152) + 90.f) < 1e-4f);
  CHECK(fabsf(freecam_deg_from_rot(-16384) + 90.f) < 1e-4f && fabsf(freecam_deg_from_rot(65536 + 100) -
                                                                      freecam_deg_from_rot(100)) < 1e-4f);
  CHECK_MSG(freecam_rot_from_deg(-90.f) == 49152 && freecam_rot_from_deg(90.f) == 16384 &&
            freecam_rot_from_deg(360.f) == 0, "written as 0..65535");
  const float loc[3] = { 1.f, -2.f, 3.f };
  const int rot[3] = { 64000, 16384, 1234 };
  const FreecamPose v = freecam_pose_from_view(loc, rot);
  CHECK(fc_at(v, 1.f, -2.f, 3.f) && fabsf(v.yaw - 90.f) < 1e-4f && fabsf(v.pitch + 8.4375f) < 1e-4f);
  float l2[3];
  int r2[3];
  freecam_view_from_pose(v, l2, r2);
  CHECK_MSG(l2[0] == 1.f && l2[1] == -2.f && l2[2] == 3.f && r2[0] == 64000 && r2[1] == 16384 && r2[2] == 0,
            "the engine's view round-trips, roll dropped");
  const int up[3] = { 16384, 0, 0 };
  CHECK_MSG(freecam_pose_from_view(loc, up).pitch == FREECAM_PITCH_MAX, "a vertical seed is clamped");
}

// freecam.h: Pause the game holds only a pause it made, waits out the game's own, pauses again when the game's menu
// or a focus resume unpauses under it, lets go on a level change, and gives up on a level that refuses SetPause.
static void tb_0342_freecam_pause() {
  FcPause s = {};
  CHECK_MSG(fcp_step(&s, 1, 7, 0) == FCP_PAUSE && s.held, "the flight starts on a running game: paused");
  CHECK(fcp_step(&s, 1, 7, 1) == FCP_NONE && s.held);
  CHECK_MSG(fcp_step(&s, 0, 7, 1) == FCP_UNPAUSE && !s.held, "camera off mid-pause: our pause undone");
  CHECK(fcp_step(&s, 0, 7, 0) == FCP_NONE);
  FcPause g = {};
  CHECK_MSG(fcp_step(&g, 1, 7, 1) == FCP_NONE && !g.held, "the game's own pause is never taken over");
  CHECK_MSG(fcp_step(&g, 0, 7, 1) == FCP_NONE, "nor undone");
  CHECK(fcp_step(&g, 1, 7, 1) == FCP_NONE && fcp_step(&g, 1, 7, 0) == FCP_PAUSE);
  CHECK(fcp_step(&g, 1, 7, 1) == FCP_NONE);
  CHECK_MSG(fcp_step(&g, 1, 7, 0) == FCP_REPAUSE && g.held, "the game unpaused under us: paused again");
  CHECK(fcp_step(&g, 1, 7, 1) == FCP_NONE);
  FcPause m = {};
  fcp_step(&m, 1, 7, 0);
  fcp_step(&m, 1, 7, 1);
  CHECK_MSG(fcp_step(&m, 1, 8, 0) == FCP_LET_GO && !m.held && m.blocked, "a level change: the pause went with it");
  CHECK_MSG(fcp_step(&m, 1, 8, 0) == FCP_NONE, "and none is made again in this flight");
  CHECK(fcp_step(&m, 0, 8, 0) == FCP_NONE && fcp_step(&m, 1, 8, 0) == FCP_PAUSE);
  FcPause r = {};
  fcp_step(&r, 1, 7, 0);
  CHECK_MSG(fcp_step(&r, 1, 7, 0) == FCP_REFUSED && !r.held, "a level that refuses SetPause: one call");
  CHECK(fcp_step(&r, 1, 7, 0) == FCP_NONE && fcp_step(&r, 0, 7, 0) == FCP_NONE);
  FcPause l = {};
  fcp_step(&l, 1, 7, 0);
  fcp_step(&l, 1, 7, 1);
  CHECK_MSG(fcp_step(&l, 0, 7, 0) == FCP_LET_GO && !l.held, "unpaused by the game when the flight ends: let go");
  FcPause z = {};
  CHECK_MSG(fcp_step(&z, 1, 0, 0) == FCP_NONE && fcp_step(&z, 0, 7, 0) == FCP_NONE, "no level, or the row off");
}

// freecam.h: the paused-frame cache is off for the whole flight, the HUD only in Screenshot mode; every way a
// flight ends (switch off, no player, level change) gives the game's values back through cvars.h. A focus loss no
// longer ends it (tb_0362).
static void tb_0343_freecam_cvars_exits() {
  CHECK(freecam_cvar_forced(FC_CVAR_FRAMECACHE, 1, 0) && !freecam_cvar_forced(FC_CVAR_FRAMECACHE, 0, 1));
  CHECK(freecam_cvar_forced(FC_CVAR_DRAWHUD, 1, 1) && !freecam_cvar_forced(FC_CVAR_DRAWHUD, 1, 0) &&
        !freecam_cvar_forced(FC_CVAR_DRAWHUD, 0, 1));
  CHECK_MSG(freecam_end_reason(1, 1, 1, 0) == FCE_NONE && freecam_end_reason(0, 0, 0, 1) == FCE_NONE,
            "a flight in progress goes on; no flight, nothing to end");
  const int ends[3][3] = { { 0, 1, 0 }, { 1, 0, 0 }, { 1, 1, 1 } };
  const int want[3] = { FCE_SWITCH_OFF, FCE_NO_PLAYER, FCE_LEVEL };
  for (int i = 0; i < 3; ++i) {
    CHECK_MSG(freecam_end_reason(1, ends[i][0], ends[i][1], ends[i][2]) == want[i], "each exit path");
    CHECK(freecam_end_text(want[i])[0] != 0);
    for (int c = 0; c < FC_CVAR_COUNT; ++c) {
      CvarHold h = cvar_hold_init();
      int w = -1;
      CHECK(cvar_step(&h, freecam_cvar_forced(c, 1, 1), 1, 0, &w) == CVAR_FORCE && w == 0);
      CHECK_MSG(cvar_step(&h, freecam_cvar_forced(c, 0, 1), 0, 0, &w) == CVAR_RESTORE && w == 1 && !h.held,
                "the flight ended: the game's value back");
    }
  }
}

// freecam.h: the camera's own keys and the Game speed key refuse the eight flight keys; other rows and keys bind.
static void tb_0344_freecam_bind_refused() {
  const int rows[4] = { BIND_ROW_CAMERA, BIND_ROW_CAMCOPY, BIND_ROW_CAMPASTE, BIND_ROW_GAMESPEED };
  int refused = 0;
  for (int r = 0; r < 4; ++r)
    for (int i = 0; i < 8; ++i) refused += freecam_bind_refused(rows[r], FREECAM_MOVE_VK[i]);
  CHECK_MSG(refused == 32, "W/A/S/D, Space, Ctrl, Shift and Alt refused on every camera row");
  CHECK(FREECAM_MOVE_VK[0] == 'W' && FREECAM_MOVE_VK[4] == 0x20 && FREECAM_MOVE_VK[5] == 0x11 &&
        FREECAM_MOVE_VK[7] == 0x12);
  CHECK_MSG(!freecam_bind_refused(BIND_ROW_MENU, 'W') && !freecam_bind_refused(BIND_ROW_AIMBOT, 0x20),
            "rows that do not fly the camera keep any key");
  CHECK(!freecam_bind_refused(BIND_ROW_CAMERA, 'V') && !freecam_bind_refused(BIND_ROW_CAMCOPY, 0x7B) &&
        !freecam_bind_refused(BIND_ROW_CAMPASTE, VK_BIND_XBUTTON2));
}

// freecam.h: the Copy view line is our own tag in Unreal units and degrees; a paste takes it from any text and
// refuses DNMP's and Megaton's lines, a missing field, a non-finite or out-of-range number, leaving the pose as it was.
static void tb_0345_freecam_view_text() {
  FreecamPose p = { { 100.5f, -2000.5f, 64.f }, -90.5f, 12.25f };
  char line[128];
  CHECK(campos_format(p, line, sizeof line));
  CHECK_MSG(strcmp(line, "dnf cam x=100.5 y=-2000.5 z=64.0 yaw=-90.50 pitch=12.25") == 0, "the line");
  FreecamPose q = fc_pose(0.f, 0.f);
  CHECK_MSG(campos_accept(line, &q) && fc_at(q, 100.5f, -2000.5f, 64.f) && q.yaw == -90.5f && q.pitch == 12.25f,
            "round trip");
  CHECK(campos_accept("seen: dnf cam x=1 y=2 z=3 yaw=270 pitch=-5 (here)", &q) && fc_at(q, 1.f, 2.f, 3.f) &&
        q.yaw == -90.f && q.pitch == -5.f);
  static const char* const kBad[] = { "dnmp cam x=1 y=2 z=3 yaw=4 pitch=5", "cam x=1 y=2 z=3 ang=4 pitch=5",
                                      "dnf cam x=1 y=2 z=3 yaw=4", "dnf cam x=1 y=2 z=3 yaw=4 pitch=95",
                                      "dnf cam x=1e9 y=2 z=3 yaw=4 pitch=5", "dnf cam x=nan y=2 z=3 yaw=4 pitch=5",
                                      "dnf cam x=1 y=2 z=3 yaw=400 pitch=5", "" };
  int took = 0;
  for (int i = 0; i < (int)(sizeof kBad / sizeof kBad[0]); ++i) took += campos_accept(kBad[i], &q);
  CHECK_MSG(took == 0 && fc_at(q, 1.f, 2.f, 3.f) && q.yaw == -90.f, "refused, the pose untouched");
  char cut[10];
  CHECK_MSG(!campos_format(p, cut, sizeof cut) && cut[0] == 0, "half a line is no line");
  char ro[96];
  const FreecamPose r = { { 12.f, -3.f, 400.f }, 45.25f, -10.f };
  freecam_readout(r, ro, sizeof ro);
  CHECK_MSG(strcmp(ro, "cam x=12 y=-3 z=400 yaw=45.2 pitch=-10.0") == 0 || strcmp(ro, "cam x=12 y=-3 z=400 yaw=45.3 "
                                                                                  "pitch=-10.0") == 0, "the readout");
}

// config.cpp: DNMP's [freecam] keys and defaults; the three key rows appended after the Aimbot button.
static void tb_0346_freecam_config() {
  config_defaults();
  CHECK_MSG(g_cfg.freecamOn == 1 && g_cfg.freecamSpeedPct == FREECAM_SPEED_DEF_PCT && g_cfg.freecamPause == 0 &&
            g_cfg.freecamMarker == 1 && g_cfg.freecamDebug == 0 && g_cfg.freecamShot == 0, "DNMP's defaults");
  CHECK(FREECAM_SPEED_DEF_PCT == 100 && FREECAM_SPEED_MIN_PCT == 1 && FREECAM_SPEED_MAX_PCT == 500);
  CHECK_MSG(BIND_ROW_CAMERA == 3 && BIND_ROW_CAMCOPY == 4 && BIND_ROW_CAMPASTE == 5 && BIND_ROW_COUNT >= 6,
            "appended, never inserted");
  CHECK_MSG(config_bind_get(BIND_ROW_CAMERA) == 0x56 && config_bind_get(BIND_ROW_CAMCOPY) == 0x42 &&
            config_bind_get(BIND_ROW_CAMPASTE) == 0x4E, "V, B and N");
  CHECK(strcmp(bind_label(0x56), "V") == 0 && strcmp(bind_label(0x42), "B") == 0 && strcmp(bind_label(0x4E), "N") == 0);
  static const char* const kKeys[] = { "on", "key", "speedpct", "pause", "marker", "debug", "shot", "copy", "paste" };
  int known = 0;
  for (int i = 0; i < 9; ++i) known += config_key_known("freecam", kKeys[i]);
  CHECK_MSG(known == 9, "DNMP's [freecam] keys");
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_freecam");
  write_text(path, "[freecam]\r\nspeedpct=0\r\nshot=1\r\nkey=66\r\n");
  config_load_from(path);
  CHECK_MSG(g_cfg.freecamSpeedPct == FREECAM_SPEED_DEF_PCT && g_cfg.freecamShot == 1 &&
            config_bind_get(BIND_ROW_CAMERA) == 66, "out of range falls back; the rest load");
  DeleteFileA(path);
  config_bind_set(BIND_ROW_CAMPASTE, 0x7B);
  CHECK(g_cfg.camPasteKey == 0x7B);
  config_defaults();
}

// pehook.h: before and after subscribers on one event, each found only in its own phase.
static void tb_0347_pe_after_phase() {
  PeSub s[3] = { { 77, 1, 0 }, { 77, 1, 1 }, { 77, 0, 1 } };
  CHECK(pe_next_phase(s, 3, 77, 0, 0) == 0 && pe_next_phase(s, 3, 77, 1, 0) == -1);
  CHECK_MSG(pe_next_phase(s, 3, 77, 0, 1) == 1 && pe_next_phase(s, 3, 77, 2, 1) == -1, "after, enabled only");
  CHECK_MSG(pe_next(s, 3, 77, 0) == 0 && pe_next(s, 3, 77, 1) == 1, "pe_next sees both phases");
  CHECK(pe_next_phase(s, 3, 0, 0, 1) == -1 && PE_MAX_SUBS >= 9);
}

// cursorgate.h and dimouse.h: in flight GetKeyState reads 0 and the engine's mouse read is real (the camera's
// counts) but hands the engine zeros; the armed Camera key is hidden from the game always.
static void tb_0348_flight_gate() {
  CHECK_MSG(keystate_passes('W', false, false, 0x56) && !keystate_passes(0x56, false, false, 0x56),
            "the Camera key never reaches the game");
  CHECK_MSG(!keystate_passes('W', false, true, 0) && !keystate_passes('W', true, false, 0), "flying or open: 0");
  CHECK(keystate_passes(0x56, false, false, 0));
  DmState d = dm_init();
  int dev = 0, other = 0;
  dm_on_created(&d, &dev);
  dm_set_kind(&d, &dev, DM_KIND_MOUSE);
  CHECK_MSG(dm_on_getstate(&d, &dev) == 1 && dm_getstate_hides(&d, &dev, 1) && !dm_getstate_hides(&d, &dev, 0),
            "flying: a real read, zeros for the engine");
  CHECK(!dm_getstate_hides(&d, &other, 1));
  dm_on_open(&d, DM_SEEN_UNKNOWN);
  CHECK_MSG(dm_on_getstate(&d, &dev) == 0 && !dm_getstate_hides(&d, &dev, 1), "open: no read at all");
  CHECK_MSG(freecam_steers(1, 1, 0, 1) && !freecam_steers(1, 0, 0, 1) && !freecam_steers(1, 1, 1, 1) &&
            !freecam_steers(1, 1, 0, 0) && !freecam_steers(0, 1, 0, 1), "steers: seeded, panel shut, in front");
}

// teleport.h: a floor hit stands Duke on it; a wall or ceiling hit steps his cylinder off it and needs a floor below
// (the sky and a sheer wall are refused); a hit normal that is not a unit vector facing the ray is refused.
static bool tp_at(const TpPlace& p, float x, float y, float z) {
  return fabsf(p.pos[0] - x) < 1e-3f && fabsf(p.pos[1] - y) < 1e-3f && fabsf(p.pos[2] - z) < 1e-3f;
}
static void tb_0349_teleport_place() {
  const float at[3] = { 100.f, 200.f, 50.f }, down[3] = { 0.f, 0.f, -1.f }, fwd[3] = { 1.f, 0.f, 0.f };
  const float up[3] = { 0.f, 0.f, 1.f }, wall[3] = { -1.f, 0.f, 0.f }, half[3] = { 0.f, 0.f, 0.5f };
  const float r = 20.f, h = 45.f;
  CHECK_MSG(tp_place(0, at, up, down, r, h).reason == TP_NO_HIT, "the sky: the ray hit nothing");
  CHECK_MSG(tp_place(1, at, half, down, r, h).reason == TP_BAD_NORMAL, "not a unit normal");
  CHECK_MSG(tp_place(1, at, up, up, r, h).reason == TP_BAD_NORMAL, "a normal facing away from the ray");
  const TpPlace f = tp_place(1, at, up, down, r, h);
  CHECK_MSG(f.reason == TP_OK && !f.drop && tp_at(f, 100.f, 200.f, 50.f + h + TP_MARGIN), "a floor: stood on it");
  const TpPlace w = tp_place(1, at, wall, fwd, r, h);
  CHECK_MSG(w.reason == TP_OK && w.drop && tp_at(w, 100.f - r - TP_MARGIN, 200.f, 50.f), "a wall: stepped off it");
  CHECK_MSG(tp_drop(w, 0, at, up, h).reason == TP_NO_FLOOR, "a wall with no floor below: refused");
  CHECK_MSG(tp_drop(w, 1, at, wall, h).reason == TP_NO_FLOOR, "a sheer drop onto another wall: refused");
  const float floorAt[3] = { 79.f, 200.f, -30.f };
  const TpPlace d = tp_drop(w, 1, floorAt, up, h);
  CHECK_MSG(d.reason == TP_OK && !d.drop && tp_at(d, 79.f, 200.f, -30.f + h + TP_MARGIN), "the floor below the wall");
  CHECK_MSG(tp_drop(f, 0, at, up, h).reason == TP_OK && tp_at(tp_drop(f, 0, at, up, h), 100.f, 200.f, 95.f + TP_MARGIN),
            "a floor placement needs no drop");
  const float ceil[3] = { 0.f, 0.f, -1.f };
  const TpPlace c = tp_place(1, at, ceil, up, r, h);
  CHECK_MSG(c.reason == TP_OK && c.drop && tp_at(c, 100.f, 200.f, 50.f - h - TP_MARGIN), "a ceiling: below it, then down");
}

// teleport.h: the refusal order (mounted first: turret, vehicle, ladder), then the cast, then FindSpot's fit; the
// marker is red for every refusal. config.cpp: DNMP's Teleport key (T) and marker (on), appended after Paste view.
static void tb_0350_teleport_verdict() {
  CHECK_MSG(tp_verdict(TP_OK, 1, 1) == TP_MOUNTED && tp_verdict(TP_NO_HIT, 1, 0) == TP_MOUNTED, "never while mounted");
  CHECK(tp_verdict(TP_NO_FLOOR, 0, 1) == TP_NO_FLOOR && tp_verdict(TP_OK, 0, 0) == TP_NO_ROOM);
  CHECK(tp_verdict(TP_OK, 0, 1) == TP_OK);
  int red = 0;
  for (int i = 0; i < TP_REASON_COUNT; ++i) {
    red += tp_marker_red(i);
    CHECK(tp_reason_text(i)[0] != 0);
  }
  CHECK_MSG(red == TP_REASON_COUNT - 1 && !tp_marker_red(TP_OK), "red for every refusal");
  config_defaults();
  CHECK_MSG(BIND_ROW_TELEPORT == 6 && config_bind_get(BIND_ROW_TELEPORT) == 0x54 && g_cfg.teleportMarker == 1,
            "T, marker on");
  CHECK(config_key_known("freecam", "teleport") && config_key_known("freecam", "teleportmarker"));
  int refused = 0;
  for (int i = 0; i < 8; ++i) refused += freecam_bind_refused(BIND_ROW_TELEPORT, FREECAM_MOVE_VK[i]);
  CHECK_MSG(refused == 8, "the Teleport key refuses the flight keys");
}

// photo.h: the tile grid is the engine's own MegaShot loop (TakeMegaShot 0x10115910): row r, top first, has
// YShift 1-N+2r, column c has XShift N-1-2c, MegaShotScale N; outside a tile the cvars are the engine's 1, 0, 0.
static void tb_0351_photo_tiles() {
  const float xs[4] = { 3.f, 1.f, -1.f, -3.f }, ys[4] = { -3.f, -1.f, 1.f, 3.f };
  int same = 0;
  for (int i = 0; i < 4; ++i) same += photo_shift_x(4, i) == xs[i] && photo_shift_y(4, i) == ys[i];
  CHECK_MSG(same == 4, "the engine's shift steps");
  CHECK_MSG(photo_shift_x(1, 0) == 0.f && photo_shift_y(1, 0) == 0.f, "one tile is the plain view");
  PhotoSm s = photo_begin(3);
  s.phase = PH_TILE;
  s.r = 2;
  s.c = 1;
  int scale = 0;
  float x = 9.f, y = 9.f;
  photo_megashot(s, &scale, &x, &y);
  CHECK_MSG(scale == 3 && x == 0.f && y == 2.f, "tile (2, 1) of 3");
  const int idle[3] = { PH_IDLE, PH_ARMING, PH_DRAIN };
  int plain = 0;
  for (int i = 0; i < 3; ++i) {
    s.phase = idle[i];
    photo_megashot(s, &scale, &x, &y);
    plain += scale == 1 && x == 0.f && y == 0.f;
  }
  CHECK_MSG(plain == 3, "no tile up: the engine's own values");
  s.phase = PH_WAIT;
  photo_megashot(s, &scale, &x, &y);
  CHECK_MSG(scale == 3, "between rows the scale holds, so the exposure stays frozen (review m1)");
  CHECK(photo_mult_clamped(1) == PHOTO_MULT_MIN && photo_mult_clamped(99) == PHOTO_MULT_MAX &&
        photo_mult_clamped(5) == 5 && PHOTO_MULT_DEF == 4);
  CHECK_MSG(photo_cvar_forced(PH_ARMING) && photo_cvar_forced(PH_TILE) && photo_cvar_forced(PH_WAIT) &&
            !photo_cvar_forced(PH_DRAIN) && !photo_cvar_forced(PH_IDLE), "the engine's six zeroed until the last tile");
  CHECK(freecam_cvar_forced(FC_CVAR_DRAWHUD, 1, 0, 1) && !freecam_cvar_forced(FC_CVAR_DRAWHUD, 0, 0, 1) &&
        freecam_cvar_forced(FC_CVAR_FRAMECACHE, 1, 0, 0));
}

// photo.h: a tile (BGRA rows, top first, as the back buffer reads) lands in its column of a 24-bit strip whose rows
// are padded to 4 bytes, split in chunks; an all-black tile is reported.
static void tb_0352_photo_rows() {
  CHECK(photo_stride(1) == 4 && photo_stride(4) == 12 && photo_stride(5) == 16);
  const unsigned char tile[2 * 2 * 4] = { 1, 2, 3, 0xFF, 4, 5, 6, 0xFF, 7, 8, 9, 0xFF, 10, 11, 12, 0xFF };
  unsigned char strip[2 * 16];
  memset(strip, 0xEE, sizeof strip);
  CHECK(photo_copy_bgra(tile, 8, 2, 2, strip, 16, 1));
  CHECK_MSG(strip[6] == 1 && strip[7] == 2 && strip[8] == 3 && strip[9] == 4 && strip[11] == 6,
            "row 0 into column 1, B G R");
  CHECK_MSG(strip[16 + 6] == 7 && strip[16 + 11] == 12 && strip[0] == 0xEE && strip[16 + 12] == 0xEE,
            "row 1 a stride below; column 0 and the padding untouched");
  const unsigned char black[4 * 4] = { 0, 0, 0, 0xFF, 0, 0, 0, 0xFF, 0, 0, 0, 0xFF, 0, 0, 0, 0xFF };
  CHECK_MSG(!photo_copy_bgra(black, 8, 2, 2, strip, 16, 0), "a black tile is reported");
  const unsigned stride = photo_stride(30720);
  const int rows = photo_chunk_rows(stride, 1080);
  CHECK_MSG(rows * stride <= PHOTO_CHUNK_BYTES && photo_chunk_count(1080, rows) <= PHOTO_CHUNKS_MAX, "16 MB chunks");
  CHECK(photo_chunk_rows_in(photo_chunk_count(1080, rows) - 1, 1080, rows) == 1080 - (photo_chunk_count(1080, rows) - 1) * rows);
  CHECK(photo_chunk_rows(4, 3) == 3 && photo_chunk_count(0, 5) == 0);
}

// photofolder.h and config.cpp: the Photo folder is a string key, "" for <Pictures>\Nuke Panel; shown in ASCII and
// shortened from the left. photo.h: the file name, and DNMP's Photo key (F12) and Photo size (4x) rows.
static int tb_measure(const char* s, void*) { return (int)strlen(s); }
static void tb_0353_photo_folder_config() {
  CHECK(photo_folder_is_default("") && photo_folder_is_default(0) && !photo_folder_is_default("Shots"));
  char out[64];
  photo_folder_display("Sh\xC3\xB6ts\\", out, sizeof out);
  CHECK_MSG(strcmp(out, "Sh?ts") == 0, "non-ASCII as '?', no trailing separator");
  photo_folder_shorten("Users\\Me\\Pictures\\Nuke Panel", 16, tb_measure, 0, out, sizeof out);
  CHECK_MSG(strcmp(out, "...\\Nuke Panel") == 0, "whole leading components go first");
  char name[128];
  CHECK(photo_file_name(name, sizeof name, "map:01?", 2026, 10, 7, 9, 5, 3, 7680, 4320) > 0 &&
        strcmp(name, "map_01_ 2026-10-07 09-05-03 7680x4320.png") == 0);
  CHECK(photo_file_name(name, 8, "level", 2026, 1, 1, 0, 0, 0, 1, 1) < 0 && name[0] == 0);
  photo_size_text(4, 1920, 1080, out, sizeof out);
  CHECK(strcmp(out, "4x 7680x4320") == 0);
  config_defaults();
  CHECK_MSG(g_cfg.photoFolder[0] == 0 && g_cfg.photoMult == PHOTO_MULT_DEF && config_bind_get(BIND_ROW_PHOTO) == 0x7B &&
            BIND_ROW_PHOTO == 7, "DNMP's defaults, the row appended");
  CHECK(config_key_known("freecam", "photo") && config_key_known("freecam", "photosize") &&
        config_key_known("freecam", "photofolder"));
  int refused = 0;
  for (int i = 0; i < 8; ++i) refused += freecam_bind_refused(BIND_ROW_PHOTO, FREECAM_MOVE_VK[i]);
  CHECK_MSG(refused == 8, "the Photo key refuses the flight keys");
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_photo");
  write_text(path, "[freecam]\r\nphotofolder=\"Shots\\Duke\"\r\nphotosize=99\r\n");
  config_load_from(path);
  CHECK_MSG(strcmp(g_cfg.photoFolder, "Shots\\Duke") == 0 && g_cfg.photoMult == PHOTO_MULT_DEF,
            "a quoted folder loads; an out-of-range size falls back");
  std::string longer = "[freecam]\r\nphotofolder=";
  longer.append(300, 'x');
  write_text(path, longer.c_str());
  config_load_from(path);
  CHECK_MSG(g_cfg.photoFolder[0] == 0, "a folder over the cap is refused, not cut");
  DeleteFileA(path);
  config_defaults();
}

// photo.h: the capture's steps: the key starts it, the world must settle frozen, each read tile moves on, a row's
// end hands its strip over and waits for a free one, the encoder ends it; the key again, the camera ending or the
// panel opening aborts it.
static PhotoIn ph_in(int frozen) {
  PhotoIn in;
  memset(&in, 0, sizeof in);
  in.cam = in.world = in.focus = in.sameSize = 1;
  in.frozen = frozen;
  return in;
}
static void tb_0354_photo_steps_cancel() {
  PhotoSm s = photo_begin(2);
  PhotoIn in = ph_in(1);
  in.press = 1;
  CHECK_MSG(photo_step(&s, in) == PA_START && s.phase == PH_ARMING, "the key starts it");
  in.press = 0;
  for (int i = 0; i < PHOTO_SETTLE_FRAMES; ++i) photo_step(&s, in);
  CHECK_MSG(s.phase == PH_TILE && s.r == 0 && s.c == 0, "settled frozen: the first tile");
  CHECK(photo_step(&s, in) == PA_NONE && s.c == 0);
  in.tileRead = 1;
  CHECK(photo_step(&s, in) == PA_NONE && s.c == 1);
  CHECK_MSG(photo_step(&s, in) == PA_FLUSH && s.flushRow == 0 && s.phase == PH_WAIT, "a row's strip handed over");
  in.tileRead = 0;
  CHECK(photo_step(&s, in) == PA_NONE && s.phase == PH_WAIT);
  in.bufferFree = 1;
  CHECK(photo_step(&s, in) == PA_NONE && s.phase == PH_TILE && s.r == 1);
  in.bufferFree = 0;
  in.tileRead = 1;
  photo_step(&s, in);
  CHECK(photo_step(&s, in) == PA_FLUSH && s.flushRow == 1 && s.phase == PH_DRAIN);
  CHECK(photo_step(&s, in) == PA_NONE);
  in.encDone = 1;
  CHECK_MSG(photo_step(&s, in) == PA_FINISH && s.phase == PH_IDLE, "the encoder done: finished");
  PhotoSm c = photo_begin(2);
  PhotoIn k = ph_in(1);
  k.press = 1;
  photo_step(&c, k);
  CHECK_MSG(photo_step(&c, k) == PA_ABORT && c.reason == PHR_CANCELLED && c.phase == PH_IDLE, "the key again cancels");
  PhotoSm e = photo_begin(2);
  PhotoIn m = ph_in(1);
  m.press = 1;
  photo_step(&e, m);
  m.press = 0;
  m.cam = 0;
  CHECK_MSG(photo_step(&e, m) == PA_ABORT && e.reason == PHR_CAMERA, "the camera ended");
  PhotoSm p = photo_begin(2);
  PhotoIn q = ph_in(0);
  q.press = 1;
  photo_step(&p, q);
  q.press = 0;
  p.arm = PHOTO_ARM_TIMEOUT - 3;   // the wait's end, not the whole wait (loops capped at 64)
  int act = PA_NONE;
  for (int i = 0; i < 4 && act == PA_NONE; ++i) act = photo_step(&p, q);
  CHECK_MSG(act == PA_ABORT && p.reason == PHR_NOT_FROZEN, "a world that never froze");
  PhotoSm t = photo_begin(2);
  PhotoIn u = ph_in(1);
  u.press = 1;
  photo_step(&t, u);
  u.press = 0;
  u.panel = 1;
  CHECK(photo_step(&t, u) == PA_ABORT && t.reason == PHR_PANEL && photo_why_text(PHR_PANEL)[0]);
  PhotoSm i0 = photo_begin(2);
  PhotoIn v = ph_in(1);
  v.press = 1;
  v.cam = 0;
  CHECK_MSG(photo_step(&i0, v) == PA_NONE && i0.phase == PH_IDLE, "not flying: the key does nothing");
}

// freecam.h: a world fault drops the player for a while with the level still loaded and the serial
// unchanged. Our pause stays held through it, and is undone through the player once it is back; only a real level
// change (a new serial) lets it go.
static void tb_0355_freecam_pause_world_drop() {
  FcPause s = {};
  CHECK(fcp_step(&s, 1, 7, 1, 0) == FCP_PAUSE && fcp_step(&s, 1, 7, 1, 1) == FCP_NONE);
  CHECK_MSG(fcp_step(&s, 1, 7, 0, 0) == FCP_NONE && s.held, "the player not read: still ours, nothing called");
  CHECK_MSG(fcp_step(&s, 0, 7, 0, 0) == FCP_NONE && s.held, "the flight ended meanwhile: still held");
  CHECK_MSG(fcp_step(&s, 0, 7, 1, 1) == FCP_UNPAUSE && !s.held, "the player back: unpaused through it");
  FcPause l = {};
  fcp_step(&l, 1, 7, 1, 0);
  fcp_step(&l, 1, 7, 1, 1);
  fcp_step(&l, 0, 7, 0, 0);
  CHECK_MSG(fcp_step(&l, 0, 8, 1, 0) == FCP_LET_GO && !l.held, "a new level: the pause went with the old one");
  FcPause r = {};
  fcp_step(&r, 1, 7, 1, 0);
  fcp_step(&r, 1, 7, 1, 1);
  CHECK_MSG(fcp_step(&r, 1, 7, 1, 0) == FCP_REPAUSE, "the five-argument step keeps the re-pause");
}

// photo.h: the two strips in flight stay within 256 MB in this 32-bit process. A size past it is refused
// before anything is allocated, and the Photo size row's maximum is the largest that fits the screen.
static void tb_0356_photo_budget() {
  CHECK(PHOTO_BUDGET_BYTES == 256ull << 20);
  CHECK_MSG(photo_budget_ok(3440, 1440, 9) && !photo_budget_ok(3440, 1440, 10), "3440x1440: 9x fits, 10x does not");
  CHECK_MSG(photo_mult_max(3440, 1440) == PHOTO_MULT_MAX, "the user's screen: 16x in bands (tb_0412)");
  CHECK(photo_mult_max(3840, 2160) == PHOTO_MULT_MAX && photo_mult_max(1920, 1080) == PHOTO_MULT_MAX);
  CHECK_MSG(photo_mult_max(20000, 20000) == 0, "not even 2x fits: none");
  CHECK_MSG(photo_mult_max(0, 0) == PHOTO_MULT_MAX, "screen not known yet: no cap");
  char why[64];
  photo_too_large_text(9, why, sizeof why);
  CHECK(strcmp(why, "Too large for this screen: max 9x.") == 0);
  photo_too_large_text(0, why, sizeof why);
  CHECK(strcmp(why, "Too large for this screen: even 2x does not fit.") == 0);
}

// freecam.h: No distance fog forces r_DrawDistanceFog 0 only while flying with the row on (HandleFogDeferred
// 0x1032D4A0 draws the fog tile only while it is set), and every way the flight ends gives the game's value back.
static void tb_0357_freecam_no_fog() {
  CHECK_MSG(freecam_cvar_forced(FC_CVAR_FOG, 1, 0, 0, 1) && !freecam_cvar_forced(FC_CVAR_FOG, 1, 1, 1, 0) &&
            !freecam_cvar_forced(FC_CVAR_FOG, 0, 0, 0, 1), "flying with the row on only");
  CHECK_MSG(freecam_cvar_forced(FC_CVAR_DRAWHUD, 1, 1, 0, 0) && freecam_cvar_forced(FC_CVAR_FRAMECACHE, 1, 0, 0, 0) &&
            !freecam_cvar_forced(FC_CVAR_DRAWHUD, 1, 0, 0, 1), "the row leaves the other two alone");
  CHECK(FC_CVAR_ALL > FC_CVAR_FOG && freecam_cvar_name(FC_CVAR_FOG)[0] == L'r');
  CvarHold h = cvar_hold_init();
  int w = -1;
  CHECK(cvar_step(&h, freecam_cvar_forced(FC_CVAR_FOG, 1, 0, 0, 1), 1, 0, &w) == CVAR_FORCE && w == 0);
  CHECK_MSG(cvar_step(&h, freecam_cvar_forced(FC_CVAR_FOG, 0, 0, 0, 1), 0, 0, &w) == CVAR_RESTORE && w == 1,
            "the flight ended: the fog back");
  config_defaults();
  CHECK_MSG(g_cfg.freecamNoFog == 0 && config_key_known("freecam", "nofog"), "off by default");
}

// freecam.h: UD3DRenderDevice9::Lock (D3DDrv9 0x1002E540 -> 0x10004CA0) clears Z and stencil every frame and the
// colour only when StartRendering's point check finds the camera outside the world. In flight the frame's first
// Clear (that one: whole target, Z and stencil) also takes the colour, in the engine's own clear colour; nothing else.
static void tb_0358_freecam_frame_clear() {
  CHECK_MSG(freecam_clear_flags(1, 1, 0, 0, 6u) == 7u, "flying, the frame's first whole-target Z clear: colour too");
  CHECK_MSG(freecam_clear_flags(1, 1, 0, 0, 7u) == 7u, "the engine clears it already (camera outside the world)");
  CHECK_MSG(freecam_clear_flags(0, 1, 0, 0, 6u) == 6u, "not flying: the engine's own");
  CHECK_MSG(freecam_clear_flags(1, 0, 0, 0, 6u) == 6u, "a later Clear in the frame (a weapon's Z) is left");
  CHECK(freecam_clear_flags(1, 1, 2, 0, 6u) == 6u && freecam_clear_flags(1, 1, 0, 1, 6u) == 6u);
  CHECK_MSG(freecam_clear_flags(1, 1, 0, 0, 2u) == 2u && freecam_clear_flags(1, 1, 0, 0, 4u) == 4u,
            "only the Lock's Z-and-stencil shape");
}

// d3dtrace.h: one frame's trace is armed by the game thread, starts when a Present has finished, stops at the next
// Present's entry (before our draws), and is logged once; at most 64 calls are kept.
static void tb_0359_d3d_frame_trace() {
  int s = DTR_IDLE;
  s = dtr_step(s, DTR_EV_PRESENT_ENTRY);
  CHECK_MSG(s == DTR_IDLE && dtr_step(s, DTR_EV_TAKEN) == DTR_IDLE, "not armed: nothing");
  s = dtr_step(s, DTR_EV_ARM);
  CHECK(s == DTR_ARMED && dtr_step(s, DTR_EV_PRESENT_ENTRY) == DTR_ARMED && dtr_step(s, DTR_EV_ARM) == DTR_ARMED);
  s = dtr_step(s, DTR_EV_PRESENT_DONE);
  CHECK_MSG(s == DTR_RECORDING && dtr_step(s, DTR_EV_PRESENT_DONE) == DTR_RECORDING, "recording from a Present's end");
  s = dtr_step(s, DTR_EV_PRESENT_ENTRY);
  CHECK_MSG(s == DTR_READY && dtr_step(s, DTR_EV_ARM) == DTR_READY, "ready at the next Present's entry");
  CHECK_MSG(dtr_step(s, DTR_EV_TAKEN) == DTR_IDLE, "logged once");
  CHECK(dtr_slot(0) == 0 && dtr_slot(DTR_CAP - 1) == DTR_CAP - 1 && dtr_slot(DTR_CAP) == -1 && dtr_slot(-1) == -1);
  CHECK(strcmp(dtr_fmt_name(113), "A16B16G16R16F") == 0 && strcmp(dtr_fmt_name(21), "A8R8G8B8") == 0 &&
        dtr_fmt_name(9999)[0] == 0);
}

// rtclear.h: in flight a screen-sized render target (not the back buffer) is filled with 0 at its first bind of a
// frame. Never filled: other sizes and formats; the first frame a target is seen; the motion-blur feedback target, kept
// by identity; a target sampled before its first bind; one the engine colour-cleared itself last frame.
static void tb_0360_rtclear_first_bind() {
  static RtcState st;
  rtc_init(&st, (const void*)0x100, 3440, 1440);
  const void* g0 = (const void*)0x200;
  const void* g1 = (const void*)0x300;
  const void* fb = (const void*)0x400;    // the feedback target
  const void* pre = (const void*)0x600;   // a target read before it is bound
  const void* tex0 = (const void*)0x210;
  const void* texF = (const void*)0x410;
  const void* texP = (const void*)0x610;
  CHECK_MSG(rtc_bind(&st, g0, 3440, 1440, tex0) == RTC_LEARN, "the first sight only learns");
  CHECK(rtc_bind(&st, fb, 3440, 1440, texF) == RTC_LEARN && rtc_bind(&st, g1, 3440, 1440, 0) == RTC_LEARN &&
        rtc_bind(&st, pre, 3440, 1440, texP) == RTC_LEARN);
  CHECK_MSG(rtc_bind(&st, (const void*)0x100, 3440, 1440, 0) == RTC_SKIP_SIZE, "never the back buffer");
  CHECK_MSG(rtc_bind(&st, (const void*)0x500, 860, 360, 0) == RTC_SKIP_SIZE, "never another size (bloom, shadows)");
  rtc_frame(&st);
  CHECK_MSG(rtc_bind(&st, g0, 3440, 1440, tex0) == RTC_FILL && st.s[0].verdict == RTC_FILL,
            "next frame, first bind: filled, and the verdict kept for the log");
  CHECK_MSG(rtc_bind(&st, g0, 3440, 1440, tex0) == RTC_SKIP_BOUND, "a later bind in the same frame: not again");
  // The engine's real order for the feedback target: bind, then sample it as the blend's source.
  CHECK(rtc_bind(&st, fb, 3440, 1440, texF) == RTC_FILL);
  rtc_sampled(&st, texF);
  rtc_frame(&st);
  CHECK_MSG(rtc_bind(&st, fb, 3440, 1440, texF) == RTC_FILL,
            "bind-then-sample is not caught by the sampled-before-bind guard: identity is needed");
  rtc_set_kept(&st, fb);
  rtc_frame(&st);
  CHECK_MSG(rtc_bind(&st, fb, 3440, 1440, texF) == RTC_SKIP_KEPT, "the feedback target, kept by identity");
  CHECK_MSG(rtc_bind(&st, fb, 3440, 1440, texF) == RTC_SKIP_KEPT, "every bind of it");
  rtc_sampled(&st, texP);
  CHECK_MSG(rtc_bind(&st, pre, 3440, 1440, texP) == RTC_SKIP_HISTORY, "sampled before its bind: history (second guard)");
  rtc_engine_clear(&st, g1);
  rtc_frame(&st);
  CHECK_MSG(rtc_bind(&st, pre, 3440, 1440, texP) == RTC_SKIP_HISTORY, "once history, always kept");
  CHECK_MSG(rtc_bind(&st, g1, 3440, 1440, 0) == RTC_SKIP_ENGINE, "the engine cleared it last frame: left to it");
  rtc_frame(&st);
  rtc_frame(&st);
  CHECK_MSG(rtc_bind(&st, g1, 3440, 1440, 0) == RTC_FILL, "no engine clear since: filled again");
  CHECK(rtc_bind(&st, 0, 3440, 1440, 0) == RTC_SKIP_SIZE);
  int learned = 0, full = 0;
  for (int i = 0; i < RTC_MAX + 4; ++i) {
    const int a = rtc_bind(&st, (const void*)(0x1000 + i * 16), 3440, 1440, 0);
    learned += a == RTC_LEARN;
    full += a == RTC_SKIP_FULL;
  }
  CHECK_MSG(learned == RTC_MAX - 4 && full == 8, "a full table learns no more, and says so");
  // Only the colour formats ColorFill takes and this engine uses.
  CHECK(rtc_format_ok(21) && rtc_format_ok(22) && rtc_format_ok(112) && rtc_format_ok(113) && rtc_format_ok(35));
  CHECK_MSG(!rtc_format_ok(75) && !rtc_format_ok(1280070990u) && !rtc_format_ok(0), "no depth or FOURCC formats");
  // D3DDrv9's resource hash (FUN_10020D80): bucket ((lo >> 4) & 0xFF0) + ((lo >> 16) & 0xF), XOR lo & 0xFFF.
  CHECK(d3dres_bucket(0x12345678u) == 0x31Cu && d3dres_bucket(0) == 0 && d3dres_bucket(0xFFFFFFFFu) <= 0xFFFu);
}

// freecam.h (freecam.md sections 7 and 8): No culling forces r_UseFarPlane, r_Occlude and r_UseAntiPortals to 0; No
// distance fog takes the far plane with it. Fullbright forces no cvar (it holds RendMap, tb_0364). Only while flying;
// each comes back on the flight's end through cvars.h's rule with 0 forced.
static void tb_0361_freecam_bright_cull() {
  static const int kCull[3] = { FC_CVAR_FARPLANE, FC_CVAR_OCCLUDE, FC_CVAR_ANTIPORTALS };
  int n = 0;
  for (int i = 0; i < 3; ++i)
    n += freecam_cvar_forced(kCull[i], 1, 0, 0, 0, 1) && !freecam_cvar_forced(kCull[i], 0, 0, 0, 0, 1);
  CHECK_MSG(n == 3, "No culling: the three, flying only");
  CHECK_MSG(freecam_cvar_forced(FC_CVAR_FARPLANE, 1, 0, 0, 1, 0) && !freecam_cvar_forced(FC_CVAR_OCCLUDE, 1, 0, 0, 1, 0),
            "No distance fog takes the far plane, not the occlusion");
  int plain = 0;
  for (int c = 0; c < FC_CVAR_ALL; ++c) plain += freecam_cvar_forced(c, 1, 0, 0, 0, 0);
  CHECK_MSG(plain == 1, "a plain flight forces only the frame cache");
  int none = 0;
  for (int c = 0; c < FC_CVAR_ALL; ++c) none += freecam_cvar_forced(c, 0, 1, 1, 1, 1);
  CHECK_MSG(none == 0, "nothing while not flying");
  CHECK(!freecam_cvar_is_float(FC_CVAR_OCCLUDE));
  CvarHold u = cvar_hold_init();
  int w = -1;
  CHECK(cvar_step(&u, 1, 1, 0, &w) == CVAR_FORCE && w == 0);
  CHECK_MSG(cvar_step(&u, 0, 0, 0, &w) == CVAR_RESTORE && w == 1, "a flag cvar: forced 0, given back");
  config_defaults();
  CHECK_MSG(g_cfg.freecamBright == 0 && g_cfg.freecamNoCull == 0 && config_key_known("freecam", "fullbright") &&
            !config_key_known("freecam", "fullbrightscale") && config_key_known("freecam", "noculling"),
            "off by default; the strength is gone");
}

// freecam.h: an alt-tab keeps the flight, its pause and cvars. Away, the camera reads no keys or mouse and the engine's
// input stays gated; back in front it steers again untouched. Our pause rides out the game's focus pause: absorbed
// while ours holds, paused again if the return unpauses it, and a game pause we did not make is never released.
static void tb_0362_freecam_alt_tab_keeps_flight() {
  CHECK_MSG(freecam_end_reason(1, 1, 1, 0) == FCE_NONE, "a focus loss is no exit path: not an end reason's input");
  CHECK_MSG(!freecam_steers(1, 1, 0, 0) && freecam_steers(1, 1, 0, 1), "away: no steering; back: steering again");
  CHECK_MSG(!keystate_passes('W', false, true, 0), "the engine still reads no key while the flight holds");
  FcPause p = {};
  CHECK(fcp_step(&p, 1, 7, 1, 0) == FCP_PAUSE && fcp_step(&p, 1, 7, 1, 1) == FCP_NONE);
  CHECK_MSG(fcp_step(&p, 1, 7, 1, 1) == FCP_NONE && p.held, "the game's focus pause is absorbed under ours");
  CHECK_MSG(fcp_step(&p, 1, 7, 1, 0) == FCP_REPAUSE && p.held, "its return unpaused us: paused again");
  FcPause g = {};
  CHECK_MSG(fcp_step(&g, 0, 7, 1, 1) == FCP_NONE && fcp_step(&g, 1, 7, 1, 1) == FCP_NONE && !g.held,
            "the game's own focus pause, row off or on: never ours");
  CHECK_MSG(fcp_step(&g, 0, 7, 1, 1) == FCP_NONE, "and never released by us");
}

// freecam.h (freecam.md section 8e): under No culling the frame's zone (+0x18) is set to 0 at PlayerPawn.PreRender,
// the engine's own outside-the-world state, so the zone walk draws the whole level. Only while flying with the row
// on, only on a frame this viewport made for this level (frame +0 and +4), and only over a zone that is not 0 already.
static void tb_0363_freecam_frame_zone() {
  const void* vp = (const void*)0x10;
  const void* lv = (const void*)0x20;
  CHECK_MSG(freecam_zone_write(1, 1, vp, vp, lv, lv, 29), "flying, No culling, our frame: zone 0");
  CHECK_MSG(!freecam_zone_write(0, 1, vp, vp, lv, lv, 29) && !freecam_zone_write(1, 0, vp, vp, lv, lv, 29),
            "not flying, or the row off: untouched");
  CHECK_MSG(!freecam_zone_write(1, 1, (const void*)0x11, vp, lv, lv, 29) &&
            !freecam_zone_write(1, 1, vp, vp, (const void*)0x21, lv, 29), "another viewport's or level's frame: untouched");
  CHECK_MSG(!freecam_zone_write(1, 1, vp, vp, lv, lv, 0), "already outside the world: nothing to write");
  CHECK(!freecam_zone_write(1, 1, 0, 0, lv, lv, 29) && !freecam_zone_write(1, 1, vp, vp, 0, 0, 29));
}

// freecam.h: Fullbright sets the game's unlit view mode, PlayerPawn.RendMap, while flying and restores the game's value
// after. cvars.h's rule with the unlit value forced: a value the game set meanwhile is given back, a new level drops
// the hold, nothing is written without a player. The world stays paused while it holds, so no checkpoint saves it.
static void tb_0364_freecam_rendmap() {
  CvarHold h = cvar_hold_init();
  unsigned lvl = 0;
  int w = -1;
  const int lit = 5, unlit = FREECAM_RENDMAP_UNLIT;
  CHECK_MSG(unlit != lit && unlit >= 1 && unlit <= 23, "an RMODE value, not the game's lit 5");
  CHECK_MSG(fc_hold_step(&h, &lvl, 1, 7, 1, lit, unlit, &w) == CVAR_FORCE && w == unlit, "on: the unlit mode");
  CHECK(fc_hold_step(&h, &lvl, 1, 7, 1, unlit, unlit, &w) == CVAR_NONE);
  CHECK_MSG(fc_hold_step(&h, &lvl, 0, 7, 1, unlit, unlit, &w) == CVAR_RESTORE && w == lit, "off: the game's 5 back");
  CvarHold g = cvar_hold_init();
  fc_hold_step(&g, &lvl, 1, 7, 1, lit, unlit, &w);
  CHECK_MSG(fc_hold_step(&g, &lvl, 1, 7, 1, 8, unlit, &w) == CVAR_FORCE && w == unlit &&
            fc_hold_step(&g, &lvl, 0, 7, 1, unlit, unlit, &w) == CVAR_RESTORE && w == 8, "a value the game set meanwhile");
  CvarHold n = cvar_hold_init();
  fc_hold_step(&n, &lvl, 1, 7, 1, lit, unlit, &w);
  CHECK_MSG(fc_hold_step(&n, &lvl, 0, 7, 0, -1, unlit, &w) == CVAR_NONE && n.held, "no player: nothing written, still held");
  CHECK(fc_hold_step(&n, &lvl, 0, 7, 1, unlit, unlit, &w) == CVAR_RESTORE && w == lit);
  CvarHold l = cvar_hold_init();
  fc_hold_step(&l, &lvl, 1, 7, 1, lit, unlit, &w);
  CHECK_MSG(fc_hold_step(&l, &lvl, 0, 8, 1, lit, unlit, &w) == CVAR_NONE && !l.held, "a new level: dropped");
  // Fullbright no longer forces the pause; the row or a photo does.
  CHECK_MSG(freecam_pause_armed(1, 1, 0) && freecam_pause_armed(1, 0, 1) && !freecam_pause_armed(1, 0, 0) &&
            !freecam_pause_armed(0, 1, 1), "the row or a photo, flying only");
}

// freecam.h: every skipped frame zone write is counted by its reason, so a gate shows
// which check failed; the level compared is LevelInfo's ULevel (CreateMasterFrame 0x1027A760: frame +4 = viewport actor
// +0x7C -> +0x6C8), which the first build took from a property that does not exist.
static void tb_0365_freecam_zone_reasons() {
  const void* vp = (const void*)0x10;
  const void* lv = (const void*)0x20;
  CHECK(freecam_zone_reason(1, 1, 1, vp, vp, lv, lv, 29) == FZ_WRITE);
  CHECK(freecam_zone_reason(1, 0, 1, vp, vp, lv, lv, 29) == FZ_OFF && freecam_zone_reason(0, 1, 1, vp, vp, lv, lv, 29) == FZ_OFF);
  CHECK_MSG(freecam_zone_reason(1, 1, 0, vp, vp, lv, lv, 29) == FZ_UNRESOLVED, "an offset not resolved");
  CHECK_MSG(freecam_zone_reason(1, 1, 1, vp, 0, lv, lv, 29) == FZ_NO_VIEWPORT, "no viewport on Duke");
  CHECK_MSG(freecam_zone_reason(1, 1, 1, (const void*)0x11, vp, lv, lv, 29) == FZ_VIEWPORT, "another viewport's frame");
  CHECK_MSG(freecam_zone_reason(1, 1, 1, vp, vp, (const void*)0x21, lv, 29) == FZ_LEVEL, "another level's frame");
  CHECK_MSG(freecam_zone_reason(1, 1, 1, vp, vp, lv, 0, 29) == FZ_LEVEL, "no level read");
  CHECK_MSG(freecam_zone_reason(1, 1, 1, vp, vp, lv, lv, 0) == FZ_ZONE0, "already outside the world");
  int named = 0;
  for (int r = 0; r < FZ_COUNT; ++r) named += freecam_zone_reason_text(r)[0] != 0;
  CHECK(named == FZ_COUNT);
}

// freecam.h: while Fullbright holds RendMap, FREECAM_STUCK_FRAMES frames in a row that never bind the back buffer as
// render target 0 mean the engine stopped drawing: RendMap is given back at once and Fullbright is dropped for the rest
// of the flight. A frame that binds the back buffer resets the count; nothing counts when Fullbright does not hold.
static void tb_0366_freecam_stuck_frames() {
  FcStuck s = {};
  CHECK(FREECAM_STUCK_FRAMES == 3);
  CHECK_MSG(fc_stuck_step(&s, 1, 0) == 0 && fc_stuck_step(&s, 1, 0) == 0, "two frames: not yet");
  CHECK_MSG(fc_stuck_step(&s, 1, 1) == 0 && s.frames == 0, "the back buffer bound: the count starts again");
  fc_stuck_step(&s, 1, 0);
  fc_stuck_step(&s, 1, 0);
  CHECK_MSG(fc_stuck_step(&s, 1, 0) == 1 && s.tripped, "three in a row: stuck");
  CHECK_MSG(fc_stuck_step(&s, 1, 0) == 0, "reported once");
  FcStuck n = {};
  for (int i = 0; i < 8; ++i) CHECK(fc_stuck_step(&n, 0, 0) == 0);
  CHECK_MSG(n.frames == 0, "nothing counts without the hold");
  CHECK_MSG(freecam_bright_wanted(1, 1, 0) && !freecam_bright_wanted(1, 1, 1) && !freecam_bright_wanted(0, 1, 0) &&
            !freecam_bright_wanted(1, 0, 0), "a tripped flight drops Fullbright until the next one");
  // The restore path: dropping the want gives the game's value back even mid-flight, through the same hold.
  CvarHold h = cvar_hold_init();
  unsigned lvl = 0;
  int w = -1;
  fc_hold_step(&h, &lvl, 1, 7, 1, 5, FREECAM_RENDMAP_UNLIT, &w);
  CHECK_MSG(fc_hold_step(&h, &lvl, freecam_bright_wanted(1, 1, 1), 7, 1, FREECAM_RENDMAP_UNLIT, FREECAM_RENDMAP_UNLIT,
                         &w) == CVAR_RESTORE && w == 5, "tripped: RendMap 5 back at once");
}

// Our own Present-time drawing binds the back buffer every frame (prim_d3d9), so a net that counted
// any back-buffer bind never tripped. Only the engine's own binds say it drew.
static void tb_0367_render_watch_engine_binds() {
  CHECK_MSG(rst_engine_bb_bind(0, 1, 0), "the engine binds the back buffer as target 0: it drew");
  CHECK_MSG(!rst_engine_bb_bind(0, 1, 1), "our own bind during Present does not count");
  CHECK_MSG(!rst_engine_bb_bind(1, 1, 0) && !rst_engine_bb_bind(0, 0, 0), "another slot or surface does not count");
  // The frozen frame: the engine binds only a G-buffer target, we bind the back buffer and restore. Trips at 3.
  FcStuck s = {};
  int tripped = 0;
  for (int f = 0; f < 3; ++f) {
    int bound = 0;
    bound |= rst_engine_bb_bind(0, 0, 0);   // the engine's G-buffer bind
    bound |= rst_engine_bb_bind(0, 1, 1);   // ours
    tripped |= fc_stuck_step(&s, 1, bound);
  }
  CHECK_MSG(tripped, "frames where only we bound the back buffer trip the net");
}

static void tb_0368_render_watch_steps() {
  RstWatch w = {};
  int sample = 1;
  for (int i = 1; i < RST_FIRST; ++i) CHECK(rst_step(&w, 0, &sample) == RST_NONE);
  CHECK_MSG(rst_step(&w, 0, &sample) == RST_STALLED && sample && w.episodes == 1, "30 frames without a draw: stalled");
  CHECK_MSG(rst_step(&w, -1, &sample) == RST_NONE && w.frames == RST_FIRST, "unknown counts nothing");
  while (w.frames < RST_SECOND - 1) CHECK(rst_step(&w, 0, &sample) == RST_NONE);
  CHECK_MSG(rst_step(&w, 0, &sample) == RST_STILL && sample, "still stalled at 300: a second sample");
  CHECK_MSG(rst_step(&w, 0, &sample) == RST_NONE, "nothing more in the episode");
  CHECK_MSG(rst_step(&w, 1, &sample) == RST_BACK && w.frames == 0 && w.lastRun == RST_SECOND + 1,
            "a drawn frame ends the episode, and says how long it was gone");
  CHECK_MSG(rst_step(&w, 1, &sample) == RST_NONE, "reported once");
  for (int e = 0; e < 8; ++e) {
    rst_step(&w, 1, &sample);
    for (int i = 0; i < RST_FIRST; ++i) rst_step(&w, 0, &sample);
  }
  CHECK_MSG(w.samples == RST_MAX_SAMPLES && !sample, "samples capped for the session; the episode is still logged");
  for (int i = 0; i < 5; ++i) CHECK(rst_step(&w, 1, &sample) != RST_STALLED);
  CHECK_MSG(!strcmp(rst_thread_state(1, 259), "running") && !strcmp(rst_thread_state(1, 0), "exited") &&
            !strcmp(rst_thread_state(0, 0), "unknown"), "GetExitCodeThread named");
  CHECK_MSG(crash_take_render_slot(1) && crash_take_render_slot(CRASH_MAX_RENDER) &&
            !crash_take_render_slot(CRASH_MAX_RENDER + 1), "the render thread keeps slots of its own");
}

static void tb_0369_freecam_bright_research() {
  // Verified in game 2026-10-08 with the render-target heal: the row is back for users, no research key.
  CHECK_MSG(!strcmp(freecam_bright_hint(), "The game's unlit view mode while flying: surfaces without their lighting. "
                                           "The game's view mode comes back after."),
            "the row's plain hint; no pause");
  CHECK_MSG(freecam_bright_wanted(1, 1, 0) && !freecam_pause_armed(1, 0, 0), "Fullbright flies without the pause");
}

// The frame record keeps a frame's first 64 calls and its last 448: the start (Lock) and the end (the final writes).
static void tb_0370_frame_record_slots() {
  CHECK(DREC_CAP == 512 && DREC_HEAD == 64);
  CHECK_MSG(drec_slot(0) == 0 && drec_slot(63) == 63 && drec_slot(64) == 64, "the head fills in order");
  CHECK_MSG(drec_slot(511) == 511 && drec_slot(512) == 64 && drec_slot(513) == 65, "the tail is a ring");
  CHECK_MSG(drec_slot(-1) == -1, "no slot before the first call");
  // A short frame logs in order.
  CHECK(drec_kept(100) == 100 && drec_order(70, 100) == 70 && drec_call_no(70, 100) == 70);
  // A long frame: the head, then the ring from its oldest entry.
  const long total = 1000;
  CHECK(drec_kept(total) == DREC_CAP);
  CHECK_MSG(drec_order(63, total) == 63 && drec_call_no(63, total) == 63, "the head stays");
  CHECK_MSG(drec_call_no(64, total) == total - DREC_RING && drec_call_no(DREC_CAP - 1, total) == total - 1,
            "the ring holds the last 448 calls");
  for (int i = DREC_HEAD; i < DREC_CAP; ++i)
    if (drec_order(i, total) != drec_slot(drec_call_no(i, total))) { CHECK_MSG(0, "order maps to the call's slot"); break; }
}

static void tb_0371_frame_record_arming() {
  CHECK_MSG(drec_keep_shader(0, (const void*)8) && !drec_keep_shader((const void*)8, (const void*)8),
            "a shader bind is kept only when it changes");
  int counts[DREC_WHY_COUNT] = {};
  for (int i = 0; i < DREC_PER_WHY; ++i) CHECK(drec_may_arm(counts, DREC_WHY_FLIGHT));
  CHECK_MSG(!drec_may_arm(counts, DREC_WHY_FLIGHT), "flight records capped");
  CHECK_MSG(drec_may_arm(counts, DREC_WHY_STALL), "a stall still gets its own");
  CHECK(!drec_may_arm(counts, DREC_WHY_COUNT));
}

// The RendMap 6 latch (freecam.md section 7): Lock adopts the render target 0 the last frame left as the next frame's
// main target. At Present, render target 0 goes back to the back buffer whenever the engine's frame left it elsewhere.
static void tb_0372_render_target_heal() {
  CHECK_MSG(rst_heal_rt(1, 0, 1), "the frame ended on another target: back to the back buffer");
  CHECK_MSG(!rst_heal_rt(1, 1, 1), "already the back buffer: nothing");
  CHECK_MSG(!rst_heal_rt(0, 0, 1) && !rst_heal_rt(1, 0, 0), "a target or back buffer not known: nothing");
  // The net reads where each frame's Lock cleared: any miss, or no Lock at all, is a frame off the back buffer.
  CHECK_MSG(rst_lock_verdict(1, 0) == 1 && rst_lock_verdict(2, 1) == 0 && rst_lock_verdict(0, 0) == 0,
            "Lock's clear on the back buffer is the healthy frame");
}

// The net's patience: 3 frames for users, more for a research gate that records the frozen frame first.
static void tb_0373_stuck_net_delay() {
  FcStuck s = {};
  for (int i = 0; i < 59; ++i) CHECK(fc_stuck_step_n(&s, 1, 0, 60) == 0);
  CHECK_MSG(fc_stuck_step_n(&s, 1, 0, 60) == 1, "trips at the delay");
  FcStuck t = {};
  CHECK(fc_stuck_step_n(&t, 1, 0, 0) == 1);   // a delay under 1 counts as 1
  config_defaults();
  CHECK_MSG(g_cfg.freecamNetDelay == FREECAM_STUCK_FRAMES, "the default is the user's 3");
}

// Fullbright holds RendMap 6 for the whole flight, paused or not, so a checkpoint during an
// unpaused flight can save it. On a new level or a newly read player, a 6 we do not hold for this level is carried
// from a save and goes back to 5; any other value is the game's own and stays.
static void tb_0374_freecam_rendmap_carried() {
  CHECK_MSG(freecam_rendmap_carried(FREECAM_RENDMAP_UNLIT, 0, 0, 4), "a 6 with no hold: carried from a save");
  CHECK_MSG(!freecam_rendmap_carried(FREECAM_RENDMAP_UNLIT, 1, 4, 4), "ours, held for this level: kept");
  CHECK_MSG(freecam_rendmap_carried(FREECAM_RENDMAP_UNLIT, 1, 3, 4), "a hold from the last level is not this pawn's");
  CHECK_MSG(!freecam_rendmap_carried(5, 0, 0, 4) && !freecam_rendmap_carried(8, 0, 0, 4) &&
            !freecam_rendmap_carried(-1, 0, 0, 4), "the game's own values, or unreadable: left alone");
  CHECK_MSG(FREECAM_RENDMAP_LIT == 5, "restored to the game's lit mode");
  // The hold no longer asks for the pause: wanted is the hold's want, unpaused or not.
  CvarHold h = cvar_hold_init();
  unsigned lvl = 0;
  int w = -1;
  CHECK(fc_hold_step(&h, &lvl, freecam_bright_wanted(1, 1, 0), 3, 1, 5, FREECAM_RENDMAP_UNLIT, &w) == CVAR_FORCE && w == 6);
  CHECK_MSG(fc_hold_step(&h, &lvl, freecam_bright_wanted(1, 1, 0), 3, 1, 6, FREECAM_RENDMAP_UNLIT, &w) == CVAR_NONE,
            "held through the flight");
}

// Handling (Weapons > HANDLING): DNMP's stops and labels, the factor per weapon state, the lockout lever, the
// restore rule, the AnimRate 0.0 measurement's verdict and the rows' defaults (docs/re/handling.md 5-7).
static void tb_0380_weap_slider_stops_and_labels() {
  CHECK(weap_fire_pct_clamped(0) == 100 && weap_fire_pct_clamped(124) == 100 && weap_fire_pct_clamped(125) == 150);
  CHECK(weap_fire_pct_clamped(400) == 400 && weap_fire_pct_clamped(999) == 400);
  CHECK(weap_reload_pct_clamped(50) == 100 && weap_reload_pct_clamped(1000) == 1000);
  CHECK(weap_reload_pct_clamped(1074) == 1050 && weap_reload_pct_clamped(1075) == 1100);
  CHECK(weap_reload_pct_clamped(5000) == 1100 && weap_reload_is_max(1100) && !weap_reload_is_max(1000));
  char b[16];
  weap_fire_text(250, b, sizeof b);    CHECK(strcmp(b, "250%") == 0);
  weap_reload_text(1000, b, sizeof b); CHECK(strcmp(b, "1000%") == 0);
  weap_reload_text(1100, b, sizeof b); CHECK(strcmp(b, "Max") == 0);
  weap_reload_text(100, b, sizeof b);  CHECK(strcmp(b, "100%") == 0);
}
static void tb_0381_handling_factor_per_state() {
  CHECK_MSG(weap_fire_factor(200) == 2.f && weap_reload_factor(1100) == 20.f && weap_reload_factor(300) == 3.f,
            "Max is 20x, not instant");
  for (int st = 0; st <= 10; st++) {
    const float w = hd_want(st, 200, 1100);
    const float expect = (st >= 4 && st <= 6) ? 2.f : (st >= 7 && st <= 9) ? 20.f : 1.f;
    CHECK_MSG(w == expect, "fire 4-6, reload 7-9, stock elsewhere");
  }
  CHECK_MSG(hd_want(5, 100, 100) == 1.f && !hd_active(100, 100) && !hd_active(110, 90), "stock stops: identity");
  CHECK(hd_active(150, 100) && hd_active(100, 1100));
  CHECK_MSG(hd_call_due(1, 1.f, 2.f, 0, 0.f) && !hd_call_due(1, 2.f, 2.f, 1, 2.f), "active: live vs wanted");
  CHECK_MSG(hd_call_due(1, 2.f, 1.f, 1, 2.f), "active, out of the fire state: back to 1.0");
}
static void tb_0382_handling_lockout_timing() {
  HdLock l = hd_lock_init();
  CHECK(hd_lock_step(&l, 0, 10.f, 0.15f, 2.f) == 0);
  CHECK_MSG(hd_lock_step(&l, 1, 10.f, 0.15f, 2.f) == 0, "the rise starts the clock");
  CHECK(hd_lock_step(&l, 1, 10.07f, 0.15f, 2.f) == 0);
  CHECK_MSG(hd_lock_step(&l, 1, 10.076f, 0.15f, 2.f) == 1, "due at 0.15 / 2 of game time");
  CHECK_MSG(hd_lock_step(&l, 1, 10.2f, 0.15f, 2.f) == 0, "once per lockout");
  hd_lock_after_clear(&l, 1, 10.2f);
  CHECK_MSG(hd_lock_step(&l, 1, 10.2f, 0.15f, 2.f) == 0 && hd_lock_step(&l, 1, 10.28f, 0.15f, 2.f) == 1,
            "ClearLockout refired the gun: a new lockout from now");
  HdLock s = hd_lock_init();
  hd_lock_step(&s, 1, 1.f, 0.15f, 1.f);
  CHECK_MSG(hd_lock_step(&s, 1, 9.f, 0.15f, 1.f) == 0, "100 %: the engine's own timer only");
  HdLock z = hd_lock_init();
  hd_lock_step(&z, 1, 1.f, 0.f, 4.f);
  CHECK_MSG(hd_lock_step(&z, 1, 9.f, 0.f, 4.f) == 0, "no lockout configured: nothing to clear");
  HdLock r = hd_lock_init();
  hd_lock_step(&r, 1, 50.f, 1.5f, 3.f);
  CHECK_MSG(hd_lock_step(&r, 1, 2.f, 1.5f, 3.f) == 0 && hd_lock_step(&r, 1, 2.5f, 1.5f, 3.f) == 1,
            "game time went back (a new level): the clock restarts");
}
static void tb_0383_handling_restore_rule() {
  CHECK_MSG(hd_restore_due(1, 0, 2.f, 2.f), "switched away with ours in place: 1.0 back");
  CHECK_MSG(!hd_restore_due(1, 1, 2.f, 2.f), "still held: the poll owns it");
  CHECK_MSG(!hd_restore_due(1, 0, 3.f, 2.f), "changed since: left");
  CHECK_MSG(!hd_restore_due(0, 0, 2.f, 2.f), "gone with its weapon or level: nothing");
  CHECK(!hd_restore_due(1, 0, 1.f, 1.f));
  CHECK_MSG(hd_call_due(0, 3.f, 1.f, 1, 3.f) && !hd_call_due(0, 3.f, 1.f, 1, 2.f) && !hd_call_due(0, 3.f, 1.f, 0, 0.f),
            "rows back to 100 %: only our own value goes");
  CHECK_MSG(hd_adopt(0, 3.f) && !hd_adopt(1, 3.f) && !hd_adopt(0, 1.f), "a save that carried our scale");
}
static void tb_0384_handling_config_defaults() {
  config_defaults();
  CHECK(g_cfg.fireRatePct == 100 && g_cfg.reloadPct == 100);
}
static void tb_0385_handling_rate_verdict() {
  const float b[4] = { 1.f, 0.f, 0.f, 0.5f }, sc[4] = { 2.f, 0.f, 0.f, 1.f }, un[4] = { 1.f, 0.f, 0.f, 0.5f };
  const float z[4] = { 0.f, 0.f, 0.f, 0.f }, mx[4] = { 2.f, 0.f, 0.f, 0.5f };
  CHECK(hd_rate_verdict(b, sc, 4, 1.f, 2.f) == HDV_SCALED);
  CHECK(hd_rate_verdict(b, un, 4, 1.f, 2.f) == HDV_UNCHANGED);
  CHECK_MSG(hd_rate_verdict(z, z, 4, 1.f, 2.f) == HDV_ZERO, "AnimRate 0.0 taken literally");
  CHECK(hd_rate_verdict(b, mx, 4, 1.f, 2.f) == HDV_MIXED);
  CHECK_MSG(hd_rate_effect(0.3f, 0.15f, 2.f) == 1 && hd_rate_effect(0.3f, 0.29f, 2.f) == 0 &&
            hd_rate_effect(0.3f, 0.22f, 2.f) == -1, "the shot interval halves at 200 %");
}

// collect.h: a phone's id is the two digits of its StartPlayingMessagesEvent, matched by name index against the 12
// names DukePlayer registers; a decoy (no event, or another) has none.
static void tb_0390_cl_phone_id() {
  char b[32];
  CHECK(cl_phone_event_name(7, b, sizeof b) && strcmp(b, "ACH_PhoneMessage_07") == 0);
  CHECK(cl_phone_event_name(12, b, sizeof b) && strcmp(b, "ACH_PhoneMessage_12") == 0);
  CHECK(!cl_phone_event_name(0, b, sizeof b) && !cl_phone_event_name(13, b, sizeof b) && b[0] == 0);
  int names[PG_PHONES];
  for (int i = 0; i < PG_PHONES; ++i) names[i] = 500 + i;
  CHECK(cl_phone_id(500, names) == 1 && cl_phone_id(506, names) == 7 && cl_phone_id(511, names) == 12);
  CHECK_MSG(cl_phone_id(0, names) == 0 && cl_phone_id(42, names) == 0, "no event or another: a decoy");
  names[3] = 0;   // a name that did not resolve never matches None
  CHECK(cl_phone_id(0, names) == 0);
}
// collect.h: the helmet letter is the ACH_DiscoveredHelmetX among a Dispatcher's OutEvents; the Trigger whose Event
// (A, B) or LookUseEvents (C, a look) names that dispatcher's Tag carries the marker, from the pass after the tag
// is learned.
static void tb_0391_cl_helmet_letter_and_join() {
  char b[32];
  CHECK(cl_helmet_event_name(0, b, sizeof b) && strcmp(b, "ACH_DiscoveredHelmetA") == 0);
  CHECK(cl_helmet_event_name(2, b, sizeof b) && strcmp(b, "ACH_DiscoveredHelmetC") == 0);
  CHECK(!cl_helmet_event_name(3, b, sizeof b));
  const int helm[PG_HELMETS] = { 70, 71, 72 };
  const int outA[3] = { 70, 80, 81 }, outC[2] = { 0, 72 }, none[2] = { 80, 0 };
  CHECK_MSG(cl_letter_of_events(outA, 3, helm) == 0, "Map05: the event first, then the music and the script");
  CHECK_MSG(cl_letter_of_events(outC, 2, helm) == 2, "Map21: NULL first, the helmet event at +2.75 s");
  CHECK(cl_letter_of_events(none, 2, helm) == -1 && cl_letter_of_events(0, 0, helm) == -1);
  ClTags t;
  cl_tags_clear(&t);
  int look = 9;
  const int lookC[16] = { 300 };
  CHECK_MSG(cl_trigger_letter(t, 0, lookC, 16, &look) == -1 && look == 0, "no tag learned: no join, None never");
  CHECK(cl_tags_learn(&t, 0, 200) == 1 && cl_tags_learn(&t, 0, 200) == 0 && cl_tags_learn(&t, 2, 300) == 1);
  CHECK(cl_tags_learn(&t, 1, 0) == 0 && cl_tags_learn(&t, 3, 5) == 0);
  CHECK(cl_trigger_letter(t, 200, 0, 0, &look) == 0 && look == 0);
  CHECK_MSG(cl_trigger_letter(t, 0, lookC, 16, &look) == 2 && look == 1, "helmet C: LookUseEvents[0], a look");
  CHECK(cl_trigger_letter(t, 201, 0, 0, &look) == -1);
  const int r = cl_trig_rule(2, 1);
  CHECK(cl_trig_letter(r) == 2 && cl_trig_look(r) && !cl_trig_look(cl_trig_rule(0, 0)));
}
// collect.h: a marker only while the profile flag is false, whatever the actor's state.
static void tb_0392_cl_unfound_rule() {
  PgFlags f;
  memset(&f, 0, sizeof f);
  f.helmet[1] = 1;
  f.phone[6] = 1;   // PhoneMessage7
  CHECK(cl_helmet_marker(pg_helmet_found(f, 0), 0) && !cl_helmet_marker(pg_helmet_found(f, 1), 0));
  CHECK(cl_phone_marker(pg_phone_found(f, 6), 0, 0) && !cl_phone_marker(pg_phone_found(f, 7), 0, 0));
  CHECK(!pg_phone_found(f, 0) && !pg_phone_found(f, 13) && !pg_helmet_found(f, 3));
  CHECK_MSG(!cl_helmet_marker(0, 1) && !cl_phone_marker(0, 0, 1), "being destroyed: no marker");
}
// collect.h: a hidden phone (Map12B's 7 until its office is shown) has no marker until it is shown; the helmet rule
// takes no bHidden (a Trigger is always hidden).
static void tb_0393_cl_hidden_rule() {
  CHECK(!cl_phone_marker(0, 1, 0));
  CHECK_MSG(cl_phone_marker(0, 0, 0), "shown later: marked, read live");
  CHECK(!cl_phone_marker(1, 0, 0));
}
// collect.h: per helmet the first live trigger, else its dispatcher.
static void tb_0394_cl_pick_helmets() {
  int pick[PG_HELMETS];
  const int trig[3] = { 2, -1, 2 }, disp[3] = { 0, 2, 1 };
  cl_pick_helmets(trig, 3, disp, 3, pick);
  CHECK_MSG(pick[2] == 0, "C: its first trigger");
  CHECK_MSG(pick[0] == -2 && pick[1] == -4, "A and B: no trigger joined, their dispatchers");
  cl_pick_helmets(0, 0, 0, 0, pick);
  CHECK_MSG(pick[0] == -1 && pick[1] == -1 && pick[2] == -1, "a level with none: nothing");
}
static void tb_0395_cl_labels() {
  char b[24];
  cl_helmet_label(0, 0, b, sizeof b);
  CHECK(strcmp(b, "Helmet A") == 0);
  cl_helmet_label(2, 1, b, sizeof b);
  CHECK(strcmp(b, "Helmet C (look)") == 0);
  cl_phone_label(7, b, sizeof b);
  CHECK(strcmp(b, "Phone message 7") == 0);
  cl_phone_label(0, b, sizeof b);
  CHECK(b[0] == 0);
}
// progress.h: the chapter rows' helmet and phone columns match progress.md section 6's "Per chapter" table.
static void tb_0396_pg_chapter_collectibles() {
  PgTable t;
  pg_table_builtin(&t);
  PgFlags f;
  memset(&f, 0, sizeof f);
  PgChapter c[PG_CHAPTERS_MAX];
  const int n = pg_build_chapters(t, f, c);
  const int helm[PG_CHAPTERS] = { 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0 };
  const int phone[PG_CHAPTERS] = { 1, 0, 0, 0, 0, 0, 1, 0, 0, 3, 0, 1, 1, 0, 0, 0, 1, 0, 1, 0, 3, 0, 0, 0 };
  int hs = 0, ps = 0, bad = 0;
  for (int i = 0; i < n; ++i) {
    bad += c[i].helmTotal != helm[i] || c[i].phoneTotal != phone[i] || c[i].helmFound || c[i].phoneFound;
    hs += c[i].helmTotal;
    ps += c[i].phoneTotal;
  }
  CHECK_MSG(bad == 0, "helmets in 5/7/21, phones in 0/6/9x3/11/12/16/18/20x3");
  CHECK(hs == PG_HELMETS && ps == PG_PHONES);
  f.phone[3] = 1;    // PhoneMessage4, Map09
  f.helmet[2] = 1;   // HelmetC, Map21
  f.phone[11] = 1;   // PhoneMessage12, Map20B
  pg_build_chapters(t, f, c);
  CHECK(c[9].phoneFound == 1 && c[9].phoneTotal == 3 && c[21].helmFound == 1 && c[20].phoneFound == 1);
  CHECK(c[5].helmFound == 0 && c[0].phoneFound == 0);
  strcpy_s(t.url[5], PG_URL_CAP, "MAP05");
  pg_build_chapters(t, f, c);
  CHECK_MSG(c[5].helmTotal == 1, "a URL as the game spells it");
}
static void tb_0397_collect_config_defaults() {
  config_defaults();
  CHECK_MSG(g_cfg.espHelmets == 0 && g_cfg.espPhones == 0, "both rows off by default");
  CHECK(config_key_known("esp", "helmets") && config_key_known("esp", "phones"));
}
// worldscan.h: a client that needs no rescan (nothing of it spawns) passes once per level, not every WS_RESCAN_MS.
static void tb_0398_ws_once_per_level() {
  static int lvA;
  WsClock c;
  ws_clock_reset(&c);
  CHECK(ws_scan_due(c, 1, &lvA, 1, 1000, 0));
  ws_scan_done(&c, 1, &lvA, 1000);
  CHECK_MSG(!ws_scan_due(c, 1, &lvA, 1, 1000 + 10 * WS_RESCAN_MS, 0), "the same level: never again by time");
  CHECK(ws_scan_due(c, 1, &lvA, 1, 1000 + WS_RESCAN_MS, 1));
  CHECK_MSG(ws_scan_due(c, 2, &lvA, 1, 1001, 0), "a new level: at once");
}

// achreset.h: the reset row arms on a click and resets only on a second click inside AR_CONFIRM_MS.
static void tb_0400_ar_confirm_rule() {
  ArConfirm c = ar_confirm_init();
  CHECK(!ar_armed(c, 1000));
  CHECK(ar_click(&c, 1000) == AR_ACT_ARM);
  CHECK(ar_armed(c, 1000) && ar_armed(c, 1000 + AR_CONFIRM_MS - 1));
  CHECK_MSG(!ar_armed(c, 1000 + AR_CONFIRM_MS), "the arm lapses at 5 s");
  CHECK(ar_click(&c, 3000) == AR_ACT_CONFIRM);
  CHECK_MSG(!ar_armed(c, 3001), "a confirm disarms");
  CHECK_MSG(ar_click(&c, 3002) == AR_ACT_ARM, "the click after a confirm arms again");
  CHECK_MSG(ar_click(&c, 3002 + AR_CONFIRM_MS) == AR_ACT_ARM, "a click after the lapse arms, never confirms");
  ArConfirm w = ar_confirm_init();
  CHECK(ar_click(&w, 0xFFFFF000u) == AR_ACT_ARM);
  CHECK_MSG(ar_click(&w, 0x00000100u) == AR_ACT_CONFIRM, "the tick count's wrap is a short age");
}
// The reset follows only a written backup that a confirm asked for.
static void tb_0401_ar_reset_needs_backup() {
  CHECK(ar_reset_go(1, 1));
  CHECK_MSG(!ar_reset_go(1, 0), "a failed backup refuses the reset");
  CHECK_MSG(!ar_reset_go(0, 1), "a plain backup never resets");
  CHECK(!ar_reset_go(0, 0));
}
// Unix seconds to local civil time and its text, with the offset.
static void tb_0402_ar_time_text() {
  char b[48];
  ar_time_text(ar_time_from_unix(0, 0), b, sizeof b);
  CHECK(strcmp(b, "1970-01-01 00:00:00 UTC+00:00") == 0);
  ar_time_text(ar_time_from_unix(1302728131, 0), b, sizeof b);
  CHECK(strcmp(b, "2011-04-13 20:55:31 UTC+00:00") == 0);
  ar_time_text(ar_time_from_unix(1302728131, 180), b, sizeof b);
  CHECK(strcmp(b, "2011-04-13 23:55:31 UTC+03:00") == 0);
  ar_time_text(ar_time_from_unix(1302728131, 240), b, sizeof b);
  CHECK_MSG(strcmp(b, "2011-04-14 00:55:31 UTC+04:00") == 0, "the offset rolls the day");
  ar_time_text(ar_time_from_unix(1302728131, -210), b, sizeof b);
  CHECK(strcmp(b, "2011-04-13 17:25:31 UTC-03:30") == 0);
  ar_time_text(ar_time_from_unix(951782400, 0), b, sizeof b);
  CHECK_MSG(strcmp(b, "2000-02-29 00:00:00 UTC+00:00") == 0, "a leap day");
}
static void tb_0403_ar_file_name() {
  const ArTime t = { 2026, 10, 8, 9, 5, 7, 180 };
  char b[64];
  ar_file_name(t, 1, b, sizeof b);
  CHECK(strcmp(b, "achievements-backup-2026-10-08_09-05-07.txt") == 0);
  ar_file_name(t, 2, b, sizeof b);
  CHECK_MSG(strcmp(b, "achievements-backup-2026-10-08_09-05-07-2.txt") == 0, "a second file in the same second");
}
// Steam's display names: control characters become spaces, a missing one is named, a cut never splits UTF-8.
static void tb_0404_ar_clean() {
  char b[16];
  ar_clean(nullptr, b, sizeof b);
  CHECK(strcmp(b, "(no name)") == 0);
  ar_clean("", b, sizeof b);
  CHECK(strcmp(b, "(no name)") == 0);
  ar_clean("a\tb\r\nc", b, sizeof b);
  CHECK(strcmp(b, "a b  c") == 0);
  ar_clean("abc\xC3\xA9", b, 5);
  CHECK_MSG(strcmp(b, "abc") == 0, "no half of a two-byte character");
  ar_clean("ab\xC3\xA9", b, 5);
  CHECK(strcmp(b, "ab\xC3\xA9") == 0);
}
static ArEntry ar_entry(int id, const char* api, const char* name, int answered, int achieved, uint32_t unlock,
                        int offMin) {
  ArEntry e;
  memset(&e, 0, sizeof e);
  e.id = id; e.api = api;
  strcpy_s(e.name, sizeof e.name, name);
  e.answered = answered; e.achieved = achieved; e.unlock = unlock; e.offMin = offMin;
  return e;
}
// Unlocked oldest first, an unknown time last, ties by id; locked and unanswered in the game's id order.
static void tb_0405_ar_order() {
  const ArEntry e[] = {
    ar_entry(3, "ACH_3", "c", 1, 1, 200, 0), ar_entry(1, "ACH_1", "a", 1, 1, 100, 0),
    ar_entry(5, "ACH_5", "e", 1, 1, 0, 0),   ar_entry(2, "ACH_2", "b", 1, 1, 100, 0),
    ar_entry(4, "ACH_4", "d", 1, 0, 0, 0),   ar_entry(0, "ACH_0", "z", 1, 0, 0, 0),
    ar_entry(6, "ACH_6", "f", 0, 0, 0, 0),
  };
  int idx[8];
  CHECK(ar_section(e, 7, AR_SEC_UNLOCKED, idx, 8) == 4);
  CHECK(e[idx[0]].id == 1 && e[idx[1]].id == 2 && e[idx[2]].id == 3 && e[idx[3]].id == 5);
  CHECK(ar_section(e, 7, AR_SEC_LOCKED, idx, 8) == 2);
  CHECK(e[idx[0]].id == 0 && e[idx[1]].id == 4);
  CHECK(ar_section(e, 7, AR_SEC_UNANSWERED, idx, 8) == 1);
  CHECK(e[idx[0]].id == 6);
  CHECK_MSG(ar_section(e, 7, AR_SEC_UNLOCKED, idx, 2) == 2, "never past the caller's cap");
  CHECK(strcmp(ar_name(0), "ACH_SomeKills") == 0 && strcmp(ar_name(AR_COUNT - 1), "ACH_Deprecated2") == 0);
  CHECK(ar_name(-1) == nullptr && ar_name(AR_COUNT) == nullptr);
}
// The backup file's text, whole.
static void tb_0406_ar_backup_text() {
  const ArEntry e[] = {
    ar_entry(0, "ACH_A", "Alpha", 1, 1, 1302728131, 180), ar_entry(1, "ACH_B", "Beta", 1, 0, 0, 0),
    ar_entry(2, "ACH_C", "(no name)", 0, 0, 0, 0),         ar_entry(3, "ACH_D", "Delta", 1, 1, 0, 0),
  };
  const ArTime gen = { 2026, 10, 8, 9, 5, 7, 180 };
  char b[2048];
  const int n = ar_backup_text(e, 4, gen, b, sizeof b);
  const char* want =
    "Nuke Panel achievement backup\r\n"
    "Generated: 2026-10-08 09:05:07 UTC+03:00\r\n"
    "Game: Duke Nukem Forever (Steam app 57900)\r\n"
    "Achievements: 4 in the game's list; 2 unlocked, 1 locked, 1 not answered by Steam\r\n"
    "\r\n"
    "UNLOCKED (2), oldest first: API name | name | unlocked (local time)\r\n"
    "ACH_A | Alpha | 2011-04-13 23:55:31 UTC+03:00\r\n"
    "ACH_D | Delta | time unknown\r\n"
    "\r\n"
    "LOCKED (1): API name | name\r\n"
    "ACH_B | Beta\r\n"
    "\r\n"
    "NOT ANSWERED BY STEAM (1): API name\r\n"
    "ACH_C\r\n";
  CHECK(n == (int)strlen(want));
  CHECK(strcmp(b, want) == 0);
  CHECK_MSG(ar_backup_text(e, 4, gen, b, 64) < 0, "a buffer too small is a failure, not a cut file");
  const int m = ar_backup_text(e, 0, gen, b, sizeof b);
  CHECK_MSG(m > 0 && strstr(b, "UNLOCKED (0)") && strstr(b, "(none)"), "an empty section says so");
}

// An arm lasts only while its row stays on screen: a frame without it (panel closed, another tab, scrolled out)
// disarms, and says so only for a live arm.
static void tb_0407_ar_disarm_off_screen() {
  ArConfirm c = ar_confirm_init();
  CHECK_MSG(!ar_frame_check(&c, 0, 100), "nothing armed: nothing to drop");
  ar_click(&c, 1000);
  CHECK_MSG(!ar_frame_check(&c, 1, 1100) && ar_armed(c, 1100), "the row on screen keeps the arm");
  CHECK_MSG(ar_frame_check(&c, 0, 1200) && !ar_armed(c, 1200), "a frame without the row disarms");
  CHECK_MSG(ar_click(&c, 1300) == AR_ACT_ARM, "the next click arms again, never confirms");
  ArConfirm l = ar_confirm_init();
  ar_click(&l, 1000);
  CHECK_MSG(!ar_frame_check(&l, 0, 1000 + AR_CONFIRM_MS) && !l.armed, "a lapsed arm is cleared quietly");
}
// A confirm under AR_CONFIRM_MIN_MS after the arm (a double-click) is ignored and the arm stays.
static void tb_0408_ar_confirm_min_gap() {
  ArConfirm c = ar_confirm_init();
  CHECK(ar_click(&c, 1000) == AR_ACT_ARM);
  CHECK_MSG(ar_click(&c, 1000 + AR_CONFIRM_MIN_MS - 1) == AR_ACT_IGNORED, "a double-click does not confirm");
  CHECK_MSG(ar_armed(c, 1000 + AR_CONFIRM_MIN_MS) && c.armedAt == 1000, "the ignored click neither disarms nor re-arms");
  CHECK(ar_click(&c, 1000 + AR_CONFIRM_MIN_MS) == AR_ACT_CONFIRM);
  ArConfirm w = ar_confirm_init();
  ar_click(&w, 0xFFFFFF00u);
  CHECK_MSG(ar_click(&w, 0x00000010u) == AR_ACT_IGNORED, "the gap is an unsigned age across the wrap");
}
// A confirmed reset runs only if its backup is written within AR_RESET_TTL_MS of the confirm.
static void tb_0409_ar_reset_expires() {
  CHECK(ar_reset_now(1, 1, 5000, 5000 + AR_RESET_TTL_MS - 1));
  CHECK_MSG(!ar_reset_now(1, 1, 5000, 5000 + AR_RESET_TTL_MS), "a backup that lands at 10 s drops the reset");
  CHECK_MSG(!ar_reset_now(1, 0, 5000, 5001), "a failed backup refuses the reset");
  CHECK_MSG(!ar_reset_now(0, 1, 5000, 5001), "a plain backup never resets");
  CHECK(!ar_reset_expired(0xFFFFF000u, 0x00000100u) && ar_reset_expired(0xFFFFF000u, 0xFFFFF000u + AR_RESET_TTL_MS));
}

// photo.h: every tile is rendered PHOTO_MARGIN px wider than it keeps on each side, and the margin is cropped. The
// tile's shifts are the engine's own scaled by the kept share (UpdateTransforms 0x10214390: x' = N x + XShift, y' = N y
// + YShift), so the kept pixels of neighbouring tiles abut exactly: rendered pixel M of tile c is photo pixel c K.
static double tb_photo_px(int n, int s, int tile, int p, int vertical) {
  PhotoSm t = photo_begin(n);
  t.phase = PH_TILE;
  if (vertical) t.r = tile; else t.c = tile;
  int scale = 0;
  float x = 0.f, y = 0.f;
  photo_megashot_k(t, photo_keep_ratio(s), photo_keep_ratio(s), &scale, &x, &y);
  const double k = (double)photo_kept(s) / s;
  if (!vertical) {   // NDC x grows right; pixel 0 is the left edge
    const double u = ((-1.0 + 2.0 * p / s) - x) / scale;
    return (u + k) * n * s / 2.0;
  }
  const double v = ((1.0 - 2.0 * p / s) - y) / scale;   // NDC y grows up; pixel row 0 is the top
  return (k - v) * n * s / 2.0;
}
static void tb_0410_photo_margin_math() {
  CHECK(photo_margin(3440) == PHOTO_MARGIN && photo_kept(3440) == 3440 - 2 * PHOTO_MARGIN && PHOTO_MARGIN >= 8);
  CHECK_MSG(photo_kept(40) == 40, "a size too small for a margin keeps every pixel");
  int exact = 0;
  for (int c = 0; c < 4; ++c) {
    const int K = photo_kept(3440), M = photo_margin(3440);
    exact += fabs(tb_photo_px(4, 3440, c, M, 0) - c * K) < 1e-3 && fabs(tb_photo_px(4, 3440, c, 3440 - M, 0) - (c + 1) * K) < 1e-3;
    const int Kh = photo_kept(1440), Mh = photo_margin(1440);
    exact += fabs(tb_photo_px(4, 1440, c, Mh, 1) - c * Kh) < 1e-3 && fabs(tb_photo_px(4, 1440, c, 1440 - Mh, 1) - (c + 1) * Kh) < 1e-3;
  }
  CHECK_MSG(exact == 8, "the kept parts of the tiles abut to the pixel, across and down");
  PhotoSm s = photo_begin(3);
  s.phase = PH_TILE;
  s.r = 2;
  s.c = 1;
  int scale = 0;
  float x = 9.f, y = 9.f;
  photo_megashot_k(s, 1.f, 1.f, &scale, &x, &y);
  CHECK_MSG(scale == 3 && x == 0.f && y == 2.f, "no margin: the engine's own shifts");
  photo_megashot_k(s, 0.5f, 0.25f, &scale, &x, &y);
  CHECK(scale == 3 && x == 0.f && y == 0.5f);
  CHECK_MSG(photo_tile_src_offset(100, 3, 2, 5) == (2 + 5) * 100 + 3 * 4, "the crop's first byte: margin rows and columns");
}
// photo.h: the mirrors render with the full frustum, never the tile's (UpdateTransforms skips MegaShot for IsMirror
// frames), so a reflection jumps at every seam: both mirror switches go off with TakeMegaShot's four.
static void tb_0411_photo_capture_cvars() {
  CHECK(PHOTO_CVAR_COUNT == 6);
  int mirrors = 0;
  for (int i = 0; i < PHOTO_CVAR_COUNT; ++i)
    mirrors += wcscmp(photo_cvar_name(i), L"r_DrawBSPMirrors") == 0 || wcscmp(photo_cvar_name(i), L"r_DrawActorMirrors") == 0;
  CHECK_MSG(mirrors == 2, "BSP and actor mirrors are off for the capture");
  CHECK(wcscmp(photo_cvar_name(0), L"r_DrawConsole") == 0);
}

// photo.h: a tile row is captured in B bands, each a render of the row's tiles keeping one band of rows, so two
// strips in flight stay within 256 MB up to 16x on any screen up to 4K. A band is a strip: flushed, encoded, freed.
static void tb_0412_photo_bands() {
  CHECK_MSG(photo_bands(photo_kept(3440), photo_kept(1440), 9) == 1, "9x on 3440x1440: one band, as before");
  CHECK_MSG(photo_bands(photo_kept(3440), photo_kept(1440), 16) == 2, "16x on 3440x1440: two bands");
  CHECK_MSG(photo_bands(photo_kept(3840), photo_kept(2160), 16) == 3, "16x at 4K: three bands");
  const int B = photo_bands(photo_kept(3840), photo_kept(2160), 16), rows = photo_band_rows(photo_kept(2160), B);
  CHECK(2ull * photo_stride(photo_kept(3840) * 16) * (unsigned long long)rows <= PHOTO_BUDGET_BYTES);
  CHECK(photo_band_rows(10, 3) == 4 && photo_chunk_rows_in(2, 10, 4) == 2);
  CHECK_MSG(photo_bands(19968, 19968, 2) == 0, "not even PHOTO_BANDS_MAX bands fit: refused");
  PhotoSm s = photo_begin(2);
  s.bands = 2;
  PhotoIn in = ph_in(1);
  in.press = 1;
  photo_step(&s, in);
  in.press = 0;
  for (int i = 0; i < PHOTO_SETTLE_FRAMES; ++i) photo_step(&s, in);
  in.tileRead = 1;
  int flushes[4] = { -1, -1, -1, -1 }, nf = 0, acts = 0;
  for (int i = 0; i < 24 && s.phase != PH_DRAIN; ++i) {
    in.bufferFree = s.phase == PH_WAIT;
    if (s.phase == PH_TILE && s.r == 0 && s.b == 1 && s.c == 1) acts += photo_tiles_done(s) == 3;
    if (photo_step(&s, in) == PA_FLUSH && nf < 4) flushes[nf++] = s.flushRow;
  }
  CHECK_MSG(nf == 4 && flushes[0] == 0 && flushes[1] == 1 && flushes[2] == 2 && flushes[3] == 3,
            "a strip per band, top band first, row by row");
  CHECK_MSG(acts == 1 && photo_tiles_total(s) == 8, "the progress counts every render");
  CHECK(photo_begin(3).bands == 1);
}

// freecam.h: in flight the wheel sets the camera's own field of view in DNMP's 2-degree notches within the Custom FOV
// slider's range, away from you narrowing; a part notch carries over. At the flight's end Pawn.FovAngle goes back
// only while ours is still in it; the Debug readout shows it.
static void tb_0413_freecam_wheel_fov() {
  int res = 0;
  CHECK(fcf_take_notches(&res, 240) == 2 && res == 0);
  CHECK_MSG(fcf_take_notches(&res, 60) == 0 && fcf_take_notches(&res, 60) == 1 && res == 0, "part notches add up");
  CHECK(fcf_take_notches(&res, -130) == -1 && res == -10);
  CHECK_MSG(fcf_deg_after(90.f, 1) == 88.f && fcf_deg_after(90.f, -3) == 96.f, "away narrows, toward widens");
  CHECK_MSG(fcf_deg_after(41.f, 5) == (float)FOV_MIN_DEG && fcf_deg_after(149.f, -5) == (float)FOV_MAX_DEG,
            "the Custom FOV slider's range");
  CHECK_MSG(fcf_restore(1, 70.f, 70.f) && !fcf_restore(1, 85.f, 70.f) && !fcf_restore(0, 70.f, 70.f),
            "given back only while ours is in place");
  CHECK_MSG(fcf_offset_ok(0x1274) && !fcf_offset_ok(0x1270), "FovAngle where CreateMasterFrame reads it");
  FreecamPose p = { { 1.f, 2.f, 3.f }, 10.f, -5.f };
  char line[96];
  freecam_readout_fov(p, 72.5f, line, sizeof line);
  CHECK(strcmp(line, "cam x=1 y=2 z=3 yaw=10.0 pitch=-5.0 fov=72.5") == 0);
}

// The fists (dnGame.MightyFoot, config MightyFootWeaponConfig) are the melee kind: the gate opens for them.
static void tb_0414_melee_kind() {
  CHECK(aim_weapon_kind(L"MightyFootWeaponConfig") == AIMW_MELEE);
  const AimGate g = { 1, 0, 1, 1, 0, 0, 0, 0, AIMW_MELEE };
  CHECK_MSG(aim_gate(g) == AIM_GO, "the fists open the gate like a gun");
  CHECK_MSG(aim_gun_kind(AIMW_HITSCAN) && aim_gun_kind(AIMW_PROJECTILE) && aim_gun_kind(AIMW_BEAM) &&
            !aim_gun_kind(AIMW_MELEE) && !aim_gun_kind(AIMW_NONE), "guns only");
}

// The melee reach (Weapon.PerformMelee): MeleeTraceDistance (Shrunk while shrunk) plus ShrinkScale x MeleeHitExtent.
static void tb_0415_melee_reach() {
  CHECK(aim_melee_reach(90.f, 30.f, 10.f, 0, 1.f) == 100.f);
  CHECK_MSG(aim_melee_reach(90.f, 30.f, 10.f, 1, 0.25f) == 32.5f, "shrunk: the shrunk distance, the extent scaled");
  CHECK_MSG(aim_melee_reach(0.f, 30.f, 10.f, 0, 1.f) == 0.f && aim_melee_reach(sqrtf(-1.f), 0.f, 10.f, 0, 1.f) == 0.f,
            "unread: no reach");
  // The crosshair: LookHitDistance is a surface distance, compared with the reach.
  CHECK_MSG(tbot_crosshair_enemy(1, 1, 0, 100.f, AIMW_MELEE, 100.f), "within reach");
  CHECK_MSG(!tbot_crosshair_enemy(1, 1, 0, 101.f, AIMW_MELEE, 100.f), "beyond reach");
  CHECK_MSG(!tbot_crosshair_enemy(1, 1, 0, 50.f, AIMW_MELEE, 0.f), "an unread reach never punches");
  CHECK_MSG(!tbot_crosshair_enemy(1, 1, 0, 40.f, AIMW_MELEE, aim_melee_reach(90.f, 30.f, 10.f, 1, 0.25f)),
            "shrunk: 40 units is out of reach");
  // The distance-to-centre picks add the target's collision radius.
  CHECK(aim_melee_centre_ok(130.f, 100.f, 30.f) && !aim_melee_centre_ok(131.f, 100.f, 30.f));
  CHECK_MSG(!aim_melee_centre_ok(10.f, 0.f, 30.f) && !aim_melee_centre_ok(sqrtf(-1.f), 100.f, 30.f), "unread or NaN");
}

// The aimbot does not lock while the held gun is completely out of ammo; never for the fists or an unread answer.
static void tb_0416_aimbot_out_of_ammo() {
  CHECK(aimbot_out_of_ammo(AIMW_HITSCAN, 1) && aimbot_out_of_ammo(AIMW_PROJECTILE, 1) &&
        aimbot_out_of_ammo(AIMW_BEAM, 1));
  CHECK_MSG(!aimbot_out_of_ammo(AIMW_MELEE, 1) && !aimbot_out_of_ammo(AIMW_NONE, 1), "the fists: never");
  CHECK_MSG(!aimbot_out_of_ammo(AIMW_HITSCAN, 0) && !aimbot_out_of_ammo(AIMW_HITSCAN, -1), "ammo left, or unread");
  CHECK(aimbot_hold_why(1, 1, 0, AIM_GO, 0) == 0);
  CHECK(strcmp(aimbot_hold_why(1, 1, 0, AIM_GO, 1), "out of ammo") == 0);
  CHECK_MSG(strcmp(aimbot_hold_why(0, 1, 0, AIM_GO, 1), "switched off") == 0 &&
            strcmp(aimbot_hold_why(1, 0, 0, AIM_GO, 1), "key up") == 0 &&
            strcmp(aimbot_hold_why(1, 1, 1, AIM_GO, 1), "panel open") == 0 &&
            strcmp(aimbot_hold_why(1, 1, 0, AIM_G_DEAD, 1), "player dead") == 0, "the earlier reasons first");
}

// Silent aim never covers a punch (PerformMelee never passes the bracket); the fists hold the trigger.
static void tb_0417_tbot_melee() {
  CHECK_MSG(!tbot_locked_covered(AIMW_MELEE, 1), "melee: not covered");
  CHECK_MSG(tbot_holds(TBOT_WHEN_LOCKED, 1, 0, 1, 0) && !tbot_holds(TBOT_WHEN_LOCKED, 0, 0, 1, 0, 0),
            "Locked with the fists falls back as for an uncovered weapon");
  CHECK_MSG(tbot_fire_mode_kind(AIMW_MELEE, TBOT_FIRE_PULSE) == TBOT_FIRE_HOLD &&
            tbot_fire_mode_kind(AIMW_HITSCAN, TBOT_FIRE_PULSE) == TBOT_FIRE_PULSE, "the fists hold");
  CHECK(tbot_punch_began(5, 6, TBOT_WSTATE_MELEE) && !tbot_punch_began(6, 6, TBOT_WSTATE_MELEE));
  CHECK_MSG(!tbot_punch_began(5, 6, 3) && !tbot_punch_began(-1, 6, TBOT_WSTATE_MELEE), "another state, or unread");
}

// A fully mounted turret opens the attached gate (docs/re/turrets.md section 6); nothing else attached does.
static void tb_0420_turret_gate() {
  CHECK_MSG(aim_turret_mounted(1, AIM_CRS_FULLY_ATTACHED, 1), "the turret remapper, fully attached, on a turret");
  CHECK_MSG(!aim_turret_mounted(1, AIM_CRS_FULLY_ATTACHED - 1, 1) && !aim_turret_mounted(1, AIM_CRS_FULLY_ATTACHED + 1, 1),
            "interpolating in or out");
  CHECK_MSG(!aim_turret_mounted(0, AIM_CRS_FULLY_ATTACHED, 1) && !aim_turret_mounted(1, AIM_CRS_FULLY_ATTACHED, 0),
            "another remapper (an execution, a ladder), or no turret under it");
  AimGate g = { 1, 0, 1, 1, 0, 1, 0, 0, AIMW_HITSCAN, 1 };
  CHECK_MSG(aim_gate(g) == AIM_GO, "a mounted turret: go");
  g.mountedTurret = 0;
  CHECK_MSG(aim_gate(g) == AIM_G_ATTACHED, "attached without a mounted turret stays closed");
  g.mountedTurret = 1;
  g.panelOpen = 1;
  CHECK_MSG(aim_gate(g) == AIM_G_PANEL, "the earlier gates still close it");
  g.panelOpen = 0;
  g.weaponKind = AIMW_NONE;
  CHECK_MSG(aim_gate(g) == AIM_G_WEAPON, "an unknown turret class");
  // ViewMapper and InteractiveDecoration close the gate unless they are the mounted turret's own.
  static int objs_[2];  // distinct storage: an optimiser may fold equal unwritten locals
  int& a = objs_[0]; int& b = objs_[1];
  CHECK_MSG(!aim_mount_closes(1, &a, &a) && aim_mount_closes(1, &a, &b), "the turret's own, or another's");
  CHECK_MSG(aim_mount_closes(0, &a, &a) && !aim_mount_closes(1, 0, &a), "not mounted, or unset");
  // Silent aim: a gate reason of its own on a mounted gun it cannot bracket (turrets.md section 8).
  CHECK(aim_silent_gate(AIM_GO, 1, 0) == AIM_G_MOUNTED && aim_silent_gate(AIM_GO, 0, 0) == AIM_GO);
  CHECK(aim_silent_gate(AIM_G_PANEL, 1, 0) == AIM_G_PANEL);
  CHECK(strcmp(aim_gate_name(AIM_G_MOUNTED), "this mounted gun cannot be bracketed") == 0);
}

// The turret class to weapon kind: the launcher first (it derives from the MG42).
static void tb_0421_turret_kind() {
  const wchar_t* gl[3] = { L"dnControl_Turret_MountedGrenadeLauncher", L"dnControl_Turret_MG42", L"dnControl_Turret" };
  CHECK_MSG(aim_turret_class(gl, 3) == AIM_TUR_LAUNCHER, "the launcher before the MG42 it derives from");
  CHECK(aim_turret_class(gl + 1, 2) == AIM_TUR_MG42);
  const wchar_t* st[2] = { L"dnControl_Turret_SuperTurret", L"dnControl_Turret" };
  CHECK(aim_turret_class(st, 2) == AIM_TUR_SUPER);
  CHECK(aim_turret_class(gl + 2, 1) == AIM_TUR_NONE && aim_turret_class(0, 0) == AIM_TUR_NONE);
  AimTurretKind k = aim_turret_kind(AIM_TUR_MG42, 0.f, 0.f);
  CHECK(k.kind == AIMW_HITSCAN && k.maxTrace == 50000.f);
  CHECK_MSG(aim_turret_kind(AIM_TUR_MG42, 40000.f, 0.f).maxTrace == 40000.f, "the live ShotDistance");
  CHECK(aim_turret_kind(AIM_TUR_SUPER, 0.f, 0.f).kind == AIMW_HITSCAN);
  k = aim_turret_kind(AIM_TUR_LAUNCHER, 0.f, 0.f);
  CHECK(k.kind == AIMW_PROJECTILE && k.speed == 2500.f);
  CHECK_MSG(aim_turret_kind(AIM_TUR_LAUNCHER, 0.f, 2600.f).speed == 2600.f, "Map12's GrenadeVelocity");
  CHECK(aim_turret_kind(AIM_TUR_NONE, 0.f, 0.f).kind == AIMW_NONE);
}

// A target is a candidate only inside the turret's live limits, relative to the gun (RotatePitch/RotateYaw).
static void tb_0422_turret_limits() {
  const AimTurretLimits l = { -8192, 8192, -8192, 8192, 1 };
  CHECK_MSG(aim_turret_reachable(l, 0, 0, 0, 0, 1000, 8000), "inside");
  CHECK_MSG(!aim_turret_reachable(l, 0, 0, 0, 0, 0, 9000), "yaw outside");
  CHECK_MSG(!aim_turret_reachable(l, 0, 0, 0, 0, -9000, 0), "pitch below");
  CHECK_MSG(aim_turret_reachable(l, 0, 4000, 0, 20000, 0, 24000), "RotateYaw 4000 plus 4000 more");
  CHECK_MSG(!aim_turret_reachable(l, 0, 7000, 0, 20000, 0, 24000), "7000 plus 4000 is past MaxYaw");
  AimTurretLimits fr = l;
  fr.clampYaw = 0;
  CHECK_MSG(aim_turret_reachable(fr, 0, 0, 0, 0, 0, 32000), "bClampYaw off: any yaw");
  CHECK_MSG(!aim_turret_reachable(fr, 0, 0, 0, 0, 9000, 32000), "but the pitch still");
  CHECK_MSG(aim_turret_reachable(l, 0, 0, 0, 65000, 0, 100), "the short way round the wrap");
}

// Mounted, the press is DnControlFireEnable/Disable, held, and released in the mode it was made in.
static void tb_0423_tbot_mounted_press() {
  CHECK(wcscmp(tbot_press_fn(1, 1), L"DnControlFireEnable") == 0 && wcscmp(tbot_press_fn(1, 0), L"DnControlFireDisable") == 0);
  CHECK(wcscmp(tbot_press_fn(0, 1), L"FireDown") == 0 && wcscmp(tbot_press_fn(0, 0), L"FireUp") == 0);
  CHECK_MSG(tbot_mode_gate(1, 1, 1, 1) && tbot_mode_gate(1, 0, 1, 0), "the same mode, or nothing pending");
  CHECK_MSG(!tbot_mode_gate(1, 1, 1, 0) && !tbot_mode_gate(1, 1, 0, 1), "the mode changed under our press: released");
  CHECK(!tbot_mode_gate(0, 0, 0, 0));
  CHECK_MSG(tbot_mounted_cadence(1, 1) == 0 && tbot_mounted_cadence(0, 1) == 1, "mounted: never pulsed or tapped");
  TbotPress p = TbotPress();
  CHECK(tbot_act(&p, 1, 1, 0, 0, 0, 1000) == TB_DOWN);
  CHECK_MSG(tbot_act(&p, tbot_mode_gate(1, p.ours, 1, 0), 1, 0, 0, 1, 1016) == TB_UP && !p.ours, "dismounted: released");
}

// Mounted on an instant gun (bUseInstantAimRate: the MG42 and the launcher, verified with the snap): the whole turn
// in one event, the user's smoothing alone, never the damping. A rate-limited gun takes the slower of the damped step
// and the user's smoothing, never both stacked.
static void tb_0424_aimbot_mounted_instant_snap() {
  const AimbotView v = { 6036, 6036, 17124, 16384, 49152, 0, 0 };
  const int cam[2] = { 6036, 17124 };
  int fP = 0, fY = 0;
  aimbot_turn(v, 0, 0, 1225, 28656, 0, 1.0 / 175.0, &fP, &fY);
  AimbotMountState st;
  memset(&st, 0, sizeof st);
  int dP = fP, dY = fY;
  CHECK(aimbot_mount_step(&st, 1, cam, fP, fY, &dP, &dY) == 1);
  CHECK_MSG(abs(6036 + dP - 1225) <= 64 && abs(17124 + dY - 28656) <= 64, "instant: on target in one event");
  int sP = 0, sY = 0;
  aimbot_turn(v, 0, 0, 1225, 28656, 50, 1.0 / 30.0, &sP, &sY);
  dP = sP;
  dY = sY;
  aimbot_mount_step(&st, 1, cam, fP, fY, &dP, &dY);
  CHECK_MSG(dP == sP && dY == sY, "instant: the user's smoothing alone");
  memset(&st, 0, sizeof st);
  dP = fP;
  dY = fY;
  aimbot_mount_step(&st, 0, cam, fP, fY, &dP, &dY);
  CHECK_MSG(dP == fP / 2 && dY == fY / 2, "rate-limited, no smoothing: the damped half");
  memset(&st, 0, sizeof st);
  aimbot_turn(v, 0, 0, 1225, 28656, 80, 1.0 / 30.0, &sP, &sY);
  dP = sP;
  dY = sY;
  aimbot_mount_step(&st, 0, cam, fP, fY, &dP, &dY);
  CHECK_MSG(dP == sP && dY == sY, "rate-limited, smoothing 80: the slower, not both");
  memset(&st, 0, sizeof st);
  dP = fP;
  dY = fY;
  aimbot_mount_step(&st, -1, cam, fP, fY, &dP, &dY);
  CHECK_MSG(dP == fP / 2, "unread: damped, the safe side");
}

// The mount log: a pause or a faulted read (not read) changes nothing; a dismount after AIM_MOUNT_OFF_FRAMES read
// frames off, at once when another gun is mounted.
static void tb_0425_mount_log_debounce() {
  AimMountLog s = AimMountLog();
  int a = 0, b = 0;
  CHECK(aim_mount_log_step(&s, 1, &a) == AIM_ML_ATTACH && aim_mount_log_step(&s, 1, &a) == AIM_ML_NONE);
  int pause = AIM_ML_NONE;
  for (int i = 0; i < 64; i++) pause |= aim_mount_log_step(&s, 0, 0);
  CHECK_MSG(pause == AIM_ML_NONE && aim_mount_log_step(&s, 1, &a) == AIM_ML_NONE, "a pause: no dismount, no attach");
  int off = AIM_ML_NONE;
  for (int i = 0; i < AIM_MOUNT_OFF_FRAMES; i++) off |= aim_mount_log_step(&s, 1, 0);
  CHECK_MSG(off == AIM_ML_NONE && aim_mount_log_step(&s, 1, &a) == AIM_ML_NONE, "a short gap: kept");
  for (int i = 0; i < AIM_MOUNT_OFF_FRAMES; i++) aim_mount_log_step(&s, 1, 0);
  CHECK_MSG(aim_mount_log_step(&s, 1, 0) == AIM_ML_DISMOUNT, "off past the debounce: dismounted");
  CHECK(aim_mount_log_step(&s, 1, 0) == AIM_ML_NONE && aim_mount_log_step(&s, 1, &a) == AIM_ML_ATTACH);
  CHECK_MSG(aim_mount_log_step(&s, 1, &b) == AIM_ML_SWITCH, "another gun: at once");
}

// progress.h: pg_pick_map_name keeps a chapter map's package name and takes LevelInfo.MapFileName for a checkpoint
// package (cpNN) or anything else; neither known gives -1.
static void tb_0426_pg_pick_map_name() {
  PgTable t;
  pg_table_builtin(&t);
  CHECK(strcmp(pg_pick_map_name(t, "Map05", "Map09"), "Map05") == 0);
  CHECK(strcmp(pg_pick_map_name(t, "cp01", "Map05"), "Map05") == 0);
  CHECK(pg_chapter_of_map(t, pg_pick_map_name(t, "cp01", "Map05")) == 5);
  CHECK(pg_chapter_of_map(t, pg_pick_map_name(t, "cp01", "")) == -1);
  CHECK(pg_chapter_of_map(t, pg_pick_map_name(t, 0, 0)) == -1);
  CHECK(pg_chapter_of_map(t, pg_pick_map_name(t, "Map_mydigs", "Map_x")) == -1);
}

// aircraft.h: hostile and live (docs/re/aircraft.md sections 2-3): an enemy, shown, not being destroyed, DecoState
// alive, Health above 0. The fighter is hostile by class.
static void tb_0427_ac_hostile_live() {
  CHECK(ac_hostile_live(AC_DROPSHIP, 1, 0, 0, 0, 1200.f, 0, 1) && ac_hostile_live(AC_GUNSHIP, 1, 0, 0, 0, 1.f, 0, 1));
  CHECK_MSG(!ac_hostile_live(AC_DROPSHIP, 0, 0, 0, 0, 1200.f, 0, 1), "bEnemy off");
  CHECK_MSG(!ac_hostile_live(AC_DROPSHIP, 1, 1, 0, 0, 1200.f, 0, 1), "placed hidden until its script runs");
  CHECK(!ac_hostile_live(AC_DROPSHIP, 1, 0, 1, 0, 1200.f, 0, 1));
  CHECK_MSG(!ac_hostile_live(AC_GUNSHIP, 1, 0, 0, 1, 1200.f, 0, 1) && !ac_hostile_live(AC_GUNSHIP, 1, 0, 0, 2, 1200.f, 0, 1),
            "DecoState 1 and 2 are dead");
  CHECK_MSG(ac_hostile_live(AC_GUNSHIP, 1, 0, 0, -1, 5.f, 0, 1), "an unread DecoState is not dead");
  CHECK(!ac_hostile_live(AC_DROPSHIP, 1, 0, 0, 0, 0.f, 0, 1) && !ac_hostile_live(AC_DROPSHIP, 1, 0, 0, 0, nanf(""), 0, 1));
  CHECK_MSG(ac_hostile_live(AC_FIGHTER, 0, 0, 0, 0, 150.f, 0, 1), "a damageable fighter, by class");
  CHECK_MSG(!ac_hostile_live(AC_FIGHTER, 0, 0, 0, 0, 150.f, 1, 1), "the logged scenic fighter: bNoDamage 1");
  CHECK_MSG(!ac_hostile_live(AC_FIGHTER, 0, 0, 0, 0, 150.f, 0, 0), "not vulnerable: scenery");
  CHECK_MSG(ac_hostile_live(AC_FIGHTER, 0, 0, 0, 0, 150.f, 0, -1), "an unread bIsVulnerable counts as vulnerable");
  CHECK_MSG(ac_hostile_live(AC_DROPSHIP, 1, 0, 0, 0, 1200.f, 1, 0), "a dropship ignores both fighter flags");
  CHECK(!ac_hostile_live(-1, 1, 0, 0, 0, 100.f, 0, 1));
}

// aircraft.h: the EDF wasps are matched only to be refused, whatever bEnemy reads.
static void tb_0428_ac_wasp_never_hostile() {
  int wasps = 0;
  for (int r = 0; r < AC_CLASS_COUNT; r++)
    if (strncmp(AC_CLASSES[r].name, "dnEDFWasp", 9) == 0) {
      wasps++;
      CHECK(ac_kind_of_rule(r) == AC_WASP);
    }
  CHECK(wasps == 2);
  CHECK(!ac_enemy(AC_WASP, 1, 0, 1) && !ac_hostile_live(AC_WASP, 1, 0, 0, 0, 500.f, 0, 1));
  CHECK(ac_kind_of_rule(-1) == -1 && ac_kind_of_rule(AC_CLASS_COUNT) == -1);
  CHECK(strcmp(ac_label(AC_DROPSHIP), "Dropship") == 0 && strcmp(ac_label(AC_GUNSHIP), "Gunship") == 0 &&
        strcmp(ac_label(AC_FIGHTER), "Fighter") == 0);
}

// aircraft.h: damageable (aircraft.md section 2): bNoDamage, HealthPrefab NeverBreak, the fighter's bIsVulnerable.
static void tb_0429_ac_damageable() {
  CHECK(ac_damageable(AC_DROPSHIP, 0, 5, 0) && ac_damageable(AC_GUNSHIP, 0, -1, -1));
  CHECK_MSG(!ac_damageable(AC_DROPSHIP, 1, 5, 1), "bNoDamage");
  CHECK_MSG(!ac_damageable(AC_DROPSHIP, 0, 0, 1), "Map11C's dummies: HealthPrefab 0");
  CHECK_MSG(!ac_damageable(AC_FIGHTER, 0, 5, 0) && ac_damageable(AC_FIGHTER, 0, 5, 1), "the fighter's bIsVulnerable");
  CHECK_MSG(!ac_damageable(AC_FIGHTER, 0, 5, -1), "an unread bIsVulnerable never targets a fighter");
}

// aircraft.h: only explosions and the MG42's large-calibre bullets hurt the ships.
static void tb_0430_ac_weapon_hurts() {
  const wchar_t* rpg[1] = { L"RPGWeaponConfig" };
  const wchar_t* dev[1] = { L"DevastatorWeaponConfig" };
  const wchar_t* pipe[1] = { L"PipeBombWeaponConfig" };
  const wchar_t* dual[2] = { L"DualPistolWeaponConfig", L"PistolWeaponConfig" };
  const wchar_t* guns[5] = { L"ShotgunWeaponConfig", L"RailgunWeaponConfig", L"MachineGunWeaponConfig",
                             L"ATLaserWeaponConfig", L"MightyFootWeaponConfig" };
  CHECK(ac_weapon_hurts(rpg, 1, AIM_TUR_NONE) && ac_weapon_hurts(dev, 1, AIM_TUR_NONE) &&
        ac_weapon_hurts(pipe, 1, AIM_TUR_NONE));
  CHECK_MSG(!ac_weapon_hurts(dual, 2, AIM_TUR_NONE), "bullets do nothing");
  for (int i = 0; i < 5; i++) CHECK(!ac_weapon_hurts(guns + i, 1, AIM_TUR_NONE));
  CHECK_MSG(ac_weapon_hurts(dual, 2, AIM_TUR_MG42) && ac_weapon_hurts(0, 0, AIM_TUR_LAUNCHER) &&
            ac_weapon_hurts(0, 0, AIM_TUR_SUPER), "every mounted gun");
  CHECK(!ac_weapon_hurts(0, 0, AIM_TUR_NONE));
}

// aircraft.h: the pick order. A pawn always wins; a ship only with no pawn picked and a weapon that hurts it.
static void tb_0431_ac_pick_order() {
  CHECK_MSG(!ac_ship_pass(1, 1) && !ac_ship_pass(1, 0), "a pawn candidate always wins");
  CHECK(ac_ship_pass(0, 1));
  CHECK_MSG(!ac_ship_pass(0, 0), "never with a gun that cannot hurt them");
  // The pawn pass's aim_pick: a visible pawn gives a pick, so the ship pass never runs.
  AimCand pawns[2] = { { 3, 5.f, AIM_VIS_NO }, { 7, 9.f, AIM_VIS_YES } };
  CHECK(!ac_ship_pass(aim_pick(pawns, 2, -1) >= 0, 1));
  AimCand hidden[1] = { { 3, 5.f, AIM_VIS_NO } };
  CHECK(ac_ship_pass(aim_pick(hidden, 1, -1) >= 0, 1));
}

// aircraft.h: a pawn pass stopped on an unknown pawn keeps the previous ship from the cache alone; with no
// previous ship, or a pawn picked, or a gun that cannot hurt them, no ship.
static void tb_0433_ac_keep_ship_on_unknown_pawn() {
  CHECK(ac_ship_pass_mode(0, 0, 1, 0) == AC_PASS_FULL && ac_ship_pass_mode(0, 0, 1, 1) == AC_PASS_FULL);
  CHECK_MSG(ac_ship_pass_mode(0, 1, 1, 1) == AC_PASS_CACHE, "unknown pawn, previous ship: the cache alone");
  CHECK_MSG(ac_ship_pass_mode(0, 1, 1, 0) == AC_PASS_NONE, "unknown pawn, no previous ship: none");
  CHECK(ac_ship_pass_mode(1, 0, 1, 1) == AC_PASS_NONE && ac_ship_pass_mode(1, 1, 1, 1) == AC_PASS_NONE);
  CHECK(ac_ship_pass_mode(0, 0, 0, 1) == AC_PASS_NONE && ac_ship_pass_mode(0, 1, 0, 1) == AC_PASS_NONE);
  // The cache-only walk: the nearer ship unknown (no trace), the previous one cached visible: kept.
  AimCand ships[2] = { { 0, 5.f, AIM_VIS_UNKNOWN }, { 1, 9.f, AIM_VIS_YES } };
  CHECK(aim_pick(ships, 2, 1, AIM_PICK_CROSSHAIR) == 1);
  ships[1].vis = AIM_VIS_UNKNOWN;
  CHECK_MSG(aim_pick(ships, 2, 1, AIM_PICK_CROSSHAIR) < 0, "its cache expired: dropped");
}

// aircraft.h (Map10 cp02, MG42, 2026-10-10): two fighters near the crosshair, the nearer one (A) on the gun's pitch
// limit, strictly reachable only every other frame, and the pass alternating full and cache-only. The log swapped A and
// B every frame. The incumbent keeps through the limits with its hull's slack; the cache pass never switches.
static void tb_0446_ac_ship_target_stable() {
  const AimTurretLimits lim = { -5120, 4096, -6144, 6144, 1 };
  const int slackA = ac_limit_slack(116.f, 3400.f);
  CHECK(slackA > 300 && slackA <= AC_LIMIT_SLACK_MAX);
  CHECK(ac_limit_slack(10000.f, 100.f) == AC_LIMIT_SLACK_MAX && ac_limit_slack(100.f, 0.f) == 0);
  int target = -1, swaps = 0, oldTarget = -1, oldSwaps = 0;
  for (int f = 0; f < 16; f++) {
    const int camP = 4044 + (f % 2 ? 60 : -60);   // the camera against RotatePitch jitters by the turn
    const int wantA = 4150, wantB = 4000;
    const int strictA = ac_turret_reachable(lim, 3990, 0, camP, 0, wantA, 0, 0, 0);
    const int slackOkA = ac_turret_reachable(lim, 3990, 0, camP, 0, wantA, 0, slackA, slackA);
    const int strictB = ac_turret_reachable(lim, 3990, 0, camP, 0, wantB, 0, 0, 0);
    const int cacheOnly = f % 4 == 3;
    // The new rule.
    AimCand c[2];
    int nc = 0;
    if (ac_ship_admit(target == 0, cacheOnly, strictA, slackOkA)) { const AimCand a = { 0, 3400.f, AIM_VIS_YES }; c[nc++] = a; }
    if (ac_ship_admit(target == 1, cacheOnly, strictB, strictB)) { const AimCand b = { 1, 3900.f, AIM_VIS_YES }; c[nc++] = b; }
    aim_cand_sort(c, nc);
    const int at = aim_pick(c, nc, target, AIM_PICK_NEAREST);
    const int t = at >= 0 ? c[at].idx : -1;
    if (f > 0 && t != target) swaps++;
    target = t;
    // The old rule: every ship by the strict test, the cache pass free to pick any cached-visible ship.
    AimCand o[2];
    int no = 0;
    if (strictA) { const AimCand a = { 0, 3400.f, AIM_VIS_YES }; o[no++] = a; }
    if (strictB) { const AimCand b = { 1, 3900.f, AIM_VIS_YES }; o[no++] = b; }
    aim_cand_sort(o, no);
    const int ot = aim_pick(o, no, oldTarget, AIM_PICK_NEAREST);
    const int otIdx = ot >= 0 ? o[ot].idx : -1;
    if (f > 0 && otIdx != oldTarget) oldSwaps++;
    oldTarget = otIdx;
  }
  CHECK_MSG(oldSwaps >= 8, "the old rule reproduces the ping-pong");
  CHECK_MSG(swaps == 1 && target == 0, "A taken once by the margin, then kept");
  CHECK_MSG(!ac_ship_admit(0, 1, 1, 1) && ac_ship_admit(1, 1, 0, 1), "the cache pass keeps only the incumbent");
  CHECK(!ac_ship_admit(0, 0, 0, 1) && ac_ship_admit(0, 0, 1, 0));
}

// aircraft.h (2026-10-11, SuperTurret): the flip line names the first ship-pass test the lost target failed, in the
// pass's order; a target that passed them all and was still not picked was out-ranked.
static void tb_0517_ac_lost_reason() {
  const AcShipVerdict all = { 1, AC_PASS_FULL, 1, 1, 0, 1, 1, 1, 1, 1, AIM_VIS_YES, 1 };
  CHECK(ac_lost_reason(all) == AC_LOST_NONE);
  AcShipVerdict v = all;
  v.picked = 0;
  CHECK(ac_lost_reason(v) == AC_LOST_OUTRANKED);
  v.vis = AIM_VIS_UNKNOWN;
  CHECK(ac_lost_reason(v) == AC_LOST_UNKNOWN);
  v.vis = AIM_VIS_NO;
  CHECK(ac_lost_reason(v) == AC_LOST_HIDDEN);
  v.cone = 0;
  CHECK_MSG(ac_lost_reason(v) == AC_LOST_CONE, "the cone before visibility");
  v.limits = 0;
  CHECK(ac_lost_reason(v) == AC_LOST_LIMITS);
  v.reach = 0;
  CHECK(ac_lost_reason(v) == AC_LOST_REACH);
  v.onScreen = 0;
  CHECK(ac_lost_reason(v) == AC_LOST_SCREEN);
  v.inRange = 0;
  CHECK(ac_lost_reason(v) == AC_LOST_RANGE);
  v.doomed = 1;
  CHECK(ac_lost_reason(v) == AC_LOST_DOOMED);
  v.damageable = 0;
  CHECK(ac_lost_reason(v) == AC_LOST_UNDAMAGEABLE);
  v.seen = 0;
  CHECK_MSG(ac_lost_reason(v) == AC_LOST_GONE, "not hostile and live in this frame's scan");
  v.passMode = AC_PASS_NONE;
  CHECK_MSG(ac_lost_reason(v) == AC_LOST_NO_PASS, "a pawn picked, a pawn unknown or the gun: no ship pass");
  v.viewOk = 0;
  CHECK(ac_lost_reason(v) == AC_LOST_NO_VIEW);
  v = all;
  v.passMode = AC_PASS_CACHE;
  CHECK_MSG(ac_lost_reason(v) == AC_LOST_NONE, "the cache pass is a pass");
  for (int r = 0; r < AC_LOST_COUNT; r++) CHECK(ac_lost_name(r)[0] != 0);
  CHECK(strcmp(ac_lost_name(AC_LOST_LIMITS), "outside the turret's limits") == 0);
  CHECK(strcmp(ac_lost_name(-1), "?") == 0 && strcmp(ac_lost_name(AC_LOST_COUNT), "?") == 0);
}

// aim.h (2026-10-11, the flip line on the SuperTurret): the gun's own direction is its base Rotation plus
// RotatePitch/RotateYaw (dnControl_Turret.UpdateRotations), not the free-look camera. The logged ship was inside the
// limits; asked from the camera it was outside.
static void tb_0518_gun_view_limits() {
  const AimTurretLimits lim = { -128, 10240, -16384, 16384, 1 };
  const int base[3] = { 0, 16384, 0 };
  int gv[2] = { 0, 0 };
  CHECK(aim_gun_view(1, base, 5376, 4843, 7368, 12476, gv) == 1 && gv[0] == 5376 && gv[1] == 21227);
  CHECK_MSG(aim_turret_reachable(lim, 5376, 4843, gv[0], gv[1], 2267, 24149), "from the gun: inside");
  CHECK_MSG(!aim_turret_reachable(lim, 5376, 4843, 7368, 12476, 2267, 24149), "from the camera: outside");
  const int tilted[3] = { 600, 16384, 0 }, rolled[3] = { 0, 16384, 300 }, nearly[3] = { -16, 16384, 16 };
  CHECK_MSG(aim_gun_view(1, nearly, 5376, 4843, 7368, 12476, gv) == 1 && gv[1] == 21227, "16 units: still flat");
  CHECK_MSG(aim_gun_view(1, tilted, 5376, 4843, 7368, 12476, gv) == 0 && gv[0] == 7368 && gv[1] == 12476,
            "a tilted base: the camera, as before");
  CHECK(aim_gun_view(1, rolled, 1, 2, 3, 4, gv) == 0 && aim_gun_view(0, base, 1, 2, 3, 4, gv) == 0 && gv[1] == 4);
}

// aimbot.h mounted step. Replay: the camera took each add within one event, by 0.82 to 1.47 of it. Half the remaining
// error an event (k g < 1 for each) never changes sign and is within 64 units by event 16; the old rule (whole error,
// gun queue subtracted) swings. A camera that has not shown half the previous add holds the next, 8 events at most.
static int tb_mount_sim(double gP, double gY, int oldRule, int* flips) {
  const int want[2] = { 1225, 28656 };
  double cam[2] = { 6036, 17124 };
  int pend[2] = { 0, 0 }, lastSign[2] = { 0, 0 }, within = -1;
  AimbotMountState st;
  memset(&st, 0, sizeof st);
  *flips = 0;
  for (int e = 0; e < 24; e++) {
    const int c[2] = { (int)cam[0], (int)cam[1] };
    const AimbotView v = { c[0], c[0], c[1], 16384, 49152, 0, 0 };
    int dP = 0, dY = 0;
    aimbot_turn(v, 0, 0, want[0], want[1], 0, 1.0 / 175.0, &dP, &dY);
    if (oldRule) {   // the old rule: the whole error less the gun's pending queue (no pitch clamp is near here)
      dP -= pend[0];
      dY -= pend[1];
    } else {
      const int fP = dP, fY = dY;
      aimbot_mount_step(&st, 0, c, fP, fY, &dP, &dY);
    }
    const int add[2] = { dP, dY };
    const double g[2] = { gP, gY };
    for (int k = 0; k < 2; k++) {
      cam[k] += g[k] * add[k];
      pend[k] += add[k];
      pend[k] -= pend[k] > 46 ? 46 : pend[k] < -46 ? -46 : pend[k];
      const int err = want[k] - (int)cam[k];
      const int sg = err > 16 ? 1 : err < -16 ? -1 : 0;
      if (sg && lastSign[k] && sg != lastSign[k]) ++*flips;
      if (sg) lastSign[k] = sg;
    }
    if (within < 0 && abs(want[0] - (int)cam[0]) <= 64 && abs(want[1] - (int)cam[1]) <= 64) within = e;
  }
  return within;
}
static void tb_0519_mounted_turn_converges() {
  static const double G[6][2] = { { 1.468, 0.883 }, { 1.433, 0.821 }, { 1.110, 1.047 }, { 1.088, 1.045 },
                                  { 1.0, 1.0 }, { 1.168, 1.019 } };   // the last: the least-squares fit
  for (int i = 0; i < 6; i++) {
    int flips = 0;
    const int at = tb_mount_sim(G[i][0], G[i][1], 0, &flips);
    CHECK_MSG(at >= 0 && at <= 16 && flips == 0, "converged within 16 events, the error never changed sign");
  }
  int flips = 0;
  tb_mount_sim(1.468, 0.883, 1, &flips);
  CHECK_MSG(flips >= 4, "the old rule swings");
  // The hold: an add of 8000 the camera has not shown; then half shown.
  AimbotMountState st;
  memset(&st, 0, sizeof st);
  const int c0[2] = { 0, 0 };
  int dP = 0, dY = 16000;
  CHECK(aimbot_mount_step(&st, 0, c0, 0, 16000, &dP, &dY) == 1 && dY == 8000 && dP == 0);
  int held = 0;
  for (int e = 0; e < AIMBOT_MOUNT_HOLDS_MAX + 1; e++) {
    dP = 0;
    dY = 16000;
    held += aimbot_mount_step(&st, 0, c0, 0, 16000, &dP, &dY) == 0;
  }
  CHECK_MSG(held == AIMBOT_MOUNT_HOLDS_MAX, "a camera that never moves holds 8 events, then the step goes on");
  memset(&st, 0, sizeof st);
  dY = 16000;
  aimbot_mount_step(&st, 0, c0, 0, 16000, &dP, &dY);
  const int c1[2] = { 0, 4100 };
  dY = 11900;
  CHECK_MSG(aimbot_mount_step(&st, 0, c1, 0, 11900, &dP, &dY) == 1 && dY == 5950, "half shown: the next step");
  dY = 1;
  memset(&st, 0, sizeof st);
  CHECK(aimbot_mount_step(&st, 0, c0, 0, 1, &dP, &dY) == 1 && dY == 1);
}

// aircraft.h: the bar's cap and the render box's checks; the retired Aircraft key is ignored.
static void tb_0432_ac_cap_box_config() {
  CHECK(ac_health_cap(1200.f, 900.f) == 1200.f && ac_health_cap(0.f, 900.f) == 900.f);
  CHECK_MSG(ac_health_cap(1200.f, 8000.f) == 8000.f, "a live Health above the cap");
  const float mn[3] = { -330.f, -500.f, -160.f }, mx[3] = { 448.f, 491.f, 160.f }, z[3] = { 0.f, 0.f, 0.f };
  CHECK(ac_box_ok(mn, mx) && !ac_box_ok(mx, mn) && !ac_box_ok(z, z));
  const float wide[3] = { 9000.f, 1.f, 1.f };
  CHECK(!ac_box_ok(z, wide));
  config_defaults();
  CHECK_MSG(!config_key_known("esp", "aircraft"), "the old Aircraft row key is gone");
}

// handling.h: the Ripper's lockout is cleared on the frame it rises (handling.md section 8); every other gun keeps
// the timed clear.
static void tb_0434_handling_eager_lockout() {
  CHECK_MSG(hd_lock_eager(1, 0) && !hd_lock_eager(0, 0) && !hd_lock_eager(1, 1), "fire start without a looping fire");
  CHECK_MSG(!hd_lock_eager(-1, 0) && !hd_lock_eager(1, -1), "a flag not read: the timed clear");
  HdLock e = hd_lock_init();
  CHECK_MSG(hd_lock_step(&e, 1, 10.f, 0.06f, 1.5f, 1) == 1, "eager: due on the frame the lockout is seen");
  CHECK_MSG(hd_lock_step(&e, 1, 10.01f, 0.06f, 1.5f, 1) == 0, "once per lockout");
  hd_lock_after_clear(&e, 0, 10.01f);
  CHECK_MSG(hd_lock_step(&e, 1, 10.02f, 0.06f, 1.5f, 1) == 1, "the next shot's lockout");
  HdLock s = hd_lock_init();
  CHECK_MSG(hd_lock_step(&s, 1, 10.f, 0.06f, 1.f, 1) == 0 && hd_lock_step(&s, 1, 11.f, 0.06f, 1.f, 1) == 0,
            "100 %: the engine's own timer only");
  HdLock z = hd_lock_init();
  CHECK_MSG(hd_lock_step(&z, 1, 10.f, 0.f, 4.f, 1) == 0, "no lockout configured: nothing to clear");
  HdLock t = hd_lock_init();
  CHECK_MSG(hd_lock_step(&t, 1, 10.f, 0.06f, 2.f, 0) == 0 && hd_lock_step(&t, 1, 10.031f, 0.06f, 2.f, 0) == 1,
            "not eager: LockoutFireTime / f as before");
}
// handling.h: the fire trace logs a WeaponState change while fire is held (or on its release), 120 lines a level.
static void tb_0435_handling_fire_trace_rule() {
  CHECK(hd_trace_due(1, 1, 4, 6, 0) && hd_trace_due(0, 1, 6, 3, 0));
  CHECK_MSG(!hd_trace_due(1, 1, 4, 4, 0), "no change, no line");
  CHECK_MSG(!hd_trace_due(0, 0, 3, 4, 0), "not held: no line");
  CHECK_MSG(hd_trace_due(-1, -1, 3, 5, 0) && hd_trace_due(-1, -1, 6, 3, 0) && !hd_trace_due(-1, -1, 3, 7, 0),
            "held unknown: changes into or out of a fire state");
  CHECK_MSG(hd_trace_due(1, 1, 4, 6, HD_TRACE_MAX - 1) && !hd_trace_due(1, 1, 4, 6, HD_TRACE_MAX), "the cap");
  CHECK(HD_TRACE_MAX == 120);
}

// autowin.h: video poker and Alien Abortion. The switch class is generic, so only its Tag makes it the game; each
// game's path is named for the log.
static void tb_0438_aw_poker_abortion_kinds() {
  CHECK(aw_aa_kind(AW_KIND_AASWITCH, 77, 77) == AW_KIND_AASWITCH);
  CHECK_MSG(aw_aa_kind(AW_KIND_AASWITCH, 78, 77) == -1, "another push switch: no minigame");
  CHECK_MSG(aw_aa_kind(AW_KIND_AASWITCH, 0, 0) == -1, "Tag None or the Tag name not loaded: no minigame");
  CHECK_MSG(aw_aa_kind(AW_KIND_POKER, 0, 77) == AW_KIND_POKER && aw_aa_kind(-1, 77, 77) == -1, "other kinds pass");
  CHECK(aw_path_of(AW_KIND_POKER) == AW_PATH_GRANT && aw_path_of(AW_KIND_AASWITCH) == AW_PATH_PLAY);
  CHECK(aw_path_of(AW_KIND_PINBALL) == AW_PATH_WIN_FN && aw_path_of(AW_KIND_POOLBALL) == AW_PATH_WIN_FN);
  CHECK_MSG(aw_path_of(AW_KIND_TAP) == AW_PATH_NONE && aw_path_of(-1) == AW_PATH_NONE, "not a minigame");
  CHECK(aw_path_name(AW_PATH_GRANT)[0] && aw_path_name(AW_PATH_PLAY)[0] && aw_path_name(99)[0]);
}

// autowin.h: one hit per pop-up, when the pop-up dispatcher waits on its last event (the alien going down), so the
// earlier events (WackModifyRound counts the pop-up) have all fired.
static void tb_0439_aw_whack_rule() {
  int spent = 0;
  CHECK_MSG(!aw_whack_step(&spent, 0, 4, 5), "not dispatching: no hit");
  CHECK_MSG(!aw_whack_step(&spent, 1, 0, 5) && !aw_whack_step(&spent, 1, 3, 5), "still rising: no hit");
  CHECK_MSG(aw_whack_step(&spent, 1, 4, 5), "the last event pending: the hit");
  CHECK_MSG(!aw_whack_step(&spent, 1, 4, 5), "once per pop-up, while the hit's reset is pending");
  CHECK_MSG(!aw_whack_step(&spent, 1, 0, 5) && aw_whack_step(&spent, 1, 4, 5), "restarted unseen: a new pop-up");
  CHECK(!aw_whack_step(&spent, 0, 4, 5) && aw_whack_step(&spent, 1, 4, 5));
  int none = 0;
  CHECK_MSG(!aw_whack_step(&none, 1, 0, 0) && !aw_whack_step(&none, 1, -1, 0), "no events: never");
  int hits = 0, s2 = 0;
  for (int i = 0; i < 64; i++) hits += aw_whack_step(&s2, (i % 8) < 6, i % 8 < 4 ? 0 : 4, 5);
  CHECK_MSG(hits == 8, "eight pop-ups, eight hits");
}

// autowin.h: an Alien Abortion game is one use: begun by the Start press, ended by the game's own result (its win or
// loss dispatcher starting) or by AW_AA_IDLE_MS of in-level time with no alien up. A press while live is no restart.
static void tb_0440_aw_aa_game() {
  AwAa g = {};
  CHECK_MSG(!aw_aa_step(&g, 0, 0, 0, 0, 16) && !g.live, "no press: nothing");
  CHECK_MSG(aw_aa_step(&g, 1, 0, 0, 0, 16) == AW_AA_BEGIN && g.live, "the press begins");
  CHECK_MSG(!aw_aa_step(&g, 1, 1, 0, 0, 16) && g.live, "pressed again mid-game: the same game");
  unsigned gap = 0;
  for (int i = 0; i < 29; i++) gap |= aw_aa_step(&g, 0, 0, 0, 0, 1000);
  CHECK_MSG(!gap && g.live, "a 29 s gap between waves");
  CHECK_MSG(!aw_aa_step(&g, 0, 1, 0, 0, 2000) && g.live, "an alien up resets the idle time");
  CHECK_MSG(aw_aa_step(&g, 0, 0, 1, 0, 16) == AW_AA_WON && !g.live, "the expert-score dispatcher: won");
  CHECK_MSG(!aw_aa_step(&g, 0, 0, 1, 0, 16), "after the end: nothing");
  CHECK_MSG(aw_aa_step(&g, 1, 0, 1, 0, 16) == AW_AA_BEGIN && g.live, "a new game while the win still plays");
  CHECK_MSG(!aw_aa_step(&g, 0, 0, 1, 0, 16) && g.live, "the old win is no result for the new game");
  CHECK_MSG(aw_aa_step(&g, 0, 0, 0, 1, 16) == AW_AA_LOST && !g.live, "the end-game dispatcher: lost");
  aw_aa_step(&g, 1, 0, 0, 0, 16);
  unsigned ev = 0;
  for (int i = 0; i < 64 && !ev; i++) ev = aw_aa_step(&g, 0, 0, 0, 0, 1000);
  CHECK_MSG(ev == AW_AA_IDLE && !g.live, "idle: ended without a result");
  aw_aa_step(&g, 1, 0, 0, 0, 16);
  CHECK_MSG(!aw_aa_step(&g, 0, 0, 0, 0, 0xFFFFFFFFu) && g.live, "one long frame (a pause) counts as AW_AA_DT_CAP");
  CHECK(AW_AA_IDLE_MS == 30000 && AW_AA_DT_CAP == 1000);
}

// autowin.h: the Alien Abortion result names award 29 only where the map has its TriggerPlayer (Map_mydigs has the game
// and no award); a dispatcher's state is read from the offsets resolved once per level.
static void tb_0441_aw_aa_result_and_read() {
  CHECK(strstr(aw_aa_result(AW_AA_WON, 1), "award 29") != 0);
  CHECK_MSG(strstr(aw_aa_result(AW_AA_WON, 0), "award 29 ") == 0 && strstr(aw_aa_result(AW_AA_WON, 0), "no award"),
            "no TriggerPlayer: no award claimed");
  CHECK(strstr(aw_aa_result(AW_AA_LOST, 1), "lost") && strstr(aw_aa_result(AW_AA_IDLE, 1), "no result"));
  const AwDispOfs bad = { 0x300, 0, 0x304, 0x308 };
  CHECK_MSG(!aw_disp_ofs_ok(bad), "no bit mask: unresolved");
  const AwDispOfs ofs = { 0x300, 0x4, 0x304, 0x308 };
  CHECK(aw_disp_ofs_ok(ofs));
  const AwDispOfs neg = { -4, 0x4, 0x304, 0x308 };
  CHECK_MSG(!aw_disp_ofs_ok(neg), "a negative offset: unresolved");
  unsigned char o[0x320] = {};
  int w = 0x5, idx = 4, num = 5;
  memcpy(o + 0x300, &w, 4); memcpy(o + 0x304, &idx, 4); memcpy(o + 0x308 + 4, &num, 4);
  AwDisp d = aw_disp_read(o, ofs);
  CHECK_MSG(d.on && d.index == 4 && d.count == 5, "bit 0x4 set, Index 4, OutDelays.Num() 5");
  w = 0x3; memcpy(o + 0x300, &w, 4);
  CHECK_MSG(!aw_disp_read(o, ofs).on, "other bits only: not dispatching");
}

// pehook.h: off the game thread (the loading thread raises level-start events) only before-phase subscribers that
// asked for any thread run; on the game thread every enabled one does.
static void tb_0442_pe_any_thread() {
  PeSub s[3] = { { 77, 1, 0, 0 }, { 77, 1, 0, 1 }, { 77, 1, 1, 1 } };
  CHECK(pe_next_call(s, 3, 77, 0, 0, 1) == 0 && pe_next_call(s, 3, 77, 1, 0, 1) == 1);
  CHECK_MSG(pe_next_call(s, 3, 77, 0, 0, 0) == 1 && pe_next_call(s, 3, 77, 2, 0, 0) == -1,
            "off the game thread: the any-thread subscriber only");
  CHECK_MSG(pe_next_call(s, 3, 77, 0, 1, 0) == -1, "off the game thread: never the after phase");
  CHECK(pe_next_call(s, 3, 77, 0, 1, 1) == 2);
  PeSub off[1] = { { 77, 0, 0, 1 } };
  CHECK_MSG(pe_next_call(off, 1, 77, 0, 0, 0) == -1, "a disabled any-thread subscriber is skipped");
}

// bigpackage.h: the flags are re-applied before DukePlayer's own PreGameInit only once the menu call was made, while
// the switch is on, and only on the default object that call checked (RescanDLC's ResetDLC zeroes them each load).
static void tb_0443_bp_reapply_rule() {
  int cdo = 0, other = 0, duke = 0, actor = 0;
  CHECK_MSG(!bp_armed(true, false, &cdo), "no menu call yet: not armed");
  CHECK_MSG(!bp_armed(false, true, &cdo), "switch off: not armed");
  CHECK(bp_armed(true, true, &cdo) == &cdo);
  CHECK(bp_reapply_ok(&cdo, &cdo, &duke));
  CHECK_MSG(!bp_reapply_ok(0, &cdo, &duke), "unarmed");
  CHECK_MSG(!bp_reapply_ok(&cdo, &other, &duke), "the default object moved");
  CHECK_MSG(!bp_reapply_ok(&cdo, &cdo, 0), "no DukePlayer class in the Outer's chain");
  (void)actor;
}

// bigpackage.h: DukePlayerDLC03.PreGameInit's Outer derives from DukePlayer, so the walk up US_SUPER finds it;
// an unrelated class, a chain past the bound and a failed read do not.
struct BpFakeCls { const BpFakeCls* super; };
static bool bp_fake_read(const void* c, const void** super) {
  if (!c) return false;
  *super = ((const BpFakeCls*)c)->super;
  return true;
}
static bool bp_fake_read_fail(const void*, const void**) { return false; }
// Fullbright's probe (freecam.md 7f): invisible collision (NoRenderGFX) is passed, at most FB_PROBE_SKIPS times, from
// FB_PROBE_STEP past the hit.
static void tb_0555_fb_probe_skips_norender() {
  CHECK_MSG(fbp_skip(7, 7, 0), "NoRenderGFX: traced again");
  CHECK_MSG(fbp_skip(7, 7, FB_PROBE_SKIPS - 1) && !fbp_skip(7, 7, FB_PROBE_SKIPS), "at most FB_PROBE_SKIPS times");
  CHECK_MSG(!fbp_skip(8, 7, 0), "another material: kept");
  CHECK_MSG(!fbp_skip(0, 0, 0) && !fbp_skip(7, 0, 0), "NoRenderGFX unresolved, or no material: kept");
  const float at[3] = { 10.f, -20.f, 30.f }, dir[3] = { 0.f, 1.f, 0.f };
  float out[3];
  fbp_past(at, dir, out);
  CHECK_MSG(out[0] == 10.f && out[1] == -20.f + FB_PROBE_STEP && out[2] == 30.f, "the next start is past the hit");
}

// Fullbright's probe: "forward" only when both deferred classes resolved; an unresolved class makes it unknown.
static void tb_0556_fb_probe_path() {
  CHECK(fbp_path(0, 1, 0, 1, 0) == FBP_NO_MATERIAL);
  CHECK(fbp_path(1, 1, 1, 1, 0) == FBP_DEFERRED);
  CHECK(fbp_path(1, 1, 0, 1, 1) == FBP_DEFERRED);
  CHECK(fbp_path(1, 1, 1, 0, 0) == FBP_DEFERRED);
  CHECK(fbp_path(1, 1, 0, 1, 0) == FBP_FORWARD);
  CHECK_MSG(fbp_path(1, 0, 0, 1, 0) == FBP_UNKNOWN && fbp_path(1, 1, 0, 0, 0) == FBP_UNKNOWN &&
            fbp_path(1, 0, 0, 0, 0) == FBP_UNKNOWN, "a class unresolved: unknown, not forward");
}

// fblift.h (freecam.md 7g): the slider's 0..100 % is a gamma 1 / (1 + k). 0% is the identity and draws nothing; the
// curve rises, keeps black and white, lifts the darks and leaves the highlights near.
static void tb_0554_fblift_curve_and_slider() {
  CHECK_MSG(fblift_exponent(0) == 1.f, "0%: the identity");
  CHECK_MSG(fblift_exponent(-5) == 1.f && fblift_exponent(250) == fblift_exponent(100), "clamped to 0..100");
  CHECK_MSG(fabsf(fblift_exponent(100) - 1.f / (1.f + FBLIFT_K_MAX)) < 1e-6f, "100%: the full k");
  CHECK_MSG(fblift_exponent(60) < fblift_exponent(30) && fblift_exponent(30) < 1.f, "more lift, a smaller exponent");
  const float e = fblift_exponent(FBLIFT_DEF_PCT);
  float prev = -1.f;
  int rises = 1, bounded = 1;
  for (int i = 0; i < 64; ++i) {
    const float x = (float)i / 63.f, y = fblift_curve(x, e);
    rises &= y > prev;
    bounded &= y >= x && y <= 1.f;
    prev = y;
  }
  CHECK_MSG(rises, "strictly increasing");
  CHECK_MSG(bounded, "never darker, never past white");
  CHECK_MSG(fblift_curve(0.f, e) == 0.f && fblift_curve(1.f, e) == 1.f, "black stays black, white stays white");
  CHECK_MSG(fblift_curve(0.1f, e) >= 0.25f, "the darks lift strongly at the default");
  CHECK_MSG(fblift_curve(0.9f, e) - 0.9f <= 0.06f, "the highlights nearly unchanged at the default");
  CHECK_MSG(fblift_curve(-0.5f, e) == 0.f && fblift_curve(2.f, e) == 1.f, "the input is clamped");
  CHECK_MSG(fblift_wanted(1, 60, 0), "holding, above 0, nothing failed: drawn");
  CHECK_MSG(!fblift_wanted(0, 60, 0) && !fblift_wanted(1, 0, 0) && !fblift_wanted(1, 60, 1),
            "not holding, 0%, or failed: nothing drawn");
  config_defaults();
  CHECK_MSG(g_cfg.freecamLift == FBLIFT_DEF_PCT && config_key_known("freecam", "fblift"), "[freecam] fblift, 60%");
}

// Fullbright's probe (freecam.md 7f): a sample every FB_PROBE_MS while active, a line when the hit changes, 64 a flight.
static void tb_0553_fb_probe_rule() {
  FbProbe p;
  fbp_flight(&p);
  CHECK_MSG(!fbp_due(&p, 0, 1000), "inactive: no sample");
  CHECK_MSG(fbp_due(&p, 1, 1000), "the first active frame samples");
  CHECK_MSG(!fbp_due(&p, 1, 1000 + FB_PROBE_MS - 1), "not before the period");
  CHECK_MSG(fbp_due(&p, 1, 1000 + FB_PROBE_MS), "at the period");
  fbp_flight(&p);
  CHECK_MSG(fbp_due(&p, 1, 0xFFFFFF00u) && fbp_due(&p, 1, 0xFFFFFF00u + FB_PROBE_MS), "the tick count wraps");
  int a = 0, b = 0, m1 = 0, m2 = 0;
  fbp_flight(&p);
  CHECK_MSG(fbp_take(&p, &a, -1, &m1), "the first hit is logged");
  CHECK_MSG(!fbp_take(&p, &a, -1, &m1), "the same hit: no line");
  CHECK_MSG(fbp_take(&p, &a, 7, &m2), "the level's BSP: another surface is another line");
  CHECK_MSG(fbp_take(&p, &b, -1, &m2), "another actor");
  CHECK_MSG(fbp_take(&p, 0, -1, 0), "the sky (no hit) is a line too");
  int lines = 4;
  for (int i = 0; i < 64; ++i) lines += fbp_take(&p, (i & 1) ? (void*)&a : (void*)&b, i, 0);
  CHECK_MSG(lines == FB_PROBE_CAP, "capped at FB_PROBE_CAP lines a flight");
  fbp_flight(&p);
  CHECK_MSG(fbp_take(&p, &a, -1, &m1), "a new flight logs again");
}

static void tb_0552_bp_class_chain() {
  BpFakeCls object = { 0 }, duke = { &object }, dlc03 = { &duke }, actor = { &object };
  CHECK_MSG(bp_match_class(&duke, &duke, bp_fake_read) == &duke, "the class itself");
  CHECK_MSG(bp_match_class(&dlc03, &duke, bp_fake_read) == &duke, "DukePlayerDLC03 -> DukePlayer");
  CHECK_MSG(!bp_match_class(&actor, &duke, bp_fake_read), "an unrelated class");
  CHECK_MSG(!bp_match_class(&dlc03, &duke, bp_fake_read_fail), "a failed read");
  CHECK_MSG(!bp_match_class(&dlc03, 0, bp_fake_read), "DukePlayer unresolved");
  BpFakeCls chain[10];
  for (int i = 0; i < 10; i++) chain[i].super = i < 9 ? &chain[i + 1] : 0;
  CHECK_MSG(bp_match_class(&chain[0], &chain[8], bp_fake_read) == &chain[8], "8 steps up still matches");
  CHECK_MSG(!bp_match_class(&chain[0], &chain[9], bp_fake_read), "past the bound");
  BpFakeCls loop = { 0 };
  loop.super = &loop;
  CHECK_MSG(!bp_match_class(&loop, &duke, bp_fake_read), "a cycle ends at the bound");
}

// progress.h: every phone id sits in exactly one chapter row and Map09 holds three (its fourth desk phone, tag
// ChampagnePhone, has no message and no event: progress.md section 6, "Phone audit").
static void tb_0444_pg_phone_ids_partition() {
  unsigned seen = 0;
  int dup = 0, map09 = 0;
  for (int k = 0; k < PG_COLLECT_COUNT; ++k) {
    dup += (seen & PG_COLLECT[k].phones) != 0;
    seen |= PG_COLLECT[k].phones;
    if (strcmp(PG_COLLECT[k].url, "map09") == 0)
      for (int i = 0; i < PG_PHONES; ++i) map09 += (PG_COLLECT[k].phones >> i) & 1;
  }
  CHECK_MSG(dup == 0 && seen == (1u << PG_PHONES) - 1, "ids 1..12, each in one chapter");
  CHECK(map09 == 3);
}

// progress.h / bigpackage.h: no game-thread config read while a level loads (g_LoadingOnThread nonzero: the loading
// thread writes the unlocked cache then); the flags line waits for the first frame after the flag clears.
static void tb_0445_cfg_reads_pause_while_loading() {
  CHECK(pg_cfg_read_allowed(0) && !pg_cfg_read_allowed(1) && !pg_cfg_read_allowed(-1));
  CHECK_MSG(!bp_flags_log_due(true, 1, 2, 1, 0, 0), "a re-apply mid-load: the line waits");
  CHECK_MSG(bp_flags_log_due(true, 0, 2, 1, 0, 0), "the first frame after the load: logged");
  CHECK_MSG(!bp_flags_log_due(true, 0, 2, 2, 0, 0), "nothing new: not logged again");
  CHECK_MSG(bp_flags_log_due(true, 0, 2, 2, 1, 0), "a refusal alone is logged");
  CHECK_MSG(!bp_flags_log_due(false, 0, 2, 1, 0, 0), "not scriptable: waits");
}

// turretwep.h: which mounted gun gets which lever (turrets.md section 7).
static void tb_0447_tw_levers() {
  CHECK_MSG(tw_levers(AIM_TUR_MG42) == (TW_L_SPREAD | TW_L_GUNANIM), "MG42: spread and its own fire anim");
  CHECK_MSG(tw_levers(AIM_TUR_LAUNCHER) == (TW_L_GUNANIM | TW_L_USERANIM), "launcher: both anims, no spread");
  CHECK_MSG(tw_levers(AIM_TUR_SUPER) == TW_L_USERANIM, "SuperTurret: the player's anim end refires it");
  CHECK_MSG(tw_levers(AIM_TUR_NONE) == 0, "not a turret: nothing");
}
// turretwep.h: a rate lever raised in FireDown, put back when firing stops, on dismount and with the row at 100 %.
static void tb_0448_tw_anim_step() {
  TwAnim a = tw_anim_init();
  float out = 0.f;
  CHECK_MSG(tw_anim_step(&a, 1, 4.f, 1, 1.f, &out) == TW_SET && out == 4.f, "FireDown at 400 %: stock x 4");
  CHECK_MSG(tw_anim_step(&a, 1, 4.f, 1, 4.f, &out) == TW_NONE, "ours in place: no call");
  CHECK_MSG(tw_anim_step(&a, 1, 2.f, 1, 4.f, &out) == TW_SET && out == 2.f, "the row moved while firing: stock x 2");
  CHECK_MSG(tw_anim_step(&a, 1, 2.f, 1, 1.2f, &out) == TW_SET && fabsf(out - 2.4f) < 1e-5f,
            "an anim restart (launcher refire): its own stock x f");
  CHECK_MSG(tw_anim_step(&a, 0, 2.f, 1, 2.4f, &out) == TW_SET && fabsf(out - 1.2f) < 1e-5f,
            "FireDown left with ours in place: the stock back");
  CHECK_MSG(tw_anim_step(&a, 0, 2.f, 1, 1.f, &out) == TW_NONE, "released once");
  TwAnim b = tw_anim_init();
  tw_anim_step(&b, 1, 3.f, 1, 1.f, &out);
  CHECK_MSG(tw_anim_step(&b, 0, 3.f, 1, 1.f, &out) == TW_NONE && !b.applied,
            "the next state's anim replaced ours: nothing written, record gone");
  TwAnim c = tw_anim_init();
  tw_anim_step(&c, 1, 3.f, 1, 1.f, &out);
  CHECK_MSG(tw_anim_step(&c, 1, 1.f, 1, 3.f, &out) == TW_SET && out == 1.f, "row to 100 % while firing: stock back");
  CHECK_MSG(tw_anim_step(&c, 1, 1.f, 1, 1.f, &out) == TW_NONE, "100 %: never a call");
  TwAnim d = tw_anim_init();
  tw_anim_step(&d, 1, 3.f, 1, 1.f, &out);
  CHECK_MSG(tw_anim_step(&d, 1, 3.f, 0, 0.f, &out) == TW_NONE && d.applied, "an unread frame keeps the record");
  CHECK_MSG(tw_anim_step(&d, 0, 3.f, 1, 3.f, &out) == TW_SET && out == 1.f, "dismount (want 0): stock back");
  TwAnim e = tw_anim_init();
  CHECK_MSG(tw_anim_step(&e, 1, 3.f, 1, 0.f, &out) == TW_NONE && !e.applied, "a stopped channel is not raised");
  TwAnim l = tw_anim_init();   // a level change while mounted: the caller starts over, writing nothing
  tw_anim_step(&l, 1, 3.f, 1, 1.f, &out);
  l = tw_anim_init();
  CHECK_MSG(tw_anim_step(&l, 0, 3.f, 1, 1.f, &out) == TW_NONE, "after a level change nothing is put back");
}
// turretwep.h: the MaxSpread record, through mount, dismount, the row and a level change while mounted.
static void tb_0449_tw_spread_step() {
  int k1 = 1, k2 = 2;
  const void* mg = &k1;
  const void* other = &k2;
  TwSpread s = tw_spread_init();
  float back = -1.f;
  CHECK_MSG(tw_spread_step(&s, 0, mg, 7, 1, 256.f, &back) == TW_SP_NONE, "off, no record: nothing");
  CHECK_MSG(tw_spread_step(&s, 1, mg, 7, 1, 256.f, &back) == TW_SP_APPLY && s.r.saved == 256.f, "mount: 256 -> 0");
  CHECK_MSG(tw_spread_step(&s, 1, mg, 7, 1, 0.f, &back) == TW_SP_NONE, "ours in place");
  CHECK_MSG(tw_spread_step(&s, 1, mg, 7, 0, 0.f, &back) == TW_SP_NONE && s.cls == mg, "an unread frame changes nothing");
  CHECK_MSG(tw_spread_step(&s, 0, 0, 0, 1, 0.f, &back) == TW_SP_RESTORE && back == 256.f && !s.cls,
            "dismount (or the row off): 256 back, record gone");
  CHECK_MSG(tw_spread_step(&s, 0, 0, 0, 1, 256.f, &back) == TW_SP_NONE, "released once");
  tw_spread_step(&s, 1, mg, 7, 1, 256.f, &back);
  CHECK_MSG(tw_spread_step(&s, 0, mg, 7, 1, 100.f, &back) == TW_SP_DROP && !s.cls, "changed since: left, record gone");
  tw_spread_step(&s, 1, mg, 7, 1, 256.f, &back);
  // A level change while mounted keeps the record (the class default outlives the level); the next frame off restores.
  CHECK_MSG(acc_keep_record(1, 7, s.clsName) && s.cls == mg, "level change: the class still loaded keeps the record");
  CHECK_MSG(tw_spread_step(&s, 0, 0, 0, 1, 0.f, &back) == TW_SP_RESTORE && back == 256.f, "then restored");
  tw_spread_step(&s, 1, mg, 7, 1, 256.f, &back);
  CHECK_MSG(tw_spread_step(&s, 1, other, 9, 1, 0.f, &back) == TW_SP_RESTORE && back == 256.f && !s.cls,
            "another gun class: the held record is released first");
  CHECK_MSG(tw_spread_step(&s, 1, other, 9, 1, 256.f, &back) == TW_SP_APPLY && s.cls == other, "then applied to it");
  TwSpread z = tw_spread_init();
  tw_spread_step(&z, 1, mg, 7, 1, 0.f, &back);
  CHECK_MSG(tw_spread_step(&z, 0, 0, 0, 1, 0.f, &back) == TW_SP_DROP, "0 before ours: nothing to write back");
}
// turretwep.h: the per-mount cap on MaxSpread applies (each one walks the object table).
static void tb_0451_tw_apply_cap() {
  CHECK_MSG(tw_apply_allowed(0) && tw_apply_allowed(TW_SPREAD_APPLY_MAX - 1), "under the cap: written");
  CHECK_MSG(!tw_apply_allowed(TW_SPREAD_APPLY_MAX) && !tw_apply_allowed(TW_SPREAD_APPLY_MAX + 5), "at the cap: stop");
  CHECK(TW_SPREAD_APPLY_MAX == 3);
  int k = 1;
  TwSpread s = tw_spread_init();
  float back = -1.f;
  tw_spread_step(&s, 1, &k, 7, 1, 256.f, &back);   // capped: the caller wrote nothing, the default keeps 256
  CHECK_MSG(tw_spread_step(&s, 0, 0, 0, 1, 256.f, &back) == TW_SP_DROP, "a capped apply's release writes nothing");
}
// turretwep.h: shots from MuzzleIndex, and the held-fire rate window.
static void tb_0450_tw_shot_rate() {
  CHECK(tw_muzzle_shots(0, 1) == 1 && tw_muzzle_shots(2, 0) == 1 && tw_muzzle_shots(1, 0) == 2);
  CHECK_MSG(tw_muzzle_shots(1, 1) == 0, "no change, no shot");
  CHECK_MSG(tw_muzzle_shots(-1, 0) == 1 && tw_muzzle_shots(-1, 1) == 2, "the first shots after -1");
  CHECK_MSG(tw_muzzle_shots(5, 1) == 0 && tw_muzzle_shots(0, -3) == 0, "an unread or odd index counts nothing");
  TwRate r = tw_rate_init();
  int n = 0;
  float secs = 0.f;
  CHECK_MSG(tw_rate_step(&r, 1, 10.f, 0, &n, &secs) == 0 && !r.open, "held, no shot yet: not open");
  CHECK(tw_rate_step(&r, 1, 10.1f, 1, &n, &secs) == 0 && r.open);
  CHECK(tw_rate_step(&r, 1, 10.2f, 1, &n, &secs) == 0);
  CHECK(tw_rate_step(&r, 1, 11.1f, 1, &n, &secs) == 0);
  CHECK_MSG(tw_rate_step(&r, 0, 11.2f, 0, &n, &secs) == 1 && n == 3 && fabsf(secs - 1.f) < 1e-4f,
            "released: 3 shots, first to last 1 s");
  CHECK_MSG(fabsf(tw_rate_per_s(n, secs) - 2.f) < 1e-4f, "2 intervals a second");
  CHECK_MSG(tw_rate_step(&r, 0, 11.3f, 0, &n, &secs) == 0, "closed once");
  CHECK(tw_rate_per_s(1, 0.f) == 0.f);
}

// overkill.h: expected damage against Health. The live Projectile.Damage wins (the launcher writes its GrenadeDamage
// there); the scale is ModifyDamage's DamageScaling and dnSinglePlayer.IssuedDamageScaler, for the player only.
static void tb_0452_ok_expected_damage() {
  CHECK_MSG(ok_doomed(80.f, 80.f) && !ok_doomed(79.9f, 80.f) && ok_doomed(160.f, 150.f), "enough damage dooms");
  CHECK_MSG(!ok_doomed(80.f, 0.f) && !ok_doomed(0.f, 0.f), "a dead target is never doomed");
  const float nan = sqrtf(-1.f);
  CHECK_MSG(!ok_doomed(nan, 80.f) && !ok_doomed(80.f, nan), "NaN: not doomed");
  CHECK_MSG(ok_proj_damage(80.f, 50.f) == 80.f && ok_proj_damage(0.f, 50.f) == 50.f && ok_proj_damage(-1.f, 0.f) == 0.f,
            "the live Damage, else the class default, else none");
  CHECK(ok_issued_scale(0) == 1.25f && ok_issued_scale(1) == 1.f && ok_issued_scale(2) == 0.75f &&
        ok_issued_scale(3) == 0.5f && ok_issued_scale(-1) == 1.f);
  CHECK_MSG(ok_damage_scale(1, 1.f, 1, 0, 3) == 0.5f && ok_damage_scale(1, 2.f, 1, 0, 1) == 2.f,
            "the player's DamageScaling times the difficulty");
  CHECK_MSG(ok_damage_scale(0, 2.f, 1, 0, 3) == 1.f, "Instigator not the player (the turret's grenade): unscaled");
  CHECK_MSG(ok_damage_scale(1, 1.f, 1, 1, 3) == 1.f && ok_damage_scale(1, 1.f, 0, 0, 3) == 1.f,
            "bIgnoreDifficultyDamageScaling, or not dnSinglePlayer: no difficulty");
  CHECK_MSG(ok_damage_scale(1, 0.f, 1, -1, 1) == 1.f && ok_damage_scale(1, nan, 1, 0, -1) == 1.f, "unread: 1");
  CHECK_MSG(ok_doomed(80.f * ok_damage_scale(1, 1.f, 1, 0, 3) * 2.f, 100.f) == 0, "two 80s on the hardest: 80 < 100");
  int a, b, c, d;
  const void* pl = &a; const void* wp = &b; const void* gun = &c; const void* other = &d;
  CHECK_MSG(ok_is_mine(pl, 0, pl, wp, gun) && ok_is_mine(0, gun, pl, wp, gun) && ok_is_mine(other, wp, pl, wp, gun),
            "Instigator the player, or Owner the player, its weapon or the mounted gun");
  CHECK_MSG(!ok_is_mine(other, other, pl, wp, gun) && !ok_is_mine(0, 0, pl, wp, gun) && !ok_is_mine(0, 0, pl, 0, 0),
            "anyone else's, or nobody's, is not");
}
// overkill.h: the mark's clear rules: the target dead, its projectiles gone, the cap, or too few left.
static void tb_0453_ok_clear_rules() {
  OkMark m = { 0, 0, 0 };
  CHECK(ok_mark_step(&m, 1, 100.f, 160.f, 2, 0, 1000) == OK_EV_MARK && m.doomed && m.since == 1000);
  CHECK_MSG(ok_mark_step(&m, 1, 100.f, 160.f, 2, 0, 1100) == OK_EV_NONE && m.doomed, "kept while enough flies");
  CHECK_MSG(ok_mark_step(&m, 1, 20.f, 80.f, 1, 0, 1200) == OK_EV_NONE && m.doomed, "a hit lowered Health: still doomed");
  CHECK_MSG(ok_mark_step(&m, 0, 0.f, 80.f, 1, 0, 1300) == OK_EV_DEAD && !m.doomed, "dead: cleared");
  CHECK_MSG(ok_mark_step(&m, 0, 0.f, 0.f, 0, 0, 1400) == OK_EV_NONE, "dead and unmarked: nothing");
  m.doomed = 0;
  ok_mark_step(&m, 1, 100.f, 160.f, 2, 0, 0);
  CHECK_MSG(ok_mark_step(&m, 1, 100.f, 0.f, 0, 0, 500) == OK_EV_GONE && !m.doomed, "all gone, alive: a miss");
  ok_mark_step(&m, 1, 100.f, 160.f, 2, 0, 0);
  CHECK_MSG(ok_mark_step(&m, 1, 100.f, 0.f, 0, 2, 2100) == OK_EV_CAP && !m.doomed, "still flying past the cap");
  ok_mark_step(&m, 1, 100.f, 160.f, 2, 0, 0);
  CHECK_MSG(ok_mark_step(&m, 1, 100.f, 80.f, 1, 0, 300) == OK_EV_SHORT && !m.doomed, "one gone, the rest too few");
  CHECK_MSG(ok_mark_step(&m, 1, 100.f, 160.f, 2, 0, 400) == OK_EV_MARK, "marked again by new projectiles");
  CHECK_MSG(ok_young(1000, 2999) && !ok_young(1000, 3000) && ok_young(0xFFFFFF00u, 0x100u), "the cap, wrap-safe");
}
// overkill.h: hitscan, beam and melee are never doomed; only a projectile gun with a mark.
static void tb_0454_ok_scope() {
  CHECK(ok_in_scope(AIMW_PROJECTILE) && !ok_in_scope(AIMW_HITSCAN) && !ok_in_scope(AIMW_BEAM) &&
        !ok_in_scope(AIMW_MELEE) && !ok_in_scope(AIMW_NONE));
  CHECK_MSG(ok_applies(AIMW_PROJECTILE, 1) && !ok_applies(AIMW_PROJECTILE, 0), "the mark decides for a projectile gun");
  CHECK_MSG(!ok_applies(AIMW_HITSCAN, 1) && !ok_applies(AIMW_MELEE, 1) && !ok_applies(AIMW_BEAM, 1),
            "switched to a hitscan gun, the rockets' target is shot as before");
}
// spawnbox.h: doomed is the last exclusion; every other reason is named first.
static void tb_0455_ok_exclusion_precedence() {
  CHECK(spawnbox_exclusion(0, 0, 0, 0, 0, 0, 1) == SB_EXCL_DOOMED);
  CHECK(spawnbox_exclusion(0, 0, 0, 0, 0, 0, 0) == SB_EXCL_NONE);
  CHECK_MSG(spawnbox_exclusion(0, 0, 0, 0, 0, 1, 1) == SB_EXCL_NEUTRAL &&
            spawnbox_exclusion(0, 0, 0, 0, 1, 0, 1) == SB_EXCL_PLAYERDMG0 &&
            spawnbox_exclusion(0, 0, 0, 1, 0, 0, 1) == SB_EXCL_NODAMAGE &&
            spawnbox_exclusion(0, 0, 1, 0, 0, 0, 1) == SB_EXCL_TELEPORT &&
            spawnbox_exclusion(0, 1, 0, 0, 0, 0, 1) == SB_EXCL_DOOMED &&
            spawnbox_exclusion(1, 0, 0, 0, 0, 0, 1) == SB_EXCL_FAR, "doomed comes after every other reason");
}
// overkill.h: the lock a shot records, and the queue its projectile takes it from.
static void tb_0456_ok_shot_lock() {
  int a, b, c;
  const void* t = &a; const void* u = &b; const void* w = &c;
  CHECK_MSG(ok_lock(t, u, w) == t && ok_lock(0, u, w) == u && ok_lock(0, 0, w) == w && ok_lock(0, 0, 0) == 0,
            "the triggerbot's target, else the aimbot's lock, else the aim's pick");
  OkShots q = {};
  const void* got = &got;
  CHECK_MSG(!ok_shot_take(&q, 100, &got), "empty");
  ok_shot_push(&q, t, 100);
  ok_shot_push(&q, u, 150);
  CHECK_MSG(ok_shot_take(&q, 200, &got) && got == t && q.n == 1, "oldest first");
  CHECK(ok_shot_take(&q, 200, &got) && got == u && q.n == 0);
  ok_shot_push(&q, 0, 100);
  CHECK_MSG(ok_shot_take(&q, 150, &got) && got == 0, "a shot with no lock is taken with none");
  ok_shot_push(&q, t, 100);
  ok_shot_push(&q, u, 1500);
  CHECK_MSG(ok_shot_take(&q, 1500, &got) && got == u && q.n == 0, "a stale shot is dropped, never matched");
  for (int i = 0; i < OK_SHOTS + 3; i++) ok_shot_push(&q, i == OK_SHOTS + 2 ? w : t, 1000 + i);
  CHECK_MSG(q.n == OK_SHOTS && q.s[OK_SHOTS - 1].target == w && q.s[0].ms == 1003, "full: the oldest goes");
}
// overkill.h: the object-table pass while armed: at once after a shot, else every OK_SCAN_MS while active.
static void tb_0457_ok_scan_due() {
  CHECK_MSG(!ok_scan_due(0, 1, 0, 0, 500, 1), "not armed: never");
  CHECK_MSG(ok_scan_due(1, 0, 0, 0, 5, 1) && ok_scan_due(1, 1, 1, 100, 101, 1), "the first, and a shot");
  CHECK(!ok_scan_due(1, 0, 1, 100, 199, 1) && ok_scan_due(1, 0, 1, 100, 200, 1) &&
        ok_scan_due(1, 0, 1, 0xFFFFFFF0u, 0x60u, 1));
}
// overkill.h: idle, a projectile gun up finds nothing new, so the periodic pass waits for activity; a
// shot still passes at once.
static void tb_0458_ok_scan_activity() {
  CHECK_MSG(!ok_scan_due(1, 0, 1, 100, 5000, 0) && !ok_scan_due(1, 0, 0, 0, 5000, 0), "idle: no periodic pass");
  CHECK_MSG(ok_scan_due(1, 1, 1, 100, 101, 0) && ok_scan_due(1, 1, 0, 0, 5, 0), "a shot passes at once, idle or not");
  CHECK_MSG(ok_active(1, 1000, 2999) && !ok_active(1, 1000, 3000), "active for OK_CAP_MS after the last activity");
  CHECK_MSG(!ok_active(0, 0, 10) && ok_active(1, 0xFFFFFF00u, 0x100u), "none yet: idle; wrap-safe");
}

// aim.h: the straight intercept (physProjectile: speed min(Speed + accel t, MaxSpeed), aim.md section 8).
static void tb_0459_ic_straight() {
  const float o[3] = { 0.f, 0.f, 0.f }, still[3] = { 0.f, 0.f, 0.f }, p[3] = { 1000.f, 0.f, 0.f };
  const AimProjModel c = { AIM_PM_STRAIGHT, 1000.f, 1000.f, 0.f, 0.f, 0.f, 6.f, 0 };
  AimIntercept r = aim_intercept(c, o, p, still);
  CHECK_MSG(r.ok && fabsf(r.t - 1.f) < 1e-3f && fabsf(r.dir[0] - 1.f) < 1e-5f, "a still target: one second");
  const float side[3] = { 0.f, 300.f, 0.f };
  r = aim_intercept(c, o, p, side);
  const float tc = sqrtf(1.f / 0.91f);   // 1000^2 + (300 t)^2 = (1000 t)^2
  CHECK_MSG(r.ok && fabsf(r.t - tc) < 1e-3f && fabsf(r.hit[1] - 300.f * tc) < 0.5f, "constant speed, moving target");
  CHECK_MSG(fabsf(r.dir[1] / r.dir[0] - 300.f * tc / 1000.f) < 1e-3f, "aimed at where it will be");
  const AimProjModel a = { AIM_PM_STRAIGHT, 900.f, 1600.f, 50.f, 0.f, 0.f, 6.f, 0 };
  const float far2[3] = { 2000.f, 0.f, 0.f };
  r = aim_intercept(a, o, far2, still);
  CHECK_MSG(r.ok && fabsf(r.t - (-900.f + sqrtf(1010000.f)) / 50.f) < 1e-3f, "900 t + 25 t^2 = 2000");
  const float walk[3] = { 0.f, 200.f, 50.f };
  r = aim_intercept(a, o, far2, walk);
  const float hd = sqrtf(r.hit[0] * r.hit[0] + r.hit[1] * r.hit[1] + r.hit[2] * r.hit[2]);
  CHECK_MSG(r.ok && fabsf(hd - aim_straight_dist(a, r.t)) < 0.5f && fabsf(r.hit[1] - 200.f * r.t) < 0.1f,
            "an accelerating rocket meets the moving target");
  const AimProjModel capped = { AIM_PM_STRAIGHT, 900.f, 1000.f, 50.f, 0.f, 0.f, 6.f, 0 };
  CHECK_MSG(fabsf(aim_straight_dist(capped, 3.f) - 2900.f) < 1e-2f, "MaxSpeed after 2 s");
  const float flee[3] = { 2000.f, 0.f, 0.f }, beyond[3] = { 7000.f, 0.f, 0.f };
  CHECK_MSG(!aim_intercept(c, o, p, flee).ok, "faster than the rocket, moving away: no solution");
  CHECK_MSG(!aim_intercept(c, o, beyond, still).ok, "past the LifeSpan's reach: no solution");
  int sp = 1, sy = 1;
  aim_spawn_offset(L"EnforcerGunWeaponConfig", &sp, &sy);
  CHECK(sp == 200 && sy == -200);
  aim_spawn_offset(L"PistolWeaponConfig", &sp, &sy);
  CHECK(sp == 0 && sy == 0);
}

// aim.h: the ballistic intercept (physFalling), the low arc; checked against the closed form and its own flight.
static void tb_0460_ic_ballistic() {
  const float o[3] = { 0.f, 0.f, 0.f }, still[3] = { 0.f, 0.f, 0.f };
  const double kDeg = 57.29577951308232;
  const AimProjModel m = { AIM_PM_BALLISTIC, 2500.f, 0.f, 0.f, 950.f, 0.f, 10.f, 0 };
  const float level[3] = { 4000.f, 0.f, 0.f };
  AimIntercept r = aim_intercept(m, o, level, still);
  const double cf = 0.5 * asin(950.0 * 4000.0 / (2500.0 * 2500.0));
  CHECK_MSG(r.ok && fabs(asin(r.dir[2]) - cf) * kDeg < 0.01, "level: 0.5 asin(g D / s^2)");
  CHECK_MSG(fabs(r.t - 4000.0 / (2500.0 * cos(cf))) < 1e-3, "the time of flight");
  CHECK_MSG(cf * kDeg < 45.0 && asin(r.dir[2]) * kDeg < 45.0, "the low arc");
  const AimProjModel half = { AIM_PM_BALLISTIC, 2500.f, 0.f, 0.f, 950.f, 0.f, 10.f, 1 };
  r = aim_intercept(half, o, level, still);
  CHECK_MSG(r.ok && fabs(asin(r.dir[2]) - 0.5 * asin(475.0 * 4000.0 / (2500.0 * 2500.0))) * kDeg < 0.25,
            "bBounce: half gravity (UE1's physFalling)");
  const float up[3] = { 3000.f, 0.f, 500.f }, down[3] = { 3000.f, 0.f, -800.f };
  for (int k = 0; k < 2; k++) {
    const float* q = k ? down : up;
    r = aim_intercept(m, o, q, still);
    const double s2 = 2500.0 * 2500.0, g = 950.0, D = 3000.0, H = q[2];
    const double low = atan((s2 - sqrt(s2 * s2 - g * (g * D * D + 2.0 * H * s2))) / (g * D));
    CHECK_MSG(r.ok && fabs(asin(r.dir[2]) - low) * kDeg < 0.01, k ? "a target below" : "a target above");
  }
  const AimProjModel clamp = { AIM_PM_BALLISTIC, 2500.f, 0.f, 0.f, 950.f, 2500.f, 10.f, 0 };
  const AimProjModel models[3] = { m, half, clamp };
  const float run[3] = { 0.f, 400.f, 0.f }, start[3] = { 3000.f, 0.f, 0.f };
  for (int k = 0; k < 3; k++) {
    r = aim_intercept(models[k], o, start, run);
    const float v0[3] = { 2500.f * r.dir[0], 2500.f * r.dir[1], 2500.f * r.dir[2] };
    float at[3];
    aim_ballistic_pos(models[k], o, v0, r.t, at);
    const float dx = at[0] - r.hit[0], dy = at[1] - r.hit[1], dz = at[2] - r.hit[2];
    CHECK_MSG(r.ok && fabsf(r.hit[1] - 400.f * r.t) < 0.1f && sqrtf(dx * dx + dy * dy + dz * dz) < 3.f,
              "a moving target: the grenade's own flight meets it");
  }
  const float tooFar[3] = { 20000.f, 0.f, 0.f }, tooHigh[3] = { 100.f, 0.f, 5000.f };
  CHECK_MSG(!aim_intercept(m, o, tooFar, still).ok, "beyond s^2 / g: no solution");
  CHECK_MSG(!aim_intercept(m, o, tooHigh, still).ok, "above the apex: no solution");
}

// aim.h: the mounted guns' models; the retired Lead amount key is ignored.
static void tb_0461_ic_turret_model_config() {
  AimProjModel g = aim_turret_model(AIM_TUR_LAUNCHER, 2600.f, -950.f, 2500.f, 1);
  CHECK(g.kind == AIM_PM_BALLISTIC && g.speed == 2600.f && g.gravity == 950.f && g.terminal == 2500.f && g.bounce == 1);
  g = aim_turret_model(AIM_TUR_LAUNCHER, 0.f, 0.f, 0.f, -1);
  CHECK_MSG(g.speed == 2500.f && g.gravity == 950.f && g.terminal == 2500.f && g.bounce == 1, "unread: the defaults");
  const AimProjModel s = aim_turret_model(AIM_TUR_SUPER, 0.f, 0.f, 0.f, -1);
  CHECK_MSG(s.kind == AIM_PM_STRAIGHT && s.speed == 16000.f && s.accel == 100000.f, "dnRocket_SuperTurret");
  CHECK(aim_turret_model(AIM_TUR_MG42, 0.f, 0.f, 0.f, -1).kind == AIM_PM_NONE);
  config_defaults();
  CHECK_MSG(!config_key_known("aim", "leadamount"), "the Lead amount key is gone");
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_leadamount");
  write_text(path, "[aim]\r\nleadamount=30\r\nlead=0\r\nrange=40\r\n");
  CHECK(config_load_from(path) == true);
  CHECK_MSG(g_cfg.aimLead == 0 && g_cfg.aimRangeM == 40, "the old key skipped, the rest read");
  DeleteFileA(path);
}

// aim.h: a mounted gun's limits are asked of the solved launch direction (the lob), not the direct line; the
// SuperTurret model takes class defaults where read. The held weapon's damage type against DamageScaleInfo's lists
// (Ripper vs Battlelord boss: its only entry zeroes all types but two NOT types). PassDamageType: child-class matching.
static void tb_0463_spawnbox_dmgtype() {
  int cls[6];   // stand-ins: Rocket < Explosion < DamageType; Bullet < DamageType; Fire
  const void* rocket[3] = { &cls[0], &cls[1], &cls[2] };
  const void* bullet[2] = { &cls[3], &cls[2] };
  const void* expl[1] = { &cls[1] };
  const void* nots[3] = { 0, &cls[1], &cls[4] };
  CHECK_MSG(spawnbox_pass_type(rocket, 3, nots, 3) && !spawnbox_pass_type(bullet, 2, nots, 3),
            "a child of a listed type passes; an unrelated type does not; a null element is skipped");
  const void* childList[1] = { &cls[0] };
  CHECK_MSG(!spawnbox_pass_type(expl, 1, childList, 1) && spawnbox_pass_type(expl, 1, expl, 1),
            "a parent type does not pass a list of its child; the type itself passes");
  CHECK_MSG(!spawnbox_pass_type(rocket, 3, nots, 0) && !spawnbox_pass_type(rocket, 0, nots, 3), "empty lists pass nothing");
  // The Battlelord's entry: QualifierClass None, 2 not-types, DamageScale 0.
  const SbDsEntry ripper[1] = { { 1, 0, 2, 0.f, 1, 0, 0 } };
  CHECK_MSG(spawnbox_dscale_verdict(ripper, 1) == SB_DS_TYPE_ZERO, "a type outside the NOT list is zeroed: excluded");
  const SbDsEntry rpg[1] = { { 1, 0, 2, 0.f, 1, 0, 1 } };
  CHECK_MSG(spawnbox_dscale_verdict(rpg, 1) == SB_DS_OK, "a type in the NOT list skips the entry: damageable");
  const SbDsEntry unk[1] = { { 1, 0, 2, 0.f, 0, 0, 0 } };
  CHECK_MSG(spawnbox_dscale_verdict(unk, 1) == SB_DS_UNKNOWN, "an unknown weapon type stays unknown, not excluded");
  const SbDsEntry only[2] = { { 1, 2, 0, 0.f, 1, 1, 0 }, { 1, 2, 0, 1.f, 1, 0, 0 } };
  CHECK_MSG(spawnbox_dscale_verdict(only, 2) == SB_DS_TYPE_ZERO, "a type list: the last entry misses, the earlier one matches");
  const SbDsEntry both[1] = { { 1, 1, 1, 0.f, 1, 1, 1 } };
  CHECK_MSG(spawnbox_dscale_verdict(both, 1) == SB_DS_OK, "in the types and in the NOT types: no match");
  const SbDsEntry later[2] = { { 1, 0, 0, 0.f }, { 1, 0, 1, 2.f, 1, 0, 0 } };
  CHECK_MSG(spawnbox_dscale_verdict(later, 2) == SB_DS_OK, "a later typed match shadows an earlier zero");
  const SbDsEntry fall[2] = { { 1, 0, 0, 0.f }, { 1, 0, 1, 2.f, 1, 0, 1 } };
  CHECK_MSG(spawnbox_dscale_verdict(fall, 2) == SB_DS_TYPE_ZERO, "past a typed miss, the zero holds for this weapon only");
  const SbDsEntry cls2[2] = { { 1, 0, 1, 0.f, 1, 0, 0 }, { 0, 0, 0, 1.f } };
  CHECK_MSG(spawnbox_dscale_verdict(cls2, 2) == SB_DS_TYPE_ZERO, "a class mismatch is still walked past first");
  CHECK_MSG(spawnbox_exclusion(0, 0, 0, 0, 0, 0, 0, 1) == SB_EXCL_WRONGTYPE && SB_EXCL_WRONGTYPE == 8 &&
            spawnbox_exclusion(0, 0, 0, 0, 0, 1, 0, 1) == SB_EXCL_NEUTRAL &&
            spawnbox_exclusion(0, 0, 0, 0, 1, 0, 0, 1) == SB_EXCL_PLAYERDMG0 &&
            spawnbox_exclusion(0, 0, 0, 0, 0, 0, 1, 1) == SB_EXCL_WRONGTYPE,
            "wrong damage type: after the neutral zone, before doomed");
  int t1, t2;
  CHECK_MSG(spawnbox_dmg_type_excludes(SB_DS_TYPE_ZERO, &t1, &t1) && !spawnbox_dmg_type_excludes(SB_DS_TYPE_ZERO, &t1, &t2) &&
            !spawnbox_dmg_type_excludes(SB_DS_TYPE_ZERO, 0, 0) && !spawnbox_dmg_type_excludes(SB_DS_OK, &t1, &t1) &&
            !spawnbox_dmg_type_excludes(SB_DS_UNKNOWN, &t1, &t1),
            "only a verdict read with the weapon held now excludes: a switch to an explosive frees the target at once");
  static SbDmgs d;
  memset(&d, 0, sizeof d);
  int a[1], due = 0;
  const int i = spawnbox_dmg_slot(&d, &a[0], 1000, 5, &due, &t1);
  CHECK_MSG(i >= 0 && due && d.d[i].wtype == &t1, "a new pawn is due, read with the weapon's type");
  CHECK_MSG(spawnbox_dmg_slot(&d, &a[0], 1010, 6, &due, &t1) == i && !due, "same weapon: not due");
  CHECK_MSG(spawnbox_dmg_slot(&d, &a[0], 1020, 7, &due, &t2) == i && due && d.d[i].wtype == &t2,
            "a weapon change makes it due at once");
  const void* wt = 0;
  int v = -9, nz = -9;
  d.d[i].verdict = SB_DS_TYPE_ZERO;
  CHECK_MSG(spawnbox_dmg_get(&d, &a[0], 1030, &v, &nz, &wt) && v == SB_DS_TYPE_ZERO && wt == &t2, "read back with its type");
}

static void tb_0462_ic_turret_lob_limits() {
  const AimTurretLimits l = { -8192, 2048, -8192, 8192, 1 };
  const AimProjModel m = { AIM_PM_BALLISTIC, 2500.f, 0.f, 0.f, 950.f, 0.f, 10.f, 0 };
  const float o[3] = { 0.f, 0.f, 0.f }, still[3] = { 0.f, 0.f, 0.f };
  const float far4[3] = { 4000.f, 0.f, 0.f }, near1[3] = { 1000.f, 0.f, 0.f }, tooFar[3] = { 20000.f, 0.f, 0.f };
  const float flat[3] = { 1.f, 0.f, 0.f };
  CHECK_MSG(aim_turret_dir_ok(l, 0, 0, 0, 0, flat), "the direct line to the far target is inside the limits");
  AimIntercept r = aim_intercept(m, o, far4, still);
  CHECK_MSG(r.ok && !aim_turret_dir_ok(l, 0, 0, 0, 0, r.dir), "its 18.7 degree lob is above MaxPitch 2048");
  r = aim_intercept(m, o, near1, still);
  CHECK_MSG(r.ok && aim_turret_dir_ok(l, 0, 0, 0, 0, r.dir), "a near lob fits");
  CHECK_MSG(aim_turret_dir_ok(l, 1500, 0, 0, 0, r.dir) == 0, "relative to the gun's RotatePitch");
  CHECK_MSG(!aim_intercept(m, o, tooFar, still).ok, "no solution: no lock either way");
  const AimProjModel fb = aim_turret_model(AIM_TUR_SUPER, 0.f, 0.f, 0.f, -1);
  AimProjModel d = aim_pm_defaults(fb, 15000.f, -1.f, 90000.f, -1.f);
  CHECK_MSG(d.speed == 15000.f && d.accel == 90000.f && d.maxSpeed == fb.maxSpeed && d.maxT == fb.maxT,
            "read values replace, unread keep the fallback");
  d = aim_pm_defaults(fb, -1.f, -1.f, 0.f, 3.f);
  CHECK_MSG(d.speed == 16000.f && d.accel == 0.f && d.maxT == 3.f, "a read 0 accel is a value");
}

// achhelp.h: the level match (package or MapFileName, path and extension dropped) and Duke's digs.
static void tb_0480_ah_map_match() {
  char k[32];
  ah_map_key("Maps\\map09.dnf", k, sizeof k);
  CHECK_MSG(strcmp(k, "map09") == 0, "path and extension dropped");
  CHECK(ah_map_in("Map16 Map17B", "map17b") && !ah_map_in("Map16 Map17B", "Map17") &&
        !ah_map_in("Map16 Map17B", "Map16B") && !ah_map_in("Map16", ""));
  CHECK(ah_is_mydigs("Map_mydigs") && ah_is_mydigs("map_MyDigs.dnf") && !ah_is_mydigs("Map09"));
}
// achhelp.h: which rows a level lists (Map09, Map21, Map00, Map_mydigs).
static bool ah_has(const int* ids, int n, int id) {
  for (int i = 0; i < n; ++i)
    if (ids[i] == id) return true;
  return false;
}
static void tb_0481_ah_listing() {
  int seen[AR_COUNT] = {}, ids[AH_ROWS_MAX];
  AhLevel lv = { "Map09", 0, 3, 14 };
  int n = ah_list(lv, seen, ids, AH_ROWS_MAX);
  const int map09[] = { 23, 24, 28, 38, 42, 29, 37, 12 };
  CHECK_MSG(n == 8, "Map09: five level rows, ego and phones, Noms");
  for (int id : map09) CHECK(ah_has(ids, n, id));
  seen[25] = 2;
  CHECK_MSG(ah_list(lv, seen, ids, AH_ROWS_MAX) == 9 && ah_has(ids, 9, 25), "an any-level row once its targets are seen");
  seen[25] = 0;
  const AhLevel m21 = { "Map21", 1, 0, 3 };
  n = ah_list(m21, seen, ids, AH_ROWS_MAX);
  CHECK(n == 4 && ah_has(ids, n, 39) && ah_has(ids, n, 49) && ah_has(ids, n, 36) && ah_has(ids, n, 29));
  const AhLevel m00 = { "map00", 0, 1, 3 };
  n = ah_list(m00, seen, ids, AH_ROWS_MAX);
  CHECK(ah_has(ids, n, 17) && ah_has(ids, n, 28) && ah_has(ids, n, 40) && ah_has(ids, n, 44) && !ah_has(ids, n, 30));
  seen[25] = 3;
  const AhLevel digs = { "Map_mydigs", 0, 0, 0 };
  CHECK_MSG(ah_list(digs, seen, ids, AH_ROWS_MAX) == 0, "nothing awards in Duke's digs");
  const AhAch* a = ah_find(55);
  CHECK_MSG(!a && !ah_find(63) && !ah_find(31) && ah_find(30), "karaoke, the dead id and completion are never rows");
}
// achhelp.h: a class chain's candidate kinds and the instance key that picks one.
static int ah_test_name(const char* cls) {   // one index per distinct class name, as uname_find gives
  for (int k = 0; k < AK_COUNT; ++k)
    if (strcmp(AH_KINDS[k].cls, cls) == 0) return 100 + k;
  return 0;
}
static void tb_0482_ah_kind_match() {
  int cls[AK_COUNT], key[AK_COUNT];
  for (int k = 0; k < AK_COUNT; ++k) {
    cls[k] = ah_test_name(AH_KINDS[k].cls);
    key[k] = AH_KINDS[k].key ? 500 + k : 0;
  }
  const int popcorn[2] = { 7, ah_test_name("Snacks_Generic") };
  const unsigned food = ah_class_mask(popcorn, 2, cls);
  CHECK(food == 1u << AK_FOOD && ah_pick_kind(food, 0, 0, 0, key) == AK_FOOD);
  const int rc[2] = { 9, ah_test_name("Veh_MonsterTruck") }, truck[1] = { ah_test_name("Veh_MonsterTruck") };
  CHECK_MSG(!ah_class_mask(rc, 2, cls) && ah_class_mask(truck, 1, cls) == 1u << AK_MONSTERTRUCK,
            "the RC truck subclass is not the monster truck");
  const int counter[1] = { ah_test_name("Counter") };
  const unsigned cm = ah_class_mask(counter, 1, cls);
  CHECK(ah_pick_kind(cm, key[AK_CATFISH_COUNTER], 0, 0, key) == AK_CATFISH_COUNTER &&
        ah_pick_kind(cm, key[AK_CAN_COUNTER], 0, 0, key) == AK_CAN_COUNTER && ah_pick_kind(cm, 77, 0, 0, key) == -1);
  const int fish[1] = { ah_test_name("Biology_Generic_Catfish") };
  const unsigned fm = ah_class_mask(fish, 1, cls);
  CHECK_MSG(ah_pick_kind(fm, 0, key[AK_CATFISH], 0, key) == AK_CATFISH && ah_pick_kind(fm, 0, 0, 0, key) == -1,
            "catfish by Event");
  const int none[2] = { 0, 0 };
  CHECK_MSG(!ah_class_mask(none, 2, cls) && ah_pick_kind(1u << AK_BARREL, 0, 0, 0, key) == -1,
            "an unresolved name never matches; a Tag kind needs its Tag");
}
// achhelp.h: auto/on/off and the switch's presence.
static void tb_0483_ah_modes() {
  CHECK(ah_esp_on(AH_MODE_AUTO, 0) && !ah_esp_on(AH_MODE_AUTO, 1) && ah_esp_on(AH_MODE_ON, 1) &&
        !ah_esp_on(AH_MODE_OFF, 0));
  int m = -1;
  CHECK(ah_mode_parse("auto", &m) && m == AH_MODE_AUTO && ah_mode_parse("On", &m) && m == AH_MODE_ON &&
        ah_mode_parse("OFF", &m) && m == AH_MODE_OFF && !ah_mode_parse("1", &m) && m == AH_MODE_OFF);
  CHECK(strcmp(ah_mode_text(AH_MODE_ON), "on") == 0 && strcmp(ah_mode_text(9), "auto") == 0);
  CHECK_MSG(ah_has_switch(*ah_find(38)) && !ah_has_switch(*ah_find(44)) && !ah_has_switch(*ah_find(36)) &&
            !ah_has_switch(*ah_find(35)), "bosses, collectibles and the credits have no switch");
}
// achhelp.h: the sort (group, then unearned first, then table order), the progress text and the marker label.
static void tb_0484_ah_sort_progress_label() {
  const AhSortKey k[4] = { { 2, 0, 0 }, { 0, 1, 1 }, { 0, 0, 2 }, { 1, 0, 3 } };
  int idx[4];
  ah_sort(k, 4, idx);
  CHECK(idx[0] == 2 && idx[1] == 1 && idx[2] == 3 && idx[3] == 0);
  char t[16];
  CHECK(ah_progress_text(AH_PROG_LEVEL, 9, 9, 1, t, sizeof t) == 0 && strcmp(t, "0/9") == 0);
  CHECK(ah_progress_text(AH_PROG_LEVEL, 9, 6, 1, t, sizeof t) == 3 && strcmp(t, "3/9") == 0);
  CHECK(ah_progress_text(AH_PROG_LEVEL, 9, -2, 1, t, sizeof t) == 9);
  CHECK(ah_progress_text(AH_PROG_PROFILE, 10, 14, 1, t, sizeof t) == 10 && strcmp(t, "10/10") == 0);
  CHECK_MSG(ah_progress_text(AH_PROG_PROFILE, 10, 6, 0, t, sizeof t) == -1 && strcmp(t, "...") == 0,
            "unread until a real read lands: never 0/10");
  CHECK(ah_progress_text(AH_PROG_NONE, 0, 0, 1, t, sizeof t) == -1 && !t[0]);
  char l[64];
  ah_marker_label("Party Animal", "can", "3/9", l, sizeof l);
  CHECK(strcmp(l, "Party Animal: can (3/9)") == 0);
  ah_marker_label("Turd Burglar", 0, "", l, sizeof l);
  CHECK(strcmp(l, "Turd Burglar") == 0);
  ah_marker_label("Noms", 0, "6/10", l, sizeof l);
  CHECK(strcmp(l, "Noms (6/10)") == 0);
}
// achhelp.h: when a target is marked, the earned and header texts, and the instrument's line budget.
static void tb_0485_ah_live_texts_budget() {
  CHECK(ah_target_live(0, 0, 0, 0, 0.f, 0) && !ah_target_live(0, 1, 0, 0, 0.f, 0) && !ah_target_live(0, 0, 1, 0, 0.f, 0));
  CHECK_MSG(ah_target_live(0, 0, 0, 1, 0.f, 0) && !ah_target_live(AH_K_ALIVE, 0, 0, 1, 0.f, 0) &&
            ah_target_live(AH_K_ALIVE, 0, 0, 0, 0.f, 0) && ah_target_live(AH_K_ALIVE, 0, 0, 1, 5.f, 0),
            "Health only for a kind that dies, and only once read");
  CHECK(!ah_target_live(AH_K_GRABBED, 0, 0, 0, 0.f, 1) && ah_target_live(0, 0, 0, 0, 0.f, 1));
  char t[48];
  ah_earned_text(1300000000u, 0, t, sizeof t);
  CHECK(strcmp(t, "Earned 2011-03-13") == 0);
  ah_earned_text(0, 0, t, sizeof t);
  CHECK(strcmp(t, "Earned") == 0);
  ah_count_text(2, 6, t, sizeof t);
  CHECK(strcmp(t, "2 of 6 earned") == 0);
  int used = 0, took = 0;
  for (int i = 0; i < 64; ++i) took += ah_log_take(&used);
  CHECK(took == AH_LOG_CAP && AH_LOG_CAP == 40);
}

// config.cpp: [achievements] ach.esp.<id> = auto | on | off, auto by default and for a value it does not know.
static void tb_0486_ach_config_keys() {
  config_defaults();
  bool allAuto = true;
  for (int id = 0; id < AR_COUNT; ++id) allAuto &= g_cfg.achEsp[id] == AH_MODE_AUTO;
  CHECK(allAuto);
  CHECK(config_key_known("achievements", "ach.esp.38") && !config_key_known("achievements", "ach.esp.64") &&
        !config_key_known("achievements", "ach.esp.x"));
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_achesp");
  write_text(path, "[achievements]\r\nach.esp.38=on\r\nach.esp.17=OFF\r\nach.esp.5=maybe\r\n");
  CHECK(config_load_from(path) == true);
  CHECK(g_cfg.achEsp[38] == AH_MODE_ON && g_cfg.achEsp[17] == AH_MODE_OFF && g_cfg.achEsp[5] == AH_MODE_AUTO);
  g_cfg.achEsp[38] = AH_MODE_OFF;
  g_cfgDirty = 1;
  config_write_changed();
  char v[16] = "";
  GetPrivateProfileStringA("achievements", "ach.esp.38", "", v, sizeof v, path);
  CHECK_MSG(strcmp(v, "off") == 0, "written as its word");
  CHECK(config_load_from(path) == true && g_cfg.achEsp[38] == AH_MODE_OFF && g_cfg.achEsp[17] == AH_MODE_OFF);
  DeleteFileA(path);
  config_defaults();
}

// config.cpp: an ach.esp id is plain decimal; a leading zero must not read as octal ("010" is 10, "08" is 8).
static void tb_0523_ach_key_base10() {
  config_defaults();
  CHECK(config_key_known("achievements", "ach.esp.010") && config_key_known("achievements", "ach.esp.08"));
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_achesp10");
  write_text(path, "[achievements]\r\nach.esp.010=on\r\n");
  CHECK(config_load_from(path) == true);
  CHECK(g_cfg.achEsp[10] == AH_MODE_ON && g_cfg.achEsp[8] == AH_MODE_AUTO);
  DeleteFileA(path);
  config_defaults();
}

// achhelp.h: the aim takes catfish while Pescaphobe is listed here and its switch resolves on,
// as its markers; never in another map, Duke's digs or a pending checkpoint, never a kind without AH_K_AIM.
static void tb_0524_ah_aim_activation() {
  int mode[AR_COUNT] = {}, earned[AR_COUNT] = {};   // AH_MODE_AUTO, unearned
  CHECK(ah_aim_kinds("Map21", mode, earned) == 1u << AK_CATFISH);
  CHECK_MSG(ah_aim_kinds("../Maps/map21.dnf", mode, earned) == 1u << AK_CATFISH, "the map key as the tab matches it");
  mode[39] = AH_MODE_ON;
  earned[39] = 1;
  CHECK_MSG(ah_aim_kinds("Map21", mode, earned) == 1u << AK_CATFISH, "on: earned or not");
  mode[39] = AH_MODE_OFF;
  earned[39] = 0;
  CHECK_MSG(ah_aim_kinds("Map21", mode, earned) == 0u, "off");
  mode[39] = AH_MODE_AUTO;
  CHECK_MSG(!ah_aim_kinds("Map09", mode, earned) && !ah_aim_kinds("Map_mydigs", mode, earned) &&
            !ah_aim_kinds("cp03", mode, earned) && !ah_aim_kinds("", mode, earned),
            "only where the row is listed");
  CHECK_MSG(!(AH_KINDS[AK_CAN].flags & AH_K_AIM) && (AH_KINDS[AK_CATFISH].flags & AH_K_AIM) &&
            !(AH_KINDS[AK_CATFISH_COUNTER].flags & AH_K_AIM), "the catfish alone, never its Counter");
}
// achhelp.h: any hostile pawn candidate this frame (in range and the cone, hidden or not)
// holds the fish back, so the bot never shoots fish during a fight; a picked ship wins too; an unknown count refuses.
static void tb_0525_ah_aim_pawn_priority() {
  CHECK(ah_aim_pass(0, 0));
  CHECK_MSG(!ah_aim_pass(1, 0), "a hidden pawn candidate: no fish");
  CHECK(!ah_aim_pass(5, 0) && !ah_aim_pass(0, 1) && !ah_aim_pass(2, 1));
  CHECK_MSG(!ah_aim_pass(-1, 0), "the count unknown (no pick ran)");
}
// achhelp.h: a dead, destroyed, hidden or bNoDamage fish is no target; Health must read.
static void tb_0526_ah_aim_target_live() {
  const int f = AH_KINDS[AK_CATFISH].flags;
  CHECK(ah_aim_target_ok(f, 0, 0, 1, 1.f, 0));
  CHECK_MSG(ah_aim_target_ok(f, 0, 0, 1, 1.f, -1), "bNoDamage unread is no refusal");
  CHECK_MSG(!ah_aim_target_ok(f, 0, 0, 1, 0.f, 0) && !ah_aim_target_ok(f, 0, 0, 1, -3.f, 0), "dead");
  CHECK_MSG(!ah_aim_target_ok(f, 1, 0, 1, 1.f, 0), "being destroyed");
  CHECK_MSG(!ah_aim_target_ok(f, 0, 1, 1, 1.f, 0) && !ah_aim_target_ok(f, 0, 0, 1, 1.f, 1), "hidden, bNoDamage");
  CHECK_MSG(!ah_aim_target_ok(f, 0, 0, 0, 1.f, 0), "Health unread");
  CHECK_MSG(!ah_aim_target_ok(AH_KINDS[AK_CAN].flags, 0, 0, 1, 1.f, 0), "not an aim kind");
}
// achhelp.h: auto stops once Steam says earned, for the aim as for the markers.
static void tb_0527_ah_aim_earned_auto() {
  int mode[AR_COUNT] = {}, earned[AR_COUNT] = {};
  earned[39] = 1;
  CHECK(ah_aim_kinds("Map21", mode, earned) == 0u);
  earned[49] = 1;   // another Map21 row earned changes nothing for the fish
  earned[39] = 0;
  CHECK(ah_aim_kinds("Map21", mode, earned) == 1u << AK_CATFISH);
}
// achhelp.h: dnDecoration.TakeDamage's type rules (dnGame.u): a non-empty DamageTypesAcceptedOnly needs a parent of the
// type, DamageTypesIgnored refuses an exact match; an unknown type passes only empty lists; an unread list refuses.
static void tb_0528_ah_aim_damage_type() {
  CHECK(ah_aim_type_ok(0, 0, 0, 0, 1) && ah_aim_type_ok(0, 0, 0, 0, 0));
  CHECK(ah_aim_type_ok(2, 0, 1, 0, 1) && !ah_aim_type_ok(2, 0, 0, 0, 1));
  CHECK(!ah_aim_type_ok(0, 1, 0, 1, 1) && ah_aim_type_ok(0, 1, 0, 0, 1));
  CHECK_MSG(!ah_aim_type_ok(1, 0, 0, 0, 0) && !ah_aim_type_ok(0, 1, 0, 0, 0), "unknown type, non-empty list");
  CHECK_MSG(!ah_aim_type_ok(-1, 0, 0, 0, 1) && !ah_aim_type_ok(0, -1, 0, 0, 1), "unread");
}

// achhelp.h (2026-10-11, checkpoint bug): a checkpoint load goes through Entry, which frees Map21's names; back in
// Map21 under a new level serial, catfishCounter has a new index. The pass looks every name up again, so the fish
// (by Event) and the Counter (by Tag) match again; the old index never does.
struct AhFakeNames { int fish, counter, key; };
static int ah_fake_find(const char* s, void* ctx) {
  const AhFakeNames* f = (const AhFakeNames*)ctx;
  if (strcmp(s, "Biology_Generic_Catfish") == 0) return f->fish;
  if (strcmp(s, "Counter") == 0) return f->counter;
  if (strcmp(s, "catfishCounter") == 0) return f->key;
  return 0;
}
static void tb_0529_ah_names_after_checkpoint() {
  int cls[AK_COUNT] = {}, key[AK_COUNT] = {};
  AhFakeNames f = { 300, 301, 500 };                    // serial 2: Map21 from the menu
  CHECK(ah_resolve_names(ah_fake_find, &f, cls, key) >= 2 && key[AK_CATFISH] == 500 && key[AK_CATFISH_COUNTER] == 500);
  f.fish = 0;                                           // serial 3: Entry, Map21's package and names gone
  f.key = 0;
  ah_resolve_names(ah_fake_find, &f, cls, key);
  CHECK_MSG(key[AK_CATFISH] == 0 && cls[AK_CATFISH] == 0 && cls[AK_CATFISH_COUNTER] == 301, "Entry: no stale key");
  f.fish = 300;                                         // serial 4: Map21 again, its key under a new index
  f.key = 777;
  ah_resolve_names(ah_fake_find, &f, cls, key);
  CHECK(key[AK_CATFISH] == 777 && key[AK_CATFISH_COUNTER] == 777 && cls[AK_CATFISH] == 300);
  const int fish[1] = { 300 }, counter[1] = { 301 };
  const unsigned fm = ah_class_mask(fish, 1, cls), cm = ah_class_mask(counter, 1, cls);
  CHECK_MSG(ah_pick_kind(fm, 0, 777, 0, key) == AK_CATFISH && ah_pick_kind(cm, 777, 0, 0, key) == AK_CATFISH_COUNTER,
            "the rescan finds the fish and the Counter");
  CHECK_MSG(ah_pick_kind(fm, 0, 500, 0, key) == -1, "the old index matches nothing");
}

// lvnames.h (2026-10-11, checkpoint bug audit): a map's names (a stash Event, a phone's ACH_ Event) get new indices
// after a level load. Keys looked up again per pass match the new indices, the old ones match nothing, and a name that
// is gone reads as absent, never as the old index.
struct LnFake { int ev, phone; };
static int ln_fake_find(const char* s, void* ctx) {
  const LnFake* f = (const LnFake*)ctx;
  if (strcmp(s, "SecretSteroids_RoundRobin") == 0) return f->ev;
  if (strcmp(s, "ACH_PhoneMessage_03") == 0) return f->phone;
  return 0;
}
static void tb_0534_ln_level_names_move() {
  const char* sn[BK_STASH_COUNT];
  for (int i = 0; i < BK_STASH_COUNT; ++i) sn[i] = BK_STASHES[i].event;
  char pb[PG_PHONES][32];
  const char* pn[PG_PHONES];
  for (int i = 0; i < PG_PHONES; ++i) { cl_phone_event_name(i + 1, pb[i], 32); pn[i] = pb[i]; }
  static_assert(BK_STASH_COUNT <= LN_MAX && PG_PHONES <= LN_MAX, "lvnames.h keeps LN_MAX keys");
  LnKeys stash = {}, phone = {};
  LnFake f = { 1100, 2200 };                            // serial 2
  ln_refresh(&stash, sn, BK_STASH_COUNT, ln_fake_find, &f);
  ln_refresh(&phone, pn, PG_PHONES, ln_fake_find, &f);
  CHECK(bk_stash_of(1100, stash.cur, BK_STASH_COUNT) == 1 && cl_phone_id(2200, phone.cur) == 3);
  char line[256];
  CHECK(ln_due(&stash, 2, BK_STASH_COUNT) && ln_format(&stash, sn, BK_STASH_COUNT, line, sizeof line) == 0);
  ln_logged(&stash, 2, BK_STASH_COUNT);
  CHECK_MSG(!ln_due(&stash, 2, BK_STASH_COUNT), "one line per serial");
  f.ev = 0;                                             // serial 3: Entry, the names are gone
  f.phone = 0;
  ln_refresh(&stash, sn, BK_STASH_COUNT, ln_fake_find, &f);
  ln_refresh(&phone, pn, PG_PHONES, ln_fake_find, &f);
  CHECK_MSG(bk_stash_of(1100, stash.cur, BK_STASH_COUNT) == -1 && cl_phone_id(2200, phone.cur) == 0, "no stale index");
  CHECK(ln_due(&stash, 3, BK_STASH_COUNT) && ln_format(&stash, sn, BK_STASH_COUNT, line, sizeof line) == 1 &&
        strstr(line, "gone"));
  ln_logged(&stash, 3, BK_STASH_COUNT);
  f.ev = 1900;                                          // serial 4: back, new indices
  f.phone = 2900;
  ln_refresh(&stash, sn, BK_STASH_COUNT, ln_fake_find, &f);
  ln_refresh(&phone, pn, PG_PHONES, ln_fake_find, &f);
  CHECK(bk_stash_of(1900, stash.cur, BK_STASH_COUNT) == 1 && bk_stash_of(1100, stash.cur, BK_STASH_COUNT) == -1);
  CHECK(cl_phone_id(2900, phone.cur) == 3 && cl_phone_id(2200, phone.cur) == 0);
  ln_logged(&stash, 4, BK_STASH_COUNT);
  f.ev = 1950;                                          // same names, moved between two logged lines
  ln_refresh(&stash, sn, BK_STASH_COUNT, ln_fake_find, &f);
  CHECK(ln_due(&stash, 4, BK_STASH_COUNT) && ln_format(&stash, sn, BK_STASH_COUNT, line, sizeof line) == 1 &&
        strstr(line, "moved from 1900"));
}

// achhelp.h: the scan and the markers follow the achievement switches alone, never a Visuals row:
// the tab, or in a level a listed row's switch on, or an any-level row's switch that would be on.
static void tb_0487_ah_scan_wanted() {
  CHECK(ah_scan_wanted(1, 0, 0, 0) && ah_scan_wanted(0, 1, 1, 0) && ah_scan_wanted(0, 1, 0, 1));
  CHECK(!ah_scan_wanted(0, 1, 0, 0) && !ah_scan_wanted(0, 0, 1, 1));
}

// achhelp.h: a pass keeps only this map's kinds (the barrel goal's instance name only in Map17B),
// a map is pending while it reads cpNN or nothing, and a ship kind maps to the aircraft pass's kind.
static void tb_0488_ah_kind_here_pending_ships() {
  CHECK(ah_kind_here(AK_BARREL_GOAL, "Map17B") && !ah_kind_here(AK_BARREL_GOAL, "Map09"));
  CHECK(ah_kind_here(AK_CAN, "Map09") && ah_kind_here(AK_CAN_COUNTER, "map09") && !ah_kind_here(AK_CAN, "Map21"));
  CHECK_MSG(ah_kind_here(AK_BEER, "Map05") && ah_kind_here(AK_FOOD, "Map09") && !ah_kind_here(AK_FOOD, "Map02"),
            "an any-level kind everywhere, a listed map's kind only there");
  CHECK(!ah_kind_here(AK_BEER, "Map_mydigs") && !ah_kind_here(AK_POO, ""));
  CHECK(ah_map_pending("") && ah_map_pending("cp02") && ah_map_pending("CP11") && !ah_map_pending("Map09") &&
        !ah_map_pending("cpu"));
  CHECK(ah_ship_kind(AK_FIGHTER) == AC_FIGHTER && ah_ship_kind(AK_GUNSHIP) == AC_GUNSHIP &&
        ah_ship_kind(AK_DROPSHIP) == AC_DROPSHIP && ah_ship_kind(AK_BEER) == -1);
  CHECK_MSG(!ac_damageable(ah_ship_kind(AK_DROPSHIP), 0, 0, -1) && !ac_damageable(AC_GUNSHIP, 1, -1, -1) &&
            ac_damageable(AC_DROPSHIP, 0, 1, -1), "Map11C's NeverBreak dummies and bNoDamage ships do not count");
}

// menu.h (the Achievements tab text is spilling outside of the tab bar): the rail fits its longest
// name with RAIL_LABEL_X each side, and a scale that measures wider widens it at runtime.
static void tb_0489_menu_rail_fits_names() {
  int widest = 0;
  for (int t = 0; t < TAB_COUNT; ++t) {
    const int w = tw(menu_tab_name(t), FONT_UI);
    if (w > widest) widest = w;
  }
  CHECK_MSG(strcmp(menu_tab_name(TAB_ACHIEVEMENTS), "Achievements") == 0, "the name stays");
  CHECK_MSG(menu_rail_w(widest) == RAIL_W && widest + 2 * RAIL_LABEL_X + 7 <= RAIL_W,
            "every name fits at 100%, with 7 px for a scaled atlas's rounding");
  CHECK(menu_rail_w(150) == 150 + 2 * RAIL_LABEL_X && menu_rail_w(0) == RAIL_W);
  CHECK_MSG(PANEL_W - RAIL_W == 620 - 116 && PANEL_MIN_W - RAIL_W == 560 - 116,
            "the panel widens with the rail: the content column keeps its width");
}

// menu.h: the live count is one pure function over a table of (name, on, refused);
// the tooltip names the live ones. A new gameplay row joins by adding one table entry in menu.cpp.
static void tb_0490_menu_live_table() {
  const MenuLive t[] = { { "Aimbot", 1, 0 }, { "ESP-ish", 0, 0 }, { "Triggerbot", 1, 0 }, { "Silent aim", 1, 1 },
                         { "Fire rate", 1, 0 } };
  CHECK_MSG(menu_live_count(t, 5) == 3, "on and not refused only");
  CHECK(menu_live_count(t, 0) == 0 && menu_live_count(0, 0) == 0);
  char names[128];
  menu_live_names(t, 5, names, sizeof names);
  CHECK_MSG(strcmp(names, "Aimbot, Triggerbot, Fire rate") == 0, "the live ones, in table order");
  const MenuLive none[] = { { "Aimbot", 0, 0 } };
  menu_live_names(none, 1, names, sizeof names);
  CHECK_MSG(names[0] == 0, "nothing live: no text");
  char tiny[12];
  menu_live_names(t, 5, tiny, sizeof tiny);
  CHECK_MSG(strlen(tiny) < sizeof tiny && strncmp(tiny, "Aimbot", 6) == 0, "a short buffer is cut, never overrun");
  menu_live_names(t, 5, tiny, 0);   // no room: nothing written, no crash
  CHECK(menu_live_count(t, 5) == 3);
}

// pbtrace.h: table space is `d << Rotation`; at yaw 90 deg world +X is local -Y; the inverse round-trips with pitch
// and roll; Map02's table (Yaw 23552) keeps lengths.
static void tb_0491_pb_local_transform() {
  const float wx[3] = { 1.f, 0.f, 0.f }, wy[3] = { 0.f, 1.f, 0.f };
  float l[3];
  pb_local(wx, 0, 0, 0, l);
  CHECK(fabsf(l[0] - 1.f) < 1e-5f && fabsf(l[1]) < 1e-5f && fabsf(l[2]) < 1e-5f);
  pb_local(wx, 0, 16384, 0, l);
  CHECK_MSG(fabsf(l[0]) < 1e-5f && fabsf(l[1] + 1.f) < 1e-5f && fabsf(l[2]) < 1e-5f, "yaw 90: world +X is local -Y");
  pb_local(wy, 0, 16384, 0, l);
  CHECK_MSG(fabsf(l[0] - 1.f) < 1e-5f && fabsf(l[1]) < 1e-5f, "yaw 90: world +Y is local +X");
  const int rots[4][3] = { { 0, 23552, 0 }, { 1000, 23552, -2000 }, { -16384, 5000, 30000 }, { 8192, -8192, 4096 } };
  const float v[3] = { 44.f, -21.f, 8.f };
  for (int i = 0; i < 4; i++) {
    float w[3], back[3];
    pb_world(v, rots[i][0], rots[i][1], rots[i][2], w);
    pb_local(w, rots[i][0], rots[i][1], rots[i][2], back);
    CHECK_MSG(fabsf(back[0] - v[0]) < 1e-3f && fabsf(back[1] - v[1]) < 1e-3f && fabsf(back[2] - v[2]) < 1e-3f,
              "pb_local inverts pb_world");
    const float lw = sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]), lv = sqrtf(44.f * 44.f + 21.f * 21.f + 64.f);
    CHECK(fabsf(lw - lv) < 1e-3f);
  }
}

// pbtrace.h: the frame and event rows, exactly; a row that does not fit writes nothing; commas in a detail.
static void tb_0492_pb_rows() {
  PbFrame f;
  memset(&f, 0, sizeof f);
  f.frame = 7; f.gameTime = 12.5f; f.ms = 1000.25; f.dtMs = 16.67;
  f.pos[0] = 44.f; f.pos[1] = -21.f; f.pos[2] = 7.5f; f.vel[0] = -350.f; f.vsrc = 'K';
  f.lUp = 3; f.rUp = 1; f.lBtn = 1; f.fire = 1; f.plunger = 1; f.score = 2500; f.balls = 3; f.mult = 2;
  char b[256];
  const int n = pb_fmt_frame(b, sizeof b, f);
  CHECK(n > 0 && strcmp(b, "F,7,12.5000,1000.25,16.67,44.000,-21.000,7.500,-350.00,0.00,0.00,K,3,1,1,0,1,0,1,2500,3,2\n")
                 == 0);
  CHECK_MSG(pb_fmt_frame(b, 20, f) == 0, "truncated: nothing");
  const int e = pb_fmt_event(b, sizeof b, 9, 1.f, 2.5, "input", 23, "bIBackward, pressed");
  CHECK(e > 0 && strcmp(b, "E,9,1.0000,2.50,input,23,bIBackward; pressed\n") == 0);
  CHECK(pb_fmt_event(b, 8, 9, 1.f, 2.5, "input", 23, "") == 0);
  int cols = 0;
  for (const char* p = PB_FRAME_COLUMNS; *p; p++) cols += *p == ',';
  int vals = 0;
  pb_fmt_frame(b, sizeof b, f);
  for (const char* p = b; *p; p++) vals += *p == ',';
  CHECK_MSG(cols == vals, "the column line matches the row");
}

// pbtrace.h: flag edges to control events, drains, the flush and row caps, the frame rate.
static void tb_0493_pb_rules() {
  CHECK(pb_flag_event(23, 0, 1) == 23 && pb_flag_event(23, 1, 0) == 25 && pb_flag_event(23, 1, 1) == -1 &&
        pb_flag_event(14, 0, 0) == -1 && pb_flag_event(-1, 0, 1) == -1);
  CHECK(pb_drain(0, 1, 3, 2) == PB_DRAIN_LOST && pb_drain(0, 1, 3, 3) == PB_DRAIN_SAVED &&
        pb_drain(1, 1, 3, 3) == PB_DRAIN_NONE && pb_drain(1, 0, 0, 3) == PB_DRAIN_NONE);
  CHECK_MSG(pb_drain(0, 0, 1, 0) == PB_DRAIN_LOST, "the last ball: lost without a gutter edge");
  CHECK(!pb_flush_due(5000, 0, 0, 100, 10) && pb_flush_due(1000, 0, 1, 100, 10) && !pb_flush_due(999, 0, 50, 100, 10) &&
        pb_flush_due(999, 0, 95, 100, 10));
  CHECK_MSG(pb_flush_due(5, 0xFFFFFFF0u, 1, 100, 1) == 0 && pb_flush_due(1000, 0xFFFFFFF0u, 1, 100, 1),
            "tick wrap");
  CHECK(pb_row_admit(0) && pb_row_admit(PB_ROW_CAP - 1) && !pb_row_admit(PB_ROW_CAP));
  const float dts[4] = { 16.f, 0.f, 16.f, 18.f };
  CHECK(fabsf(pb_fps(dts, 4) - 3000.f / 50.f) < 1e-3f && pb_fps(dts, 0) == 0.f && pb_fps(0, 4) == 0.f);
}

// pinbot.h: the Map02 table as the trace's header read it (left flippers (46.5, 9) and (16, 18), right (46.5, -5)).
static PbbTable pbb_map02() {
  PbbTable t = {};
  t.f[PBB_LEFT][0] = PbbFlipper{ 46.5f, 9.f, -8188 };
  t.f[PBB_LEFT][1] = PbbFlipper{ 16.f, 18.f, -8188 };
  t.f[PBB_RIGHT][0] = PbbFlipper{ 46.5f, -5.f, 8191 };
  t.n[PBB_LEFT] = 2;
  t.n[PBB_RIGHT] = 1;
  return t;
}
static PbbIn pbb_ball(unsigned ms, float x, float y, float vx, float vy) {
  PbbIn in = {};
  in.active = 1; in.attached = 1; in.nowMs = ms; in.haveBall = 1; in.x = x; in.y = y; in.vx = vx; in.vy = vy;
  return in;
}

// pinbot.h: the flipper frame and the zone, on the trace's own presses (minigame-autoplay.md section 7).
static void tb_0494_pbb_zone() {
  const PbbTable t = pbb_map02();
  float a, h;
  pbb_flip_frame(t.f[PBB_LEFT][0], 43.6f, 6.4f, &a, &h);
  CHECK_MSG(fabsf(a + 0.3f) < 0.1f && fabsf(h - 3.9f) < 0.1f, "f1411: along -0.3, perp 3.9");
  pbb_flip_frame(t.f[PBB_RIGHT][0], 49.2f, 1.3f, &a, &h);
  CHECK_MSG(fabsf(a - 6.4f) < 0.1f && fabsf(h - 2.5f) < 0.1f, "f7697: along 6.4, perp 2.5");
  CHECK_MSG(pbb_in_zone(t.f[PBB_LEFT][0], 43.6f, 6.4f) && pbb_in_zone(t.f[PBB_LEFT][0], 46.4f, 4.3f) &&
            pbb_in_zone(t.f[PBB_RIGHT][0], 45.6f, -2.7f) && pbb_in_zone(t.f[PBB_RIGHT][0], 41.6f, -0.1f),
            "the hits' places are in their zones");
  CHECK_MSG(!pbb_in_zone(t.f[PBB_LEFT][0], 41.f, 3.2f) && !pbb_in_zone(t.f[PBB_RIGHT][0], 40.7f, 3.2f),
            "f3638: too early, perp 8 and 10");
  CHECK_MSG(!pbb_in_zone(t.f[PBB_RIGHT][0], 49.2f, 1.3f), "f7697: too late, past the tip");
  CHECK_MSG(!pbb_in_zone(t.f[PBB_LEFT][0], 0.f, 0.f) && !pbb_in_zone(t.f[PBB_RIGHT][0], 44.f, -21.f),
            "mid-table and the lane are in no zone");
  CHECK(pbb_in_lane(44.f, -21.f) && !pbb_in_lane(44.f, 0.f) && !pbb_in_lane(20.f, -21.f));
}

// pinbot.h: a ball entering a zone toward the drain fires that side only; held PBB_HOLD_MS, then released; the
// side rearms after PBB_REARM_MS; a ball moving up the table or far away fires nothing.
static void tb_0495_pbb_fire_hold_rearm() {
  const PbbTable t = pbb_map02();
  PbbState s = {};
  PbbOut o = pbb_step(&s, pbb_ball(1000, 20.f, 0.f, 60.f, 0.f), t);
  CHECK_MSG(!o.press[PBB_LEFT] && !o.press[PBB_RIGHT], "far away: nothing");
  o = pbb_step(&s, pbb_ball(1010, 43.6f, 6.4f, -80.f, 0.f), t);
  CHECK_MSG(!o.press[PBB_LEFT], "in the zone but moving up the table: nothing");
  o = pbb_step(&s, pbb_ball(1020, 43.6f, 6.4f, 69.f, -9.f), t);
  CHECK(o.press[PBB_LEFT] && !o.press[PBB_RIGHT] && o.why[PBB_LEFT] == PBB_WHY_FIRE && s.down[PBB_LEFT]);
  o = pbb_step(&s, pbb_ball(1030, 44.f, 6.f, 60.f, 0.f), t);
  CHECK_MSG(!o.press[PBB_LEFT] && !o.release[PBB_LEFT], "held: no second press");
  o = pbb_step(&s, pbb_ball(1020 + PBB_HOLD_MS, 20.f, 0.f, -150.f, 0.f), t);
  CHECK(o.release[PBB_LEFT] && o.why[PBB_LEFT] == PBB_WHY_HELD && !s.down[PBB_LEFT]);
  o = pbb_step(&s, pbb_ball(1020 + PBB_HOLD_MS + PBB_REARM_MS - 1, 43.6f, 6.4f, 69.f, 0.f), t);
  CHECK_MSG(!o.press[PBB_LEFT], "not rearmed yet");
  o = pbb_step(&s, pbb_ball(1020 + PBB_HOLD_MS + PBB_REARM_MS, 43.6f, 6.4f, 69.f, 0.f), t);
  CHECK_MSG(o.press[PBB_LEFT], "rearmed");
  // The lead: a fast ball just above the zone fires a frame early.
  PbbState s2 = {};
  o = pbb_step(&s2, pbb_ball(5000, 39.f, -0.5f, 300.f, 0.f), t);
  CHECK_MSG(o.press[PBB_RIGHT], "predicted into the right zone at 300 u/s");
}

// pinbot.h: both sides only when the ball is in both zones (the centre gap); the upper left flipper fires the left.
static void tb_0496_pbb_both_and_upper() {
  const PbbTable t = pbb_map02();
  PbbState s = {};
  PbbOut o = pbb_step(&s, pbb_ball(100, 46.f, 2.f, 40.f, 0.f), t);
  CHECK_MSG(o.press[PBB_LEFT] && o.press[PBB_RIGHT], "the centre gap is in both zones");
  PbbState s2 = {};
  o = pbb_step(&s2, pbb_ball(100, 14.f, 15.5f, 40.f, 0.f), t);
  CHECK_MSG(o.press[PBB_LEFT] && !o.press[PBB_RIGHT], "the upper left flipper");
  PbbState s3 = {};
  o = pbb_step(&s3, pbb_ball(100, 30.f, 2.f, 40.f, 0.f), t);
  CHECK_MSG(!o.press[PBB_LEFT] && !o.press[PBB_RIGHT], "mid-table: neither");
}

// pinbot.h: a shot is held PBB_HOLD_MS whatever the ball does (cradles are their own state, tests 0508-0509).
static void tb_0497_pbb_cradle_cap() {
  const PbbTable t = pbb_map02();
  PbbState s = {};
  PbbOut o = pbb_step(&s, pbb_ball(0, 44.f, 6.f, 60.f, 0.f), t);
  CHECK(o.press[PBB_LEFT] && o.why[PBB_LEFT] == PBB_WHY_FIRE);
  o = pbb_step(&s, pbb_ball(PBB_HOLD_MS - 1, 44.f, 6.f, 2.f, 1.f), t);
  CHECK(!o.release[PBB_LEFT]);
  o = pbb_step(&s, pbb_ball(PBB_HOLD_MS, 44.f, 6.f, 2.f, 1.f), t);
  CHECK_MSG(o.release[PBB_LEFT] && o.why[PBB_LEFT] == PBB_WHY_HELD, "a slow ball does not stretch a shot");
}

// pinbot.h: the plunger: pressed once with bAllowPlunger, idle and the ball at rest in the lane; held each frame;
// released after PBB_PLUNGE_HOLD_MS; not again before PBB_PLUNGE_REARM_MS; never on the user's own key.
static void tb_0498_pbb_plunger() {
  const PbbTable t = pbb_map02();
  PbbState s = {};
  PbbIn in = pbb_ball(100, 44.f, -21.f, 0.f, 0.f);
  in.allowPlunger = 1;
  PbbOut o = pbb_step(&s, in, t);
  CHECK_MSG(o.plunge == PBB_PLUNGE_NONE, "not idle (AttachUserAnim): no plunge");
  in.idle = 1;
  in.userBackward = 1;
  o = pbb_step(&s, in, t);
  CHECK_MSG(o.plunge == PBB_PLUNGE_NONE, "the user's own key: no plunge");
  in.userBackward = 0;
  o = pbb_step(&s, in, t);
  CHECK(o.plunge == PBB_PLUNGE_PRESS && s.plunging);
  CHECK_MSG(!o.press[PBB_LEFT] && !o.press[PBB_RIGHT], "no flip with the plunge");
  in.nowMs = 100 + PBB_PLUNGE_HOLD_MS - 1;
  o = pbb_step(&s, in, t);
  CHECK(o.plunge == PBB_PLUNGE_HOLD);
  in.nowMs = 100 + PBB_PLUNGE_HOLD_MS;
  o = pbb_step(&s, in, t);
  CHECK(o.plunge == PBB_PLUNGE_RELEASE && !s.plunging);
  in.nowMs += PBB_PLUNGE_REARM_MS - 1;
  o = pbb_step(&s, in, t);
  CHECK_MSG(o.plunge == PBB_PLUNGE_NONE, "not rearmed");
  in.nowMs += 1;
  o = pbb_step(&s, in, t);
  CHECK(o.plunge == PBB_PLUNGE_PRESS);
  PbbState s2 = {};
  PbbIn moving = pbb_ball(100, 44.f, -21.f, -300.f, 0.f);
  moving.allowPlunger = moving.idle = 1;
  CHECK_MSG(pbb_step(&s2, moving, t).plunge == PBB_PLUNGE_NONE, "a ball moving in the lane: no plunge");
}

// pinbot.h: any stop (switch off, panel, game over, the table left) releases what is down, once, then does
// nothing; presses and releases balance.
static void tb_0499_pbb_stop_releases() {
  const PbbTable t = pbb_map02();
  PbbState s = {};
  pbb_step(&s, pbb_ball(0, 46.f, 2.f, 40.f, 0.f), t);
  s.plunging = 1;   // as if a plunge were in flight too
  s.plungeMs = 0;
  PbbIn off = pbb_ball(10, 46.f, 2.f, 40.f, 0.f);
  off.active = 0;
  PbbOut o = pbb_step(&s, off, t);
  CHECK(o.release[PBB_LEFT] && o.release[PBB_RIGHT] && o.why[PBB_LEFT] == PBB_WHY_STOP &&
        o.plunge == PBB_PLUNGE_RELEASE);
  CHECK(!s.down[PBB_LEFT] && !s.down[PBB_RIGHT] && !s.plunging);
  o = pbb_step(&s, off, t);
  CHECK_MSG(!o.release[PBB_LEFT] && !o.release[PBB_RIGHT] && o.plunge == PBB_PLUNGE_NONE && !o.press[PBB_LEFT],
            "stopped: nothing more");
}

// pinbot.h: the player's own held button: that side is left to them.
static void tb_0500_pbb_yield_user() {
  const PbbTable t = pbb_map02();
  PbbState s = {};
  PbbIn in = pbb_ball(0, 46.f, 2.f, 40.f, 0.f);
  in.userHolds[PBB_LEFT] = 1;
  const PbbOut o = pbb_step(&s, in, t);
  CHECK(!o.press[PBB_LEFT] && o.press[PBB_RIGHT]);
}

// pinbot.h: a press is judged a hit when the ball turns up the table (vx < PBB_HIT_VX) inside PBB_JUDGE_MS, else a
// miss once that has passed.
static void tb_0501_pbb_outcome() {
  const PbbTable t = pbb_map02();
  PbbState s = {};
  pbb_step(&s, pbb_ball(0, 43.6f, 6.4f, 69.f, -9.f), t);
  PbbOut o = pbb_step(&s, pbb_ball(10, 44.f, 6.f, 20.f, 0.f), t);
  CHECK(o.outcome[PBB_LEFT] == PBB_OUT_NONE);
  o = pbb_step(&s, pbb_ball(28, 44.1f, 5.8f, -64.f, -44.f), t);
  CHECK(o.outcome[PBB_LEFT] == PBB_OUT_HIT && o.outcome[PBB_RIGHT] == PBB_OUT_NONE);
  PbbState s2 = {};
  pbb_step(&s2, pbb_ball(0, 45.6f, -2.7f, 14.f, 11.f), t);
  o = pbb_step(&s2, pbb_ball(PBB_JUDGE_MS - 1, 47.f, 0.f, 30.f, 20.f), t);
  CHECK(o.outcome[PBB_RIGHT] == PBB_OUT_NONE);
  o = pbb_step(&s2, pbb_ball(PBB_JUDGE_MS, 48.f, 1.f, 30.f, 20.f), t);
  CHECK(o.outcome[PBB_RIGHT] == PBB_OUT_MISS);
}

// pinbot.h and pbtrace.h: the bot row's detail is one CSV field; the summary's points a minute.
static void tb_0502_pbb_detail_rate() {
  char b[128];
  const int n = pbb_fmt_detail(b, sizeof b, "press", PBB_RIGHT, 45.6f, -2.7f, 14.f, 11.f);
  CHECK(n > 0 && strcmp(b, "press right pos 45.6 -2.7 vel 14 11") == 0 && !strchr(b, ','));
  CHECK(pbb_fmt_detail(b, 8, "press", PBB_LEFT, 0.f, 0.f, 0.f, 0.f) == 0);
  CHECK(pbb_fmt_detail(b, sizeof b, "plunge", -1, 44.f, -21.f, 0.f, 0.f) > 0 &&
        strcmp(b, "plunge plunger pos 44.0 -21.0 vel 0 0") == 0);
  CHECK(fabsf(pb_per_minute(25850, 61.4) - 25260.6f) < 1.f && pb_per_minute(100, 0.0) == 0.f);
}

// autowin.h and config: Auto-win minigames leaves pinball to the autoplay while it is on; the switch is off by
// default under [autowin].
static void tb_0503_pbb_autowin_and_config() {
  CHECK(aw_pinball_awards(1, 0) && !aw_pinball_awards(1, 1) && !aw_pinball_awards(0, 0) && !aw_pinball_awards(0, 1));
  config_defaults();
  CHECK(g_cfg.pinballAuto == 0 && config_key_known("autowin", "pinball"));
}

// pinbot.h: the player presses a side the bot holds: the bot lets go of it at once without a release (that would
// clear the player's own flag) and leaves it to them until they release it.
static void tb_0504_pbb_takeover_side() {
  const PbbTable t = pbb_map02();
  PbbState s = {};
  PbbOut o = pbb_step(&s, pbb_ball(0, 43.6f, 6.4f, 69.f, -9.f), t);
  CHECK(o.press[PBB_LEFT]);
  PbbIn in = pbb_ball(20, 44.f, 6.f, 60.f, 0.f);
  in.userPressed[PBB_LEFT] = 1;
  o = pbb_step(&s, in, t);
  CHECK_MSG(!o.release[PBB_LEFT] && o.why[PBB_LEFT] == PBB_WHY_TAKEN && !s.down[PBB_LEFT], "taken: no release");
  in = pbb_ball(PBB_HOLD_MS + 50, 44.f, 6.f, 60.f, 0.f);
  in.userHolds[PBB_LEFT] = 1;
  o = pbb_step(&s, in, t);
  CHECK_MSG(!o.press[PBB_LEFT] && !o.release[PBB_LEFT], "the player's until released: no timed release, no press");
  o = pbb_step(&s, pbb_ball(PBB_HOLD_MS + 60, 44.f, 6.f, 60.f, 0.f), t);
  CHECK_MSG(o.press[PBB_LEFT], "released by the player: the bot plays it again");
}

// pinbot.h: the plunger: the player's own backward key during a plunge, or the table left, drops the press without
// a release call (the real key's release would go nowhere); a stop while attached still releases.
static void tb_0505_pbb_plunge_drop() {
  const PbbTable t = pbb_map02();
  PbbState s = {};
  PbbIn in = pbb_ball(0, 44.f, -21.f, 0.f, 0.f);
  in.allowPlunger = in.idle = 1;
  CHECK(pbb_step(&s, in, t).plunge == PBB_PLUNGE_PRESS);
  in.nowMs = 50;
  in.userBackward = 1;
  PbbOut o = pbb_step(&s, in, t);
  CHECK(o.plunge == PBB_PLUNGE_DROP && !s.plunging);
  PbbState s2 = {};
  in.nowMs = 0;
  in.userBackward = 0;
  pbb_step(&s2, in, t);
  PbbIn gone = pbb_ball(50, 44.f, -21.f, 0.f, 0.f);
  gone.active = 0;
  gone.attached = 0;
  o = pbb_step(&s2, gone, t);
  CHECK_MSG(o.plunge == PBB_PLUNGE_DROP && !s2.plunging, "the table left: dropped, not released");
}

// pbtrace.h: the trace file starts over (one .old kept) once a write would take it past PB_FILE_CAP.
static void tb_0506_pb_file_cap() {
  CHECK(!pb_rotate_due(0, 1000, PB_FILE_CAP) && !pb_rotate_due(PB_FILE_CAP - 1000, 1000, PB_FILE_CAP));
  CHECK(pb_rotate_due(PB_FILE_CAP - 1000, 1001, PB_FILE_CAP) && pb_rotate_due(PB_FILE_CAP + 5, 1, PB_FILE_CAP));
  CHECK_MSG(!pb_rotate_due(0, PB_FILE_CAP + 1, PB_FILE_CAP), "an empty file is never rotated");
  CHECK(PB_FILE_CAP == 50u * 1024u * 1024u);
}

// pinbot.h: shots only where the traces scored (minigame-autoplay.md section 7, the shot table): a slow ball at along
// -1 is not fired (the bot's weak shots), the window fires, a fast ball's first touch at the inlane mouth waits for
// its predicted contact to reach the window, a ball past the window still fires before the tip (drain safety).
static void tb_0507_pbb_window_choice() {
  const PbbTable t = pbb_map02();
  const PbbFlipper& l0 = t.f[PBB_LEFT][0];
  CHECK_MSG(pbb_window_of(-1.f) == PBB_WIN_EARLY && pbb_window_of(0.f) == PBB_WIN_SHOT &&
            pbb_window_of(PBB_WIN_MAX) == PBB_WIN_SHOT && pbb_window_of(5.f) == PBB_WIN_LATE &&
            pbb_window_of(6.5f) == PBB_WIN_PAST, "the windows along the flipper");
  // (44.24, 8.15): along -1.0, perp 2.2 on the left flipper, as the bot's f2396.
  float a, h;
  pbb_flip_frame(l0, 44.24f, 8.15f, &a, &h);
  CHECK(fabsf(a + 1.f) < 0.15f && fabsf(h - 2.2f) < 0.2f);
  PbbState s = {};
  PbbOut o = pbb_step(&s, pbb_ball(1000, 44.24f, 8.15f, 5.f, -6.f), t);
  CHECK_MSG(!o.press[PBB_LEFT], "slow at along -1: not a shot yet (nor a cradle: it is below the crook)");
  // The same ball rolled on to along ~2.7 (the lower left flipper's aim, test 0511).
  o = pbb_step(&s, pbb_ball(1100, 46.85f, 5.53f, 6.f, -6.f), t);
  CHECK(o.press[PBB_LEFT] && o.why[PBB_LEFT] == PBB_WHY_FIRE && o.along[PBB_LEFT] >= 2.5f && o.along[PBB_LEFT] < 3.5f);
  // A fast ball touching at the inlane mouth (along -3.1, perp 2): its contact 16 ms on is still before the window.
  PbbState s2 = {};
  o = pbb_step(&s2, pbb_ball(0, 42.8f, 9.9f, 30.f, -10.f), t);
  CHECK_MSG(!o.press[PBB_LEFT], "first touch at the mouth: no shot");
  o = pbb_step(&s2, pbb_ball(60, 44.6f, 7.9f, 40.f, -30.f), t);
  CHECK_MSG(o.press[PBB_LEFT] && o.why[PBB_LEFT] == PBB_WHY_FIRE, "predicted into the window: shot");
  // Past the window, short of the tip: fired anyway.
  PbbState s3 = {};
  o = pbb_step(&s3, pbb_ball(0, 48.77f, 3.91f, 30.f, -10.f), t);   // along 5.2
  CHECK_MSG(o.press[PBB_LEFT] && o.why[PBB_LEFT] == PBB_WHY_SAFETY, "drain safety before the tip");
}

// pinbot.h: the cradle. A ball coming down at PBB_TRAP_V or slower toward the crook raises the flipper early; it is
// held until the ball has been still (< PBB_SETTLE_V) for PBB_SETTLE_MS, then released; the ball rolls down and is
// fired in the window, never cradled again on the way.
static void tb_0508_pbb_cradle_states() {
  const PbbTable t = pbb_map02();
  PbbState s = {};
  PbbOut o = pbb_step(&s, pbb_ball(0, 42.8f, 9.9f, 8.f, -6.f), t);   // the inlane mouth, 10 u/s
  CHECK(o.press[PBB_LEFT] && o.why[PBB_LEFT] == PBB_WHY_CRADLE && s.side[PBB_LEFT] == PBB_SIDE_CRADLE);
  o = pbb_step(&s, pbb_ball(100, 43.5f, 8.5f, 4.f, -3.f), t);
  CHECK_MSG(!o.release[PBB_LEFT], "still moving: held");
  o = pbb_step(&s, pbb_ball(200, 43.6f, 8.4f, 1.f, 0.f), t);   // still from here
  o = pbb_step(&s, pbb_ball(200 + PBB_SETTLE_MS - 1, 43.6f, 8.4f, 0.5f, 0.f), t);
  CHECK_MSG(!o.release[PBB_LEFT], "not settled long enough");
  o = pbb_step(&s, pbb_ball(200 + PBB_SETTLE_MS, 43.6f, 8.4f, 0.f, 0.f), t);
  CHECK(o.release[PBB_LEFT] && o.why[PBB_LEFT] == PBB_WHY_SETTLED && s.side[PBB_LEFT] == PBB_SIDE_ROLL);
  const unsigned t0 = 200 + PBB_SETTLE_MS;
  o = pbb_step(&s, pbb_ball(t0 + 100, 44.2f, 7.6f, 6.f, -6.f), t);
  CHECK_MSG(!o.press[PBB_LEFT], "rolling down, slow, before the window: no second cradle, no shot");
  o = pbb_step(&s, pbb_ball(t0 + 300, 46.6f, 5.6f, 8.f, -8.f), t);
  CHECK(o.press[PBB_LEFT] && o.why[PBB_LEFT] == PBB_WHY_FIRE && s.side[PBB_LEFT] == PBB_SIDE_SHOT);
  o = pbb_step(&s, pbb_ball(t0 + 300 + PBB_HOLD_MS, 30.f, 5.f, -150.f, 0.f), t);
  CHECK(o.release[PBB_LEFT] && s.side[PBB_LEFT] == PBB_SIDE_DOWN);
}

// pinbot.h: a cradle never lasts past PBB_CRADLE_MAX_MS (3 s), and ends when the ball leaves the zone.
static void tb_0509_pbb_cradle_ends() {
  const PbbTable t = pbb_map02();
  PbbState s = {};
  pbb_step(&s, pbb_ball(0, 42.8f, 9.9f, 8.f, -6.f), t);
  PbbOut o = pbb_step(&s, pbb_ball(PBB_CRADLE_MAX_MS - 1, 43.5f, 8.5f, 5.f, 2.f), t);   // jiggling, never still
  CHECK(!o.release[PBB_LEFT]);
  o = pbb_step(&s, pbb_ball(PBB_CRADLE_MAX_MS, 43.5f, 8.5f, 5.f, 2.f), t);
  CHECK(o.release[PBB_LEFT] && o.why[PBB_LEFT] == PBB_WHY_CRADLE_END && PBB_CRADLE_MAX_MS == 3000);
  PbbState s2 = {};
  pbb_step(&s2, pbb_ball(0, 42.8f, 9.9f, 8.f, -6.f), t);
  o = pbb_step(&s2, pbb_ball(50, 30.f, 9.f, -90.f, 0.f), t);
  CHECK_MSG(o.release[PBB_LEFT] && o.why[PBB_LEFT] == PBB_WHY_CRADLE_LOST, "bounced away: let go");
  CHECK_MSG(s2.side[PBB_LEFT] == PBB_SIDE_DOWN, "a ball that left is not rolling down: the cradle can come again");
}

// pinbot.h: the per-bin tally of points a shot (the shot table's bins): a shot closes at the next press, a drain or
// 3 s; a second side pressed in the same ms joins the open shot.
static void tb_0510_pbb_tally() {
  CHECK(pbb_tally_bin(-3.2f) == 0 && pbb_tally_bin(-1.f) == 1 && pbb_tally_bin(0.f) == 2 && pbb_tally_bin(1.f) == 3 &&
        pbb_tally_bin(2.f) == 4 && pbb_tally_bin(3.5f) == 5 && pbb_tally_bin(6.5f) == 6 && PBB_BINS == 7);
  PbbTally t = {};
  pbb_tally_press(&t, 2, 1000, 0);
  pbb_tally_frame(&t, 1500, 2000, 0);
  CHECK_MSG(t.open, "still open inside 3 s");
  pbb_tally_frame(&t, 1750, PBB_TALLY_MS, 0);
  CHECK(!t.open && t.shots[2] == 1 && t.points[2] == 750 && PBB_TALLY_MS == 3000);
  pbb_tally_press(&t, 1, 2000, 5000);
  pbb_tally_frame(&t, 2000, 5100, 1);
  CHECK_MSG(!t.open && t.shots[1] == 1 && t.points[1] == 0 && t.drained[1] == 1, "a drain closes it");
  pbb_tally_press(&t, 5, 2000, 6000);
  pbb_tally_press(&t, 3, 2300, 6100);
  CHECK_MSG(t.shots[5] == 1 && t.points[5] == 300 && t.open && t.bin == 3, "the next press closes it");
  pbb_tally_press(&t, 4, 2300, 6100);
  CHECK_MSG(t.bin == 3 && t.shots[4] == 0, "a second side in the same ms joins the shot");
}

// pinbot.h: the lower left flipper's slow ball waits for along PBB_AIM_LEFT (its drop shot at -0.3 left at +19 deg,
// scored ~250 a shot and was followed by every left outlane drain); the right keeps its drop shot at -0.4 (-18 deg,
// ~10k a shot, no drain); fast balls and the upper left flipper keep the window from 0.
static void tb_0511_pbb_left_aim() {
  const PbbTable t = pbb_map02();
  // Left lower flipper, along -0.3 perp 2.7, rolling at 25 u/s along it (the drop shot's own numbers).
  PbbState s = {};
  PbbOut o = pbb_step(&s, pbb_ball(0, 44.38f, 7.27f, 17.7f, -17.7f), t);
  CHECK_MSG(!o.press[PBB_LEFT], "the left drop shot at -0.3 waits");
  o = pbb_step(&s, pbb_ball(30, 45.9f, 5.0f, 20.f, -20.f), t);   // along ~2.4, predicted ~2.8
  CHECK(o.press[PBB_LEFT] && o.why[PBB_LEFT] == PBB_WHY_FIRE && o.along[PBB_LEFT] >= PBB_AIM_LEFT);
  // A fast ball at along 0.5 on the left still fires at once.
  PbbState s2 = {};
  o = pbb_step(&s2, pbb_ball(0, 45.3f, 7.09f, 60.f, -40.f), t);
  CHECK_MSG(o.press[PBB_LEFT], "fast: the window from 0");
  // The right drop shot: along -0.4, perp 2.5, 27 u/s.
  PbbState s3 = {};
  o = pbb_step(&s3, pbb_ball(0, 44.45f, -3.51f, 19.f, 19.f), t);
  CHECK_MSG(o.press[PBB_RIGHT], "the right drop shot is kept");
  // The upper left flipper (16, 18): a slow ball at along 0.4 fires, the aim is the lower flipper's.
  PbbState s4 = {};
  o = pbb_step(&s4, pbb_ball(0, 14.f, 15.5f, 15.f, -15.f), t);
  CHECK(o.press[PBB_LEFT]);
}

// pbtrace.h: a lost ball's cause from its last place before the drain (outside a lower flipper's pivot: that
// outlane; between: the centre gap, then held, a flip within 500 ms, or unhit) and a tilt in the last 2 s.
static void tb_0512_pb_drain_cause() {
  CHECK(pb_drain_cause(10.6f, 9.f, -5.f, 0, 0, 1500) == PB_CAUSE_LEFT_OUTLANE);
  CHECK(pb_drain_cause(-6.6f, 9.f, -5.f, 0, 0, 4000) == PB_CAUSE_RIGHT_OUTLANE);
  CHECK(pb_drain_cause(0.6f, 9.f, -5.f, 1, 0, 0xFFFFFFFFu) == PB_CAUSE_TILT);
  CHECK(pb_drain_cause(0.3f, 9.f, -5.f, 0, 0, 0xFFFFFFFFu) == PB_CAUSE_CENTRE_UNHIT);
  CHECK(pb_drain_cause(0.3f, 9.f, -5.f, 0, 0, 400) == PB_CAUSE_CENTRE_FLIP);
  CHECK(pb_drain_cause(0.3f, 9.f, -5.f, 0, 1, 400) == PB_CAUSE_CENTRE_HELD);
  CHECK(strcmp(pb_cause_name(PB_CAUSE_LEFT_OUTLANE), "left outlane") == 0 && pb_cause_name(99)[0] == '?');
}

// achhelp.h: a row seen locked that turns earned in play drops a switch left on back to auto,
// so its markers stop; off stays off; a row already earned when first answered keeps its mode.
static void tb_0522_ah_mode_after_earned() {
  CHECK(ah_mode_after_steam(AH_MODE_ON, 1, 1) == AH_MODE_AUTO);
  CHECK(ah_mode_after_steam(AH_MODE_OFF, 1, 1) == AH_MODE_OFF && ah_mode_after_steam(AH_MODE_AUTO, 1, 1) == AH_MODE_AUTO);
  CHECK_MSG(ah_mode_after_steam(AH_MODE_ON, 0, 1) == AH_MODE_ON, "earned before it was seen locked: kept on");
  CHECK_MSG(ah_mode_after_steam(AH_MODE_ON, 1, 0) == AH_MODE_ON, "still locked: kept on");
}

// pinbot.h: a takeover clears the side's judging (a later hit is the player's, not the bot's), during a shot and
// during a cradle; a cradle ends on a stop (released) and when the ball is not read (let go as lost).
static void tb_0513_pbb_takeover_judging_cradle_ends() {
  const PbbTable t = pbb_map02();
  PbbState s = {};
  pbb_step(&s, pbb_ball(0, 43.6f, 6.4f, 69.f, -9.f), t);   // a shot: judging
  PbbIn in = pbb_ball(10, 44.f, 6.f, 20.f, 0.f);
  in.userPressed[PBB_LEFT] = 1;
  PbbOut o = pbb_step(&s, in, t);
  CHECK(o.why[PBB_LEFT] == PBB_WHY_TAKEN && !o.release[PBB_LEFT] && !s.judging[PBB_LEFT]);
  o = pbb_step(&s, pbb_ball(30, 44.f, 6.f, -90.f, 0.f), t);
  CHECK_MSG(o.outcome[PBB_LEFT] == PBB_OUT_NONE, "the player's hit is not the bot's");
  // Taken during a cradle.
  PbbState c = {};
  o = pbb_step(&c, pbb_ball(0, 42.8f, 9.9f, 8.f, -6.f), t);
  CHECK(o.why[PBB_LEFT] == PBB_WHY_CRADLE);
  in = pbb_ball(50, 43.5f, 8.5f, 4.f, -3.f);
  in.userPressed[PBB_LEFT] = 1;
  o = pbb_step(&c, in, t);
  CHECK(o.why[PBB_LEFT] == PBB_WHY_TAKEN && !o.release[PBB_LEFT] && c.side[PBB_LEFT] == PBB_SIDE_DOWN &&
        !c.down[PBB_LEFT]);
  // A cradle ends on a stop.
  PbbState c2 = {};
  pbb_step(&c2, pbb_ball(0, 42.8f, 9.9f, 8.f, -6.f), t);
  PbbIn off = pbb_ball(50, 43.5f, 8.5f, 4.f, -3.f);
  off.active = 0;
  o = pbb_step(&c2, off, t);
  CHECK(o.release[PBB_LEFT] && o.why[PBB_LEFT] == PBB_WHY_STOP && c2.side[PBB_LEFT] == PBB_SIDE_DOWN);
  // A cradle ends when the ball is not read.
  PbbState c3 = {};
  pbb_step(&c3, pbb_ball(0, 42.8f, 9.9f, 8.f, -6.f), t);
  PbbIn blind = pbb_ball(50, 43.5f, 8.5f, 4.f, -3.f);
  blind.haveBall = 0;
  o = pbb_step(&c3, blind, t);
  CHECK(o.release[PBB_LEFT] && o.why[PBB_LEFT] == PBB_WHY_CRADLE_LOST && c3.side[PBB_LEFT] == PBB_SIDE_DOWN);
}

// pbtrace.h: a failed rename never truncates: the trace opens a new uniquely named file instead; a session's
// header rows are kept whole (all or nothing) for a rotated file.
static void tb_0514_pb_open_choice_and_header() {
  CHECK(pb_open_choice(0, 0) == PB_OPEN_MAIN && pb_open_choice(1, 1) == PB_OPEN_MAIN);
  CHECK_MSG(pb_open_choice(1, 0) == PB_OPEN_UNIQUE, "a file that could not be renamed is never truncated");
  wchar_t name[64];
  CHECK(pb_unique_name(name, 64, L"x\\", 1234u) && wcscmp(name, L"x\\pinball-trace-1234.csv") == 0);
  CHECK(!pb_unique_name(name, 10, L"x\\", 1234u));
  char hdr[16];
  int used = 0;
  CHECK(pb_hdr_append(hdr, sizeof hdr, &used, "H,a\n", 4) && used == 4);
  CHECK(pb_hdr_append(hdr, sizeof hdr, &used, "H,bbbbbb\n", 9) && used == 13 && memcmp(hdr, "H,a\nH,bbbbbb\n", 13) == 0);
  CHECK_MSG(!pb_hdr_append(hdr, sizeof hdr, &used, "H,cccc\n", 7) && used == 13, "a row that does not fit: none of it");
}

// pinbot.h: the bot's clock is the game's (LevelInfo.TimeSeconds in ms), so at 300% game speed a hold is still 200 ms
// of game time; the lookahead is at least the last frame's game time (one frame early, never late); a paused game
// clock changes no press.
static void tb_0515_pbb_game_clock() {
  CHECK(pbb_game_ms(1.5f) == 1500u && pbb_game_ms(0.f) == 0u && pbb_game_ms(-1.f) == 0u);
  CHECK_MSG(pbb_game_ms(1000.0005f) >= 1000000u && pbb_game_ms(1000.0005f) <= 1000001u, "float seconds to whole ms");
  CHECK(pbb_lead_ms(5) == PBB_LEAD_MS && pbb_lead_ms(48) == 48u && pbb_lead_ms(0) == PBB_LEAD_MS);
  CHECK_MSG(pbb_lead_ms(5000) == PBB_LEAD_MAX_MS, "a hitch does not throw the prediction across the table");
  const PbbTable t = pbb_map02();
  // At 3x a frame is ~17 ms of game time... the ball 2 frames' worth before the right zone: 16 ms is short of it,
  // one 48 ms frame reaches it.
  PbbState s = {};
  PbbIn in = pbb_ball(1000, 36.f, -0.5f, 150.f, 0.f);   // predicted x 38.4 at 16 ms, 43.2 at 48 ms
  in.frameMs = 5;
  CHECK_MSG(!pbb_step(&s, in, t).press[PBB_RIGHT], "16 ms ahead: not yet");
  PbbState s2 = {};
  in.frameMs = 48;
  CHECK_MSG(pbb_step(&s2, in, t).press[PBB_RIGHT], "one 48 ms frame ahead: fired a frame early");
  // A paused clock: no press, no release, whatever the times say.
  PbbState s3 = {};
  PbbIn p = pbb_ball(0, 43.6f, 6.4f, 69.f, -9.f);
  CHECK(pbb_step(&s3, p, t).press[PBB_LEFT]);
  p = pbb_ball(PBB_HOLD_MS + 500, 44.f, 6.f, 0.f, 0.f);
  p.paused = 1;
  PbbOut o = pbb_step(&s3, p, t);
  CHECK_MSG(!o.release[PBB_LEFT] && s3.down[PBB_LEFT], "paused: the shot stays held");
  PbbState s4 = {};
  PbbIn q = pbb_ball(0, 43.6f, 6.4f, 69.f, -9.f);
  q.paused = 1;
  CHECK_MSG(!pbb_step(&s4, q, t).press[PBB_LEFT], "paused: no press");
  p.paused = 0;
  CHECK_MSG(pbb_step(&s3, p, t).release[PBB_LEFT], "unpaused: the hold's game time has passed");
}

// pbtrace.h: the summary's game speed (game time over wall time) and the game-time frame deltas.
static void tb_0516_pb_speed_stats() {
  CHECK(fabsf(pb_speed_ratio(183.f, 61.f) - 3.f) < 1e-4f && pb_speed_ratio(5.f, 0.f) == 0.f);
  PbDtStats d = {};
  pb_dt_add(&d, 5.f);
  pb_dt_add(&d, 17.f);
  pb_dt_add(&d, 0.f);   // a frame with no tick: not counted
  CHECK(d.n == 2 && fabsf(pb_dt_mean(d) - 11.f) < 1e-4f && fabsf(d.max - 17.f) < 1e-4f);
  PbDtStats e = {};
  CHECK(pb_dt_mean(e) == 0.f);
}

// The shotgun's per-shell reload is interrupted by a fire input (Shotgun.Reloading calls AttemptFire first), so the
// triggerbot waits while a reload has shells to load; whole-clip reloads (not interruptible) and unread values are left.
static void tb_0520_tbot_reload_wait() {
  CHECK_MSG(tbot_reload_wait(TBOT_WSTATE_RELOAD, 3, 7, 1, 20.f), "shotgun mid-reload, reserve left: wait");
  CHECK_MSG(tbot_reload_wait(TBOT_WSTATE_RELOAD_START, 0, 7, 1, 5.f) && tbot_reload_wait(TBOT_WSTATE_RELOAD_STOP, 6, 7, 1, 9.f),
            "start, loop and stop all wait");
  CHECK_MSG(!tbot_reload_wait(TBOT_WSTATE_RELOAD, 7, 7, 1, 20.f), "clip full: fire");
  CHECK_MSG(!tbot_reload_wait(TBOT_WSTATE_RELOAD, 3, 7, 1, 3.f), "reserve empty (Charge == loaded): fire");
  CHECK_MSG(!tbot_reload_wait(3, 3, 7, 1, 20.f) && !tbot_reload_wait(4, 3, 7, 1, 20.f), "not in a reload state: fire");
  CHECK_MSG(!tbot_reload_wait(TBOT_WSTATE_RELOAD, 3, 8, 8, 20.f), "pistol reload takes the whole clip and cannot be interrupted");
  CHECK_MSG(!tbot_reload_wait(TBOT_WSTATE_RELOAD, 3, 0, 0, 20.f) && !tbot_reload_wait(TBOT_WSTATE_RELOAD, -1, 7, 1, 20.f) &&
            !tbot_reload_wait(-1, 3, 7, 1, 20.f) && !tbot_reload_wait(TBOT_WSTATE_RELOAD, 3, 7, -1, 20.f),
            "no clip, or any value unread: never held");
}

// Splash: Duke at the origin (radius 21, half-height 45); one candidate point set at a time.
static SplashPts spl_pts(int src, float x, float y, float z) {
  SplashPts p;
  memset(&p, 0, sizeof p);
  p.ok[src] = 1;
  p.p[src][0] = x;
  p.p[src][1] = y;
  p.p[src][2] = z;
  return p;
}
static void tb_0530_splash_close_holds_far_fires() {
  const float duke[3] = { 0.f, 0.f, 0.f };
  const int k = splash_kind(1, 0, 0, 1, 260.f);   // the RPG's rocket
  CHECK_MSG(k == SPL_READ && splash_radius(k, 260.f) == 260.f, "a rocket with DamageRadius read is explosive");
  const SplashVerdict cl = splash_judge(k, 260.f, duke, 21.f, 45.f, spl_pts(SPL_PT_TARGET, 250.f, 0.f, 0.f));
  CHECK_MSG(cl.hold && cl.src == SPL_PT_TARGET, "the target 250 units away: its blast reaches Duke, fire held");
  CHECK_MSG(fabsf(cl.dist - 229.f) < 0.5f && fabsf(cl.reach - 286.f) < 0.5f,
            "distance to Duke's cylinder, reach 1.1 x DamageRadius");
  CHECK_MSG(!splash_judge(k, 260.f, duke, 21.f, 45.f, spl_pts(SPL_PT_TARGET, 800.f, 0.f, 0.f)).hold,
            "the target 800 units away: fires");
  // Below his feet the cylinder's height counts, not his radius (the Devastator's 96).
  CHECK_MSG(splash_judge(k, 96.f, duke, 21.f, 45.f, spl_pts(SPL_PT_TARGET, 0.f, 0.f, -150.f)).hold &&
            !splash_judge(k, 96.f, duke, 21.f, 45.f, spl_pts(SPL_PT_TARGET, 0.f, 0.f, -160.f)).hold,
            "a point below Duke is measured from his feet");
  CHECK_MSG(splash_judge(k, 96.f, duke, 0.f, -1.f, spl_pts(SPL_PT_TARGET, 0.f, 0.f, -150.f)).hold,
            "Duke's collision unread: 21 and 45");
}
static void tb_0531_splash_blocker_nearer_holds() {
  const float duke[3] = { 0.f, 0.f, 0.f };
  SplashPts p = spl_pts(SPL_PT_TARGET, 800.f, 0.f, 0.f);
  p.ok[SPL_PT_BLOCKER] = 1;
  p.p[SPL_PT_BLOCKER][0] = 200.f;
  const SplashVerdict v = splash_judge(SPL_READ, 260.f, duke, 21.f, 45.f, p);
  CHECK_MSG(v.hold && v.src == SPL_PT_BLOCKER, "a wall 200 units ahead of a far target: held, by the blocker");
  p.p[SPL_PT_BLOCKER][0] = 600.f;
  CHECK_MSG(!splash_judge(SPL_READ, 260.f, duke, 21.f, 45.f, p).hold, "the blocker out of reach: fires");
  p.ok[SPL_PT_ARC] = 1;
  p.p[SPL_PT_ARC][0] = 100.f;
  p.p[SPL_PT_ARC][2] = -60.f;
  const SplashVerdict a = splash_judge(SPL_READ, 220.f, duke, 21.f, 45.f, p);
  CHECK_MSG(a.hold && a.src == SPL_PT_ARC, "the grenade's arc lands near Duke: held, by the arc");
}
static void tb_0532_splash_hitscan_unaffected() {
  const float duke[3] = { 0.f, 0.f, 0.f };
  CHECK_MSG(splash_kind(0, -1, 0, 1, 260.f) == SPL_NONE && splash_kind(0, -1, 0, 0, 0.f) == SPL_NONE,
            "a hitscan gun has no splash, whatever is read");
  CHECK_MSG(!splash_judge(SPL_NONE, 0.f, duke, 21.f, 45.f, spl_pts(SPL_PT_TARGET, 30.f, 0.f, 0.f)).hold,
            "a hitscan shot at point-blank range is never held");
  CHECK_MSG(splash_kind(1, 1, 0, 1, 5.f) == SPL_NONE, "bDamageDirectly (the AT laser): no splash");
  CHECK_MSG(splash_kind(1, 0, 1, 1, 64.f) == SPL_NONE, "the shrink blast spares its Instigator: no splash");
  CHECK_MSG(splash_kind(1, 0, 0, 1, 0.f) == SPL_NONE, "DamageRadius read as 0: HurtRadius reaches nobody");
  CHECK_MSG(splash_kind(1, -1, 0, 1, 130.f) == SPL_READ, "bDamageDirectly unread: the blast is assumed");
}
static void tb_0533_splash_unread_radius_default() {
  const float duke[3] = { 0.f, 0.f, 0.f };
  const int k = splash_kind(1, 0, 0, 0, 0.f);
  CHECK_MSG(k == SPL_DEFAULT && splash_radius(k, 0.f) == SPLASH_DEFAULT_RADIUS, "DamageRadius unread: the default 400");
  CHECK_MSG(splash_kind(1, 0, 0, 1, sqrtf(-1.f)) == SPL_DEFAULT, "a NaN radius is unread");
  const float r = splash_radius(k, 0.f);
  CHECK_MSG(splash_judge(k, r, duke, 21.f, 45.f, spl_pts(SPL_PT_TARGET, 400.f, 0.f, 0.f)).hold,
            "400 units away under the default: held");
  CHECK_MSG(!splash_judge(k, r, duke, 21.f, 45.f, spl_pts(SPL_PT_TARGET, 500.f, 0.f, 0.f)).hold,
            "500 units away (479 from his cylinder, reach 440): fires");
  SplashPts none;
  memset(&none, 0, sizeof none);
  CHECK_MSG(!splash_judge(k, r, duke, 21.f, 45.f, none).hold, "no point to judge: nothing held");
}

// aim.h boss priority: the verdict is the first failing test in the pick's order; every one passing is ON.
static void tb_0535_boss_verdict() {
  AimBossTests t = aim_boss_tests_pass();
  CHECK_MSG(aim_boss_verdict(t) == AIM_BOSS_ON, "every test passing: the boss is possible");
  t.found = 0;
  CHECK_MSG(aim_boss_verdict(t) == AIM_BOSS_NONE, "no boss meter: no priority");
  t = aim_boss_tests_pass();
  t.vis = AIM_VIS_NO;
  CHECK_MSG(aim_boss_verdict(t) == AIM_BOSS_HIDDEN, "hidden");
  t.vis = AIM_VIS_UNKNOWN;
  CHECK_MSG(aim_boss_verdict(t) == AIM_BOSS_UNKNOWN, "visibility not known: not possible");
  t = aim_boss_tests_pass();
  t.excl = AIM_BOSS_INVULNERABLE;
  CHECK_MSG(aim_boss_verdict(t) == AIM_BOSS_INVULNERABLE, "invulnerable");
  t.excl = AIM_BOSS_WRONGTYPE;
  CHECK_MSG(aim_boss_verdict(t) == AIM_BOSS_WRONGTYPE, "the held gun's damage type");
  t = aim_boss_tests_pass();
  t.eligible = 0;
  t.vis = AIM_VIS_NO;
  CHECK_MSG(aim_boss_verdict(t) == AIM_BOSS_RANGE, "out of range: the first failure wins");
  t = aim_boss_tests_pass();
  t.alive = 0;
  t.eligible = 0;
  CHECK_MSG(aim_boss_verdict(t) == AIM_BOSS_DEAD, "dead (a knockdown): not possible");
  t = aim_boss_tests_pass();
  t.cone = 0;
  CHECK_MSG(aim_boss_verdict(t) == AIM_BOSS_CONE, "outside the cone");
  t = aim_boss_tests_pass();
  t.reach = 0;
  CHECK_MSG(aim_boss_verdict(t) == AIM_BOSS_REACH, "out of the projectile's reach");
}
// aim.h boss priority: a boss the splash guard would hold fire on is not possible; judged without
// the cone, the boss must be on screen whatever the Off-screen setting.
static void tb_0541_boss_blast_and_screen() {
  AimBossTests t = aim_boss_tests_pass();
  t.safe = 0;
  CHECK_MSG(aim_boss_verdict(t) == AIM_BOSS_BLAST, "visible but the blast would reach Duke: not possible");
  t.vis = AIM_VIS_NO;
  CHECK_MSG(aim_boss_verdict(t) == AIM_BOSS_HIDDEN, "hidden comes first");
  CHECK_MSG(aim_boss_offscreen(0, 1) == 0 && aim_boss_offscreen(0, 0) == 0, "no cone: on screen required");
  CHECK_MSG(aim_boss_offscreen(1, 1) == 1 && aim_boss_offscreen(1, 0) == 0, "with the cone: the setting as the pick's");
  CHECK_MSG(!aim_eligible(1, 0, 100.f, 1000.f, 0, aim_boss_offscreen(0, 1)), "a boss behind Duke: not eligible");
}
// aim.h boss priority: the pick takes the boss while it is possible, the adds otherwise, back at once.
static void tb_0536_boss_pick() {
  // Walk index 3 an add nearer the crosshair (key 2), 7 the boss (key 9), both visible.
  const AimCand base[2] = { { 3, 2.f, AIM_VIS_YES }, { 7, 9.f, AIM_VIS_YES } };
  AimCand c[2];
  memcpy(c, base, sizeof c);
  CHECK_MSG(aim_boss_pick(c, 2, -1, AIM_PICK_CROSSHAIR, 7, AIM_BOSS_ON) == 7, "boss possible: the boss, not the add");
  memcpy(c, base, sizeof c);
  CHECK_MSG(aim_boss_pick(c, 2, 3, AIM_PICK_NEAREST, 7, AIM_BOSS_ON) == 7, "possible again: back at once, no hysteresis");
  memcpy(c, base, sizeof c);
  CHECK_MSG(aim_boss_pick(c, 0, -1, AIM_PICK_CROSSHAIR, 7, AIM_BOSS_ON) == 7, "possible though no candidate list");
  static const int OFF[4] = { AIM_BOSS_HIDDEN, AIM_BOSS_INVULNERABLE, AIM_BOSS_WRONGTYPE, AIM_BOSS_RANGE };
  for (int k = 0; k < 4; k++) {
    memcpy(c, base, sizeof c);
    CHECK_MSG(aim_boss_pick(c, 2, 7, AIM_PICK_CROSSHAIR, 7, OFF[k]) == 3, "boss impossible: the add, the boss not kept");
  }
  // The boss first in key order but impossible: its entry is out, the add is taken.
  const AimCand first[2] = { { 7, 1.f, AIM_VIS_YES }, { 3, 2.f, AIM_VIS_YES } };
  memcpy(c, first, sizeof c);
  CHECK_MSG(aim_boss_pick(c, 2, -1, AIM_PICK_CROSSHAIR, 7, AIM_BOSS_HIDDEN) == 3, "impossible boss nearest: the add");
  memcpy(c, first, sizeof c);
  CHECK_MSG(aim_boss_pick(c, 2, -1, AIM_PICK_CROSSHAIR, 7, AIM_BOSS_ON) == 7, "possible: the boss");
  memcpy(c, first, sizeof c);
  CHECK_MSG(aim_boss_pick(c, 2, -1, AIM_PICK_CROSSHAIR, -1, AIM_BOSS_NONE) == 7, "no boss meter: the pick as before");
  const AimCand close[2] = { { 3, 2.f, AIM_VIS_YES }, { 7, 2.5f, AIM_VIS_YES } };
  memcpy(c, close, sizeof c);
  CHECK_MSG(aim_boss_pick(c, 2, 7, AIM_PICK_CROSSHAIR, -1, AIM_BOSS_NONE) == 7,
            "no boss meter: hysteresis keeps the previous target as before");
  memcpy(c, close, sizeof c);
  CHECK_MSG(aim_boss_pick(c, 2, 7, AIM_PICK_CROSSHAIR, 7, AIM_BOSS_CONE) == 3,
            "the boss out of the cone: hysteresis does not keep it");
  memcpy(c, base, sizeof c);
  c[0].vis = AIM_VIS_NO;
  CHECK_MSG(aim_boss_pick(c, 2, -1, AIM_PICK_CROSSHAIR, 7, AIM_BOSS_HIDDEN) == -1, "no add visible, boss hidden: none");
}
// tbot.h boss priority: while the boss is possible an add is never fired at, in any mode.
static void tb_0537_tbot_boss_skip() {
  static int objs_[2];  // distinct storage: an optimiser may fold equal unwritten locals
  int& boss = objs_[0]; int& add = objs_[1];
  CHECK_MSG(tbot_boss_skips(1, &add, &boss), "boss possible: an add is skipped");
  CHECK_MSG(!tbot_boss_skips(1, &boss, &boss), "boss possible: the boss is fired at");
  CHECK_MSG(!tbot_boss_skips(0, &add, &boss) && !tbot_boss_skips(0, &add, 0), "boss impossible or none: the add");
  CHECK_MSG(!tbot_boss_skips(1, 0, &boss), "no target: nothing to skip");
}

static void tb_0538_splash_fail_closed_and_budget() {
  const float duke[3] = { 0.f, 0.f, 0.f };
  SplashBudget b = { 0, 2, 0 };
  CHECK_MSG(splash_budget_take(&b) && splash_budget_take(&b) && !b.capped, "two traces within a budget of two");
  CHECK_MSG(!splash_budget_take(&b) && b.capped && b.used == 2, "the third is refused and the cap noted");
  SplashPts p;
  memset(&p, 0, sizeof p);
  p.fail = SPL_FAIL_CAP;
  const SplashVerdict c = splash_judge(SPL_READ, 260.f, duke, 21.f, 45.f, p);
  CHECK_MSG(c.hold && c.fail == SPL_FAIL_CAP && c.src < 0, "the budget spent before a verdict: held");
  p.fail = SPL_FAIL_POINT;
  CHECK_MSG(splash_judge(SPL_DEFAULT, 400.f, duke, 21.f, 45.f, p).hold, "the aim point unread: held");
  p.fail = SPL_FAIL_DUKE;
  CHECK_MSG(!splash_judge(SPL_NONE, 0.f, duke, 21.f, 45.f, p).hold, "a hitscan gun is never held, even unread");
  SplashPts g = spl_pts(SPL_PT_BLOCKER, 600.f, 0.f, 0.f);
  g.ok[SPL_PT_BLOCKER_GUN] = 1;
  g.p[SPL_PT_BLOCKER_GUN][0] = 150.f;
  const SplashVerdict v = splash_judge(SPL_READ, 260.f, duke, 21.f, 45.f, g);
  CHECK_MSG(v.hold && v.src == SPL_PT_BLOCKER_GUN && v.fail == SPL_FAIL_NONE,
            "a wall on the gun's current line, nearer than the lead path's: held by it");
}
static void tb_0539_ln_report_cap() {
  LnCap c;
  memset(&c, 0, sizeof c);
  int ok = 1;
  for (int i = 0; i < LN_LINES_PER_LEVEL; ++i) ok = ok && ln_cap_take(&c, 7);
  CHECK_MSG(ok, "16 lines in a level");
  CHECK_MSG(!ln_cap_take(&c, 7) && !ln_cap_take(&c, 7) && c.dropped == 2, "the 17th and on are dropped and counted");
  CHECK_MSG(ln_cap_take(&c, 8), "a new level serial starts again");
}

static void tb_0540_splash_passes_triggers_and_water() {
  CHECK_MSG(splash_passes(1, 0, 0, 0) && splash_passes(1, 0, -1, -1), "a trigger, a volume or water (response 1): passed");
  CHECK_MSG(!splash_passes(2, 0, 1, 0), "a wall or a pawn (response 2): the detonation point");
  CHECK_MSG(!splash_passes(0, 0, 0, 0) && !splash_passes(-1, 0, 0, 0), "Abort or unread: the detonation point");
  CHECK_MSG(!splash_passes(1, 1, 0, 0), "glass and a rocket without bBreakGlass: it explodes there");
  CHECK_MSG(splash_passes(1, 1, 1, 0), "glass the SuperTurret's rocket breaks: passed");
  CHECK_MSG(!splash_passes(1, 1, 1, 1) && !splash_passes(1, 1, -1, 0) && !splash_passes(1, 1, 1, -1),
            "unbreakable glass, or either flag unread: the detonation point");
}

// progress.h: a DLC map's chapter is its whole name against the URL (dlc.md section 1); the base 5-character cut stays.
static void tb_0542_pg_dlc_chapter_match() {
  PgTable d, b;
  pg_dlc_table_builtin(&d);
  pg_table_builtin(&b);
  CHECK_MSG(pg_chapter_of_map(d, "DLC03_Map00") == 0 && pg_chapter_of_map(d, "dlc03_map04") == 1,
            "case-insensitive, the whole name");
  CHECK_MSG(pg_chapter_of_map(d, "DLC03_Map15.dnf") == 13 && pg_chapter_of_map(d, "Maps\\DLC03_Map01") == 9,
            "a path or an extension is not part of the name");
  CHECK_MSG(pg_chapter_of_map(d, "DLC03_DM-Rooftops") == -1 && pg_chapter_of_map(d, "Map00") == -1 &&
            pg_chapter_of_map(d, "") == -1 && pg_chapter_of_map(d, 0) == -1, "MP maps and base maps are no DLC chapter");
  CHECK_MSG(pg_chapter_of_map(b, "DLC03_Map00") == -1 && pg_chapter_of_map(b, "Map04B") == 4 &&
            pg_chapter_of_map(b, "map00") == 0, "the base table keeps its cut");
  CHECK_MSG(strcmp(pg_pick_map_name(b, "DLC03_Map05", "x", &d), "DLC03_Map05") == 0 &&
            strcmp(pg_pick_map_name(b, "cp03", "DLC03_Map05", &d), "DLC03_Map05") == 0,
            "a DLC package is a chapter map; a DLC checkpoint takes MapFileName");
}
// progress.h: the DLC chapter table, the game's order and awards first, the built-in copy as the fallback.
static void tb_0543_pg_dlc_table() {
  PgTable t;
  pg_dlc_table_builtin(&t);
  int sum = 0;
  for (int i = 0; i < t.n; ++i) sum += t.awardN[i];
  CHECK_MSG(t.n == 14 && sum == 30 && !t.fromGame, "14 chapters, 30 table entries (dlc.md section 1)");
  CHECK(strcmp(t.url[4], "DLC03_Map07") == 0 && t.awardN[4] == 9 && t.awards[4][0] == 15 &&
        strcmp(t.title[13], "Duke Side of the Moon") == 0 && t.awardN[10] == 0);
  float cs = 0.f;
  for (int i = 0; i < PG_DLC_AWARDS; ++i) cs += PG_DLC_CAP_AWARDS[i];
  CHECK_MSG(cs == 35.f && strcmp(PG_DLC_AWARD_NAMES[17], "Toilet") == 0 && strcmp(PG_DLC_AWARD_NAMES[0], "AmericanFlag") == 0,
            "19 caps summing 35; the enum order");
  const char urls[3][PG_URL_CAP] = { "dlc03_map04", "DLC03_Map00", "DLC03_Map99" };
  pg_dlc_table_build(&t, urls, 3, 0);
  CHECK_MSG(t.n == 3 && t.fromGame == PG_SRC_ORDER && t.awardN[0] == 1 && t.awards[0][0] == 10 &&
            strcmp(t.title[0], "Going Down") == 0 && t.awardN[2] == 0 && strcmp(t.title[2], "DLC03_Map99") == 0,
            "CampaignList's order; built-in awards and titles by URL; an unknown URL titled by itself");
  PgMapAwards aw;
  memset(&aw, 0, sizeof aw);
  aw.n = 2;
  strcpy_s(aw.map[0], PG_URL_CAP, "DLC03_MAP00");
  aw.awardN[0] = 2;
  aw.awards[0][0] = 17;
  aw.awards[0][1] = 1;
  strcpy_s(aw.map[1], PG_URL_CAP, "DLC03_Map99");
  aw.awardN[1] = 1;
  aw.awards[1][0] = 5;
  pg_dlc_table_build(&t, 0, 0, &aw);
  CHECK_MSG(t.n == 14 && t.fromGame == PG_SRC_AWARDS && t.awardN[0] == 2 && t.awards[0][1] == 1 && t.awardN[1] == 0,
            "the game's awards by map name; a chapter it does not list has none");
  pg_dlc_table_build(&t, urls, 3, &aw);
  CHECK(t.fromGame == (PG_SRC_ORDER | PG_SRC_AWARDS) && t.awardN[1] == 2 && t.awardN[2] == 1 && t.awards[2][0] == 5);
}
// progress.h: DLC mode is the map's DLC03_ prefix or the DLC player class.
static void tb_0544_pg_dlc_mode() {
  CHECK(pg_dlc_mode("DLC03_Map00", 0) && pg_dlc_mode("dlc03_map13.dnf", 0) && pg_dlc_mode("cp03", 1));
  CHECK_MSG(!pg_dlc_mode("Map00", 0) && !pg_dlc_mode("", 0) && !pg_dlc_mode(0, 0) && !pg_dlc_mode("Map00", -1),
            "a base map with the base player (or unread) is not DLC");
}

// progress.h: the DLC rows count their awards through the DLC table and tick from [DLCProgress]; no helmets or phones.
static void tb_0545_pg_dlc_chapters() {
  PgTable t;
  pg_dlc_table_builtin(&t);
  PgDlcFlags f;
  memset(&f, 0, sizeof f);
  f.award[17] = 1;
  f.done[0][0] = f.done[0][1] = 1;
  PgChapter c[PG_CHAPTERS_MAX];
  const int n = pg_build_dlc_chapters(t, f, c);
  CHECK(n == 14 && c[0].egoFound == 1 && c[0].egoTotal == 1 && c[0].done[0] && c[0].done[1] && !c[0].done[2]);
  CHECK_MSG(c[11].egoFound == 1 && c[11].egoTotal == 5 && c[10].egoTotal == 0 && strcmp(c[13].url, "DLC03_Map15") == 0,
            "the toilet counts in Map00 and Map03; Tunnel Vision has none");
  int helm = 0;
  for (int i = 0; i < n; ++i) helm += c[i].helmTotal + c[i].phoneTotal;
  CHECK_MSG(helm == 0 && pg_dlc_award_found(f, 17) && !pg_dlc_award_found(f, 0) && !pg_dlc_award_found(f, 19),
            "no helmets or phones in the DLC; the found test is bounded");
}
// progress.h: the DLC run totals (40 + 30 + found caps over 100, the preorder bonus on both), the profile count for
// achievement 61 and the chapters done per difficulty; the tab opens on the campaign in play.
static void tb_0546_pg_dlc_totals_and_view() {
  PgDlcFlags f;
  memset(&f, 0, sizeof f);
  PgDlcTotals t = pg_dlc_totals(f, 14, 0, PG_DLC_CAP_AWARDS, PG_CAP_BASE, PG_DLC_AUTO_EGO, PG_CAP_MAX, PG_PREORDER_EGO,
                                0, 0.f);
  CHECK_MSG(t.run.awardsFound == 0 && t.run.awardsTotal == 19 && t.run.capPermanent == 70 && t.run.capMax == 100,
            "nothing found: 40 + 30 over 100");
  for (int i = 0; i < PG_DLC_AWARDS; ++i) f.award[i] = 1;
  f.ever[3] = f.ever[18] = 1;
  f.done[0][2] = f.done[5][2] = f.done[13][2] = 1;
  f.done[15][2] = 1;   // past the table: not counted
  t = pg_dlc_totals(f, 14, 0, PG_DLC_CAP_AWARDS, PG_CAP_BASE, PG_DLC_AUTO_EGO, PG_CAP_MAX, PG_PREORDER_EGO, 1, 88.f);
  CHECK_MSG(t.run.awardsFound == 19 && t.run.capPermanent == 100 && t.run.capNowOk && t.run.capNow == 88,
            "all found: 105 held at the maximum");
  CHECK(t.ever == 2 && t.everTotal == 19 && t.doneAt[2] == 3 && t.doneAt[0] == 0 && t.chapters == 14);
  t = pg_dlc_totals(f, 14, 1, PG_DLC_CAP_AWARDS, PG_CAP_BASE, PG_DLC_AUTO_EGO, PG_CAP_MAX, PG_PREORDER_EGO, 0, 0.f);
  CHECK_MSG(t.run.capPermanent == 105 && t.run.capMax == 105, "the preorder bonus raises both");
  CHECK_MSG(pg_view_pick(0, 1, 1) == PG_VIEW_DLC && pg_view_pick(1, 1, 0) == PG_VIEW_BASE &&
            pg_view_pick(PG_VIEW_DLC, 0, 0) == PG_VIEW_DLC && pg_view_pick(PG_VIEW_BASE, 0, 1) == PG_VIEW_BASE,
            "opening picks the campaign in play; afterwards the switch holds");
}

// egoesp.h: in DLC mode the DLC rule set alone applies (dlc.md section 6). A DLC toilet's chain also carries the base
// urination receptacle: the DLC set reads award 17, the base set would read base 0.
static int eg_set_rule(const EgRuleSet& s, const char* name) {
  for (int i = 0; i < s.nCls; ++i)
    if (strcmp(s.cls[i].name, name) == 0) return i;
  return -1;
}
static void tb_0547_eg_dlc_toilet_reads_17() {
  const EgRuleSet& dlc = eg_rules(1);
  const EgRuleSet& base = eg_rules(0);
  CHECK(base.cls == EG_CLASSES && base.nCls == EG_CLASS_COUNT && base.awards == PG_AWARDS && dlc.awards == PG_DLC_AWARDS);
  // Names: the DLC toilet 300, the base receptacle 301 (its super), Actor 7.
  const int chain[] = { 300, 301, 7 };
  int dn[EG_CLASS_COUNT] = {}, bn[EG_CLASS_COUNT] = {};
  const int dt = eg_set_rule(dlc, "dnControl_Bathroom_Toilet_DLC03");
  const int bt = eg_set_rule(base, "dnControl_Bathroom_UrinationReceptacle");
  CHECK(dt >= 0 && bt >= 0);
  dn[dt] = 300;
  bn[bt] = 301;
  int ids[EG_IDS];
  const int rd = eg_class_rule(chain, 3, dn, dlc.nCls), rb = eg_class_rule(chain, 3, bn, base.nCls);
  CHECK_MSG(rd == dt && eg_giver_ids(dlc.cls[rd], 0, 0, 0, 0, ids) == 1 && ids[0] == 17, "a DLC toilet reads award 17");
  CHECK_MSG(rb == bt && eg_giver_ids(base.cls[rb], 0, 0, 0, 0, ids) == 1 && ids[0] == 0, "the base set would say 0");
  bn[bt] = 0;
  CHECK_MSG(eg_class_rule(chain, 3, bn, base.nCls) == -1, "an unresolved name never matches");
  for (int i = 0; i < dlc.nCls; ++i)
    CHECK_MSG(dlc.cls[i].id0 >= 0 && dlc.cls[i].id0 < PG_DLC_AWARDS && dlc.cls[i].id1 < PG_DLC_AWARDS &&
              dlc.cls[i].kind != EG_BOOSTER, "DLC ids only, no instance-id booster");
  const int bag = eg_set_rule(dlc, "Settings_Workout_PunchingBag_DLC03");
  CHECK_MSG(bag >= 0 && dlc.cls[bag].id0 == 13 && eg_set_rule(dlc, "Settings_Workout_SpeedBag") < 0,
            "the punching bag is fixed 13; the base speed bag awards nothing in the DLC");
  const int mon = eg_set_rule(dlc, "Electronics_Generic_Computer_Monitor_DLC03");
  CHECK_MSG(mon >= 0 && dlc.cls[mon].kind == EG_MONITOR && eg_giver_ids(dlc.cls[mon], 0, 0, 1, 0, ids) == 0 &&
            eg_giver_ids(dlc.cls[mon], 0, 0, 1, 1, ids) == 1 && ids[0] == 3, "a monitor needs its keyboard");
  CHECK(eg_set_rule(dlc, "DrProton_SpiderBot") < 0);
}
// egoesp.h: the DLC's event rules, its TriggerPlayerDLC03 list, its labels and its awards-open gate.
static void tb_0548_eg_dlc_events_labels_gate() {
  const EgRuleSet& dlc = eg_rules(1);
  CHECK(strcmp(dlc.tpClass, "TriggerPlayerDLC03") == 0 && strcmp(dlc.tpAwards, "EgoCapAwardsDLC03") == 0 &&
        strcmp(eg_rules(0).tpAwards, "EgoCapAwards") == 0);
  int arcade = -1;
  for (int k = 0; k < dlc.nEv; ++k)
    if (strcmp(dlc.ev[k].tag, "NukeTankArcadeMachine") == 0) arcade = k;
  CHECK(arcade >= 0 && dlc.ev[arcade].id == 1 && strcmp(dlc.ev[arcade].cls, "dnControl_ArcadeMachine") == 0);
  CHECK(eg_rule_event_in(dlc, dlc.nCls + arcade) == arcade && eg_rule_event_in(dlc, 0) == -1 &&
        eg_rule_event_in(dlc, dlc.nCls + dlc.nEv) == -1);
  int cls[EG_EVENT_COUNT] = {}, tag[EG_EVENT_COUNT] = {};
  cls[arcade] = 400;
  tag[arcade] = 500;
  const int chain[] = { 400, 7 };
  const unsigned m = eg_event_mask_n(chain, 2, cls, dlc.nEv);
  CHECK(m == (1u << arcade) && eg_event_pick_n(m, 500, tag, dlc.nEv) == arcade && eg_event_pick_n(m, 501, tag, dlc.nEv) == -1);
  unsigned char listed[PG_AWARDS] = {};
  const unsigned char tp[] = { 1, 7, 19, 30 };
  eg_note_listed_n(listed, tp, 4, PG_DLC_AWARDS);
  CHECK_MSG(listed[1] && listed[7] && !listed[19] && !listed[30], "a DLC list keeps DLC ids only");
  CHECK(eg_event_id(dlc.ev[arcade], listed, 0) == 1);
  char l[40];
  eg_label_in(1, 17, l, sizeof l);
  CHECK(strcmp(l, "Toilet") == 0);
  eg_label_in(1, 10, l, sizeof l);
  CHECK(strcmp(l, "Porn Magazine E") == 0);
  eg_label_in(1, 0, l, sizeof l);
  CHECK(strcmp(l, "American Flag") == 0);
  eg_label_in(1, 19, l, sizeof l);
  CHECK(l[0] == 0);
  eg_label_in(0, 0, l, sizeof l);
  CHECK(strcmp(l, "Toilet") == 0);
  CHECK_MSG(eg_awards_open_in(1, 1, 0, 0) && !eg_awards_open_in(1, 1, 1, 1) && eg_awards_open_in(1, 1, 0, -1),
            "DLC: only the cheat flag closes (EgoCapAwardsEnabled is not its gate)");
  CHECK(!eg_awards_open_in(0, 1, 0, 0) && eg_awards_open_in(0, 1, 1, 0));
}

// achhelp.h: the DLC rows (dlc.md section 5): 50-52 counters on the DLC maps with nothing to mark, 57-60 from the
// DLC ticks, 61 from the DLC ego awards; 29 and the base completion stay out of DLC levels; 62 marks the cabinet.
static void tb_0549_ah_dlc_rows() {
  int seen[AR_COUNT] = {}, ids[AH_ROWS_MAX];
  AhLevel m00 = { "DLC03_Map00", 0, 0, 1, 1 };
  int n = ah_list(m00, seen, ids, AH_ROWS_MAX);
  const int want00[] = { 50, 51, 52, 57, 58, 59, 60, 61 };
  CHECK_MSG(n == 8, "DLC03_Map00: three counters, four completions, the DLC ego");
  for (int id : want00) CHECK(ah_has(ids, n, id));
  const AhLevel m03 = { "dlc03_map03", 0, 0, 5, 1 };
  n = ah_list(m03, seen, ids, AH_ROWS_MAX);
  CHECK(ah_has(ids, n, 54) && ah_has(ids, n, 62) && ah_has(ids, n, 61) && !ah_has(ids, n, 29) && n == 10);
  const AhLevel m02 = { "DLC03_Map02", 0, 0, 0, 1 };
  n = ah_list(m02, seen, ids, AH_ROWS_MAX);
  CHECK_MSG(!ah_has(ids, n, 61) && ah_has(ids, n, 57) && ah_has(ids, n, 50), "Tunnel Vision has no ego award");
  const AhLevel m09 = { "Map09", 0, 3, 14 };
  n = ah_list(m09, seen, ids, AH_ROWS_MAX);
  CHECK_MSG(!ah_has(ids, n, 61) && !ah_has(ids, n, 57) && !ah_has(ids, n, 50) && ah_has(ids, n, 29),
            "a base level lists no DLC row");
  const AhAch* a50 = ah_find(50);
  const AhAch* a52 = ah_find(52);
  const AhAch* a61 = ah_find(61);
  CHECK(a50 && a50->scope == AH_SCOPE_COUNTER && a50->prog == AH_PROG_PROFILE && a50->total == 10 && !ah_has_switch(*a50));
  CHECK(a52 && a52->total == 20 && ah_find(51) && ah_find(51)->prog == AH_PROG_NONE);
  CHECK(a61 && a61->scope == AH_SCOPE_COLLECTED && a61->prog == AH_PROG_EGO_DLC && a61->total == 19);
  CHECK(ah_find(58) && ah_find(58)->prog == AH_PROG_DLC_DONE && ah_find(58)->total == 14 && ah_dlc_diff(57) == 0 &&
        ah_dlc_diff(60) == 3 && ah_dlc_diff(56) == -1 && ah_dlc_diff(61) == -1);
  const AhAch* a62 = ah_find(62);
  CHECK_MSG(a62 && a62->kinds[0] == AK_BS3000 && strcmp(AH_KINDS[AK_BS3000].cls, "dnDecoration") == 0 &&
            AH_KINDS[AK_BS3000].match == AH_MATCH_TAG && strcmp(AH_KINDS[AK_BS3000].key, "BS3000_ArcadeMachine") == 0 &&
            (AH_KINDS[AK_BS3000].flags & AH_K_EXACT) && !(AH_KINDS[AK_BS3000].flags & AH_K_INVISIBLE),
            "row 62 marks the visible cabinet, not the trigger");
  CHECK(ah_kind_here(AK_BS3000, "DLC03_Map03") && !ah_kind_here(AK_BS3000, "DLC03_Map07"));
}
// items.h: the DLC's quest pickups are their own classes (not the base quest pickups) and read as quest items.
static void tb_0550_it_dlc_quest_pickups() {
  int names[IT_CLASS_COUNT];
  for (int i = 0; i < IT_CLASS_COUNT; ++i) names[i] = 100 + i;
  int gas = -1, cell = -1, pickup = -1;
  for (int i = 0; i < IT_CLASS_COUNT; ++i) {
    if (strcmp(IT_CLASSES[i].name, "GasCanPickup2") == 0) gas = i;
    if (strcmp(IT_CLASSES[i].name, "MoonBuggy_PowerCellPickup") == 0) cell = i;
    if (strcmp(IT_CLASSES[i].name, "Pickup") == 0) pickup = i;
  }
  CHECK(gas >= 0 && cell >= 0 && IT_CLASSES[gas].kind == IT_QUEST && IT_CLASSES[cell].kind == IT_QUEST);
  const int gasChain[] = { 100 + gas, 900, 100 + pickup }, cellChain[] = { 100 + cell, 901, 100 + pickup };
  CHECK(it_rule_of_chain(gasChain, 3, names, IT_CLASS_COUNT) == gas &&
        it_rule_of_chain(cellChain, 3, names, IT_CLASS_COUNT) == cell && it_row_of_kind(IT_QUEST) == IT_ROW_QUEST);
  char l[32];
  it_class_label("GasCan2", l, sizeof l);
  CHECK(strcmp(l, "Gas can") == 0);
  it_class_label("MoonBuggy_PowerCell", l, sizeof l);
  CHECK(strcmp(l, "Power cell") == 0);
}

// achhelp.h: rows 57-60 count to the DLC table's row count (CampaignList may differ from dlc.md's 14); the rest keep
// their fixed total.
static void tb_0551_ah_dlc_done_total() {
  const AhAch* a57 = ah_find(57);
  const AhAch* a61 = ah_find(61);
  CHECK(a57 && a61);
  CHECK_MSG(ah_total(*a57, 14) == 14 && ah_total(*a57, 12) == 12, "the table's rows");
  CHECK_MSG(ah_total(*a57, 0) == a57->total && ah_total(*a57, -1) == a57->total, "no table: the fixed total");
  CHECK_MSG(ah_total(*a61, 12) == 19 && ah_total(*ah_find(52), 12) == 20, "other rows keep theirs");
}

// aim.h (aim.md section 13): a config the table does not name is classified by what PerformFire does with it. The
// Expander's dnRocket_ExpandBlast is PHYS_Projectile (6); the Impregnator's grenade and the pipe bomb PHYS_Meqon (18),
// the trip mine 9: thrown. The hot sauce fires nothing.
static void tb_0557_weapon_classify() {
  AimKindBy r = aim_weapon_classify(AIMW_NONE, 1, 6, 0.f, 0);
  CHECK_MSG(r.kind == AIMW_PROJECTILE && r.by == AIMW_BY_PROJECTILE, "the Expander: a straight projectile");
  r = aim_weapon_classify(AIMW_NONE, 1, 18, 0.f, 0);
  CHECK_MSG(r.kind == AIMW_NONE && r.by == AIMW_BY_THROWN, "a Meqon grenade without impact detonation (the pipe bomb): thrown");
  CHECK(aim_weapon_classify(AIMW_NONE, 1, 9, 0.f, 0).kind == AIMW_NONE);
  CHECK_MSG(aim_weapon_classify(AIMW_NONE, 1, -1, 0.f, 0).kind == AIMW_NONE, "physics unread: not redirected");
  r = aim_weapon_classify(AIMW_NONE, 0, -1, 3000.f, 0);
  CHECK_MSG(r.kind == AIMW_NONE && r.by == AIMW_BY_NOTHING, "the hot sauce: a trace distance but no trace type");
  r = aim_weapon_classify(AIMW_NONE, 0, -1, 3000.f, 1);
  CHECK_MSG(r.kind == AIMW_HITSCAN && r.by == AIMW_BY_TRACE, "TraceDamageType and a distance: TraceFire");
  CHECK_MSG(aim_weapon_classify(AIMW_NONE, 0, -1, 0.f, 1).kind == AIMW_NONE, "no distance: no trace");
  r = aim_weapon_classify(AIMW_BEAM, 1, 18, 0.f, 0);
  CHECK_MSG(r.kind == AIMW_BEAM && r.by == AIMW_BY_NAME, "a named config keeps its kind");
  CHECK(strcmp(aim_kind_by_label(AIMW_BY_PROJECTILE), "?") != 0 && strcmp(aim_kind_by_label(99), "?") == 0);
  // Pinned by name: the AT Captain's laser and the nuke are straight rockets the player can carry.
  CHECK(aim_weapon_kind(L"ATCaptainLaserWeaponConfig") == AIMW_PROJECTILE && aim_weapon_kind(L"NukeWeaponConfig") == AIMW_PROJECTILE);
  int sp = 0, sy = 0;
  aim_spawn_offset(L"ATCaptainLaserWeaponConfig", &sp, &sy);
  CHECK_MSG(sp == 200 && sy == -200, "ATCaptainLaser.AdjustProjectileSpawn adds Rot(200, -200, 0)");
}

// bloat.h (aim.md section 13): ExpanderDamage deals no Health damage in single player and bloats; a class with
// SecondBloat pops on the second hit, any other is done after one (it pops when full, or only bloats).
static void tb_0558_bloat_verdict() {
  const wchar_t* exp[2] = { L"ExpanderDamage", L"DamageType" };
  const wchar_t* pop[2] = { L"ExpanderPopDamage", L"DamageType" };
  CHECK(bloat_type(exp, 2) && !bloat_type(pop, 2) && !bloat_type(exp, 0) && !bloat_type(0, 2));
  CHECK_MSG(bloat_can(1, 0) && !bloat_can(0, 0) && !bloat_can(1, 1) && bloat_can(-1, -1), "CanBloat; unread: can");
  CHECK_MSG(bloat_verdict(0, 0, -1, 0) == BL_IMMUNE, "bCanBloat false: the gun does nothing to it");
  CHECK(bloat_verdict(1, 0, 0, 0) == BL_SHOOT && bloat_verdict(1, 2, 0, 0) == BL_SHOOT);
  CHECK_MSG(bloat_verdict(1, 2, 1, 0) == BL_DONE && bloat_verdict(1, 1, 0, 1) == BL_DONE, "popping, or a pop in flight");
  CHECK_MSG(bloat_verdict(1, 0, 0, 2) == BL_DONE && bloat_verdict(1, 0, 0, 1) == BL_SHOOT, "two blasts pop it");
  CHECK_MSG(bloat_verdict(1, 0, -1, 0) == BL_SHOOT && bloat_verdict(1, 1, -1, 0) == BL_DONE &&
            bloat_verdict(1, 0, -1, 1) == BL_DONE, "no SecondBloat: one hit is all it takes");
  CHECK_MSG(bloat_verdict(1, 3, -1, 0) == BL_SHOOT && bloat_verdict(1, 3, 0, 0) == BL_SHOOT &&
            bloat_verdict(1, 3, 0, 1) == BL_DONE, "shrinking back: bloat it again; a SecondBloat class pops");
  CHECK_MSG(bloat_verdict(1, -1, -1, 0) == BL_SHOOT, "state unread: fire");
  CHECK(bloat_steps(0, -1) == 0 && bloat_steps(2, -1) == 1 && bloat_steps(3, -1) == 0 && bloat_steps(3, 0) == 1 &&
        bloat_steps(1, 1) == 2 && bloat_steps(-1, -1) == 0);
}

// bloat.h: the per-pawn reads, fresh for BLOAT_MS; a change is reported. spawnbox.h: an expanded pawn is left out after
// the lasting reasons, before doomed. splash.h: the Expander's blast spares its Instigator, as the shrink blast.
static void tb_0559_bloat_table_and_exclusion() {
  static BloatTable t;
  memset(&t, 0, sizeof t);
  int a = 0, b = 0, c = 0, pawn = 0;
  CHECK_MSG(bloat_note(&t, &pawn, 100, 1, 0, 0) == 1 && bloat_note(&t, &pawn, 120, 1, 0, 0) == 0 &&
            bloat_note(&t, &pawn, 140, 1, 2, 0) == 1, "new, same, changed");
  CHECK(bloat_get(&t, &pawn, 140 + BLOAT_MS - 1, &a, &b, &c) && a == 1 && b == 2 && c == 0);
  CHECK_MSG(!bloat_get(&t, &pawn, 140 + BLOAT_MS, &a, &b, &c) && !bloat_get(&t, &a, 140, &a, &b, &c), "stale or unknown");
  int many[BLOAT_SLOTS + 1];
  for (int i = 0; i <= BLOAT_SLOTS; i++) bloat_note(&t, &many[i], 200u + (unsigned)i, 1, 0, -1);
  CHECK_MSG(t.n == BLOAT_SLOTS && bloat_get(&t, &many[BLOAT_SLOTS], 300, &a, &b, &c), "full: the oldest goes");
  CHECK_MSG(spawnbox_exclusion(0, 0, 0, 0, 0, 0, 1, 0, 0, 1) == SB_EXCL_EXPANDED &&
            spawnbox_exclusion(0, 0, 0, 0, 0, 0, 0, 1, 0, 1) == SB_EXCL_WRONGTYPE &&
            spawnbox_exclusion(0, 0, 1, 0, 0, 0, 0, 0, 0, 1) == SB_EXCL_TELEPORT, "expanded: before doomed only");
  const wchar_t* eb[3] = { L"dnRocket_ExpandBlast", L"dnRocket", L"Projectile" };
  const wchar_t* sb[2] = { L"dnRocket_ShrinkBlast", L"dnRocket" };
  const wchar_t* rpg[2] = { L"dnRocket_RPG_Homing", L"dnRocket_RPG" };
  CHECK(splash_spares_instigator(eb, 3) && splash_spares_instigator(sb, 2) && !splash_spares_instigator(rpg, 2));
  CHECK(strcmp(aim_boss_label(AIM_BOSS_EXPANDED), "?") != 0);
}

// overkill.h: the Expander's blasts in flight doom by bloat steps, not by damage (each deals 0 in single player).
static void tb_0560_ok_bloat_expected() {
  CHECK_MSG(ok_bloat_expected(250.f, 0, 1, 1, 80.f) == 250.f, "no bloat shot: the damage");
  CHECK_MSG(ok_bloat_expected(0.f, 1, 1, 1, 80.f) == 80.f && ok_doomed(ok_bloat_expected(0.f, 1, 1, 1, 80.f), 80.f),
            "the pop is on its way: doomed");
  CHECK_MSG(ok_bloat_expected(30.f, 1, 1, 0, 80.f) == 30.f, "not yet: the other projectiles' damage stays");
  CHECK_MSG(ok_bloat_expected(120.f, 1, 1, 1, 80.f) == 120.f && ok_doomed(ok_bloat_expected(120.f, 1, 1, 0, 80.f), 80.f),
            "either the damage or the pop dooms it");
  CHECK_MSG(ok_bloat_expected(0.f, 1, 0, 1, 80.f) == 0.f, "the bloat state unread: not doomed by it");
  CHECK_MSG(ok_proj_counts_dmg(100.f, 1, 1) == 0.f && ok_proj_counts_dmg(100.f, 1, 0) == 100.f &&
            ok_proj_counts_dmg(100.f, 0, 1) == 100.f, "an Expander blast deals a pawn nothing; a catfish its Damage");
  CHECK_MSG(ok_bloat_landed(OK_EV_GONE, 0, 1) == OK_EV_LANDED && ok_bloat_landed(OK_EV_CAP, 1, 2) == OK_EV_LANDED &&
            ok_bloat_landed(OK_EV_SHORT, 0, 1) == OK_EV_LANDED, "a landed blast bloated or popped it: a hit");
  CHECK_MSG(ok_bloat_landed(OK_EV_GONE, 1, 1) == OK_EV_GONE && ok_bloat_landed(OK_EV_GONE, -1, 2) == OK_EV_GONE &&
            ok_bloat_landed(OK_EV_DEAD, 0, 1) == OK_EV_DEAD && ok_bloat_landed(OK_EV_GONE, 0, -1) == OK_EV_GONE,
            "no step gained, not a bloat mark, dead, or unread: as before");
}

// mpgate.h: every combat, ESP and gameplay feature refuses outside standalone play (Level.NetMode, Engine.u ENetMode).
static void tb_0561_mp_gate() {
  CHECK_MSG(mp_reason(1, MP_NM_STANDALONE) == 0, "a standalone level allows");
  CHECK_MSG(mp_reason(1, MP_NM_CLIENT) != 0 && mp_reason(1, MP_NM_LISTEN) != 0 && mp_reason(1, MP_NM_DEDICATED) != 0,
            "client, listen and dedicated refuse");
  CHECK_MSG(mp_reason(1, MP_NET_UNREAD) != 0, "an unread mode in a level fails closed");
  CHECK_MSG(mp_reason(1, 77) != 0, "a value outside the enum is not standalone");
  CHECK_MSG(mp_reason(0, MP_NET_UNREAD) == 0 && mp_reason(0, MP_NM_CLIENT) == 0, "no level (the Entry menu): allows");
  CHECK_MSG(strcmp(mp_reason(1, MP_NM_CLIENT), "Not in multiplayer") == 0, "the row's reason text");
  {
    int st = 0;
    st = mp_streak(st, 1);
    st = mp_streak(st, 1);
    CHECK_MSG(!mp_sustained(st), "two multiplayer frames do not end a flight");
    st = mp_streak(st, 0);
    st = mp_streak(st, 1);
    st = mp_streak(st, 1);
    CHECK_MSG(!mp_sustained(st), "one clear frame restarts the count");
    st = mp_streak(st, 1);
    CHECK_MSG(mp_sustained(st), "three in a row end it");
    st = mp_streak(st, 1);
    CHECK_MSG(mp_sustained(st) && mp_streak(st, 0) == 0, "the count holds, and clears on a standalone frame");
  }
  CHECK_MSG(dv_on(1, 0, 0) && !dv_on(1, 0, MP_WHY) && !dv_on(1, "x", 0) && !dv_on(0, 0, 0),
            "Clean Duke Vision runs only when wanted, resolved and standalone");
}

// aim.h (aim.md 13): the Impregnator's spit is a PHYS_Meqon grenade exploding on any impact: a lob the arc solver aims.
// KSetLinearVelocity(Speed x dir + UpwardsBoost), zone gravity x GravityScale, terminal clamp, no damping; its fuse
// (ExplosionDelay - Variance) bounds the flight. A Meqon grenade without impact detonation (pipe bomb) stays thrown.
static void tb_0562_lob_classify_and_model() {
  AimKindBy r = aim_weapon_classify(AIMW_NONE, 1, 18, 0.f, 0, 1);
  CHECK_MSG(r.kind == AIMW_PROJECTILE && r.by == AIMW_BY_LOB, "the Impregnader: a lob");
  CHECK_MSG(aim_weapon_classify(AIMW_NONE, 1, 18, 0.f, 0, 0).kind == AIMW_NONE, "the pipe bomb: thrown");
  CHECK_MSG(aim_weapon_classify(AIMW_NONE, 1, 9, 0.f, 0, 1).kind == AIMW_NONE, "the trip mine: not a lob");
  CHECK(strcmp(aim_kind_by_label(AIMW_BY_LOB), "?") != 0);
  AimProjModel m = aim_lob_model(800.f, 140.f, 0.5f, -950.f, 2500.f, 4.f, 2.f, 1.f);
  CHECK(m.kind == AIM_PM_BALLISTIC && m.speed == 800.f && m.boostZ == 140.f && m.gravity == 475.f &&
        m.terminal == 2500.f && m.maxT == 2.f && m.bounce == 0);
  m = aim_lob_model(800.f, 140.f, -1.f, 0.f, 0.f, -1.f, -1.f, 1.f);
  CHECK_MSG(m.gravity == 475.f && m.terminal == 2500.f && m.maxT == 2.f, "unread: dnGrenade's 0.5, -950, 2500, 4 - 2 s");
  m = aim_lob_model(800.f, 140.f, 0.5f, -950.f, 2500.f, 4.f, 2.f, 0.5f);
  const float k = powf(0.5f, 1.25f);
  CHECK_MSG(fabsf(m.speed - 800.f * k) < 0.01f && fabsf(m.boostZ - 140.f * k) < 0.01f,
            "a shrunk Duke: Speed x ratio, then the throw x DrawScale^0.25");
  const float dir[3] = { 1.f, 0.f, 0.f };
  float v[3];
  aim_launch_vel(aim_lob_model(800.f, 140.f, 0.5f, -950.f, 2500.f, 4.f, 2.f, 1.f), dir, v);
  CHECK(v[0] == 800.f && v[1] == 0.f && v[2] == 140.f);
}

// aim.h: the boosted arc. The intercept's own flight (launch velocity Speed x dir + the boost) meets the target.
static void tb_0563_lob_boosted_arc() {
  const AimProjModel m = aim_lob_model(800.f, 140.f, 0.5f, -950.f, 2500.f, 4.f, 2.f, 1.f);
  const float o[3] = { 0.f, 0.f, 0.f }, still[3] = { 0.f, 0.f, 0.f }, v0[3] = { 800.f, 0.f, 140.f };
  float at[3];
  aim_ballistic_pos(m, o, v0, 1.f, at);
  AimIntercept r = aim_intercept(m, o, at, still);
  CHECK_MSG(r.ok && fabs(asin(r.dir[2])) * 57.29578 < 0.3 && fabsf(r.t - 1.f) < 0.02f, "a level launch lands where it flew");
  const float run[3] = { 0.f, 200.f, 0.f }, start[3] = { 900.f, 0.f, -100.f };
  r = aim_intercept(m, o, start, run);
  float lv[3], hit[3];
  aim_launch_vel(m, r.dir, lv);
  aim_ballistic_pos(m, o, lv, r.t, hit);
  const float dx = hit[0] - r.hit[0], dy = hit[1] - r.hit[1], dz = hit[2] - r.hit[2];
  CHECK_MSG(r.ok && sqrtf(dx * dx + dy * dy + dz * dz) < 3.f, "a moving target: the spit's own flight meets it");
  const float beyond[3] = { 3000.f, 0.f, 0.f };
  CHECK_MSG(!aim_intercept(m, o, beyond, still).ok, "past the fuse: no solution");
}

// aim.h (aim.md section 13): the spit leaves about 12 units below the eye; Actor.Velocity reads 0 for
// a Meqon body, so the flight's first velocity is the Location difference, its z put back by the half step of gravity.
static void tb_0564_lob_origin_and_fd_velocity() {
  const float eye[3] = { 10.f, 20.f, 100.f };
  float o[3];
  aim_lob_origin(eye, 1, o);
  CHECK_MSG(o[0] == 10.f && o[1] == 20.f && o[2] == 100.f + AIM_LOB_SPAWN_DZ && AIM_LOB_SPAWN_DZ < 0.f, "a lob: lowered");
  aim_lob_origin(eye, 0, o);
  CHECK_MSG(o[2] == 100.f, "any other gun: the eye");
  const AimProjModel m = aim_lob_model(800.f, 140.f, 0.5f, -950.f, 2500.f, 4.f, 2.f, 1.f);
  const float p0[3] = { 0.f, 0.f, 0.f }, v0[3] = { 800.f, 0.f, 140.f };
  float p1[3], v[3];
  aim_ballistic_pos(m, p0, v0, 0.05f, p1);
  CHECK(aim_fd_velocity(p0, p1, 0.05f, m.gravity, v) && fabsf(v[0] - 800.f) < 1.f && fabsf(v[2] - 140.f) < 2.f);
  CHECK_MSG(!aim_fd_velocity(p0, p1, 0.f, m.gravity, v), "no time between the samples: none");
}

// aim.h: a target with no solution is not solved again for AIM_NOSOLVE_MS (the low-angle scan's cost).
static void tb_0565_nosolve_cache() {
  static AimNoSolve c;
  memset(&c, 0, sizeof c);
  static int objs_[2];  // distinct storage: an optimiser may fold equal unwritten locals
  int& a = objs_[0]; int& b = objs_[1];
  CHECK(!aim_nosolve_recent(&c, &a, 1000));
  aim_nosolve_put(&c, &a, 1000);
  CHECK_MSG(aim_nosolve_recent(&c, &a, 1000 + AIM_NOSOLVE_MS - 1) && !aim_nosolve_recent(&c, &b, 1000),
            "skipped within the window, for that target only");
  CHECK_MSG(!aim_nosolve_recent(&c, &a, 1000 + AIM_NOSOLVE_MS), "solved again after it");
  int many[AIM_NOSOLVE_SLOTS + 1];
  for (int i = 0; i <= AIM_NOSOLVE_SLOTS; i++) aim_nosolve_put(&c, &many[i], 2000u + (unsigned)i);
  CHECK_MSG(c.n == AIM_NOSOLVE_SLOTS && aim_nosolve_recent(&c, &many[AIM_NOSOLVE_SLOTS], 2050), "full: the oldest goes");
}

// arctrace.h: the Nuke Tank frame row, exactly; an actor or Variable that did not resolve leaves its fields empty;
// the column line matches the row; a row that does not fit writes nothing.
static void tb_0566_at_frame_row() {
  AtFrame f;
  memset(&f, 0, sizeof f);
  f.frame = 3; f.gameTime = 10.5f; f.ms = 100.25; f.dtMs = 16.67;
  f.tank.have = 1; f.tank.pos[0] = -7771.f; f.tank.pos[1] = -1210.5f; f.tank.pos[2] = 120.f;
  f.lane = 2; f.laneOff = 5.5f;
  f.soul[0].have = 1; f.soul[0].pos[0] = -7878.f; f.soul[0].pos[1] = -1088.f; f.soul[0].pos[2] = 432.f;
  f.soul[0].hidden = 1; f.soul[0].hasHealth = 1; f.soul[0].health = 100.f;
  f.soul[AT_BOSS].have = 1; f.soul[AT_BOSS].pos[0] = -7874.f; f.soul[AT_BOSS].pos[1] = -1216.f;
  f.soul[AT_BOSS].pos[2] = 496.f; f.soul[AT_BOSS].hasHealth = 1; f.soul[AT_BOSS].health = 300.f;
  f.lives = 3; f.total = 120; f.stage[0] = 1000; f.stage[1] = AT_UNSET; f.stage[2] = 0; f.bigBad = AT_UNSET;
  f.left = 1; f.fire = 1; f.inLeft = 1; f.inFire = 1;
  char b[768];
  const int n = at_fmt_frame(b, sizeof b, f);
  CHECK(n > 0 && n == (int)strlen(b) &&
        strcmp(b, "F,3,10.5000,100.25,16.67,-7771.00,-1210.50,120.00,0,2,5.50,-7878.00,-1088.00,432.00,1,100.0"
                  ",,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,-7874.00,-1216.00,496.00,0,300.0,3,120,1000,,0,,1,0,1,1,0,1\n") == 0);
  int cols = 0, vals = 0;
  for (const char* p = AT_FRAME_COLUMNS; *p; p++) cols += *p == ',';
  for (const char* p = b; *p; p++) vals += *p == ',';
  CHECK_MSG(cols == vals && cols == 62, "the column line matches the row");
  f.tank.have = 0;
  f.lane = -1;
  f.soul[AT_BOSS].hasHealth = 0;
  CHECK(at_fmt_frame(b, sizeof b, f) > 0 && strncmp(b, "F,3,10.5000,100.25,16.67,,,,,,,-7878.00", 39) == 0 &&
        strstr(b, ",496.00,0,,3,120,") != 0);
  vals = 0;
  for (const char* p = b; *p; p++) vals += *p == ',';
  CHECK_MSG(vals == cols, "a missing tank keeps the columns");
  CHECK_MSG(at_fmt_frame(b, 40, f) == 0 && b[0] == 0, "truncated: nothing");
  CHECK(at_fmt_frame(0, 40, f) == 0 && at_fmt_frame(b, 0, f) == 0);
}

// arctrace.h: the transform (the tank's lane and its offset from the lane's Y), the dispatcher start edge, a hit,
// lives, and the unique file name.
static void tb_0567_at_lane_and_rules() {
  const float lanes[5] = { -1088.f, -1152.f, -1216.f, -1280.f, -1344.f };
  int have[5] = { 1, 1, 1, 1, 1 };
  float off = 99.f;
  CHECK(at_lane(lanes, have, 5, -1210.5f, &off) == 2 && fabsf(off - 5.5f) < 1e-4f);
  CHECK_MSG(at_lane(lanes, have, 5, -1184.f, &off) == 1 && fabsf(off + 32.f) < 1e-4f, "a tie: the lower lane");
  CHECK(at_lane(lanes, have, 5, -2000.f, &off) == 4 && fabsf(off + 656.f) < 1e-3f);
  have[4] = 0;
  CHECK_MSG(at_lane(lanes, have, 5, -1344.f, &off) == 3 && fabsf(off + 64.f) < 1e-4f, "an unresolved lane is skipped");
  CHECK(at_lane(lanes, have, 0, -1088.f, &off) == -1 && off == 0.f && at_lane(0, have, 5, 0.f, &off) == -1);
  CHECK(at_disp_started(0, 0, 1, 0) && !at_disp_started(1, 3, 1, 3) && at_disp_started(1, 3, 1, 0) &&
        !at_disp_started(1, 0, 0, 0) && !at_disp_started(0, 5, 0, 0));
  CHECK(at_hit(1, 100.f, 1, 50.f) && !at_hit(1, 50.f, 1, 100.f) && !at_hit(0, 100.f, 1, 50.f) &&
        !at_hit(1, 100.f, 1, 100.f) && !at_hit(1, 100.f, 0, 0.f));
  const int h3[3] = { 1, 1, 1 }, hid[3] = { 0, 1, 1 }, none[3] = { 0, 0, 0 }, h2[3] = { 0, 1, 1 }, hid2[3] = { 1, 0, 0 };
  CHECK(at_lives(h3, hid, 3) == 1 && at_lives(none, hid, 3) == AT_UNSET && at_lives(h2, hid2, 3) == 2);
  wchar_t name[64];
  CHECK(at_unique_name(name, 64, L"x\\", 77u) && wcscmp(name, L"x\\arcade-trace-77.csv") == 0);
  CHECK(!at_unique_name(name, 10, L"x\\", 77u));
}

// arcbot.h: a Nuke Tank frame as the 2026-10-11 trace saw it: five lanes, every soul home, the tank at a Y.
static ArbIn arb_frame(unsigned ms, float tankY) {
  static const float kLane[ARB_LANES] = { -1088.f, -1152.f, -1216.f, -1280.f, -1344.f };
  ArbIn in;
  memset(&in, 0, sizeof in);
  in.active = 1;
  in.nowMs = ms;
  in.frameMs = 6;
  in.haveTank = 1;
  in.tankY = tankY;
  for (int i = 0; i < ARB_LANES; i++) {
    in.laneY[i] = kLane[i];
    in.haveLane[i] = 1;
    in.soul[i] = ArbSoul{ 1, kLane[i], 432.f, 100.f };
  }
  in.soul[ARB_BOMB0] = ArbSoul{ 1, -940.f, 364.f, 100.f };
  in.soul[ARB_BOMB0 + 1] = ArbSoul{ 1, -1493.f, 364.f, 100.f };
  in.soul[ARB_BOSS] = ArbSoul{ 1, -1216.f, 496.f, 99.f };
  return in;
}

// arcbot.h: which souls are live, when an enemy lands, how long the tank takes, the tank's reach.
static void tb_0568_arb_rules() {
  CHECK_MSG(!arb_live(0, ArbSoul{ 1, -1088.f, 432.f, 100.f }), "home: not attacking");
  CHECK(arb_live(0, ArbSoul{ 1, -1088.f, 320.f, 100.f }));
  CHECK_MSG(!arb_live(0, ArbSoul{ 1, -1088.f, 246.f, 97.f }), "hit (100 -> 97): falling dead for ~1 s");
  CHECK(!arb_live(0, ArbSoul{ 0, -1088.f, 320.f, 100.f }));
  CHECK_MSG(!arb_live(ARB_BOMB0, ArbSoul{ 1, -940.f, 364.f, 100.f }) && arb_live(ARB_BOMB0, ArbSoul{ 1, -1022.f, 318.f, 100.f }),
            "a bomb waits at Z 364 and starts at 318");
  CHECK_MSG(!arb_live(ARB_BOSS, ArbSoul{ 1, -1216.f, 496.f, 99.f }) && arb_live(ARB_BOSS, ArbSoul{ 1, -1216.f, 480.f, 99.f }) &&
            !arb_live(ARB_BOSS, ArbSoul{ 1, -1216.f, 480.f, 1.f }), "the boss: below its home, not destroyed");
  CHECK_MSG(fabsf(arb_enemy_land_s(320.f) - 6.9375f) < 1e-3f && arb_enemy_land_s(90.f) == 0.f,
            "320 -> 98 at 32 u/s: the trace's 6.95 s");
  CHECK(fabsf(arb_travel_s(-1216.f, -1088.f) - 128.f / 96.f) < 1e-4f && arb_travel_s(-1088.f, -1088.f) == 0.f);
  CHECK_MSG(ARB_REACH > 24.f + 16.f && ARB_REACH < 64.f, "trigger 24 + the soul's 16, short of the next lane");
  CHECK_MSG(ARB_FIRE_TOL < 7.f, "inside the trace's hits (|off| <= 7.1)");
}

// arcbot.h: the tank goes to the lowest attacker's lane with Left (+Y) or Right (-Y), stops inside the tolerance,
// fires once aligned, holds the shot 80 ms and waits out the gap; a hit soul is not fired at again.
static void tb_0569_arb_aim_and_fire() {
  ArbState s = {};
  ArbIn in = arb_frame(1000, -1216.f);
  in.soul[0].z = 250.f;
  ArbOut o = arb_step(&s, in);
  CHECK(o.press[ARB_LEFT] && !o.press[ARB_RIGHT] && !o.press[ARB_FIRE] && o.decision == ARB_DEC_AIM && o.target == 0);
  in.nowMs = 1006;
  in.tankY = -1215.4f;
  o = arb_step(&s, in);
  CHECK_MSG(o.held[ARB_LEFT] && !o.press[ARB_LEFT] && !o.release[ARB_LEFT], "held each frame, as the key's tick");
  in.nowMs = 2330;
  in.tankY = -1088.3f;
  o = arb_step(&s, in);
  CHECK_MSG(o.release[ARB_LEFT] && o.press[ARB_FIRE], "inside the stop tolerance: released, and aligned: fired");
  in.nowMs = 2330 + ARB_FIRE_HOLD_MS;
  o = arb_step(&s, in);
  CHECK(o.release[ARB_FIRE] && !o.press[ARB_LEFT] && !o.press[ARB_RIGHT]);
  in.nowMs = 2330 + ARB_FIRE_GAP_MS - 1;
  CHECK_MSG(!arb_step(&s, in).press[ARB_FIRE], "the gap");
  in.nowMs = 2330 + ARB_FIRE_GAP_MS;
  CHECK(arb_step(&s, in).press[ARB_FIRE]);
  in.nowMs += ARB_FIRE_HOLD_MS;
  arb_step(&s, in);
  in.soul[0].hp = 97.f;
  in.nowMs += ARB_FIRE_GAP_MS;
  o = arb_step(&s, in);
  CHECK_MSG(!o.press[ARB_FIRE] && o.decision != ARB_DEC_AIM, "hit: no second shot");
  // Right is -Y; past the lane (an overshoot) the hold is released, never reversed in the same frame.
  ArbState r = {};
  ArbIn q = arb_frame(0, -1216.f);
  q.soul[4].z = 300.f;
  CHECK(arb_step(&r, q).press[ARB_RIGHT]);
  q.nowMs = 1500;
  q.tankY = -1350.f;
  o = arb_step(&r, q);
  CHECK_MSG(o.release[ARB_RIGHT] && !o.press[ARB_LEFT], "overshoot: release first");
  q.nowMs = 1506;
  CHECK_MSG(arb_step(&r, q).press[ARB_LEFT], "then back, a frame later");
}

// arcbot.h: the lowest attacker the tank can still reach and shoot before it lands; the boss when nothing else,
// fired at with a lead (it moves at 192 u/s, twice the tank).
static void tb_0570_arb_target_choice() {
  ArbState s = {};
  ArbIn in = arb_frame(0, -1088.f);
  in.soul[4].z = 120.f;   // lands in 0.69 s, 2.7 s away
  in.soul[1].z = 300.f;
  ArbOut o = arb_step(&s, in);
  CHECK_MSG(o.target == 1 && o.press[ARB_RIGHT], "the unreachable one is skipped");
  ArbState s2 = {};
  in = arb_frame(0, -1216.f);
  in.soul[3].z = 200.f;
  in.soul[1].z = 260.f;
  CHECK_MSG(arb_step(&s2, in).target == 3, "the lowest of the reachable");
  // The lead's flight scales with the boss's height (Z 300: 0.18 s at 1000 u/s from Z 120).
  ArbState b = {};
  in = arb_frame(0, -1234.6f);
  in.soul[ARB_BOSS].z = 300.f;
  in.soul[ARB_BOSS].y = -1196.16f;
  o = arb_step(&b, in);
  CHECK(o.decision == ARB_DEC_BOSS && o.target == ARB_BOSS && !o.press[ARB_FIRE]);
  in.nowMs = 20;
  in.soul[ARB_BOSS].y = -1200.f;   // -192 u/s: at the shot's arrival -1234.56
  o = arb_step(&b, in);
  CHECK_MSG(o.press[ARB_FIRE], "led by its velocity over its height");
  // Above the bullet's reach (LifeSpan 0.2 s): aligned, not fired.
  ArbState h = {};
  in = arb_frame(0, -1216.f);
  in.soul[ARB_BOSS].z = 470.f;
  o = arb_step(&h, in);
  CHECK_MSG(o.decision == ARB_DEC_BOSS && !o.press[ARB_FIRE], "out of reach: no shot");
}

// arcbot.h: the dodge. An enemy that will land on the tank before a shot can kill it sends the tank to the nearest
// safe lane; one the tank is aligned with and can shoot in time does not; a bomb's predicted landing counts too.
static void tb_0571_arb_dodge() {
  ArbState s = {};
  s.fired = 1;
  s.lastFireMs = 990;   // just fired (missed): the next shot is 0.39 s away, it lands in 0.55 s
  s.shotSoul = -1;
  ArbIn in = arb_frame(1000, -1216.f);
  in.soul[2].z = 98.f + 0.55f * 32.f;
  ArbOut o = arb_step(&s, in);
  CHECK(o.decision == ARB_DEC_DODGE && o.dodge == ARB_DODGE_ENEMY && o.dodgeSoul == 2 &&
        (o.press[ARB_LEFT] || o.press[ARB_RIGHT]) && !o.press[ARB_FIRE]);
  CHECK_MSG(fabsf(o.goalY + 1152.f) < 0.5f || fabsf(o.goalY + 1280.f) < 0.5f, "a neighbouring lane, 64 u away");
  ArbState k = {};
  o = arb_step(&k, in);
  CHECK_MSG(o.decision == ARB_DEC_AIM && o.press[ARB_FIRE] && !o.press[ARB_LEFT] && !o.press[ARB_RIGHT],
            "shootable in time: shot, not dodged");
  // Bomb02's sweep (+84.7 u/s in Y, -46.9 in Z), about to end 25 u from the tank at the right edge.
  ArbState m = {};
  ArbIn a = arb_frame(980, -1076.f);
  a.soul[ARB_BOMB0 + 1] = ArbSoul{ 1, -1061.694f, 112.938f, 100.f };
  arb_step(&m, a);
  a.nowMs = 1000;
  a.soul[ARB_BOMB0 + 1] = ArbSoul{ 1, -1060.f, 112.f, 100.f };
  o = arb_step(&m, a);
  CHECK(o.decision == ARB_DEC_DODGE && o.dodge == ARB_DODGE_BOMB && o.press[ARB_RIGHT]);
}

// arcbot.h: the player's own input is left to them; every stop releases what the bot holds; a paused clock changes
// nothing.
static void tb_0572_arb_takeover_and_stop() {
  ArbState s = {};
  ArbIn in = arb_frame(0, -1216.f);
  in.soul[0].z = 300.f;
  CHECK(arb_step(&s, in).press[ARB_LEFT]);
  in.nowMs = 10;
  in.userHolds[ARB_LEFT] = 1;
  ArbOut o = arb_step(&s, in);
  CHECK_MSG(o.taken[ARB_LEFT] && !o.release[ARB_LEFT] && !s.down[ARB_LEFT] && o.decision == ARB_DEC_YIELD,
            "the same key: let go without a release (it would clear the player's own)");
  in.nowMs = 20;
  o = arb_step(&s, in);
  CHECK(!o.press[ARB_LEFT] && !o.press[ARB_RIGHT]);
  ArbState r = {};
  ArbIn q = arb_frame(0, -1216.f);
  q.soul[4].z = 300.f;
  CHECK(arb_step(&r, q).press[ARB_RIGHT]);
  q.nowMs = 10;
  q.userHolds[ARB_LEFT] = 1;
  o = arb_step(&r, q);
  CHECK_MSG(o.release[ARB_RIGHT] && !o.taken[ARB_RIGHT], "the other key: released, the player's move goes through");
  // Fire: the player's own Enable exec takes the held shot over.
  ArbState f = {};
  ArbIn w = arb_frame(0, -1088.f);
  w.soul[0].z = 300.f;
  CHECK(arb_step(&f, w).press[ARB_FIRE]);
  w.nowMs = 10;
  w.userPressed = 1;
  o = arb_step(&f, w);
  CHECK(o.taken[ARB_FIRE] && !o.release[ARB_FIRE] && !f.down[ARB_FIRE]);
  // Stops: switch off (inactive), game over, a win. Each releases what is held and presses nothing.
  for (int k = 0; k < 3; k++) {
    ArbState t = {};
    ArbIn p = arb_frame(0, -1216.f);
    p.soul[0].z = 300.f;
    arb_step(&t, p);   // Left held
    p.nowMs = 10;
    p.tankY = -1088.f;
    arb_step(&t, p);   // Left released, Fire held
    p.nowMs = 20;
    p.tankY = -1216.f;
    CHECK(arb_step(&t, p).press[ARB_LEFT] && t.down[ARB_FIRE]);
    p.nowMs = 30;
    if (k == 0) p.active = 0;
    if (k == 1) p.gameOver = 1;
    if (k == 2) p.won = 1;
    o = arb_step(&t, p);
    CHECK(o.release[ARB_LEFT] && o.release[ARB_FIRE] && !o.press[ARB_RIGHT] && !t.down[ARB_LEFT] && !t.down[ARB_FIRE]);
    CHECK(o.decision == (k == 0 ? ARB_DEC_OFF : k == 1 ? ARB_DEC_GAMEOVER : ARB_DEC_WON));
    p.nowMs = 5000;
    o = arb_step(&t, p);
    CHECK_MSG(!o.press[ARB_FIRE] && !o.press[ARB_LEFT] && !o.press[ARB_RIGHT], "stopped: no menu presses either");
  }
  ArbState z = {};
  ArbIn p = arb_frame(0, -1216.f);
  p.soul[0].z = 300.f;
  arb_step(&z, p);
  p.nowMs = 5000;
  p.tankY = -1000.f;
  p.paused = 1;
  o = arb_step(&z, p);
  CHECK_MSG(!o.release[ARB_LEFT] && !o.press[ARB_FIRE] && z.down[ARB_LEFT], "paused: everything stays");
}

// arcbot.h: the menu (title, instructions) is played with Fire once a second after 1.5 s with nothing attacking;
// the switch's config key; a bot row's detail.
static void tb_0573_arb_menu_config_detail() {
  ArbState s = {};
  ArbIn in = arb_frame(0, -1212.f);
  ArbOut o = arb_step(&s, in);
  CHECK(!o.press[ARB_FIRE] && !o.press[ARB_LEFT] && !o.press[ARB_RIGHT]);
  in.nowMs = ARB_MENU_IDLE_MS - 1;
  CHECK(!arb_step(&s, in).press[ARB_FIRE]);
  in.nowMs = ARB_MENU_IDLE_MS;
  o = arb_step(&s, in);
  CHECK(o.press[ARB_FIRE] && o.decision == ARB_DEC_MENU);
  in.nowMs += ARB_FIRE_HOLD_MS;
  CHECK(arb_step(&s, in).release[ARB_FIRE]);
  in.nowMs = ARB_MENU_IDLE_MS + ARB_MENU_GAP_MS - 1;
  CHECK(!arb_step(&s, in).press[ARB_FIRE]);
  in.nowMs = ARB_MENU_IDLE_MS + ARB_MENU_GAP_MS;
  CHECK(arb_step(&s, in).press[ARB_FIRE]);
  config_defaults();
  CHECK(g_cfg.arcadeAuto == 0 && config_key_known("autowin", "arcade"));
  char d[160];
  CHECK(arb_fmt_detail(d, sizeof d, "press left", ARB_DEC_AIM, 0, ARB_DODGE_NONE, -1216.f, -1088.f) > 0 &&
        strcmp(d, "press left aim target e1 dodge none tank -1216.0 goal -1088.0") == 0);
  CHECK(arb_fmt_detail(d, sizeof d, "dodge", ARB_DEC_DODGE, -1, ARB_DODGE_BOMB, -1076.f, -1152.f) > 0 &&
        strcmp(d, "dodge dodge target - dodge bomb tank -1076.0 goal -1152.0") == 0 && !strchr(d, ','));
  CHECK(arb_fmt_detail(d, 8, "press left", ARB_DEC_AIM, 0, 0, 0.f, 0.f) == 0 &&
        arb_fmt_detail(0, 8, "x", 0, 0, 0, 0.f, 0.f) == 0);
}

// arcbot.h: no Fire press in the frame Fire is released; a target whose path crosses a lane with a
// landing before the tank passes is rejected; the start and fire tolerances grow with half a frame's travel.
static void tb_0574_arb_review_rules() {
  ArbState s = {};
  ArbIn in = arb_frame(10000, -1088.f);
  in.soul[0].z = 250.f;
  s.down[ARB_FIRE] = 1;
  s.downMs[ARB_FIRE] = 10000 - ARB_FIRE_GAP_MS;
  s.fired = 1;
  s.lastFireMs = 10000 - ARB_FIRE_GAP_MS;
  ArbOut o = arb_step(&s, in);
  CHECK_MSG(o.release[ARB_FIRE] && !o.press[ARB_FIRE], "released: no press the same frame (the edges would cancel)");
  in.nowMs = 10006;
  CHECK_MSG(arb_step(&s, in).press[ARB_FIRE], "the next frame");
  // Lane 1 lands in 0.5 s; the tank at lane 0 would pass it on the way to lane 3 (within reach 0.21-1.13 s).
  ArbState p = {};
  ArbIn q = arb_frame(0, -1088.f);
  q.soul[3].z = 300.f;
  q.soul[1].z = ARB_TOUCH_Z + 0.5f * ARB_ENEMY_V;
  o = arb_step(&p, q);
  CHECK_MSG(o.target != 3 && !o.press[ARB_RIGHT], "the path crosses a landing: rejected");
  // A 100 ms frame (half a frame's travel 4.8 u): 4.5 u off is settled and aligned.
  ArbState f = {};
  ArbIn w = arb_frame(0, -1083.5f);
  w.frameMs = 100;
  w.soul[0].z = 250.f;
  o = arb_step(&f, w);
  CHECK(!o.press[ARB_LEFT] && !o.press[ARB_RIGHT] && o.press[ARB_FIRE]);
  ArbState g = {};
  w.frameMs = 6;
  o = arb_step(&g, w);
  CHECK_MSG(o.press[ARB_RIGHT] && !o.press[ARB_FIRE], "a 6 ms frame: the fixed tolerances");
}

// Silent aim on mounted guns (docs/re/turrets.md section 8): which hook brackets which gun.
static void tb_0575_ms_hooks() {
  CHECK(ms_hook_brackets(MS_HOOK_FIREEFFECTS, AIM_TUR_MG42) && ms_hook_brackets(MS_HOOK_FIREEFFECTS, AIM_TUR_LAUNCHER));
  CHECK_MSG(!ms_hook_brackets(MS_HOOK_FIREEFFECTS, AIM_TUR_SUPER), "the SuperTurret's FireEffects fires no shot");
  CHECK(ms_hook_brackets(MS_HOOK_BEGINSTATE, AIM_TUR_SUPER) && ms_hook_brackets(MS_HOOK_ANIMEND, AIM_TUR_SUPER));
  CHECK(!ms_hook_brackets(MS_HOOK_BEGINSTATE, AIM_TUR_MG42) && !ms_hook_brackets(MS_HOOK_ANIMEND, AIM_TUR_LAUNCHER));
  CHECK(!ms_hook_brackets(MS_HOOK_FIREEFFECTS, AIM_TUR_NONE) && !ms_hook_brackets(MS_HOOK_COUNT, AIM_TUR_SUPER));
  CHECK_MSG(ms_uses_yaw_bone(AIM_TUR_MG42) && ms_uses_yaw_bone(AIM_TUR_SUPER) && !ms_uses_yaw_bone(AIM_TUR_LAUNCHER),
            "the launcher's yaw is CalcedViewRotation's");
  CHECK(strcmp(ms_hook_label(MS_HOOK_ANIMEND), "AnimCallback_AnimEnd") == 0 && strcmp(ms_hook_label(9), "?") == 0);
  // The gate: a mounted gun is redirected unless it cannot be bracketed.
  CHECK(aim_silent_gate(AIM_GO, 1, 1) == AIM_GO && aim_silent_gate(AIM_GO, 1, 0) == AIM_G_MOUNTED);
  CHECK(aim_silent_gate(AIM_GO, 0, 0) == AIM_GO && aim_silent_gate(AIM_G_PANEL, 1, 1) == AIM_G_PANEL);
}

// The rotator written: pitch alone (MG42, SuperTurret) or whole (launcher), the short way round, roll kept.
static void tb_0576_ms_rot_write() {
  const int cur[3] = { 65000, 70000, 12 };
  int out[3];
  ms_rot_write(cur, 500, 1000, 0, out);
  CHECK_MSG(out[0] == 66036 && out[1] == 70000 && out[2] == 12, "pitch only: 65000 -> 500 the short way, yaw kept");
  ms_rot_write(cur, -2000, 4464, 1, out);
  CHECK_MSG(aim_wrap16(out[0]) == -2000 && out[1] == 70000 && out[2] == 12,
            "whole: yaw 70000 (4464) unchanged when wanted");
  ms_rot_write(cur, 0, 4000, 1, out);
  CHECK(aim_wrap16(out[1]) == 4000 && out[1] - cur[1] == -464);
}

// RotateYaw for the wanted muzzle yaw, inside the gun's limits (a target outside them is not redirected).
static void tb_0577_ms_yaw_plan() {
  const AimTurretLimits l = { -8192, 8192, -8192, 8192, 1 };
  int r = 0;
  CHECK_MSG(ms_yaw_plan(1000, 17384, 18384, l, &r) && r == 2000, "the bone's world yaw follows RotateYaw");
  CHECK_MSG(ms_yaw_plan(8000, 0, 150, l, &r) && r == 8150, "within the limit");
  CHECK_MSG(!ms_yaw_plan(8000, 0, 300, l, &r), "past MaxYaw by more than the slack: not redirected");
  CHECK_MSG(ms_yaw_plan(8000, 0, 240, l, &r) && r == 8192, "within the slack: clamped to MaxYaw");
  CHECK_MSG(ms_yaw_plan(-8000, 65000, 64800, l, &r) && r == -8192, "the short way round, clamped to MinYaw");
  AimTurretLimits free = l;
  free.clampYaw = 0;
  CHECK_MSG(ms_yaw_plan(30000, 0, 16000, free, &r) && r == 46000, "no bClampYaw: no yaw limit");
}

// The second step by the measured slope: a sign the model got wrong is corrected, a wild slope is not followed.
static void tb_0578_ms_yaw_secant() {
  const AimTurretLimits l = { -8192, 8192, -8192, 8192, 1 };
  int r = 0;
  CHECK_MSG(!ms_yaw_secant(0, 100, 500, 590, 600, l, &r), "residual within the tolerance: done");
  CHECK_MSG(ms_yaw_secant(0, 100, 500, -400, 600, l, &r) && r == -500, "the bone turned the other way: slope -1");
  CHECK_MSG(ms_yaw_secant(0, 100, 500, 350, 600, l, &r) && r == 1000, "half the turn: slope 0.5, doubled");
  CHECK_MSG(!ms_yaw_secant(0, 100, 500, 110, 600, l, &r), "the bone barely moved (bCanFire false?): no step");
  CHECK_MSG(!ms_yaw_secant(0, 100, 0, 100, 600, l, &r), "no change in RotateYaw: no slope");
  CHECK_MSG(!ms_yaw_secant(7000, 100, 8000, 1100, 9000, l, &r), "the next step past MaxYaw: not taken");
}

// The bracket: put back only what is still ours, by the hook that opened it; nested hooks write nothing.
static void tb_0579_ms_bracket() {
  MsBracket b;
  memset(&b, 0, sizeof b);
  int gun = 0, other = 0, fn[MS_HOOK_COUNT] = { 0, 0, 0 };
  const int cur[3] = { 100, 200, 0 }, wr[3] = { 900, 200, 0 };
  MsRestore r;
  CHECK(ms_open(&b, MS_HOOK_FIREEFFECTS, &fn[MS_HOOK_FIREEFFECTS], &gun, cur, wr, 1, 50, 700));
  CHECK_MSG(!ms_open(&b, MS_HOOK_BEGINSTATE, &fn[MS_HOOK_BEGINSTATE], &gun, cur, wr, 1, 50, 700) && b.nested == 1, "nested: nothing written");
  CHECK_MSG(!ms_close(&b, MS_HOOK_BEGINSTATE, &fn[MS_HOOK_BEGINSTATE], &gun, wr, 1, 700, 1, &r), "an inner hook does not close it");
  CHECK_MSG(!ms_close(&b, MS_HOOK_FIREEFFECTS, &fn[MS_HOOK_FIREEFFECTS], &other, wr, 1, 700, 1, &r), "another gun's does not");
  ms_yaw_wrote(&b, 720);
  CHECK(ms_close(&b, MS_HOOK_FIREEFFECTS, &fn[MS_HOOK_FIREEFFECTS], &gun, wr, 1, 720, 1, &r) && r.rot && r.yaw && r.yawV == 50 && r.rotV[0] == 100);
  CHECK(!b.open && b.opened == 1 && b.closed == 1);
  CHECK(ms_open(&b, MS_HOOK_ANIMEND, &fn[MS_HOOK_ANIMEND], &gun, cur, wr, 1, 50, 700));
  const int moved[3] = { 905, 200, 0 };
  CHECK(ms_close(&b, MS_HOOK_ANIMEND, &fn[MS_HOOK_ANIMEND], &gun, moved, 1, 701, 1, &r));
  CHECK_MSG(!r.rot && !r.yaw && b.rotNotOurs == 1 && b.yawNotOurs == 1, "changed meanwhile: the game's, kept");
  CHECK(ms_open(&b, MS_HOOK_FIREEFFECTS, &fn[MS_HOOK_FIREEFFECTS], &gun, cur, wr, 0, 0, 0));
  CHECK_MSG(ms_close(&b, MS_HOOK_FIREEFFECTS, &fn[MS_HOOK_FIREEFFECTS], &gun, wr, 0, 0, 0, &r) && !r.rot && !r.yaw && b.yawNotOurs == 1,
            "unread now: not written back; no yaw in this bracket");
  CHECK(ms_open(&b, MS_HOOK_FIREEFFECTS, &fn[MS_HOOK_FIREEFFECTS], &gun, cur, wr, 1, 50, 700));
  CHECK_MSG(ms_frame_start(&b, wr, 1, 700, 1, &r) && r.rot && r.yaw && b.leftOpen == 1 && !b.open,
            "left open: closed at the next frame start");
  CHECK(!ms_frame_start(&b, wr, 1, 700, 1, &r));
}

// A shot inside the bracket, by the gun's own counter.
// The same hook raised again inside its own bracket: only the matching after-call closes it; a
// second step equal to the first is not taken.
static void tb_0581_ms_nested_same_fn() {
  MsBracket b;
  memset(&b, 0, sizeof b);
  int gun = 0, fnA = 0, fnB = 0;
  const int cur[3] = { 100, 200, 0 }, wr[3] = { 900, 200, 0 };
  MsRestore r;
  CHECK(ms_open(&b, MS_HOOK_BEGINSTATE, &fnA, &gun, cur, wr, 1, 50, 700));
  CHECK(!ms_open(&b, MS_HOOK_BEGINSTATE, &fnA, &gun, cur, wr, 1, 50, 700));
  CHECK(!ms_open(&b, MS_HOOK_BEGINSTATE, &fnB, &gun, cur, wr, 1, 50, 700) && b.nested == 2);
  CHECK_MSG(!ms_close(&b, MS_HOOK_BEGINSTATE, &fnB, &gun, wr, 1, 700, 1, &r), "another function's after-call");
  CHECK_MSG(!ms_close(&b, MS_HOOK_BEGINSTATE, &fnA, &gun, wr, 1, 700, 1, &r) && b.open,
            "the inner call of the same function does not close it");
  CHECK_MSG(ms_close(&b, MS_HOOK_BEGINSTATE, &fnA, &gun, wr, 1, 700, 1, &r) && !b.open && r.rot && r.yaw,
            "the opening call's after closes it");
  CHECK(ms_open(&b, MS_HOOK_BEGINSTATE, &fnA, &gun, cur, wr, 1, 50, 700));
  CHECK(!ms_open(&b, MS_HOOK_BEGINSTATE, &fnA, &gun, cur, wr, 1, 50, 700));
  CHECK_MSG(ms_frame_start(&b, wr, 1, 700, 1, &r) && !b.open, "left open with a nested call: closed at the frame");
  CHECK_MSG(ms_open(&b, MS_HOOK_BEGINSTATE, &fnA, &gun, cur, wr, 1, 50, 700) &&
            ms_close(&b, MS_HOOK_BEGINSTATE, &fnA, &gun, wr, 1, 700, 1, &r), "a fresh bracket starts with no depth");
  const AimTurretLimits l = { -8192, 8192, -8192, 8192, 1 };
  int y = 0;
  CHECK_MSG(!ms_yaw_secant(7000, 0, 8192, 1000, 1040, l, &y), "clamped to MaxYaw, where the first step already was");
}

static void tb_0580_ms_shot_seen() {
  CHECK(ms_shot_seen(AIM_TUR_MG42, 0, 1, -1, -1) == 1 && ms_shot_seen(AIM_TUR_LAUNCHER, 2, 2, -1, -1) == 0);
  CHECK(ms_shot_seen(AIM_TUR_MG42, -9, 1, -1, -1) == -1);
  CHECK(ms_shot_seen(AIM_TUR_SUPER, -9, -9, 0, 1) == 1 && ms_shot_seen(AIM_TUR_SUPER, -9, -9, 1, 1) == 0);
  CHECK(ms_shot_seen(AIM_TUR_SUPER, 0, 1, -1, 1) == -1);
}

// photofolder.h: only a drive-letter absolute path is a Photo folder. A UNC path would open an SMB connection (and
// send the user's NTLM hash) when a photo is taken; a \\?\ or \\.\ path reaches devices. Both fall back to the default.
// The paths are spelled with '|' for '\' and '~' for '/': the publish gate reads literal drive and UNC paths as PII.
static std::string tb_path(const char* s) {
  std::string r(s);
  for (char& c : r) c = c == '|' ? '\\' : c == '~' ? '/' : c;
  return r;
}
static void tb_0582_photo_folder_path_ok() {
  for (const char* p : { "C:|Shots", "d:~shots", "Z:|", "C:|Users|Me|Pictures|Nuke Panel" })
    CHECK_MSG(photo_folder_path_ok(tb_path(p).c_str()), p);
  for (const char* p : { "||host|share", "~~host~share", "||?|C:|Shots", "||.|C:", "||?|UNC|host|s",
                         "Shots", "C:Shots", "C:", "1:|x", "C:|a:b", "", " C:|x" })
    CHECK_MSG(!photo_folder_path_ok(tb_path(p).c_str()), p);
  CHECK(!photo_folder_path_ok(0));
}
// photo.h: a photo never overwrites a file; the next free name gets "-n" before ".png".
static void tb_0583_photo_file_name_n() {
  char out[64];
  CHECK(photo_file_name_n("e1l1 2026-10-07 09-05-03 7680x4320.png", 1, out, sizeof out) > 0 &&
        strcmp(out, "e1l1 2026-10-07 09-05-03 7680x4320.png") == 0);
  CHECK(photo_file_name_n("e1l1 2026-10-07 09-05-03 7680x4320.png", 2, out, sizeof out) > 0 &&
        strcmp(out, "e1l1 2026-10-07 09-05-03 7680x4320-2.png") == 0);
  CHECK(photo_file_name_n("a.png", 9, out, sizeof out) == 7 && strcmp(out, "a-9.png") == 0);
  CHECK_MSG(photo_file_name_n("a.png", 2, out, 7) < 0 && out[0] == 0, "never a cut name");
  CHECK_MSG(photo_file_name_n("a.txt", 2, out, sizeof out) < 0 && out[0] == 0, "only a .png name");
  CHECK(PHOTO_NAME_TRIES >= 2 && PHOTO_NAME_TRIES <= 64);
}
// scallparms.h: ProcessEvent writes up to ParmsSize bytes into the block, so a block smaller than that is refused.
static void tb_0584_scall_parms_fit() {
  unsigned char p[16];
  CHECK(scall_parms_fit(16, p, sizeof p) && scall_parms_fit(4, p, sizeof p));
  CHECK_MSG(!scall_parms_fit(17, p, sizeof p), "ParmsSize past the block");
  CHECK_MSG(!scall_parms_fit(4, 0, 64), "parms needed and none given");
  CHECK_MSG(scall_parms_fit(0, 0, 0) && scall_parms_fit(0, p, 0), "a function without parms");
}
// ini.h / config.cpp: a line over INI_LINE_MAX - 1 bytes is skipped whole, never cut into a second line that could
// be read as another key; the DllMain peek agrees. A line of exactly INI_LINE_MAX - 1 bytes is still read.
static void tb_0585_ini_overlong_line() {
  const std::string tail = "bigpackage=0\r\n";
  const std::string cut = "[extras]\r\n; " + std::string(INI_LINE_MAX - 3, 'a') + tail;   // the tail starts at 511
  const std::string fits = "[extras]\r\nbigpackage=0" + std::string(INI_LINE_MAX - 1 - 12, ' ') + "\r\n";
  const std::string over = "[extras]\r\nbigpackage=0" + std::string(INI_LINE_MAX - 12, ' ') + "\r\n";
  const std::string after = "[extras]\r\n; " + std::string(700, 'a') + "\r\nbigpackage=0\r\n";
  const struct { const std::string* text; int want; } kCases[] = { { &cut, 1 }, { &fits, 0 }, { &over, 1 },
                                                                   { &after, 0 } };
  char path[MAX_PATH];
  temp_ini(path, sizeof path, "nukepanel_overlong");
  for (const auto& c : kCases) {
    const int peek = ini_peek_int(c.text->c_str(), (int)c.text->size(), "extras", "bigpackage", 0, 1, 1);
    write_text(path, c.text->c_str());
    config_load_from(path);
    if (peek != c.want || g_cfg.bigPackage != c.want)
      printf("  %u bytes: peek %d, config_load_from %d, want %d\n", (unsigned)c.text->size(), peek, g_cfg.bigPackage,
             c.want);
    CHECK_MSG(peek == c.want && g_cfg.bigPackage == c.want, "an overlong line is skipped whole, in both readers");
  }
  DeleteFileA(path);
}

int main() {
  tb_0001_logfmt_short_line_ends_in_newline();
  tb_0002_logfmt_overlong_line_truncates_without_nul();
  tb_0003_logfmt_tiny_cap_still_newline();
  tb_0004_hostgate_only_the_game_exe();
  tb_0005_paths_join_beside_module();
  tb_0006_stock_digest_hex_compare();
  tb_0007_hook_prologue_accepts_foreign_jmp();
  tb_0008_hook_prologue_len_whole_instructions();
  tb_0009_d3dres_reset_lifetime();
  tb_0010_primbatch_flush_on_texture_or_full();
  tb_0011_d3dres_device_replaced_releases_all();
  tb_0012_cursor_gate_and_keyhold();
  tb_0013_cursor_gate_intents();
  tb_0014_cursor_scale_maps_client_to_render();
  tb_0015_clip_rect_inside_is_unchanged();
  tb_0016_clip_rect_fully_outside_returns_zero();
  tb_0017_clip_rect_half_left_moves_u_halfway();
  tb_0018_clip_rect_all_four_sides();
  tb_0019_clip_rect_zero_area_clip_returns_zero();
  tb_0020_clip_poly_triangle_half_clipped();
  tb_0021_panel_layout();
  tb_0022_panel_box_clamps_1280x720_and_3440x1440();
  tb_0023_ui_panel_max_h_for();
  tb_0024_bind_key_is_live_matches_label();
  tb_0025_bind_hold_is_active_requires_live_key();
  tb_0026_bind_menu_key_effective_matches_live();
  tb_0027_bind_swallows_requires_live_key_and_window();
  tb_0028_anim_core();
  tb_0029_menu_scroll_rules();
  tb_0030_panel_key_effective_is_a_key();
  tb_0031_config_missing_file_gives_defaults();
  tb_0032_config_garbage_value_gives_default();
  tb_0033_config_unknown_key_survives_write();
  tb_0034_panel_min_h_is_aas();
  tb_0035_menu_tab_rail_shows_tabs_with_rows();
  tb_0036_entry_vk_events();
  tb_0037_entry_commit_and_clamp();
  tb_0038_config_keys_are_this_ports();
  tb_0039_menu_collapse_keeps_ordinals();
  tb_0040_ini_peek_int();
  tb_0041_menu_tab_rail();
  tb_0042_menu_fold_ease();
  tb_0043_menu_scroll_cue();
  tb_0044_fontpack_rules();
  tb_0045_canvas_atlas_step();
  tb_0046_ui_scale_settled_and_px();
  tb_0047_ui_scale_ceiling();
  tb_0048_slider_geometry_uniform();
  tb_0049_capped_panel_drag();
  tb_0050_tip_wrap();
  tb_0051_tip_place();
  tb_0052_framestats_reports_after_one_second();
  tb_0053_hitch_thresholds();
  tb_0054_framestats_avg_any_window();
  tb_0055_stall_policy();
  tb_0056_stall_long_frames();
  tb_0057_stall_walk();
  tb_0058_stall_fmt_addr();
  tb_0059_stall_find_module();
  tb_0060_slowopen_rule();
  tb_0061_slowopen_paths();
  tb_0062_fc_next_deadline_cases();
  tb_0063_fc_wait_ticks_zero_when_past();
  tb_0064_fc_pacing_64_frames_at_175hz();
  tb_0065_fc_effective_fps_sentinel_rule();
  tb_0066_fc_cap_clamp_vsync_cases();
  tb_0067_bp_gate();
  tb_0068_bp_cdo_checks();
  tb_0069_bp_gate_clock();
  tb_0070_atlas_white_block();
  tb_0071_dimouse_rule();
  tb_0072_crash_skip_codes();
  tb_0073_crash_log_cap();
  tb_0074_crash_av_kind();
  tb_0075_crash_fmt_where();
  tb_0076_dimouse_kind_and_zero();
  tb_0077_dimouse_seen_at_open();
  tb_0078_cursor_shape_restored_on_close();
  tb_0079_config_write_backoff();
  tb_0080_panel_open_refused_while_drawing_disabled();
  tb_0081_d3d_body_verdict();
  tb_0082_uw_isa_depth_cap_and_cycle();
  tb_0083_uw_find_prop_children_then_super();
  tb_0084_uw_pawn_list_stops();
  tb_0085_view_w2s();
  tb_0086_uw_read_health_typed();
  tb_0087_world_gate();
  tb_0120_uw_alive_on_raw_float();
  tb_0122_world_scan_backoff();
  tb_0088_esp_box_from_corners();
  tb_0089_esp_pawn_box();
  tb_0090_esp_markers();
  tb_0091_esp_health_label();
  tb_0092_marker_pipeline();
  tb_0093_esp_distance();
  tb_0094_esp_label_outline();
  tb_0095_esp_names();
  tb_0096_esp_config_defaults();
  tb_0097_dmg_progress_rise_alpha();
  tb_0098_dmg_ring();
  tb_0099_dmgtrack_steps();
  tb_0100_dmgtrack_bounds();
  tb_0123_dmgtrack_incomplete_and_bulk();
  tb_0130_esp_pawn_silhouette();
  tb_0124_view_effective_hfov();
  tb_0125_world_paused_gate();
  tb_0126_world_attitude();
  tb_0127_dmg_text_scale();
  tb_0128_esp_draw_distance();
  tb_0131_world_rel_source();
  tb_0132_world_fov_log();
  tb_0133_world_wants_pawns();
  tb_0134_ms_window();
  tb_0135_scallparms_put();
  tb_0136_optrestore();
  tb_0137_opt_step();
  tb_0138_fov_rules();
  tb_0139_gs_settings();
  tb_0140_accuracy_rules();
  tb_0141_fov_release_after_reload();
  tb_0142_fov_clear_carried();
  tb_0143_menu_live_count();
  tb_0144_accuracy_probe();
  tb_0145_recoil_rules();
  tb_0160_bl_pick_monitor();
  tb_0161_bl_rewrite_pp();
  tb_0162_bl_style_and_pos();
  tb_0163_borderless_peek();
  tb_0164_bl_bg_hold();
  tb_0180_cvar_hold();
  tb_0181_focus_flag();
  tb_0182_postfx_config();
  tb_0170_pg_builtin_table();
  tb_0171_pg_chapter_rows();
  tb_0172_pg_completion_bits();
  tb_0173_pg_totals();
  tb_0174_pg_refresh_rule();
  tb_0175_pg_missing_keys();
  tb_0176_pg_map_and_text();
  tb_0177_pg_diff_labels();
  tb_0178_pg_found_gate();
  tb_0179_pg_scan_backoff_and_span();
  tb_0230_eg_giver_filter();
  tb_0231_eg_cache_rule();
  tb_0183_raw_turn();
  tb_0184_shake_filter();
  tb_0185_pe_dispatch();
  tb_0186_cl_coop_level();
  tb_0187_cl_hold_rule();
  tb_0188_cl_raw_row();
  tb_0200_world_classify_by_target();
  tb_0201_world_att_log_key();
  tb_0202b_world_downed();
  tb_0202_dmgtrack_knockdown_and_left();
  tb_0203_world_alive_set();
  tb_0190_postfx_uber_levers();
  tb_0191_postfx_game_off();
  tb_0193_gs_audio_rate();
  tb_0194_gs_audio_site();
  tb_0210_pg_scope_split();
  tb_0211_aw_mash_rule();
  tb_0212_aw_use_rule();
  tb_0213_aw_helpers();
  tb_0214_aw_config();
  tb_0220_world_boss_meter();
  tb_0221_aw_pool_rule();
  tb_0222_eg_awards_open();
  tb_0240_ws_rescan_rule();
  tb_0241_ws_cache();
  tb_0242_it_filter();
  tb_0243_it_hide_rule();
  tb_0244_esp2_config();
  tb_0245_bk_filter();
  tb_0246_bk_alive();
  tb_0247_bk_label();
  tb_0248_bk_config();
  tb_0249_bk_family();
  tb_0255_mb_local_ok();
  tb_0256_mb_pivots();
  tb_0257_mb_corners();
  tb_0258_mb_screen_box();
  tb_0259_mb_marker_box();
  tb_0260_eg_monitor_wired();
  tb_0265_dv_plan();
  tb_0266_dv_config();
  tb_0267_dv_refusal();
  tb_0270_aex_target();
  tb_0271_aex_one_at_a_time();
  tb_0272_aex_config();
  tb_0280_aim_target_rule();
  tb_0281_aim_shake_delta();
  tb_0282_aim_bracket();
  tb_0283_aim_lead();
  tb_0284_aim_gates();
  tb_0285_aim_config();
  tb_0286_tbot_condition();
  tb_0287_tbot_press_pairing();
  tb_0288_tbot_ally();
  tb_0289_tbot_config();
  tb_0290_tbot_locked_cover();
  tb_0291_tbot_release_backstop();
  tb_0300_aimbot_yaw_wrap();
  tb_0301_aimbot_pitch_clamp();
  tb_0302_aimbot_smoothing();
  tb_0303_aim_pick_cone();
  tb_0304_aimbot_hold();
  tb_0305_tbot_aimbot_mode();
  tb_0306_aimbot_config();
  tb_0307_aim_cone_pick_keep();
  tb_0308_aim_marker_aimbot();
  tb_0309_aimbot_wide_limits();
  tb_0320_tbot_fire_mode();
  tb_0321_tbot_pulse_press();
  tb_0322_tbot_attached_pawn();
  tb_0323_tbot_locked_follows_aimbot();
  tb_0324_aim_pick_hysteresis();
  tb_0325_spawnbox_offset();
  tb_0326_spawnbox_rate();
  tb_0327_tbot_up_retry();
  tb_0328_spawnbox_flags();
  tb_0329_spawnbox_exclusion();
  tb_0329b_spawnbox_teleport();
  tb_0329c_spawnbox_nodamage_watch();
  tb_0329d_spawnbox_role_probe();
  tb_0329e_spawnbox_noeffect();
  tb_0329f_spawnbox_health_floor();
  tb_0329g_spawnbox_invincible();
  tb_0329h_spawnbox_dmgscale();
  tb_0330_aim_point_order();
  tb_0331_aim_hit_category();
  tb_0332_aim_shot_trace();
  tb_0340_freecam_flight();
  tb_0341_freecam_look_rotator();
  tb_0342_freecam_pause();
  tb_0343_freecam_cvars_exits();
  tb_0344_freecam_bind_refused();
  tb_0345_freecam_view_text();
  tb_0346_freecam_config();
  tb_0347_pe_after_phase();
  tb_0348_flight_gate();
  tb_0349_teleport_place();
  tb_0350_teleport_verdict();
  tb_0351_photo_tiles();
  tb_0352_photo_rows();
  tb_0353_photo_folder_config();
  tb_0354_photo_steps_cancel();
  tb_0355_freecam_pause_world_drop();
  tb_0356_photo_budget();
  tb_0357_freecam_no_fog();
  tb_0358_freecam_frame_clear();
  tb_0359_d3d_frame_trace();
  tb_0360_rtclear_first_bind();
  tb_0361_freecam_bright_cull();
  tb_0362_freecam_alt_tab_keeps_flight();
  tb_0363_freecam_frame_zone();
  tb_0364_freecam_rendmap();
  tb_0365_freecam_zone_reasons();
  tb_0366_freecam_stuck_frames();
  tb_0367_render_watch_engine_binds();
  tb_0368_render_watch_steps();
  tb_0369_freecam_bright_research();
  tb_0370_frame_record_slots();
  tb_0371_frame_record_arming();
  tb_0372_render_target_heal();
  tb_0373_stuck_net_delay();
  tb_0374_freecam_rendmap_carried();
  tb_0380_weap_slider_stops_and_labels();
  tb_0381_handling_factor_per_state();
  tb_0382_handling_lockout_timing();
  tb_0383_handling_restore_rule();
  tb_0384_handling_config_defaults();
  tb_0385_handling_rate_verdict();
  tb_0390_cl_phone_id();
  tb_0391_cl_helmet_letter_and_join();
  tb_0392_cl_unfound_rule();
  tb_0393_cl_hidden_rule();
  tb_0394_cl_pick_helmets();
  tb_0395_cl_labels();
  tb_0396_pg_chapter_collectibles();
  tb_0397_collect_config_defaults();
  tb_0398_ws_once_per_level();
  tb_0400_ar_confirm_rule();
  tb_0401_ar_reset_needs_backup();
  tb_0402_ar_time_text();
  tb_0403_ar_file_name();
  tb_0404_ar_clean();
  tb_0405_ar_order();
  tb_0406_ar_backup_text();
  tb_0407_ar_disarm_off_screen();
  tb_0408_ar_confirm_min_gap();
  tb_0409_ar_reset_expires();
  tb_0410_photo_margin_math();
  tb_0411_photo_capture_cvars();
  tb_0412_photo_bands();
  tb_0413_freecam_wheel_fov();
  tb_0414_melee_kind();
  tb_0415_melee_reach();
  tb_0416_aimbot_out_of_ammo();
  tb_0417_tbot_melee();
  tb_0418_dmg_atlas_pct();
  tb_0419_dmg_text_path();
  tb_0420_turret_gate();
  tb_0421_turret_kind();
  tb_0422_turret_limits();
  tb_0423_tbot_mounted_press();
  tb_0424_aimbot_mounted_instant_snap();
  tb_0425_mount_log_debounce();
  tb_0426_pg_pick_map_name();
  tb_0427_ac_hostile_live();
  tb_0428_ac_wasp_never_hostile();
  tb_0429_ac_damageable();
  tb_0430_ac_weapon_hurts();
  tb_0431_ac_pick_order();
  tb_0432_ac_cap_box_config();
  tb_0433_ac_keep_ship_on_unknown_pawn();
  tb_0434_handling_eager_lockout();
  tb_0435_handling_fire_trace_rule();
  tb_0436_eg_poker_cigar_givers();
  tb_0437_eg_event_rules();
  tb_0438_aw_poker_abortion_kinds();
  tb_0439_aw_whack_rule();
  tb_0440_aw_aa_game();
  tb_0441_aw_aa_result_and_read();
  tb_0442_pe_any_thread();
  tb_0443_bp_reapply_rule();
  tb_0444_pg_phone_ids_partition();
  tb_0445_cfg_reads_pause_while_loading();
  tb_0446_ac_ship_target_stable();
  tb_0447_tw_levers();
  tb_0448_tw_anim_step();
  tb_0449_tw_spread_step();
  tb_0450_tw_shot_rate();
  tb_0451_tw_apply_cap();
  tb_0452_ok_expected_damage();
  tb_0453_ok_clear_rules();
  tb_0454_ok_scope();
  tb_0455_ok_exclusion_precedence();
  tb_0456_ok_shot_lock();
  tb_0457_ok_scan_due();
  tb_0458_ok_scan_activity();
  tb_0459_ic_straight();
  tb_0460_ic_ballistic();
  tb_0461_ic_turret_model_config();
  tb_0462_ic_turret_lob_limits();
  tb_0463_spawnbox_dmgtype();
  tb_0464_fs_watch();
  tb_0465_cg_show_redundant();
  tb_0466_cl_hold_watch();
  tb_0467_beam_reach_cap();
  tb_0468_spawnbox_cutscene_target();
  tb_0469_tbot_box_and_vis_drop();
  tb_0470_bk_crate_rule();
  tb_0471_aim_ptlog();
  tb_0472_aim_bullet_passes();
  tb_0473_mc_helper_choice();
  tb_0474_mc_union_box();
  tb_0475_mc_velocity();
  tb_0476_mc_body_without_helpers();
  tb_0477_mc_tiebreak();
  tb_0478_mc_hysteresis();
  tb_0479_mc_vel_position_changes();
  tb_0480_ah_map_match();
  tb_0481_ah_listing();
  tb_0482_ah_kind_match();
  tb_0483_ah_modes();
  tb_0484_ah_sort_progress_label();
  tb_0485_ah_live_texts_budget();
  tb_0486_ach_config_keys();
  tb_0487_ah_scan_wanted();
  tb_0488_ah_kind_here_pending_ships();
  tb_0489_menu_rail_fits_names();
  tb_0490_menu_live_table();
  tb_0491_pb_local_transform();
  tb_0492_pb_rows();
  tb_0493_pb_rules();
  tb_0494_pbb_zone();
  tb_0495_pbb_fire_hold_rearm();
  tb_0496_pbb_both_and_upper();
  tb_0497_pbb_cradle_cap();
  tb_0498_pbb_plunger();
  tb_0499_pbb_stop_releases();
  tb_0500_pbb_yield_user();
  tb_0501_pbb_outcome();
  tb_0502_pbb_detail_rate();
  tb_0503_pbb_autowin_and_config();
  tb_0504_pbb_takeover_side();
  tb_0505_pbb_plunge_drop();
  tb_0506_pb_file_cap();
  tb_0507_pbb_window_choice();
  tb_0508_pbb_cradle_states();
  tb_0509_pbb_cradle_ends();
  tb_0510_pbb_tally();
  tb_0511_pbb_left_aim();
  tb_0512_pb_drain_cause();
  tb_0513_pbb_takeover_judging_cradle_ends();
  tb_0514_pb_open_choice_and_header();
  tb_0515_pbb_game_clock();
  tb_0516_pb_speed_stats();
  tb_0517_ac_lost_reason();
  tb_0518_gun_view_limits();
  tb_0519_mounted_turn_converges();
  tb_0520_tbot_reload_wait();
  tb_0521_bk_path();
  tb_0522_ah_mode_after_earned();
  tb_0523_ach_key_base10();
  tb_0524_ah_aim_activation();
  tb_0525_ah_aim_pawn_priority();
  tb_0526_ah_aim_target_live();
  tb_0527_ah_aim_earned_auto();
  tb_0528_ah_aim_damage_type();
  tb_0529_ah_names_after_checkpoint();
  tb_0530_splash_close_holds_far_fires();
  tb_0531_splash_blocker_nearer_holds();
  tb_0532_splash_hitscan_unaffected();
  tb_0533_splash_unread_radius_default();
  tb_0534_ln_level_names_move();
  tb_0535_boss_verdict();
  tb_0536_boss_pick();
  tb_0537_tbot_boss_skip();
  tb_0538_splash_fail_closed_and_budget();
  tb_0539_ln_report_cap();
  tb_0540_splash_passes_triggers_and_water();
  tb_0541_boss_blast_and_screen();
  tb_0542_pg_dlc_chapter_match();
  tb_0543_pg_dlc_table();
  tb_0544_pg_dlc_mode();
  tb_0545_pg_dlc_chapters();
  tb_0546_pg_dlc_totals_and_view();
  tb_0547_eg_dlc_toilet_reads_17();
  tb_0548_eg_dlc_events_labels_gate();
  tb_0549_ah_dlc_rows();
  tb_0550_it_dlc_quest_pickups();
  tb_0551_ah_dlc_done_total();
  tb_0552_bp_class_chain();
  tb_0553_fb_probe_rule();
  tb_0554_fblift_curve_and_slider();
  tb_0555_fb_probe_skips_norender();
  tb_0556_fb_probe_path();
  tb_0557_weapon_classify();
  tb_0558_bloat_verdict();
  tb_0559_bloat_table_and_exclusion();
  tb_0560_ok_bloat_expected();
  tb_0561_mp_gate();
  tb_0562_lob_classify_and_model();
  tb_0563_lob_boosted_arc();
  tb_0564_lob_origin_and_fd_velocity();
  tb_0565_nosolve_cache();
  tb_0566_at_frame_row();
  tb_0567_at_lane_and_rules();
  tb_0568_arb_rules();
  tb_0569_arb_aim_and_fire();
  tb_0570_arb_target_choice();
  tb_0571_arb_dodge();
  tb_0572_arb_takeover_and_stop();
  tb_0573_arb_menu_config_detail();
  tb_0574_arb_review_rules();
  tb_0575_ms_hooks();
  tb_0576_ms_rot_write();
  tb_0577_ms_yaw_plan();
  tb_0578_ms_yaw_secant();
  tb_0579_ms_bracket();
  tb_0580_ms_shot_seen();
  tb_0581_ms_nested_same_fn();
  tb_0582_photo_folder_path_ok();
  tb_0583_photo_file_name_n();
  tb_0584_scall_parms_fit();
  tb_0585_ini_overlong_line();
  printf("%d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}

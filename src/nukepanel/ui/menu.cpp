// The Nuke Panel, duke-dnmp's ui/menu.cpp (itself AA's): the chrome, drag and resize, the tab rail, the row
// widgets, the springs, tooltips, scrolling, folds, the numeric entry field and key capture, unchanged. The rows
// are this port's; a tab draws only once it has one (menu_tab_visible).
#include <windows.h>   // InterlockedExchange64/CompareExchange64, the commit slot
#include "menu.h"
#include "panel_layout.h"
#include "canvas.h"
#include "font.h"
#include "anim.h"
#include "wallclock.h"
#include "tooltip.h"
#include "tipwrap.h"      // the hint wrapped to TIP_MAX_W
#include "bind.h"
#include "entry.h"
#include "input.h"        // ui/input.h: g_ui, filled once a frame
#include "../config.h"    // g_cfg, g_cfgDirty, config_bind_get/set
#include "../log.h"
#include "../input.h"     // input_panel_open: the logical open state
#include "../framecap.h"  // framecap_cap_info: the Cap row's effective value and slider ceiling
#include "../bigpackage.h" // bigpackage_state: the Big Package row's note
#include "../esp_layout.h" // ESP_DRAWDIST_MIN/MAX/DEFAULT, for the RANGE group
#include "../fov.h"        // the FIELD OF VIEW rows
#include "../gamespeed.h"  // g_gameSpeedOn and the GAME SPEED rows
#include "../accuracy.h"   // the PERFECT ACCURACY row's availability
#include "../handling.h"   // the HANDLING rows: DNMP's stops and labels
#include "../display.h"    // display_note: the Borderless window row
#include "../postfx.h"     // the POST PROCESSING rows
#include "../focuskeep.h"  // the Keep running row's availability
#include "../rawmouse.h"   // the Raw mouse input row
#include "../noshake.h"    // the No screen shake row
#include "../dukevision.h" // the Clean Duke Vision row
#include "../progress.h"   // the Progress tab's rows, read by progress.cpp
#include "../autowin.h"    // the AUTO-WIN rows' availability
#include "../pinbot.h"     // Pinball autoplay's availability
#include "../arcbot.h"     // Arcade autoplay's availability
#include "../autoexec.h"
#include "../aim.h"        // the TARGETING rows
#include "../tbot.h"
#include "../aimbot.h"     // the AIMBOT rows
#include "../freecam.h"    // the FREE CAMERA rows and the flight keys a bind refuses
#include "../photo.h"      // the Photo size row and its readout
#include "../fblift.h"     // the Brightness lift row
#include "../photofolder.h" // the Photo folder row
#include "../achreset.h"   // the STEAM ACHIEVEMENTS rows
#include "../achhelp.h"    // the Achievements tab's rows
#include "../mpgate.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// The first visible tab (menu_tab_visible).
static int s_tab = TAB_VISUALS;

// --- Where each tab is scrolled to ----------------------------------------
// Session state, not a setting: cleared at menu_draw's open edge. s_contentH is the last measurement of each tab's
// height and survives a close, so the first wheel notch of a reopened panel clamps properly.
static int s_scroll[TAB_COUNT]   = {};
static int s_contentH[TAB_COUNT] = {};
static double s_wheel = 0.0;

void menu_set_wheel(double notches) { s_wheel += notches; }

// --- The key capture ------------------------------------------------------
// One armed row for the whole panel; every transition is keycap_step's (bind.h).
static void menu_note_new_session();
static void menu_entry_close();
static int field_text_y(const UiRect& field);

static KeyCapState s_keycap = { KEYCAP_IDLE, -1 };

bool menu_keycap_armed() { return s_keycap.phase == KEYCAP_ARMED; }

void menu_keycap_bind(int vk) {
  KeyCapEvent e;
  e.kind = bind_is_mouse(vk) ? KEYCAP_EV_MOUSE : KEYCAP_EV_KEY;
  e.row = -1;
  e.vk = vk;
  const KeyCapResult r = keycap_step(s_keycap, e);
  s_keycap = r.state;
  // The camera's keys refuse a fixed movement key (freecam_bind_refused): the binding stays as it was.
  if (r.boundRow >= 0 && freecam_bind_refused(r.boundRow, r.boundVk)) {
    np_log("menu: row %d refused 0x%02X: a free camera movement key", r.boundRow, r.boundVk);
    return;
  }
  if (r.boundRow >= 0) config_bind_set(r.boundRow, r.boundVk);   // raises g_cfgDirty itself
}

void menu_keycap_cancel() {
  KeyCapEvent e;
  e.kind = KEYCAP_EV_ESCAPE;
  e.row = -1;
  e.vk = 0;
  s_keycap = keycap_step(s_keycap, e).state;
}

void menu_keycap_close() {
  // A capture left armed across a close would eat the first key back in the game.
  KeyCapEvent e;
  e.kind = KEYCAP_EV_CLOSE;
  e.row = -1;
  e.vk = 0;
  s_keycap = keycap_step(s_keycap, e).state;
  menu_entry_close();
  menu_note_new_session();
}

// --- The numeric entry field (AA's menu.cpp:180-740, ui/entry.h) ------------
// The store only: every transition is entry_step's. One field for the whole panel, like the key capture.
static EntryState s_entry = entry_begin();
// The left button went down inside the field's well and is still down: later frames drag the selection.
static bool s_entryCaretDrag = false;
// The field's drawn box, for telling a press inside the well from one outside (the click-away).
static UiRect s_entryFieldRect = { 0, 0, 0, 0 };
// row_slider_at drew the open field this frame; a tab switch or dropped row closes it by commit.
static bool s_entrySeenThisFrame = false;
static char s_entryClip[ENTRY_TEXT_CAP + 1] = { 0 };
// The cursor x before the body parks it, for a caret drag that leaves the panel.
static int s_frameMx = 0;

// --- The panel moves and resizes ---------------------------------------
static PanelDrag s_drag = { PDRAG_IDLE, 0, 0, { 0, 0, 0, 0 } };
static PanelBox  s_dragBox = { 0, 0, 0, 0 };
static bool      s_dragLive = false;

// A press a glyph or list consumed, latched for the whole hold (AA).
static bool      s_pressSpent = false;

// The slider row drawn last had its reset arrow pressed this frame (read by a row with a second value).
static bool      s_resetPressedThisRow = false;

// A press some control took this frame; an unclaimed one moves the panel.
static bool      s_pressClaimed = false;

static PanelBox panel_stored_box() {
  PanelBox b = { g_cfg.panelx, g_cfg.panely, g_cfg.panelw, g_cfg.panelh };
  return b;
}

// The stored box as it draws at `pct` on the logical screen: its height capped so the whole panel fits the screen
// at that scale (menu_panel_drawn). Drawing is not a write.
static PanelBox panel_drawn_box(int screenH, int pct, int logicalW, int logicalH) {
  return menu_panel_drawn(panel_stored_box(), logicalW, logicalH, ui_panel_h_cap(screenH, pct));
}

int menu_applied_scale(int screenW, int screenH) {
  return ui_scale_settled(g_cfg.uiScale, ui_scale_ceiling(screenW, screenH));
}

// The scale held for the whole of a press (ui_scale_effective), and the one the
// canvas actually drew the last panel at (100 when the scaled atlas was refused).
static int s_scaleHeld = UI_SCALE_DEFAULT;
static int s_drawnPct = UI_SCALE_DEFAULT;

PanelBox menu_panel_box(int screenW, int screenH) {
  const int logicalW = ui_unscale_px(screenW, s_drawnPct);
  const int logicalH = ui_unscale_px(screenH, s_drawnPct);
  const PanelBox laid = panel_drawn_box(screenH, s_drawnPct, logicalW, logicalH);
  return s_dragLive ? s_dragBox : laid;
}

UiRect menu_panel_rect(int screenW, int screenH) {
  const PanelBox b = menu_panel_box(screenW, screenH);
  const int x0 = ui_scale_px(b.x, s_drawnPct), y0 = ui_scale_px(b.y, s_drawnPct);
  return UiRect{ x0, y0, ui_scale_px(b.x + b.w, s_drawnPct) - x0, ui_scale_px(b.y + b.h, s_drawnPct) - y0 };
}

int  menu_active_tab() { return s_tab; }
void menu_set_tab(int tab) { if (tab >= 0 && tab < TAB_COUNT && menu_tab_visible(tab)) s_tab = tab; }

// AA's names and accents, verbatim, and Progress in its own teal.
// Achievements in gold; the names are menu_tab_name's.
static const unsigned TAB_COLOURS[TAB_COUNT] = { C_BLUE, C_RED, C_HAZARD, C_GREEN, C_GREEN, C_PROGRESS, C_ACHIEVE, C_SETTINGS };

// Off and Simple: AA's config carries a 0/1 switch, and Glass was never drawn.
static int anim_style() { return g_cfg.animations ? ANIM_SIMPLE : ANIM_OFF; }

// --- The presentation state --------------------------------------------
// Cosmetic only: every hit test compares against the logical rect ui_row hands back, so a click mid-transition lands
// where the control is going. Arrays are indexed by row ordinal this frame; a tab switch snaps them.
struct MenuPresentation {
  unsigned lastMs;
  AnimSpring open;
  int openTarget;
  int wasOpen;
  unsigned tabSwitchMs;
  int tabFrom;
  AnimSpring knob[64];
  AnimSpring fill[64];
  double hover[64];
  double resetHover[64];
  int rowsThisFrame;
};
static MenuPresentation s_pres;

static const int ANIM_ROW_SLOTS = 64;
static bool s_tabSwitchedThisFrame = false;
static double s_frameDt = 0.0;

// The eased fold's frame state (collapse_begin): a frame count to tell a block drawn last frame from one
// coming back, a snap for a tab switch or a new session, the clip rows are drawn under (canvas_push_clip
// replaces rather than intersects) and the product of the enclosing folds' fades.
static unsigned s_frameNo = 0;
static bool s_foldSnap = false;
static UiRect s_clipNow = { 0, 0, 0, 0 };
static double s_foldAlpha = 1.0;

// --- Hints as tooltips --------------------------------------------------
// row_hint attaches to the row drawn just before it; the key is
// s_tab * 1000 + ordinal. The hint is copied: it may be a stack buffer.
static UiRect s_lastRow = { 0, 0, 0, 0 };
static int s_lastRowKey = -1;
static int s_hoverRowKey = -1;
static UiRect s_bodyRect = { 0, 0, 0, 0 };
// A row that ran out of layout budget and drew nothing: logged once a session.
static int s_rowsDropped = 0;
static bool s_rowsDroppedLogged = false;
static char s_hintBuf[256];   // the longest hints (an area row's) run past 96
static const char* s_hoverHint = nullptr;
static const char* s_tooltipText = nullptr;
static TooltipState s_tooltip = tooltip_begin();
static const int TOOLTIP_R = 4;
static const char* s_glyphHint = nullptr;
// The status line's tooltip key: past every row key (tab * 1000 + ordinal).
static const int STATUS_TIP_KEY = 9000000;

// What a panel session must not inherit from the one before: a drag armed by a
// panel closed mid-move, a spent press, a tooltip resting on the closing row.
static void menu_note_new_session() {
  s_drag.phase = PDRAG_IDLE;
  s_dragLive = false;
  s_pressSpent = false;
  s_tooltip = tooltip_begin();
  s_tooltipText = nullptr;
  s_hoverHint = nullptr;
  s_glyphHint = nullptr;
  s_hoverRowKey = -1;
  s_lastRowKey = -1;
}

const char* menu_tooltip_text() { return s_tooltipText; }

// tip_wrap's measure: a hint's piece in the tooltip's font.
static int tip_measure(const char* s, int n, void*) {
  char b[sizeof s_hintBuf];
  _snprintf_s(b, sizeof b, _TRUNCATE, "%.*s", n, s);
  return canvas_text_width(b, FONT_SMALL);
}

static void row_hint(const char* text) {
  if (s_lastRowKey < 0 || !text) return;
  if (!ui_hit(s_lastRow, g_ui.mx, g_ui.my)) return;
  s_hoverRowKey = s_lastRowKey;
  strncpy_s(s_hintBuf, sizeof(s_hintBuf), text, _TRUNCATE);
  s_hoverHint = s_hintBuf;
}

// A hint that gives way to a refusal's reason, as the avail rows' hints do.
static void row_hint_or(const char* why, const char* text) { row_hint(why ? why : text); }

static unsigned s_nowOverride = 0;
void menu_set_now_ms(unsigned ms) { s_nowOverride = ms; }
static unsigned menu_now() { return s_nowOverride ? s_nowOverride : wallclock_ms(); }

bool menu_closing_animation_active() {
  if (anim_style() == ANIM_OFF || input_panel_open()) return false;
  // A spring nothing has stepped for a second is at rest, whatever it last held.
  if (menu_now() - s_pres.lastMs > ANIM_STALE_MS) { s_pres.open.x = 0.0; s_pres.open.v = 0.0; }
  return !anim_spring_settled(s_pres.open, 0.0, 0.01);
}

// The whole-panel open alpha; identity under ANIM_OFF.
static unsigned pres_colour(unsigned c) {
  if (anim_style() == ANIM_OFF) return c;
  const int srcA = (int)((c >> 24) & 0xFFu);
  const int a = (int)(srcA * s_pres.open.x + 0.5);
  return canvas_with_alpha(c, a);
}

// The tooltip's own fade, independent of the panel's open.
static unsigned alpha_scale(unsigned c, double a) {
  if (a < 0.0) a = 0.0;
  if (a > 1.0) a = 1.0;
  const int srcA = (int)((c >> 24) & 0xFFu);
  return canvas_with_alpha(c, (int)(srcA * a + 0.5));
}

// The open alpha further scaled by the tab crossfade, for tab content only.
static unsigned pres_content_colour(unsigned c) {
  const unsigned withOpen = pres_colour(c);
  if (anim_style() == ANIM_OFF) return withOpen;
  const double tabP = anim_ease_out(anim_progress(s_pres.tabSwitchMs, s_pres.lastMs, ANIM_TAB_MS));
  const int srcA = (int)((withOpen >> 24) & 0xFFu);
  const int a = (int)(srcA * tabP * s_foldAlpha + 0.5);
  return canvas_with_alpha(withOpen, a);
}

static unsigned pres_hover_lighten(unsigned c, double hoverFrac) {
  if (anim_style() == ANIM_OFF) return c;
  return anim_blend_colour(c, C_BONE, hoverFrac);
}

// For a field whose parts must not all move toward the same colour: a well,
// rim and text all lightened toward C_BONE lose the text into the well.
static unsigned pres_hover_toward(unsigned c, unsigned target, double hoverFrac) {
  if (anim_style() == ANIM_OFF) return c;
  return anim_blend_colour(c, target, hoverFrac);
}

static const int ROW_R = 4;
static const int SIMPLE_SLIDE_PX = 8;

// No row acts while the panel is being dragged or on a spent press.
static bool row_pressed() { return !s_dragLive && !s_pressSpent && g_ui.pressed; }
static bool row_down()    { return !s_dragLive && !s_pressSpent && g_ui.down; }

// Where a value lands from outside its own row: One slot, rowKey high and value low in one word (row_commit_pack,
// common.h): the reset glyph and the dropdown list write it, the owning row takes it on its next draw. Kept interlocked
// as in the sibling ports, though everything here runs on the one main thread.
static LONG64 s_rowCommit = row_commit_pack(-1, 0);

static void row_commit_publish(int rowKey, int value) {
  InterlockedExchange64(&s_rowCommit, row_commit_pack(rowKey, value));
}

// Claims the pending commit for `key` only, exactly the word it read.
static bool row_commit_take(int key, int* value) {
  const LONG64 seen = InterlockedCompareExchange64(&s_rowCommit, 0, 0);
  if (row_commit_key(seen) != key) return false;
  if (InterlockedCompareExchange64(&s_rowCommit, row_commit_pack(-1, 0), seen) != seen) return false;
  if (value) *value = row_commit_value(seen);
  return true;
}

// A panel close means no row is about to ask for a pending commit: it goes with the field.
static void row_commit_drop() { InterlockedExchange64(&s_rowCommit, row_commit_pack(-1, 0)); }

bool menu_entry_open() { return s_entry.open != 0; }

void menu_entry_paste(const char* s) {
  if (!s_entry.open || !s) return;
  s_entry = entry_step(s_entry, ENTRY_EV_PASTE, 0, 0, EK_NONE, 0, 0, s).state;   // text in, never a commit
}

// The field goes with the panel: left open, it would keep eating keys with nothing on screen.
static void menu_entry_close() {
  s_entry = entry_begin();
  s_entryCaretDrag = false;
  row_commit_drop();
}

// The one place a click away becomes a commit: a left press outside the field, a right press opening a
// field on another row, or the field's row leaving the body. Escape is the only cancel (entry.h).
static void entry_clickaway_commit() {
  if (!s_entry.open) return;
  const int row = s_entry.rowKey;
  const EntryResult r = entry_step(s_entry, ENTRY_EV_CLICKAWAY, 0, 0, EK_NONE, 0, 0, 0);
  s_entry = r.state;
  if (r.commit) row_commit_publish(row, r.value);
}

static void menu_entry_begin_edit(int rowKey, int kind, int lo, int hi, const char* text) {
  // An armed capture goes down first: keys go to the field before the capture (panel.cpp).
  menu_keycap_cancel();
  s_entryCaretDrag = false;
  s_entry = entry_step(s_entry, ENTRY_EV_OPEN, 0, rowKey, kind, lo, hi, text).state;
}

const char* menu_entry_key_vk(int vk, bool shift, bool ctrl) {
  if (!s_entry.open) return nullptr;
  const int row = s_entry.rowKey;   // before the step: a commit closes the field
  int ch = 0;
  const int ev = entry_event_of_vk(vk, shift, ctrl, &ch);
  if (ev < 0 || ev == ENTRY_EV_PASTE) return nullptr;
  const EntryResult r = entry_step(s_entry, ev, ch, 0, EK_NONE, 0, 0, 0);
  s_entry = r.state;
  if (r.commit) row_commit_publish(row, r.value);
  if (!r.hasClip) return nullptr;
  int n = 0;
  while (n < ENTRY_TEXT_CAP && r.clip[n]) { s_entryClip[n] = r.clip[n]; n++; }
  s_entryClip[n] = 0;
  return s_entryClip;
}

// A prefix's width, measured rather than one advance multiplied (AA's entry_prefix_w).
static int entry_prefix_w(const EntryState& s, int i) {
  if (i < 0) i = 0;
  if (i > s.len) i = s.len;
  char buf[ENTRY_TEXT_CAP + 1];
  int n = 0;
  while (n < i && n < ENTRY_TEXT_CAP && s.text[n]) { buf[n] = s.text[n]; n++; }
  buf[n] = 0;
  return canvas_text_width(buf, FONT_MONO);
}
// The nearest caret boundary to a pixel, ties rounding up (AA's entry_index_measured).
static int entry_index_measured(const EntryState& s, int textX, int px) {
  int i = 0, wPrev = 0;
  while (i < s.len) {
    const int wNext = entry_prefix_w(s, i + 1);
    if (px - textX < (wPrev + wNext + 1) / 2) break;
    wPrev = wNext;
    i++;
  }
  return i;
}

// A sub-row hangs off the row above it by an elbow: a trunk down the left edge, from the parent's foot (or the
// previous sub-row's elbow, so a run of sub-rows shares one trunk), then a tick into the label.
static int s_subPrevY = -100000;
static void sub_connector(const UiRect& r) {
  const int x = r.x + SUB_RULE_X, ey = r.y + ROW_H / 2;
  const int y0 = (s_subPrevY + ROW_H + ROW_GAP == r.y) ? s_subPrevY + ROW_H / 2 + 1 : r.y - ROW_GAP;
  const unsigned c = pres_content_colour(C_RAISED);
  canvas_rect(x, y0, 1, ey - y0 + 1, c);
  canvas_rect(x, ey, SUB_INDENT - 8, 1, c);
  s_subPrevY = r.y;
}

// Group plates: Each group sits on a sunken plate in the recess colour with its name on the top edge. Rows draw in
// order with no z-order, so the plate is drawn first at the height it measured last frame (kept per tab and group); a
// fold easing open or shut leaves it one frame behind.
enum { PLATE_OUT = 6, PLATE_GAP = 12, PLATE_LEGEND_Y = 13, PLATE_BOTTOM = 2, PLATE_HEADLESS_PAD = 6, PLATE_SLOTS = 24 };
struct PlateState { bool open; int top, slot, x, w; };
static PlateState s_plate = { false, 0, 0, 0, 0 };
static int s_plateOrd = 0;
static int s_plateH[TAB_COUNT][PLATE_SLOTS] = {};

// Closes the open plate: measures it for next frame, then leaves the gap before the next one (none after the last).
static void plate_close(UiRect& cursor, bool last) {
  if (!s_plate.open) return;
  s_plate.open = false;
  const int bottom = cursor.y + PLATE_BOTTOM;
  if (s_plate.slot < PLATE_SLOTS) s_plateH[s_tab][s_plate.slot] = bottom - s_plate.top;
  if (!last && cursor.h >= PLATE_BOTTOM + PLATE_GAP) ui_row(cursor, PLATE_BOTTOM + PLATE_GAP, 0);
}

// Opens a plate under `heading`; a null or empty heading opens one with no name (the caller draws its own).
static void plate_open(UiRect& cursor, const char* heading) {
  plate_close(cursor, false);
  const bool headed = heading && heading[0];
  if (cursor.h < GROUP_H + 2) { s_rowsDropped = 1; return; }
  const UiRect r = headed ? ui_row(cursor, GROUP_H, 2) : ui_row(cursor, PLATE_HEADLESS_PAD, 0);
  const int top = headed ? r.y + PLATE_LEGEND_Y : r.y;
  const int x = r.x - PLATE_OUT, w = r.w + 2 * PLATE_OUT;
  const int slot = s_plateOrd++;
  const int known = slot < PLATE_SLOTS ? s_plateH[s_tab][slot] : 0;
  if (known > 0) {
    canvas_bevel(x, top, w, known, pres_content_colour(C_RECESS), pres_content_colour(C_BEVELLO),
                 pres_content_colour(C_RAISED));
    if (headed) {
      // The name breaks the plate's top edge, as a fieldset's legend does.
      const int tw = canvas_text_width(heading, FONT_SMALL);
      canvas_rect(r.x + 1, top, tw + 8, 1, pres_content_colour(C_PANEL));
    }
  }
  if (headed) canvas_text(r.x + 5, r.y + 5, heading, pres_content_colour(C_BONEFNT), FONT_SMALL);
  s_plate.open = true; s_plate.top = top; s_plate.slot = slot; s_plate.x = x; s_plate.w = w;
}

// --- The row widgets ------------------------------------------------------
// The sibling ports' whole vocabulary, carried complete: several have no caller yet (the dropdown, the named and sub
// sliders, the sub rows); every later row is built from them.

// The shared body of both switch rows; returns the new value. A disabled row
// is C_BONEFNT and inert. A row with no room draws nothing.
// `unavailable`: a refused feature keeps its label and shows MENU_UNAVAILABLE_TAG where the switch would be.
static int row_switch_at(UiRect& cursor, const char* label, int value, unsigned accent,
                         bool enabled, int indent, unsigned labelOn, bool unavailable = false) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; s_rowsDropped = 1; return value; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const UiRect sw = { r.x + r.w - 42, r.y + 5, 42, 20 };
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;

  const bool hovered = enabled && ui_hit(sw, g_ui.mx, g_ui.my);
  if (anim_style() == ANIM_OFF) {
    s_pres.hover[idx] = hovered ? 1.0 : 0.0;
  } else {
    const double rate = hovered ? (1.0 / ANIM_HOVER_IN_MS) : (-1.0 / ANIM_HOVER_OUT_MS);
    double hv = s_pres.hover[idx] + rate * s_frameDt;
    if (hv < 0.0) hv = 0.0; else if (hv > 1.0) hv = 1.0;
    s_pres.hover[idx] = hv;
  }

  // The knob's drawn ON fraction: sprung toward the value, snapped on a tab switch.
  int knobX;
  if (anim_style() == ANIM_OFF) {
    knobX = value ? sw.x + 23 : sw.x + 2;
  } else {
    const double targetX = value ? 1.0 : 0.0;
    if (s_tabSwitchedThisFrame) { s_pres.knob[idx].x = targetX; s_pres.knob[idx].v = 0.0; }
    else anim_spring_step(&s_pres.knob[idx], targetX, ANIM_SPRING_CRITICAL.stiffness,
                          ANIM_SPRING_CRITICAL.damping, s_frameDt);
    knobX = sw.x + 2 + (int)(21.0 * s_pres.knob[idx].x + 0.5);
  }

  if (indent > 0) sub_connector(r);
  canvas_text(r.x + indent, r.y + 6, label,
              pres_content_colour(pres_hover_lighten(enabled ? labelOn : C_BONEFNT, s_pres.hover[idx])), FONT_UI);
  if (unavailable) {
    // A dim tag, right-aligned to the switch's slot; it takes no click.
    const int tagPad = 6;
    const int tagW = canvas_text_width(MENU_UNAVAILABLE_TAG, FONT_SMALL) + 2 * tagPad;
    const int tagX = sw.x + sw.w - tagW;
    canvas_round_rect_outline(tagX, sw.y, tagW, sw.h, ROW_R, 1, pres_content_colour(C_RAISED));
    canvas_text(tagX + tagPad, sw.y + (sw.h - font_height[FONT_SMALL]) / 2, MENU_UNAVAILABLE_TAG,
                pres_content_colour(C_BONEFNT), FONT_SMALL);
    return value;
  }
  canvas_bevel(sw.x, sw.y, sw.w, sw.h,
               pres_content_colour(value ? (enabled ? accent : C_RAISED) : C_WELL),
               pres_content_colour(C_BEVELLO), pres_content_colour(C_RAISED));
  canvas_rect(knobX, sw.y + 2, 17, 16,
              pres_content_colour((anim_style() == ANIM_OFF ? value != 0 : s_pres.knob[idx].x >= 0.5)
                                   ? C_BONE : C_RAISED));
  if (ui_row_accepts_click(enabled, row_pressed(), ui_hit(sw, g_ui.mx, g_ui.my))) {
    s_pressClaimed = true;
    g_cfgDirty = 1;
    return !value;
  }
  return value;
}

static int row_switch(UiRect& cursor, const char* label, int value, unsigned accent, bool enabled) {
  return row_switch_at(cursor, label, value, accent, enabled, 0, C_BONE);
}

static int row_switch_sub(UiRect& cursor, const char* label, int value, unsigned accent,
                          bool enabled) {
  return row_switch_at(cursor, label, value, accent, enabled, SUB_INDENT, C_BONEDIM);
}

// A switch whose feature may be refused (a hook or check failed): unavailable, the row keeps its label, shows
// the Unavailable tag instead of the switch and keeps the stored value. The hint says why.
static int row_switch_avail(UiRect& cursor, const char* label, int value, unsigned accent, bool enabled,
                            bool available) {
  return row_switch_at(cursor, label, value, accent, enabled && available, 0, C_BONE, !available);
}


// The slider inventory: each slider row drawn this frame, its track and readout column in logical pixels from the
// content's left edge (a sub-row indents only its label). Logged once per tab per session, and again only if a fold
// opened more slider rows on that tab.
static char s_sliderInv[768];
static int  s_sliderInvLen = 0;
static int  s_sliderInvRows = 0;
static int  s_sliderLoggedRows[TAB_COUNT] = {};

static void slider_inventory_add(const char* label, const MenuSliderGeom& g) {
  if (s_sliderInvLen >= (int)sizeof(s_sliderInv) - 1) return;
  const int n = _snprintf_s(s_sliderInv + s_sliderInvLen, sizeof(s_sliderInv) - s_sliderInvLen, _TRUNCATE,
                            "%s%s x=%d w=%d readout=%d+%d", s_sliderInvRows ? "; " : "", label, g.trackX, g.trackW,
                            g.readoutX, g.readoutW);
  s_sliderInvLen = n < 0 ? (int)sizeof(s_sliderInv) - 1 : s_sliderInvLen + n;
  s_sliderInvRows++;
}

// The shared body of the slider rows. Every row has the same track and readout column (menu_slider_geom), so
// no track depends on its row's value, readout or indent. `kind` EK_NONE (a name or a fixed value) grows no reset
// glyph; `def` is the glyph's value.
static int row_slider_at(UiRect& cursor, const char* label, int value, int lo, int hi,
                         const char* valueText, bool enabled, int indent, unsigned labelOn,
                         int kind, int def) {
  s_resetPressedThisRow = false;
  if (cursor.h < ROW_H) { s_lastRowKey = -1; s_rowsDropped = 1; return value; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;
  const int key = s_lastRowKey;
  const int valueW = canvas_text_width(valueText, FONT_MONO);
  const int valueX = r.x + r.w - VALUE_MARGIN - valueW;
  const MenuSliderGeom geom = menu_slider_geom(r.w, canvas_text_width(MENU_READOUT_RESERVE, FONT_MONO));
  slider_inventory_add(label, geom);
  const UiRect track = { r.x + geom.trackX, r.y + 11, geom.trackW, 8 };
  const UiRect grab = { track.x - 6, track.y - 8, track.w + 12, track.h + 16 };
  const UiRect glyph = { r.x + geom.glyphX, r.y + 5, RESET_W, RESET_W };
  // No reset glyph under the hand while its row's field is open (AA).
  const bool fieldHere = s_entry.open != 0 && s_entry.rowKey == key;
  const bool glyphLive = enabled && kind != EK_NONE && !fieldHere && !s_dragLive && ui_hit(r, g_ui.mx, g_ui.my);
  const bool glyphHot  = glyphLive && ui_hit(glyph, g_ui.mx, g_ui.my);

  s_resetPressedThisRow = glyphHot && row_pressed();
  if (s_resetPressedThisRow) {
    row_commit_publish(key, def);
    s_pressSpent = true;
    s_pressClaimed = true;
  }

  // A committed value lands clamped to [lo, hi], widened to admit `def` when
  // it sits outside that range (the frame-cap row's 0 sentinel); every other
  // caller's def is already inside [lo, hi], so behaviour there is unchanged.
  bool committed = false;
  int landedRaw = 0;
  if (row_commit_take(key, &landedRaw)) {
    const int commitLo = lo < def ? lo : def;
    const int commitHi = hi > def ? hi : def;
    const int landed = clamp_int(landedRaw, commitLo, commitHi);
    committed = landed != value;
    value = landed;
  }

  // A right press anywhere on an enabled numeric row opens the field (AA's menu.cpp:950-961), after
  // committing a field open elsewhere: leaving it is a commit, not a discard.
  if (kind != EK_NONE && enabled && !s_dragLive && g_ui.rpressed && ui_hit(r, g_ui.mx, g_ui.my)) {
    char prefill[ENTRY_TEXT_CAP + 1];
    entry_format(prefill, sizeof prefill, value, kind);
    entry_clickaway_commit();
    menu_entry_begin_edit(key, kind, lo, hi, prefill);
  }
  const bool fieldOpen = s_entry.open != 0 && s_entry.rowKey == key;

  if (glyphHot) { s_hoverRowKey = key; s_glyphHint = MENU_RESET_HINT; }

  const int kx = ui_slider_x(track, value, lo, hi);

  const bool hovered = enabled && ui_hit(grab, g_ui.mx, g_ui.my);
  if (anim_style() == ANIM_OFF) {
    s_pres.hover[idx] = hovered ? 1.0 : 0.0;
  } else {
    const double rate = hovered ? (1.0 / ANIM_HOVER_IN_MS) : (-1.0 / ANIM_HOVER_OUT_MS);
    double hv = s_pres.hover[idx] + rate * s_frameDt;
    if (hv < 0.0) hv = 0.0; else if (hv > 1.0) hv = 1.0;
    s_pres.hover[idx] = hv;
  }
  if (anim_style() == ANIM_OFF) {
    s_pres.resetHover[idx] = glyphHot ? 1.0 : 0.0;
  } else {
    const double rate = glyphHot ? (1.0 / ANIM_HOVER_IN_MS) : (-1.0 / ANIM_HOVER_OUT_MS);
    double rv = s_pres.resetHover[idx] + rate * s_frameDt;
    if (rv < 0.0) rv = 0.0; else if (rv > 1.0) rv = 1.0;
    s_pres.resetHover[idx] = rv;
  }

  int drawnKx;
  if (anim_style() == ANIM_OFF) {
    drawnKx = kx;
  } else {
    const double targetFrac = track.w > 0 ? (double)(kx - track.x) / (double)track.w : 0.0;
    if (s_tabSwitchedThisFrame) { s_pres.fill[idx].x = targetFrac; s_pres.fill[idx].v = 0.0; }
    else anim_spring_step(&s_pres.fill[idx], targetFrac, ANIM_SPRING_CRITICAL.stiffness,
                          ANIM_SPRING_CRITICAL.damping, s_frameDt);
    drawnKx = track.x + (int)(s_pres.fill[idx].x * track.w + 0.5);
    if (drawnKx < track.x) drawnKx = track.x;
    if (drawnKx > track.x + track.w) drawnKx = track.x + track.w;
  }

  if (indent > 0) sub_connector(r);
  canvas_text(r.x + indent, r.y + 6, label,
              pres_content_colour(pres_hover_lighten(enabled ? labelOn : C_BONEFNT, s_pres.hover[idx])), FONT_UI);
  canvas_bevel(track.x, track.y, track.w, track.h, pres_content_colour(C_WELL),
               pres_content_colour(C_BEVELLO), pres_content_colour(C_RAISED));
  canvas_rect(track.x, track.y, drawnKx - track.x, track.h, pres_content_colour(C_HAZARDDIM));
  canvas_rect(drawnKx - 4, track.y - 4, 9, 16, pres_content_colour(enabled ? C_HAZARD : C_BONEFNT));
  if (fieldOpen) {
    // AA's field (its menu.cpp:1023-1096): a rounded well sized for the widest int, a C_HAZARD rim, the
    // selection in C_HAZARDDIM under the text, a caret blinking on 500 ms (steady under ANIM_OFF).
    const int fieldW = canvas_text_width(MENU_ENTRY_WIDEST, FONT_MONO) + 2 * FIELD_PAD;
    const UiRect field = { r.x + r.w - fieldW, r.y + 5, fieldW, 20 };
    s_entryFieldRect = field;
    s_entrySeenThisFrame = true;
    if (!ui_rect_inside(field, s_bodyRect)) entry_clickaway_commit();   // scrolled out: commit, not strand
    canvas_round_rect(field.x, field.y, field.w, field.h, ROW_R, pres_content_colour(C_WELL));
    canvas_round_rect_outline(field.x, field.y, field.w, field.h, ROW_R, 1, pres_content_colour(C_HAZARD));
    const int textY = field_text_y(field);
    const int textX = field.x + FIELD_PAD;
    // The raw button, not row_pressed(): the press into this well is also what spends the row's press.
    if (g_ui.pressed && ui_hit(field, g_ui.mx, g_ui.my)) {
      s_entry = entry_step(s_entry, ENTRY_EV_SET_CARET, entry_index_measured(s_entry, textX, g_ui.mx),
                           0, EK_NONE, 0, 0, 0).state;
      s_entryCaretDrag = true;
    } else if (s_entryCaretDrag && g_ui.down) {
      s_entry = entry_step(s_entry, ENTRY_EV_SET_CARET, entry_index_measured(s_entry, textX, s_frameMx),
                           1, EK_NONE, 0, 0, 0).state;
    }
    const int selA = entry_sel_lo(s_entry), selB = entry_sel_hi(s_entry);
    if (selB > selA) {
      const int ax = entry_prefix_w(s_entry, selA), bx = entry_prefix_w(s_entry, selB);
      canvas_rect(textX + ax, field.y + 4, bx - ax, field.h - 8, pres_content_colour(C_HAZARDDIM));
    }
    canvas_text(textX, textY, s_entry.text, pres_content_colour(C_BONE), FONT_MONO);
    const bool caretOn = (anim_style() == ANIM_OFF) || ((menu_now() / 500u) % 2u == 0u);
    if (caretOn)
      canvas_rect(textX + entry_prefix_w(s_entry, s_entry.caret) + 1, field.y + 4, 1, field.h - 8,
                  pres_content_colour(C_BONE));
  } else {
    canvas_text(valueX, r.y + 7, valueText, pres_content_colour(C_BONE), FONT_MONO);
  }

  // The reset button: a 20x20 well with a circular arrow drawn as two open
  // rings of seven segments (2px stroke) and an arrowhead (AA's geometry).
  if (glyphLive) {
    const double rf = s_pres.resetHover[idx];
    canvas_round_rect(glyph.x, glyph.y, glyph.w, glyph.h, ROW_R,
                      pres_content_colour(pres_hover_toward(C_WELL, C_RAISED, rf)));
    canvas_round_rect_outline(glyph.x, glyph.y, glyph.w, glyph.h, ROW_R, 1,
                              pres_content_colour(pres_hover_toward(C_RAISED, C_BONEDIM, rf)));
    static const int RING6[8][2] = {
      { 16, 10 }, { 15, 6 }, { 11, 4 }, { 7, 5 }, { 4, 8 },
      { 4, 12 }, { 7, 15 }, { 11, 16 }
    };
    static const int RING5[8][2] = {
      { 15, 10 }, { 14, 7 }, { 11, 5 }, { 7, 6 }, { 5, 8 },
      { 5, 12 }, { 7, 14 }, { 11, 15 }
    };
    static const int BARB[2] = { 13, 13 };
    const int ix = glyph.x - 1, iy = glyph.y;
    const unsigned gc = pres_content_colour(glyphHot ? C_BONE : C_BONEDIM);
    for (int i = 0; i < 7; i++) {
      canvas_line(ix + RING6[i][0], iy + RING6[i][1],
                  ix + RING6[i + 1][0], iy + RING6[i + 1][1], gc);
      canvas_line(ix + RING5[i][0], iy + RING5[i][1],
                  ix + RING5[i + 1][0], iy + RING5[i + 1][1], gc);
    }
    canvas_rect(ix + RING6[0][0] - 1, iy + RING6[0][1] - 1, 3, 3, gc);
    canvas_line(ix + RING6[0][0], iy + RING6[0][1], ix + BARB[0], iy + BARB[1], gc);
  }

  // The held button, not the edge: a slider is dragged, off the logical track.
  if (ui_row_accepts_click(enabled, row_down(), ui_hit(grab, g_ui.mx, g_ui.my))) {
    s_pressClaimed = true;
    const int v = ui_slider_value(track, g_ui.mx, lo, hi);
    if (v != value) { g_cfgDirty = 1; return v; }
  }
  if (committed) g_cfgDirty = 1;
  return value;
}

static int row_slider(UiRect& cursor, const char* label, int value, int lo, int hi,
                      const char* suffix, bool enabled, int def) {
  char buf[32];
  sprintf_s(buf, "%d%s", value, suffix);
  return row_slider_at(cursor, label, value, lo, hi, buf, enabled, 0, C_BONE, EK_INT, def);
}

static int row_slider_sub(UiRect& cursor, const char* label, int value, int lo, int hi,
                          const char* suffix, bool enabled, int def) {
  char buf[32];
  sprintf_s(buf, "%d%s", value, suffix);
  return row_slider_at(cursor, label, value, lo, hi, buf, enabled, SUB_INDENT, C_BONEDIM, EK_INT, def);
}

// A readout that is a name.
static int row_slider_named(UiRect& cursor, const char* label, int value, int lo, int hi,
                            const char* (*labelFn)(int), bool enabled) {
  return row_slider_at(cursor, label, value, lo, hi, labelFn(value), enabled, 0, C_BONE, EK_NONE, 0);
}

static int row_slider_named_sub(UiRect& cursor, const char* label, int value, int lo, int hi,
                                const char* (*labelFn)(int), bool enabled) {
  return row_slider_at(cursor, label, value, lo, hi, labelFn(value), enabled, SUB_INDENT, C_BONEDIM, EK_NONE, 0);
}

// --- A dropdown row ---------------------------------------------------------
// The open list is drawn after every row (menu_dropdown_draw), since rows
// paint in order with no z-order.
struct MenuDropdown {
  int  open;
  int  rowKey;
  UiRect box;
  int  count;
  const char* (*labelFn)(int);
};
static MenuDropdown s_dd = { 0, -1, { 0, 0, 0, 0 }, 0, 0 };
static bool s_ddSeenThisFrame = false;

static const int DD_CHEVRON_W = 14;
static const int DD_PAD       = 6;

static int row_dropdown(UiRect& cursor, const char* label, int value, int count,
                        const char* (*labelFn)(int), bool enabled) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; s_rowsDropped = 1; return value; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;
  const int key = s_lastRowKey;

  int nameW = 0;
  for (int v = 0; v < count; v++) {
    const int w = canvas_text_width(labelFn(v), FONT_MONO);
    if (w > nameW) nameW = w;
  }
  const int boxW = nameW + DD_PAD * 2 + DD_CHEVRON_W;
  const UiRect box = { r.x + r.w - VALUE_MARGIN - boxW, r.y + 4, boxW, ROW_H - 8 };

  const bool hot = enabled && ui_hit(box, g_ui.mx, g_ui.my);
  const bool pressed = row_pressed();
  if (hot && pressed) s_pressSpent = true;
  const bool wasMine = s_dd.open != 0 && s_dd.rowKey == key;
  bool seen = false;
  const MenuDdState newSt = menu_dd_row_step(MenuDdState{ s_dd.open, s_dd.rowKey },
                                              key, hot, pressed,
                                              ui_rect_inside(box, s_bodyRect), &seen);
  s_dd.open = newSt.open; s_dd.rowKey = newSt.rowKey;
  if (seen) s_ddSeenThisFrame = true;
  const bool mine = newSt.open != 0 && newSt.rowKey == key;
  if (mine && !wasMine) { s_dd.box = box; s_dd.count = count; s_dd.labelFn = labelFn; }
  else if (mine) { s_dd.box = box; }

  const unsigned fill = enabled ? (hot || mine ? C_RAISED : C_WELL) : C_WELL;
  canvas_bevel(box.x, box.y, box.w, box.h, pres_content_colour(fill),
               pres_content_colour(C_BEVELHI), pres_content_colour(C_BEVELLO));
  canvas_text(r.x, r.y + 8, label, pres_colour(enabled ? C_BONE : C_BONEFNT), FONT_UI);
  canvas_text(box.x + DD_PAD, box.y + 4, labelFn(clamp_int(value, 0, count - 1)),
              pres_colour(enabled ? C_BONE : C_BONEFNT), FONT_MONO);
  {
    const int cx = box.x + box.w - DD_CHEVRON_W / 2 - DD_PAD / 2;
    const int cy = box.y + box.h / 2;
    const unsigned cc = pres_colour(enabled ? C_BONEDIM : C_BONEFNT);
    const int dy = mine ? -3 : 3;
    canvas_line(cx - 4, cy - dy / 2, cx, cy + dy, cc);
    canvas_line(cx, cy + dy, cx + 4, cy - dy / 2, cc);
  }

  int pickedRaw = 0;
  if (row_commit_take(key, &pickedRaw)) {
    const int v = clamp_int(pickedRaw, 0, count - 1);
    if (v != value) g_cfgDirty = 1;
    return v;
  }
  return value;
}

static void menu_dropdown_draw() {
  if (!s_dd.open || !s_dd.labelFn || s_dd.count <= 0) return;
  const int itemH = ROW_H - 6;
  const UiRect list = { s_dd.box.x, s_dd.box.y + s_dd.box.h + 2,
                        s_dd.box.w, itemH * s_dd.count + 4 };
  canvas_bevel(list.x, list.y, list.w, list.h, pres_content_colour(C_PANEL),
               pres_content_colour(C_BEVELHI), pres_content_colour(C_BEVELLO));
  bool onAnItem = false;
  for (int i = 0; i < s_dd.count; i++) {
    const UiRect it = { list.x + 2, list.y + 2 + i * itemH, list.w - 4, itemH };
    const bool hot = ui_hit(it, g_ui.mx, g_ui.my);
    if (hot) {
      onAnItem = true;
      canvas_rect(it.x, it.y, it.w, it.h, pres_content_colour(C_RAISED));
    }
    canvas_text(it.x + DD_PAD - 2, it.y + 3, s_dd.labelFn(i),
                pres_colour(hot ? C_BONE : C_BONEDIM), FONT_MONO);
    if (hot && row_pressed()) {
      s_pressSpent = true;
      row_commit_publish(s_dd.rowKey, i);
      s_dd.open = 0; s_dd.rowKey = -1;
      return;
    }
  }
  if (!onAnItem && row_pressed() && !ui_hit(s_dd.box, g_ui.mx, g_ui.my)) {
    s_pressSpent = true;
    s_dd.open = 0; s_dd.rowKey = -1;
  }
}

// A group heading on its plate (plate_open).
static void draw_group(UiRect& cursor, const char* heading) { plate_open(cursor, heading); }

// A line of text that is a row of its own (AA's note row).
static void draw_placeholder(UiRect& cursor, const char* text) {
  if (cursor.h < ROW_H) { s_rowsDropped = 1; return; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  canvas_text(r.x, r.y + 6, text, pres_content_colour(C_BONEFNT), FONT_SMALL);
}

// A field's width: the widest thing a bind field can ever show, so the well
// never changes width under the hand (AA's row_bind_at rule).
static int field_well_w() {
  int w = canvas_text_width(MENU_KEYCAP_PROMPT, FONT_MONO);
  const int hoverW = canvas_text_width(MENU_KEYCAP_HOVER, FONT_MONO);
  const int nameW  = canvas_text_width(BIND_LABEL_WIDEST, FONT_MONO);
  if (hoverW > w) w = hoverW;
  if (nameW  > w) w = nameW;
  return w + 2 * FIELD_PAD;
}

// FONT_MONO's ink band (cap top to descender) centred in the well.
static int field_text_y(const UiRect& field) {
  const Glyph& capA  = font_glyphs[FONT_MONO]['A' - 0x20];
  const Glyph& descY = font_glyphs[FONT_MONO]['y' - 0x20];
  const int inkTop = capA.by;
  const int inkH   = (descY.by + descY.h) - inkTop;
  return field.y + (field.h - inkH) / 2 - inkTop;
}

static void field_hover_step(int idx, bool hovered) {
  if (anim_style() == ANIM_OFF) {
    s_pres.hover[idx] = hovered ? 1.0 : 0.0;
  } else {
    const double rate = hovered ? (1.0 / ANIM_HOVER_IN_MS) : (-1.0 / ANIM_HOVER_OUT_MS);
    double hv = s_pres.hover[idx] + rate * s_frameDt;
    if (hv < 0.0) hv = 0.0; else if (hv > 1.0) hv = 1.0;
    s_pres.hover[idx] = hv;
  }
}

// A binding row: a well that arms a capture on a click anywhere on the row;
// the next key the panel receives binds. The rim is hazard yellow while armed.
static void row_bind_at(UiRect& cursor, const char* label, int row, bool enabled,
                        int indent, unsigned labelOn) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; s_rowsDropped = 1; return; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;
  const int fieldW = field_well_w();
  const UiRect field = { r.x + r.w - fieldW, r.y + 5, fieldW, 20 };

  const bool hovered = enabled && ui_hit(field, g_ui.mx, g_ui.my);
  field_hover_step(idx, hovered);

  if (indent > 0) sub_connector(r);
  canvas_text(r.x + indent, r.y + 6, label,
              pres_content_colour(enabled ? labelOn : C_BONEFNT), FONT_UI);
  const bool armed = s_keycap.phase == KEYCAP_ARMED && s_keycap.row == row;
  const int vk = config_bind_get(row);
  canvas_round_rect(field.x, field.y, field.w, field.h, ROW_R,
                    pres_content_colour(pres_hover_toward(C_WELL, C_RAISED, s_pres.hover[idx])));
  canvas_round_rect_outline(field.x, field.y, field.w, field.h, ROW_R, 1,
                            pres_content_colour(armed ? C_HAZARD
                                                      : pres_hover_toward(C_RAISED, C_BONEDIM, s_pres.hover[idx])));
  const char* value = bind_field_text(armed, hovered, vk, MENU_KEYCAP_PROMPT, MENU_KEYCAP_HOVER);
  const int valueW = canvas_text_width(value, FONT_MONO);
  canvas_text(field.x + (field.w - valueW) / 2, field_text_y(field), value,
              pres_content_colour(armed ? C_HAZARD : (enabled ? C_BONE : C_BONEFNT)), FONT_MONO);
  if (ui_row_accepts_click(enabled, row_pressed(), ui_hit(r, g_ui.mx, g_ui.my))) {
    KeyCapEvent e; e.kind = KEYCAP_EV_CLICK; e.row = row; e.vk = 0;
    s_keycap = keycap_step(s_keycap, e).state;
    s_pressClaimed = true;
  }
}

static void row_bind(UiRect& cursor, const char* label, int row) {
  row_bind_at(cursor, label, row, true, 0, C_BONE);
}

static void row_bind_sub(UiRect& cursor, const char* label, int row, bool enabled) {
  row_bind_at(cursor, label, row, enabled, SUB_INDENT, C_BONEDIM);
}

static int folder_measure(const char* s, void*) { return canvas_text_width(s, FONT_SMALL); }

// DNMP's Photo folder row (photofolder.h): the bind well stretched to the row's end, holding the folder shortened from
// the left, dim while it is the default. A click anywhere on the row opens the Windows folder picker once the frame
// has drawn (photo_folder_frame).
static void row_folder(UiRect& cursor, const char* label) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; s_rowsDropped = 1; return; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;
  int wellX = r.x + canvas_text_width(label, FONT_UI) + 12;
  if (wellX > r.x + r.w - 2 * FIELD_PAD) wellX = r.x + r.w - 2 * FIELD_PAD;
  const UiRect field = { wellX, r.y + 5, r.x + r.w - wellX, 20 };
  field_hover_step(idx, ui_hit(field, g_ui.mx, g_ui.my));
  canvas_text(r.x, r.y + 6, label, pres_content_colour(C_BONE), FONT_UI);
  canvas_round_rect(field.x, field.y, field.w, field.h, ROW_R,
                    pres_content_colour(pres_hover_toward(C_WELL, C_RAISED, s_pres.hover[idx])));
  canvas_round_rect_outline(field.x, field.y, field.w, field.h, ROW_R, 1,
                            pres_content_colour(pres_hover_toward(C_RAISED, C_BONEDIM, s_pres.hover[idx])));
  char raw[CONFIG_STR_CAP], shown[CONFIG_STR_CAP], fit[CONFIG_STR_CAP];
  bool isDefault = true;
  photo_folder_shown(raw, sizeof raw, &isDefault);
  photo_folder_display(raw, shown, sizeof shown);
  photo_folder_shorten(shown, field.w - 2 * FIELD_PAD, folder_measure, 0, fit, sizeof fit);
  canvas_text(field.x + FIELD_PAD, field.y + (field.h - font_height[FONT_SMALL]) / 2, fit,
              pres_content_colour(isDefault ? C_BONEFNT : C_BONE), FONT_SMALL);
  if (ui_row_accepts_click(true, row_pressed(), ui_hit(r, g_ui.mx, g_ui.my))) {
    photo_folder_request();
    s_pressClaimed = true;
  }
}

// Collapse facility (menu.h): `if (collapse_begin(&f, cursor, parentOn, rows)) { rows } collapse_end(...)`. A collapsed
// block draws and hit-tests nothing. `rows` counts row ordinals (heading, placeholder or "Also bound" note take none);
// a collapsed block skips the larger of `rows` and the rows it drew, so a wrong count cannot shift later rows (logged).
struct FoldSlot {
  int countedRows; int mismatchLogged;
  MenuFold anim; unsigned seenFrame; int fullH;   // fullH: the block's laid-out height, last frame it drew
};
// Keyed by tab and the block's first row ordinal, which a block above it never moves.
static FoldSlot s_folds[TAB_COUNT][ANIM_ROW_SLOTS];

struct MenuFoldBlock {
  MenuCollapse c; int start; int declared; int top;
  double frac; bool animating; bool clipped;
  UiRect savedClip; double savedAlpha; int savedMx, savedMy;
};

static FoldSlot& fold_slot(int start) { return s_folds[s_tab][start % ANIM_ROW_SLOTS]; }

static UiRect rect_intersect(const UiRect& a, const UiRect& b) {
  const int x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
  const int x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
  const int y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
  const UiRect r = { x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0 };
  return r;
}

static bool collapse_begin(MenuFoldBlock* f, UiRect& cursor, bool parentOn, int rows) {
  f->start = s_pres.rowsThisFrame;
  f->declared = rows;
  f->top = cursor.y;
  f->clipped = false;
  FoldSlot& slot = fold_slot(f->start);
  const unsigned now = s_pres.lastMs;
  // A block not drawn last frame (another tab, a shut parent block, a new session) pops rather than replays.
  const bool animate = anim_style() != ANIM_OFF && !s_foldSnap && slot.seenFrame + 1u == s_frameNo;
  slot.anim = menu_fold_step(slot.anim, parentOn ? 1 : 0, now, animate ? 1 : 0);
  slot.seenFrame = s_frameNo;
  f->frac = menu_fold_frac(slot.anim, now);
  f->animating = menu_fold_animating(slot.anim, now);
  const int reserve = slot.countedRows > rows ? slot.countedRows : rows;
  f->c = menu_collapse_begin(menu_fold_laid_out(slot.anim, now) ? 1 : 0, f->start, reserve);
  if (f->c.shown && f->animating) {
    // Last frame's height: this frame's is known only once the rows are laid out.
    const UiRect part = { s_bodyRect.x, cursor.y, s_bodyRect.w, menu_fold_height(slot.fullH, f->frac) };
    const UiRect clip = rect_intersect(part, s_clipNow);
    canvas_push_clip(clip.x, clip.y, clip.w, clip.h);   // pushes replace, so intersected here
    f->clipped = true;
    f->savedClip = s_clipNow;
    s_clipNow = clip;
    f->savedAlpha = s_foldAlpha;
    s_foldAlpha *= f->frac;
    f->savedMx = g_ui.mx; f->savedMy = g_ui.my;
    const bool inPart = clip.h > 0 && ui_hit(clip, g_ui.mx, g_ui.my);
    if (!menu_fold_hit_live(slot.anim, now, inPart)) {
      // A press on a row sliding shut does nothing, and does not grab the panel either.
      if (inPart && g_ui.pressed) s_pressClaimed = true;
      g_ui.mx = MENU_MOUSE_NOWHERE; g_ui.my = MENU_MOUSE_NOWHERE;
    }
  }
  return f->c.shown != 0;
}

static void collapse_end(const MenuFoldBlock& f, UiRect& cursor) {
  if (f.c.shown) {
    FoldSlot& slot = fold_slot(f.start);
    const int drawn = s_pres.rowsThisFrame - f.start;
    slot.countedRows = drawn;
    if (drawn != f.declared && !slot.mismatchLogged) {
      slot.mismatchLogged = 1;
      np_log("menu: fold at tab %d ordinal %d declares %d rows but drew %d", s_tab, f.start, f.declared, drawn);
    }
    slot.fullH = cursor.y - f.top;
    if (f.animating) {
      const int h = menu_fold_height(slot.fullH, f.frac);
      cursor.h += (cursor.y - f.top) - h;
      cursor.y = f.top + h;
    }
  }
  if (f.clipped) {
    canvas_pop_clip();
    s_clipNow = f.savedClip;
    s_foldAlpha = f.savedAlpha;
    g_ui.mx = f.savedMx; g_ui.my = f.savedMy;
  }
  s_lastRowKey = -1;
  s_pres.rowsThisFrame = menu_collapse_end(f.c, s_pres.rowsThisFrame);
}

// --- The Progress tab (split by scope) ---
// A THIS PLAYTHROUGH block (what New Game clears) and a PROFILE block (what it keeps), then one row per chapter: four
// difficulty ticks, helmets and phones (profile) and ego items N/M (playthrough) in fixed columns by scope. Read only.
enum { PG_TICK = 7, PG_TICK_GAP = 3, PG_GAP = 10, PG_TAG_PAD = 3 };
static const char kPgRunCaption[] = "PLAYTHROUGH", kPgProfileCaption[] = "PROFILE";
static const char kPgHelmCaption[] = "HELM", kPgPhoneCaption[] = "PHONE";
static const char kPgDiffCaption[] = "DIFFICULTY";
static int pg_ticks_w() { return PG_DIFFS * PG_TICK + (PG_DIFFS - 1) * PG_TICK_GAP; }
// Wide enough for its scope caption too, so the caption sits over its own column.
static int pg_col_w() {
  const int num = canvas_text_width("00/00", FONT_MONO), cap = canvas_text_width(kPgRunCaption, FONT_SMALL);
  return num > cap ? num : cap;
}
static int pg_small_w(const char* caption) {
  const int num = canvas_text_width("0/0", FONT_MONO), cap = canvas_text_width(caption, FONT_SMALL);
  return num > cap ? num : cap;
}
// Each column's left edge from the row's left, shared by the heading and the rows.
static int pg_diff_w() { return pg_diff_col_w(pg_ticks_w(), canvas_text_width(kPgDiffCaption, FONT_SMALL)); }
// tick is the DIFFICULTY column's left edge; pips sit right-aligned in it, under the caption's right end.
struct PgCols { int ego, phone, helm, tick, pips; };
static PgCols pg_cols(int rowW) {
  PgCols c;
  c.ego = rowW - VALUE_MARGIN - pg_col_w();
  c.phone = c.ego - PG_GAP - pg_small_w(kPgPhoneCaption);
  c.helm = c.phone - PG_GAP - pg_small_w(kPgHelmCaption);
  c.tick = c.helm - PG_GAP - pg_diff_w();
  c.pips = c.tick + pg_diff_w() - pg_ticks_w();
  return c;
}

static bool pg_row_begin(UiRect& cursor, UiRect* r) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; s_rowsDropped = 1; return false; }
  *r = ui_row(cursor, ROW_H, ROW_GAP);
  s_lastRow = *r;
  s_lastRowKey = s_tab * 1000 + s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  return true;
}

// s cut to maxW with "..." (a long chapter title in a narrow panel).
static void pg_fit(const char* s, char* out, int cap, int maxW, int font) {
  strncpy_s(out, (size_t)cap, s, _TRUNCATE);
  if (canvas_text_width(out, font) <= maxW) return;
  for (int n = (int)strlen(out); n > 0; --n) {
    _snprintf_s(out, (size_t)cap, _TRUNCATE, "%.*s...", n, s);
    if (canvas_text_width(out, font) <= maxW) return;
  }
  out[0] = 0;
}

// A label and a right-aligned mono readout (sub: indented under the row above, as a sub-row).
static void pg_row_value(UiRect& cursor, const char* label, const char* value, unsigned colour, bool sub = false) {
  UiRect r;
  if (!pg_row_begin(cursor, &r)) return;
  if (sub) sub_connector(r);
  canvas_text(r.x + (sub ? SUB_INDENT : 0), r.y + 6, label, pres_content_colour(sub ? C_BONEDIM : C_BONE), FONT_UI);
  canvas_text(r.x + r.w - VALUE_MARGIN - canvas_text_width(value, FONT_MONO), r.y + 7, value,
              pres_content_colour(colour), FONT_MONO);
}

// The CHAPTERS heading, two lines: each column's scope (PROFILE over the ticks, helmets and phones, PLAYTHROUGH over
// the ego items), then DIFFICULTY (over the four chapter-completion pips; its hover tip says which pip is which), HELM, PHONE and EGO.
static void pg_chapters_heading(UiRect& cursor) {
  plate_open(cursor, nullptr);   // its own plate; the captions below are drawn by this heading
  if (cursor.h < 2 * GROUP_H + GROUP_GAP) { s_rowsDropped = 1; return; }
  const unsigned c = pres_content_colour(C_BONEFNT);
  {
    const UiRect s = ui_row(cursor, GROUP_H, 0);
    canvas_text(s.x, s.y + 5, "CHAPTERS", c, FONT_SMALL);
    const int colR = s.x + s.w - VALUE_MARGIN;
    const int runW = canvas_text_width(kPgRunCaption, FONT_SMALL);
    canvas_text(colR - runW, s.y + 5, kPgRunCaption, c, FONT_SMALL);
    // Centred over the profile columns, kept clear of the PLAYTHROUGH caption.
    const PgCols k = pg_cols(s.w);
    const int spanW = k.phone + pg_small_w(kPgPhoneCaption) - k.tick;
    const int pw = canvas_text_width(kPgProfileCaption, FONT_SMALL);
    int px = s.x + k.tick + (spanW - pw) / 2;
    const int pMax = colR - runW - PG_GAP - pw;
    if (px > pMax) px = pMax;
    canvas_text(px, s.y + 5, kPgProfileCaption, c, FONT_SMALL);
  }
  const UiRect r = ui_row(cursor, GROUP_H, GROUP_GAP);
  const PgCols k = pg_cols(r.w);
  canvas_text(r.x + k.ego + pg_col_w() - canvas_text_width("EGO", FONT_SMALL), r.y + 5, "EGO", c, FONT_SMALL);
  canvas_text(r.x + k.phone + pg_small_w(kPgPhoneCaption) - canvas_text_width(kPgPhoneCaption, FONT_SMALL), r.y + 5,
              kPgPhoneCaption, c, FONT_SMALL);
  canvas_text(r.x + k.helm + pg_small_w(kPgHelmCaption) - canvas_text_width(kPgHelmCaption, FONT_SMALL), r.y + 5,
              kPgHelmCaption, c, FONT_SMALL);
  canvas_text(r.x + k.tick + pg_diff_w() - canvas_text_width(kPgDiffCaption, FONT_SMALL), r.y + 5, kPgDiffCaption, c,
              FONT_SMALL);
  // The heading row is not a pg_row_begin row, so it takes the last slot's key for its tip (no chapter row reaches it).
  s_lastRow = r;
  s_lastRowKey = s_tab * 1000 + ANIM_ROW_SLOTS;
  const UiRect cap = { r.x + k.tick, r.y, pg_diff_w(), r.h };
  if (ui_hit(cap, g_ui.mx, g_ui.my)) row_hint(PG_DIFF_TIP);
}

// One chapter: its title (cut to fit), a "Now" tag on the map in play, four difficulty ticks (filled once finished
// on that difficulty; none for Press Conference, which keeps no completion), its helmets and phones N/M where it
// has any, and its ego items N/M: green when all are found, hazard otherwise, faint for a chapter with no ego items.
static void pg_row_ratio(const UiRect& r, int x, int w, int found, int total) {
  if (total <= 0) return;
  char v[16];
  pg_ratio(v, sizeof v, found, total);
  canvas_text(r.x + x + w - canvas_text_width(v, FONT_MONO), r.y + 7, v,
              pres_content_colour(found >= total ? C_GREEN : C_HAZARD), FONT_MONO);
}
static void pg_row_chapter(UiRect& cursor, const PgChapter& c, int index, bool now) {
  UiRect r;
  if (!pg_row_begin(cursor, &r)) return;
  const PgCols k = pg_cols(r.w);
  const int colX = k.ego;
  const int tickX = k.tick;   // title and Now tag stop short of the column
  static const char kNow[] = "Now";
  const int tagW = now ? canvas_text_width(kNow, FONT_SMALL) + 2 * PG_TAG_PAD : 0;
  char title[PG_TITLE_CAP + 4];
  pg_fit(c.title, title, sizeof title, tickX - PG_GAP - (tagW ? tagW + PG_GAP : 0), FONT_UI);
  canvas_text(r.x, r.y + 6, title, pres_content_colour(C_BONE), FONT_UI);
  if (tagW) {
    const int tx = canvas_text_width(title, FONT_UI) + PG_GAP;
    if (tx + tagW + PG_GAP <= tickX) {
      const UiRect tag = { r.x + tx, r.y + 6, tagW, 18 };
      canvas_round_rect_outline(tag.x, tag.y, tag.w, tag.h, ROW_R, 1, pres_content_colour(C_PROGRESS));
      canvas_text(tag.x + PG_TAG_PAD, tag.y + (tag.h - font_height[FONT_SMALL]) / 2, kNow,
                  pres_content_colour(C_PROGRESS), FONT_SMALL);
    }
  }
  if (index < PG_COMPLETION_LEVELS) {
    const int py = r.y + (r.h - PG_TICK) / 2;
    const unsigned fill = pres_content_colour(C_GREEN), rim = pres_content_colour(C_RAISED);
    for (int d = 0; d < PG_DIFFS; ++d) {
      const int px = r.x + k.pips + d * (PG_TICK + PG_TICK_GAP);
      if (c.done[d]) { canvas_rect(px, py, PG_TICK, PG_TICK, fill); continue; }
      canvas_rect(px, py, PG_TICK, 1, rim);
      canvas_rect(px, py + PG_TICK - 1, PG_TICK, 1, rim);
      canvas_rect(px, py + 1, 1, PG_TICK - 2, rim);
      canvas_rect(px + PG_TICK - 1, py + 1, 1, PG_TICK - 2, rim);
    }
  }
  pg_row_ratio(r, k.helm, pg_small_w(kPgHelmCaption), c.helmFound, c.helmTotal);
  pg_row_ratio(r, k.phone, pg_small_w(kPgPhoneCaption), c.phoneFound, c.phoneTotal);
  char v[16];
  pg_ratio(v, sizeof v, c.egoFound, c.egoTotal);
  const unsigned vc = c.egoTotal <= 0 ? C_BONEFNT : (c.egoFound >= c.egoTotal ? C_GREEN : C_HAZARD);
  canvas_text(r.x + colX + pg_col_w() - canvas_text_width(v, FONT_MONO), r.y + 7, v, pres_content_colour(vc), FONT_MONO);
}

// The Base / DLC switch: two segments in one sunken well where a dropdown's
// box sits; the chosen one is raised with the tab's teal under it. Returns the view, changed on a click.
enum { PG_SEG_PAD = 10, PG_SEG_BAR = 2 };
static int pg_campaign_row(UiRect& cursor, int view) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; s_rowsDropped = 1; return view; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;
  static const char* const kSeg[2] = { "Base game", "DLC" };
  int segW = 0;
  for (int i = 0; i < 2; ++i) {
    const int w = canvas_text_width(kSeg[i], FONT_MONO) + 2 * PG_SEG_PAD;
    if (w > segW) segW = w;
  }
  const UiRect well = { r.x + r.w - VALUE_MARGIN - 2 * segW - 4, r.y + 4, 2 * segW + 4, ROW_H - 8 };
  canvas_text(r.x, r.y + 6, "Campaign", pres_content_colour(C_BONE), FONT_UI);
  canvas_bevel(well.x, well.y, well.w, well.h, pres_content_colour(C_WELL), pres_content_colour(C_BEVELLO),
               pres_content_colour(C_RAISED));
  int picked = view;
  for (int i = 0; i < 2; ++i) {
    const UiRect s = { well.x + 2 + i * segW, well.y + 2, segW, well.h - 4 };
    const bool on = view == i, hot = !on && ui_hit(s, g_ui.mx, g_ui.my);
    if (on) {
      canvas_bevel(s.x, s.y, s.w, s.h, pres_content_colour(C_RAISED), pres_content_colour(C_BEVELHI),
                   pres_content_colour(C_BEVELLO));
      canvas_rect(s.x + 2, s.y + s.h - PG_SEG_BAR - 1, s.w - 4, PG_SEG_BAR, pres_content_colour(C_PROGRESS));
    }
    const int tw = canvas_text_width(kSeg[i], FONT_MONO);
    canvas_text(s.x + (s.w - tw) / 2, s.y + (s.h - font_height[FONT_MONO]) / 2 - 1, kSeg[i],
                pres_content_colour(on ? C_BONE : (hot ? C_BONE : C_BONEDIM)), FONT_MONO);
    if (!on && ui_row_accepts_click(true, row_pressed(), ui_hit(s, g_ui.mx, g_ui.my))) {
      s_pressClaimed = true;
      picked = i;
    }
  }
  return picked;
}

// The DLC's chapters, its run totals and the profile count achievement 61 waits on (dlc.md section 6): no helmets
// or phones, so their columns read "-".
static void pg_row_dash(const UiRect& r, int x, int w) {
  canvas_text(r.x + x + w - canvas_text_width("-", FONT_MONO), r.y + 7, "-", pres_content_colour(C_BONEFNT), FONT_MONO);
}
static void tab_progress_dlc(UiRect& cursor) {
  const PgDlcTotals t = progress_dlc_totals();
  int n = 0;
  const PgChapter* ch = progress_dlc_chapters(&n);
  const int now = progress_dlc_chapter_now();
  draw_group(cursor, "THIS PLAYTHROUGH");
  char v[32];
  pg_ratio(v, sizeof v, t.run.awardsFound, t.run.awardsTotal);
  pg_row_value(cursor, "Ego awards", v, t.run.awardsFound >= t.run.awardsTotal ? C_GREEN : C_HAZARD);
  row_hint("The Doctor Who Cloned Me's own ego awards found in this playthrough; each raises the ego cap. A New Game "
           "clears them.");
  _snprintf_s(v, sizeof v, _TRUNCATE, "%d/%d", t.run.capPermanent, t.run.capMax);
  pg_row_value(cursor, "Ego cap", v, t.run.capPermanent >= t.run.capMax ? C_GREEN : C_HAZARD);
  row_hint("The DLC's ego cap: the base 40, the 30 the DLC adds at the start and the found awards' caps, over its "
           "maximum.");
  if (t.run.capNowOk) {
    _snprintf_s(v, sizeof v, _TRUNCATE, "%d", t.run.capNow);
    pg_row_value(cursor, "In this level", v, C_BONE, true);
    row_hint("Duke's ego cap right now.");
  }
  draw_group(cursor, "PROFILE");
  pg_ratio(v, sizeof v, t.ever, t.everTotal);
  pg_row_value(cursor, "Ego awards ever found", v, t.ever >= t.everTotal ? C_GREEN : C_HAZARD);
  row_hint("The DLC's ego awards found in any playthrough, as the game records them for I Am All That Is Man - Again!. "
           "Kept across New Games.");
  pg_chapters_heading(cursor);
  for (int i = 0; i < n; ++i) {
    pg_row_chapter(cursor, ch[i], i, i == now);
    if (s_lastRowKey >= 0) {
      const PgCols k = pg_cols(s_lastRow.w);
      pg_row_dash(s_lastRow, k.helm, pg_small_w(kPgHelmCaption));
      pg_row_dash(s_lastRow, k.phone, pg_small_w(kPgPhoneCaption));
    }
    char done[96], hint[320];
    pg_done_text(ch[i], i, progress_diff_names(), done, sizeof done);
    if (ch[i].egoTotal > 0)
      _snprintf_s(hint, sizeof hint, _TRUNCATE, "%s (%s): %d of %d ego items found this playthrough; %s (profile).",
                  ch[i].title, ch[i].url, ch[i].egoFound, ch[i].egoTotal, done);
    else
      _snprintf_s(hint, sizeof hint, _TRUNCATE, "%s (%s): no ego items; %s (profile).", ch[i].title, ch[i].url, done);
    row_hint(hint);
  }
}

static int s_pgView = PG_VIEW_BASE;
static unsigned s_pgSeenFrame = ~0u;

static void tab_progress(UiRect& cursor) {
  // Opens on the campaign in play (not drawn last frame: the tab or the panel just opened), then the switch holds.
  const int opening = s_pgSeenFrame + 1u != s_frameNo;
  s_pgSeenFrame = s_frameNo;
  s_pgView = pg_view_pick(s_pgView, opening, progress_is_dlc());
  if (const char* note = progress_note()) draw_placeholder(cursor, note);
  s_pgView = pg_campaign_row(cursor, s_pgView);
  row_hint("Base game: Duke Nukem Forever's chapters. DLC: The Doctor Who Cloned Me's. The tab opens on the one "
           "you are playing.");
  if (s_pgView == PG_VIEW_DLC) {
    tab_progress_dlc(cursor);
    return;
  }
  const PgTotals t = progress_totals();
  int n = 0;
  const PgChapter* ch = progress_chapters(&n);
  const int now = progress_chapter_now();

  draw_group(cursor, "THIS PLAYTHROUGH");
  char v[32];
  pg_ratio(v, sizeof v, t.run.awardsFound, t.run.awardsTotal);
  pg_row_value(cursor, "Ego awards", v, t.run.awardsFound >= t.run.awardsTotal ? C_GREEN : C_HAZARD);
  row_hint("Ego awards found in this playthrough, in any chapter or Duke's digs; each raises the ego cap. A New Game "
           "clears them.");
  _snprintf_s(v, sizeof v, _TRUNCATE, "%d/%d", t.run.capPermanent, t.run.capMax);
  pg_row_value(cursor, "Ego cap", v, t.run.capPermanent >= t.run.capMax ? C_GREEN : C_HAZARD);
  row_hint("The ego cap this playthrough's awards give, over its maximum. A New Game starts it over.");
  if (t.run.capNowOk) {
    _snprintf_s(v, sizeof v, _TRUNCATE, "%d", t.run.capNow);
    pg_row_value(cursor, "In this level", v, C_BONE, true);
    row_hint("Duke's ego cap right now; some levels raise it for that level only.");
  }

  draw_group(cursor, "PROFILE");
  _snprintf_s(v, sizeof v, _TRUNCATE, "%d/%d", t.profile.helmets, PG_HELMETS);
  pg_row_value(cursor, "Helmets", v, t.profile.helmets >= PG_HELMETS ? C_GREEN : C_HAZARD);
  row_hint("Hidden helmets found, as the game records them for their achievement. Kept across New Games.");
  _snprintf_s(v, sizeof v, _TRUNCATE, "%d/%d", t.profile.phones, PG_PHONES);
  pg_row_value(cursor, "Phone messages", v, t.profile.phones >= PG_PHONES ? C_GREEN : C_HAZARD);
  row_hint("Phone messages heard, as the game records them for their achievement. Kept across New Games.");

  pg_chapters_heading(cursor);
  for (int i = 0; i < n; ++i) {
    pg_row_chapter(cursor, ch[i], i, i == now);
    char done[96], extra[96] = "", hint[320];
    pg_done_text(ch[i], i, progress_diff_names(), done, sizeof done);
    if (ch[i].helmTotal > 0 || ch[i].phoneTotal > 0)   // section 6: no chapter has both
      _snprintf_s(extra, sizeof extra, _TRUNCATE, "; %d of %d %s (profile)",
                  ch[i].helmTotal > 0 ? ch[i].helmFound : ch[i].phoneFound,
                  ch[i].helmTotal > 0 ? ch[i].helmTotal : ch[i].phoneTotal,
                  ch[i].helmTotal > 0 ? "helmets found" : "phone messages heard");
    if (ch[i].egoTotal > 0)
      _snprintf_s(hint, sizeof hint, _TRUNCATE, "%s (%s): %d of %d ego items found this playthrough; %s (profile)%s.",
                  ch[i].title, ch[i].url, ch[i].egoFound, ch[i].egoTotal, done, extra);
    else
      _snprintf_s(hint, sizeof hint, _TRUNCATE, "%s (%s): no ego items; %s (profile)%s.", ch[i].title, ch[i].url, done,
                  extra);
    row_hint(hint);
  }
}

// The Achievements tab: The rows are two lines tall: a medal (gold with a tick once earned; a gold rim while its
// markers show), the name over the description, the state (earned date, progress, Missable) and the row's own switch.
// Earned rows are dimmed and sorted last in their group (achhelp.cpp).
enum { AH_UI_ROW_H = 44, AH_UI_HEAD_H = 34, AH_UI_MEDAL = 12, AH_UI_SW_W = 42, AH_UI_TAG_PAD = 4 };

// The chapter's name, "N of M earned", and a gold tally of the same fraction under both.
static void ah_header(UiRect& cursor, const AhRow* rows, int n) {
  if (cursor.h < AH_UI_HEAD_H) { s_rowsDropped = 1; return; }
  const UiRect r = ui_row(cursor, AH_UI_HEAD_H, GROUP_GAP);
  int earned = 0;
  for (int i = 0; i < n; ++i) earned += rows[i].earned != 0;
  char count[32], title[PG_TITLE_CAP + 4];
  ah_count_text(earned, n, count, sizeof count);
  const int cw = canvas_text_width(count, FONT_MONO);
  const int barW = r.w - VALUE_MARGIN;
  pg_fit(achhelp_title()[0] ? achhelp_title() : "This level", title, sizeof title, barW - cw - PG_GAP, FONT_UI);
  canvas_text(r.x, r.y + 4, title, pres_content_colour(C_BONE), FONT_UI);
  canvas_text(r.x + barW - cw, r.y + 5, count, pres_content_colour(n && earned >= n ? C_GREEN : C_ACHIEVE), FONT_MONO);
  const int by = r.y + AH_UI_HEAD_H - 7;
  canvas_rect(r.x, by, barW, 3, pres_content_colour(C_WELL));
  const int fill = n > 0 ? barW * earned / n : 0;
  if (fill > 0) canvas_rect(r.x, by, fill, 3, pres_content_colour(C_ACHIEVE));
  // Not a counted row: its tip takes the slot past the last one, as the Progress heading does.
  s_lastRow = r;
  s_lastRowKey = s_tab * 1000 + ANIM_ROW_SLOTS;
  row_hint("The achievements this level can still award or already did, with Steam's state. Each row's switch shows "
           "gold markers on its targets.");
}

// A line of warning in red, its full text in the tip.
static void ah_warning(UiRect& cursor, const char* text) {
  if (cursor.h < ROW_H) { s_rowsDropped = 1; return; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const int w = r.w - VALUE_MARGIN;
  canvas_round_rect_outline(r.x, r.y + 3, w, r.h - 6, ROW_R, 1, pres_content_colour(C_RED));
  char shown[160];
  pg_fit(text, shown, sizeof shown, w - 16, FONT_SMALL);
  canvas_text(r.x + 8, r.y + (r.h - font_height[FONT_SMALL]) / 2, shown, pres_content_colour(C_RED), FONT_SMALL);
  s_lastRow = r;
  s_lastRowKey = s_tab * 1000 + ANIM_ROW_SLOTS;
  row_hint(text);
}

static void ah_medal(int x, int y, const AhRow& a) {
  if (a.earned) {
    canvas_rect(x, y, AH_UI_MEDAL, AH_UI_MEDAL, pres_content_colour(canvas_with_alpha(C_ACHIEVE, 0xB0)));
    const unsigned t = pres_content_colour(C_PANEL);
    canvas_line(x + 2, y + 6, x + 5, y + 9, t);
    canvas_line(x + 5, y + 9, x + 10, y + 3, t);
    return;
  }
  const unsigned rim = pres_content_colour(a.espOn ? C_ACHIEVE : C_RAISED);
  canvas_round_rect_outline(x, y, AH_UI_MEDAL, AH_UI_MEDAL, 2, 1, rim);
}

static void ah_row(UiRect& cursor, const AhRow& a) {
  if (cursor.h < AH_UI_ROW_H) { s_lastRowKey = -1; s_rowsDropped = 1; return; }
  const UiRect r = ui_row(cursor, AH_UI_ROW_H, ROW_GAP);
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;
  const UiRect sw = { r.x + r.w - AH_UI_SW_W, r.y + (AH_UI_ROW_H - 20) / 2, AH_UI_SW_W, 20 };

  // The state column, right-aligned before the switch's slot (kept on every row so the columns line up).
  const int stateR = sw.x - PG_GAP;
  char top[32] = "";
  unsigned topC = C_HAZARD;
  int topFont = FONT_MONO;
  if (a.earned) {
    ah_earned_text(a.unlock, a.offMin, top, sizeof top);
    topC = C_GREEN;
    topFont = FONT_SMALL;
  } else if (a.progress[0]) {
    strcpy_s(top, sizeof top, a.progress);
  } else if (!a.answered) {
    strcpy_s(top, sizeof top, "No Steam state");
    topC = C_BONEFNT;
    topFont = FONT_SMALL;
  }
  static const char kMissable[] = "Missable";
  const bool missTag = a.missable && !a.earned;
  const int topW = top[0] ? canvas_text_width(top, topFont) : 0;
  const int tagW = missTag ? canvas_text_width(kMissable, FONT_SMALL) + 2 * AH_UI_TAG_PAD : 0;
  const int stateW = topW > tagW ? topW : tagW;
  if (top[0])
    canvas_text(stateR - topW, r.y + (missTag ? 5 : (AH_UI_ROW_H - font_height[topFont]) / 2), top,
                pres_content_colour(topC), topFont);
  if (missTag) {
    const int ty = top[0] ? r.y + 23 : r.y + (AH_UI_ROW_H - 16) / 2;
    canvas_round_rect_outline(stateR - tagW, ty, tagW, 16, ROW_R, 1, pres_content_colour(C_RED));
    canvas_text(stateR - tagW + AH_UI_TAG_PAD, ty + (16 - font_height[FONT_SMALL]) / 2, kMissable,
                pres_content_colour(C_RED), FONT_SMALL);
  }

  // Medal, name, description.
  ah_medal(r.x, r.y + 7, a);
  const int textX = r.x + AH_UI_MEDAL + 8;
  const int textW = stateR - (stateW ? stateW + PG_GAP : 0) - textX;
  char name[80], desc[180];
  pg_fit(a.name, name, sizeof name, textW, FONT_UI);
  pg_fit(a.desc, desc, sizeof desc, textW, FONT_SMALL);
  const bool hovered = a.hasSwitch && ui_hit(sw, g_ui.mx, g_ui.my);
  field_hover_step(idx, hovered);
  canvas_text(textX, r.y + 4, name,
              pres_content_colour(pres_hover_lighten(a.earned ? C_BONEFNT : C_BONE, s_pres.hover[idx])), FONT_UI);
  canvas_text(textX, r.y + 25, desc, pres_content_colour(a.earned ? C_BONEFNT : C_BONEDIM), FONT_SMALL);

  // The row's switch, the panel's own: on shows this achievement's markers.
  if (!a.hasSwitch) return;
  if (mp_refused()) {   // refused in multiplayer: the Unavailable tag in the switch's place, as the other rows
    const int tagW = canvas_text_width(MENU_UNAVAILABLE_TAG, FONT_SMALL) + 12;
    const int tagX = sw.x + sw.w - tagW;
    canvas_round_rect_outline(tagX, sw.y, tagW, sw.h, ROW_R, 1, pres_content_colour(C_RAISED));
    canvas_text(tagX + 6, sw.y + (sw.h - font_height[FONT_SMALL]) / 2, MENU_UNAVAILABLE_TAG,
                pres_content_colour(C_BONEFNT), FONT_SMALL);
    return;
  }
  int knobX;
  if (anim_style() == ANIM_OFF) {
    knobX = a.espOn ? sw.x + 23 : sw.x + 2;
  } else {
    const double targetX = a.espOn ? 1.0 : 0.0;
    if (s_tabSwitchedThisFrame) { s_pres.knob[idx].x = targetX; s_pres.knob[idx].v = 0.0; }
    else anim_spring_step(&s_pres.knob[idx], targetX, ANIM_SPRING_CRITICAL.stiffness, ANIM_SPRING_CRITICAL.damping,
                          s_frameDt);
    knobX = sw.x + 2 + (int)(21.0 * s_pres.knob[idx].x + 0.5);
  }
  canvas_bevel(sw.x, sw.y, sw.w, sw.h, pres_content_colour(a.espOn ? C_ACHIEVE : C_WELL),
               pres_content_colour(C_BEVELLO), pres_content_colour(C_RAISED));
  canvas_rect(knobX, sw.y + 2, 17, 16,
              pres_content_colour((anim_style() == ANIM_OFF ? a.espOn != 0 : s_pres.knob[idx].x >= 0.5) ? C_BONE
                                                                                                       : C_RAISED));
  if (ui_row_accepts_click(!mp_refused(), row_pressed(), ui_hit(sw, g_ui.mx, g_ui.my))) {
    s_pressClaimed = true;
    achhelp_set_switch(a.id, !a.espOn);
  }
}

// The row's tip: Steam's description, how (the table's note) when it says more, and what the switch does.
static void ah_row_hint(const AhRow& a) {
  char hint[256], state[48] = "";
  if (a.earned) ah_earned_text(a.unlock, a.offMin, state, sizeof state);
  const bool sameNote = strcmp(a.desc, a.note) == 0;
  const char* sw = !a.hasSwitch ? (a.group == AH_SCOPE_LEVEL     ? " Nothing to mark: the Enemies row marks a boss."
                                    : a.group == AH_SCOPE_COUNTER ? " Nothing to mark: it counts kills."
                                                                  : "")
                   : mp_refused() ? " Markers: " MP_WHY "."
                   : a.mode == AH_MODE_AUTO ? " Markers: on until earned."
                   : a.mode == AH_MODE_ON ? " Markers: on." : " Markers: off.";
  _snprintf_s(hint, sizeof hint, _TRUNCATE, "%s%s%s%s%s%s%s", a.desc, sameNote ? "" : " ", sameNote ? "" : a.note,
              a.missable && !a.earned ? " Missable." : "", state[0] ? " " : "", state, sw);
  row_hint(hint);
}

static void tab_achievements(UiRect& cursor) {
  const AhRow* rows = 0;
  const int n = achhelp_rows(&rows);
  if (!achhelp_in_level()) {
    draw_placeholder(cursor, "Load a level to see what it can award.");
    return;
  }
  if (achhelp_map_pending()) {   // a checkpoint's map name is read again each second until it resolves
    draw_placeholder(cursor, "Reading which map this checkpoint belongs to...");
    return;
  }
  if (achhelp_mydigs()) {   // the native award refuses there (achievements.md section 1)
    draw_placeholder(cursor, "Nothing can be earned here.");
    draw_placeholder(cursor, "The game awards no achievements in Duke's digs.");
    return;
  }
  ah_header(cursor, rows, n);
  if (achhelp_cheated() == 1) ah_warning(cursor, "Cheats were used this level: the game will not award achievements");
  if (!n) {
    draw_placeholder(cursor, "Nothing in this level counts toward an achievement.");
    return;
  }
  static const char* const kGroups[AH_GROUPS] = { "THIS LEVEL", "COLLECTED ACROSS LEVELS", "COUNTERS" };
  int group = -1;
  for (int i = 0; i < n; ++i) {
    if (rows[i].group != group) {
      group = rows[i].group;
      draw_group(cursor, kGroups[group >= 0 && group < AH_GROUPS ? group : 0]);
    }
    ah_row(cursor, rows[i]);
    ah_row_hint(rows[i]);
  }
}

// A button row (achievement reset): the bind row's well holding the action, a click anywhere on the
// row presses it. `armed` turns the row red, its label the confirm prompt (a smaller font, then cut, if it does not
// fit). `unavailable` shows the Unavailable tag instead of the well. Returns true on a click.
static bool row_button(UiRect& cursor, const char* label, const char* action, bool enabled, bool armed,
                       bool unavailable) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; s_rowsDropped = 1; return false; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;
  const int fieldW = field_well_w();
  const UiRect field = { r.x + r.w - fieldW, r.y + 5, fieldW, 20 };
  const bool live = enabled && !unavailable;
  field_hover_step(idx, live && ui_hit(r, g_ui.mx, g_ui.my));
  const double hv = s_pres.hover[idx];
  char shown[160];
  const int labelW = field.x - r.x - PG_GAP;
  int font = FONT_UI;
  pg_fit(label, shown, sizeof shown, labelW, FONT_UI);
  if (strcmp(shown, label) != 0) { font = FONT_SMALL; pg_fit(label, shown, sizeof shown, labelW, FONT_SMALL); }
  const unsigned labelC = armed ? C_RED : pres_hover_lighten(live ? C_BONE : C_BONEFNT, hv);
  canvas_text(r.x, r.y + (r.h - font_height[font]) / 2, shown, pres_content_colour(labelC), font);
  if (unavailable) {
    const int tagPad = 6;
    const int tagW = canvas_text_width(MENU_UNAVAILABLE_TAG, FONT_SMALL) + 2 * tagPad;
    const int tagX = field.x + field.w - tagW;
    canvas_round_rect_outline(tagX, field.y, tagW, field.h, ROW_R, 1, pres_content_colour(C_RAISED));
    canvas_text(tagX + tagPad, field.y + (field.h - font_height[FONT_SMALL]) / 2, MENU_UNAVAILABLE_TAG,
                pres_content_colour(C_BONEFNT), FONT_SMALL);
    return false;
  }
  canvas_round_rect(field.x, field.y, field.w, field.h, ROW_R, pres_content_colour(pres_hover_toward(C_WELL, C_RAISED, hv)));
  canvas_round_rect_outline(field.x, field.y, field.w, field.h, ROW_R, 1,
                            pres_content_colour(armed ? C_RED : pres_hover_toward(C_RAISED, C_BONEDIM, hv)));
  const int aw = canvas_text_width(action, FONT_MONO);
  canvas_text(field.x + (field.w - aw) / 2, field_text_y(field), action,
              pres_content_colour(armed ? C_RED : (live ? C_BONE : C_BONEFNT)), FONT_MONO);
  if (ui_row_accepts_click(live, row_pressed(), ui_hit(r, g_ui.mx, g_ui.my))) {
    s_pressClaimed = true;
    s_pressSpent = true;
    return true;
  }
  return false;
}

// The status line's table. ESP, damage numbers, level stats, freecam, photo, FOV, post processing and the display
// settings change what is drawn, not the game, so they have no entry. Fire rate and Reload speed are sliders: live
// while off their stock stop. Big Package is the game's own pre-order bonus, not a cheat, so it has no entry.
static int live_table(MenuLive* t) {
  int n = 0;
  auto add = [&](const char* name, int on, const char* why) {
    if (n < MENU_LIVE_MAX) t[n++] = MenuLive{ name, on != 0, why != nullptr };
  };
  add("Aimbot", g_cfg.aimbotOn, aimbot_refused());
  add("Silent aim", g_cfg.silentOn, aim_refused());
  add("Triggerbot", g_cfg.tbotOn, tbot_refused());
  add("Auto-execute", g_cfg.autoExecute, autoexec_refused());
  add("Perfect accuracy", g_cfg.accuracyOn, accuracy_refused());
  add("No recoil", g_cfg.recoilOn, recoil_refused());
  add("Fire rate", weap_fire_pct_clamped(g_cfg.fireRatePct) != WEAP_FIRE_MIN, handling_refused());
  add("Reload speed", weap_reload_pct_clamped(g_cfg.reloadPct) != WEAP_FIRE_MIN, handling_refused());
  add("Game speed", g_gameSpeedOn, gamespeed_refused());
  add("Auto-win QTEs", g_cfg.autoQte, autowin_qte_refused());
  add("Auto-win minigames", g_cfg.autoMinigames, autowin_minigames_refused());
  add("Pinball autoplay", g_cfg.pinballAuto, pinbot_refused());
  add("Arcade autoplay", g_cfg.arcadeAuto, arcbot_refused());
  return n;
}

void menu_draw(int screenW, int screenH) {
  // --- The presentation preamble, once per frame ---------------------------
  s_pres.rowsThisFrame = 0;
  s_plate.open = false; s_plateOrd = 0; s_subPrevY = -100000;
  s_rowsDropped = 0;
  s_hoverRowKey = -1;
  s_hoverHint = nullptr;
  s_lastRowKey = -1;
  s_glyphHint = nullptr;
  if (!g_ui.down) { s_pressSpent = false; s_entryCaretDrag = false; }
  s_pressClaimed = false;
  s_entrySeenThisFrame = false;
  s_sliderInvLen = 0; s_sliderInv[0] = 0; s_sliderInvRows = 0;
  s_frameNo++;
  bool newSession = false;
  {
    const unsigned now = menu_now();
    // A gap this long means no frame drew the panel: its state is a finished session's.
    const bool stale = (now - s_pres.lastMs) > ANIM_STALE_MS;
    s_frameDt = anim_dt_clamped(s_pres.lastMs, now);
    s_pres.lastMs = now;
    s_pres.openTarget = input_panel_open() ? 1 : 0;
    if (s_pres.openTarget && (!s_pres.wasOpen || stale)) {
      newSession = true;
      menu_note_new_session();
      for (int i = 0; i < TAB_COUNT; i++) { s_scroll[i] = 0; s_sliderLoggedRows[i] = 0; }
      if (stale) { s_pres.open.x = 0.0; s_pres.open.v = 0.0; }
    }
    s_pres.wasOpen = s_pres.openTarget;
    if (anim_style() == ANIM_OFF) {
      s_pres.open.x = (double)s_pres.openTarget;
      s_pres.open.v = 0.0;
    } else {
      anim_spring_step(&s_pres.open, (double)s_pres.openTarget,
                       ANIM_SPRING_CRITICAL.stiffness, ANIM_SPRING_CRITICAL.damping, s_frameDt);
    }
  }
  // Logical pixels from here on; the canvas scales them out and is back at 100 before this returns (the ESP shares it).
  // `settled` is what this screen can show; the drawn scale stays at its pre-press value while the button is down
  // (ui_scale_effective), so the panel resizes once, on release. Read back: a refused atlas draws at 100.
  const int settled = menu_applied_scale(screenW, screenH);
  const int wantPct = ui_scale_effective(settled, s_scaleHeld, g_ui.down);
  if (!g_ui.down) s_scaleHeld = settled;
  canvas_set_scale(wantPct);
  const int pct = canvas_scale();
  s_drawnPct = pct;
  const int deviceMx = g_ui.mx, deviceMy = g_ui.my;
  g_ui.mx = ui_unscale_px(deviceMx, pct);
  g_ui.my = ui_unscale_px(deviceMy, pct);
  const int logicalW = ui_unscale_px(screenW, pct);
  const int logicalH = ui_unscale_px(screenH, pct);
  s_frameMx = g_ui.mx;

  // An open field claims the left press wherever it lands: inside the well it is the caret, outside it is
  // the close (a commit), and neither may also grab the panel. Before the drag machine takes the press.
  if (s_entry.open && (g_ui.pressed || g_ui.rpressed) && !s_dragLive) {
    if (g_ui.pressed) { s_pressClaimed = true; s_pressSpent = true; }
    if (!ui_hit(s_entryFieldRect, g_ui.mx, g_ui.my)) entry_clickaway_commit();
  }

  // --- The drag, before anything is drawn -----------------------------------
  // The grip is asked first inside panel_drag_step, so it wins at the corner.
  const PanelBox laid = panel_drawn_box(screenH, pct, logicalW, logicalH);
  PanelBox box = laid;
  bool gripHit = false;
  {
    const bool titleHit = ui_hit(panel_title_rect(laid, STRIP_H, STATUS_H), g_ui.mx, g_ui.my);
    gripHit = ui_hit(panel_grip_rect(s_dragLive ? s_dragBox : laid), g_ui.mx, g_ui.my);
    bool ended = false;
    s_drag = panel_drag_step(s_drag, g_ui.pressed, g_ui.down, titleHit, gripHit,
                             g_ui.mx, g_ui.my, laid, logicalW, logicalH, &box, &ended, ui_panel_h_cap(screenH, pct));
    s_dragLive = s_drag.phase != PDRAG_IDLE;
    if (ended) {
      box = s_dragBox;
      // A click that moved nothing stores nothing.
      if (box.x != laid.x || box.y != laid.y || box.w != laid.w || box.h != laid.h) {
        g_cfg.panelx = box.x; g_cfg.panely = box.y;
        // Only a resize stores a size: a move at a high UI scale must not save the capped one.
        if (box.w != laid.w) g_cfg.panelw = box.w;
        if (box.h != laid.h) g_cfg.panelh = box.h;
        g_cfgDirty = 1;
      }
    } else if (s_dragLive) {
      s_dragBox = box;
    }
  }
  const UiRect p = { box.x, box.y, box.w, box.h };

  // The status line's count and tooltip: every switch that changes gameplay while on and available (live_table).
  MenuLive live[MENU_LIVE_MAX];
  const int liveN = live_table(live);
  const int liveCount = menu_live_count(live, liveN);
  const unsigned strip = liveCount ? C_RED : C_HAZARD;

  // The whole panel slides as one unit under Simple; hit tests stay logical.
  if (anim_style() == ANIM_SIMPLE) {
    const int slideY = (int)((1.0 - s_pres.open.x) * (double)SIMPLE_SLIDE_PX + 0.5);
    canvas_set_transform(0, -slideY, 1.0, 0, 0);
  } else {
    canvas_set_transform(0, 0, 1.0, 0, 0);
  }

  canvas_bevel(p.x, p.y, p.w, p.h,
               pres_colour(C_PANEL), pres_colour(C_BEVELHI), pres_colour(C_BEVELLO));
  canvas_hazard(p.x + 1, p.y + 1, p.w - 2, STRIP_H, pres_colour(strip), pres_colour(C_PANEL), 9);

  // Status line: the lamp and one sentence of state.
  const int sy = p.y + 1 + STRIP_H;
  canvas_rect(p.x + 1, sy, p.w - 2, STATUS_H, pres_colour(C_RECESS));
  canvas_rect(p.x + PAD, sy + 11, 9, 9, pres_colour(strip));
  char status[32];
  if (liveCount) sprintf_s(status, "%d cheat%s live", liveCount, liveCount == 1 ? "" : "s");
  else           sprintf_s(status, "Idle");
  canvas_text(p.x + PAD + 18, sy + 7, status, pres_colour(C_BONEDIM), FONT_SMALL);
  if (liveCount && !s_dragLive && ui_hit(UiRect{ p.x + 1, sy, p.w - 2, STATUS_H }, g_ui.mx, g_ui.my)) {
    char names[200];
    menu_live_names(live, liveN, names, sizeof names);
    _snprintf_s(s_hintBuf, sizeof s_hintBuf, _TRUNCATE, "Live: %s", names);
    s_hoverRowKey = STATUS_TIP_KEY;
    s_hoverHint = s_hintBuf;
  }
  // The panel key by name, through the same fallback input.cpp applies.
  char closeText[32];
  sprintf_s(closeText, "%s closes",
            bind_label(panel_key_effective(config_bind_get(BIND_ROW_MENU))));
  canvas_text(p.x + p.w - PAD - canvas_text_width(closeText, FONT_SMALL), sy + 7,
              closeText, pres_colour(C_BONEFNT), FONT_SMALL);

  const int bodyY = sy + STATUS_H;
  const int bodyH = p.y + p.h - bodyY - 1;

  // Tab rail: the shown tabs, packed in the sibling ports' order (menu_tab_visible). The click is applied after the
  // content, so a pending commit is taken by the row that owns it. As wide as the longest name measures at this scale,
  // never under RAIL_W.
  int railLabelW = 0;
  for (int i = 0; i < TAB_COUNT; i++) {
    const int w = menu_tab_visible(i) ? canvas_text_width(menu_tab_name(i), FONT_UI) : 0;
    if (w > railLabelW) railLabelW = w;
  }
  const int railW = menu_rail_w(railLabelW);
  canvas_rect(p.x + 1, bodyY, railW, bodyH, pres_colour(C_WELL));
  int railTab = -1;
  for (int i = 0, slot = 0; i < TAB_COUNT; i++) {
    if (!menu_tab_visible(i)) continue;
    const UiRect t = { p.x + 1, bodyY + slot++ * MENU_RAIL_ENTRY_H, railW, MENU_RAIL_ENTRY_H };
    if (i == s_tab) {
      canvas_rect(t.x, t.y, t.w, t.h, pres_colour(C_PANEL));
      canvas_rect(t.x, t.y, 3, t.h, pres_colour(TAB_COLOURS[i]));
    }
    const int nameY = t.y + (t.h - font_height[FONT_UI]) / 2;
    canvas_text(t.x + RAIL_LABEL_X, nameY, menu_tab_name(i),
                pres_colour(i == s_tab ? C_BONE : C_BONEDIM), FONT_UI);
    if (g_ui.pressed && ui_hit(t, g_ui.mx, g_ui.my)) {
      railTab = i;
      s_pressClaimed = true;
    }
  }

  s_tabSwitchedThisFrame = (s_tab != s_pres.tabFrom);
  if (s_tabSwitchedThisFrame) s_pres.tabSwitchMs = s_pres.lastMs;
  s_pres.tabFrom = s_tab;
  s_foldSnap = s_tabSwitchedThisFrame || newSession;

  // --- The tab body scrolls ------------------------------------------------
  // The offset comes off the cursor's starting y, so draw and hit test follow
  // together; content is laid out into MENU_CONTENT_BUDGET and measured whole.
  const UiRect bodyRect = { p.x + 1 + railW, bodyY, p.w - railW - 2, bodyH };
  s_bodyRect = bodyRect;
  s_ddSeenThisFrame = false;
  const int viewH = bodyH - 2 * PAD;
  s_scroll[s_tab] = menu_scroll_after_wheel(s_scroll[s_tab], s_wheel, s_contentH[s_tab], viewH);
  s_wheel = 0.0;
  const int scroll = s_scroll[s_tab];

  UiRect cursor = { p.x + 1 + railW + PAD, bodyY + PAD - scroll, p.w - railW - 2 * PAD - 2, MENU_CONTENT_BUDGET };
  const int contentTop = cursor.y;
  canvas_push_clip(bodyRect.x, bodyRect.y, bodyRect.w, bodyRect.h);
  s_clipNow = bodyRect;
  s_foldAlpha = 1.0;
  // The clip stops a scrolled-out row being drawn; parking the cursor stops
  // one being clicked outside the body.
  const int bodyMx = g_ui.mx, bodyMy = g_ui.my;
  if (!ui_hit(bodyRect, g_ui.mx, g_ui.my)) {
    g_ui.mx = MENU_MOUSE_NOWHERE; g_ui.my = MENU_MOUSE_NOWHERE;
  }
  switch (s_tab) {
    case TAB_VISUALS: {
      // duke-dnmp's Visuals rows less what this game lacks (no arrows, no ambushes). No ENEMIES heading, as there:
      // it would repeat its own first row.
      plate_open(cursor, nullptr);
      // Every marker row refuses outside standalone play (mpgate.h): the row greys and its hint says why.
      const char* mpWhy = mp_refused();
      const bool mpOk = !mpWhy;
      g_cfg.espOn = row_switch_avail(cursor, "Enemies", g_cfg.espOn, C_RED, true, mpOk);
      // Hidden while the parent is off (folds): none draws on its own.
      const bool enemiesOn = g_cfg.espOn != 0 && mpOk;
      MenuFoldBlock enemyBlock;
      if (collapse_begin(&enemyBlock, cursor, enemiesOn, 5)) {
        g_cfg.espBoundingBoxes = row_switch_sub(cursor, "Bounding boxes", g_cfg.espBoundingBoxes, C_RED, enemiesOn);
        g_cfg.espHealthBars    = row_switch_sub(cursor, "Health bars",    g_cfg.espHealthBars,    C_RED, enemiesOn);
        g_cfg.espNames         = row_switch_sub(cursor, "Name and health", g_cfg.espNames,        C_RED, enemiesOn);
        g_cfg.espDistance      = row_switch_sub(cursor, "Distance",       g_cfg.espDistance,      C_RED, enemiesOn);
        g_cfg.espAllies        = row_switch_sub(cursor, "Allies",         g_cfg.espAllies,        C_FRIENDLY, enemiesOn);
        row_hint_or(mpWhy, "Friendly and neutral characters (EDF marines, civilians), in pink.");
      }
      collapse_end(enemyBlock, cursor);
      draw_group(cursor, "WORLD");
      // duke-dnmp's WORLD rows (its menu.cpp:2035-2043) in that order.
      g_cfg.espItems = row_switch_avail(cursor, "Items and pickups", g_cfg.espItems, C_BLUE, true, mpOk);
      MenuFoldBlock itemBlock;
      if (collapse_begin(&itemBlock, cursor, g_cfg.espItems != 0 && mpOk, 1)) {
        g_cfg.espHideTaken = row_switch_sub(cursor, "Hide what you can't take", g_cfg.espHideTaken, C_BLUE,
                                            g_cfg.espItems != 0 && mpOk);
        row_hint_or(mpWhy, "Hides what the game would refuse now: full ammo, items at their cap, crates whose contents all are.");
      }
      collapse_end(itemBlock, cursor);
      g_cfg.espAmmoCrates = row_switch_avail(cursor, "Ammo crates", g_cfg.espAmmoCrates, C_GREEN, true, mpOk);
      row_hint_or(mpWhy, "Infinite ammo crates and piles.");
      g_cfg.espQuest = row_switch_avail(cursor, "Quest items", g_cfg.espQuest, C_SETTINGS, true, mpOk);
      row_hint_or(mpWhy, "Batteries, gas cans and energy cells, until you carry or use them.");
      g_cfg.espBreakables = row_switch_avail(cursor, "Breakables", g_cfg.espBreakables, C_HAZARD, true, mpOk);
      row_hint_or(mpWhy, "Padlocks and breakables that open a path, a puzzle or a stash.");
      // Unfound award givers, in the Progress tab's teal.
      g_cfg.espEgo = row_switch_avail(cursor, "Ego boosts", g_cfg.espEgo, C_PROGRESS, true, mpOk);
      row_hint_or(mpWhy, "Marks each object that still gives an ego award you have not found, with the award's name. Boss and "
               "story awards are not marked. No ego awards while Extras cheats (god mode, infinite ammo, InstaGib) are on this level.");
      // Profile collectibles, each in its own colour.
      g_cfg.espHelmets = row_switch_avail(cursor, "Helmets", g_cfg.espHelmets, C_HELMET, true, mpOk);
      row_hint_or(mpWhy, "Marks each hidden helmet you have not found yet, where you must walk. Helmet C also needs a look at it.");
      g_cfg.espPhones = row_switch_avail(cursor, "Phone messages", g_cfg.espPhones, C_PHONE, true, mpOk);
      row_hint_or(mpWhy, "Marks each phone whose message you have not heard yet; use it to play the message.");
      g_cfg.dmgNumbers = row_switch_avail(cursor, "Damage numbers", g_cfg.dmgNumbers, C_HAZARD, true, mpOk);
      row_hint_or(mpWhy, "Numbers over enemies as they lose health; red on a kill.");
      // duke-dnmp's row: this chapter's count in the corner.
      g_cfg.levelStats = row_switch(cursor, "Level stats", g_cfg.levelStats, C_HAZARD, true);
      row_hint("This chapter's ego items found, top left, as the game's Chapter Select counts them.");

      draw_group(cursor, "RANGE");
      // AA's RANGE group (menu.cpp, World Tour's), in metres here. Always live, not gated by Enemies: a range, not
      // a feature; it also bounds the damage numbers. Default is the maximum (esp_layout.h).
      g_cfg.espDrawDist = row_slider(cursor, "Draw distance", g_cfg.espDrawDist, ESP_DRAWDIST_MIN, ESP_DRAWDIST_MAX,
                                     " m", true, ESP_DRAWDIST_DEFAULT);
      row_hint("Markers stop at this range.");
      // Its own term in the cull, not a magic slider value; always clickable, as AA's row.
      g_cfg.espNoLimit = row_switch_sub(cursor, "Unlimited range", g_cfg.espNoLimit, C_GREEN, true);
      row_hint("Ignores the distance.");
      break;
    }
    case TAB_PROGRESS:
      tab_progress(cursor);
      break;
    case TAB_ACHIEVEMENTS:
      tab_achievements(cursor);
      break;
    case TAB_COMBAT: {
      // duke-dnmp's TARGETING rows, labels, ranges and defaults, adapted to DNF (docs/re/aim.md). The settings fold
      // away while neither Silent aim, the Triggerbot nor the Aimbot is on, as DNMP's tgtOn. AIMBOT above it: AA's rows
      // and defaults.
      draw_group(cursor, "AIMBOT");
      const char* abWhy = aimbot_refused();
      g_cfg.aimbotOn = row_switch_avail(cursor, "Aimbot", g_cfg.aimbotOn, C_RED, true, !abWhy);
      row_hint(abWhy ? abWhy : "Turns your view onto the nearest visible enemy.");
      const bool abOn = g_cfg.aimbotOn != 0 && !abWhy;
      g_cfg.aimHold = row_switch_sub(cursor, "Hold key", g_cfg.aimHold, C_RED, abOn);
      row_hint("Only while the Aimbot button is held down.");
      // AA19: the key belongs to the row above it, and does nothing unless Hold key is on too.
      row_bind_sub(cursor, "Aimbot button", BIND_ROW_AIMBOT, abOn && g_cfg.aimHold != 0);
      row_hint("Held to aim when Hold key is on.");
      g_cfg.aimSmooth = row_slider_sub(cursor, "Smoothing", g_cfg.aimSmooth, AIMBOT_SMOOTH_MIN, AIMBOT_SMOOTH_MAX, "%",
                                       abOn, AIMBOT_SMOOTH_DEF);
      draw_group(cursor, "TARGETING");
      {
        // DNMP's rows: the switch, then Fire when and Delay under it.
        const char* tbWhy = tbot_refused();
        g_cfg.tbotOn = row_switch_avail(cursor, "Triggerbot", g_cfg.tbotOn, C_RED, true, !tbWhy);
        row_hint(tbWhy ? tbWhy : "Fires by itself while the condition below holds. With the fists it punches an "
                                 "enemy within reach. Not with the pipe bomb or trip mine.");
        const bool tbOn = g_cfg.tbotOn != 0 && !tbWhy;
        g_cfg.tbotWhen = row_slider_named_sub(cursor, "Fire when", g_cfg.tbotWhen, TBOT_WHEN_CROSSHAIR,
                                              TBOT_WHEN_AIMBOT, tbot_when_label, tbOn);
        row_hint("Crosshair: an enemy under your crosshair, never an ally in front of it. Locked: Silent aim has a "
                 "visible target; without Silent aim, or with the freeze-ray beam, as Crosshair. Aimbot: the Aimbot "
                 "holds a visible target.");
        g_cfg.tbotDelay = row_slider_sub(cursor, "Delay", g_cfg.tbotDelay, TBOT_DELAY_MS_MIN, TBOT_DELAY_MS_MAX, " ms",
                                         tbOn, TBOT_DELAY_MS_DEF);
        row_hint("How long the condition must hold before it fires.");
      }
      const char* aimWhy = aim_refused();
      g_cfg.silentOn = row_switch_avail(cursor, "Silent aim", g_cfg.silentOn, C_RED, true, !aimWhy);
      row_hint(aimWhy ? aimWhy : "Shots go to the target without moving your view. Not the pipe bomb, trip mine, "
                                 "freeze-ray beam, melee, turrets or vehicles.");
      const bool tgtOn = (g_cfg.silentOn != 0 && !aimWhy) || (g_cfg.tbotOn != 0 && !tbot_refused()) || abOn;
      MenuFoldBlock tgtBlock;
      if (collapse_begin(&tgtBlock, cursor, tgtOn, 7)) {
        g_cfg.aimTarget = row_slider_named(cursor, "Pick target", g_cfg.aimTarget, AIM_PICK_CROSSHAIR, AIM_PICK_NEAREST,
                                           aim_pick_label, tgtOn);
        row_hint("Crosshair picks what you are facing. Nearest picks by distance.");
        g_cfg.aimPoint = row_slider_named(cursor, "Aim at", g_cfg.aimPoint, AIM_AT_BODY, AIM_AT_HEAD, aim_point_label,
                                          tgtOn);
        row_hint("Head aims at the head bone where the enemy has one. Head hits do x4 with the Pistol and x2 with the "
                 "Ripper and the Minigun.");
        g_cfg.aimCone = row_slider(cursor, "Cone", g_cfg.aimCone, AIM_CONE_MIN, AIM_CONE_MAX, " deg",
                                   tgtOn, AIM_CONE_DEF);
        row_hint("How far off centre a target may be, in either Pick target mode.");
        g_cfg.aimRangeM = row_slider(cursor, "Aim range", g_cfg.aimRangeM, AIM_RANGE_MIN_M, AIM_RANGE_MAX_M, " m",
                                     tgtOn, AIM_RANGE_MAX_M);
        row_hint("Targets are visible enemies within this range. Allies are never targets. "
                 "Mounted guns use their own full reach.");
        g_cfg.aimOffscreen = row_switch(cursor, "Off-screen targets", g_cfg.aimOffscreen, C_RED, tgtOn);
        row_hint("Targets outside the screen too, behind you included.");
        g_cfg.aimLead = row_switch(cursor, "Lead moving targets", g_cfg.aimLead, C_RED, tgtOn);
        row_hint("Aims where a moving enemy will be when the rocket or grenade arrives. Off still aims the grenade "
                 "launcher's arc, or it would land short.");
        g_cfg.aimMarker = row_switch(cursor, "Aim calibration marker", g_cfg.aimMarker, C_RED, tgtOn);
        row_hint("Shows the raw aim point and the led point.");
      }
      collapse_end(tgtBlock, cursor);
      // Moved here from Settings > AUTO-WIN.
      draw_group(cursor, "EXECUTIONS");
      const char* xWhy = autoexec_refused();
      g_cfg.autoExecute = row_switch_avail(cursor, "Auto-execute enemies", g_cfg.autoExecute, C_RED, true, !xWhy);
      row_hint(xWhy ? xWhy : "Executes a stunned or frozen enemy in reach through the game's own Use, ego refill "
                             "included. Executions count toward the game's stats and its Steam achievement.");
      break;
    }
    case TAB_WEAPONS: {
      // duke-dnmp's PERFECT ACCURACY group, plus No recoil: both cover each weapon Duke holds.
      draw_group(cursor, "PERFECT ACCURACY");
      const char* accWhy = accuracy_refused();
      g_cfg.accuracyOn = row_switch_avail(cursor, "All weapons", g_cfg.accuracyOn, C_HAZARD, true, !accWhy);
      row_hint(accWhy ? accWhy : "Removes the spread of each weapon Duke holds; a shotgun puts every pellet on one "
                                 "point.");
      // Its own switch: the view kick, not the spread, is what scatters a pistol's shots.
      const char* rkWhy = recoil_refused();
      g_cfg.recoilOn = row_switch_avail(cursor, "No recoil", g_cfg.recoilOn, C_HAZARD, true, !rkWhy);
      row_hint(rkWhy ? rkWhy : "Removes each weapon's view kick when firing.");
      // DNMP's HANDLING rows (its menu.cpp): the held weapon's own animation rate and lockout (docs/re/handling.md).
      draw_group(cursor, "HANDLING");
      const char* hdWhy = handling_refused();
      {
        char val[16];
        const int fire = weap_fire_pct_clamped(g_cfg.fireRatePct);
        weap_fire_text(fire, val, sizeof val);
        // Put back on the 50 stops, so the knob only rests on one.
        const int v = weap_fire_pct_clamped(row_slider_at(cursor, "Fire rate", fire, WEAP_FIRE_MIN, WEAP_FIRE_MAX,
                                                          val, !hdWhy, 0, C_BONE, EK_INT, WEAP_FIRE_MIN));
        if (v != g_cfg.fireRatePct) { g_cfg.fireRatePct = v; g_cfgDirty = 1; }
      }
      row_hint(hdWhy ? hdWhy : "The freeze ray's beam is unchanged.");
      {
        // Max is one knob stop past 1000, stored as 1100 (DNMP's WEAP_RELOAD_SLIDER_MAX): 20x, since the ammo
        // still comes from the animation.
        char val[16];
        const int rel = weap_reload_pct_clamped(g_cfg.reloadPct);
        weap_reload_text(rel, val, sizeof val);
        const int v = weap_reload_pct_clamped(row_slider_at(cursor, "Reload speed", rel, WEAP_FIRE_MIN,
                                                            WEAP_RELOAD_SLIDER_MAX, val, !hdWhy, 0, C_BONE,
                                                            EK_INT, WEAP_FIRE_MIN));
        if (v != g_cfg.reloadPct) { g_cfg.reloadPct = v; g_cfgDirty = 1; }
      }
      row_hint(hdWhy ? hdWhy : "Only the pistol, dual pistols, shotgun, Ripper and railgun reload.");
      break;
    }
    case TAB_VIEW: {
      // duke-dnmp's pair: the switch, then the slider as its sub-row, in the game's own FOV degrees.
      draw_group(cursor, "FIELD OF VIEW");
      const char* fovWhy = fov_refused();
      const bool fovOk = !fovWhy;
      g_cfg.fovOn = row_switch_avail(cursor, "Custom field of view", g_cfg.fovOn, C_GREEN, true, fovOk);
      row_hint(fovWhy ? fovWhy : "Sets the field of view through the game's own FOV modifier, never its settings. "
                                 "Off gives the game's value back.");
      MenuFoldBlock fovBlock;
      if (collapse_begin(&fovBlock, cursor, fovOk && g_cfg.fovOn != 0, 1)) {
        double stock = 0.0;
        const bool haveStock = fov_stock(&stock);
        const int shown = fov_slider_value(g_cfg.fovDeg, stock);
        char buf[24];
        fov_value_text(g_cfg.fovDeg, stock, haveStock ? 1 : 0, buf, sizeof buf);
        // def FOV_STOCK (0) sits outside [lo, hi]: row_slider_at admits it, as the frame cap's 0 does.
        const int r = row_slider_at(cursor, "Field of view", shown, FOV_MIN_DEG, FOV_MAX_DEG, buf,
                                    fovOk && g_cfg.fovOn != 0, SUB_INDENT, C_BONEDIM, EK_INT, FOV_STOCK);
        if (r != shown) g_cfg.fovDeg = fov_cfg_from_slider(r);
        row_hint("Degrees, as the game's own FOV option counts them; the reset arrow gives the game's value back "
                 "(shown with a decimal).");
      }
      collapse_end(fovBlock, cursor);

      // DNMP's group (its menu.cpp 2186-2245): the switch, its settings indented under it, every key row
      // top-level. The switch arms the keys and starts no flight; freecam.cpp ends a flight when it goes off.
      draw_group(cursor, "FREE CAMERA");
      {
        const char* fcWhy = freecam_refused();
        g_cfg.freecamOn = row_switch_avail(cursor, "Free camera", g_cfg.freecamOn, C_GREEN, true, !fcWhy);
        row_hint(fcWhy ? fcWhy : "Turns on the Camera, Teleport, Copy view, Paste view and Photo keys below.");
        const bool camOn = !fcWhy && g_cfg.freecamOn != 0;
        MenuFoldBlock camBlock;
        if (collapse_begin(&camBlock, cursor, camOn, 18)) {   // ten settings, two sliders, five key rows, the folder
          g_cfg.freecamSpeedPct = row_slider_sub(cursor, "Flight speed", g_cfg.freecamSpeedPct, FREECAM_SPEED_MIN_PCT,
                                                 FREECAM_SPEED_MAX_PCT, "%", camOn, FREECAM_SPEED_DEF_PCT);
          row_hint("100% is 8 m/s; Shift flies 4 times faster, Alt 4 times slower.");
          g_cfg.freecamPause = row_switch_sub(cursor, "Pause the game", g_cfg.freecamPause, C_GREEN, camOn);
          row_hint("Freezes the world while the camera flies; it runs again when the camera ends.");
          g_cfg.freecamMarker = row_switch_sub(cursor, "Show player", g_cfg.freecamMarker, C_GREEN, camOn);
          row_hint("A cyan marker on Duke while the camera flies.");
          g_cfg.freecamDebug = row_switch_sub(cursor, "Debug readout", g_cfg.freecamDebug, C_GREEN, camOn);
          row_hint("Shows the camera's position and angle while flying.");
          g_cfg.teleportMarker = row_switch_sub(cursor, "Teleport marker", g_cfg.teleportMarker, C_GREEN, camOn);
          row_hint("Shows where Duke would land; red where he cannot.");
          g_cfg.freecamShot = row_switch_sub(cursor, "Screenshot mode", g_cfg.freecamShot, C_GREEN, camOn);
          row_hint("Hides the game HUD and all Nuke Panel overlays while flying.");
          g_cfg.freecamNoFog = row_switch_sub(cursor, "No distance fog", g_cfg.freecamNoFog, C_GREEN, camOn);
          row_hint("Removes the distance fog while flying (Duke's, not the camera's) and the far plane it hides; glass, "
                   "water and particles keep their own. The game's fog comes back when the camera ends.");
          g_cfg.freecamNoCull = row_switch_sub(cursor, "No culling", g_cfg.freecamNoCull, C_GREEN, camOn);
          row_hint("Draws the whole level while flying, past the far plane, occlusion, anti-portals and zone walls. Costs "
                   "frame rate in big levels, and sounds tied to areas may change.");
          g_cfg.freecamBright = row_switch_sub(cursor, "Fullbright", g_cfg.freecamBright, C_GREEN, camOn);
          {
            const char* brWhy = freecam_bright_refused();
            row_hint(brWhy ? brWhy : freecam_bright_hint());
          }
          g_cfg.freecamLift = row_slider_sub(cursor, "Brightness lift", g_cfg.freecamLift, FBLIFT_MIN_PCT, FBLIFT_MAX_PCT,
                                             "%", camOn && g_cfg.freecamBright != 0, FBLIFT_DEF_PCT);
          {
            const char* lfWhy = fblift_refused();
            row_hint(lfWhy ? lfWhy : "Brightens the dark tones while Fullbright is on, for the surfaces it leaves "
                                     "shaded; photos include it. 0% turns it off.");
          }
          {
            // The pixel size takes the line under the slider (DNMP: the readout column holds nine cells).
            // The slider stops at the largest size the 256 MB strip budget allows on this screen (photo.h).
            int sw = 0, shh = 0;
            photo_screen_size(&sw, &shh);
            const int maxN = photo_mult_max(sw, shh);
            const int hi = maxN >= PHOTO_MULT_MIN ? maxN : PHOTO_MULT_MIN;
            const int stored = photo_mult_clamped(g_cfg.photoMult);
            const int mult = stored < hi ? stored : hi;
            char val[8], size[48], big[64];
            sprintf_s(val, "%dx", mult);
            const int v = row_slider_at(cursor, "Photo size", mult, PHOTO_MULT_MIN, hi, val, camOn && maxN != 0,
                                        SUB_INDENT, C_BONEDIM, EK_INT, PHOTO_MULT_DEF);
            if (v != mult) { g_cfg.photoMult = v; g_cfgDirty = 1; }
            const char* phWhy = photo_refused();
            if (!phWhy && stored > maxN) { photo_too_large_text(maxN, big, sizeof big); phWhy = big; }
            row_hint(phWhy ? phWhy : "The screen times this on each side, as a PNG in the Photo folder.");
            photo_size_text(mult, photo_kept(sw), photo_kept(shh), size, sizeof size);   // less the cropped margin
            if (sw > 0 && shh > 0) draw_placeholder(cursor, size);
          }
          row_bind(cursor, "Camera key", BIND_ROW_CAMERA);
          row_hint("Toggles the camera; W/A/S/D, Space, Ctrl, Shift and Alt fly it and cannot be bound here. The wheel "
                   "sets its field of view until the flight ends.");
          row_bind(cursor, "Teleport key", BIND_ROW_TELEPORT);
          row_hint("Moves Duke to the spot under the crosshair; scripted events on the way do not run. The camera's "
                   "movement keys cannot be bound here.");
          row_bind(cursor, "Copy view key", BIND_ROW_CAMCOPY);
          row_hint("Copies the camera's view to the clipboard. The movement keys cannot be bound here.");
          row_bind(cursor, "Paste view key", BIND_ROW_CAMPASTE);
          row_hint("Flies the camera to a copied view. The movement keys cannot be bound here.");
          row_bind(cursor, "Photo key", BIND_ROW_PHOTO);
          row_hint("Takes the photo while flying; again cancels. Files go to the Photo folder.");
          row_folder(cursor, "Photo folder");
          row_hint(photo_folder_can_pick() ? "Where photos are saved. Click to choose a folder."
                                           : "Where photos are saved. The folder picker needs a window: Borderless "
                                             "window or the game's windowed mode.");
        }
        collapse_end(camBlock, cursor);
      }

      // Every shake but the weapon kicks (No recoil's).
      draw_group(cursor, "CAMERA");
      {
        const char* nsWhy = noshake_refused();
        g_cfg.noShake = row_switch_avail(cursor, "No screen shake", g_cfg.noShake, C_GREEN, true, !nsWhy);
        row_hint(nsWhy ? nsWhy : "Removes explosion, damage, impact and vehicle shakes. The head bob stays. Weapon "
                                 "recoil stays: No recoil (Weapons) removes that.");
      }

      // One switch per effect: on shows it as the game has it, off forces its variable to 0.
      draw_group(cursor, "POST PROCESSING");
      for (int i = 0; i < PFX_COUNT; i++) {
        const PostFx& fx = postfx_info(i);
        const char* why = postfx_refused(i);
        g_cfg.*(fx.field) = row_switch_avail(cursor, fx.label, g_cfg.*(fx.field), C_GREEN, true, !why);
        row_hint(why ? why : fx.hint);
      }
      // Inverted from the rows above: on removes the overlay.
      {
        const char* dvWhy = dukevision_refused();
        g_cfg.cleanDukeVision = row_switch_avail(cursor, "Clean Duke Vision", g_cfg.cleanDukeVision, C_GREEN, true,
                                                 !dvWhy);
        row_hint(dvWhy ? dvWhy : "Removes Duke Vision's scanlines, distortion, tint and vignette; the brightening "
                                 "stays.");
      }
      break;
    }
    case TAB_GAME: {
      // Moved from Settings: the switches that change the game's own rules. The home of minigame
      // autoplay too.
      // The switch is not saved, off at every launch; the Speed value and the key are (duke-dnmp).
      draw_group(cursor, "GAME SPEED");
      {
        const char* gsWhy = gamespeed_refused();
        g_gameSpeedOn = row_switch_avail(cursor, "Game speed", g_gameSpeedOn, C_GREEN, true, !gsWhy);
        row_hint(gsWhy ? gsWhy : "Multiplies the game's own speed (Extras > Game Speed); slow motion below 100%.");
        MenuFoldBlock speedBlock;
        if (collapse_begin(&speedBlock, cursor, !gsWhy && g_gameSpeedOn != 0, 1)) {
          const int cur = gs_clamp_step(g_cfg.gameSpeedPct);
          const int v = gs_clamp_step(row_slider_sub(cursor, "Speed", cur, GS_PCT_MIN, GS_PCT_MAX, "%",
                                                     g_gameSpeedOn != 0, GS_PCT_DEF));
          if (v != g_cfg.gameSpeedPct) { g_cfg.gameSpeedPct = v; g_cfgDirty = 1; }
          row_hint("100% is the game's own speed; lower is slow motion. The frame rate is unaffected.");
        }
        collapse_end(speedBlock, cursor);
      }
      row_bind(cursor, "Game speed key", BIND_ROW_GAMESPEED);
      row_hint("Turns Game speed on and off.");

      // The game's own success paths, off by default.
      draw_group(cursor, "AUTO-WIN");
      {
        const char* qWhy = autowin_qte_refused();
        g_cfg.autoQte = row_switch_avail(cursor, "Auto-win QTEs", g_cfg.autoQte, C_GREEN, true, !qWhy);
        row_hint(qWhy ? qWhy : "Presses Jump for you through every button-mash QTE: boss finishers, lift doors, vents "
                               "and the workout bench. The game plays its own win; the Steam achievements it leads to "
                               "are real.");
        const char* mWhy = autowin_minigames_refused();
        g_cfg.autoMinigames = row_switch_avail(cursor, "Auto-win minigames", g_cfg.autoMinigames, C_GREEN, true,
                                               !mWhy);
        row_hint(mWhy ? mWhy : "Pool (on the first ball you hit with Use, once per level), pinball, slots (after a spin) "
                               "and air hockey win once per use with the game's own award, where the level allows ego awards (none while Extras cheats "
                               "(god mode, infinite ammo, InstaGib) are on this level). Air hockey can "
                               "unlock its Steam achievement; pinball can fire the table's score events and its own.");
        if (const char* note = autowin_minigames_note()) draw_placeholder(cursor, note);
        // Played with the table's own controls; while on, Auto-win minigames leaves pinball alone.
        const char* pWhy = pinbot_refused();
        g_cfg.pinballAuto = row_switch_avail(cursor, "Pinball autoplay", g_cfg.pinballAuto, C_GREEN, true, !pWhy);
        row_hint(pWhy ? pWhy : "Plays the pinball table with its own flippers and plunger while you use it. Earns "
                               "Balls of Steel at 1,000,000 points.");
        // Nuke Tank (DLC, The Doctor Who Cloned Me) played with the cabinet's own Fire, Left and Right.
        const char* aWhy = arcbot_refused();
        g_cfg.arcadeAuto = row_switch_avail(cursor, "Arcade autoplay", g_cfg.arcadeAuto, C_GREEN, true, !aWhy);
        row_hint(aWhy ? aWhy : "Plays Nuke Tank with the cabinet's own Fire, Left and Right while you use it: starts "
                               "the game, shoots every invader before it lands and dodges what it cannot. Stops at "
                               "a win. Your own keys take over.");
      }

      draw_group(cursor, "EXTRAS");
      g_cfg.bigPackage = row_switch(cursor, "Duke's Big Package", g_cfg.bigPackage, C_GREEN, true);
      row_hint("Unlocks the pre-order bonus (Big Heads, Ego Boost, T-shirts) every launch.");
      if (const char* bpNote = bp_state_note(bigpackage_state())) draw_placeholder(cursor, bpNote);
      break;
    }
    case TAB_SETTINGS: {
      draw_group(cursor, "HOTKEYS");
      row_bind(cursor, "Panel key", BIND_ROW_MENU);
      row_hint("The key that opens and closes this panel.");

      draw_group(cursor, "INPUT");
      {
        const char* rmWhy = rawmouse_refused();
        g_cfg.rawMouse = row_switch_avail(cursor, "Raw mouse input", g_cfg.rawMouse, C_GREEN, true, !rmWhy);
        row_hint(rmWhy ? rmWhy : "Turns the view by the mouse's own counts at the game's sensitivity: no speed cap, "
                                 "no low-frame-rate loss. Keyboard and gamepad turning stop while it is on.");
      }

      draw_group(cursor, "FRAME RATE");
      g_cfg.frameCapOn = row_switch(cursor, "Frame cap", g_cfg.frameCapOn, C_GREEN, true);
      row_hint("Paces the frame rate to the value below.");
      // A sub-row: it does nothing while Frame cap is off.
      MenuFoldBlock capBlock;
      if (collapse_begin(&capBlock, cursor, g_cfg.frameCapOn != 0, 1)) {
        // 0 stored means "follow the monitor": the row draws the effective fps and writes back only when the row
        // itself changed this frame (a drag, or the reset glyph's 0). A vsynced device cannot exceed the
        // refresh, so the slider's top and the shown value follow it.
        const FcCapInfo capInfo = framecap_cap_info(input_window());
        const bool following = g_cfg.frameCapFps < 30;
        const int effective = clamp_int(capInfo.effectiveFps, 30, capInfo.sliderHi);
        char buf[24];
        menu_cap_readout(buf, sizeof buf, effective, following ? 1 : 0);
        const int r = row_slider_at(cursor, "Cap", effective, 30, capInfo.sliderHi, buf, g_cfg.frameCapOn != 0,
                                    SUB_INDENT, C_BONEDIM, EK_INT, 0);
        if (r != effective) g_cfg.frameCapFps = r;
        row_hint("Frames per second at the cap; the reset arrow follows the monitor's refresh rate. "
                 "Vehicle jumps fail above about 150 fps.");
        if (capInfo.vsyncCapped) {
          char note[64];
          sprintf_s(note, "VSync is on: the game cannot exceed %d fps", capInfo.sliderHi);
          draw_placeholder(cursor, note);
        }
      }
      collapse_end(capBlock, cursor);

      // Spec 2026-10-08 (docs/re/steam-stats.md): a backup on its own, and a reset only on a second click within
      // AR_CONFIRM_MS and only once its backup is written. The game's Progress.ini is not touched.
      draw_group(cursor, "STEAM ACHIEVEMENTS");
      {
        const char* achWhy = achreset_refused();
        const bool busy = achreset_busy();
        if (row_button(cursor, "Back up achievements", busy ? "Writing..." : "Back up", !busy, false, achWhy != nullptr))
          achreset_backup_click();
        row_hint(achWhy ? achWhy : "Writes every achievement, unlocked with its unlock date and time, and locked, to "
                                   "System\\NukePanel\\achievements-backup-<date>.txt.");
        const bool armed = !achWhy && achreset_armed();
        if (row_button(cursor, armed ? "Click again to reset all DNF achievements (a backup is written first)"
                                     : "Reset achievements",
                       armed ? "Confirm" : "Reset", !busy, armed, achWhy != nullptr))
          achreset_reset_click();
        // An arm lives only while this row is fully on screen in the open panel (achreset_frame disarms otherwise).
        if (s_lastRowKey >= 0 && input_panel_open() && ui_rect_inside(s_lastRow, s_bodyRect)) achreset_row_shown();
        row_hint(achWhy ? achWhy : "Locks every Duke Nukem Forever Steam achievement on this account again. Click twice "
                                   "within 5 s; a backup is written first, and nothing is reset if it fails. "
                                   "Progress.ini (helmets, phones, counters) stays.");
        if (const char* note = achreset_note()) draw_placeholder(cursor, note);
      }

      // DNMP's DISPLAY row and hint; read at launch only (display.h), so the note says when a change waits.
      draw_group(cursor, "DISPLAY");
      g_cfg.borderless = row_switch(cursor, "Borderless window", g_cfg.borderless, C_GREEN, true);
      row_hint("Fills the monitor with no frame; the game stays windowed. Takes effect the next time the game starts.");
      if (const char* note = display_note()) draw_placeholder(cursor, note);
      {
        const char* krWhy = focuskeep_refused();
        g_cfg.keepRunning = row_switch_avail(cursor, "Keep running when alt-tabbed", g_cfg.keepRunning, C_GREEN, true,
                                             !krWhy);
        row_hint(krWhy ? krWhy : "The game keeps playing, with sound, while another window is in front; keys and the "
                                 "mouse still go to that window. Exclusive fullscreen cannot draw in the background.");
      }

      draw_group(cursor, "PANEL");
      // AA's row, 75-200% in steps of 5 on every screen; the readout is the settled scale, which this screen's
      // ceiling caps (ui_scale_ceiling). Drawing is not a write: only a drag stores anything.
      {
        char scaleText[16];
        sprintf_s(scaleText, "%d%%", settled);
        const int v = ui_scale_step(row_slider_at(cursor, "UI scale", settled, UI_SCALE_MIN, UI_SCALE_MAX,
                                                  scaleText, true, 0, C_BONE, EK_INT, UI_SCALE_DEFAULT));
        if (v != settled) g_cfg.uiScale = v;
        if (!g_ui.down && pct != settled) row_hint("This size could not be built; see nukepanel.log.");
      }
      g_cfg.animations = row_switch(cursor, "Panel animations", g_cfg.animations, C_GREEN, true);
      row_hint("Fades and slides in the panel");
      break;
    }
    default:
      break;
  }
  plate_close(cursor, true);

  canvas_pop_clip();
  g_ui.mx = bodyMx; g_ui.my = bodyMy;

  // The owning row did not draw this frame (tab change, dropped row): close.
  if (s_dd.open && !s_ddSeenThisFrame) { s_dd.open = 0; s_dd.rowKey = -1; }
  // Likewise the field, through its own commit path.
  if (s_entry.open && !s_entrySeenThisFrame) entry_clickaway_commit();

  // How tall this tab is, measured after the fact for the next frame's wheel.
  const int contentH = cursor.y - contentTop;
  s_contentH[s_tab] = contentH;
  s_scroll[s_tab] = menu_scroll_clamp(scroll, contentH, viewH);

  // The scroll cue, only with something to scroll; it reports and takes no click. At the offset the rows were
  // drawn at this frame.
  {
    const MenuScrollCue cue = menu_scroll_cue(viewH, scroll, contentH, viewH);
    if (cue.shown) {
      if (cue.fade > 0.0) {
        const int fadeY = bodyRect.y + bodyRect.h - MENU_SCROLL_FADE_H;
        canvas_gradient_rect(bodyRect.x, fadeY, bodyRect.w, MENU_SCROLL_FADE_H,
                             pres_content_colour(canvas_with_alpha(C_PANEL, 0)),
                             pres_content_colour(alpha_scale(C_PANEL, cue.fade)));
      }
      const int barX = p.x + p.w - 1 - 3 - MENU_SCROLL_CUE_W;
      canvas_rect(barX, bodyY + PAD + cue.thumbY, MENU_SCROLL_CUE_W, cue.thumbH, pres_content_colour(C_BONEFNT));
    }
  }

  if (s_sliderInvRows > s_sliderLoggedRows[s_tab]) {
    s_sliderLoggedRows[s_tab] = s_sliderInvRows;
    np_log("menu: sliders tab %d: %s | row w=%d at %d%%", s_tab, s_sliderInv, cursor.w, pct);
  }

  if (s_rowsDropped && !s_rowsDroppedLogged) {
    s_rowsDroppedLogged = true;
    np_log("menu: tab %d exhausted the %d px layout budget (laid out %d px) at panel %dx%d -- rows not drawn",
           s_tab, MENU_CONTENT_BUDGET, contentH, box.w, box.h);
  }

  if (railTab >= 0) menu_set_tab(railTab);

  // --- The resize grip: three diagonals in the corner, lit while hovered ------
  {
    const UiRect g = panel_grip_rect(box);
    const unsigned gc = pres_colour(gripHit ? C_BONEDIM : C_BONEFNT);
    for (int i = 1; i <= 3; i++) {
      const int off = i * 4;
      canvas_line(g.x + off, g.y + g.h - 1, g.x + g.w - 1, g.y + off, gc);
    }
  }

  menu_dropdown_draw();

  // --- The tooltip, after everything else the panel draws --------------------
  s_tooltip = tooltip_step(s_tooltip, s_hoverRowKey, g_ui.pressed || g_ui.rpressed,
                           s_pres.lastMs, anim_style(), s_frameDt);
  const char* const hint = s_glyphHint ? s_glyphHint : s_hoverHint;
  s_tooltipText = nullptr;
  if (s_tooltip.shown && s_tooltip.alpha > 0.0 && hint && s_pres.openTarget) {
    int st[TIP_LINES_MAX], ln[TIP_LINES_MAX], widest = 0;
    const int lines = tip_wrap(hint, tip_max_w(p.w, logicalW), tip_measure, nullptr, st, ln, TIP_LINES_MAX, &widest);
    const int pitch = font_height[FONT_SMALL] + TIP_LINE_GAP;
    const UiRect b = tooltip_box_place(g_ui.mx, g_ui.my, widest,
                                       lines > 0 ? lines * pitch - TIP_LINE_GAP : 0, logicalW, logicalH);
    canvas_round_rect(b.x, b.y, b.w, b.h, TOOLTIP_R,
                      alpha_scale(C_WELL, s_tooltip.alpha));
    canvas_round_rect_outline(b.x, b.y, b.w, b.h, TOOLTIP_R, 1,
                              alpha_scale(C_RAISED, s_tooltip.alpha));
    for (int i = 0; i < lines; ++i) {
      char line[sizeof s_hintBuf];
      _snprintf_s(line, sizeof line, _TRUNCATE, "%.*s", ln[i], hint + st[i]);
      canvas_text(b.x + TOOLTIP_PAD, b.y + TOOLTIP_PAD + i * pitch, line,
                  alpha_scale(C_BONE, s_tooltip.alpha), FONT_SMALL);
    }
    s_tooltipText = hint;
  }

  // --- A press no control took moves the panel ------------------------------
  // Decided last: "no control took this press" is only known once all have looked.
  if (g_ui.pressed && g_ui.down && !s_pressClaimed && !s_dragLive &&
      s_pres.openTarget && ui_hit(p, g_ui.mx, g_ui.my)) {
    s_drag.phase = PDRAG_MOVE;
    s_drag.start = box;
    s_drag.grabDx = g_ui.mx - box.x;
    s_drag.grabDy = g_ui.my - box.y;
    s_dragBox = box;
    s_dragLive = true;
  }

  g_ui.mx = deviceMx;
  g_ui.my = deviceMy;
  canvas_set_scale(UI_SCALE_DEFAULT);
}

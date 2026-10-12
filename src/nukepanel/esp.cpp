// Enemy ESP: corner brackets, health bar, name and health, distance, over every hostile pawn whose box is on screen and
// within the draw distance (friendly or neutral ones with Allies on, in the friendly colour), fed from world.h's pawn
// list and view. No off-screen arrows.
#include "esp.h"
#include "esp_layout.h"
#include "espnames.h"
#include "world.h"
#include "egoesp.h"
#include "items.h"
#include "breakables.h"
#include "collect.h"
#include "achhelp.h"
#include "aircraft.h"
#include "modelbox.h"
#include "spawnbox.h"
#include "aim.h"
#include "uname.h"
#include "canvas.h"
#include "font.h"
#include "config.h"
#include "log.h"
#include "mswindow.h"
#include "mpgate.h"
#include <windows.h>
#include <stdio.h>
#include <math.h>

static EkMarker s_markers[EK_MARKER_COLLECT_MAX];

// Counts over the current 600 frames; every pawn entry lands in exactly one of skipped, far, offscreen, capped or
// drawn.
static unsigned s_frames, s_entries, s_skipped, s_far, s_offscreen, s_capped, s_drawn, s_enemies, s_allies;
static unsigned s_calls, s_ego, s_egoFar, s_egoOff, s_items, s_itemsFar, s_itemsOff, s_bk, s_bkFar,
    s_bkOff, s_cl, s_clFar, s_clOff, s_ac, s_acFar, s_acOff, s_ah, s_ahFar, s_ahOff;
// The ego category's colour: the Progress tab's teal, which the Level stats line also uses.
static const unsigned C_EGO = C_PROGRESS;
static MsWindow s_cost;   // esp_draw's milliseconds over the 600-frame window
static LARGE_INTEGER s_qpf;
static int s_unitLogged;

// Eight rects, two per corner: brackets rather than an outline so markers do not merge into a grid in a crowd.
static void draw_brackets(int x, int y, int w, int h, unsigned colour) {
  const int cx = esp_bracket_arm(w);
  const int cy = esp_bracket_arm(h);
  const int t  = esp_bracket_thickness(cx, cy);
  if (t <= 0) return;
  const int x1 = x + w, y1 = y + h;
  canvas_rect(x,       y,       cx, t,  colour);
  canvas_rect(x,       y,       t,  cy, colour);
  canvas_rect(x1 - cx, y,       cx, t,  colour);
  canvas_rect(x1 - t,  y,       t,  cy, colour);
  canvas_rect(x,       y1 - t,  cx, t,  colour);
  canvas_rect(x,       y1 - cy, t,  cy, colour);
  canvas_rect(x1 - cx, y1 - t,  cx, t,  colour);
  canvas_rect(x1 - t,  y1 - cy, t,  cy, colour);
}

// The display label per class name index, so the engine's name lock is taken once per class, not per enemy per
// frame. Direct-mapped; names are append-only so an entry never goes stale. An empty (deferred) name is not cached.
struct LabelSlot { int idx; char s[48]; };
static LabelSlot s_labels[64];
static const char* label_for(int nameIdx) {
  LabelSlot& l = s_labels[(unsigned)nameIdx % 64];
  if (l.idx == nameIdx && nameIdx != 0 && l.s[0]) return l.s;
  char cls[64];
  esp_wide_to_ascii(uname_str(nameIdx), cls, sizeof cls);
  esp_display_name(cls, l.s, sizeof l.s);
  l.idx = l.s[0] ? nameIdx : 0;
  return l.s;
}

static void draw_marker(const EkMarker& m, const WPawn& e, const WView& v) {
  const int bx = m.box.x, by = m.box.y, bw = m.box.w, bh = m.box.h;
  const unsigned colour = m.kind == EK_MARKER_ALLY ? C_FRIENDLY : C_RED;
  // Bounding boxes off hides the brackets only, keeping the labels and the health bar.
  if (g_cfg.espBoundingBoxes) draw_brackets(bx, by, bw, bh, colour);

  const int hp = e.health, maxhp = e.healthCap;
  if (g_cfg.espHealthBars && hp > 0 && maxhp > 0) {
    const int barX = esp_bar_x(bx);
    const int fill = canvas_bar_fill(bh, hp, maxhp);   // never past bh, even above the cap
    canvas_rect(barX, by, ESP_BAR_W, bh, C_WELL);
    if (fill > 0) canvas_rect(barX, by + (bh - fill), ESP_BAR_W, fill, canvas_health_colour(hp, maxhp));
  }

  // "Name and health": the display name in FONT_SMALL and cur/max in FONT_MONO, centred as one pair.
  if (g_cfg.espNames) {
    char num[ESP_HEALTH_NUM_BUFSIZE];
    const char* name = label_for(e.nameIdx);
    // A downed enemy (Health 0, kneeling in the finish-me stun) is tagged: it can be executed.
    char tagged[96];   // truncated, never aborted: sprintf_s ends the process on overflow
    if (e.downed && name[0]) { _snprintf_s(tagged, sizeof tagged, _TRUNCATE, "%s - EXECUTE", name); name = tagged; }
    else if (e.downed) name = "EXECUTE";
    const bool haveNum = esp_health_number(num, hp, maxhp);
    if (name[0] || haveNum) {
      const int nameW = name[0] ? canvas_text_width(name, FONT_SMALL) : 0;
      const int numW = haveNum ? canvas_text_width(num, FONT_MONO) : 0;
      const EspLabelRuns runs = esp_label_runs(bx, bw, nameW, numW);
      const int nameY = esp_name_label_y(by);
      if (name[0]) esp_draw_label(runs.nameX, nameY, name, C_BONE, FONT_SMALL);
      if (haveNum)
        esp_draw_label(runs.numX, esp_label_mono_y(nameY, font_height[FONT_SMALL], font_height[FONT_MONO]), num,
                       C_BONE, FONT_MONO);
    }
  }

  if (g_cfg.espDistance) {
    // Player-relative, not box-relative: the view sits at the player's eye, so this is the gameplay distance.
    const float dm = esp_dist_m(e.x - v.x, e.y - v.y);
    char buf[24];
    sprintf_s(buf, "%d m", clamp_double_to_int((double)dm + 0.5));
    esp_draw_label(esp_label_x(bx, bw, canvas_text_width(buf, FONT_SMALL)), esp_dist_label_y(by, bh), buf, C_BONEDIM,
                   FONT_SMALL);
  }
}

static void esp_draw(int W, int H) {
  const WPawn* pawns;
  const int n = world_pawns(&pawns);
  const WView v = world_view();
  if (!v.ok || n <= 0) return;
  s_frames++;
  s_entries += (unsigned)n;
  int count = 0, seq = 0;
  for (int i = 0; i < n && count < EK_MARKER_COLLECT_MAX; ++i) {
    const WPawn& e = pawns[i];
    // Neutrals (civilians, no relationship) share the Allies row: never drawn as enemies (world.h). An enemy kneeling
    // in the finish-me stun reads Health 0 but is still an enemy to see (downed, world.h).
    const int kind = e.enemy || e.downed ? EK_MARKER_ENEMY
                                         : ((e.ally || e.neutral) && g_cfg.espAllies ? EK_MARKER_ALLY : -1);
    if (kind < 0) { s_skipped++; continue; }
    // The same whole metres the distance label shows, so a "256 m" label is never culled at a 256 m range.
    const int metres = clamp_double_to_int((double)esp_dist_m(e.x - v.x, e.y - v.y) + 0.5);
    if (!ekm_within_draw_distance(metres, g_cfg.espDrawDist, g_cfg.espNoLimit)) { s_far++; continue; }
    int ok = 0;
    // A pawn hit through its mounted collision is boxed round it, a drop-in animated away from its Location at its
    // rendered body (spawnbox_body); else the cylinder.
    float bc[3], bh[3];
    const int body = spawnbox_body(e.obj, bc, bh);
    const EkScreenBox box = body ? esp_pawn_box(v, bc[0], bc[1], bc[2], bh[0] > bh[1] ? bh[0] : bh[1], bh[2], W, H, &ok)
                                 : esp_pawn_box(v, e.x, e.y, e.z, e.radius, e.height, W, H, &ok);
    if (!ok) { s_offscreen++; continue; }
    // Selection and painter order by horizontal distance from the camera, in UU.
    const double dx = (double)e.x - v.x, dy = (double)e.y - v.y;
    EkMarker& m = s_markers[count++];
    m.kind = kind; m.index = i; m.seq = seq++;
    m.dist2d = clamp_double_to_int(sqrt(dx * dx + dy * dy));
    m.p.x = box.x + box.w / 2; m.p.y = box.y + box.h / 2; m.p.dist2d = m.dist2d;
    m.box = box;
    if (kind == EK_MARKER_ALLY) s_allies++; else s_enemies++;
  }
  if (count == 0) return;
  // Select, clamp, order, in that order: a clamp first would keep the first collected, not the nearest.
  ekm_keep_nearest(s_markers, count, EK_MARKER_MAX);
  if (count > EK_MARKER_MAX) { s_capped += (unsigned)(count - EK_MARKER_MAX); count = EK_MARKER_MAX; }
  ekm_sort_far_to_near(s_markers, count);
  if (!s_unitLogged) {
    s_unitLogged = 1;
    np_log("esp: distance unit %.1f UU per metre (UE1 convention, assumed; not yet measured in DNF)",
           ESP_UU_PER_METRE);
  }
  canvas_open(W, H);
  for (int i = 0; i < count; ++i) draw_marker(s_markers[i], pawns[s_markers[i].index], v);
  s_drawn += (unsigned)count;
}

// Brackets round the giver's model (else its clamped collision box), the award's name above and the distance
// below; the enemy ESP's draw distance applies, and world_frame's gate (no level view while paused) hides them all.
static void ego_draw(int W, int H) {
  const EgMarker* m;
  const int n = egoesp_markers(&m);
  const WView v = world_view();
  if (!v.ok || n <= 0) return;
  bool open = false;
  for (int i = 0; i < n; ++i) {
    const int metres = clamp_double_to_int((double)esp_dist_m(m[i].x - v.x, m[i].y - v.y) + 0.5);
    if (!ekm_within_draw_distance(metres, g_cfg.espDrawDist, g_cfg.espNoLimit)) { s_egoFar++; continue; }
    int ok = 0;
    const EkScreenBox b = mb_marker_box(v, m[i].box, m[i].x, m[i].y, m[i].z, eg_clamp_extent(m[i].radius),
                                        eg_clamp_extent(m[i].height), W, H, &ok);
    if (!ok) { s_egoOff++; continue; }
    if (!open) { canvas_open(W, H); open = true; }
    draw_brackets(b.x, b.y, b.w, b.h, C_EGO);
    char label[40];
    eg_label_in(m[i].dlc, m[i].id, label, sizeof label);
    esp_draw_label(esp_label_x(b.x, b.w, canvas_text_width(label, FONT_SMALL)), esp_name_label_y(b.y), label, C_EGO,
                   FONT_SMALL);
    char d[24];
    sprintf_s(d, "%d m", metres);
    esp_draw_label(esp_label_x(b.x, b.w, canvas_text_width(d, FONT_SMALL)), esp_dist_label_y(b.y, b.h), d, C_BONEDIM,
                   FONT_SMALL);
    s_ego++;
  }
}

// Items, Ammo crates, Quest items: one colour per row, indexed by IT_ROW_*.
static const unsigned kItemColour[IT_ROWS] = { C_BLUE, C_GREEN, C_SETTINGS };
static bool items_on(void) { return g_cfg.espItems || g_cfg.espAmmoCrates || g_cfg.espQuest; }

// As ego_draw: brackets round the model (else the clamped collision box), the item's name above, the distance below.
static void items_draw(int W, int H) {
  const ItMarker* m;
  const int n = items_markers(&m);
  const WView v = world_view();
  if (!v.ok || n <= 0) return;
  bool open = false;
  for (int i = 0; i < n; ++i) {
    const int metres = clamp_double_to_int((double)esp_dist_m(m[i].x - v.x, m[i].y - v.y) + 0.5);
    if (!ekm_within_draw_distance(metres, g_cfg.espDrawDist, g_cfg.espNoLimit)) { s_itemsFar++; continue; }
    int ok = 0;
    const EkScreenBox b = mb_marker_box(v, m[i].box, m[i].x, m[i].y, m[i].z, eg_clamp_extent(m[i].radius),
                                        eg_clamp_extent(m[i].height), W, H, &ok);
    if (!ok) { s_itemsOff++; continue; }
    if (!open) { canvas_open(W, H); open = true; }
    const unsigned colour = m[i].row >= 0 && m[i].row < IT_ROWS ? kItemColour[m[i].row] : C_BLUE;
    draw_brackets(b.x, b.y, b.w, b.h, colour);
    esp_draw_label(esp_label_x(b.x, b.w, canvas_text_width(m[i].label, FONT_SMALL)), esp_name_label_y(b.y),
                   m[i].label, colour, FONT_SMALL);
    char d[24];
    sprintf_s(d, "%d m", metres);
    esp_draw_label(esp_label_x(b.x, b.w, canvas_text_width(d, FONT_SMALL)), esp_dist_label_y(b.y, b.h), d, C_BONEDIM,
                   FONT_SMALL);
    s_items++;
  }
}

// As items_draw: C_HAZARD, a stash opener (not a route breakable) in C_BONE.
static void breakables_draw(int W, int H) {
  const BkMarker* m;
  const int n = breakables_markers(&m);
  const WView v = world_view();
  if (!v.ok || n <= 0) return;
  bool open = false;
  for (int i = 0; i < n; ++i) {
    const int metres = clamp_double_to_int((double)esp_dist_m(m[i].x - v.x, m[i].y - v.y) + 0.5);
    if (!ekm_within_draw_distance(metres, g_cfg.espDrawDist, g_cfg.espNoLimit)) { s_bkFar++; continue; }
    int ok = 0;
    const EkScreenBox b = mb_marker_box(v, m[i].box, m[i].x, m[i].y, m[i].z, eg_clamp_extent(m[i].radius),
                                        eg_clamp_extent(m[i].height), W, H, &ok);
    if (!ok) { s_bkOff++; continue; }
    if (!open) { canvas_open(W, H); open = true; }
    const unsigned colour = m[i].stash >= 0 ? C_BONE : C_HAZARD;
    draw_brackets(b.x, b.y, b.w, b.h, colour);
    esp_draw_label(esp_label_x(b.x, b.w, canvas_text_width(m[i].label, FONT_SMALL)), esp_name_label_y(b.y),
                   m[i].label, colour, FONT_SMALL);
    char d[24];
    sprintf_s(d, "%d m", metres);
    esp_draw_label(esp_label_x(b.x, b.w, canvas_text_width(d, FONT_SMALL)), esp_dist_label_y(b.y, b.h), d, C_BONEDIM,
                   FONT_SMALL);
    s_bk++;
  }
}

// As breakables_draw: helmets in C_HELMET, phones in C_PHONE.
static void collect_draw(int W, int H) {
  const ClMarker* m;
  const int n = collect_markers(&m);
  const WView v = world_view();
  if (!v.ok || n <= 0) return;
  bool open = false;
  for (int i = 0; i < n; ++i) {
    const int metres = clamp_double_to_int((double)esp_dist_m(m[i].x - v.x, m[i].y - v.y) + 0.5);
    if (!ekm_within_draw_distance(metres, g_cfg.espDrawDist, g_cfg.espNoLimit)) { s_clFar++; continue; }
    int ok = 0;
    const EkScreenBox b = mb_marker_box(v, m[i].box, m[i].x, m[i].y, m[i].z, eg_clamp_extent(m[i].radius),
                                        eg_clamp_extent(m[i].height), W, H, &ok);
    if (!ok) { s_clOff++; continue; }
    if (!open) { canvas_open(W, H); open = true; }
    const unsigned colour = m[i].kind == CL_MARK_HELMET ? C_HELMET : C_PHONE;
    draw_brackets(b.x, b.y, b.w, b.h, colour);
    esp_draw_label(esp_label_x(b.x, b.w, canvas_text_width(m[i].label, FONT_SMALL)), esp_name_label_y(b.y),
                   m[i].label, colour, FONT_SMALL);
    char d[24];
    sprintf_s(d, "%d m", metres);
    esp_draw_label(esp_label_x(b.x, b.w, canvas_text_width(d, FONT_SMALL)), esp_dist_label_y(b.y, b.h), d, C_BONEDIM,
                   FONT_SMALL);
    s_cl++;
  }
}

// As collect_draw, in gold, labelled with the achievement, the target and its progress.
static void ach_draw(int W, int H) {
  const AhMarker* m;
  const int n = achhelp_markers(&m);
  const WView v = world_view();
  if (!v.ok || n <= 0) return;
  bool open = false;
  for (int i = 0; i < n; ++i) {
    const int metres = clamp_double_to_int((double)esp_dist_m(m[i].x - v.x, m[i].y - v.y) + 0.5);
    if (!ekm_within_draw_distance(metres, g_cfg.espDrawDist, g_cfg.espNoLimit)) { s_ahFar++; continue; }
    int ok = 0;
    const EkScreenBox b = mb_marker_box(v, m[i].box, m[i].x, m[i].y, m[i].z, eg_clamp_extent(m[i].radius),
                                        eg_clamp_extent(m[i].height), W, H, &ok);
    if (!ok) { s_ahOff++; continue; }
    if (!open) { canvas_open(W, H); open = true; }
    draw_brackets(b.x, b.y, b.w, b.h, C_ACHIEVE);
    esp_draw_label(esp_label_x(b.x, b.w, canvas_text_width(m[i].label, FONT_SMALL)), esp_name_label_y(b.y),
                   m[i].label, C_ACHIEVE, FONT_SMALL);
    char d[24];
    sprintf_s(d, "%d m", metres);
    esp_draw_label(esp_label_x(b.x, b.w, canvas_text_width(d, FONT_SMALL)), esp_dist_label_y(b.y, b.h), d, C_BONEDIM,
                   FONT_SMALL);
    s_ah++;
  }
}

// As items_draw, in the enemy colour: the render box (GetRenderBoundingBox, a world box) as spawnbox's bodies are
// drawn, the health bar left of it from Health over the cap, the class's display name above.
static void aircraft_draw(int W, int H) {
  const AcShip* s;
  const int n = aircraft_ships(&s);
  const WView v = world_view();
  int drawn = 0;
  if (!v.ok || n <= 0) {
    aircraft_drawn(0);
    return;
  }
  bool open = false;
  for (int i = 0; i < n; ++i) {
    const int metres = clamp_double_to_int((double)esp_dist_m(s[i].c[0] - v.x, s[i].c[1] - v.y) + 0.5);
    if (!ekm_within_draw_distance(metres, g_cfg.espDrawDist, g_cfg.espNoLimit)) { s_acFar++; continue; }
    int ok = 0;
    const float r = s[i].h[0] > s[i].h[1] ? s[i].h[0] : s[i].h[1];
    const EkScreenBox b = esp_pawn_box(v, s[i].c[0], s[i].c[1], s[i].c[2], r, s[i].h[2], W, H, &ok);
    if (!ok) { s_acOff++; continue; }
    if (!open) { canvas_open(W, H); open = true; }
    draw_brackets(b.x, b.y, b.w, b.h, C_RED);
    const int hp = clamp_double_to_int((double)s[i].health + 0.5), cap = clamp_double_to_int((double)s[i].cap + 0.5);
    if (hp > 0 && cap > 0) {
      const int barX = esp_bar_x(b.x);
      const int fill = canvas_bar_fill(b.h, hp, cap);
      canvas_rect(barX, b.y, ESP_BAR_W, b.h, C_WELL);
      if (fill > 0) canvas_rect(barX, b.y + (b.h - fill), ESP_BAR_W, fill, canvas_health_colour(hp, cap));
    }
    const char* label = ac_label(s[i].kind);
    esp_draw_label(esp_label_x(b.x, b.w, canvas_text_width(label, FONT_SMALL)), esp_name_label_y(b.y), label, C_RED,
                   FONT_SMALL);
    char d[24];
    sprintf_s(d, "%d m", metres);
    esp_draw_label(esp_label_x(b.x, b.w, canvas_text_width(d, FONT_SMALL)), esp_dist_label_y(b.y, b.h), d, C_BONEDIM,
                   FONT_SMALL);
    drawn++;
  }
  s_ac += (unsigned)drawn;
  aircraft_drawn(drawn);
}

// duke-dnmp's calibration marker (itself AA's): hollow at the raw aim point, filled and labelled at the led one.
static void aim_marker_draw(int W, int H) {
  float raw[3], led[3];
  const WView v = world_view();
  if (!v.ok || !aim_marker_points(raw, led)) return;
  float rx, ry, lx, ly;
  const int rOk = view_w2s(v, raw[0], raw[1], raw[2], W, H, &rx, &ry);
  const int lOk = view_w2s(v, led[0], led[1], led[2], W, H, &lx, &ly);
  if (!rOk && !lOk) return;
  const int half = 4, t = 1, span = 2 * half + 1;
  canvas_open(W, H);
  if (rOk) {
    const int cx = clamp_double_to_int((double)rx + 0.5), cy = clamp_double_to_int((double)ry + 0.5);
    const int bx = cx - half, by = cy - half;
    canvas_rect(bx, by, span, t, C_AIM);
    canvas_rect(bx, cy + half + 1 - t, span, t, C_AIM);
    canvas_rect(bx, by, t, span, C_AIM);
    canvas_rect(cx + half + 1 - t, by, t, span, C_AIM);
  }
  if (lOk) {
    const int cx = clamp_double_to_int((double)lx + 0.5), cy = clamp_double_to_int((double)ly + 0.5);
    canvas_rect(cx - half, cy - half, span, span, C_AIM);
    esp_draw_label(esp_label_x(cx, 0, canvas_text_width("Aim", FONT_SMALL)), cy - half - ESP_LABEL_GAP, "Aim", C_AIM,
                   FONT_SMALL);
  }
}

void esp_frame(int W, int H) {
  // No marker of any kind outside standalone play (mpgate.h).
  if (!mp_refused() && (g_cfg.espOn || g_cfg.espEgo || items_on() || g_cfg.espBreakables || g_cfg.espHelmets ||
                        g_cfg.espPhones || achhelp_any_on())) {
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    if (g_cfg.espOn) esp_draw(W, H);
    if (g_cfg.espEgo) ego_draw(W, H);
    if (items_on()) items_draw(W, H);
    if (g_cfg.espBreakables) breakables_draw(W, H);
    if (g_cfg.espHelmets || g_cfg.espPhones) collect_draw(W, H);
    if (g_cfg.espOn) aircraft_draw(W, H);   // ships are enemies: drawn with the Enemies row
    ach_draw(W, H);                         // achhelp_markers gates on the rows' own switches
    QueryPerformanceCounter(&t1);
    if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
    ms_window_add(&s_cost, (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)s_qpf.QuadPart);
  }
  if (aim_marker_shown(g_cfg.silentOn, g_cfg.aimbotOn, g_cfg.aimMarker)) aim_marker_draw(W, H);   // over the boxes
  if (++s_calls % 600 != 0) return;   // like the other instruments: every 600 frames
  if (s_frames)
    np_log("esp: frames=%u entries=%u enemies=%u allies=%u drawn=%u capped=%u offscreen=%u far=%u skipped=%u "
           "range=%s esp=%.3f/%.3fms", s_frames, s_entries, s_enemies, s_allies, s_drawn, s_capped, s_offscreen,
           s_far, s_skipped, g_cfg.espNoLimit ? "unlimited" : "set", ms_window_avg(s_cost), s_cost.max);
  if (s_ego || s_egoFar || s_egoOff) np_log("esp: ego boosts drawn=%u far=%u offscreen=%u", s_ego, s_egoFar, s_egoOff);
  if (s_items || s_itemsFar || s_itemsOff)
    np_log("esp: items drawn=%u far=%u offscreen=%u", s_items, s_itemsFar, s_itemsOff);
  if (s_bk || s_bkFar || s_bkOff) np_log("esp: breakables drawn=%u far=%u offscreen=%u", s_bk, s_bkFar, s_bkOff);
  if (s_cl || s_clFar || s_clOff) np_log("esp: collectibles drawn=%u far=%u offscreen=%u", s_cl, s_clFar, s_clOff);
  if (s_ac || s_acFar || s_acOff) np_log("esp: aircraft drawn=%u far=%u offscreen=%u", s_ac, s_acFar, s_acOff);
  if (s_ah || s_ahFar || s_ahOff) np_log("esp: achievement targets drawn=%u far=%u offscreen=%u", s_ah, s_ahFar, s_ahOff);
  modelbox_log();
  ms_window_reset(&s_cost);
  s_frames = s_entries = s_skipped = s_far = s_offscreen = s_capped = s_drawn = s_enemies = s_allies = 0;
  s_ego = s_egoFar = s_egoOff = 0;
  s_items = s_itemsFar = s_itemsOff = 0;
  s_bk = s_bkFar = s_bkOff = 0;
  s_cl = s_clFar = s_clOff = 0;
  s_ac = s_acFar = s_acOff = 0;
  s_ah = s_ahFar = s_ahOff = 0;
}

// Damage numbers: this frame's hostile pawns go through the health-delta tracker (dmgtrack.h), each event enters the
// ring at the pawn's last position, and the live ones draw rising and fading (dmgnum.h owns the rules). No damage hook:
// Engine.dll code is CEG-protected. Game/Present thread only, so nothing here is shared.
#include "dmgnum.h"
#include "esp_layout.h"   // esp_draw_label
#include <windows.h>
#include "world.h"
#include "spawnbox.h"
#include "canvas.h"
#include "font.h"
#include "config.h"
#include "log.h"
#include "ui/wallclock.h"
#include "ui/common.h"
#include "mpgate.h"
#include <stdio.h>

enum { kSeenMax = 512 };   // world.cpp's pawn cap

static DmgTrack s_track;
static DmgRing s_ring;
static DmgSeen s_seen[kSeenMax];
static int s_active;
static unsigned s_hits, s_kills, s_bulk, s_drawn, s_far, s_incomplete;
static int s_pathLogged = -1;
static ULONGLONG s_nextLog;

static void dmg_log(void) {
  const ULONGLONG tick = GetTickCount64();
  if (tick < s_nextLog) return;
  s_nextLog = tick + 5000;
  static unsigned lastHits = 0xFFFFFFFFu, lastDrawn = 0xFFFFFFFFu;
  if (s_hits == lastHits && s_drawn == lastDrawn) return;   // cumulative: an unchanged line says nothing
  lastHits = s_hits;
  lastDrawn = s_drawn;
  np_log("dmgnum: tracked=%d hits=%u kills=%u bulkDropped=%u incompleteFrames=%u drawn=%u far=%u", s_track.n, s_hits,
         s_kills, s_bulk, s_incomplete, s_drawn, s_far);
}

void dmgnum_frame(int W, int H) {
  dmg_log();
  const WView v = world_view();
  const WPawn* pawns;
  const int n = world_pawns(&pawns);
  const int complete = world_pawns_complete();
  // Off or no level: forget everything, so turning it on or entering a level starts clean (a first sight is no event).
  if (!g_cfg.dmgNumbers || !v.ok || mp_refused()) {   // refused in multiplayer (mpgate.h)
    if (s_active) { s_track = dmgtrack_init(); dmg_reset(&s_ring); s_active = 0; }
    return;
  }
  s_active = 1;
  const unsigned now = wallclock_ms();
  int ns = 0;
  for (int i = 0; i < n && ns < kSeenMax; ++i) {
    const WPawn& p = pawns[i];
    // A dead pawn leaves this list (the tracker reads it as a kill) unless knocked down; one alive but no longer
    // hostile (a scripted target moved off the player) leaves the tracker quietly.
    const int mode = p.enemy ? DMGSEEN_ENEMY : p.knockdown ? DMGSEEN_KNOCKDOWN : p.alive ? DMGSEEN_LEFT : -1;
    if (mode < 0) continue;
    DmgSeen& s = s_seen[ns++];
    // A knockdown's damage is what it had left: Health may overshoot below 0.
    s.who = p.obj; s.health = mode == DMGSEEN_KNOCKDOWN && p.health < 0 ? 0 : p.health;
    s.x = p.x; s.y = p.y; s.z = p.z; s.mode = mode;
    // A drop-in animated away from its Location (spawnbox.h): the number rises from its rendered body.
    float bc[3], bh[3];
    if (spawnbox_body(p.obj, bc, bh)) { s.x = bc[0]; s.y = bc[1]; s.z = bc[2]; }
  }
  DmgDelta d[DMG_MAX];
  const int nd = dmgtrack_step_if(&s_track, complete, s_seen, ns, d, DMG_MAX);
  if (!complete) s_incomplete++;
  int kills = 0;
  for (int i = 0; i < nd; ++i) kills += d[i].kill;
  if (dmg_is_bulk_unload(kills)) {
    s_bulk += (unsigned)nd;
    np_log("dmgnum: %d pawns left the list in one frame; dropped, not shown (level change?)", kills);
  } else {
    for (int i = 0; i < nd; ++i) {
      DmgEvent ev;
      ev.at = d[i].at; ev.damage = d[i].damage; ev.kill = d[i].kill; ev.victim = d[i].who; ev.t0 = now; ev.live = 1;
      dmg_push(&s_ring, ev);
      s_hits++;
      s_kills += (unsigned)d[i].kill;
    }
  }

  int live = 0;
  for (int k = 0; k < DMG_MAX; ++k) live += dmg_is_live(s_ring.e[k], now);
  if (!live) return;
  const int textH = font_height[FONT_UI];
  int path = -1;   // decided at the first number drawn, once the canvas frame is open
  for (int k = 0; k < DMG_MAX; ++k) {
    const DmgEvent& e = s_ring.e[k];
    if (!dmg_is_live(e, now)) continue;
    float sx, sy;
    // Each number is projected at its hit-time anchor, never the victim's current place; on screen only.
    if (!view_w2s(v, e.at.x, e.at.y, e.at.z, W, H, &sx, &sy) || sx < 0.f || sx > (float)W || sy < 0.f ||
        sy > (float)H)
      continue;
    // The ESP's range applies to the numbers too (AA's dmgnum.cpp), by the hit point's horizontal distance.
    const int metres = clamp_double_to_int((double)esp_dist_m(e.at.x - v.x, e.at.y - v.y) + 0.5);
    if (!ekm_within_draw_distance(metres, g_cfg.espDrawDist, g_cfg.espNoLimit)) { s_far++; continue; }
    char s[16];
    sprintf_s(s, "%d", e.damage);
    const double prog = dmg_progress(e.t0, now);
    const int rise = dmg_rise_px(prog, textH) + dmg_stack_index(s_ring, k, now) * textH;
    const unsigned col = canvas_with_alpha(dmg_colour(e.kill), dmg_alpha(prog));
    canvas_open(W, H);
    if (path < 0) {
      const int ui = canvas_scale();
      path = dmg_text_path(ui, canvas_push_text_scale(dmg_atlas_pct(ui)));
      if (path != DMG_TEXT_ATLAS) canvas_pop_text_scale();
      if (path != s_pathLogged) {   // the first draw, and any change (a failed build, a lost device)
        s_pathLogged = path;
        const int px = font_height[FONT_UI] * dmg_drawn_pct(ui) / 100;
        if (path == DMG_TEXT_ATLAS)
          np_log("dmgnum: text %.0fx FONT_UI (%d px tall), glyphs from the %d%% text atlas", DMG_TEXT_SCALE, px,
                 dmg_atlas_pct(ui));
        else
          np_log("dmgnum: text %.0fx FONT_UI (%d px tall), no %d%% text atlas: baked glyphs through the canvas "
                 "transform, point sampled", DMG_TEXT_SCALE, px, dmg_drawn_pct(ui));
      }
    }
    // Laid out at 1x around the anchor, then scaled about it: rise, stack and keyline all grow with the text. On
    // the text atlas only the glyph origins are scaled; the glyphs are already the drawn size.
    const int ax = clamp_double_to_int((double)sx + 0.5), ay = clamp_double_to_int((double)sy + 0.5);
    canvas_set_transform(0, 0, DMG_TEXT_SCALE, ax, ay);
    esp_draw_label(ax - canvas_text_width(s, FONT_UI) / 2, ay - textH / 2 - rise, s, col, FONT_UI);
    canvas_set_transform(0, 0, 1.0, 0, 0);
    s_drawn++;
  }
  canvas_pop_text_scale();
}

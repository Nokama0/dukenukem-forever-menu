// Ported from duke-dnmp's canvas.h (itself AA's ui/canvas.h): the palette and the drawing declarations, so
// menu.cpp ports unchanged. Dropped: the item and secret colours and the engine quit release (DNMP engine facts).
// The backend is prim_d3d9.cpp on the captured device.
#pragma once
#include "ui/ui.h"     // ui_scale_clamped, UI_SCALE_DEFAULT for canvas_atlas_step
// Colours are 0xAARRGGBB.
const unsigned C_PANEL   = 0xF014171A;
const unsigned C_RECESS  = 0xF01C2127;
const unsigned C_RAISED  = 0xFF333C46;
const unsigned C_BEVELHI = 0xFF48535F;
const unsigned C_BEVELLO = 0xFF0A0C0E;
const unsigned C_HAZARD  = 0xFFF2B01E;
const unsigned C_RED     = 0xFFD8402F;
const unsigned C_BLUE    = 0xFF3C9BD6;
const unsigned C_GREEN   = 0xFF6FBF5B;
const unsigned C_BONE    = 0xFFE8E4DA;
const unsigned C_BONEDIM = 0xFF9AA1A8;
const unsigned C_BONEFNT = 0xFF666E77;
const unsigned C_WELL      = 0xFF0E1114;  // darkest sunken steel: switch/slider wells, tab rail
const unsigned C_HAZARDDIM = 0xFF8A6512;  // dimmed hazard, the slider's filled track
const unsigned C_SETTINGS  = 0xFFB8A9E0;  // Settings tab rail accent: muted lavender, distinct from the other tab colours
const unsigned C_PROGRESS  = 0xFF4FC9B0;  // Progress tab rail accent: teal, between the View green and the Visuals blue
// Helmets and Phone messages ESP: apart from every ESP colour in use.
const unsigned C_HELMET    = 0xFFE8873A;  // warm orange
const unsigned C_PHONE     = 0xFF9FE3F0;  // pale cyan
// The Achievements tab's rail and every achievement marker: gold.
const unsigned C_ACHIEVE   = 0xFFE8C547;
const unsigned C_KEYLINE   = 0xFF000000;  // world-overlay text outline (AA's C_KEYLINE), always on
// Friendlies (the sibling ports' hue 325): every other region is taken (red enemies, blue items, yellow secrets).
const unsigned C_FRIENDLY  = 0xFFE85FA8;
// The aim calibration marker (duke-dnmp's): violet, used nowhere else so it never reads as an ESP category.
const unsigned C_AIM       = 0xFFC061E8;

// Glass style's four colours (menuanim == 2 only; unused under Off/Simple),
// the only translucent-surface entries in this palette -- what a surface is
// seen through, not a surface itself.
const unsigned C_GLASS_TINT = 0x99141C26;  // slab ground over the blurred backdrop capture; cool/dark since Duke levels read brown/orange, alpha doubles as "how much scene shows through"
const unsigned C_GLASS_RIM  = 0xB0A8C8E0;  // one-pixel rim under the slab; brighter than C_BEVELHI since Glass has no bevel to share the job
const unsigned C_GLASS_TOP  = 0x40D8E8F8;  // top-edge sheen gradient, not a second rim -- stops the slab reading as a flat hole
const unsigned C_GLASS_GLOW = 0x66A8D8FF;  // additive bloom (open sweep, switch flare, hover glow); one colour since additive RGB adds to what's under it

// True when canvas_hazard's stripe geometry is well formed: a non-positive period never advances the stripe loop, and a
// non-positive w/h yields a degenerate scissor box. Pure and header-only so test_host exercises it without linking
// canvas.cpp.
inline bool canvas_hazard_params_ok(int w, int h, int period) {
  return w > 0 && h > 0 && period > 0;
}

// Health bar colour at hp/maxhp.
inline unsigned canvas_health_colour(int hp, int maxhp) {
  if (maxhp <= 0 || hp <= 0) return C_RED;   // no band, and no division by zero
  const long long pct = (long long)hp * 100 / maxhp;
  if (pct > 50) return C_GREEN;
  if (pct > 25) return C_HAZARD;
  return C_RED;
}

// Filled length of a bar `total` pixels long at hp/maxhp; never exceeds `total` even when hp reads above the max.
inline int canvas_bar_fill(int total, int hp, int maxhp) {
  if (total <= 0 || maxhp <= 0 || hp <= 0) return 0;
  if (hp >= maxhp) return total;
  return (int)((long long)total * hp / maxhp);
}

// Alpha tap `i` of a `taps`-tap blur, given the tint alpha it must sum to. Cumulative rounding, not a flat aTint/taps
// plus remainder: that goes negative on the last tap (aTint=5, taps=9) and a negative alpha wraps to a wrong colour
// byte. Each tap is the difference of two rounded running totals, so none is negative and they sum to `aTint`.
inline int canvas_tap_alpha(int aTint, int taps, int i) {
  if (taps <= 0 || i < 0 || i >= taps) return 0;
  if (aTint < 0) aTint = 0;
  if (aTint > 255) aTint = 255;
  const int upto = (int)((double)aTint * (double)(i + 1) / (double)taps + 0.5);
  const int prev = (int)((double)aTint * (double)i / (double)taps + 0.5);
  return upto - prev;
}

// A colour (0xAARRGGBB) with its alpha byte replaced, clamped to a byte; the three colour channels survive bit for bit.
// Shared by the damage numbers' fade, the panel's open alpha and tab crossfade, and the Glass style's grounds and
// glows.
inline unsigned canvas_with_alpha(unsigned rgba, int alpha) {
  if (alpha < 0) alpha = 0;
  if (alpha > 255) alpha = 255;
  return (rgba & 0x00FFFFFFu) | ((unsigned)alpha << 24);
}

// Second canvas-wide mode beside the UI scale below: the panel's open motion moves every drawn pixel while the LOGICAL
// layout used for hit testing stays put. On device coordinates after the UI scale: d = ((p - c) * scale + c) + offset.
// Identity is the exact default (Off and the ESP unchanged); canvas_begin resets it, menu_draw sets it each frame.
struct CanvasTransform { int dx, dy, cx, cy; double scale; };
void canvas_set_transform(int dx, int dy, double scale, int cx, int cy);

// Which atlas a scale draws from. 100 is the baked one; any other scale needs its own rasterised atlas (fontgen.h): the
// held one if it is that scale's, a build if not, a refusal (100% again) for a scale whose build failed: one warning,
// never a rebuild per frame. pct is clamped as canvas_set_scale does; heldPct, failedPct 0 for none.
enum { CANVAS_ATLAS_BAKED = 0, CANVAS_ATLAS_HELD = 1, CANVAS_ATLAS_BUILD = 2, CANVAS_ATLAS_REFUSED = 3 };
inline int canvas_atlas_step(int pct, int heldPct, int failedPct) {
  const int p = ui_scale_clamped(pct);
  if (p == UI_SCALE_DEFAULT) return CANVAS_ATLAS_BAKED;
  if (p == heldPct) return CANVAS_ATLAS_HELD;
  if (p == failedPct) return CANVAS_ATLAS_REFUSED;
  return CANVAS_ATLAS_BUILD;
}

// Every coordinate handed to the primitives below is LOGICAL; the canvas multiplies by this factor (`pct`, clamped by
// the setter). menu_draw sets it on entry and restores the default on return (the ESP shares these primitives). Other
// scales use an atlas built on first use (fontgen.h); if that fails 100 applies: read canvas_scale().
void canvas_set_scale(int pct);
int  canvas_scale();

// Text drawn from an atlas rasterised at `pct` until the pop, for text larger than the UI scale through the transform
// (damage numbers). `pct` is the drawn size, UI scale times transform scale; widths stay logical. False (nothing
// changed) if no atlas can be had (pct 100, out of range, failed build, logged once). Not nested; canvas_begin ends it.
bool canvas_push_text_scale(int pct);
void canvas_pop_text_scale(void);

// The device the canvas draws on and keeps its atlas textures on (managed pool, so they survive Reset), set by
// frame.cpp every frame inside its BeginScene; NULL draws nothing this frame.
void canvas_device(struct IDirect3DDevice9* dev);
// The device is being replaced: every texture of ours on it goes, so the game's last Release can destroy it.
// Returns how many were released; the next canvas_device rebuilds them on the new device.
int canvas_release_device(void);
// Whether a texture of ours holds a reference on the device it was made on.
bool canvas_holds_device(void);

void canvas_begin(int w, int h);
void canvas_end();
// One begin/end per frame for everything we draw (a begin is a full state-block capture): the first drawer calls
// canvas_open, later ones find it open, frame.cpp calls canvas_close once. A frame nobody draws in costs nothing.
void canvas_open(int w, int h);
void canvas_close();
void canvas_rect(int x, int y, int w, int h, unsigned rgba);
void canvas_bevel(int x, int y, int w, int h, unsigned fill, unsigned hi, unsigned lo);
void canvas_line(int x0, int y0, int x1, int y1, unsigned rgba);
// A flat triangle, any winding (reordered to the engine quad's); a degenerate
// one draws nothing. The arrowheads and the secret volumes' faces.
void canvas_tri(int x0, int y0, int x1, int y1, int x2, int y2, unsigned rgba);
void canvas_hazard(int x, int y, int w, int h, unsigned stripe, unsigned ground, int period);
int canvas_text(int x, int y, const char* s, unsigned rgba, int font);
int canvas_text_width(const char* s, int font);

void canvas_round_rect(int x, int y, int w, int h, int r, unsigned rgba);
// A rounded rectangle's outline: a ring `thickness` logical pixels wide, just inside the rect, nothing in the middle.
// Its own primitive: a filled round rect under the slab only reads as an edge under an opaque slab (Glass's translucent
// slab washed the face light blue). `thickness` floors at one device pixel so it cannot round away the panel's edge.
void canvas_round_rect_outline(int x, int y, int w, int h, int r, int thickness,
                               unsigned rgba);
// A clip rect over everything drawn until the matching pop, in the same LOGICAL coordinates every primitive takes (UI
// scale and panel transform applied here); it covers every shape and glyph (canvas_hazard, the scrolled tab body).
// Pushed without intersecting, like the scissor box it stands in for. Every push is popped in the same frame.
void canvas_push_clip(int x, int y, int w, int h);
void canvas_pop_clip();

void canvas_gradient_rect(int x, int y, int w, int h, unsigned top, unsigned bottom);
void canvas_glow_rect(int x, int y, int w, int h, unsigned rgba);
bool canvas_backdrop_capture(int x, int y, int w, int h);
void canvas_backdrop_draw(int x, int y, int w, int h, int r, unsigned tint,
                          double wobbleAmp, double wobblePhase, int blurTaps, int blurRadius);
void canvas_backdrop_release();

// The atlas texel a flat fill samples, for a caller queueing its OWN triangles through the batcher (the solid fills are
// a projected polygon). Selects the frame's atlas as a side effect, so the triangles that follow land in the same draw
// call. False when no atlas could be built, which is a reason to draw nothing.
bool canvas_white_texel(float* u, float* v);

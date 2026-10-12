// duke-dnmp's canvas.cpp (itself AA's ui/canvas.cpp) on prim_d3d9: the device arithmetic, corner tessellation,
// glyph quads, CPU clipping (canvas_clip.h) and UI scale are theirs. The atlases are managed-pool textures on the
// captured device (d3dres.h's lifetime): the baked one from font_data.cpp, a scaled one rasterised by fontgen.
#include <d3d9.h>
#include "canvas.h"
#include "canvas_clip.h"
#include "font.h"
#include "prim.h"
#include "log.h"
#include "fontgen.h"   // the scaled atlas
#include "atlaswhite.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------ scale, transform
// The applied UI scale (canvas.h). Only ever a scale whose atlas is in hand: canvas_set_scale falls back to 100
// when the rasterised atlas could not be built, so nothing is drawn as stretched baked glyphs.
static int s_scale = UI_SCALE_DEFAULT;

static int    s_txDx = 0, s_txDy = 0, s_txCx = 0, s_txCy = 0;
static double s_txScale = 1.0;
static bool   s_txIdentity = true;

void canvas_set_transform(int dx, int dy, double scale, int cx, int cy) {
  s_txDx = dx; s_txDy = dy; s_txCx = cx; s_txCy = cy;
  s_txScale = scale;
  s_txIdentity = (dx == 0 && dy == 0 && scale == 1.0);
}

static int tx_round(double v) {
  v = v < 0.0 ? v - 0.5 : v + 0.5;
  if (!(v > -2147483648.0)) return -2147483647 - 1;
  if (v > 2147483647.0) return 2147483647;
  return (int)v;
}
// AA's composition: a device coordinate (already through the UI scale) moved and scaled by the panel transform,
// whose displacement and centre are logical and take the UI scale too.
static int tx_dev_x(int d) {
  if (s_txIdentity) return d;
  const double c = (double)ui_scale_px(s_txCx, s_scale);
  return tx_round(((double)d - c) * s_txScale + c + (double)ui_scale_px(s_txDx, s_scale));
}
static int tx_dev_y(int d) {
  if (s_txIdentity) return d;
  const double c = (double)ui_scale_px(s_txCy, s_scale);
  return tx_round(((double)d - c) * s_txScale + c + (double)ui_scale_px(s_txDy, s_scale));
}
static int tx_dev_len(int d) {
  if (s_txIdentity) return d;
  return tx_round((double)d * s_txScale);
}
// Logical -> device: the UI scale, then the transform. At 100% the scale is skipped, so the ESP's coordinates
// pass through untouched.
static int scale_px(int v) { return s_scale == UI_SCALE_DEFAULT ? v : ui_scale_px(v, s_scale); }
static int dev_x(int x)   { return tx_dev_x(scale_px(x)); }
static int dev_y(int y)   { return tx_dev_y(scale_px(y)); }
static int dev_len(int v) { return tx_dev_len(scale_px(v)); }

// ------------------------------------------------------------------ the atlases The baked atlas is uploaded from a
// copy of font_atlas with an opaque white block at (508..511, 252..255) (atlaswhite.h), which serves every flat fill so
// the whole canvas draws from one texture. Without it, fills draw in vertex colour alone and text is skipped.
static IDirect3DDevice9* s_dev;      // this frame's device (canvas_device), NULL: draw nothing
static IDirect3DDevice9* s_texDev;   // the device our textures live on
static void* s_baked;
static bool s_bakedFailed;
static bool s_live;
static float s_flatU, s_flatV;       // the current atlas's white texel (apply_atlas)
static const float kAtlasW = 512.f, kAtlasH = 256.f;
static const float kBakedFlatU = 510.f / kAtlasW, kBakedFlatV = 254.f / kAtlasH;

// At any scale but 100 the three faces are rasterised at that size (fontgen.cpp) into a texture of their own.
// One slot: the panel is the only thing drawn scaled. It is replaced only when another scale is applied; back at
// 100 it is kept, so returning to it costs nothing. The pixels stay to rebuild it on a replaced device.
struct RuntimeAtlas {
  FontAtlas run;        // glyph metrics in device pixels, and the pixels
  void* tex;            // on s_texDev, 0 for none
  int pct;              // the scale `run` was built for, 0 for none
  int failedPct;        // the last scale refused (build or upload), 0 for none: never retried every frame
  float flatU, flatV;
};
static RuntimeAtlas s_rt;
// The text scale's atlas (canvas_push_text_scale): text drawn at another size than the UI scale's, the damage
// numbers' FONT_UI at 2x. Its own slot, so it never evicts the panel's; on only between a push and its pop.
static RuntimeAtlas s_lbl;
static bool s_lblOn;

// A slot built on a replaced device goes up again from its pixels; refused, it is dropped and not retried.
static void runtime_reupload(RuntimeAtlas* slot, IDirect3DDevice9* dev, const char* what, const char* keep) {
  if (!slot->pct || slot->tex || !slot->run.pixels) return;
  slot->tex = prim_make_alpha_texture(dev, slot->run.pixels, slot->run.w, slot->run.h);
  if (slot->tex) return;
  np_log("canvas: %s %d%%: atlas not re-uploaded on the new device; keeping %s", what, slot->pct, keep);
  fontgen_free(&slot->run);
  slot->failedPct = slot->pct;
  slot->pct = 0;
}

void canvas_device(IDirect3DDevice9* dev) {
  s_dev = dev;
  if (!dev) return;
  if (s_texDev && s_texDev != dev) canvas_release_device();   // frame.cpp normally did this already
  s_dev = dev;
  s_texDev = dev;
  if (!s_baked && !s_bakedFailed) {
    unsigned char* px = (unsigned char*)malloc((size_t)FONT_ATLAS_W * FONT_ATLAS_H);
    if (px) {
      atlas_add_white_block(px, font_atlas, FONT_ATLAS_W, FONT_ATLAS_H, 508, 252);
      s_baked = prim_make_alpha_texture(dev, px, FONT_ATLAS_W, FONT_ATLAS_H);
      free(px);
    }
    s_bakedFailed = !s_baked;
    if (s_bakedFailed) np_log("canvas: no baked atlas texture; fills in vertex colour, no text");
  }
  runtime_reupload(&s_rt, dev, "UI scale", "100%");
  runtime_reupload(&s_lbl, dev, "text scale", "the baked glyphs");
}

int canvas_release_device(void) {
  int n = 0;
  unsigned long bakedRef = 0, scaledRef = 0, textRef = 0;
  if (s_baked) { bakedRef = prim_release_texture(s_baked); s_baked = 0; n++; }
  if (s_rt.tex) { scaledRef = prim_release_texture(s_rt.tex); s_rt.tex = 0; n++; }
  if (s_lbl.tex) { textRef = prim_release_texture(s_lbl.tex); s_lbl.tex = 0; n++; }
  if (n)
    np_log("canvas: released %d atlas texture(s) (refs left: baked %lu, scaled %lu, text %lu)", n, bakedRef,
           scaledRef, textRef);
  s_bakedFailed = false;
  s_texDev = 0;
  s_dev = 0;
  s_live = false;
  s_lblOn = false;
  return n;
}

bool canvas_holds_device(void) { return s_baked || s_rt.tex || s_lbl.tex; }

// Builds and uploads `pct`'s atlas into `slot`, all or nothing; on success the old one goes. `what` and `keep`
// name the slot and its fallback in the log.
static bool runtime_build(RuntimeAtlas* slot, int pct, const char* what, const char* keep) {
  if (!s_dev || !s_baked) { slot->failedPct = pct; return false; }
  FontAtlas built;
  memset(&built, 0, sizeof(built));
  if (!fontgen_build(pct, &built)) { slot->failedPct = pct; return false; }   // fontgen logged why
  void* tex = prim_make_alpha_texture(s_dev, built.pixels, built.w, built.h);
  if (!tex) {
    np_log("canvas: %s %d%%: the %dx%d atlas texture was refused; keeping %s", what, pct, built.w, built.h, keep);
    fontgen_free(&built);
    slot->failedPct = pct;
    return false;
  }
  if (slot->tex) prim_release_texture(slot->tex);
  fontgen_free(&slot->run);
  slot->run = built;
  slot->tex = tex;
  slot->pct = pct;
  slot->flatU = (float)(built.whiteX + 2) / (float)built.w;
  slot->flatV = (float)(built.whiteY + 2) / (float)built.h;
  np_log("canvas: %s %d%%: atlas %dx%d uploaded", what, pct, built.w, built.h);
  return true;
}

static bool runtime_active(void) { return s_scale != UI_SCALE_DEFAULT && s_rt.tex && s_rt.pct == s_scale; }
static void* current_texture(void) { return s_lblOn ? s_lbl.tex : runtime_active() ? s_rt.tex : s_baked; }

// The current scale's white texel and, mid-frame, its texture (prim draws the queued run on a change, keeping order).
static void apply_atlas(void) {
  if (s_lblOn)               { s_flatU = s_lbl.flatU; s_flatV = s_lbl.flatV; }
  else if (runtime_active()) { s_flatU = s_rt.flatU; s_flatV = s_rt.flatV; }
  else                       { s_flatU = kBakedFlatU; s_flatV = kBakedFlatV; }
  if (s_live) prim_texture(current_texture());
}

void canvas_set_scale(int pct) {
  const int want = ui_scale_clamped(pct);
  switch (canvas_atlas_step(want, s_rt.pct, s_rt.failedPct)) {
    case CANVAS_ATLAS_HELD:  s_scale = want; break;
    case CANVAS_ATLAS_BUILD: s_scale = runtime_build(&s_rt, want, "UI scale", "100%") ? want : UI_SCALE_DEFAULT; break;
    default:                 s_scale = UI_SCALE_DEFAULT; break;   // 100 itself, or a refused scale
  }
  apply_atlas();
}
int canvas_scale() { return s_scale; }

bool canvas_push_text_scale(int pct) {
  s_lblOn = false;
  if (pct != ui_scale_clamped(pct)) { apply_atlas(); return false; }   // fontgen would draw it at the clamp
  switch (canvas_atlas_step(pct, s_lbl.tex ? s_lbl.pct : 0, s_lbl.failedPct)) {
    case CANVAS_ATLAS_HELD:  s_lblOn = true; break;
    case CANVAS_ATLAS_BUILD:
      // No device yet (a frame without BeginScene): try again next time instead of recording a refusal.
      if (s_dev && s_baked) s_lblOn = runtime_build(&s_lbl, pct, "text scale", "the baked glyphs");
      break;
    default: break;   // 100 is the baked atlas itself; a refused pct is not retried
  }
  apply_atlas();
  return s_lblOn;
}

void canvas_pop_text_scale(void) {
  if (!s_lblOn) return;
  s_lblOn = false;
  apply_atlas();
}

// ------------------------------------------------------------------ clipping
static const int kClipMax = 16;
static ClipRect s_clip[kClipMax];
static int s_clipDepth;
static unsigned s_clipOverflow;   // pushes past kClipMax, cumulative

// Past the stack's depth the deepest stored clip stands in, so a runaway
// nesting still clips rather than drawing over the game.
static const ClipRect* clip_top(void) {
  if (s_clipDepth <= 0) return 0;
  return &s_clip[(s_clipDepth <= kClipMax ? s_clipDepth : kClipMax) - 1];
}

void canvas_push_clip(int x, int y, int w, int h) {
  if (s_clipDepth >= kClipMax) {
    if (s_clipOverflow++ == 0) np_log("canvas: clip stack deeper than %d", kClipMax);
    s_clipDepth++;
    return;
  }
  const int dx = dev_x(x), dy = dev_y(y);
  ClipRect c = { (float)dx, (float)dy, (float)(dx + dev_len(w)), (float)(dy + dev_len(h)) };
  s_clip[s_clipDepth++] = c;
}

void canvas_pop_clip() {
  if (s_clipDepth > 0) s_clipDepth--;
}

// ------------------------------------------------------------------ emitters
static unsigned lerp_colour(unsigned a, unsigned b, float t) {
  unsigned out = 0;
  for (int sh = 0; sh < 32; sh += 8) {
    const float ca = (float)((a >> sh) & 0xFF), cb = (float)((b >> sh) & 0xFF);
    int v = (int)(ca + (cb - ca) * t + 0.5f);
    v = v < 0 ? 0 : v > 255 ? 255 : v;
    out |= (unsigned)v << sh;
  }
  return out;
}

// An axis-aligned rect, textured, colour ramping top to bottom.
static void emit_rect(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                      unsigned top, unsigned bottom) {
  if (!s_live || !(x1 > x0) || !(y1 > y0)) return;
  const float oy0 = y0, oh = y1 - y0;
  const ClipRect* c = clip_top();
  if (c && !clip_rect_uv(c, &x0, &y0, &x1, &y1, &u0, &v0, &u1, &v1)) return;
  const unsigned ct = top == bottom ? top : lerp_colour(top, bottom, (y0 - oy0) / oh);
  const unsigned cb = top == bottom ? top : lerp_colour(top, bottom, (y1 - oy0) / oh);
  const PrimVert a = { x0, y0, u0, v0, ct }, b = { x1, y0, u1, v0, ct };
  const PrimVert d = { x1, y1, u1, v1, cb }, e = { x0, y1, u0, v1, cb };
  prim_tri(&a, &b, &d);
  prim_tri(&a, &d, &e);
}

// A flat convex polygon of 3 or 4 points, fanned from the first.
static void emit_poly(const float* px, const float* py, int n, unsigned rgba) {
  if (!s_live) return;
  ClipVert in[4], out[CLIP_POLY_MAX];
  for (int i = 0; i < n; ++i) { in[i].x = px[i]; in[i].y = py[i]; in[i].u = s_flatU; in[i].v = s_flatV; }
  const ClipVert* v = in;
  const ClipRect* c = clip_top();
  if (c) { n = clip_poly(c, in, n, out); v = out; }
  for (int i = 1; i + 1 < n; ++i) {
    const PrimVert a = { v[0].x, v[0].y, s_flatU, s_flatV, rgba };
    const PrimVert b = { v[i].x, v[i].y, s_flatU, s_flatV, rgba };
    const PrimVert d = { v[i + 1].x, v[i + 1].y, s_flatU, s_flatV, rgba };
    prim_tri(&a, &b, &d);
  }
}

static void flat_quad(float x0, float y0, float x1, float y1,
                      float x2, float y2, float x3, float y3, unsigned rgba) {
  const float px[4] = { x0, x1, x2, x3 }, py[4] = { y0, y1, y2, y3 };
  emit_poly(px, py, 4, rgba);
}

static void fill_device(int x, int y, int w, int h, unsigned rgba) {
  emit_rect((float)x, (float)y, (float)(x + w), (float)(y + h), s_flatU, s_flatV, s_flatU, s_flatV, rgba, rgba);
}

// ------------------------------------------------------------------ frame
// Every frame starts at 100% as well as at the identity transform: menu_draw restores 100 on its way out, and this
// keeps a scale left standing by any other path off what draws next.
void canvas_begin(int w, int h) {
  canvas_set_transform(0, 0, 1.0, 0, 0);
  s_clipDepth = 0;
  s_live = false;
  s_scale = UI_SCALE_DEFAULT;
  s_lblOn = false;
  apply_atlas();
  if (!s_dev) return;
  if (!prim_begin(s_dev, w, h)) return;
  prim_texture(current_texture());
  s_live = true;
}

void canvas_end() {
  if (s_clipDepth != 0) {
    static int said = 0;
    if (said < 3) { said++; np_log("canvas: %d clip(s) left pushed at frame end", s_clipDepth); }
    s_clipDepth = 0;
  }
  if (s_live) prim_end();
  s_live = false;
}

static bool s_frameOpen;
void canvas_open(int w, int h) {
  if (s_frameOpen) return;
  canvas_begin(w, h);
  s_frameOpen = true;
}
void canvas_close() {
  if (!s_frameOpen) return;
  s_frameOpen = false;
  canvas_end();
}

// ------------------------------------------------------------------ primitives
void canvas_rect(int x, int y, int w, int h, unsigned rgba) {
  fill_device(dev_x(x), dev_y(y), dev_len(w), dev_len(h), rgba);
}

void canvas_line(int x0, int y0, int x1, int y1, unsigned rgba) {
  const int dx0 = dev_x(x0), dy0 = dev_y(y0);
  const int dx1 = dev_x(x1), dy1 = dev_y(y1);
  // A quad half a pixel either side of the segment between pixel centres:
  // exactly GL's one-pixel line when axis aligned (AA canvas.cpp).
  const float ax = (float)dx0 + 0.5f, ay = (float)dy0 + 0.5f;
  const float bx = (float)dx1 + 0.5f, by = (float)dy1 + 0.5f;
  float nx = 0.5f, ny = 0.0f;
  const float ex = bx - ax, ey = by - ay;
  const float len = (float)sqrt((double)(ex * ex + ey * ey));
  if (len > 0.0f) { nx = -ey / len * 0.5f; ny = ex / len * 0.5f; }
  // A zero-length segment would be a zero-width quad; make it one pixel.
  if (len == 0.0f) { fill_device(dx0, dy0, 1, 1, rgba); return; }
  flat_quad(ax - nx, ay - ny, bx - nx, by - ny, bx + nx, by + ny, ax + nx, ay + ny, rgba);
}

void canvas_tri(int x0, int y0, int x1, int y1, int x2, int y2, unsigned rgba) {
  float px[3] = { (float)dev_x(x0), (float)dev_x(x1), (float)dev_x(x2) };
  float py[3] = { (float)dev_y(y0), (float)dev_y(y1), (float)dev_y(y2) };
  // Positive area with y down, as TL -> TR -> BR: every other canvas triangle's winding.
  const float area = (px[1] - px[0]) * (py[2] - py[0]) - (py[1] - py[0]) * (px[2] - px[0]);
  if (area == 0.f) return;
  if (area < 0.f) {
    const float tx = px[1], ty = py[1];
    px[1] = px[2]; py[1] = py[2]; px[2] = tx; py[2] = ty;
  }
  emit_poly(px, py, 3, rgba);
}

void canvas_bevel(int x, int y, int w, int h, unsigned fill, unsigned hi, unsigned lo) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  int e = dev_len(1);
  if (e < 1) e = 1;
  fill_device(dx,          dy,          dw, dh,         fill);
  fill_device(dx,          dy,          dw, e,          hi);  // top, full width
  fill_device(dx,          dy + dh - e, dw, e,          lo);  // bottom, full width
  fill_device(dx,          dy + e,      e,  dh - 2 * e, hi);  // left, between top and bottom
  fill_device(dx + dw - e, dy + e,      e,  dh - 2 * e, lo);  // right, between top and bottom
}

void canvas_hazard(int x, int y, int w, int h, unsigned stripe, unsigned ground, int period) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  const int dp = dev_len(period);
  if (!canvas_hazard_params_ok(dw, dh, dp)) return;
  if (!s_live) return;
  fill_device(dx, dy, dw, dh, ground);
  // 45-degree bars clipped to the strip, pushed without intersecting as AA's
  // PushClipRect(..., false) was. Device rect, so it bypasses the logical push.
  if (s_clipDepth >= kClipMax) return;
  const ClipRect strip = { (float)dx, (float)dy, (float)(dx + dw), (float)(dy + dh) };
  s_clip[s_clipDepth++] = strip;
  for (int i = -dh; i < dw + dh; i += dp * 2) {
    // TL, TR, BR, BL: the engine quad's winding (positive area, y down).
    flat_quad((float)(dx + i + dh),      (float)dy,
              (float)(dx + i + dp + dh), (float)dy,
              (float)(dx + i + dp),      (float)(dy + dh),
              (float)(dx + i),           (float)(dy + dh), stripe);
  }
  s_clipDepth--;
}

// AA's segment rule: grows with radius, floor 3, ceiling 12.
static int round_rect_segments(int r) {
  const int s = 3 + (r > 0 ? r / 2 : 0);
  return s < 3 ? 3 : s > 12 ? 12 : s;
}

static const int ROUND_FAN_MAX = 4 * 12;
static int round_fan_points(int dx, int dy, int dw, int dh, int dr, int seg, float* px, float* py) {
  if (seg < 3) seg = 3;
  if (seg > 12) seg = 12;
  const double pi = 3.14159265358979323846;
  const float cx[4] = { (float)(dx + dr), (float)(dx + dw - dr), (float)(dx + dw - dr), (float)(dx + dr) };
  const float cy[4] = { (float)(dy + dr), (float)(dy + dr), (float)(dy + dh - dr), (float)(dy + dh - dr) };
  int n = 0;
  for (int c = 0; c < 4; c++) {
    const double a0 = (180.0 + 90.0 * c) * pi / 180.0;
    for (int i = 0; i < seg; i++) {
      const double a = a0 + (pi / 2.0) * (double)i / (double)(seg - 1);
      px[n] = cx[c] + (float)(dr * cos(a));
      py[n] = cy[c] + (float)(dr * sin(a));
      n++;
    }
  }
  return n;
}

static void round_rect_device(int dx, int dy, int dw, int dh, int dr, int seg, unsigned rgba) {
  float px[ROUND_FAN_MAX], py[ROUND_FAN_MAX];
  const int n = round_fan_points(dx, dy, dw, dh, dr, seg, px, py);
  const float cx = (float)dx + (float)dw * 0.5f, cy = (float)dy + (float)dh * 0.5f;
  for (int i = 0; i < n; i++) {
    const int j = (i + 1 < n) ? i + 1 : 0;
    const float tx[3] = { cx, px[i], px[j] }, ty[3] = { cy, py[i], py[j] };
    emit_poly(tx, ty, 3, rgba);
  }
}

static int corner_radius_device(int dw, int dh, int r) {
  int dr = dev_len(r > 0 ? r : 0);
  const int half = (dw < dh ? dw : dh) / 2;
  if (dr > half) dr = half;
  return dr < 0 ? 0 : dr;
}

void canvas_round_rect(int x, int y, int w, int h, int r, unsigned rgba) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  const int dr = corner_radius_device(dw, dh, r);
  round_rect_device(dx, dy, dw, dh, dr, round_rect_segments(dr), rgba);
}

void canvas_round_rect_outline(int x, int y, int w, int h, int r, int thickness, unsigned rgba) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  int t = dev_len(thickness > 0 ? thickness : 0);
  if (t < 1) t = 1;
  const int dr = corner_radius_device(dw, dh, r);
  const int seg = round_rect_segments(dr);
  if (dw <= 2 * t || dh <= 2 * t) { round_rect_device(dx, dy, dw, dh, dr, seg, rgba); return; }
  float ox[ROUND_FAN_MAX], oy[ROUND_FAN_MAX], ix[ROUND_FAN_MAX], iy[ROUND_FAN_MAX];
  const int n = round_fan_points(dx, dy, dw, dh, dr, seg, ox, oy);
  const int ir = dr - t < 0 ? 0 : dr - t;
  round_fan_points(dx + t, dy + t, dw - 2 * t, dh - 2 * t, ir, seg, ix, iy);
  // Each rung pair as two triangles, o0 o1 i0 and i0 o1 i1: the engine quad's
  // winding, like every other canvas triangle, so `cull none` is not load-bearing.
  for (int i = 0; i < n; i++) {
    const int j = (i + 1 < n) ? i + 1 : 0;
    const float ax[3] = { ox[i], ox[j], ix[i] }, ay[3] = { oy[i], oy[j], iy[i] };
    const float bx[3] = { ix[i], ox[j], ix[j] }, by[3] = { iy[i], oy[j], iy[j] };
    emit_poly(ax, ay, 3, rgba);
    emit_poly(bx, by, 3, rgba);
  }
}

void canvas_gradient_rect(int x, int y, int w, int h, unsigned top, unsigned bottom) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  emit_rect((float)dx, (float)dy, (float)(dx + dw), (float)(dy + dh), s_flatU, s_flatV, s_flatU, s_flatV,
            top, bottom);
}

// Additive approximated with alpha, as AA does (its canvas.cpp gives the
// measured error); the ortho pool has one blend state per shader.
void canvas_glow_rect(int x, int y, int w, int h, unsigned rgba) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  fill_device(dx, dy, dw, dh, rgba);
}

// The Glass backdrop is stubbed exactly as AA's is: the tint alone.
bool canvas_backdrop_capture(int, int, int, int) {
  static int said = 0;
  if (!said) { said = 1; np_log("canvas: the Glass backdrop capture is not implemented; the style degrades to its tint"); }
  return false;
}

void canvas_backdrop_draw(int x, int y, int w, int h, int r, unsigned tint, double, double, int, int) {
  canvas_round_rect(x, y, w, h, r, tint);
}

void canvas_backdrop_release() {}

bool canvas_white_texel(float* u, float* v) {
  if (u) *u = s_flatU;
  if (v) *v = s_flatV;
  return s_baked != 0;
}

// ------------------------------------------------------------------ text On the scaled atlas the metrics are DEVICE
// pixels: the pen starts at zero and every advance is already a device advance, so nothing rounds per character. On the
// baked atlas the pen stays logical. Width and glyphs always come from the same atlas.
int canvas_text(int x, int y, const char* s, unsigned rgba, int font) {
  if (!s || font < 0 || font >= FONT_COUNT) return x;
  if (!s_live || !s_baked) return x;
  if (s_lblOn) {
    // The text scale's glyphs are already the drawn size: only the origin takes the UI scale and the transform.
    const Glyph* glyphs = s_lbl.run.glyphs[font];
    const float aw = (float)s_lbl.run.w, ah = (float)s_lbl.run.h;
    const int ox = dev_x(x), oy = dev_y(y);
    int pen = 0;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
      if (*p < 0x20 || *p > 0x7F) continue;
      const Glyph& g = glyphs[*p - 0x20];
      if (g.w > 0 && g.h > 0) {
        const float gx = (float)(ox + pen + g.bx), gy = (float)(oy + g.by);
        emit_rect(gx, gy, gx + (float)g.w, gy + (float)g.h, (float)g.x / aw, (float)g.y / ah,
                  (float)(g.x + g.w) / aw, (float)(g.y + g.h) / ah, rgba, rgba);
      }
      pen += g.adv;
    }
    return x + ui_unscale_px(pen, s_lbl.pct);
  }
  const bool device = runtime_active();
  const Glyph* glyphs = device ? s_rt.run.glyphs[font] : font_glyphs[font];
  const float aw = device ? (float)s_rt.run.w : kAtlasW, ah = device ? (float)s_rt.run.h : kAtlasH;
  const int ox = device ? ui_scale_px(x, s_scale) : 0;
  const int oy = device ? ui_scale_px(y, s_scale) : 0;
  int pen = device ? 0 : x;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
    if (*p < 0x20 || *p > 0x7F) continue;
    const Glyph& g = glyphs[*p - 0x20];
    if (g.w > 0 && g.h > 0) {
      // Integer device pixels and texel-edge UVs: with nearest filtering each
      // glyph texel lands on exactly one screen pixel.
      const float gx = device ? (float)tx_dev_x(ox + pen + g.bx) : (float)dev_x(pen + g.bx);
      const float gy = device ? (float)tx_dev_y(oy + g.by)       : (float)dev_y(y + g.by);
      const float gw = device ? (float)tx_dev_len(g.w)           : (float)dev_len(g.w);
      const float gh = device ? (float)tx_dev_len(g.h)           : (float)dev_len(g.h);
      emit_rect(gx, gy, gx + gw, gy + gh,
                (float)g.x / aw, (float)g.y / ah,
                (float)(g.x + g.w) / aw, (float)(g.y + g.h) / ah, rgba, rgba);
    }
    pen += g.adv;
  }
  // Logical, like the x it was seeded from.
  return device ? x + ui_unscale_px(pen, s_scale) : pen;
}

// LOGICAL (canvas.h): the scaled atlas's device advances are unscaled once, for the whole string.
int canvas_text_width(const char* s, int font) {
  if (!s || font < 0 || font >= FONT_COUNT) return 0;
  if (s_lblOn) {
    int w = 0;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++)
      if (*p >= 0x20 && *p <= 0x7F) w += s_lbl.run.glyphs[font][*p - 0x20].adv;
    return ui_unscale_px(w, s_lbl.pct);
  }
  const bool device = runtime_active();
  const Glyph* glyphs = device ? s_rt.run.glyphs[font] : font_glyphs[font];
  int w = 0;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
    if (*p < 0x20 || *p > 0x7F) continue;
    w += glyphs[*p - 0x20].adv;
  }
  return device ? ui_unscale_px(w, s_scale) : w;
}

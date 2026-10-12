#pragma once
// A pawn's screen box from its collision cylinder's 8 bounding-box corners, projected with view.h. Corners behind the
// near plane have no screen point, so they never enter the min/max: the side they would reach runs to the screen edge
// instead.
#include "view.h"
#include <math.h>

struct ScreenPt { float x, y; int ok; };
struct EkScreenBox { int x, y, w, h; };

// ok = 0 when no corner is in front or the box lies wholly off screen.
inline EkScreenBox esp_box_from_corners(const ScreenPt c[8], int W, int H, int* ok) {
  EkScreenBox b = { 0, 0, 0, 0 };
  *ok = 0;
  if (W <= 0 || H <= 0) return b;
  float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
  int front = 0, behind = 0;
  for (int i = 0; i < 8; ++i) {
    if (!c[i].ok) { behind++; continue; }
    if (!front) { x0 = x1 = c[i].x; y0 = y1 = c[i].y; }
    else {
      if (c[i].x < x0) x0 = c[i].x;
      if (c[i].x > x1) x1 = c[i].x;
      if (c[i].y < y0) y0 = c[i].y;
      if (c[i].y > y1) y1 = c[i].y;
    }
    front++;
  }
  if (!front) return b;
  // A behind corner projects outward from the screen centre: extend each side that already reaches past the
  // centre toward its edge.
  const float fW = (float)W, fH = (float)H;
  if (behind) {
    if (x0 < fW * 0.5f) x0 = 0.f;
    if (x1 > fW * 0.5f) x1 = fW;
    if (y0 < fH * 0.5f) y0 = 0.f;
    if (y1 > fH * 0.5f) y1 = fH;
  }
  if (!(x1 > 0.f) || !(y1 > 0.f) || !(x0 < fW) || !(y0 < fH)) return b;
  if (x0 < 0.f) x0 = 0.f;
  if (y0 < 0.f) y0 = 0.f;
  if (x1 > fW) x1 = fW;
  if (y1 > fH) y1 = fH;
  b.x = (int)floorf(x0);
  b.y = (int)floorf(y0);
  const int ex = (int)ceilf(x1), ey = (int)ceilf(y1);
  b.w = ex - b.x < 1 ? 1 : ex - b.x;
  b.h = ey - b.y < 1 ? 1 : ey - b.y;
  *ok = 1;
  return b;
}

// The cylinder's silhouette on screen, unrounded (Location is its centre: CollisionRadius in X/Y, CollisionHeight a
// half-height in Z). The width: centre at mid-height +/- the radius along the horizontal perpendicular to the
// camera->pawn ray, so off-axis pawns aren't widened by footprint depth. Every point must be in front of the camera.
struct EspExtent { float x0, x1, y0, y1; };
inline int esp_pawn_extent(const WView& v, float x, float y, float z, float radius, float height, int W, int H,
                           EspExtent* out) {
  float cx, cy;
  if (!view_w2s(v, x, y, z, W, H, &cx, &cy)) return 0;
  const double dx = (double)x - v.x, dy = (double)y - v.y;
  const double len = sqrt(dx * dx + dy * dy);
  const double px = len > 1e-6 ? -dy / len : 0.0, py = len > 1e-6 ? dx / len : 1.0;   // straight above: any side
  float lx, ly, rx, ry, tx, ty, bx, by;
  if (!view_w2s(v, (float)(x + px * radius), (float)(y + py * radius), z, W, H, &lx, &ly) ||
      !view_w2s(v, (float)(x - px * radius), (float)(y - py * radius), z, W, H, &rx, &ry) ||
      !view_w2s(v, x, y, z + height, W, H, &tx, &ty) || !view_w2s(v, x, y, z - height, W, H, &bx, &by))
    return 0;
  out->x0 = lx < rx ? lx : rx;
  out->x1 = lx < rx ? rx : lx;
  out->y0 = ty < by ? ty : by;
  out->y1 = ty < by ? by : ty;
  return 1;
}

// The box on screen, clamped and rounded as esp_box_from_corners does; ok = 0 when any point is behind the camera
// or the box lies wholly off screen.
inline EkScreenBox esp_pawn_box(const WView& v, float x, float y, float z, float radius, float height, int W, int H,
                                int* ok) {
  *ok = 0;
  EspExtent e;
  if (!esp_pawn_extent(v, x, y, z, radius, height, W, H, &e)) { EkScreenBox none = { 0, 0, 0, 0 }; return none; }
  ScreenPt c[8];
  for (int i = 0; i < 8; ++i) {
    c[i].x = (i & 1) ? e.x1 : e.x0;
    c[i].y = (i & 2) ? e.y1 : e.y0;
    c[i].ok = 1;
  }
  return esp_box_from_corners(c, W, H, ok);
}

#pragma once
// A marker box from the actor's model, not its collision cylinder. The local box is the mesh's Primitive.BoundingBox,
// placed as AActor::LocalToWorldCoords (Engine.dll!0x101FE010) places it for the renderer
// (UStaticMesh::GetRenderBoundingBox 0x102E14D0); its 8 corners are projected and boxed, (follows size and yaw).
#include "espbox.h"
#include <math.h>

// The engine's FBox: Min, Max, IsValid (the byte at +24).
struct MbLocal { float min[3], max[3]; int valid; };
// What LocalToWorldCoords reads off the actor. rot: Pitch, Yaw, Roll, 65536 per turn.
struct MbPlace {
  float loc[3];
  int rot[3];
  float drawScale, scale3d[3], prePivot[3], postPivot[3];
  int lowerByCollision, lowerAsPrePivot;
  float collisionHeight;
};
struct MbCorners { int ok; float p[8][3]; };

// No decoration or pickup model is this long on any axis; a larger box is garbage, not bounds.
const float MB_EXTENT_MAX = 8192.f;

inline bool mb_finite(float v) { return v == v && v > -1e30f && v < 1e30f; }

// A usable local box: IsValid set, finite, Min <= Max, no axis past MB_EXTENT_MAX, not a single point.
inline bool mb_local_ok(const MbLocal& b) {
  if (!b.valid) return false;
  float longest = 0.f;
  for (int k = 0; k < 3; ++k) {
    if (!mb_finite(b.min[k]) || !mb_finite(b.max[k]) || b.min[k] > b.max[k]) return false;
    const float e = b.max[k] - b.min[k];
    if (e > MB_EXTENT_MAX) return false;
    if (e > longest) longest = e;
  }
  return longest > 0.01f;
}

// The pivots after LocalToWorldCoords' bLowerByCollision: with bLowerAsPrePivot the model drops by CollisionHeight in
// its scaled local Z (PrePivot.Z += H / (DrawScale3D.Z * DrawScale)), else in world Z (PostPivot.Z -= H). False when
// that scale cannot divide.
inline bool mb_pivots(const MbPlace& t, float pre[3], float post[3]) {
  for (int k = 0; k < 3; ++k) {
    pre[k] = t.prePivot[k];
    post[k] = t.postPivot[k];
  }
  if (!t.lowerByCollision) return true;
  if (!t.lowerAsPrePivot) {
    post[2] -= t.collisionHeight;
    return true;
  }
  const float sz = t.scale3d[2] * t.drawScale;
  if (!(fabsf(sz) > 1e-4f) || !mb_finite(sz)) return false;
  pre[2] += t.collisionHeight / sz;
  return true;
}

// World corner i: Location + PostPivot + R * (DrawScale * DrawScale3D * (local - PrePivot)), R the rotator's axes
// as view.h's camera (X forward, Y right, Z up). Bit 0 of i picks Max.X, bit 1 Max.Y, bit 2 Max.Z. ok = 0 for an
// unusable box, a zero or non-finite scale, or a non-finite result.
inline MbCorners mb_corners(const MbPlace& t, const MbLocal& b) {
  MbCorners c = {};
  float pre[3], post[3], s[3];
  if (!mb_local_ok(b) || !mb_pivots(t, pre, post)) return c;
  for (int k = 0; k < 3; ++k) {
    s[k] = t.drawScale * t.scale3d[k];
    if (!(fabsf(s[k]) > 1e-4f) || !mb_finite(s[k]) || !mb_finite(t.loc[k])) return c;
  }
  const double r = 6.283185307179586 / 65536.0;
  const double sp = sin(t.rot[0] * r), cp = cos(t.rot[0] * r);
  const double sy = sin(t.rot[1] * r), cy = cos(t.rot[1] * r);
  const double sr = sin(t.rot[2] * r), cr = cos(t.rot[2] * r);
  const double ax[3][3] = { { cp * cy, cp * sy, sp },
                            { sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp },
                            { -(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp } };
  for (int i = 0; i < 8; ++i) {
    const double l[3] = { ((i & 1) ? b.max[0] : b.min[0]) - pre[0], ((i & 2) ? b.max[1] : b.min[1]) - pre[1],
                          ((i & 4) ? b.max[2] : b.min[2]) - pre[2] };
    for (int k = 0; k < 3; ++k) {
      const double w = t.loc[k] + post[k] + ax[0][k] * s[0] * l[0] + ax[1][k] * s[1] * l[1] + ax[2][k] * s[2] * l[2];
      c.p[i][k] = (float)w;
      if (!mb_finite(c.p[i][k])) return c;
    }
  }
  c.ok = 1;
  return c;
}

// The corners on screen, boxed and clamped as esp_box_from_corners does. Every corner must be in front of the camera,
// as esp_pawn_box requires of a pawn: a model half behind the view gets no box rather than one run to the edges.
inline EkScreenBox mb_screen_box(const WView& v, const MbCorners& c, int W, int H, int* ok) {
  *ok = 0;
  EkScreenBox none = { 0, 0, 0, 0 };
  if (!c.ok) return none;
  ScreenPt s[8];
  for (int i = 0; i < 8; ++i) {
    if (!view_w2s(v, c.p[i][0], c.p[i][1], c.p[i][2], W, H, &s[i].x, &s[i].y)) return none;
    s[i].ok = 1;
  }
  return esp_box_from_corners(s, W, H, ok);
}

// The marker box: the model's when its corners resolved, else the collision cylinder's (radius and height clamped
// by the caller).
inline EkScreenBox mb_marker_box(const WView& v, const MbCorners& c, float x, float y, float z, float radius,
                                 float height, int W, int H, int* ok) {
  if (c.ok) return mb_screen_box(v, c, W, H, ok);
  return esp_pawn_box(v, x, y, z, radius, height, W, H, ok);
}

// --- The glue (modelbox.cpp), game thread ------------------------------------------------------------------------
// Outside every guard, once per frame before the reads: resolves the names and forgets the class caches on a new
// level.
void modelbox_frame(void);
// Under the caller's access-violation guard: the actor's model corners by reflection (DrawType, its StaticMesh,
// that mesh's Primitive.BoundingBox, the placement); out->ok = 0 when any part does not resolve, and the
// marker keeps its cylinder.
void modelbox_read(const void* actor, const void* cls, MbCorners* out);
// The counts since the last call, as one log line (esp.cpp's 600-frame instruments).
void modelbox_log(void);

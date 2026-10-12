#pragma once
// World-to-screen from the player's view, axes and signs as the engine's camera: ComputeRenderTransforms
// (Engine.dll!0x10215A60) applies FInverseRotationMatrix (0x1002EEB0; rotator Pitch +0, Yaw +4, Roll +8, 65536 a turn),
// maps X to depth, Y right, Z up; FPerspectiveMatrix (0x100D2990) takes a horizontal half-FOV, square pixels.
#include <math.h>

// fovDeg is the player's CurrentFOV; aspectScale the r_AspectScale cvar (0 = unread: the image's own 1.0f).
struct WView { float x, y, z; int pitch, yaw, roll; float fovDeg; int ok; float aspectScale; };

// FSceneNode::GetFOV (Engine.dll!0x10212630, read in the disassembly 2026-10-04): F = FOV * r_AspectScale; a
// view wider than 4:3 (W/H > 1.3333, double at 0x104BFBC0) widens it Hor+ to 2*atan(tan(F/2) * (W/H)/(4/3)),
// the half-width at 4:3 kept; 4:3 and narrower return F. Both render callers pass 1 (0x102146E1, 0x10212937).
inline float view_effective_hfov(float fovDeg, int W, int H, float aspectScale) {
  const double f = (double)fovDeg * aspectScale;
  if (W <= 0 || H <= 0) return (float)f;
  const double aspect = (double)W / (double)H;
  if (!(aspect > 4.0 / 3.0)) return (float)f;
  const double pi = 3.141592653589793;
  return (float)(atan(tan(f * pi / 360.0) * aspect / (4.0 / 3.0)) * 360.0 / pi);
}

// Points nearer than this along the view axis are rejected: behind the camera never projects (no mirrored boxes).
const float VIEW_NEAR = 1.0f;

// 1 with the screen point (which may lie off screen) when the point is in front of the camera; 0 when it is
// behind, on the near plane, or the view is unusable.
inline int view_w2s(const WView& v, float wx, float wy, float wz, int W, int H, float* sx, float* sy) {
  const float hfov = view_effective_hfov(v.fovDeg, W, H, v.aspectScale > 0.f ? v.aspectScale : 1.f);
  if (!v.ok || W <= 0 || H <= 0 || !(hfov > 1.f && hfov < 179.f)) return 0;
  const double k = 6.283185307179586 / 65536.0;
  const double sp = sin(v.pitch * k), cp = cos(v.pitch * k);
  const double sy_ = sin(v.yaw * k), cy = cos(v.yaw * k);
  const double sr = sin(v.roll * k), cr = cos(v.roll * k);
  const double dx = wx - v.x, dy = wy - v.y, dz = wz - v.z;
  // The columns of the engine's inverse rotation matrix: forward, right, up.
  const double depth = dx * (cp * cy) + dy * (cp * sy_) + dz * sp;
  const double right = dx * (sr * sp * cy - cr * sy_) + dy * (sr * sp * sy_ + cr * cy) + dz * (-sr * cp);
  const double up = dx * (-(cr * sp * cy + sr * sy_)) + dy * (sr * cy - cr * sp * sy_) + dz * (cr * cp);
  if (depth < VIEW_NEAR) return 0;
  const double f = (W * 0.5) / tan(hfov * 3.141592653589793 / 360.0);
  *sx = (float)(W * 0.5 + right / depth * f);
  *sy = (float)(H * 0.5 - up / depth * f);
  return 1;
}

#pragma once
// Fullbright's brightness lift (docs/re/freecam.md section 7g): while Fullbright holds RendMap, a shadow lift drawn
// over the engine's frame at Present, before the photo's read and our overlays. Pure rules here; the glue is fblift.cpp.
#include <math.h>

enum { FBLIFT_MIN_PCT = 0, FBLIFT_MAX_PCT = 100, FBLIFT_DEF_PCT = 60 };
// 100% is a gamma of 1 / (1 + FBLIFT_K_MAX): 0.1 -> 0.40, 0.9 -> 0.96.
const float FBLIFT_K_MAX = 1.5f;

// The slider's percentage as the gamma's exponent 1 / (1 + k); 0% is 1, the identity.
inline float fblift_exponent(int pct) {
  const int p = pct < FBLIFT_MIN_PCT ? FBLIFT_MIN_PCT : pct > FBLIFT_MAX_PCT ? FBLIFT_MAX_PCT : pct;
  return 1.f / (1.f + FBLIFT_K_MAX * (float)p / (float)FBLIFT_MAX_PCT);
}

// One channel through the curve, x clamped to 0..1: what the pixel shader computes. A gamma rather than
// 1 - (1 - x)^(1 + k): it lifts the darks harder (0.1 -> 0.30 against 0.18 at 60%), and its slope at white is the
// exponent (0.53 at 60%) where the other's is 0, which would flatten the highlights.
inline float fblift_curve(float x, float e) {
  const float c = x < 0.f ? 0.f : x > 1.f ? 1.f : x;
  return powf(c, e);
}

// Drawn only while Fullbright holds RendMap, the slider is above 0 and nothing has failed this session.
inline int fblift_wanted(int holding, int pct, int failed) { return holding && pct > 0 && !failed; }

// --- The glue (fblift.cpp), the presenting thread --------------------------------------------------------------
struct IDirect3DDevice9;
// Present, before the photo's read: the lift over the back buffer while fblift_wanted. holding: Fullbright holds
// RendMap (freecam_bright_holding).
void fblift_present(IDirect3DDevice9* dev, int W, int H, int holding);
// A Reset or a new device: the copy texture goes (default pool); the shader goes with a replaced device.
void fblift_device_lost(int replaced);
// The frames drawn and their CPU time since the last take (freecam's 600-frame line).
void fblift_take(unsigned* frames, double* ms);
// Null while usable; else why the lift is off for the session.
const char* fblift_refused(void);

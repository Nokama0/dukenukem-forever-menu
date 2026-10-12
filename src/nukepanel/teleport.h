#pragma once
// The free camera's teleport (freecam.md 4): where Duke would land and why not, pure for the suite. teleport.cpp:
// Actor.TraceEx from the camera, Actor.FindSpot fits the spot, Pawn.PawnSetLocation moves him. DNMP's placement shape
// (Z-up): Location is the centre of Duke's collision cylinder (CollisionRadius, CollisionHeight its half height).
#include <math.h>

enum { TP_OK = 0, TP_NO_HIT, TP_BAD_NORMAL, TP_NO_FLOOR, TP_NO_ROOM, TP_MOUNTED, TP_NO_PLAYER, TP_UNAVAILABLE,
       TP_REASON_COUNT };

const float TP_RAY_LEN = 8192.f;    // UU from the camera, about 156 m (DNMP's 8192)
const float TP_DROP_LEN = 2000.f;   // how far below a wall or ceiling spot a floor is looked for (DNMP's)
const float TP_FLOOR_NZ = 0.7f;     // a floor faces up more steeply than this (DNMP's TP_FLOOR_NY)
const float TP_MARGIN = 1.f;        // UU of clearance beyond the cylinder (freecam.md: radius + 1)

inline const char* tp_reason_text(int r) {
  switch (r) {
    case TP_OK:          return "ok";
    case TP_NO_HIT:      return "the ray hit nothing (the sky, or out of reach)";
    case TP_BAD_NORMAL:  return "the hit normal is not a unit vector facing the ray";
    case TP_NO_FLOOR:    return "no floor below the spot";
    case TP_NO_ROOM:     return "no room for Duke there (FindSpot)";
    case TP_MOUNTED:     return "Duke is mounted (a turret, a vehicle or a ladder)";
    case TP_NO_PLAYER:   return "no player";
    case TP_UNAVAILABLE: return "the trace, FindSpot or Duke's fields did not resolve";
    default:             return "unknown";
  }
}

// pos: where Duke's Location would go. drop: a wall or ceiling spot still needs a floor below it (tp_drop).
struct TpPlace { int reason; float pos[3]; int drop; };

// hit: the trace met something; at its HitLocation, n its HitNormal, dir the ray's direction.
inline TpPlace tp_place(int hit, const float at[3], const float n[3], const float dir[3], float radius, float height) {
  TpPlace p = { TP_NO_HIT, { 0.f, 0.f, 0.f }, 0 };
  if (!hit) return p;
  const float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
  if (!(len > 0.9f && len < 1.1f) || !(n[0] * dir[0] + n[1] * dir[1] + n[2] * dir[2] < 0.f)) {
    p.reason = TP_BAD_NORMAL;
    return p;
  }
  p.reason = TP_OK;
  if (n[2] > TP_FLOOR_NZ) {
    p.pos[0] = at[0]; p.pos[1] = at[1]; p.pos[2] = at[2] + height + TP_MARGIN;
    return p;
  }
  // The upright cylinder's support distance along n: clear of the surface at any angle.
  const float step = radius * sqrtf(n[0] * n[0] + n[1] * n[1]) + height * fabsf(n[2]) + TP_MARGIN;
  for (int k = 0; k < 3; ++k) p.pos[k] = at[k] + n[k] * step;
  p.drop = 1;
  return p;
}

// A wall or ceiling placement stood on the floor below it: hit/at/n from a trace straight down from p.pos
// (TP_DROP_LEN). Other placements pass through unchanged.
inline TpPlace tp_drop(TpPlace p, int hit, const float at[3], const float n[3], float height) {
  if (p.reason != TP_OK || !p.drop) return p;
  if (!hit || !(n[2] > TP_FLOOR_NZ)) {
    p.reason = TP_NO_FLOOR;
    return p;
  }
  p.pos[0] = at[0]; p.pos[1] = at[1]; p.pos[2] = at[2] + height + TP_MARGIN;
  p.drop = 0;
  return p;
}

// The answer: mounted refuses first (as Ghost and dnWarp do), then the cast, then FindSpot's fit.
inline int tp_verdict(int placeReason, int mounted, int fits) {
  if (mounted) return TP_MOUNTED;
  if (placeReason != TP_OK) return placeReason;
  return fits ? TP_OK : TP_NO_ROOM;
}

inline int tp_marker_red(int reason) { return reason != TP_OK; }

// DNMP's landing column: the camera's cyan, barely there; a refusal is a red footprint.
const unsigned TP_MARK_RGBA = 0x402FE0D0u;
const unsigned TP_REFUSED_RGBA = 0x80D8402Fu;   // C_RED at half alpha

// --- The glue (teleport.cpp), game thread, frame path ---------------------------------------------------------
// After freecam_frame: casts while the camera flies (the marker, or a Teleport key press) and acts on the key.
void teleport_frame(void);
// The landing marker from this frame's cast; inside freecam_draw's canvas.
void teleport_draw(int W, int H);

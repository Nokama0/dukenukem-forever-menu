#pragma once
// Mounted collision (docs/re/spawns.md section 8). A pawn whose own collision is 0 or tiny (EnergyLeech_Boss) is hit
// through the collision actors mounted on it (Actor.MountedActorList entries whose MountParent is the pawn and
// bCollideActors is set). Those helpers are its aim points and its ESP box, and their own motion leads a projectile.
#include <string.h>

// MC_MAX helpers kept per pawn out of the first MC_LIST_MAX list entries; MC_PAWNS pawns tracked; MC_VEL_N frames
// averaged for the velocity.
enum { MC_MAX = 16, MC_LIST_MAX = 64, MC_PAWNS = 4, MC_VEL_N = 4 };
const float MC_TINY_UU = 4.f;    // below this radius or height a pawn's own cylinder is no hit shape
const float MC_VEL_GAP_S = 0.25f;   // a longer gap between samples is a pause or a load: the velocity restarts

// The pawn's own collision is 0 or tiny. The walk stores 0 for an unread size (world.cpp), so an unread pawn counts
// as tiny: it only costs a list read, and without helpers it keeps today's behaviour.
inline int mc_tiny(float r, float h) { return r < MC_TINY_UU || h < MC_TINY_UU; }
// A mounted actor counts as a hit shape: bCollideActors set (-1 unread is not), a real cylinder, not a walked pawn.
inline int mc_is_helper(int collide, float r, float h, int isPawn) {
  return collide > 0 && r > 0.f && h > 0.f && !isPawn;
}

// c: Location (the cylinder's centre); taker: its class declares its own TakeDamage (a damage taker or weak point).
struct McHelper { const void* obj; float c[3]; float r, h; int taker, name; };

// Rank: a damage taker over none, then the larger cylinder (r^2 h). 1 a above b, -1 below, 0 equal.
inline int mc_rank_cmp(const McHelper& a, const McHelper& b) {
  if (a.taker != b.taker) return a.taker > b.taker ? 1 : -1;
  const float va = a.r * a.r * a.h, vb = b.r * b.r * b.h;
  return va > vb ? 1 : va < vb ? -1 : 0;
}
inline float mc_dist2(const McHelper& a, const float centre[3]) {
  const float d[3] = { a.c[0] - centre[0], a.c[1] - centre[1], a.c[2] - centre[2] };
  return d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
}

// The aim order: rank first; among equals the one nearest centre (the helpers' box: the middle of the body; the
// EnergyLeech's 14 equal sensors put its static tail base first in the list, 2026-10-10); then the list's order.
// centre null: no tie-break. The first one a trace finds clear is the aim point (aim.cpp's probe).
inline int mc_order(const McHelper* hs, int n, const float* centre, int out[MC_MAX]) {
  if (n > MC_MAX) n = MC_MAX;
  int k = 0;
  for (int i = 0; i < n; i++) {
    int at = k;
    while (at > 0) {
      const McHelper& p = hs[out[at - 1]];
      const int r = mc_rank_cmp(hs[i], p);
      const int before = r > 0 || (r == 0 && centre && mc_dist2(hs[i], centre) < mc_dist2(p, centre));
      if (!before) break;
      out[at] = out[at - 1];
      at--;
    }
    out[at] = i;
    k++;
  }
  return k;
}

// Hysteresis: a visible current helper gives way only to a higher rank, or to an equal one more than 30 % nearer
// centre (squared: 0.49), so the aim point does not hop between neighbouring sensors.
inline int mc_better(const McHelper& cand, const McHelper& cur, const float centre[3]) {
  const int r = mc_rank_cmp(cand, cur);
  if (r) return r > 0;
  return mc_dist2(cand, centre) < 0.49f * mc_dist2(cur, centre);
}

// The box round every helper cylinder: centre and half extents. 0 when there is none.
inline int mc_union(const McHelper* hs, int n, float c[3], float half[3]) {
  if (n <= 0) return 0;
  float mn[3], mx[3];
  for (int i = 0; i < n; i++) {
    const float e[3] = { hs[i].r, hs[i].r, hs[i].h };
    for (int k = 0; k < 3; k++) {
      const float lo = hs[i].c[k] - e[k], hi = hs[i].c[k] + e[k];
      if (!i || lo < mn[k]) mn[k] = lo;
      if (!i || hi > mx[k]) mx[k] = hi;
    }
  }
  for (int k = 0; k < 3; k++) {
    c[k] = (mn[k] + mx[k]) * 0.5f;
    half[k] = (mx[k] - mn[k]) * 0.5f;
  }
  return 1;
}

// A pawn's body this frame (spawnbox_body): its helpers' box, else the render box of a pawn animated away from
// Location, else none (the Location cylinder stays).
enum { MC_SRC_NONE = 0, MC_SRC_BOX = 1, MC_SRC_HELPERS = 2 };
inline int mc_body(int haveHelpers, const float hc[3], const float hh[3], int haveBox, const float bc[3],
                   const float bh[3], float c[3], float half[3]) {
  if (haveHelpers) {
    memcpy(c, hc, 12);
    memcpy(half, hh, 12);
    return MC_SRC_HELPERS;
  }
  if (!haveBox) return MC_SRC_NONE;
  memcpy(c, bc, 12);
  memcpy(half, bh, 12);
  return MC_SRC_BOX;
}

// The chosen helper's velocity: each move of its Location over the time since its last move (the game's tick may be
// slower than our frame, which then sees the same position twice), the mean of the last MC_VEL_N moves. A new helper
// (the aim point changed), one frame's gap over MC_VEL_GAP_S, or no move for longer than that restarts it.
struct McVel { const void* obj; float last[3]; float d[MC_VEL_N][3]; int n, at; float acc; int still; };
// One sample, dt seconds after the last: the moves in the mean (0: no velocity, out zero).
inline int mc_vel_step(McVel* v, const void* obj, const float pos[3], float dt, float out[3]) {
  out[0] = out[1] = out[2] = 0.f;
  if (obj != v->obj || !(dt <= MC_VEL_GAP_S)) {
    v->obj = obj;
    memcpy(v->last, pos, 12);
    v->n = v->at = 0;
    v->acc = 0.f;
    v->still = 0;
    return 0;
  }
  if (dt > 0.f) v->acc += dt;
  const int moved = pos[0] != v->last[0] || pos[1] != v->last[1] || pos[2] != v->last[2];
  if (moved && v->still) {
    // The first move after standing still: the time since the last move spans the pause, so it only re-anchors.
    memcpy(v->last, pos, 12);
    v->acc = 0.f;
    v->still = 0;
  } else if (moved && v->acc > 0.f) {
    for (int k = 0; k < 3; k++) v->d[v->at][k] = (pos[k] - v->last[k]) / v->acc;
    memcpy(v->last, pos, 12);
    v->acc = 0.f;
    v->at = (v->at + 1) % MC_VEL_N;
    if (v->n < MC_VEL_N) v->n++;
  } else if (!moved && v->acc > MC_VEL_GAP_S) {
    v->n = v->at = 0;   // still
    v->still = 1;
  }
  for (int i = 0; i < v->n; i++)
    for (int k = 0; k < 3; k++) out[k] += v->d[i][k] / (float)v->n;
  return v->n;
}

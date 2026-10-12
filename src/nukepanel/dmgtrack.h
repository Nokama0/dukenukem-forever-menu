#pragma once
// Damage numbers without a damage hook (Engine.dll code is CEG-protected): the per-frame Health delta of each
// tracked pawn. A drop is an event, a heal or a first sighting is not, a pawn that reaches 0 or vanishes alive is
// a kill. Pure; dmgnum.cpp feeds it the enemy pawns each frame, plus knockdowns and pawns no longer hostile.

const int DMGTRACK_MAX = 512;   // distinct pawns followed; later ones are ignored until a slot frees

// How a pawn is fed: a hostile pawn; a knockdown (Health 0 in state ControlX: the drop is damage, not a kill); or
// alive but no longer hostile (a tracked one leaves quietly, an untracked one is ignored).
enum { DMGSEEN_ENEMY, DMGSEEN_KNOCKDOWN, DMGSEEN_LEFT };
struct DmgSeen  { const void* who; int health; float x, y, z; int mode; };
struct DmgPt3   { float x, y, z; };
struct DmgDelta { const void* who; int damage; int kill; DmgPt3 at; };

struct DmgTrackEntry { const void* who; int health; float x, y, z; int seen; };
struct DmgTrack { DmgTrackEntry e[DMGTRACK_MAX]; int n; };

inline DmgTrack dmgtrack_init() { DmgTrack t = {}; return t; }

// Kills in one step at or above this are a level unload or teleport clearing the list, not combat.
const int DMGTRACK_BULK_KILLS = 16;
inline int dmg_is_bulk_unload(int kills) { return kills >= DMGTRACK_BULK_KILLS; }

// Newest events win when a step makes more than `cap`: the oldest are dropped, as the display ring's overwrite does.
inline void dmgtrack_emit(DmgDelta* out, int cap, int* n, const DmgDelta& d) {
  if (cap <= 0) return;
  if (*n >= cap) {
    for (int i = 1; i < cap; ++i) out[i - 1] = out[i];
    *n = cap - 1;
  }
  out[(*n)++] = d;
}

inline int dmgtrack_step(DmgTrack* t, const DmgSeen* seen, int nSeen, DmgDelta* out, int cap) {
  if (!t) return 0;
  if (nSeen < 0 || (nSeen > 0 && !seen)) nSeen = 0;
  int n = 0;
  for (int i = 0; i < t->n; ++i) t->e[i].seen = 0;
  const int known = t->n;   // entries added this step cannot be matched twice
  for (int s = 0; s < nSeen; ++s) {
    int at = -1;
    for (int i = 0; i < known; ++i) if (t->e[i].who == seen[s].who && t->e[i].health > 0) { at = i; break; }
    const int mode = seen[s].mode;
    if (at < 0) {
      if (mode == DMGSEEN_ENEMY && seen[s].health > 0 && t->n < DMGTRACK_MAX) {   // first sight: tracked, no event
        DmgTrackEntry& e = t->e[t->n++];
        e.who = seen[s].who; e.health = seen[s].health; e.x = seen[s].x; e.y = seen[s].y; e.z = seen[s].z; e.seen = 1;
      }
      continue;
    }
    DmgTrackEntry& e = t->e[at];
    e.seen = 1;
    if (mode == DMGSEEN_LEFT) { e.health = 0; continue; }   // retired below with no event
    if (seen[s].health < e.health) {
      const DmgDelta d = { e.who, e.health - seen[s].health, seen[s].health <= 0 && mode != DMGSEEN_KNOCKDOWN,
                           { seen[s].x, seen[s].y, seen[s].z } };
      dmgtrack_emit(out, cap, &n, d);
    }
    e.health = seen[s].health;   // a heal raises the baseline; 0 or less retires the entry below
    e.x = seen[s].x; e.y = seen[s].y; e.z = seen[s].z;
  }
  // Retire the dead, and treat a pawn that was not seen at all as a kill of its last health.
  int w = 0;
  for (int i = 0; i < t->n; ++i) {
    DmgTrackEntry& e = t->e[i];
    if (e.seen && e.health > 0) { if (w != i) t->e[w] = e; ++w; continue; }
    if (!e.seen) {
      const DmgDelta d = { e.who, e.health, 1, { e.x, e.y, e.z } };
      dmgtrack_emit(out, cap, &n, d);
    }
  }
  t->n = w;
  return n;
}

// A frame whose pawn walk stopped early lost the tail of the list: stepping on it would read those pawns as
// vanished. Such a frame emits nothing and leaves the tracker exactly as it was.
inline int dmgtrack_step_if(DmgTrack* t, int complete, const DmgSeen* seen, int nSeen, DmgDelta* out, int cap) {
  if (!complete) return 0;
  return dmgtrack_step(t, seen, nSeen, out, cap);
}

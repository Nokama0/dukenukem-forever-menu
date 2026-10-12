#pragma once
// The shared world scan behind the item and breakable ESP rows (items.md 4). Pickups spawn mid-level (drops, crates),
// so one object-table pass runs per level and again every WS_RESCAN_MS. Each row's cache is checked every frame against
// the snapshot slot it was found in; a taken pickup is destroyed and drops out by itself.
#include <string.h>

enum { WS_MAX = 128, WS_RESCAN_MS = 333 };

// When the last pass ran, and for which level (world_level_serial and the player's LevelInfo).
struct WsClock { unsigned serial; const void* level; int built; unsigned lastMs; };
inline void ws_clock_reset(WsClock* c) { memset(c, 0, sizeof *c); }
// A new level: the caches are cleared before anything reads them. No player (a load) is not a move by itself:
// the serial moves once the new level's player exists, and world_frame shows nothing until then.
inline bool ws_level_moved(const WsClock& c, unsigned serial, const void* level) {
  return c.built && (c.serial != serial || (level && c.level != level));
}
// periodic 0: no client wanting a pass needs the rescan (nothing of theirs spawns), so one pass per level.
inline bool ws_scan_due(const WsClock& c, unsigned serial, const void* level, int inLevel, unsigned nowMs,
                        int periodic = 1) {
  if (!inLevel || !level) return false;
  if (!c.built || c.serial != serial || c.level != level) return true;
  return periodic && nowMs - c.lastMs >= (unsigned)WS_RESCAN_MS;
}
inline void ws_scan_done(WsClock* c, unsigned serial, const void* level, unsigned nowMs) {
  c->serial = serial;
  c->level = level;
  c->built = 1;
  c->lastMs = nowMs;
}
// A periodic task's turn: never run, or periodMs since the last run (tick-wrap safe).
inline bool ws_period_due(unsigned lastMs, int ran, unsigned nowMs, unsigned periodMs) {
  return !ran || nowMs - lastMs >= periodMs;
}

// One row's cache, refilled by each pass. overflow counts what the current fill could not hold.
struct WsEntry { const void* obj; int slot, rule; };
struct WsCache { int n, overflow; WsEntry e[WS_MAX]; };
inline void ws_cache_clear(WsCache* c) {
  c->n = 0;
  c->overflow = 0;
}
inline bool ws_cache_add(WsCache* c, const void* obj, int slot, int rule) {
  if (c->n >= WS_MAX) { c->overflow++; return false; }
  WsEntry& e = c->e[c->n++];
  e.obj = obj;
  e.slot = slot;
  e.rule = rule;
  return true;
}
// Drops the entries alive() refuses (freed or reused slots), keeping the order; how many went.
inline int ws_cache_prune(WsCache* c, bool (*alive)(void* ctx, int slot, const void* obj), void* ctx) {
  int k = 0;
  for (int i = 0; i < c->n; ++i)
    if (alive(ctx, c->e[i].slot, c->e[i].obj)) c->e[k++] = c->e[i];
  const int dropped = c->n - k;
  c->n = k;
  return dropped;
}

// --- The glue (worldscan.cpp), game thread ----------------------------------------------------------------
// A client fills `rows` caches from one pass. resolve returns how many of its class names exist (a rise clears its
// cache); class_rule runs under the guard once per class per level; row_of per object (a row, or -1; may edit *rule).
struct WsClient {
  const char* name;
  int rows;
  WsCache* caches;
  const char* const* rowNames;
  int (*wanted)(void);
  int (*resolve)(void);
  int (*class_rule)(const void* cls);
  int (*row_of)(const void* obj, const void* cls, int* rule);
  int oncePerLevel;
  int (*again)(void);
  void (*note)(char* out, int cap);
};
// Once a frame after world_frame (inLevel its result): clears on a level change, passes when due and wanted, and
// prunes every cache against this frame's snapshot.
void worldscan_frame(int inLevel);
// The caches belong to the current level and a pass has filled them.
bool worldscan_ready(void);

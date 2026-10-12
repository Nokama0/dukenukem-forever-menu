#pragma once
// The per-frame world read (game thread = the Present thread): the player, its level, the pawn list and the view,
// all read only through the reflection layer (uobj, uprop, uname). Nothing here writes engine memory.
#include "view.h"

// attitude: the pawn's EAIAttitude toward the player's faction, -1 if none. targetsPlayer: its PrimaryTargetActor is
// the player; target: that actor (any, validated) else null. bossMeter: the HUD's boss-meter actor. knockdown: Health
// reached 0 this frame in state ControlX or as the boss-meter actor (a boss knocked down, not dead: factions.md).
struct WPawn { const void* obj; float x, y, z, radius, height; int health, healthCap; const void* faction;
               int nameIdx; int alive, enemy, ally, neutral, attitude, targetsPlayer, knockdown, bossMeter, downed;
               const void* target; };

// Nothing is read or drawn without a player pawn, its level and a usable view (menus, loads), nor while the game
// is paused (the pause menu draws over the world).
inline bool world_gate(const void* player, const void* level, int viewOk, int paused) {
  return player && level && viewOk && !paused;
}

// Paused: LevelInfo.Pauser (a string, {data, count} with the terminator counted) is non-empty, or LevelInfo.bPaused
// is set. A property that did not resolve never pauses. Which one the pause menu sets is logged (world.cpp).
inline int world_paused(int hasPauser, int pauserCount, int hasBPaused, int bPaused) {
  return (hasPauser && pauserCount > 1) || (hasBPaused && bPaused);
}

// EAIAttitude (Engine.u Actor.EAIAttitude): AIATT_InfiniteHate 0 .. AIATT_Neutral 4 .. AIATT_InfiniteLove 8.
// UAIRelationshipMgr::WeightForAttitude (dnAI.dll!0x1003a790) maps 0..3 to -1000/-3/-2/-1, 4 to 0, 5..8 positive.
enum { WATT_NEUTRAL = 4, WATT_MAX = 8, WREL_CANDIDATES = 16 };
struct WRel { const void* cls; int attitude; };

// FindRelationship (dnAI.dll!0x1003a9c0) over one AIActor's Relationships: the first entry whose class IS the subject's
// faction class; else, of the first 16 entries whose class is an ancestor of it, the most derived (most others among
// its ancestors), the first on a tie. -1: none, which GetAttitudeTowards (0x1003ad50) answers with weight 0, neutral.
inline int world_attitude(const WRel* rels, int n, const void* subject,
                          int (*isa)(void* ctx, const void* cls, const void* ancestor), void* ctx) {
  if (!subject || !rels) return -1;
  int cand[WREL_CANDIDATES], nc = 0;
  for (int i = 0; i < n; i++) {
    if (!rels[i].cls) continue;
    if (rels[i].cls == subject) return rels[i].attitude;
    if (nc < WREL_CANDIDATES && isa(ctx, subject, rels[i].cls)) cand[nc++] = i;
  }
  int best = -1, bestDepth = -1;
  for (int a = 0; a < nc; a++) {
    int depth = 0;
    for (int b = 0; b < nc; b++)
      if (b != a && isa(ctx, rels[cand[a]].cls, rels[cand[b]].cls)) depth++;
    if (depth > bestDepth) { bestDepth = depth; best = cand[a]; }
  }
  return best < 0 ? -1 : rels[best].attitude;
}

// Enemy: alive and hostile (negative weight), or alive and targeting the player (a scripted forced target can make a
// pawn that loves Duke fight him: Map00's Cycloid, factions.md). Ally: alive, friendly, not targeting the player.
// Neutral: neither, drawn with Allies. bossMeter: DukeHUD.BossMeterActor while bDrawBossMeter, the game's boss marker.
inline void world_classify(int alive, int attitude, int targetsPlayer, int bossMeter, int* enemy, int* ally,
                           int* neutral) {
  const bool known = attitude >= 0 && attitude <= WATT_MAX;
  *enemy = alive && ((known && attitude < WATT_NEUTRAL) || targetsPlayer || bossMeter);
  *ally = alive && !*enemy && known && attitude > WATT_NEUTRAL;
  *neutral = alive && !*enemy && !*ally;
}
inline void world_classify(int alive, int attitude, int targetsPlayer, int* enemy, int* ally, int* neutral) {
  world_classify(alive, attitude, targetsPlayer, 0, enemy, ally, neutral);
}

// The watch line (every 600 frames): a pawn whose attitude is not hostile but that has a target (any) or the boss
// meter, so the next override shows what it fights.
inline int world_watch(int attitude, int hasTarget, int bossMeter) {
  const bool hostile = attitude >= 0 && attitude < WATT_NEUTRAL;
  return !hostile && (hasTarget || bossMeter);
}

// Health reached 0 but not dead: state ControlX, or still the boss meter's actor (the Map00 Cycloid falls in state
// AnimX before ControlX: "Health reached 0 in state AnimX").
inline int world_knock_rule(int inControlX, int bossMeter) { return inControlX || bossMeter; }

// The attitude log line: once per (class, attitude, targets player) per level, so a second instance's map override
// and a pawn turning on the player both show. 1 when new (recorded); 0 when seen or the list is full.
struct WAttKey { const void* cls; int attitude, targets; };
const int WATT_LOG_MAX = 64;
inline int world_att_log_new(WAttKey* k, int* n, int cap, const void* cls, int attitude, int targets) {
  for (int i = 0; i < *n; i++)
    if (k[i].cls == cls && k[i].attitude == attitude && k[i].targets == targets) return 0;
  if (*n >= cap) return 0;
  const WAttKey e = { cls, attitude, targets };
  k[(*n)++] = e;
  return 1;
}

// The previous frame's living pawns: a pawn's state is read (one guarded read) only on the frame its Health
// reaches 0, to tell a knockdown from a death. Open addressing, twice the pawn cap; null is never a member.
const int WALIVE_SLOTS = 1024;
struct WAliveSet { const void* k[WALIVE_SLOTS]; };
static_assert(WALIVE_SLOTS == 1024, "walive_slot keeps 10 bits");
inline unsigned walive_slot(const void* p) {
  return ((unsigned)((uintptr_t)p >> 4) * 2654435761u) >> 22;   // the product's top 10 bits: 0..1023
}
inline void walive_clear(WAliveSet* s) { for (int i = 0; i < WALIVE_SLOTS; i++) s->k[i] = 0; }
inline int walive_add(WAliveSet* s, const void* p) {
  if (!p) return 0;
  for (unsigned i = walive_slot(p), n = 0; n < (unsigned)WALIVE_SLOTS; i = (i + 1) & (WALIVE_SLOTS - 1), n++) {
    if (s->k[i] == p) return 1;
    if (!s->k[i]) { s->k[i] = p; return 1; }
  }
  return 0;
}
inline int walive_has(const WAliveSet* s, const void* p) {
  if (!p) return 0;
  for (unsigned i = walive_slot(p), n = 0; n < (unsigned)WALIVE_SLOTS; i = (i + 1) & (WALIVE_SLOTS - 1), n++) {
    if (s->k[i] == p) return 1;
    if (!s->k[i]) return 0;
  }
  return 0;
}
inline int world_knock_probe(int aliveNow, int aliveBefore) { return !aliveNow && aliveBefore; }

// Candidates for the finish-me stun: a pawn hostile while alive whose Health reached 0. The stun keeps Health 0 and the
// state is still AnimX that frame (ControlX comes later), so state is no test; the stun's object is
// AIActor.ControlXParms.MyControl, a dnControl_FinishingMove spawned by CanUseOp_Die. Full: oldest goes.
enum { WDOWN_MAX = 16, WDOWN_GRACE = 30 };
struct WDownSet { const void* p[WDOWN_MAX]; unsigned born[WDOWN_MAX], last[WDOWN_MAX]; unsigned char seen[WDOWN_MAX];
                  int n; };
inline int wdown_index(const WDownSet* d, const void* p) {
  for (int i = 0; p && i < d->n; i++)
    if (d->p[i] == p) return i;
  return -1;
}
inline int wdown_has(const WDownSet* d, const void* p) { return wdown_index(d, p) >= 0; }
inline int wdown_seen(const WDownSet* d, const void* p) { const int i = wdown_index(d, p); return i >= 0 && d->seen[i]; }
inline void wdown_see(WDownSet* d, const void* p) { const int i = wdown_index(d, p); if (i >= 0) d->seen[i] = 1; }
inline void wdown_touch(WDownSet* d, const void* p, unsigned frame) {
  const int i = wdown_index(d, p);
  if (i >= 0) d->last[i] = frame;
}
inline void wdown_remove(WDownSet* d, const void* p) {
  const int i = wdown_index(d, p);
  if (i < 0) return;
  for (int j = i + 1; j < d->n; j++) {
    d->p[j - 1] = d->p[j];
    d->born[j - 1] = d->born[j];
    d->last[j - 1] = d->last[j];
    d->seen[j - 1] = d->seen[j];
  }
  d->n--;
}
inline void wdown_add(WDownSet* d, const void* p, unsigned frame) {
  if (!p || wdown_has(d, p)) return;
  if (d->n == WDOWN_MAX) wdown_remove(d, d->p[0]);
  d->p[d->n] = p;
  d->born[d->n] = frame;
  d->last[d->n] = frame;
  d->seen[d->n] = 0;
  d->n++;
}
// After a complete walk: a candidate not visited on `frame` has left the world.
inline void wdown_prune(WDownSet* d, unsigned frame) {
  for (int i = d->n - 1; i >= 0; i--)
    if (d->last[i] != frame) wdown_remove(d, d->p[i]);
}
// Admission: hostile while alive on the previous walk, Health 0 now.
inline int world_down_admit(int aliveNow, int aliveBefore, int hostileBefore) {
  return world_knock_probe(aliveNow, aliveBefore) && hostileBefore;
}
// Downed now: Health 0 and MyControl is a finishing move. IsControlled is no gate: it turns 1 when the execution
// starts, and the kneeling enemy is still the thing to see until MyControl clears or Health returns.
inline int world_downed(int alive, int finishCtl) { return !alive && finishCtl; }
// Stays a candidate: not revived, and MyControl is a finishing move, or was never seen yet within the grace frames.
inline int world_down_keep(int alive, int finishCtl, unsigned age, int everSeen) {
  return !alive && (finishCtl || (!everSeen && age < (unsigned)WDOWN_GRACE));
}

inline const char* world_attitude_name(int a) {
  static const char* const kNames[WATT_MAX + 1] = { "InfiniteHate", "Despise", "Hate", "Dislike", "Neutral",
                                                    "Like", "Love", "Idolize", "InfiniteLove" };
  if (a == -1) return "none";
  return a >= 0 && a <= WATT_MAX ? kNames[a] : "?";
}

// The player scan's interval while none is found: doubles from 30 to 600 frames; a level or scriptability
// rising resets it to the minimum.
const unsigned WORLD_SCAN_MIN = 30, WORLD_SCAN_MAX = 600;
inline unsigned world_scan_backoff(unsigned interval) {
  if (interval < WORLD_SCAN_MIN) return WORLD_SCAN_MIN;
  return interval >= WORLD_SCAN_MAX / 2 ? WORLD_SCAN_MAX : interval * 2;
}

// Which relationship list answers for a pawn. The runtime list (AIActor.Relations, +0x188C: the one
// FindRelationship walks and AddRelationship appends to) when it resolved with a 12-byte element and has entries;
// else the class defaults (AIActor.Relationships) the runtime list starts as a copy of; else none (neutral).
enum { WREL_SRC_NONE, WREL_SRC_RUNTIME, WREL_SRC_DEFAULTS };
const int WREL_ELEM_SIZE = 12;   // SRelationship: class, attitude byte (padded), float threshold
inline int world_rel_layout_ok(int innerValid, int elemSize) { return innerValid && elemSize == WREL_ELEM_SIZE; }
inline int world_rel_source(int rtOk, int rtCount, int defOk, int defCount) {
  if (rtOk && rtCount > 0) return WREL_SRC_RUNTIME;
  if (defOk && defCount > 0) return WREL_SRC_DEFAULTS;
  return WREL_SRC_NONE;
}

// The effective-FOV log line: at once when r_AspectScale, the back buffer or the FOV source changes; a FOV change
// (a zoom animates it over ~33 frames) logs once it has held for WFOV_STILL frames and differs from the last
// logged value. Zero-initialise before the first step.
const unsigned WFOV_STILL = 30;
struct WFovLog { float scale, fovLogged, fovLast; int w, h, src, init; unsigned stable; };
inline int world_fov_log_step(WFovLog* s, float scale, int w, int h, int src, float fov) {
  if (!s->init || scale != s->scale || w != s->w || h != s->h || src != s->src) {
    s->init = 1;
    s->scale = scale;
    s->w = w;
    s->h = h;
    s->src = src;
    s->fovLogged = s->fovLast = fov;
    s->stable = 0;
    return 1;
  }
  if (fov != s->fovLast) {
    s->fovLast = fov;
    s->stable = 0;
    return 0;
  }
  if (s->stable < WFOV_STILL) s->stable++;
  if (s->stable == WFOV_STILL && fabsf(fov - s->fovLogged) >= 0.05f) {
    s->fovLogged = fov;
    return 1;
  }
  return 0;
}

// The pawn list and every relationship read are skipped while no feature uses them; the player, level, view and
// pause are still tracked, so turning one on draws on the next frame.
inline int world_wants_pawns(int espOn, int dmgOn) { return espOn || dmgOn; }

// readPawns: world_wants_pawns for this frame's settings.
int world_frame(int W, int H, int readPawns);   // 1 when a level and player view exist this frame
int world_pawns(const WPawn** out);       // this frame's pawns (excludes the player), count
int world_pawns_complete(void);         // 0 when this frame's pawn walk stopped before the list end
WView world_view(void);                   // ok is 0 whenever world_frame returned 0
// The player pawn found this frame (also while paused), null without one or its level. Game thread.
void* world_player(void);
// The level reads paused this frame (LevelInfo.Pauser or bPaused), whoever paused it; 0 without a level.
int world_paused_now(void);
// The current level's package (the map) name index, 0 before a level (uname_str gives the text).
int world_map_name(void);
// LevelInfo.MapFileName of the current level into out (ASCII, "" when unresolved or unreadable). Game thread.
void world_level_map_file(char* out, int cap);
// The level identity the player options re-apply on: changes with each new (LevelInfo, player) pair, so a level
// load, a reload or a new pawn all count; finding the same pair again after a drop does not.
unsigned world_level_serial(void);

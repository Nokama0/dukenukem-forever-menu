#pragma once
// Drop-in enemies and the ESP box (spawns.md 5). A hostile pawn whose rendered body (RenderActor.GetRenderBoundingBox)
// is more than SPAWNBOX_FAR_UU from its Location (an AnimX drop) is flagged; the ESP box and damage-number anchor use
// that body while it stays away. A pawn the hostility filter skips while dropping (PHYS_Falling or mounted) is logged.
#include <math.h>
#include <string.h>
#include "mountcol.h"

// At most SPAWNBOX_CALLS_PER_FRAME box calls a frame, round-robin over the hostile pawns.
enum { SPAWNBOX_FAR_UU = 64, SPAWNBOX_LINES_PER_LEVEL = 8, SPAWNBOX_KEYS = 32, SPAWNBOX_CALLS_PER_FRAME = 8 };
enum { SB_PHYS_FALLING = 2 };          // Object.EPhysics PHYS_Falling: a launched egg-pod Pregnator (spawns.md 2d)
enum { SB_KIND_FAR = 0, SB_KIND_SKIPPED = 1 };

// The box's centre against Location: *dist the distance, *dz the height difference. 0 when the box is unusable:
// inverted, NaN, or the all-zero box the native leaves without a primitive.
inline int spawnbox_offset(const float mn[3], const float mx[3], const float loc[3], float* dist, float* dz) {
  *dist = *dz = 0.f;
  int zero = 1;
  for (int i = 0; i < 3; i++) {
    if (!(mn[i] <= mx[i])) return 0;   // NaN compares false
    if (mn[i] != 0.f || mx[i] != 0.f) zero = 0;
  }
  if (zero) return 0;
  float c[3];
  for (int i = 0; i < 3; i++) c[i] = (mn[i] + mx[i]) * 0.5f - loc[i];
  *dist = sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
  *dz = c[2];
  return *dist == *dist;
}
inline int spawnbox_far(float dist) { return dist > (float)SPAWNBOX_FAR_UU; }   // NaN compares false
// A pawn the hostility filter leaves out (alive, not an enemy) while it drops: falling, or mounted (a dropship rider).
inline int spawnbox_skipped(int alive, int enemy, int phys, int mounted) {
  return alive && !enemy && (phys == SB_PHYS_FALLING || mounted);
}

// The rate limit: one line per (pawn, state, kind) a level, SPAWNBOX_LINES_PER_LEVEL lines at most. 1: log it.
struct SbKey { const void* pawn; int state, kind; };
struct SbLog { SbKey k[SPAWNBOX_KEYS]; int n, lines; };
inline int spawnbox_log_new(SbLog* l, const void* pawn, int state, int kind) {
  if (l->lines >= SPAWNBOX_LINES_PER_LEVEL) return 0;
  for (int i = 0; i < l->n; i++)
    if (l->k[i].pawn == pawn && l->k[i].state == state && l->k[i].kind == kind) return 0;
  if (l->n < SPAWNBOX_KEYS) {
    const SbKey e = { pawn, state, kind };
    l->k[l->n++] = e;
  }
  l->lines++;
  return 1;
}

// The flags (spawns.md section 5 step 2): a pawn the round-robin finds far is read every frame while its body stays
// more than SPAWNBOX_FAR_UU from Location, SPAWNBOX_FLAG_MS at most, SPAWNBOX_FLAGS pawns at most; its ESP box and
// damage-number anchor come from the render bounds meanwhile. c, h: this frame's box centre and half extents.
enum { SPAWNBOX_FLAGS = 8 };
const unsigned SPAWNBOX_FLAG_MS = 5000;
struct SbFlag { const void* pawn; unsigned since, frame; float c[3], h[3]; };
struct SbFlags { SbFlag f[SPAWNBOX_FLAGS]; int n; };
inline int spawnbox_flag_find(const SbFlags* s, const void* pawn) {
  for (int i = 0; pawn && i < s->n; i++)
    if (s->f[i].pawn == pawn) return i;
  return -1;
}
// The pawn's flag, set at now if new; -1 when all SPAWNBOX_FLAGS are taken.
inline int spawnbox_flag_set(SbFlags* s, const void* pawn, unsigned now) {
  const int i = spawnbox_flag_find(s, pawn);
  if (i >= 0) return i;
  if (!pawn || s->n >= SPAWNBOX_FLAGS) return -1;
  SbFlag& f = s->f[s->n];
  f.pawn = pawn;
  f.since = now;
  f.frame = 0;
  return s->n++;
}
// After this frame's read: kept while the box is usable and far, and younger than SPAWNBOX_FLAG_MS.
inline int spawnbox_flag_keep(const SbFlag& f, int usable, int isFar, unsigned now) {
  return usable && isFar && now - f.since < SPAWNBOX_FLAG_MS;   // unsigned: safe across the wrap
}
inline void spawnbox_flag_drop(SbFlags* s, int i) {
  if (i < 0 || i >= s->n) return;
  s->f[i] = s->f[--s->n];
}

// Cut-scene pawns (AIActor.bAnimCutScene: a trooper in an animation was locked and fired on), noted
// where a box is read (round-robin and flagged); an entry not read again within SPAWNBOX_CUT_MS no longer counts.
enum { SPAWNBOX_CUTS = 32 };
const unsigned SPAWNBOX_CUT_MS = 1000;
struct SbCut { const void* pawn; unsigned stamp; };
struct SbCuts { SbCut c[SPAWNBOX_CUTS]; int n; };
// cut: set or refreshed (full: the oldest entry replaced); else removed.
inline void spawnbox_cut_note(SbCuts* s, const void* pawn, int cut, unsigned now) {
  if (!pawn) return;
  int at = -1;
  for (int i = 0; i < s->n && at < 0; i++)
    if (s->c[i].pawn == pawn) at = i;
  if (!cut) {
    if (at >= 0) s->c[at] = s->c[--s->n];
    return;
  }
  if (at < 0 && s->n < SPAWNBOX_CUTS) at = s->n++;
  if (at < 0) {
    at = 0;
    for (int i = 1; i < s->n; i++)
      if (now - s->c[i].stamp > now - s->c[at].stamp) at = i;   // unsigned ages: safe across the wrap
  }
  s->c[at].pawn = pawn;
  s->c[at].stamp = now;
}
inline int spawnbox_cut_has(const SbCuts* s, const void* pawn, unsigned now) {
  for (int i = 0; pawn && i < s->n; i++)
    if (s->c[i].pawn == pawn) return now - s->c[i].stamp < SPAWNBOX_CUT_MS;
  return 0;
}

// AI teleports (docs/re/spawns.md section 6): AIActor.CurrentOp == AIOP_Teleport, the enemy still arriving or leaving.
// 138 is the index of AIOP_Teleport in BaseAI.EAIOp (Engine.u, 151 names, read 2026-10-09 with tools/ubc.py); the
// reflection layer reads no enum names, and 137 (AIOP_TakeOff) is a flight, not a teleport. Signals are a bitmask.
enum { SB_AIOP_TELEPORT = 138 };
enum { SB_TELE_OP = 1, SB_TELE_FLAG = 2, SB_TELE_HIDDEN = 4, SB_TELE_NODAMAGE = 8 };
enum { SB_TELE_MOVING = SB_TELE_OP | SB_TELE_FLAG | SB_TELE_HIDDEN };
// RenderActor.SetHealth (Engine.dll 0x1007F7B0) clamps Health to [HealthMin, HealthCap], and TriggerAssign can raise
// HealthMin: at or under its floor a pawn takes hits and loses nothing (the scripted Captain pair, Map04, 2026-10-09).
// Counted as no-damage (SB_TELE_NODAMAGE).
inline int spawnbox_health_floored(float health, float healthMin) { return healthMin > 0.f && health <= healthMin; }   // the bits that mean mid-teleport
// op: CurrentOp (-1 unreadable); teleporting, hidden: bTeleporting, bHidden (-1 unreadable counts as clear).
// nodamage: RenderActor.bNoDamage, the general takes-no-damage flag (the Captain sets it on op 138).
inline int spawnbox_tele_signals(int op, int teleporting, int hidden, int nodamage = 0) {
  return (op == SB_AIOP_TELEPORT ? SB_TELE_OP : 0) | (teleporting > 0 ? SB_TELE_FLAG : 0) |
         (hidden > 0 ? SB_TELE_HIDDEN : 0) | (nodamage > 0 ? SB_TELE_NODAMAGE : 0);
}
// Pawn.TakeDamage returns at once when Role < ROLE_Authority (4) (Engine.u script dump, 2026-10-09): such a pawn takes
// no damage from anyone. role: Actor.Role, -1 unreadable. Counted as no-damage (SB_TELE_NODAMAGE).
inline int spawnbox_role_blocks(int role) { return role >= 0 && role < 4; }
// Pawn.ModifyHealthActivity clamps a loss to 0 while CurrentlyInvincible() (AIActor: Invulnerable, else bGodMode,
// bGhostMode, bNoDamage) or bProtectedEgoRecharge: the hit registers (DamageInfo, ThisFrameDamage) but Health keeps
// (the scripted Captain pair, Map04, measured 2026-10-09). Each -1 (unreadable) counts as clear.
inline int spawnbox_health_locked(int invulnerable, int godMode, int ghostMode, int protectedEgo) {
  return invulnerable > 0 || godMode > 0 || ghostMode > 0 || protectedEgo > 0;
}
// The per-shot damage-path probe's caps: lines a level and a pawn. One slot per pawn logged, so the table is the level cap.
enum { SPAWNBOX_PROBE_PER_LEVEL = 24, SPAWNBOX_PROBE_PER_PAWN = 6 };
struct SbProbeCap { const void* pawn[SPAWNBOX_PROBE_PER_LEVEL]; int n[SPAWNBOX_PROBE_PER_LEVEL]; int total, pawns; };
// 1: log this shot for pawn (counted).
inline int spawnbox_probe_take(SbProbeCap* c, const void* pawn) {
  if (!pawn || c->total >= SPAWNBOX_PROBE_PER_LEVEL) return 0;
  int at = -1;
  for (int i = 0; i < c->pawns && at < 0; i++)
    if (c->pawn[i] == pawn) at = i;
  if (at < 0) {
    at = c->pawns++;
    c->pawn[at] = pawn;
    c->n[at] = 0;
  }
  if (c->n[at] >= SPAWNBOX_PROBE_PER_PAWN) return 0;
  c->n[at]++;
  c->total++;
  return 1;
}
// The teleporting pawns, read every frame; an entry not read again within SPAWNBOX_CUT_MS no longer counts.
struct SbTele { const void* pawn; unsigned since, stamp; int sig; };
struct SbTeles { SbTele t[SPAWNBOX_CUTS]; int n; };
enum { SB_TELE_EV_NONE = 0, SB_TELE_EV_START = 1, SB_TELE_EV_END = 2 };
// sig 0: removed (END, *heldMs how long it was held). Else set or refreshed (START when new). A full table takes a
// new pawn only over a stale entry.
inline int spawnbox_tele_note(SbTeles* s, const void* pawn, int sig, unsigned now, unsigned* heldMs) {
  if (!pawn) return SB_TELE_EV_NONE;
  int at = -1;
  for (int i = 0; i < s->n && at < 0; i++)
    if (s->t[i].pawn == pawn) at = i;
  if (!sig) {
    if (at < 0) return SB_TELE_EV_NONE;
    *heldMs = s->t[at].stamp - s->t[at].since;
    s->t[at] = s->t[--s->n];
    return SB_TELE_EV_END;
  }
  int ev = SB_TELE_EV_NONE;
  if (at < 0) {
    if (s->n < SPAWNBOX_CUTS) at = s->n++;
    else
      for (int i = 0; i < s->n && at < 0; i++)
        if (now - s->t[i].stamp >= SPAWNBOX_CUT_MS) at = i;   // unsigned ages: safe across the wrap
    if (at < 0) return SB_TELE_EV_NONE;
    s->t[at].pawn = pawn;
    s->t[at].since = now;
    ev = SB_TELE_EV_START;
  }
  s->t[at].stamp = now;
  s->t[at].sig = sig;
  return ev;
}
// The pawn's signal bits while its entry is fresh, else 0.
inline int spawnbox_tele_sig(const SbTeles* s, const void* pawn, unsigned now) {
  for (int i = 0; pawn && i < s->n; i++)
    if (s->t[i].pawn == pawn) return now - s->t[i].stamp < SPAWNBOX_CUT_MS ? s->t[i].sig : 0;
  return 0;
}
inline int spawnbox_tele_has(const SbTeles* s, const void* pawn, unsigned now) {
  return spawnbox_tele_sig(s, pawn, now) != 0;
}

// The watch (docs/re/spawns.md section 6): what is true of a hostile pawn from first sight until SPAWNBOX_WATCH_MS after
// its last teleport/no-damage exclusion (or after first sight if none), one line per change of the tuple.
enum { SPAWNBOX_WATCHES = 32, SPAWNBOX_WATCH_LINES_PER_LEVEL = 60, SPAWNBOX_WATCH_LINES_PER_PAWN = 12 };
const unsigned SPAWNBOX_WATCH_MS = 6000;
// dscale: SB_DS_*; neutral: the zone's bNeutralZone (-1 unread); absorbers: Pawn.DamageAbsorbers count (-1 unread).
struct SbTuple { int state, op, nodamage, hidden, teleporting, shield, health, dscale, neutral, absorbers, expanding, collide, projtarget, role; };
struct SbWatch { const void* pawn; unsigned first, lastExcl, stamp; int lines; SbTuple last; };
struct SbWatches { SbWatch w[SPAWNBOX_WATCHES]; int n, lines; };
inline int spawnbox_tuple_same(const SbTuple& a, const SbTuple& b) {
  return a.state == b.state && a.op == b.op && a.nodamage == b.nodamage && a.hidden == b.hidden &&
         a.teleporting == b.teleporting && a.shield == b.shield && a.health == b.health && a.dscale == b.dscale &&
         a.neutral == b.neutral && a.absorbers == b.absorbers && a.expanding == b.expanding && a.collide == b.collide &&
         a.projtarget == b.projtarget && a.role == b.role;
}
// The pawn's watch index (a new pawn is added, *isNew set), or -1 when its window has closed or the table holds only
// live watches. excluded: it is teleporting or takes no damage now, which reopens the window.
inline int spawnbox_watch_begin(SbWatches* s, const void* pawn, unsigned now, int excluded, int* isNew) {
  *isNew = 0;
  if (!pawn) return -1;
  int at = -1;
  for (int i = 0; i < s->n && at < 0; i++)
    if (s->w[i].pawn == pawn) at = i;
  if (at < 0) {
    if (s->n < SPAWNBOX_WATCHES) at = s->n++;
    else
      for (int i = 0; i < s->n && at < 0; i++)
        if (now - s->w[i].stamp >= SPAWNBOX_WATCH_MS) at = i;   // not seen for a window: gone; unsigned-safe
    if (at < 0) return -1;
    memset(&s->w[at], 0, sizeof s->w[at]);
    s->w[at].pawn = pawn;
    s->w[at].first = s->w[at].lastExcl = now;
    *isNew = 1;
  }
  SbWatch& w = s->w[at];
  w.stamp = now;
  if (excluded) w.lastExcl = now;
  return now - w.lastExcl < SPAWNBOX_WATCH_MS ? at : -1;
}
// The tuple read this frame: 1 when a line is due (the first tuple, or a change) within the caps; *sinceMs the time
// since first sight. The tuple is remembered even when a cap holds the line back.
inline int spawnbox_watch_change(SbWatches* s, int at, int isNew, const SbTuple& t, unsigned now, unsigned* sinceMs) {
  SbWatch& w = s->w[at];
  *sinceMs = now - w.first;
  if (!isNew && spawnbox_tuple_same(w.last, t)) return 0;
  w.last = t;
  if (w.lines >= SPAWNBOX_WATCH_LINES_PER_PAWN || s->lines >= SPAWNBOX_WATCH_LINES_PER_LEVEL) return 0;
  w.lines++;
  s->lines++;
  return 1;
}

// Damage the game nullifies for the player (docs/re/spawns.md section 6): AIActor.DamageScaleInfo walked as AIScaleDamage
// does, and GameInfo.ModifyDamage's zero in a bNeutralZone. classMatch: QualifierClass is None or a class the player is.
enum { SB_DS_UNREAD = -1, SB_DS_OK = 0, SB_DS_ZERO = 1, SB_DS_UNKNOWN = 2, SB_DS_TYPE_ZERO = 3, SB_DS_MAX_ENTRIES = 64 };
// typeKnown: inTypes/inNot hold PassDamageType for the held weapon's damage type (0: the type is unknown).
struct SbDsEntry { int classMatch, nTypes, nNotTypes; float scale; int typeKnown, inTypes, inNot; };
// AAIActor::PassDamageType (dnAI.dll!0x10024680): true when the damage type or one of its supers is a (non-null)
// element of the list. chain: the type class then its supers (GetSuperStruct).
inline int spawnbox_pass_type(const void* const* chain, int nChain, const void* const* list, int nList) {
  for (int j = nList - 1; j >= 0; j--) {
    if (!list[j]) continue;
    for (int k = 0; k < nChain; k++)
      if (chain[k] == list[j]) return 1;
  }
  return 0;
}
// AIScaleDamage's walk: the last entry matching the player's class and the weapon's type decides. A typed entry
// with the type unknown stops the walk as unknown; once a typed entry was walked the answer holds for this weapon
// only, so a zero is SB_DS_TYPE_ZERO. No match: the player is not scaled.
inline int spawnbox_dscale_verdict(const SbDsEntry* e, int n) {
  int typed = 0;
  for (int i = n - 1; i >= 0; i--) {
    if (!e[i].classMatch) continue;
    if (e[i].nTypes > 0 || e[i].nNotTypes > 0) {
      if (!e[i].typeKnown) return SB_DS_UNKNOWN;
      typed = 1;
      if ((e[i].nTypes > 0 && !e[i].inTypes) || (e[i].nNotTypes > 0 && e[i].inNot)) continue;
    }
    if (!(e[i].scale <= 0.f)) return SB_DS_OK;   // NaN: damageable
    return typed ? SB_DS_TYPE_ZERO : SB_DS_ZERO;
  }
  return SB_DS_OK;
}
// A weapon-specific zero excludes only while the weapon it was read with is held (curType: its damage type now).
inline int spawnbox_dmg_type_excludes(int verdict, const void* readType, const void* curType) {
  return verdict == SB_DS_TYPE_ZERO && curType && readType == curType;
}
// Each hostile pawn's verdict, read when first seen and every SPAWNBOX_DMG_REREAD frames (designer data, rarely flips).
enum { SPAWNBOX_DMG_REREAD = 30 };
struct SbDmg { const void* pawn; unsigned stamp, frame; int verdict, neutral; const void* wtype; };
struct SbDmgs { SbDmg d[SPAWNBOX_CUTS]; int n; };
// The pawn's slot (new, unread: verdict SB_DS_UNREAD), *due set when it is new, SPAWNBOX_DMG_REREAD frames old, or read
// with another weapon damage type than wtype; -1 when the table holds only fresh entries.
inline int spawnbox_dmg_slot(SbDmgs* s, const void* pawn, unsigned now, unsigned frameNo, int* due,
                             const void* wtype = 0) {
  *due = 0;
  if (!pawn) return -1;
  int at = -1;
  for (int i = 0; i < s->n && at < 0; i++)
    if (s->d[i].pawn == pawn) at = i;
  if (at < 0) {
    if (s->n < SPAWNBOX_CUTS) at = s->n++;
    else
      for (int i = 0; i < s->n && at < 0; i++)
        if (now - s->d[i].stamp >= SPAWNBOX_CUT_MS) at = i;   // unsigned ages: safe across the wrap
    if (at < 0) return -1;
    s->d[at].pawn = pawn;
    s->d[at].verdict = SB_DS_UNREAD;
    s->d[at].neutral = -1;
    s->d[at].frame = frameNo;
    *due = 1;
  } else if (frameNo - s->d[at].frame >= (unsigned)SPAWNBOX_DMG_REREAD || s->d[at].wtype != wtype) {
    s->d[at].frame = frameNo;
    *due = 1;
  }
  s->d[at].wtype = wtype;   // the verdict read now is for this weapon's type
  s->d[at].stamp = now;
  return at;
}
// 1 with the pawn's verdict, neutral flag and the weapon damage type it was read with, while its entry is fresh.
inline int spawnbox_dmg_get(const SbDmgs* s, const void* pawn, unsigned now, int* verdict, int* neutral,
                            const void** wtype = 0) {
  for (int i = 0; pawn && i < s->n; i++)
    if (s->d[i].pawn == pawn) {
      if (now - s->d[i].stamp >= SPAWNBOX_CUT_MS) return 0;
      *verdict = s->d[i].verdict;
      *neutral = s->d[i].neutral;
      if (wtype) *wtype = s->d[i].wtype;
      return 1;
    }
  return 0;
}

// Not a target for Silent aim, the aimbot or the triggerbot: animated away from Location (flagged) with no render box
// to aim at, teleporting, or one the game will not let the player damage, or doomed by the player's projectiles in
// flight (overkill.h). A cut-scene (bAnimCutScene) never excludes (Target if damageable); 2 unused.
enum { SB_EXCL_NONE = 0, SB_EXCL_FAR = 1, SB_EXCL_TELEPORT = 3, SB_EXCL_NODAMAGE = 4,
       SB_EXCL_PLAYERDMG0 = 5, SB_EXCL_NEUTRAL = 6, SB_EXCL_DOOMED = 7, SB_EXCL_WRONGTYPE = 8, SB_EXCL_EXPANDED = 9 };
// nodamage (bNoDamage) comes after teleporting; then DamageScaleInfo scale 0, the neutral zone, a zero for the held
// weapon's damage type (wrongType), expanded (bloat.h), and last doomed: passing reasons last, so any lasting one is
// named first. hasBody: the flagged pawn's render box was read this frame (spawnbox_body), so the aim goes to it.
inline int spawnbox_exclusion(int flagged, int cutscene, int teleporting, int nodamage = 0, int dmg0 = 0, int neutral = 0,
                              int doomed = 0, int wrongType = 0, int hasBody = 0, int expanded = 0) {
  (void)cutscene;   // kept as an input so the rule states it: the visibility trace and the exact rules decide
  return flagged && !hasBody ? SB_EXCL_FAR : teleporting ? SB_EXCL_TELEPORT
         : nodamage ? SB_EXCL_NODAMAGE : dmg0 ? SB_EXCL_PLAYERDMG0 : neutral ? SB_EXCL_NEUTRAL
         : wrongType ? SB_EXCL_WRONGTYPE : expanded ? SB_EXCL_EXPANDED : doomed ? SB_EXCL_DOOMED : SB_EXCL_NONE;
}

// --- The glue (spawnbox.cpp), game thread only ---------------------------------------------------------------------
// Once a frame after world_frame, before the targeting (aim_frame), the ESP and the damage numbers; on: a level is up
// and one of them is on.
void spawnbox_frame(int on);
// A pawn's body this frame (mc_body): the box round its mounted collision (mountcol.h), else a flagged pawn's render
// box; 1 with its centre and half extents, 0 to keep the Location box.
int spawnbox_body(const void* pawn, float centre[3], float half[3]);
// The pawn's mounted collision this frame, in aim order (mc_order): the count, 0 when it has none. centre: the helpers'
// box centre (mc_better's middle); chosen: the probe's current helper's index, -1 none.
int spawnbox_helpers(const void* pawn, McHelper out[MC_MAX], float centre[3] = 0, int* chosen = 0);
// An aim pick, aimbot lock or FireDown on pawn (event names it): with its mounted collision, one line (capped a level)
// with the chosen helper, its bone, place and lead velocity; lock: 10 more once-a-second snapshots.
void spawnbox_helper_event(const void* pawn, const char* event, int lock);
// aim.cpp's probe found this helper clear: the aim point, and the one whose motion leads a projectile.
void spawnbox_helper_choose(const void* pawn, const void* helper);
// The pawn's aim point among its helpers: the chosen one, else the first in aim order. 0 when it has none.
int spawnbox_helper_point(const void* pawn, float out[3]);
// The chosen helper's velocity (mc_vel_step; zero until two frames are seen). 0 when the pawn has no helpers: the
// caller reads the pawn's own Velocity.
int spawnbox_helper_vel(const void* pawn, float out[3]);
// Any pawn's render box now (one GetRenderBoundingBox call), flagged or not: 1 with its centre and half extents, 0 when
// the call is refused or the box is unusable. For aim.cpp's point-geometry log only.
int spawnbox_render_box(const void* pawn, float centre[3], float half[3]);
// Why the pawn is no target now (SB_EXCL_*): flagged far without a box this frame, teleporting, bNoDamage, ... or
// doomed (overkill_doomed). Reads no engine memory; game thread (aim.cpp's ProcessEvent handler too).
int spawnbox_excluded(const void* pawn);
// The Expander's read of pawn (bloat.h): 1 with CanBloat, BloatState and SecondBloat (-1: its class declares none) while
// the held gun deals ExpanderDamage and the pawn was read within BLOAT_MS. Reads no engine memory.
int spawnbox_bloat(const void* pawn, int* can, int* state, int* second);
// The pawn read as in a cut-scene (bAnimCutScene) within the last SPAWNBOX_CUT_MS: for the counters and the log only.
int spawnbox_in_cutscene(const void* pawn);
// aim.cpp picked pawn: logs it (SPAWNBOX_LINES_PER_LEVEL a level, once per pawn, state and box use) when it is in a
// cut-scene or animated away, with its state, the box's offset from Location and what the aim used (MC_SRC_*).
void spawnbox_log_pick(const void* pawn, int usedBox);
// tbot.cpp, once a frame while it holds fire on pawn: shots is PlayerPawn.AccuracyShots. Only logs the per-shot probe
// line (no targeting effect).
void spawnbox_probe_feed(const void* pawn, int shots);
// aim.cpp's bullet-flag trace for target ended at hit (class name index hitCls, 0 for no hit; self: hit is the target
// itself). Kept per target for the per-shot probe line.
void spawnbox_note_hit(const void* target, int hitCls, int self);
// Reads the pawn's collision flags by name and logs them (capped per level), for a shot trace that hit nothing before
// body point `point` (aim.cpp). The numbers are -1 for a flag that does not resolve.
void spawnbox_log_nohit(const void* pawn, const char* point);

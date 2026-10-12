#pragma once
// Overkill (docs/re/aim.md section 7): a target the player's projectiles in flight will already kill is "doomed", and
// while it is, the aim does not pick it and the triggerbot does not fire at it. Only projectile guns: a hitscan shot
// lands at once, so nothing is in flight. The rules are pure for the suite; overkill.cpp reads the engine.
#include "aim.h"   // AIMW_*

enum { OK_PROJ_MAX = 32, OK_MARKS = 16, OK_SHOTS = 8, OK_LOGS_PER_LEVEL = 8 };
// OK_SCAN_MS: the object-table pass while armed (about 1 ms each); OK_CAP_MS: a projectile older than this no longer
// counts (a rocket that flew past keeps no mark); OK_SHOT_MS: a fired shot's lock waits this long for its projectile.
const unsigned OK_SCAN_MS = 100, OK_CAP_MS = 2000, OK_SHOT_MS = 1000;

inline int ok_in_scope(int kind) { return kind == AIMW_PROJECTILE; }
// A mark counts only while a projectile gun is up: switched to a hitscan gun, the target is shot as before.
inline int ok_applies(int kind, int doomed) { return ok_in_scope(kind) && doomed; }

// dnSinglePlayer.IssuedDamageScaler (dnGame.u): by GameInfo.Difficulty 0..2, anything higher 0.5; -1 unread.
inline float ok_issued_scale(int difficulty) {
  if (difficulty < 0) return 1.f;
  return difficulty == 0 ? 1.25f : difficulty == 1 ? 1.f : difficulty == 2 ? 0.75f : 0.5f;
}
// The damage the game deals for a projectile's Damage: GameInfo.ModifyDamage multiplies by InstigatedBy.DamageScaling,
// and dnSinglePlayer.ModifyDamage by IssuedDamageScaler only when the instigator is the player pawn and the damage type
// does not set bIgnoreDifficultyDamageScaling. Any other instigator (launcher grenade may have none) is unscaled here.
inline float ok_damage_scale(int instIsPlayer, float damageScaling, int singlePlayer, int ignoreDifficulty,
                             int difficulty) {
  if (!instIsPlayer) return 1.f;
  float s = damageScaling > 0.f && damageScaling < 1000.f ? damageScaling : 1.f;   // unread or NaN: 1
  if (singlePlayer && ignoreDifficulty <= 0) s *= ok_issued_scale(difficulty);
  return s;
}
// The instance's Damage (the launcher writes GrenadeDamage there after the spawn), else its class default.
inline float ok_proj_damage(float live, float def) { return live > 0.f ? live : def > 0.f ? def : 0.f; }
// Splash counts as the full damage of a direct hit.
inline int ok_doomed(float expected, float health) { return health > 0.f && expected >= health; }   // NaN: no
// The Expander's blasts deal a pawn no damage in single player (bloat.h); any other target (a catfish: DoDamage on a
// Biology_Generic) takes their Damage.
inline float ok_proj_counts_dmg(float dmg, int bloat, int onPawn) { return bloat && onPawn ? 0.f : dmg; }
// A pawn with Expander blasts on their way is doomed by the other projectiles' damage, or by the blasts when they
// complete the pop (done, from its bloat state read): then its own Health stands in. Unread state: the damage alone.
inline float ok_bloat_expected(float damage, int bloatShots, int read, int done, float health) {
  return bloatShots && read && done && health > damage ? health : damage;
}
// The player's projectile: Instigator is the player (Weapon.ProjectileFire's Spawn takes the weapon's), or Owner is
// the player, its weapon, or the mounted gun (the launcher spawns with Owner = the turret).
inline int ok_is_mine(const void* inst, const void* owner, const void* player, const void* weapon, const void* gun) {
  if (player && inst == player) return 1;
  return owner && (owner == player || owner == weapon || owner == gun);
}
// The target a shot was fired at: the triggerbot's, else the aimbot's lock, else the aim's pick.
inline const void* ok_lock(const void* tbotT, const void* aimbotT, const void* aimT) {
  return tbotT ? tbotT : aimbotT ? aimbotT : aimT;
}

// Shots fired and not yet matched to a projectile, oldest first.
struct OkShot { const void* target; unsigned ms; };
struct OkShots { OkShot s[OK_SHOTS]; int n; };
inline void ok_shot_push(OkShots* q, const void* target, unsigned now) {
  if (q->n >= OK_SHOTS) {
    for (int i = 1; i < q->n; i++) q->s[i - 1] = q->s[i];
    q->n--;
  }
  q->s[q->n].target = target;
  q->s[q->n].ms = now;
  q->n++;
}
// The oldest shot younger than OK_SHOT_MS (older ones are dropped): 1 with its target (which may be null).
inline int ok_shot_take(OkShots* q, unsigned now, const void** target) {
  while (q->n > 0) {
    const OkShot s = q->s[0];
    for (int i = 1; i < q->n; i++) q->s[i - 1] = q->s[i];
    q->n--;
    if (now - s.ms < OK_SHOT_MS) {   // unsigned: safe across the wrap
      *target = s.target;
      return 1;
    }
  }
  return 0;
}

// A projectile counts toward its target for OK_CAP_MS after it appeared.
inline int ok_young(unsigned firstMs, unsigned now) { return now - firstMs < OK_CAP_MS; }
// The mark's events. DEAD: the target died; the rest clear it with the target alive (a miss): GONE every projectile
// is gone, CAP the rest are past OK_CAP_MS, SHORT the young ones left fall short of Health.
// LANDED: an Expander mark cleared because its blasts landed and bloated or popped the pawn (a hit, not a miss).
enum { OK_EV_NONE, OK_EV_MARK, OK_EV_DEAD, OK_EV_GONE, OK_EV_CAP, OK_EV_SHORT, OK_EV_LANDED };
// steps: the pawn's bloat steps when an Expander mark was set (bloat_steps), -1 for a damage mark.
struct OkMark { const void* target; int doomed; unsigned since; int steps; };
// A cleared Expander mark whose pawn has gained a bloat step since it was set: the blast landed. stepsNow -1 unread.
inline int ok_bloat_landed(int ev, int stepsAtMark, int stepsNow) {
  return (ev == OK_EV_GONE || ev == OK_EV_CAP || ev == OK_EV_SHORT) && stepsAtMark >= 0 && stepsNow > stepsAtMark ?
         OK_EV_LANDED : ev;
}
// Once a frame per target: expectedYoung sums the young projectiles' damage; nYoung, nOld count the live ones.
inline int ok_mark_step(OkMark* m, int alive, float health, float expectedYoung, int nYoung, int nOld, unsigned now) {
  if (!alive) {
    if (!m->doomed) return OK_EV_NONE;
    m->doomed = 0;
    return OK_EV_DEAD;
  }
  const int d = ok_doomed(expectedYoung, health);
  if (d && !m->doomed) {
    m->doomed = 1;
    m->since = now;
    return OK_EV_MARK;
  }
  if (d || !m->doomed) return OK_EV_NONE;
  m->doomed = 0;
  return nYoung + nOld == 0 ? OK_EV_GONE : nYoung == 0 ? OK_EV_CAP : OK_EV_SHORT;
}
// Activity within OK_CAP_MS (a shot seen, our fire press held, projectiles tracked); seen 0: none yet.
inline int ok_active(int seen, unsigned lastMs, unsigned now) { return seen && now - lastMs < OK_CAP_MS; }
// The object-table pass while armed: at once on a shot, else every OK_SCAN_MS while active (idle, a pass finds
// nothing new; the press keeps it going in case a turret's shots are not seen).
inline int ok_scan_due(int armed, int shotPending, int ran, unsigned lastMs, unsigned now, int active) {
  return armed && (shotPending || (active && (!ran || now - lastMs >= OK_SCAN_MS)));
}

// --- The glue (overkill.cpp), game thread only ------------------------------------------------------------------------------
// Once a frame after spawnbox_frame and before aim_frame: shots fired, the player's projectiles and the marks. The aim
// inputs it reads are last frame's (aim_frame runs after); one stale frame is harmless.
void overkill_frame(int inLevel);
// The target (a pawn or a ship) is doomed and a projectile gun is up (ok_applies). Reads no engine memory.
int overkill_doomed(const void* target);
// Marks cleared with the target alive since the last call (the triggerbot's 600-frame line), and reset.
unsigned overkill_take_misses(void);

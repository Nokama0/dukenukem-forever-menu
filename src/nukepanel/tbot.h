#pragma once
// Triggerbot (evidence docs/re/aim.md section 4): PlayerPawn.FireDown and FireUp through ProcessEvent, as the Fire
// alias's "FireDown | OnRelease FireUp". Holding refires every gun by itself (Weapon.Firing's AttemptFire while
// WantsToFire), but slower than re-pressing an interrupting gun (tbot_fire_mode).
#include <string.h>
#include <wchar.h>
#include "aim.h"   // AIMW_*

// The cylinder Crosshair's projected fallback places a pawn in: its render box (centre; the larger half width, the half
// height) while its body is animated away from Location (spawnbox_body), as esp.cpp boxes it; else Location and
// CollisionRadius/CollisionHeight. 1 the box.
struct TbCyl { float x, y, z, r, h; };
inline int tbot_pawn_cyl(int hasBox, const float c[3], const float half[3], float x, float y, float z, float r, float h,
                         TbCyl* out) {
  if (hasBox) {
    const TbCyl b = { c[0], c[1], c[2], half[0] > half[1] ? half[0] : half[1], half[2] };
    *out = b;
    return 1;
  }
  const TbCyl l = { x, y, z, r, h };
  *out = l;
  return 0;
}

// DNMP's modes, range and default (its aim.h, itself AA's tbot.h).
const int TBOT_WHEN_CROSSHAIR = 0;   // the game's own look trace hits a hostile pawn
const int TBOT_WHEN_LOCKED = 1;      // Silent aim (or, without it, the aimbot) holds a visible target
const int TBOT_WHEN_AIMBOT = 2;      // AA's mode: the aimbot holds a visible target, wherever the view is yet
const int TBOT_DELAY_MS_MIN = 0, TBOT_DELAY_MS_MAX = 500, TBOT_DELAY_MS_DEF = 0;
const int TBOT_FRAME_MS_MAX = 133;   // AA's four-tic ceiling: a longer gap is a pause, not a hold
const int TBOT_HELD_MS_CAP = 1000000;
inline const char* tbot_when_label(int v) {
  return v == TBOT_WHEN_AIMBOT ? "Aimbot" : v == TBOT_WHEN_LOCKED ? "Locked" : "Crosshair";
}
inline int tbot_when_holds(int when, int crosshairEnemy, int locked) {
  return when == TBOT_WHEN_LOCKED || when == TBOT_WHEN_AIMBOT ? (locked != 0) : (crosshairEnemy != 0);
}
// Locked counts only for a shot the bracket redirects: a bracketed weapon and Silent aim's pre-checks passing
// (aim_redirect_ready). The freeze-ray beam, or a redirect that would be refused, flies down the crosshair.
inline int tbot_locked_covered(int kind, int ready) {
  return (kind == AIMW_HITSCAN || kind == AIMW_PROJECTILE) && ready;
}
// The condition: Locked never fires with an ally or neutral under the crosshair; a shot Silent aim does not cover (off,
// or a beam) follows the aimbot's lock if held. Crosshair: the look trace's hostile pawn alone. Aimbot: the lock alone,
// never with an ally or neutral under it, and the shot must land: Silent aim covers it or rayHit (-1 unresolved fires).
inline int tbot_holds(int when, int crosshairEnemy, int crosshairFriend, int locked, int covered,
                      int aimbotLocked = 0, int rayHit = -1) {
  if (when != TBOT_WHEN_LOCKED && when != TBOT_WHEN_AIMBOT) return crosshairEnemy != 0;
  if (crosshairFriend) return 0;
  if (when == TBOT_WHEN_AIMBOT) return locked != 0 && (covered || rayHit != 0);
  if (covered) return locked != 0;
  return aimbotLocked || crosshairEnemy;
}

// Boss priority (aim.md section 12): while the boss is possible (aim_boss_on), every mode fires at it alone; an add
// under the crosshair or a lock on one is skipped.
inline int tbot_boss_skips(int bossOn, const void* target, const void* boss) {
  return bossOn && target && target != boss;
}

// Whether the view is on the shot's mark (the rayHit of tbot_holds): a projectile gun is led, so the view points ahead
// of the pawn and the ray would miss; it is on the mark under TBOT_LEAD_TOL_DEG of the aimbot's own (led) point
// (angleDeg < 0: unresolved). Other kinds follow the ray (1, 0, -1).
const float TBOT_LEAD_TOL_DEG = 2.f;
inline int tbot_view_ready(int kind, int ray, float angleDeg) {
  if (kind != AIMW_PROJECTILE) return ray;
  return angleDeg < 0.f ? -1 : angleDeg < TBOT_LEAD_TOL_DEG ? 1 : 0;
}

// Crosshair: the look trace's actor (PlayerPawn.DirectLookHitActor, the raw first hit, so an ally in front shows) is a
// walked pawn world_classify calls hostile, not an ally, within MaxTraceDistance for a hitscan gun (beam:
// BeamMaxLength; maxTrace <= 0: unread, no cap). Fists: maxTrace = melee reach (aim_melee_reach); unread never punches.
inline int tbot_crosshair_enemy(int found, int enemy, int ally, float dist, int kind, float maxTrace) {
  if (!found || !enemy || ally || !(dist >= 0.f)) return 0;
  if (kind == AIMW_MELEE) return maxTrace > 0.f && dist <= maxTrace;
  return (kind != AIMW_HITSCAN && kind != AIMW_BEAM) || !(maxTrace > 0.f) || dist <= maxTrace;
}
// The walked pawn an actor the look trace hit belongs to (a flying Assault Trooper's look hits read "another actor"):
// the actor itself, else the first pawn up its Owner and MountParent links, breadth first, TBOT_ATTACH_STEPS at most.
// links fills { Owner, MountParent }; isPawn: a walked pawn, never the player; stop (may be null): a link not followed.
enum { TBOT_ATTACH_STEPS = 4, TBOT_ATTACH_SEEN = 32 };
typedef void (*TbotLinks)(const void* o, const void* out[2], void* ctx);
typedef int (*TbotIsPawn)(const void* o, void* ctx);
inline const void* tbot_attached_pawn(const void* hit, TbotLinks links, TbotIsPawn isPawn, TbotIsPawn stop, void* ctx,
                                      int* steps) {
  *steps = -1;
  if (!hit) return 0;
  if (isPawn(hit, ctx)) {
    *steps = 0;
    return hit;
  }
  const void* seen[TBOT_ATTACH_SEEN];
  int n = 0, from = 0;
  seen[n++] = hit;
  for (int step = 1; step <= TBOT_ATTACH_STEPS; step++) {
    const int to = n;   // this level is seen[from, to)
    for (int i = from; i < to; i++) {
      if (stop && stop(seen[i], ctx)) continue;   // its links are not followed
      const void* l[2] = { 0, 0 };
      links(seen[i], l, ctx);
      for (int k = 0; k < 2; k++) {
        if (!l[k]) continue;
        int dup = 0;
        for (int j = 0; j < n && !dup; j++) dup = seen[j] == l[k];
        if (dup) continue;
        if (isPawn(l[k], ctx)) {
          *steps = step;
          return l[k];
        }
        if (n < TBOT_ATTACH_SEEN) seen[n++] = l[k];
      }
    }
    from = to;
  }
  return 0;
}

// The fallback when the look-trace properties do not resolve: the screen centre inside a pawn's projected box.
inline int tbot_centre_in(int x, int y, int w, int h, int cx, int cy) {
  return w > 0 && h > 0 && cx >= x && cx <= x + w && cy >= y && cy <= y + h;
}

// The delay machine (DNMP's), stepped once a frame: 1 once the condition has held on the same target for the
// clamped delay; the first frame counts as zero held. A break or a new target starts again.
struct TbotState { int holding; int heldMs; const void* target; };
inline int tbot_step(TbotState* s, int holds, const void* target, int dtMs, int delayMs) {
  if (!holds) { s->holding = 0; s->heldMs = 0; s->target = 0; return 0; }
  if (dtMs < 0) dtMs = 0;
  if (dtMs > TBOT_FRAME_MS_MAX) dtMs = TBOT_FRAME_MS_MAX;
  if (delayMs < TBOT_DELAY_MS_MIN) delayMs = TBOT_DELAY_MS_MIN;
  if (delayMs > TBOT_DELAY_MS_MAX) delayMs = TBOT_DELAY_MS_MAX;
  if (!s->holding || s->target != target) {
    s->holding = 1; s->heldMs = 0; s->target = target;
  } else {
    s->heldMs = s->heldMs + dtMs > TBOT_HELD_MS_CAP ? TBOT_HELD_MS_CAP : s->heldMs + dtMs;
  }
  return s->heldMs >= delayMs ? 1 : 0;
}

// The RPG (a held press may lock on) and the railgun (1.5 s lockout) fire one shot a press: tapped, released the
// next frame (aim.md section 4). By the held weapon's config class name.
inline int tbot_taps(const wchar_t* cfgName) {
  return cfgName && (_wcsicmp(cfgName, L"RPGWeaponConfig") == 0 || _wcsicmp(cfgName, L"RailgunWeaponConfig") == 0);
}

// Fire cadence: a held trigger refires at the fire animation's end or, in idle, at ClearLockout; a fresh press inside
// Firing passes only if bFireInterruptFiring is set, once bWeaponFireLockout clears. So re-pressing beats holding only
// for such a gun, never one with a fire-start or looping animation. PULSE: press when ready, release next frame.
enum { TBOT_FIRE_HOLD = 0, TBOT_FIRE_PULSE = 1 };
inline int tbot_fire_mode(int haveFlags, int interrupt, int fireStart, int looping) {
  return haveFlags && interrupt && !fireStart && !looping ? TBOT_FIRE_PULSE : TBOT_FIRE_HOLD;
}

// The fists hold (Engine.u, read 2026-10-08): MeleeAttack.Fire is empty, so a press inside a punch does nothing, and
// the punch's end (MeleeAttack.WeaponCallback_MeleeComplete -> idle.BeginState -> AttemptFire) throws the next one
// while the trigger is held.
inline int tbot_fire_mode_kind(int kind, int mode) { return kind == AIMW_MELEE ? TBOT_FIRE_HOLD : mode; }

// A punch thrown since the last frame: Weapon.SetWeaponState bumps WeaponStateImpulse (authority) and WpnMelee sets
// state 10 (MeleeAttack). Two in one frame count once; a punch lasts many frames. lastImpulse < 0: unread.
enum { TBOT_WSTATE_MELEE = 10 };
inline int tbot_punch_began(int lastImpulse, int impulse, int state) {
  return lastImpulse >= 0 && impulse != lastImpulse && state == TBOT_WSTATE_MELEE;
}

// A reload fire input would cut short (handling.md 2): the shotgun's per-shell one, where Shotgun.Reloading tries
// AttemptFire at every shell's end; a whole-clip reload has no Fire in its state. The wait lasts while shells remain to
// load: loaded < clip and reserve left (Ammo.Charge counts the loaded rounds). Any value unread (< 0): never waits.
enum { TBOT_WSTATE_RELOAD = 7, TBOT_WSTATE_RELOAD_START = 8, TBOT_WSTATE_RELOAD_STOP = 9 };
inline int tbot_reload_wait(int state, int loaded, int clip, int insert, float charge) {
  if (state < TBOT_WSTATE_RELOAD || state > TBOT_WSTATE_RELOAD_STOP) return 0;
  if (loaded < 0 || clip <= 0 || insert <= 0 || insert >= clip) return 0;
  return loaded < clip && charge > (float)loaded;
}

// The press, per frame. ours: our FireDown still pending. gateOpen: nothing stops the triggerbot; userDown: the fire
// button per the engine's DInput read (dm_button0); gameFiring: PlayerPawn.bIFire. A closing gate sends FireUp at once;
// else FireUp is never sent under the player's own fire (pendingUp sends it later if still firing).
enum { TB_NONE, TB_DOWN, TB_UP, TB_FORGET };
const unsigned TBOT_TAP_GAP_MS = 250;
struct TbotPress { int ours, haveTap, pendingUp; unsigned lastTapMs; unsigned downs, ups, forgets, lateUps;
                   int upOwed; unsigned upRetries; };
// After every TB_UP: a FireUp scall refused (the engine not scriptable) is owed and sent again.
inline void tbot_up_result(TbotPress* p, int called) { p->upOwed = !called; }
inline int tbot_act(TbotPress* p, int gateOpen, int fire, int tap, int userDown, int gameFiring, unsigned now,
                    int pulse = 0, int ready = 1) {
  if (p->upOwed) {   // a refused FireUp: again each frame until it goes through
    if (!gameFiring) {
      p->upOwed = 0;   // the game released it meanwhile
    } else if (userDown) {
      p->upOwed = 0;   // never under the user's own fire: sent once the button is up
      p->pendingUp = 1;
      return TB_NONE;
    } else {
      p->upRetries++;
      return TB_UP;
    }
  }
  if (!gateOpen) {
    const int late = p->pendingUp && gameFiring && !p->ours;
    p->pendingUp = 0;
    if (late) { p->lateUps++; return TB_UP; }
    if (!p->ours) return TB_NONE;
    p->ours = 0;
    p->ups++;
    return TB_UP;
  }
  if (p->pendingUp && !userDown) {
    p->pendingUp = 0;
    if (gameFiring && !p->ours) { p->lateUps++; return TB_UP; }
  }
  if (p->ours && !gameFiring) {
    p->ours = 0;
    p->forgets++;
    return TB_FORGET;
  }
  if (p->ours) {
    if (!tap && !pulse && fire) return TB_NONE;
    p->ours = 0;
    if (tap) {
      p->haveTap = 1;
      p->lastTapMs = now;
    }
    if (userDown) {
      p->forgets++;
      p->pendingUp = 1;
      return TB_FORGET;
    }
    p->ups++;
    return TB_UP;
  }
  if (!fire || userDown || gameFiring) return TB_NONE;
  if (pulse && !ready) return TB_NONE;
  if (tap && p->haveTap && now - p->lastTapMs < TBOT_TAP_GAP_MS) return TB_NONE;   // unsigned: safe across the wrap
  p->ours = 1;
  p->downs++;
  return TB_DOWN;
}

// Mounted on a turret the on-foot weapon is lowered: fire is PlayerPawn.bIDnControlFire, set and cleared by the exec
// functions DnControlFireEnable/Disable (docs/re/turrets.md section 4). A press is released by its own mode's function.
inline const wchar_t* tbot_press_fn(int mounted, int down) {
  return mounted ? (down ? L"DnControlFireEnable" : L"DnControlFireDisable") : (down ? L"FireDown" : L"FireUp");
}
// pending: a press (or a release owed) of the last mode, pressMounted. Mounting or dismounting under it closes the
// gate this frame, so it is released before the other mode's fire is touched: bIDnControlFire never stays on.
inline int tbot_mode_gate(int gateOpen, int pending, int pressMounted, int mountedNow) {
  return gateOpen && !(pending && pressMounted != mountedNow);
}
// A turret holds: no tap, no pulse (the MG42 fires on its animation while bIDnControlFire stays set).
inline int tbot_mounted_cadence(int mounted, int flag) { return mounted ? 0 : flag; }

// --- The glue (tbot.cpp), game thread only ------------------------------------------------------------------------
// Once a frame after aim_frame and before pehook_frame. W, H: the back buffer (the fallback's screen centre).
void tbot_frame(int W, int H, int inLevel, int panelOpen);
// Null while usable; else why the Triggerbot is unavailable.
const char* tbot_refused(void);
// The target the condition held on in the last tbot_frame (overkill.cpp: the lock a shot was fired at), else null.
const void* tbot_target(void);
// A press of ours is down (FireDown or DnControlFireEnable sent, not yet released).
int tbot_pressing(void);

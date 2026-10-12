#pragma once
// Nuke Tank autoplay, played legitimately (docs/re/arcade.md section 7): the panel presses the machine's
// own Fire, Left and Right the way the keys do and never writes a Variable, an actor or the score. Pure rules here;
// arcbot.cpp presses.
#include <math.h>
#include <stdio.h>
#include <string.h>

// Inputs. Left moves the tank toward +Y (MoveTankYPos), Right toward -Y.
enum { ARB_FIRE, ARB_LEFT, ARB_RIGHT, ARB_INPUTS };
// Souls in the trace's order: Enemy01Soul..Enemy05Soul (one per lane), Bomb01Soul, Bomb02Soul, BigBadSoul.
enum { ARB_LANES = 5, ARB_BOMB0 = 5, ARB_BOSS = 7, ARB_SOULS = 8 };

struct ArbSoul { int have; float y, z, hp; };

// Measured in the 2026-10-11 trace (arcade.md section 7) or read from the map's instances; game time throughout.
const float ARB_TANK_V = 96.f;          // u/s while a direction is held (trace median 96.5); stops dead on release
const float ARB_ENEMY_V = 32.f;         // MoveEn0NDown Rate Z -32 (trace 31.6-32.0)
const float ARB_TOUCH_Z = 98.f;         // an enemy touches down here (320 -> 98 in 6.95 s)
const float ARB_HOME_Z = 431.f;         // enemies wait at Z 432 and appear at 320
const float ARB_BOMB_HOME_Z = 360.f;    // bombs wait at 364, start at 318 and sweep to Z 107 in 4.5 s
const float ARB_BOMB_END_Z = 107.f;
const float ARB_BOSS_HOME_Z = 495.f;    // BigBadSoul waits at 496
const float ARB_HIT_HP = 99.5f;         // a hit soul reads 97 until it is sent home (~1 s): dead
const float ARB_BOSS_DEAD_HP = 1.5f;    // the boss's last destruction level is HealthReq 1
// A touch-down nukes the tank through a proximity trigger of radius 24 on the lane (NukePlayerTriggers) touching
// TankAvatarSoul (CollisionRadius 16): 40 u, plus a margin. Lanes are 64 u apart.
const float ARB_REACH = 44.f;
const float ARB_EDGE = 12.f;            // the tank stopped at -1076, 12 u past the outer lane
const float ARB_FIRE_TOL = 4.f;         // trace hits at |off| <= 7.1, misses from 7.5
const float ARB_START_TOL = 3.f;        // move only when farther than this from the goal
const float ARB_STOP_TOL = 1.5f;
const float ARB_SHOT_S = 0.15f;         // bullet flight to the enemies' height: 0.10-0.14 s
// The shot speed those flights imply (Z 227-259 from the tank's 120 in 0.10-0.14 s), and the bullet's LifeSpan.
const float ARB_SHOT_V = 1000.f;
const float ARB_TANK_Z = 120.f;
const float ARB_SHOT_LIFE_S = 0.2f;
const float ARB_MARGIN_S = 0.05f;
const float ARB_DODGE_MARGIN_S = 0.15f;
const unsigned ARB_FIRE_HOLD_MS = 80;   // the user's presses held 68-102 ms
const unsigned ARB_FIRE_GAP_MS = 400;   // the user's shortest interval 463 ms; one bullet (0.2 s) at a time
const unsigned ARB_INFLIGHT_MS = 250;   // a shot at a soul counts as on its way this long
const unsigned ARB_MENU_IDLE_MS = 1500; // nothing attacking this long: the title or the instructions
const unsigned ARB_MENU_GAP_MS = 1000;  // a menu press a second, as the user pressed through them

// A live soul: an enemy below its home and not hit, a bomb on its sweep, the boss below its home and not destroyed.
inline int arb_live(int i, const ArbSoul& s) {
  if (!s.have || i < 0 || i >= ARB_SOULS) return 0;
  if (i == ARB_BOSS) return s.z < ARB_BOSS_HOME_Z && s.hp > ARB_BOSS_DEAD_HP;
  if (s.hp < ARB_HIT_HP) return 0;
  return i >= ARB_BOMB0 ? s.z < ARB_BOMB_HOME_Z && s.z > ARB_BOMB_END_Z - 5.f : s.z < ARB_HOME_Z && s.z > ARB_TOUCH_Z - 5.f;
}
// Seconds until an enemy at z touches down.
inline float arb_enemy_land_s(float z) { return z > ARB_TOUCH_Z ? (z - ARB_TOUCH_Z) / ARB_ENEMY_V : 0.f; }
inline float arb_travel_s(float from, float to) { return fabsf(to - from) / ARB_TANK_V; }

// The rule's input, one frame.
struct ArbIn {
  int active;                  // the switch, the machine in use and idle, the panel closed, not refused
  int paused;                  // the game clock did not advance (or no fresh read): nothing changes
  unsigned nowMs, frameMs;     // game time, and the last frame's game-time delta
  int haveTank;
  float tankY;                 // TankAvatarSoul's Y
  float laneY[ARB_LANES];
  int haveLane[ARB_LANES];
  ArbSoul soul[ARB_SOULS];
  int userHolds[ARB_INPUTS];   // the player's own Fire flag (one the bot did not set) and strafe keys
  int userPressed;             // the player's own DnControlFireEnable since the last frame
  int gameOver, won;           // the game-over screen is up; this session's game was won
};

enum { ARB_DEC_OFF, ARB_DEC_GAMEOVER, ARB_DEC_WON, ARB_DEC_MENU, ARB_DEC_WAIT, ARB_DEC_AIM, ARB_DEC_BOSS, ARB_DEC_DODGE,
       ARB_DEC_YIELD, ARB_DEC_COUNT };
enum { ARB_DODGE_NONE, ARB_DODGE_ENEMY, ARB_DODGE_BOMB, ARB_DODGE_COUNT };

// held: a direction is still held this frame (the remapper's per-tick "held" event); taken: the player took that input
// over and the bot let go without a release.
struct ArbOut {
  int press[ARB_INPUTS], release[ARB_INPUTS], held[ARB_INPUTS], taken[ARB_INPUTS];
  int decision, target, dodge, dodgeSoul, haveGoal;
  float goalY;
};
struct ArbState {
  int down[ARB_INPUTS];
  unsigned downMs[ARB_INPUTS], upMs[ARB_INPUTS];
  int fired, shotSoul;
  unsigned lastFireMs;
  int idle;
  unsigned idleMs;
  int haveWas;
  unsigned wasMs;
  ArbSoul was[ARB_SOULS];
  float vy[ARB_SOULS], vz[ARB_SOULS];
};

// The tank's Y range: the outer lanes plus the edge it stops at.
inline void arb_range(const ArbIn& in, float* lo, float* hi) {
  *lo = 1e9f;
  *hi = -1e9f;
  for (int k = 0; k < ARB_LANES; k++) {
    if (!in.haveLane[k]) continue;
    if (in.laneY[k] - ARB_EDGE < *lo) *lo = in.laneY[k] - ARB_EDGE;
    if (in.laneY[k] + ARB_EDGE > *hi) *hi = in.laneY[k] + ARB_EDGE;
  }
}
// When and where a soul lands: an enemy falls straight in its lane; a bomb sweeps (its measured velocity); the boss's
// touch-down ends the game wherever the tank is, so it is no dodge threat (1e9: never).
inline float arb_land_s(const ArbState* s, const ArbIn& in, int j) {
  if (j < ARB_BOMB0) return arb_enemy_land_s(in.soul[j].z);
  if (j < ARB_BOSS && s->vz[j] < -1.f) {
    const float t = (in.soul[j].z - ARB_BOMB_END_Z) / -s->vz[j];
    return t > 0.f ? t : 0.f;
  }
  return 1e9f;
}
inline float arb_land_y(const ArbState* s, const ArbIn& in, int j, float t) {
  return j < ARB_BOMB0 ? in.soul[j].y : in.soul[j].y + s->vy[j] * t;
}
// A shot's flight to a soul's height, and where the soul is when it arrives (the boss's lead grows with its height).
inline float arb_flight_s(float z) { return z > ARB_TANK_Z ? (z - ARB_TANK_Z) / ARB_SHOT_V : 0.f; }
inline float arb_aim_y(const ArbState* s, const ArbIn& in, int j) {
  return in.soul[j].y + s->vy[j] * arb_flight_s(in.soul[j].z);
}
// Seconds until the next shot may be fired.
inline float arb_fire_wait_s(const ArbState* s, const ArbIn& in) {
  if (in.userHolds[ARB_FIRE]) return 1e9f;
  if (!s->fired || in.nowMs - s->lastFireMs >= ARB_FIRE_GAP_MS) return 0.f;
  return (float)(ARB_FIRE_GAP_MS - (in.nowMs - s->lastFireMs)) / 1000.f;
}
// A threat that dies before it lands: a shot at it is on its way, or the tank is aligned and can fire in time.
inline int arb_killing(const ArbState* s, const ArbIn& in, int j, float tLand) {
  if (j >= ARB_BOSS) return 0;
  const unsigned since = in.nowMs - s->lastFireMs;
  if (s->fired && s->shotSoul == j && since < ARB_INFLIGHT_MS)
    return ARB_SHOT_S - since / 1000.f + ARB_MARGIN_S < tLand;
  return fabsf(arb_aim_y(s, in, j) - in.tankY) <= ARB_FIRE_TOL &&
         arb_fire_wait_s(s, in) + ARB_SHOT_S + ARB_MARGIN_S < tLand;
}
// Standing at y from tArr seconds on is unsafe when a threat (an enemy or a bomb other than `skip`) lands within
// ARB_REACH of it before the tank could leave again. At the tank's own place (tArr 0) a threat being killed is not one.
inline int arb_unsafe(const ArbState* s, const ArbIn& in, float y, float tArr, int skip, int* which) {
  for (int j = 0; j < ARB_BOSS; j++) {
    if (j == skip || !arb_live(j, in.soul[j])) continue;
    const float tLand = arb_land_s(s, in, j);
    if (tLand >= 1e8f) continue;
    const float d = fabsf(y - arb_land_y(s, in, j, tLand));
    if (d >= ARB_REACH) continue;
    if (tArr <= 0.f && arb_killing(s, in, j, tLand)) continue;
    const float leave = (ARB_REACH - d) / ARB_TANK_V;
    if (tLand > tArr - ARB_MARGIN_S && tLand < tArr + leave + ARB_DODGE_MARGIN_S) {
      if (which) *which = j;
      return 1;
    }
  }
  return 0;
}

// The way from `from` to `to` is unsafe when a threat (other than `skip`) lands beside it (within ARB_REACH of a
// point between them) while the tank, moving at ARB_TANK_V, is within reach of that point.
inline int arb_path_unsafe(const ArbState* s, const ArbIn& in, float from, float to, int skip) {
  const float lo = from < to ? from : to, hi = from < to ? to : from;
  for (int j = 0; j < ARB_BOSS; j++) {
    if (j == skip || !arb_live(j, in.soul[j])) continue;
    const float tLand = arb_land_s(s, in, j);
    if (tLand >= 1e8f) continue;
    const float y = arb_land_y(s, in, j, tLand);
    if (y < lo - ARB_REACH || y > hi + ARB_REACH) continue;
    const float d = fabsf(y - from);
    const float enter = d > ARB_REACH ? (d - ARB_REACH) / ARB_TANK_V : 0.f, exit = (d + ARB_REACH) / ARB_TANK_V;
    if (tLand > enter - ARB_MARGIN_S && tLand < exit + ARB_MARGIN_S) return 1;
  }
  return 0;
}

// call 0: let go without a release (the player took the input over).
inline void arb_let_go(ArbState* s, ArbOut* o, int k, unsigned now, int call) {
  if (call) o->release[k] = 1;
  else o->taken[k] = 1;
  s->down[k] = 0;
  s->upMs[k] = now;
}
inline void arb_hold(ArbState* s, ArbOut* o, int k, unsigned now) {
  o->press[k] = 1;
  s->down[k] = 1;
  s->downMs[k] = now;
}

// One frame of decisions, in game time. Kill every attacker before it lands (a touch-down costs one of five human
// resources; the sixth ends it): target the lowest one the tank can reach, move under it, fire when aligned. Dodge when
// a soul will land before it can be shot. Idle 1.5 s: the menu, Fire once a second. Boss: same aim, with a lead.
inline ArbOut arb_step(ArbState* s, const ArbIn& in) {
  ArbOut o = {};
  o.target = -1;
  o.dodgeSoul = -1;
  if (in.paused && in.active) return o;   // a stopped game clock: every press stays as it is
  const unsigned now = in.nowMs;
  // Velocities from the last frame; a jump (sent home, a new attack) reads as none.
  if (s->haveWas && now > s->wasMs) {
    const float dt = (now - s->wasMs) / 1000.f;
    for (int j = 0; j < ARB_SOULS; j++) {
      const float dy = in.soul[j].y - s->was[j].y, dz = in.soul[j].z - s->was[j].z;
      const int ok = in.soul[j].have && s->was[j].have && fabsf(dy) <= 40.f && fabsf(dz) <= 40.f;
      s->vy[j] = ok ? dy / dt : 0.f;
      s->vz[j] = ok ? dz / dt : 0.f;
    }
  }
  memcpy(s->was, in.soul, sizeof s->was);
  s->wasMs = now;
  s->haveWas = 1;
  // The player took an input over: let go without a release, which would clear their own.
  if (s->down[ARB_FIRE] && in.userPressed) arb_let_go(s, &o, ARB_FIRE, now, 0);
  for (int k = ARB_LEFT; k <= ARB_RIGHT; k++)
    if (s->down[k] && in.userHolds[k]) arb_let_go(s, &o, k, now, 0);
  if (!in.active || !in.haveTank || in.gameOver || in.won) {
    for (int k = 0; k < ARB_INPUTS; k++)
      if (s->down[k]) arb_let_go(s, &o, k, now, 1);
    s->idle = 0;
    o.decision = !in.active || !in.haveTank ? ARB_DEC_OFF : in.gameOver ? ARB_DEC_GAMEOVER : ARB_DEC_WON;
    return o;
  }
  const int userMove = in.userHolds[ARB_LEFT] || in.userHolds[ARB_RIGHT];
  int any = 0;
  for (int j = 0; j < ARB_SOULS; j++) any |= arb_live(j, in.soul[j]);
  if (!any && !s->idle) {
    s->idle = 1;
    s->idleMs = now;
  } else if (any) {
    s->idle = 0;
  }
  float lo, hi;
  arb_range(in, &lo, &hi);
  const float wait = arb_fire_wait_s(s, in);
  // The target: the soonest-landing enemy the tank can reach and shoot in time, whose lane is safe to stand in and
  // reach.
  float bestLand = 1e9f, bestArr = 1e9f;
  for (int i = 0; i < ARB_BOMB0; i++) {
    if (!arb_live(i, in.soul[i])) continue;
    const float tLand = arb_enemy_land_s(in.soul[i].z), tArr = arb_travel_s(in.tankY, in.soul[i].y);
    const float tShot = tArr > wait ? tArr : wait;
    if (tShot + ARB_SHOT_S + ARB_MARGIN_S >= tLand) continue;
    if (arb_unsafe(s, in, in.soul[i].y, tArr, i, 0) || arb_path_unsafe(s, in, in.tankY, in.soul[i].y, i)) continue;
    if (tLand < bestLand || (tLand == bestLand && tArr < bestArr)) {
      o.target = i;
      bestLand = tLand;
      bestArr = tArr;
    }
  }
  if (o.target < 0 && arb_live(ARB_BOSS, in.soul[ARB_BOSS])) o.target = ARB_BOSS;
  int which = -1;
  if (arb_unsafe(s, in, in.tankY, 0.f, -1, &which)) {
    o.decision = ARB_DEC_DODGE;
    o.dodge = which < ARB_BOMB0 ? ARB_DODGE_ENEMY : ARB_DODGE_BOMB;
    o.dodgeSoul = which;
    // The nearest safe lane; with none, the lane farthest from where the threat lands.
    float best = 1e9f, farthest = -1.f;
    const float landY = arb_land_y(s, in, which, arb_land_s(s, in, which));
    for (int k = 0; k < ARB_LANES; k++) {
      if (!in.haveLane[k]) continue;
      const float t = arb_travel_s(in.tankY, in.laneY[k]);
      if (!arb_unsafe(s, in, in.laneY[k], t, -1, 0) && t < best) {
        best = t;
        o.goalY = in.laneY[k];
        o.haveGoal = 1;
      }
      if (best >= 1e9f && fabsf(in.laneY[k] - landY) > farthest) {
        farthest = fabsf(in.laneY[k] - landY);
        o.goalY = in.laneY[k];
        o.haveGoal = 1;
      }
    }
  } else if (o.target >= 0) {
    o.decision = o.target == ARB_BOSS ? ARB_DEC_BOSS : ARB_DEC_AIM;
    o.goalY = arb_aim_y(s, in, o.target);
    o.haveGoal = 1;
  } else {
    o.decision = s->idle && now - s->idleMs >= ARB_MENU_IDLE_MS ? ARB_DEC_MENU : ARB_DEC_WAIT;
  }
  if (o.haveGoal) o.goalY = o.goalY < lo ? lo : o.goalY > hi ? hi : o.goalY;
  if (userMove) o.decision = ARB_DEC_YIELD;
  if (s->down[ARB_FIRE] && now - s->downMs[ARB_FIRE] >= ARB_FIRE_HOLD_MS) arb_let_go(s, &o, ARB_FIRE, now, 1);
  // Tolerances never below half a frame's travel, so the tank settles at a low frame rate or a high game speed.
  const float half = ARB_TANK_V * in.frameMs / 2000.f;
  const float startTol = half > ARB_START_TOL ? half : ARB_START_TOL;
  const float fireTol = half > ARB_FIRE_TOL ? half : ARB_FIRE_TOL;
  // Movement, closed loop on the tank's Y: held until within the stop tolerance (or half a frame's travel) or past
  // the goal, released, and reversed no sooner than the next frame.
  int want = 0;   // +1 Left, -1 Right
  if (o.haveGoal && !userMove) {
    const float d = o.goalY - in.tankY;
    const float stop = half > ARB_STOP_TOL ? half : ARB_STOP_TOL;
    const int dir = s->down[ARB_LEFT] ? 1 : s->down[ARB_RIGHT] ? -1 : 0;
    if (dir) want = d * dir > stop ? dir : 0;
    else if (fabsf(d) > startTol) want = d > 0.f ? 1 : -1;
  }
  int released = 0;
  for (int k = ARB_LEFT; k <= ARB_RIGHT; k++) {
    const int dir = k == ARB_LEFT ? 1 : -1;
    if (s->down[k] && want != dir) {
      arb_let_go(s, &o, k, now, 1);
      released = 1;
    }
  }
  for (int k = ARB_LEFT; k <= ARB_RIGHT; k++) {
    const int dir = k == ARB_LEFT ? 1 : -1;
    if (want == dir && !s->down[k] && !released && !o.taken[k]) arb_hold(s, &o, k, now);
    else if (s->down[k] && !o.press[k]) o.held[k] = 1;
  }
  // Fire: in play when a live soul will be over the tank as the shot arrives (wherever the tank is next frame); in
  // the menu once a second. Never in the frame Fire was released: the two edges would cancel in one tick, and a
  // shot recorded but never fired would stand in for a dodge.
  if (!s->down[ARB_FIRE] && !in.userHolds[ARB_FIRE] && !o.taken[ARB_FIRE] && !o.release[ARB_FIRE]) {
    int shoot = -1;
    if (o.decision == ARB_DEC_MENU) {
      if (!s->fired || now - s->lastFireMs >= ARB_MENU_GAP_MS) shoot = ARB_SOULS;
    } else if (wait <= 0.f) {
      const int dir = s->down[ARB_LEFT] ? 1 : s->down[ARB_RIGHT] ? -1 : 0;
      const float next = in.tankY + dir * ARB_TANK_V * in.frameMs / 1000.f;
      for (int j = 0; j < ARB_SOULS && shoot < 0; j++) {
        const int c = o.target < 0 ? j : j == 0 ? o.target : j == o.target ? 0 : j;   // the target first
        if (!arb_live(c, in.soul[c]) || arb_flight_s(in.soul[c].z) > ARB_SHOT_LIFE_S) continue;   // out of reach
        const float y = arb_aim_y(s, in, c);
        if (fabsf(y - in.tankY) <= fireTol && fabsf(y - next) <= fireTol) shoot = c;
      }
    }
    if (shoot >= 0) {
      arb_hold(s, &o, ARB_FIRE, now);
      s->fired = 1;
      s->lastFireMs = now;
      s->shotSoul = shoot < ARB_SOULS ? shoot : -1;
    }
  }
  return o;
}

// A bot row's detail: what was done, the decision, the target (e1..e5, b1, b2, boss), the dodge reason, the tank's
// and the goal's Y; no commas, so it stays one CSV field. 0 when it does not fit.
inline int arb_fmt_detail(char* out, int cap, const char* action, int decision, int target, int dodge, float tankY,
                          float goalY) {
  static const char* const kDec[ARB_DEC_COUNT] = { "off", "gameover", "won", "menu", "wait", "aim", "boss", "dodge",
                                                   "yield" };
  static const char* const kTarget[ARB_SOULS] = { "e1", "e2", "e3", "e4", "e5", "b1", "b2", "boss" };
  static const char* const kDodge[ARB_DODGE_COUNT] = { "none", "enemy", "bomb" };
  if (!out || cap <= 0) return 0;
  const int n = _snprintf_s(out, cap, _TRUNCATE, "%s %s target %s dodge %s tank %.1f goal %.1f", action ? action : "?",
                            decision >= 0 && decision < ARB_DEC_COUNT ? kDec[decision] : "?",
                            target >= 0 && target < ARB_SOULS ? kTarget[target] : "-",
                            dodge >= 0 && dodge < ARB_DODGE_COUNT ? kDodge[dodge] : "?", tankY, goalY);
  if (n < 0) {
    out[0] = 0;
    return 0;
  }
  return n;
}

// --- The glue (arcbot.cpp), game thread only ------------------------------------------------------------------------
// Once a frame after arctrace_frame (whose frame read it uses) and before arctrace_frame_end; gated: the panel is
// open or the free camera flies.
void arcbot_frame(int live, int gated);
// Null while usable; else why the row is unavailable.
const char* arcbot_refused(void);

#pragma once
// Nuke Tank trace (docs/re/arcade.md section 6): while the player uses a dnControl_ArcadeMachine, one CSV row per frame
// and one per event, into System\NukePanel\arcade-trace.csv. It only reads (GetStateName is a read-only native) and
// presses nothing. The writer's rules (flush, caps, rotation, never truncating) are pbtrace.h's. Pure rules here.
#include <stdarg.h>
#include "pbtrace.h"

// Souls in row order: Enemy01Soul..Enemy05Soul, Bomb01Soul, Bomb02Soul, BigBadSoul.
enum { AT_ENEMIES = 5, AT_BOMBS = 2, AT_SOULS = AT_ENEMIES + AT_BOMBS + 1, AT_BOSS = AT_SOULS - 1 };
// An int column that did not resolve: the field is left empty.
const int AT_UNSET = (int)0x80000000;

// One tagged actor's read: have 0 leaves its fields empty (its Tag did not resolve, or the read failed). health: the
// RenderActor float, NaN-free; hasHealth 0 for a class without it.
struct AtActor { int have; float pos[3]; int hidden; int hasHealth; float health; };

// lane/laneOff: the transform of the tank's Y against the enemies' home lanes (lane -1: no tank or lanes). Machine
// Left/Right/Fire are what the control received; inLeft/inRight/inFire the pawn's
// bIStrafeLeft/bIStrafeRight/bIDnControlFire. Scores: Variable.Value; lives: the Life0NTank actors not hidden.
struct AtFrame { unsigned frame; float gameTime; double ms, dtMs; AtActor tank; int lane; float laneOff;
                 AtActor soul[AT_SOULS]; int lives, total, stage[3], bigBad; int left, right, fire, inLeft, inRight,
                 inFire; };
const char* const AT_FRAME_COLUMNS =
    "F,frame,gameTime,ms,dtMs,tankX,tankY,tankZ,tankHid,lane,laneOff,"
    "e1x,e1y,e1z,e1hid,e1hp,e2x,e2y,e2z,e2hid,e2hp,e3x,e3y,e3z,e3hid,e3hp,e4x,e4y,e4z,e4hid,e4hp,"
    "e5x,e5y,e5z,e5hid,e5hp,b1x,b1y,b1z,b1hid,b1hp,b2x,b2y,b2z,b2hid,b2hp,bossX,bossY,bossZ,bossHid,bossHp,"
    "lives,totalScore,stage1Score,stage2Score,stage3Score,bigBadSpawnScore,left,right,fire,inLeft,inRight,inFire";
const char* const AT_EVENT_COLUMNS = PB_EVENT_COLUMNS;

// Appends to a row; 0 once anything did not fit (the row is then dropped whole).
struct AtRow { char* out; int cap, used, ok; };
inline void at_put(AtRow* r, const char* fmt, ...) {
  if (!r->ok) return;
  va_list a;
  va_start(a, fmt);
  const int n = _vsnprintf_s(r->out + r->used, (size_t)(r->cap - r->used), _TRUNCATE, fmt, a);
  va_end(a);
  if (n < 0) r->ok = 0;
  else r->used += n;
}
inline void at_put_int(AtRow* r, int v) {
  if (v == AT_UNSET) at_put(r, ",");
  else at_put(r, ",%d", v);
}
inline void at_put_actor(AtRow* r, const AtActor& a, int withHealth) {
  if (!a.have) {
    at_put(r, withHealth ? ",,,,," : ",,,,");
    return;
  }
  at_put(r, ",%.2f,%.2f,%.2f,%d", a.pos[0], a.pos[1], a.pos[2], a.hidden);
  if (!withHealth) return;
  if (a.hasHealth) at_put(r, ",%.1f", a.health);
  else at_put(r, ",");
}

// The row's length, or 0 when it does not fit in cap (nothing partial is kept).
inline int at_fmt_frame(char* out, int cap, const AtFrame& f) {
  if (!out || cap <= 0) return 0;
  AtRow r = { out, cap, 0, 1 };
  at_put(&r, "F,%u,%.4f,%.2f,%.2f", f.frame, f.gameTime, f.ms, f.dtMs);
  at_put_actor(&r, f.tank, 0);
  if (f.lane < 0) at_put(&r, ",,");
  else at_put(&r, ",%d,%.2f", f.lane, f.laneOff);
  for (int i = 0; i < AT_SOULS; i++) at_put_actor(&r, f.soul[i], 1);
  at_put_int(&r, f.lives);
  at_put_int(&r, f.total);
  for (int i = 0; i < 3; i++) at_put_int(&r, f.stage[i]);
  at_put_int(&r, f.bigBad);
  at_put(&r, ",%d,%d,%d,%d,%d,%d\n", f.left, f.right, f.fire, f.inLeft, f.inRight, f.inFire);
  if (!r.ok || r.used >= cap) {
    out[0] = 0;
    return 0;
  }
  return r.used;
}

// The transform: the lane (0 = Enemy01Soul's) whose home Y is nearest y, and y minus that lane's Y (the tank moves
// at 96 u/s with no snap, so the offset is what a bot must close). A tie goes to the lower lane; -1 without lanes.
// laneY[i] with have[i] 0 (its soul did not resolve) is skipped.
inline int at_lane(const float* laneY, const int* have, int n, float y, float* off) {
  int best = -1;
  float bestD = 0.f;
  for (int i = 0; laneY && have && i < n; i++) {
    if (!have[i]) continue;
    const float d = fabsf(y - laneY[i]);
    if (best < 0 || d < bestD) { best = i; bestD = d; }
  }
  if (off) *off = best < 0 ? 0.f : y - laneY[best];
  return best;
}

// A Dispatcher started since the last frame: bDispatching rose, or it stayed up and Index went back (StartDispatching
// resets it to 0: an interruptable one retriggered). A dispatch that starts and ends between two frames is not seen;
// the score, lives, health and hidden rows are the record that does not depend on it.
inline int at_disp_started(int wasOn, int wasIndex, int on, int index) {
  return (on && !wasOn) || (on && wasOn && index < wasIndex);
}

// A soul's Health fell: one row per drop (a bullet's damage, docs/re/arcade.md section 6). A reset upward
// (ResetEnemy0NHealth) is not a hit.
inline int at_hit(int hadHealth, float was, int hasHealth, float now) { return hadHealth && hasHealth && now < was; }

// Lives: the Life0NTank actors that resolved and are not hidden; AT_UNSET when none resolved.
inline int at_lives(const int* have, const int* hidden, int n) {
  int any = 0, lives = 0;
  for (int i = 0; have && hidden && i < n; i++) {
    if (!have[i]) continue;
    any = 1;
    lives += !hidden[i];
  }
  return any ? lives : AT_UNSET;
}

inline int at_unique_name(wchar_t* out, int cap, const wchar_t* dir, unsigned tick) {
  return out && cap > 0 && dir && _snwprintf_s(out, cap, _TRUNCATE, L"%lsarcade-trace-%u.csv", dir, tick) > 0;
}

// The autoplay's counts for the summary line (arcbot.cpp): per input (0 Fire, 1 Left, 2 Right) the presses, the
// releases, the holds the player took over and those dropped without a call (the machine left); the kills, lives
// lost, wins and game overs while it was on; held: what it holds at its last frame (a level change drops it).
struct AtBot {
  unsigned press[3], release[3], taken[3], dropped[3], kills, livesLost, wins, gameOvers;
  int on, held[3];
};
// Balanced: every press ended by a release, a takeover or a drop.
inline int at_bot_balanced(const AtBot& b) {
  for (int k = 0; k < 3; k++)
    if (b.press[k] != b.release[k] + b.taken[k] + b.dropped[k]) return 0;
  return 1;
}

// --- The glue (arctrace.cpp), game thread only ---------------------------------------------------------------------
// Once a frame, beside pbtrace_frame (before pehook_frame applies its enables); live: a level, the player and a
// view, not paused.
void arctrace_frame(int live);
// After arcbot_frame: closes a session the player left this frame, so the autoplay's release rows land in it first.
void arctrace_frame_end(void);

// This session's latest frame read, for the autoplay (arcbot.cpp): it reads nothing of its own. fresh: read this frame.
struct AtView {
  int active, attached, fresh;   // attached 0: the player left this frame (the session closes after the autoplay)
  float gameSecs, frameGameMs;   // LevelInfo.TimeSeconds of the frame read, and its delta from the frame before
  void* player;
  const void* ctrl;
  AtActor tank, soul[AT_SOULS];
  float laneY[AT_ENEMIES];
  int haveLane[AT_ENEMIES];
  int idle, inFire, inLeft, inRight;   // the machine's state is idle; the pawn's flags
  float gameOverAt;                    // game time of the last GameOverDispatcher start, 0 none
  unsigned kills, livesLost, wins, gameOvers;   // dispatch starts this session
};
int arctrace_view(AtView* out);
// A bot event row (`E,...,bot,0,detail`).
void arctrace_bot_row(const char* detail);
void arctrace_bot_counts(const AtBot& b);

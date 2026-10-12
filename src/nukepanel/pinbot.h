#pragma once
// Pinball autoplay, played legitimately (docs/re/minigame-autoplay.md section 7): the panel presses the
// table's own flippers and plunger through the paths the keys take and never writes score, ball or physics state.
// Pure rules here; pinbot.cpp presses.
#include <math.h>
#include <stdio.h>

enum { PBB_LEFT, PBB_RIGHT, PBB_SIDES };
enum { PBB_FLIP_MAX = 4 };

// A flipper: its pivot in table space and its rest yaw relative to the table (the trace's H rows: local, localYaw).
struct PbbFlipper { float px, py; int yaw; };
struct PbbTable { PbbFlipper f[PBB_SIDES][PBB_FLIP_MAX]; int n[PBB_SIDES]; };

// The ball in a flipper's own frame: `along` its length from the pivot, `perp` up the table (toward -X) from it.
inline void pbb_flip_frame(const PbbFlipper& f, float x, float y, float* along, float* perp) {
  const double a = f.yaw * (3.14159265358979323846 * 2.0 / 65536.0);
  const float dx = (float)cos(a), dy = (float)sin(a);
  // The normal on the -X side; for a flipper along the table's axis, the left-hand one.
  float nx = -dy, ny = dx;
  if (nx > 0.f) { nx = dy; ny = -dx; }
  const float rx = x - f.px, ry = y - f.py;
  *along = rx * dx + ry * dy;
  *perp = rx * nx + ry * ny;
}

// The zone, in that frame (minigame-autoplay.md section 7, from the 2026-10-11 trace): hits were pressed at along
// -1.6..3.8, perp 2.3..6.9; misses at perp 8.0..9.9 (early) and along 6.4 (late); the ball leaves the tip at 6.1..6.6.
const float PBB_ALONG_MIN = -3.f, PBB_ALONG_MAX = 5.8f, PBB_PERP_MIN = 0.5f, PBB_PERP_MAX = 7.f;
inline int pbb_in_zone(const PbbFlipper& f, float x, float y) {
  float a, h;
  pbb_flip_frame(f, x, y, &a, &h);
  return a >= PBB_ALONG_MIN && a <= PBB_ALONG_MAX && h >= PBB_PERP_MIN && h <= PBB_PERP_MAX;   // NaN: never
}

// The plunger lane where the ball waits (MoveBallToTrack puts it at (44, -21)).
inline int pbb_in_lane(float x, float y) { return x > 40.f && y < -19.f; }

// Timings (ms of game time, pbb_game_ms) and speeds (u/s), measured at 100% game speed in section 7.
const unsigned PBB_LEAD_MS = 16;          // the contact is predicted this far ahead (hits turned 11-40 ms on)
const unsigned PBB_HOLD_MS = 200;         // a shot's hold; the user's hits held 200-383 ms
const unsigned PBB_REARM_MS = 60;         // down this long before the same side flips again
const unsigned PBB_CRADLE_MAX_MS = 3000;  // no cradle lasts longer
const unsigned PBB_SETTLE_MS = 150;       // still this long on the raised flipper: settled, released
const unsigned PBB_PLUNGE_HOLD_MS = 600;  // GrabPlunger lasted ~500 ms; the user held 630-755 ms
const unsigned PBB_PLUNGE_REARM_MS = 2000;
const unsigned PBB_JUDGE_MS = 150;        // a flip that turned the ball up the table inside this is a hit
const float PBB_DRAIN_VX = 5.f;           // moving toward the drain (+X)
const float PBB_TRAP_V = 13.f;            // the bot's trace: arrivals at 10-13 u/s settled in the crook, 18-25 rarely
const float PBB_SETTLE_V = 3.f;
const float PBB_REST_V = 5.f;             // at rest in the lane
const float PBB_HIT_VX = -60.f;           // turned up the table

// The shot window along the flipper (section 7's shot table): from 0 to 4.5 the traced shots left at vx -146..-178
// and scored 625-4775 points a shot; before 0 they left at -97..-122 and scored 33-143. Past 4.5 up to the tip
// (PBB_ALONG_MAX) a shot is the drain safety. A trapped ball settles in the crook at along -2.2.
const float PBB_WIN_MIN = 0.f, PBB_WIN_MAX = 4.5f;
// The lower left flipper's aim for a slow ball (below PBB_AIM_V): its drop shot from -0.3 left at +19 deg, scored
// ~250 a shot and preceded all five left outlane drains of the 2026-10-11 01:00 games; its shots at 2.5..3.8 left at
// -5..+39 deg with no drain. The right's drop shot at -0.4 (-18 deg, ~10k a shot, no drain) is kept.
const float PBB_AIM_LEFT = 2.5f, PBB_AIM_V = 40.f;
const float PBB_CRADLE_ALONG_MIN = -3.6f, PBB_CRADLE_ALONG_MAX = -1.5f;   // the inlane mouth down to the crook
enum { PBB_WIN_EARLY, PBB_WIN_SHOT, PBB_WIN_LATE, PBB_WIN_PAST };
inline int pbb_window_of(float along) {
  if (along < PBB_WIN_MIN) return PBB_WIN_EARLY;
  if (along <= PBB_WIN_MAX) return PBB_WIN_SHOT;
  return along <= PBB_ALONG_MAX ? PBB_WIN_LATE : PBB_WIN_PAST;
}

struct PbbIn {
  int active;                  // the switch, a table in use, live, the panel closed, not game over or tilted
  unsigned nowMs;
  int haveBall;
  float x, y, vx, vy;          // the ball, table-local
  int allowPlunger, idle;      // the control's bAllowPlunger; its state is idle
  int userHolds[PBB_SIDES];    // the player's own flipper button is held (a press we did not make)
  int userBackward;            // the player's own backward key is held
  int attached;                // the player is still at the table
  int userPressed[PBB_SIDES];  // the player pressed that side since the last frame (an exec we did not make)
  unsigned frameMs;            // the last frame's game-time delta (ms, rounded up)
  int paused;                  // the game clock did not advance this frame: no press changes
};

// The bot's clock is the game's (LevelInfo.TimeSeconds, the trace's gameTime), in ms: under Game speed every hold,
// settle, cap, re-arm and the tally keep their game-time length. Seconds below 0 or NaN read 0.
inline unsigned pbb_game_ms(float seconds) { return seconds > 0.f ? (unsigned)((double)seconds * 1000.0) : 0u; }
// The lookahead: at least one frame of game time (at speed S a frame is S times more of it), so a press lands a frame
// early rather than late; capped so a hitch does not throw the prediction across the table.
const unsigned PBB_LEAD_MAX_MS = 100;
inline unsigned pbb_lead_ms(unsigned frameMs) {
  return frameMs < PBB_LEAD_MS ? PBB_LEAD_MS : frameMs > PBB_LEAD_MAX_MS ? PBB_LEAD_MAX_MS : frameMs;
}
enum { PBB_PLUNGE_NONE, PBB_PLUNGE_PRESS, PBB_PLUNGE_HOLD, PBB_PLUNGE_RELEASE, PBB_PLUNGE_DROP };
enum { PBB_OUT_NONE, PBB_OUT_HIT, PBB_OUT_MISS };
enum { PBB_WHY_NONE, PBB_WHY_FIRE, PBB_WHY_HELD, PBB_WHY_CRADLE_END, PBB_WHY_STOP, PBB_WHY_TAKEN, PBB_WHY_SAFETY,
       PBB_WHY_CRADLE, PBB_WHY_SETTLED, PBB_WHY_CRADLE_LOST, PBB_WHY_COUNT };
// A side: down; up for a shot; up for a cradle; down after a cradle while the ball rolls to the window (no cradle).
enum { PBB_SIDE_DOWN, PBB_SIDE_SHOT, PBB_SIDE_CRADLE, PBB_SIDE_ROLL };
// along: a shot's predicted contact along the flipper (the tally's bin).
struct PbbOut { int press[PBB_SIDES], release[PBB_SIDES], why[PBB_SIDES], outcome[PBB_SIDES], plunge;
                float along[PBB_SIDES]; };
struct PbbState {
  int down[PBB_SIDES], side[PBB_SIDES], judging[PBB_SIDES], released[PBB_SIDES], still[PBB_SIDES], plunging, plunged;
  unsigned downMs[PBB_SIDES], upMs[PBB_SIDES], judgeMs[PBB_SIDES], stillMs[PBB_SIDES], plungeMs, plungeUpMs;
};

inline void pbb_side_up(PbbState* s, PbbOut* o, int side, int st, int why, unsigned now) {
  o->press[side] = 1;
  o->why[side] = why;
  s->down[side] = 1;
  s->side[side] = st;
  s->downMs[side] = now;
  s->still[side] = 0;
  if (st == PBB_SIDE_SHOT) {
    s->judging[side] = 1;
    s->judgeMs[side] = now;
  }
}
// call 0: the press is let go without a release (a takeover).
inline void pbb_side_down(PbbState* s, PbbOut* o, int side, int st, int why, unsigned now, int call) {
  o->release[side] = call;
  o->why[side] = why;
  s->down[side] = 0;
  s->side[side] = st;
  s->released[side] = 1;
  s->upMs[side] = now;
}

// Per frame: a shot fires when the ball's contact, predicted PBB_LEAD_MS ahead, is in the window, or past it short of
// the tip (drain safety), heading for the drain. A slow ball at the inlane mouth is cradled: the flipper rises early
// and holds until the ball has been still for PBB_SETTLE_MS, then drops. Only the side holding the ball acts.
inline PbbOut pbb_step(PbbState* s, const PbbIn& in, const PbbTable& t) {
  PbbOut o = {};
  if (in.paused && in.active) return o;   // a stopped game clock: every press stays as it is
  const unsigned now = in.nowMs;
  for (int side = 0; side < PBB_SIDES; side++) {
    if (!s->judging[side]) continue;
    if (in.haveBall && in.vx < PBB_HIT_VX) {
      o.outcome[side] = PBB_OUT_HIT;
      s->judging[side] = 0;
    } else if (now - s->judgeMs[side] >= PBB_JUDGE_MS) {
      o.outcome[side] = PBB_OUT_MISS;
      s->judging[side] = 0;
    }
  }
  // The player took a held input over: let go without a release, which would clear their own flag (a side), or
  // send a release nobody is attached to receive (the plunger). A side stays theirs while userHolds.
  for (int side = 0; side < PBB_SIDES; side++) {
    if (!s->down[side] || !in.userPressed[side]) continue;
    pbb_side_down(s, &o, side, PBB_SIDE_DOWN, PBB_WHY_TAKEN, now, 0);
    s->judging[side] = 0;   // what the ball does now is the player's shot
  }
  if (s->plunging && (in.userBackward || !in.attached)) {
    o.plunge = PBB_PLUNGE_DROP;
    s->plunging = 0;
    s->plunged = 1;
    s->plungeUpMs = now;
  }
  if (!in.active) {
    for (int side = 0; side < PBB_SIDES; side++) {
      s->judging[side] = 0;
      if (s->down[side]) pbb_side_down(s, &o, side, PBB_SIDE_DOWN, PBB_WHY_STOP, now, 1);
      else s->side[side] = PBB_SIDE_DOWN;
    }
    if (s->plunging) {
      o.plunge = PBB_PLUNGE_RELEASE;
      s->plunging = 0;
      s->plunged = 1;
      s->plungeUpMs = now;
    }
    return o;
  }
  const float speed = sqrtf(in.vx * in.vx + in.vy * in.vy);
  if (s->plunging) {
    if (now - s->plungeMs >= PBB_PLUNGE_HOLD_MS) {
      o.plunge = PBB_PLUNGE_RELEASE;
      s->plunging = 0;
      s->plunged = 1;
      s->plungeUpMs = now;
    } else {
      o.plunge = PBB_PLUNGE_HOLD;
    }
  } else if (in.allowPlunger && in.idle && in.haveBall && pbb_in_lane(in.x, in.y) && speed < PBB_REST_V &&
             !in.userBackward && !s->down[PBB_LEFT] && !s->down[PBB_RIGHT] &&
             (!s->plunged || now - s->plungeUpMs >= PBB_PLUNGE_REARM_MS)) {
    o.plunge = PBB_PLUNGE_PRESS;
    s->plunging = 1;
    s->plungeMs = now;
  }
  const float lead = pbb_lead_ms(in.frameMs) / 1000.f;
  const float px = in.x + in.vx * lead, py = in.y + in.vy * lead;
  for (int side = 0; side < PBB_SIDES; side++) {
    int lowest = 0;   // the side's flipper nearest the drain
    for (int i = 1; i < t.n[side] && i < PBB_FLIP_MAX; i++)
      if (t.f[side][i].px > t.f[side][lowest].px) lowest = i;
    // Over this side's flippers: a shot and its contact, a cradle, the ball close to a flipper at all.
    int shot = 0, cradle = 0, onFlip = 0;
    float shotAlong = 0.f;
    for (int i = 0; in.haveBall && i < t.n[side] && i < PBB_FLIP_MAX; i++) {
      float a, h, pa, ph;
      pbb_flip_frame(t.f[side][i], in.x, in.y, &a, &h);
      pbb_flip_frame(t.f[side][i], px, py, &pa, &ph);
      const int hNow = h >= PBB_PERP_MIN && h <= PBB_PERP_MAX, hPred = ph >= PBB_PERP_MIN && ph <= PBB_PERP_MAX;
      const float from = side == PBB_LEFT && i == lowest && speed < PBB_AIM_V ? PBB_AIM_LEFT : PBB_WIN_MIN;
      const int predIn = hPred && pa >= from && pa <= PBB_ALONG_MAX;
      const int nowIn = hNow && a >= from && a <= PBB_ALONG_MAX;
      if ((predIn || nowIn) && in.vx > PBB_DRAIN_VX && !shot) {
        shot = 1;
        shotAlong = predIn ? pa : a;
      }
      cradle |= hNow && a >= PBB_CRADLE_ALONG_MIN && a <= PBB_CRADLE_ALONG_MAX && speed <= PBB_TRAP_V && in.vx > 2.f;
      onFlip |= a >= PBB_CRADLE_ALONG_MIN - 1.f && a <= PBB_ALONG_MAX + 0.5f && h >= -1.f && h <= PBB_PERP_MAX + 2.f;
    }
    const int st = s->side[side];
    if (st == PBB_SIDE_SHOT) {
      if (now - s->downMs[side] >= PBB_HOLD_MS) pbb_side_down(s, &o, side, PBB_SIDE_DOWN, PBB_WHY_HELD, now, 1);
      continue;
    }
    if (st == PBB_SIDE_CRADLE) {
      if (speed >= PBB_SETTLE_V) {
        s->still[side] = 0;
      } else if (!s->still[side]) {
        s->still[side] = 1;
        s->stillMs[side] = now;
      }
      if (!onFlip) pbb_side_down(s, &o, side, PBB_SIDE_DOWN, PBB_WHY_CRADLE_LOST, now, 1);
      else if (s->still[side] && now - s->stillMs[side] >= PBB_SETTLE_MS)
        pbb_side_down(s, &o, side, PBB_SIDE_ROLL, PBB_WHY_SETTLED, now, 1);
      else if (now - s->downMs[side] >= PBB_CRADLE_MAX_MS)
        pbb_side_down(s, &o, side, PBB_SIDE_ROLL, PBB_WHY_CRADLE_END, now, 1);
      continue;
    }
    if (st == PBB_SIDE_ROLL && !onFlip) s->side[side] = PBB_SIDE_DOWN;   // the ball went elsewhere: cradles again
    const int ready = !s->plunging && !in.userHolds[side] &&
                      (!s->released[side] || now - s->upMs[side] >= PBB_REARM_MS);
    if (!ready) continue;
    if (shot) {
      const int why = pbb_window_of(shotAlong) == PBB_WIN_SHOT ? PBB_WHY_FIRE : PBB_WHY_SAFETY;
      pbb_side_up(s, &o, side, PBB_SIDE_SHOT, why, now);
      o.along[side] = shotAlong;
    } else if (cradle && s->side[side] == PBB_SIDE_DOWN) {
      pbb_side_up(s, &o, side, PBB_SIDE_CRADLE, PBB_WHY_CRADLE, now);
    }
  }
  return o;
}

// The per-bin tally of points a shot, logged per session for the next tuning (the shot table's bins along the
// flipper): a shot is the points scored from its press to the next press, a drain or PBB_TALLY_MS.
enum { PBB_BINS = 7 };
const unsigned PBB_TALLY_MS = 3000;
inline int pbb_tally_bin(float along) {
  static const float kEdge[PBB_BINS - 1] = { -1.5f, -0.5f, 0.5f, 1.5f, 3.f, 4.5f };
  int b = 0;
  while (b < PBB_BINS - 1 && !(along < kEdge[b])) b++;   // NaN lands in the last bin
  return b;
}
struct PbbTally { int open, bin, score0; unsigned t0, shots[PBB_BINS], drained[PBB_BINS]; long long points[PBB_BINS]; };
inline void pbb_tally_close(PbbTally* t, int score, int drained) {
  t->open = 0;
  t->shots[t->bin]++;
  t->points[t->bin] += score - t->score0;
  t->drained[t->bin] += drained != 0;
}
inline void pbb_tally_press(PbbTally* t, int bin, int score, unsigned now) {
  if (bin < 0 || bin >= PBB_BINS) return;
  if (t->open && now == t->t0) return;   // the other side in the same frame: one shot
  if (t->open) pbb_tally_close(t, score, 0);
  t->open = 1;
  t->bin = bin;
  t->score0 = score;
  t->t0 = now;
}
inline void pbb_tally_frame(PbbTally* t, int score, unsigned now, int drained) {
  if (t->open && (drained || now - t->t0 >= PBB_TALLY_MS)) pbb_tally_close(t, score, drained);
}

// A bot row's detail: the decision, the flipper side (-1 the plunger), the ball's position and velocity; no commas,
// so it stays one CSV field. 0 when it does not fit.
inline int pbb_fmt_detail(char* out, int cap, const char* decision, int side, float x, float y, float vx, float vy) {
  if (!out || cap <= 0) return 0;
  const char* who = side == PBB_LEFT ? "left" : side == PBB_RIGHT ? "right" : "plunger";
  const int n = _snprintf_s(out, cap, _TRUNCATE, "%s %s pos %.1f %.1f vel %.0f %.0f", decision ? decision : "?", who,
                            x, y, vx, vy);
  return n < 0 ? 0 : n;
}

// --- The glue (pinbot.cpp), game thread only ------------------------------------------------------------------------
// Once a frame after pbtrace_frame (whose frame read it uses); gated: the panel is open or the free camera flies.
void pinbot_frame(int live, int gated);
// Null while usable; else why the row is unavailable.
const char* pinbot_refused(void);

#pragma once
// Pinball trace (docs/re/minigame-autoplay.md section 6): while the player uses a dnControl_Pinball(2), one CSV row
// per frame (ball, flippers, buttons, score) and one per event, into System\NukePanel\pinball-trace.csv. It only
// reads and calls read-only natives (KGetLinearVelocity, GetStateName): it presses nothing. Pure rules here.
#include <stdio.h>
#include <math.h>
#include <string.h>

// A rotator's axes (UE units, 65536 a turn), as UE's FRotationMatrix builds them; checked against yaw only in game,
// so the trace header records the table's full Rotation and the local Z column (PostPhysMeqon clamps it to 7..8).
inline void pb_axes(int pitch, int yaw, int roll, float x[3], float y[3], float z[3]) {
  const double k = 3.14159265358979323846 * 2.0 / 65536.0;
  const double sp = sin(pitch * k), cp = cos(pitch * k), sy = sin(yaw * k), cy = cos(yaw * k);
  const double sr = sin(roll * k), cr = cos(roll * k);
  x[0] = (float)(cp * cy); x[1] = (float)(cp * sy); x[2] = (float)sp;
  y[0] = (float)(sr * sp * cy - cr * sy); y[1] = (float)(sr * sp * sy + cr * cy); y[2] = (float)(-sr * cp);
  z[0] = (float)(-(cr * sp * cy + sr * sy)); z[1] = (float)(cy * sr - cr * sp * sy); z[2] = (float)(cr * cp);
}

// World offset d (Location - table.Location, or a velocity) into table space: `d << table.Rotation`, the transform
// dnControlHelper_Pinball_Ball.PostPhysMeqon clamps the ball with.
inline void pb_local(const float d[3], int pitch, int yaw, int roll, float out[3]) {
  float x[3], y[3], z[3];
  pb_axes(pitch, yaw, roll, x, y, z);
  out[0] = d[0] * x[0] + d[1] * x[1] + d[2] * x[2];
  out[1] = d[0] * y[0] + d[1] * y[1] + d[2] * y[2];
  out[2] = d[0] * z[0] + d[1] * z[1] + d[2] * z[2];
}
// The inverse (TransformVectorByRot, MoveBallToTrack's): table space to a world offset.
inline void pb_world(const float l[3], int pitch, int yaw, int roll, float out[3]) {
  float x[3], y[3], z[3];
  pb_axes(pitch, yaw, roll, x, y, z);
  for (int i = 0; i < 3; i++) out[i] = l[0] * x[i] + l[1] * y[i] + l[2] * z[i];
}

// One frame row. vsrc 'K' when the velocity came from KGetLinearVelocity, 'D' from the position delta over dt.
// lUp/rUp: bit i is LeftFlippers[i] / RightFlippers[i].bUp; lBtn/rBtn the control's bLeftButton/bRightButton;
// fire/melee the pawn's bIDnControlFire/bIDnControlMelee.
struct PbFrame { unsigned frame; float gameTime; double ms, dtMs; float pos[3], vel[3]; char vsrc;
                 unsigned lUp, rUp; int lBtn, rBtn, fire, melee, plunger, score, balls, mult; };
const char* const PB_FRAME_COLUMNS = "F,frame,gameTime,ms,dtMs,x,y,z,vx,vy,vz,vsrc,lUp,rUp,lBtn,rBtn,fire,melee,"
                                     "allowPlunger,score,ballsLeft,multiplier";
const char* const PB_EVENT_COLUMNS = "E,frame,gameTime,ms,event,value,detail";

// The row's length, or 0 when it does not fit in cap (nothing partial is kept).
inline int pb_fmt_frame(char* out, int cap, const PbFrame& f) {
  if (!out || cap <= 0) return 0;
  const int n = _snprintf_s(out, cap, _TRUNCATE, "F,%u,%.4f,%.2f,%.2f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%c,%u,%u,%d,%d,%d,"
                            "%d,%d,%d,%d,%d\n", f.frame, f.gameTime, f.ms, f.dtMs, f.pos[0], f.pos[1], f.pos[2],
                            f.vel[0], f.vel[1], f.vel[2], f.vsrc ? f.vsrc : '?', f.lUp, f.rUp, f.lBtn, f.rBtn, f.fire,
                            f.melee, f.plunger, f.score, f.balls, f.mult);
  return n < 0 ? 0 : n;
}

// An event row; a comma or line break in detail becomes ';' so the row keeps its columns.
inline int pb_fmt_event(char* out, int cap, unsigned frame, float gameTime, double ms, const char* name, int value,
                        const char* detail) {
  if (!out || cap <= 0) return 0;
  char d[128];
  int k = 0;
  for (const char* p = detail ? detail : ""; *p && k < (int)sizeof d - 1; p++)
    d[k++] = (*p == ',' || *p == '\n' || *p == '\r') ? ';' : *p;
  d[k] = 0;
  const int n = _snprintf_s(out, cap, _TRUNCATE, "E,%u,%.4f,%.2f,%s,%d,%s\n", frame, gameTime, ms, name ? name : "?",
                            value, d);
  return n < 0 ? 0 : n;
}

// The control event a pawn input flag's edge raises (ControlRemapperEx.HandleEvents -> dnControl.CREvent_KeyChange:
// EnumBase + EventType, pressed 0, released 2), or -1 without an edge. base: 14 fire, 17 melee, 20 forward,
// 23 backward, 26 strafe left, 29 strafe right (minigame-autoplay.md section 1).
inline int pb_flag_event(int base, int was, int now) {
  if (base < 0 || (was != 0) == (now != 0)) return -1;
  return now ? base : base + 2;
}

// A drain (TriggerFunc_BallEnteredGutter): BallsLeft dropped is a ball lost; bBallInGutter rising with BallsLeft
// kept is a save (GutterLight lit). GrabPlunger.BeginState clears bBallInGutter, so each drain is one rising edge.
enum { PB_DRAIN_NONE, PB_DRAIN_LOST, PB_DRAIN_SAVED };
inline int pb_drain(int gutterWas, int gutterNow, int ballsWas, int ballsNow) {
  if (ballsNow < ballsWas) return PB_DRAIN_LOST;
  if (gutterNow && !gutterWas) return PB_DRAIN_SAVED;
  return PB_DRAIN_NONE;
}

// The buffer goes to the file at most once a second, or sooner when the next row would not fit.
const unsigned PB_FLUSH_MS = 1000;
inline int pb_flush_due(unsigned nowMs, unsigned lastMs, int used, int cap, int need) {
  if (used <= 0) return 0;
  return nowMs - lastMs >= PB_FLUSH_MS || used + need > cap;
}

// Rows per session (frame and event rows); past it one cap row is written and the rest dropped (counted).
const unsigned PB_ROW_CAP = 200000;
inline int pb_row_admit(unsigned rows) { return rows < PB_ROW_CAP; }

// The frame rate over a ring of frame times in ms (0 entries skipped), 0 without any.
inline float pb_fps(const float* dtMs, int n) {
  double sum = 0.0;
  int k = 0;
  for (int i = 0; dtMs && i < n; i++)
    if (dtMs[i] > 0.f) { sum += dtMs[i]; k++; }
  return k && sum > 0.0 ? (float)(1000.0 * k / sum) : 0.f;
}

// The trace file starts over (the last one kept as .old) when a write would take it past cap.
const unsigned PB_FILE_CAP = 50u * 1024u * 1024u;
inline int pb_rotate_due(unsigned fileBytes, unsigned add, unsigned cap) {
  return fileBytes > 0 && (fileBytes >= cap || add > cap - fileBytes);
}

// A lost ball's cause (minigame-autoplay.md section 7, "Lost balls"): its last place before the drain against the
// lower flippers' pivots (outside one: that outlane; between: the centre gap), then a flipper held at the drain, a
// flip within PB_FLIP_LATE_MS, or no flip. A tilt in the last seconds comes first (the gutter clears bTilted).
enum { PB_CAUSE_LEFT_OUTLANE, PB_CAUSE_RIGHT_OUTLANE, PB_CAUSE_CENTRE_UNHIT, PB_CAUSE_CENTRE_FLIP, PB_CAUSE_CENTRE_HELD,
       PB_CAUSE_TILT, PB_CAUSE_COUNT };
const unsigned PB_FLIP_LATE_MS = 500;
inline int pb_drain_cause(float y, float leftPivotY, float rightPivotY, int tiltedRecently, int flipperUp,
                          unsigned msSincePress) {
  if (tiltedRecently) return PB_CAUSE_TILT;
  if (y > leftPivotY) return PB_CAUSE_LEFT_OUTLANE;
  if (y < rightPivotY) return PB_CAUSE_RIGHT_OUTLANE;
  if (flipperUp) return PB_CAUSE_CENTRE_HELD;
  return msSincePress <= PB_FLIP_LATE_MS ? PB_CAUSE_CENTRE_FLIP : PB_CAUSE_CENTRE_UNHIT;
}
inline const char* pb_cause_name(int c) {
  static const char* const k[PB_CAUSE_COUNT] = { "left outlane", "right outlane", "centre gap, past a tip unhit",
                                                 "centre gap, a missed or late flip", "centre gap, a flipper held",
                                                 "tilt" };
  return c >= 0 && c < PB_CAUSE_COUNT ? k[c] : "?";
}

// Opening a trace file over one that exists: only once it was renamed away (to .prev.csv or .old) is the name reused;
// otherwise a new uniquely named file is opened, so nothing is ever truncated.
enum { PB_OPEN_MAIN, PB_OPEN_UNIQUE };
inline int pb_open_choice(int existed, int renamed) { return existed && !renamed ? PB_OPEN_UNIQUE : PB_OPEN_MAIN; }
inline int pb_unique_name(wchar_t* out, int cap, const wchar_t* dir, unsigned tick) {
  return out && cap > 0 && dir && _snwprintf_s(out, cap, _TRUNCATE, L"%lspinball-trace-%u.csv", dir, tick) > 0;
}
// A session's header rows, kept to start a rotated file with: a row is appended whole or not at all.
inline int pb_hdr_append(char* buf, int cap, int* used, const char* row, int n) {
  if (!buf || !used || !row || n <= 0 || *used < 0 || n > cap - *used) return 0;
  memcpy(buf + *used, row, n);
  *used += n;
  return 1;
}

// The summary's game speed (game time over wall time) and its game-time frame deltas (a frame with no tick is not
// counted).
inline float pb_speed_ratio(float gameSecs, float wallSecs) { return wallSecs > 0.f ? gameSecs / wallSecs : 0.f; }
struct PbDtStats { unsigned n; double sum; float max; };
inline void pb_dt_add(PbDtStats* d, float ms) {
  if (!(ms > 0.f)) return;
  d->n++;
  d->sum += ms;
  if (ms > d->max) d->max = ms;
}
inline float pb_dt_mean(const PbDtStats& d) { return d.n ? (float)(d.sum / d.n) : 0.f; }

// The summary's rate: points a minute over secs of play, 0 for no time.
inline float pb_per_minute(int gained, double secs) { return secs > 0.0 ? (float)(gained * 60.0 / secs) : 0.f; }

// --- The glue (pbtrace.cpp), game thread only ---------------------------------------------------------------------
// Once a frame, after world_frame and before pehook_frame; live: a level, the player and a view, not paused.
void pbtrace_frame(int live);
// After pinbot_frame: closes a session the player left this frame, so the autoplay's release rows and execs land
// in it first.
void pbtrace_frame_end(void);

// This session's latest frame read, for the autoplay (pinbot.cpp): it reads nothing of its own. fresh: read this
// frame. Flippers: pivot (table-local x, y) and localYaw, per side (0 LeftFlippers, 1 RightFlippers).
struct PbView {
  int active, attached, fresh;   // attached 0: the player left this frame (the session closes after the autoplay)
  float gameSecs, frameGameMs;   // LevelInfo.TimeSeconds of the frame read, and its delta from the frame before
  void* player;
  const void* ctrl;
  int haveBall;
  float pos[3], vel[3];
  int allowPlunger, gameOver, tilted, idle, fire, melee, backward, score, balls;
  int nFlip[2];
  float flip[2][4][2];
  int flipYaw[2][4];
};
int pbtrace_view(PbView* out);
// A bot event row (`E,...,bot,side,detail`); outcome 1 a hit, 2 a miss (counted per side in the summary), 0 none.
void pbtrace_bot_row(int side, const char* detail, int outcome);

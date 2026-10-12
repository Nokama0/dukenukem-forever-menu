// Nuke Tank autoplay (arcbot.h; docs/re/arcade.md section 7). It reads nothing of its own: the trace's frame read
// (arctrace_view) gives the tank, the souls and the flags. It presses the way the keys do: Fire as the bind's exec on
// the player, Left and Right as the machine remapper's HandleEvents(2 / 3, ...). Game thread only.
#include <windows.h>
#include <string.h>
#include "arcbot.h"
#include "arctrace.h"
#include "config.h"
#include "world.h"
#include "scall.h"
#include "scallparms.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "pehook.h"
#include "log.h"
#include "mpgate.h"

// ControlRemapperEx.InputHook: HandleInputEventsWrapper(2, bIStrafeLeft, ..) and (3, bIStrafeRight, ..);
// dnControl.CREvent_KeyChange maps KeyType 2 to EnumBase 26 (Left) and 3 to 29 (Right). EventType 0 pressed, 1 held,
// 2 released.
static const unsigned char KEY_TYPE[ARB_INPUTS] = { 8, 2, 3 };
static const char* const INPUT_NAME[ARB_INPUTS] = { "fire", "left", "right" };
// After a GameOverDispatcher start its screen holds 7 s (delays 1 + 6), then StartArcGameDispatcher.
static const float GAME_OVER_S = 7.5f;

static ArbState s_st;
static const void* s_ctrl;      // the machine the state belongs to
static void* s_player;          // the player the Fire execs went to
static const void* s_remapper;  // the remapper Left and Right went to
static unsigned s_level;
static const char* s_why;
// A call the engine refused stops the bot until the player leaves the machine: what it assumed no longer holds.
static int s_stopped;
// The player's own Fire Enable execs since the last frame (pehook; ours are made inside s_inCall): a takeover.
static int s_inCall, s_userFire, s_peSub = -1;
static AtBot s_bot;
static unsigned s_killsWas, s_livesWas, s_winsWas, s_overWas;
static int s_won, s_lastDec = -1, s_lastTarget = -2, s_lastDodge = -1;

static void on_fire_enable(void* obj, void*, void*) {
  if (!s_inCall && obj && obj == s_player) s_userFire = 1;
}

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static void refuse(const char* why) {
  if (s_why) return;
  s_why = why;
  np_log("arcbot: Arcade autoplay unavailable: %s", why);
}

static void stop_session(const char* what) {
  if (s_stopped) return;
  s_stopped = 1;
  np_log("arcbot: %s refused by the engine: autoplay stopped until the player leaves the machine", what);
}

// The Fire bind's exec on the player (pinbot.cpp exec). 1 called.
static int exec(void* player, const wchar_t* fn) {
  const ScallFn f = scall_find(player, fn);
  if (!f.ok || f.parmsSize) {
    refuse("PlayerPawn's DnControlFireEnable/Disable did not resolve; see nukepanel.log");
    return 0;
  }
  unsigned char parms[4] = { 0, 0, 0, 0 };
  s_inCall = 1;
  const int ok = scall_call(player, f, parms, sizeof parms);
  s_inCall = 0;
  if (ok) return 1;
  stop_session("a Fire exec");
  return 0;
}

// dnUsableSomething.InternalControlRemapper of the machine: the remapper whose InputHook turns the pawn's flags into
// key events while the player is attached. Null when it does not resolve or is not live.
static const void* remapper_of(const void* ctrl) {
  static int s_name;
  if (!s_name) s_name = uname_find(L"InternalControlRemapper");
  if (!ctrl || !s_name) return 0;
  const void* r = 0;
  __try {
    const void* cls = uobj_valid(ctrl) ? uobj_class(ctrl) : 0;
    const UProp p = cls ? uprop_get(cls, s_name) : UProp{};
    if (!p.ok || p.type != UW_OBJECT || p.offset <= 0) return 0;
    r = uw_ptr(ctrl, p.offset);
    if (r && !uobj_valid(r)) r = 0;
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
  return r;
}

// HandleEvents(KeyType, EventType) on the remapper, as the strafe key's flag edge calls it. 1 called.
static int strafe(const void* remapper, int k, unsigned char eventType) {
  const ScallFn f = scall_find((void*)remapper, L"HandleEvents");
  ScallParm kt, et;
  unsigned char parms[64];
  memset(parms, 0, sizeof parms);
  if (!f.ok || f.parmsSize > sizeof parms || !scall_parm(f, L"KeyType", L"ByteProperty", &kt) || kt.size != 1 ||
      !scall_parm(f, L"EventType", L"ByteProperty", &et) || et.size != 1 ||
      !scallparms_put(parms, f.parmsSize, kt.offset, &KEY_TYPE[k], 1) ||
      !scallparms_put(parms, f.parmsSize, et.offset, &eventType, 1)) {
    refuse("ControlRemapperEx.HandleEvents(KeyType, EventType) did not resolve; see nukepanel.log");
    return 0;
  }
  if (scall_call((void*)remapper, f, parms, sizeof parms)) return 1;
  stop_session("HandleEvents");
  return 0;
}

static void bot_row(const char* action, const ArbOut& o, const ArbIn& in) {
  char d[160];
  if (arb_fmt_detail(d, sizeof d, action, o.decision, o.target >= 0 ? o.target : o.dodgeSoul, o.dodge, in.tankY,
                     o.haveGoal ? o.goalY : in.tankY))
    arctrace_bot_row(d);
}

static void begin_session(const AtView& v) {
  memset(&s_st, 0, sizeof s_st);
  memset(&s_bot, 0, sizeof s_bot);
  s_stopped = 0;
  s_userFire = 0;
  s_won = 0;
  s_ctrl = v.ctrl;
  s_player = v.player;
  s_remapper = 0;
  s_killsWas = v.kills;
  s_livesWas = v.livesLost;
  s_winsWas = v.wins;
  s_overWas = v.gameOvers;
  s_lastDec = -1;
  s_lastTarget = -2;
  s_lastDodge = -1;
}

static int any_down(void) { return s_st.down[ARB_FIRE] || s_st.down[ARB_LEFT] || s_st.down[ARB_RIGHT]; }

void arcbot_frame(int live, int gated) {
  if (s_peSub < 0) s_peSub = pehook_subscribe(L"DnControlFireEnable", on_fire_enable, "arcbot");
  const unsigned level = world_level_serial();
  if (level != s_level) {
    // The last level's objects are stale: what was held is forgotten, not released (the pawn went with it).
    s_level = level;
    memset(&s_st, 0, sizeof s_st);
    s_ctrl = 0;
    s_player = 0;
    s_remapper = 0;
  }
  if (!live) return;   // paused or no level: nothing read or called, the presses hold
  AtView v;
  arctrace_view(&v);
  // Another machine while something is held: released first, below, through a step with active 0.
  if (v.active && v.ctrl != s_ctrl && !(s_ctrl && any_down())) begin_session(v);
  pehook_enable(s_peSub, s_ctrl != 0);
  if (!s_ctrl) return;
  const int same = v.active && v.attached && v.ctrl == s_ctrl;
  ArbIn in;
  memset(&in, 0, sizeof in);
  in.active = same && g_cfg.arcadeAuto && !arcbot_refused() && !s_stopped && !gated && v.idle;
  // The game's clock: Game speed scales every hold and gap with the game. No game time passed, or no read this
  // frame: nothing changes.
  in.paused = same && (!v.fresh || !(v.frameGameMs > 0.f));
  in.nowMs = v.gameSecs > 0.f ? (unsigned)((double)v.gameSecs * 1000.0) : 0u;
  in.frameMs = v.frameGameMs > 0.f ? (unsigned)ceilf(v.frameGameMs) : 0u;
  in.haveTank = same && v.tank.have;
  in.tankY = v.tank.pos[1];
  int lanes = 0;
  for (int k = 0; k < ARB_LANES; k++) {
    in.laneY[k] = v.laneY[k];
    in.haveLane[k] = v.haveLane[k];
    lanes += v.haveLane[k] != 0;
  }
  for (int j = 0; j < ARB_SOULS; j++) {
    const AtActor& a = v.soul[j];
    in.soul[j] = ArbSoul{ same && a.have && a.hasHealth, a.pos[1], a.pos[2], a.health };
  }
  if (in.active && (!in.haveTank || lanes < ARB_LANES)) {
    refuse("the Nuke Tank's tank or lanes did not resolve; see nukepanel.log (arcade: lines)");
    in.active = 0;
  }
  // A flag the bot did not set is the player's own input: that input is left to them. Left and Right are never set by
  // the bot (HandleEvents skips the pawn's flags).
  in.userHolds[ARB_FIRE] = same && v.inFire && !s_st.down[ARB_FIRE];
  in.userHolds[ARB_LEFT] = same && v.inLeft;
  in.userHolds[ARB_RIGHT] = same && v.inRight;
  in.userPressed = s_userFire;
  s_userFire = 0;
  in.gameOver = same && v.gameOverAt > 0.f && v.gameSecs >= v.gameOverAt && v.gameSecs - v.gameOverAt < GAME_OVER_S;
  // A win stops the bot for this use of the machine (the machine restarts at the instructions on its own).
  if (v.wins) s_won = 1;
  in.won = s_won;
  // While on: kills, lives, wins and game overs by the trace's dispatch counts.
  if (in.active) {
    s_bot.kills += v.kills - s_killsWas;
    s_bot.livesLost += v.livesLost - s_livesWas;
    s_bot.wins += v.wins - s_winsWas;
    s_bot.gameOvers += v.gameOvers - s_overWas;
  }
  s_killsWas = v.kills;
  s_livesWas = v.livesLost;
  s_winsWas = v.wins;
  s_overWas = v.gameOvers;
  s_bot.on |= in.active;
  const ArbOut o = arb_step(&s_st, in);
  char act[32];
  for (int k = 0; k < ARB_INPUTS; k++) {
    if (o.taken[k]) {   // no call: the input is the player's now
      ++s_bot.taken[k];
      _snprintf_s(act, sizeof act, _TRUNCATE, "taken %s", INPUT_NAME[k]);
      bot_row(act, o, in);
    }
    if (o.release[k]) {
      if (k == ARB_FIRE) {
        s_bot.release[k] += exec(s_player, L"DnControlFireDisable");
      } else if (same && s_remapper) {
        // A remapper that left the object table is refused by scall: counted, the press then stays unbalanced.
        s_bot.release[k] += strafe(s_remapper, k, 2);
      } else {
        ++s_bot.dropped[k];   // the machine was left: the real key's release would reach no one
      }
      _snprintf_s(act, sizeof act, _TRUNCATE, "release %s", INPUT_NAME[k]);
      bot_row(act, o, in);
    }
    if (o.press[k]) {
      if (k == ARB_FIRE) {
        s_bot.press[k] += exec(s_player, L"DnControlFireEnable");
      } else {
        if (!s_remapper) s_remapper = remapper_of(s_ctrl);
        if (!s_remapper) {
          refuse("dnUsableSomething.InternalControlRemapper did not resolve; see nukepanel.log");
          s_st.down[k] = 0;   // nothing was pressed: nothing to release
        } else {
          s_bot.press[k] += strafe(s_remapper, k, 0);
        }
      }
      _snprintf_s(act, sizeof act, _TRUNCATE, "press %s", INPUT_NAME[k]);
      bot_row(act, o, in);
    }
    if (o.held[k] && k != ARB_FIRE && same && s_remapper) strafe(s_remapper, k, 1);
  }
  const int target = o.target >= 0 ? o.target : o.dodgeSoul;
  if (s_bot.on && (o.decision != s_lastDec || target != s_lastTarget || o.dodge != s_lastDodge)) {
    bot_row("decide", o, in);
    s_lastDec = o.decision;
    s_lastTarget = target;
    s_lastDodge = o.dodge;
  }
  for (int k = 0; k < ARB_INPUTS; k++) s_bot.held[k] = s_st.down[k];
  arctrace_bot_counts(s_bot);
  if (!same && !any_down()) {
    s_ctrl = 0;
    s_player = 0;
    s_remapper = 0;
  }
}

const char* arcbot_refused(void) { return s_why ? s_why : mp_refused(); }

// Pinball autoplay (pinbot.h; minigame-autoplay.md 7), game thread only. It reads nothing of its own: the trace's
// frame read (pbtrace_view) gives the ball, flags and flippers. It presses as the keys do: the flipper binds' execs on
// the player, and the plunger as the remapper's HandleEvents(5, ...), the call the backward key's flag edge makes.
#include <windows.h>
#include <string.h>
#include "pinbot.h"
#include "pbtrace.h"
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

// The binds' execs (user.ini: DnControlFireAction / DnControlMeleeAction): left flippers on Fire, right on Melee.
static const wchar_t* const EXEC_ON[PBB_SIDES] = { L"DnControlFireEnable", L"DnControlMeleeEnable" };
static const wchar_t* const EXEC_OFF[PBB_SIDES] = { L"DnControlFireDisable", L"DnControlMeleeDisable" };
// ControlRemapperEx.InputHook: HandleInputEventsWrapper(5, bIBackward, bIWasBackward); EventType 0 pressed, 1 held,
// 2 released (HandleInputEventsWrapper's three calls).
static const unsigned char KEY_BACKWARD = 5;

static PbbState s_st;
static const void* s_ctrl;      // the table the state belongs to
static void* s_player;          // the player the execs went to
static const void* s_remapper;  // the remapper the plunge pressed on
static unsigned s_level;
static const char* s_why;
// Per session (one use of a table), logged at its end: calls by side, plunges, calls the engine refused.
static unsigned s_press[PBB_SIDES], s_release[PBB_SIDES], s_plungeOn, s_plungeHeld, s_plungeOff, s_refused,
    s_dropped, s_faults;
static int s_logged = 1;
// A call the engine refused stops the bot until the player leaves the table: what it assumed no longer holds.
static int s_stopped;
// The player's own Enable execs since the last frame (pehook; ours are made inside s_inCall), per side: a takeover.
static int s_inCall, s_userPressed[PBB_SIDES], s_peSub[PBB_SIDES] = { -1, -1 };
static unsigned s_taken, s_plungeDropped;
// Points a shot per bin along the flipper this session (pinbot.h PbbTally), for the next tuning; not used live.
static PbbTally s_tally;
static int s_ballsWas = -1;
static unsigned s_cradles, s_settled;

static void on_enable(int side, void* obj) {
  if (!s_inCall && obj && obj == s_player) s_userPressed[side] = 1;
}
static void on_fire_enable(void* obj, void*, void*) { on_enable(PBB_LEFT, obj); }
static void on_melee_enable(void* obj, void*, void*) { on_enable(PBB_RIGHT, obj); }

static void stop_session(const char* what) {
  ++s_refused;
  if (s_stopped) return;
  s_stopped = 1;
  np_log("pinbot: %s refused by the engine: autoplay stopped until the player leaves the table", what);
}

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static void refuse(const char* why) {
  if (s_why) return;
  s_why = why;
  np_log("pinbot: Pinball autoplay unavailable: %s", why);
}

// An exec without parameters on the player (tbot.cpp press). 1 called.
static int exec(void* player, const wchar_t* fn) {
  const ScallFn f = scall_find(player, fn);
  if (!f.ok || f.parmsSize) {
    refuse("PlayerPawn's DnControlFire/MeleeEnable/Disable did not resolve; see nukepanel.log");
    return 0;
  }
  unsigned char parms[4] = { 0, 0, 0, 0 };
  s_inCall = 1;
  const int ok = scall_call(player, f, parms, sizeof parms);
  s_inCall = 0;
  if (ok) return 1;
  stop_session("a flipper exec");
  return 0;
}

// dnUsableSomething.InternalControlRemapper of the table: the remapper whose InputHook turns the pawn's flags into
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
    ++s_faults;
    return 0;
  }
  return r;
}

// HandleEvents(KeyType 5, EventType) on the remapper. 1 called.
static int backward(const void* remapper, unsigned char eventType) {
  const ScallFn f = scall_find((void*)remapper, L"HandleEvents");
  ScallParm kt, et;
  unsigned char parms[64];
  memset(parms, 0, sizeof parms);
  if (!f.ok || f.parmsSize > sizeof parms || !scall_parm(f, L"KeyType", L"ByteProperty", &kt) || kt.size != 1 ||
      !scall_parm(f, L"EventType", L"ByteProperty", &et) || et.size != 1 ||
      !scallparms_put(parms, f.parmsSize, kt.offset, &KEY_BACKWARD, 1) ||
      !scallparms_put(parms, f.parmsSize, et.offset, &eventType, 1)) {
    refuse("ControlRemapperEx.HandleEvents(KeyType, EventType) did not resolve; see nukepanel.log");
    return 0;
  }
  if (scall_call((void*)remapper, f, parms, sizeof parms)) return 1;
  stop_session("HandleEvents");
  return 0;
}

static void bot_row(const char* decision, int side, const PbView& v, int outcome) {
  char d[128];
  if (pbb_fmt_detail(d, sizeof d, decision, side, v.pos[0], v.pos[1], v.vel[0], v.vel[1]))
    pbtrace_bot_row(side, d, outcome);
}

static void log_session(const char* why) {
  if (s_logged) return;
  s_logged = 1;
  if (!(s_press[0] | s_press[1] | s_release[0] | s_release[1] | s_plungeOn | s_plungeOff | s_dropped | s_refused |
        s_faults))
    return;   // a table played by hand: nothing to say
  np_log("pinbot: %s: left %u press(es) / %u release(s), right %u / %u, %u taken over by the player; %u cradle(s), "
         "%u settled; plunger %u press(es) / %u held / %u release(s), %u dropped (the player's key or the table "
         "left); %u dropped with the level, %u call(s) refused, %u fault(s)", why, s_press[0], s_release[0],
         s_press[1], s_release[1], s_taken, s_cradles, s_settled, s_plungeOn, s_plungeHeld, s_plungeOff,
         s_plungeDropped, s_dropped, s_refused, s_faults);
  // The tally: per bin along the flipper (edges -1.5 -0.5 0.5 1.5 3 4.5), shots, points a shot, shots ended by a drain.
  char line[512];
  int len = _snprintf_s(line, sizeof line, _TRUNCATE, "pinbot: points a shot by contact along the flipper:");
  static const char* const kBin[PBB_BINS] = { "<-1.5", "-1.5..-0.5", "-0.5..0.5", "0.5..1.5", "1.5..3", "3..4.5",
                                              ">4.5" };
  for (int b = 0; b < PBB_BINS && len > 0 && len < (int)sizeof line; b++) {
    const unsigned n = s_tally.shots[b];
    const int k = _snprintf_s(line + len, sizeof line - len, _TRUNCATE, " %s %u shot(s) %.0f a shot %u drained;",
                              kBin[b], n, n ? (double)s_tally.points[b] / n : 0.0, s_tally.drained[b]);
    if (k < 0) break;
    len += k;
  }
  np_log("%s", line);
}

static void begin_session(const void* ctrl, void* player) {
  memset(&s_st, 0, sizeof s_st);
  memset(s_press, 0, sizeof s_press);
  memset(s_release, 0, sizeof s_release);
  s_plungeOn = s_plungeHeld = s_plungeOff = s_refused = s_dropped = s_faults = s_taken = s_plungeDropped = 0;
  s_stopped = 0;
  s_userPressed[0] = s_userPressed[1] = 0;
  memset(&s_tally, 0, sizeof s_tally);
  s_ballsWas = -1;
  s_cradles = s_settled = 0;
  s_ctrl = ctrl;
  s_player = player;
  s_remapper = 0;
  s_logged = 0;
}

void pinbot_frame(int live, int gated) {
  if (s_peSub[0] < 0) s_peSub[0] = pehook_subscribe(L"DnControlFireEnable", on_fire_enable, "pinbot");
  if (s_peSub[1] < 0) s_peSub[1] = pehook_subscribe(L"DnControlMeleeEnable", on_melee_enable, "pinbot");
  const unsigned level = world_level_serial();
  if (level != s_level) {
    // The last level's objects are stale: what was held is forgotten, not released (the pawn went with it).
    s_level = level;
    for (int side = 0; side < PBB_SIDES; side++) s_dropped += s_st.down[side];
    s_dropped += s_st.plunging;
    if (s_ctrl) log_session("level changed");
    memset(&s_st, 0, sizeof s_st);
    s_ctrl = 0;
    s_player = 0;
    s_remapper = 0;
  }
  if (!live) return;   // paused or no level: nothing read or called, the presses hold
  PbView v;
  pbtrace_view(&v);
  if (v.active && v.ctrl != s_ctrl) {
    if (s_ctrl && (s_st.down[0] || s_st.down[1] || s_st.plunging)) {
      // Another table: what the old one held is released first, below, through a step with active 0.
    } else {
      if (s_ctrl) log_session("left the table");
      begin_session(v.ctrl, v.player);
    }
  }
  pehook_enable(s_peSub[0], s_ctrl != 0);
  pehook_enable(s_peSub[1], s_ctrl != 0);
  if (!s_ctrl) return;
  // v.attached 0: the player left this frame; the trace stays open until pbtrace_frame_end for the release rows.
  const int same = v.active && v.attached && v.ctrl == s_ctrl;
  PbbIn in = {};
  in.active = same && g_cfg.pinballAuto && !pinbot_refused() && !s_stopped && !gated && !v.gameOver && !v.tilted;
  in.attached = same;
  for (int side = 0; side < PBB_SIDES; side++) {
    in.userPressed[side] = s_userPressed[side];
    s_userPressed[side] = 0;
  }
  // The game's clock (pbb_game_ms): Game speed scales every timing with the ball. A frame read with no game time
  // passed is a paused clock.
  in.nowMs = pbb_game_ms(v.gameSecs);
  in.frameMs = v.frameGameMs > 0.f ? (unsigned)ceilf(v.frameGameMs) : 0u;
  in.paused = same && v.fresh && !(v.frameGameMs > 0.f);
  in.haveBall = same && v.fresh && v.haveBall;
  in.x = v.pos[0];
  in.y = v.pos[1];
  in.vx = v.vel[0];
  in.vy = v.vel[1];
  in.allowPlunger = same && v.allowPlunger;
  in.idle = same && v.idle;
  // A flag we did not set is the player's own press: that side is left to them.
  in.userHolds[PBB_LEFT] = same && v.fire && !s_st.down[PBB_LEFT];
  in.userHolds[PBB_RIGHT] = same && v.melee && !s_st.down[PBB_RIGHT];
  in.userBackward = same && v.backward;   // our plunge never sets bIBackward
  PbbTable t = {};
  for (int side = 0; same && side < PBB_SIDES; side++) {
    t.n[side] = v.nFlip[side] < PBB_FLIP_MAX ? v.nFlip[side] : PBB_FLIP_MAX;
    for (int i = 0; i < t.n[side]; i++)
      t.f[side][i] = PbbFlipper{ v.flip[side][i][0], v.flip[side][i][1], v.flipYaw[side][i] };
  }
  if (in.active && (t.n[PBB_LEFT] < 1 || t.n[PBB_RIGHT] < 1)) {
    refuse("the table's LeftFlippers or RightFlippers did not resolve; see nukepanel.log");
    in.active = 0;
  }
  const PbbOut o = pbb_step(&s_st, in, t);
  static const char* const kWhy[PBB_WHY_COUNT] = { "", "press", "release held", "release cradle cap", "release stop",
                                                  "taken by the player", "press safety", "cradle", "release settled",
                                                  "release cradle lost" };
  // The tally closes a shot on a drain (BallsLeft dropped) or after 3 s; a shot's press opens the next.
  pbb_tally_frame(&s_tally, v.score, in.nowMs, same && s_ballsWas >= 0 && v.balls < s_ballsWas);
  if (same) s_ballsWas = v.balls;
  for (int side = 0; side < PBB_SIDES; side++) {
    if (o.outcome[side]) bot_row(o.outcome[side] == PBB_OUT_HIT ? "hit" : "miss", side, v, o.outcome[side]);
    if (o.press[side]) {
      s_press[side] += exec(s_player, EXEC_ON[side]);
      bot_row(kWhy[o.why[side]], side, v, 0);
      if (o.why[side] == PBB_WHY_CRADLE) ++s_cradles;
      else pbb_tally_press(&s_tally, pbb_tally_bin(o.along[side]), v.score, in.nowMs);
    }
    if (o.release[side]) {
      s_settled += o.why[side] == PBB_WHY_SETTLED;
      s_release[side] += exec(s_player, EXEC_OFF[side]);
      bot_row(kWhy[o.why[side]], side, v, 0);
    }
    if (o.why[side] == PBB_WHY_TAKEN) {   // no call: the press is the player's now
      ++s_taken;
      bot_row(kWhy[PBB_WHY_TAKEN], side, v, 0);
    }
  }
  if (o.plunge == PBB_PLUNGE_PRESS) {
    s_remapper = remapper_of(s_ctrl);
    if (!s_remapper) {
      refuse("dnUsableSomething.InternalControlRemapper did not resolve; see nukepanel.log");
      s_st.plunging = 0;   // nothing was pressed: nothing to release
    } else {
      s_plungeOn += backward(s_remapper, 0);
      bot_row("plunge", -1, v, 0);
    }
  } else if (o.plunge == PBB_PLUNGE_HOLD && s_remapper) {
    s_plungeHeld += backward(s_remapper, 1);
  } else if (o.plunge == PBB_PLUNGE_RELEASE && s_remapper) {
    // A remapper that left the object table is refused by scall: counted, the press then stays unbalanced.
    s_plungeOff += backward(s_remapper, 2);
    bot_row("plunge release", -1, v, 0);
    s_remapper = 0;
  } else if (o.plunge == PBB_PLUNGE_DROP) {
    // The player's own key took it, or the table was left: no release, as the real key's would reach no one.
    ++s_plungeDropped;
    bot_row("plunge dropped", -1, v, 0);
    s_remapper = 0;
  }
  if (!same && !s_st.down[0] && !s_st.down[1] && !s_st.plunging) {
    log_session(v.active ? "left for another table" : "left the table");
    s_ctrl = 0;
    s_player = 0;
  }
}

const char* pinbot_refused(void) { return s_why ? s_why : mp_refused(); }

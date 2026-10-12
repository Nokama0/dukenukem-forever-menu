#pragma once
// Auto-win (autowin.md): QTEs by pressing Jump for the player (PlayerPawn.JumpUp) while it uses a dnControl_TapPowered
// in state idle, and minigames by calling the game's own win function once per use. Every call goes through scall
// (ProcessEvent) on the game thread; nothing writes engine memory. Pure rules; autowin.cpp reads the engine.
#include <string.h>

// What the player is using, by class name along the class's super chain (autowin.md sections 1-2).
enum { AW_KIND_TAP, AW_KIND_PINBALL, AW_KIND_SLOTS, AW_KIND_HOCKEY, AW_KIND_POOLCUE, AW_KIND_POOLBALL,
       AW_KIND_POKER, AW_KIND_AASWITCH, AW_KIND_COUNT };

// The first kind whose class name is on the chain (chain[0] the object's own class), or -1. NAME_None never matches.
inline int aw_kind_of_chain(const int* chain, int n, const int* kindNames, int kinds) {
  for (int i = 0; chain && i < n; i++)
    for (int k = 0; kindNames && k < kinds; k++)
      if (chain[i] && chain[i] == kindNames[k]) return k;
  return -1;
}

// The mash rule (autowin.md section 1, lever 1). One engagement per control: a press each frame while the control is a
// TapPowered whose PlayerUser is the player, in state idle, still lifting (bLift) and below 1. Once a lift we pressed
// completes, nothing more is pressed until the player uses another control, so no press is left pending after Finish.
struct AwMash { const void* ctrl; unsigned presses; int lifted; };
enum { AW_EV_BEGIN = 1, AW_EV_LIFTED = 2, AW_EV_END = 4 };
struct AwMashOut { int press, ev; };
// on: the switch, and the level running (not paused). AW_EV_END reports the engagement *s held before this step.
inline AwMashOut aw_mash_step(AwMash* s, const void* ctrl, int tap, int user, int idle, int bLift, float val, int on) {
  AwMashOut o = { 0, 0 };
  if (ctrl != s->ctrl) {
    if (s->presses) o.ev |= AW_EV_END;
    s->ctrl = ctrl;
    s->presses = 0;
    s->lifted = 0;
  }
  if (!ctrl || !tap) return o;
  if (s->presses && !s->lifted && (!bLift || val >= 1.f)) {
    s->lifted = 1;
    o.ev |= AW_EV_LIFTED;
  }
  if (on && user && idle && bLift && val < 1.f && !s->lifted) {   // NaN never presses
    o.press = 1;
    if (!s->presses) o.ev |= AW_EV_BEGIN;
    s->presses++;
  }
  return o;
}

// The minigame rule (A2): one win per use. A use starts when the object the player uses changes (a control it is
// attached to, or the pool cue it carries), so leaving and coming back is a new use; the win waits until the game is
// ready for it and the switch is on, then is spent whatever the call returns.
struct AwUse { const void* obj; int won; };
inline int aw_use_step(AwUse* s, const void* obj, int ready, int on) {
  if (obj != s->obj) {
    s->obj = obj;
    s->won = 0;
  }
  if (!obj || s->won || !on || !ready) return 0;
  s->won = 1;
  return 1;
}

// Pool (autowin.md section 2): DNF plays pool by using a ball (Details_Balls_PoolBall's UsePhrase "Hit Ball";
// PlayerPawn.ServerUse calls LookHitActor.Used, a KarmaImpulse); the cue is a physics prop. A hit: Use held
// (PlayerPawn.bIUse) while the player looks at a ball, or a ball or the cue carried.
inline int aw_pool_hit(int useDown, int lookKind, int carriedKind) {
  return (useDown && lookKind == AW_KIND_POOLBALL) || carriedKind == AW_KIND_POOLBALL ||
         carriedKind == AW_KIND_POOLCUE;
}
// The pool use is the level's table, latched by the first hit and kept until the level changes: award 23 is
// permanent, so one win per level (aw_use_step on the latch).
inline const void* aw_pool_latch(const void* latched, int hit, const void* table) {
  return latched ? latched : hit ? table : 0;
}

// Alien Abortion's Start switch is a generic Switches_Generic_PushSwitch_B: only its Tag (SwitchAlienAbortReset01 in
// Map09 and Map_mydigs) makes it the game. aaTag 0 (the name not loaded) never matches.
inline int aw_aa_kind(int kind, int tag, int aaTag) {
  if (kind != AW_KIND_AASWITCH) return kind;
  return tag && tag == aaTag ? kind : -1;
}

// How each minigame is won (autowin.md section 2), named in the win line: the game's own win function; the award a
// win grants (poker: no deterministic win exists); or the game played by its own hit, every alien (Alien Abortion).
enum { AW_PATH_NONE, AW_PATH_WIN_FN, AW_PATH_GRANT, AW_PATH_PLAY };
inline int aw_path_of(int kind) {
  switch (kind) {
    case AW_KIND_PINBALL: case AW_KIND_SLOTS: case AW_KIND_HOCKEY: case AW_KIND_POOLBALL: return AW_PATH_WIN_FN;
    case AW_KIND_POKER: return AW_PATH_GRANT;
    case AW_KIND_AASWITCH: return AW_PATH_PLAY;
    default: return AW_PATH_NONE;
  }
}
inline const char* aw_path_name(int path) {
  switch (path) {
    case AW_PATH_WIN_FN: return "the game's win function";
    case AW_PATH_GRANT: return "the award a win grants";
    case AW_PATH_PLAY: return "the game's own hit on every alien";
    default: return "none";
  }
}

// Alien Abortion hit (autowin.md "Alien Abortion"): a pop-up dispatcher DisPopUp0N is hit once it waits on its last
// event (Index == count - 1, the alien going down unhit), so WackModifyRound, which counts the pop-up toward the
// wave, has fired. spent: this pop-up was hit; cleared when it stops dispatching or restarts.
inline int aw_whack_step(int* spent, int dispatching, int index, int count) {
  if (!dispatching || count < 1 || index < count - 1) {
    *spent = 0;
    return 0;
  }
  if (*spent) return 0;
  *spent = 1;
  return 1;
}

// One Alien Abortion game is one use: begun by a Start press, ended by the game's own result (DisExpertScore or
// DisEndGame starting to dispatch during this game) or by AW_AA_IDLE_MS of in-level time with no alien up (the
// longest gap, before the bonus waves, is about 15 s). A frame's time is capped so a pause does not end it.
const unsigned AW_AA_IDLE_MS = 30000, AW_AA_DT_CAP = 1000;
enum { AW_AA_BEGIN = 1, AW_AA_WON = 2, AW_AA_LOST = 4, AW_AA_IDLE = 8 };
struct AwAa { int live, wonWas, lostWas; unsigned idleMs; };
inline unsigned aw_aa_step(AwAa* s, int press, int anyUp, int won, int lost, unsigned dtMs) {
  unsigned ev = 0;
  if (!s->live) {
    if (press) {
      s->live = 1;
      s->idleMs = 0;
      ev = AW_AA_BEGIN;
    }
  } else if (won && !s->wonWas) {
    s->live = 0;
    ev = AW_AA_WON;
  } else if (lost && !s->lostWas) {
    s->live = 0;
    ev = AW_AA_LOST;
  } else if (anyUp) {
    s->idleMs = 0;
  } else {
    s->idleMs += dtMs < AW_AA_DT_CAP ? dtMs : AW_AA_DT_CAP;
    if (s->idleMs >= AW_AA_IDLE_MS) {
      s->live = 0;
      ev = AW_AA_IDLE;
    }
  }
  s->wonWas = won;
  s->lostWas = lost;
  return ev;
}

// The game-over text. award: the level has the TriggerPlayer tagged AAWinEgoTriggerPlayer (Map09; Map_mydigs has
// the game but neither award 29 nor ACH_AliensAborted).
inline const char* aw_aa_result(unsigned ev, int award) {
  if (ev & AW_AA_WON)
    return award ? "won: DisExpertScore started (award 29's TriggerPlayer, ACH_AliensAborted)"
                 : "won: DisExpertScore started (no award on this map: no AAWinEgoTriggerPlayer)";
  if (ev & AW_AA_LOST) return "lost: DisEndGame started";
  return "no result: 30 s with no alien up";
}

// Dispatcher's bDispatching (offset, bit), Index and OutDelays (Dispatch ends on OutDelays.Num() - 1), resolved once
// per level from the class all eight dispatchers share, then read in one guarded pass a frame.
struct AwDispOfs { int dispatching; unsigned bit; int index; int delays; };
struct AwDisp { int on, index, count; };
inline bool aw_disp_ofs_ok(const AwDispOfs& o) {
  return o.dispatching > 0 && o.bit && o.index > 0 && o.delays > 0;
}
inline AwDisp aw_disp_read(const void* d, const AwDispOfs& o) {
  AwDisp r;
  const char* p = (const char*)d;
  int w;
  memcpy(&w, p + o.dispatching, 4);
  r.on = (w & (int)o.bit) != 0;
  memcpy(&r.index, p + o.index, 4);
  memcpy(&r.count, p + o.delays + 4, 4);   // TArray {data, count, max}
  return r;
}

// AirHockeyTable.GameWon(PlayerIndex) wins for PlayerInfo[PlayerIndex].Ctrl and loses the other: the player's index
// is the slot whose Ctrl is the control it uses, -1 when neither (never a guess: the wrong one loses the game).
inline int aw_hockey_index(const void* ctrl0, const void* ctrl1, const void* mine) {
  if (!mine || ctrl0 == ctrl1) return -1;
  return ctrl0 == mine ? 0 : ctrl1 == mine ? 1 : -1;
}

// dnControl_Pinball.AwardPoints(Points) adds Points * max(ScoreMultiplier, 1) and grants award 9 when Score passes
// highscore: the fewest points that pass it (at least 1).
inline int aw_pinball_points(int score, int highscore) {
  const long long need = (long long)highscore - (long long)score + 1;
  if (need < 1) return 1;
  return need > 0x7FFFFFFF ? 0x7FFFFFFF : (int)need;
}

// Auto-win minigames awards pinball only while Pinball autoplay is off: the autoplay plays the table instead.
inline int aw_pinball_awards(int autoMinigames, int pinballAutoplay) { return autoMinigames && !pinballAutoplay; }

// dnControl_CoinOp_SlotMachine.Win(amount) grants award 2 to LastUser for any amount (autowin.md section 2); 10 is
// the first case its switch tests after the award.
const int AW_SLOTS_AMOUNT = 10;

// --- The glue (autowin.cpp), game thread only -----------------------------------------------------------------
// Once a frame after world_frame; inLevel is its result (a level, the player and a view, not paused).
void autowin_frame(int inLevel);
// Null while usable; else why the row is unavailable (a function or property did not resolve).
const char* autowin_qte_refused(void);
const char* autowin_minigames_refused(void);
// Null, or which minigame's call did not resolve (that game is skipped; the others still win).
const char* autowin_minigames_note(void);

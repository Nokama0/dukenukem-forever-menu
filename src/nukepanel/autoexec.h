#pragma once
// Auto-execute (evidence docs/re/executions.md): the game's own Use path, victim.CanBeUsedBy(player) then
// victim.Used(player, player), on a stunned or frozen enemy within Pawn.UseDistance. Every call goes through scall
// (ProcessEvent) on the game thread. The rules are pure for the suite; autoexec.cpp reads the engine.
#include "world.h"   // WATT_NEUTRAL

// Hostile as world_classify judges an enemy, without "alive": a kneeling enemy's Health is already 0 (world.h,
// world_knock_rule), and a frozen ally must never be executed (executions.md section 3).
inline int aex_hostile(int attitude, int targetsPlayer, int bossMeter) {
  return (attitude >= 0 && attitude < WATT_NEUTRAL) || targetsPlayer || bossMeter;
}

// What makes an enemy executable now (executions.md section 1). Stun: state ControlX with ControlXParms.MyControl a
// dnControl_Execution (bosses' finish-me controls are not) not yet controlled. Frozen: FrozenState != 0 with this
// instance's AllowFrozenExecution, and still alive.
enum { AEX_NONE, AEX_STUN, AEX_FROZEN };
inline int aex_kind(int inControlX, int controlIsExec, int isControlled, int frozen, int allowFrozen, int alive) {
  if (inControlX && controlIsExec && !isControlled) return AEX_STUN;
  if (frozen && allowFrozen && alive) return AEX_FROZEN;
  return AEX_NONE;
}

// The reach (executions.md section 5): the horizontal centre distance within UseDistance plus both CollisionRadius
// values, and the vertical gap within UseDistance plus both CollisionHeight values. NaN is never in reach.
inline int aex_in_reach(float dx, float dy, float dz, float useDist, float rP, float rV, float hP, float hV) {
  if (!(useDist > 0.f)) return 0;
  const float r = useDist + rP + rV, v = useDist + hP + hV;
  return dx * dx + dy * dy <= r * r && dz <= v && -dz <= v;   // every comparison with a NaN is false
}

// One walked pawn; eligible when hostile, executable and in reach.
struct AexCand { const void* obj; float dist2; int hostile, kind, reach; };
// The nearest eligible candidate (the first on a tie), or -1.
inline int aex_pick(const AexCand* c, int n) {
  int best = -1;
  for (int i = 0; c && i < n; i++)
    if (c[i].hostile && c[i].kind != AEX_NONE && c[i].reach && (best < 0 || c[i].dist2 < c[best].dist2)) best = i;
  return best;
}

// The gate (executions.md section 5.1): the first reason that stops a call, AEX_GO when none. Paused, menus and loads
// arrive as !inLevel. attached: bAttachedToUsableSomething; viewMapper: ViewMapper set (a vehicle, a cutscene camera);
// control: InteractiveDecoration set (a QTE or a minigame). canPressUse: PlayerPawn.CanPressUse(), the last check.
enum { AEX_GO, AEX_G_OFF, AEX_G_PANEL, AEX_G_LEVEL, AEX_G_DEAD, AEX_G_GRAB, AEX_G_ATTACHED, AEX_G_VIEWMAPPER,
       AEX_G_CONTROL, AEX_G_CANTUSE, AEX_G_COUNT };
struct AexGate { int on, panelOpen, inLevel, playerAlive, grabbing, attached, viewMapper, control, canPressUse; };
inline int aex_gate(const AexGate& g) {
  if (!g.on) return AEX_G_OFF;
  if (g.panelOpen) return AEX_G_PANEL;
  if (!g.inLevel) return AEX_G_LEVEL;
  if (!g.playerAlive) return AEX_G_DEAD;
  if (g.grabbing) return AEX_G_GRAB;
  if (g.attached) return AEX_G_ATTACHED;
  if (g.viewMapper) return AEX_G_VIEWMAPPER;
  if (g.control) return AEX_G_CONTROL;
  if (!g.canPressUse) return AEX_G_CANTUSE;
  return AEX_GO;
}
inline const char* aex_gate_name(int r) {
  static const char* const kNames[AEX_G_COUNT] = { "go", "off", "panel open", "no level or paused", "player dead",
                                                   "grabbing", "attached to a usable", "view mapper set",
                                                   "using a control", "CanPressUse false" };
  return r >= 0 && r < AEX_G_COUNT ? kNames[r] : "?";
}

// One execution at a time. A call latches its victim; nothing else is called while the victim is still in the walk,
// still down and the latch younger than AEX_LATCH_MS (past the 8 s stun, so a Used that did not take never blocks for
// good). A refused attempt (CanPressUse, the trace or CanBeUsedBy false) waits AEX_RETRY_MS.
const unsigned AEX_LATCH_MS = 12000, AEX_RETRY_MS = 250;
struct AexLatch { const void* victim; unsigned sinceMs, waitUntil; int waiting; };
enum { AEX_L_FREE, AEX_L_HELD, AEX_L_GONE, AEX_L_LEFT, AEX_L_TIMEOUT };
// present: the victim is in this frame's walk; down: in ControlX or frozen. A release returns its reason once.
inline int aex_latch_step(AexLatch* l, int present, int down, unsigned now) {
  if (!l->victim) return AEX_L_FREE;
  int r = AEX_L_HELD;
  if (!present) r = AEX_L_GONE;
  else if (!down) r = AEX_L_LEFT;
  else if (now - l->sinceMs >= AEX_LATCH_MS) r = AEX_L_TIMEOUT;
  if (r != AEX_L_HELD) l->victim = 0;
  return r;
}
inline void aex_latch_set(AexLatch* l, const void* victim, unsigned now) {
  l->victim = victim;
  l->sinceMs = now;
}
inline void aex_wait(AexLatch* l, unsigned now) {
  l->waitUntil = now + AEX_RETRY_MS;
  l->waiting = 1;
}
// The candidate to call this frame, or -1: the gate open, no latched victim, no wait running, then the nearest.
inline int aex_choose(AexLatch* l, int gate, const AexCand* c, int n, unsigned now) {
  if (gate != AEX_GO || l->victim) return -1;
  if (l->waiting && (int)(now - l->waitUntil) < 0) return -1;   // unsigned difference: safe across the wrap
  l->waiting = 0;   // over: cleared, so a stale waitUntil never blocks again ~24.8 days later
  return aex_pick(c, n);
}

// --- The glue (autoexec.cpp), game thread only ----------------------------------------------------------------
// Once a frame after world_frame; inLevel is its result (a level, the player and a view, not paused).
void autoexec_frame(int inLevel, int panelOpen);
// Null while usable; else why the row is unavailable (a function or property did not resolve).
const char* autoexec_refused(void);

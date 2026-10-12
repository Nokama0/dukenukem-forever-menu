#pragma once
// Aimbot (AA's TAB_COMBAT rows, aimsolve.h rules): on the player's TriggerViewRotationAdjust, after raw mouse's
// handler, the delta from the rendered view (CalcedViewRotation) to the target is added to PitchAdjust/YawAdjust, so
// UpdateRotation turns ViewRotation by it. Only the event's own Parms are written.
#include <math.h>
#include "aim.h"
#include "ui/bind.h"

enum { AIMBOT_SMOOTH_MIN = 0, AIMBOT_SMOOTH_MAX = 100, AIMBOT_SMOOTH_DEF = 0 };
const int AIMBOT_KEY_DEF = VK_BIND_XBUTTON1;   // mouse button 4

// AA's ekm_smooth_step: (100 - percent) % of the remaining delta, per tic. 0 snaps, 100 never moves; the forced
// unit step keeps a high setting from parking the view one unit short for ever.
inline int aimbot_smooth_step(int delta, int percent) {
  if (delta == 0) return 0;
  if (percent <= 0) return delta;
  if (percent >= 100) return 0;
  int step = (int)((long long)delta * (100 - percent) / 100);
  if (step == 0) step = delta > 0 ? 1 : -1;
  return step;
}

// AA's tic (duke3d-eduke32-aa aimsolve.h:30, EKM_MS_PER_TIC = 1000/30) and its cap on the tics one frame may
// cover (aimsolve.h:33 EKM_LEAD_TICS_MAX, applied at aim.cpp:2072): Smoothing's percentage is kept per tic.
const double AIMBOT_SECONDS_PER_TIC = 1.0 / 30.0;
const int AIMBOT_TICS_MAX = 4;

// AA's ekm_smooth_step_tics (aimsolve.h:850): percent % of the delta retained per tic over the event's DeltaTime in
// tics, so a glide takes the same wall time at 30 fps and at 240 (AA's per-frame step snapped at every setting at
// high frame rates). One tic is exactly aimbot_smooth_step. 0 still snaps, 100 never moves, the unit step stays.
inline int aimbot_smooth_step_dt(int delta, int percent, double dtSeconds) {
  if (delta == 0) return 0;
  if (percent <= 0) return delta;
  if (percent >= 100) return 0;
  double tics = dtSeconds / AIMBOT_SECONDS_PER_TIC;
  if (!(tics > 0.0)) return 0;   // NaN too
  if (tics > (double)AIMBOT_TICS_MAX) tics = (double)AIMBOT_TICS_MAX;
  const double keptN = pow((double)percent, tics), fullN = pow(100.0, tics);
  int step = (int)((double)delta * (fullN - keptN) / fullN);
  if (step == 0) step = delta > 0 ? 1 : -1;
  return step;
}

// UpdateRotation (Engine.dll!0x1026ACA0) clamps ViewRotation.Pitch (0..65535) lying strictly between MinViewPitch
// (+0x1FD4, the up limit) and MaxViewPitch (+0x1FD8, the down limit), to one or the other by aLookUp's sign. Ours goes to the nearer limit, so
// no pitch we leave is ever in that band. Signed out (-32768..32767). An empty band (unread limits) clamps nothing.
inline int aimbot_clamp_pitch(int pitch, int maxUp, int minDown) {
  int u = pitch & 0xFFFF;
  if (maxUp < u && u < minDown) u = u - maxUp <= minDown - u ? maxUp : minDown;
  return aim_wrap16(u);
}

// The view as the handler reads it: ViewRotation.Pitch (what the adjust and the limits act on), the rendered
// CalcedViewRotation (what the player aims with), the limits, and ViewRotationMode's axis locks (2 pitch, 1 yaw:
// UpdateRotation still adds a locked axis' adjust, so ours must stay 0 there).
struct AimbotView { int vrPitch, calcPitch, calcYaw; int maxUp, minDown; int lockPitch, lockYaw; };

// What to add to the event's PitchAdjust/YawAdjust (adjPitch/adjYaw: game or raw mouse input) to bring the rendered
// view onto (wantPitch, wantYaw), one smoothing step over DeltaTime (dtSeconds). Yaw takes the short way. Pitch aims
// inside the limits in ViewRotation's terms, through level, never the band behind (limits wider than half a turn).
inline void aimbot_turn(const AimbotView& v, int adjPitch, int adjYaw, int wantPitch, int wantYaw, int smooth,
                        double dtSeconds, int* dPitch, int* dYaw) {
  *dPitch = *dYaw = 0;
  if (!v.lockPitch) {
    const int shake = aim_wrap16(v.calcPitch - v.vrPitch);
    const int target = aimbot_clamp_pitch(wantPitch - shake, v.maxUp, v.minDown);
    const int base = aim_wrap16(v.vrPitch + adjPitch);
    const int step = aimbot_smooth_step_dt(target - base, smooth, dtSeconds);   // both signed: the way through 0
    *dPitch = aim_wrap16(aimbot_clamp_pitch(base + step, v.maxUp, v.minDown) - base);
  }
  if (!v.lockYaw)
    *dYaw = aimbot_smooth_step_dt(aim_wrap16(wantYaw - (v.calcYaw + adjYaw)), smooth, dtSeconds);
}

// Mounted (aim.md section 9): the camera is the player's free look and took each added turn within one event (0.82 to
// 1.47 of it); a rate-limited gun follows ViewOffset at its own rate. So each event adds K % of the remaining error and
// holds while the camera has not shown half the last add, HOLDS_MAX events at most. An instant gun keeps the snap.
struct AimbotMountState { int valid, add[2], cam[2], holds; };   // the last add and the camera it was made from
enum { AIMBOT_MOUNT_K_PCT = 50, AIMBOT_MOUNT_HOLD_MIN = 512, AIMBOT_MOUNT_HOLDS_MAX = 8 };
inline int aimbot_mount_unshown(int add, int moved) {
  if (add > -AIMBOT_MOUNT_HOLD_MIN && add < AIMBOT_MOUNT_HOLD_MIN) return 0;
  const long long along = add > 0 ? (long long)moved : -(long long)moved;
  return along * 2 < (add > 0 ? (long long)add : -(long long)add);
}
inline int aimbot_mount_scale(int d) {
  if (!d) return 0;
  const int s = (int)((long long)d * AIMBOT_MOUNT_K_PCT / 100);
  return s ? s : (d > 0 ? 1 : -1);
}
inline int aimbot_mount_slower(int a, int b) { return (a < 0 ? -a : a) <= (b < 0 ? -b : b) ? a : b; }
// instant: bUseInstantAimRate (1 true, 0 false, -1 unread: damped). fullP/fullY: aimbot_turn's whole turn (no
// smoothing); dP/dY in: the user's smoothed turn. Instant: dP/dY unchanged. Else the slower of the damped step and
// the smoothed one, per axis, never both stacked; zeroed on a hold. cam: the rendered pitch, yaw. 1 added, 0 held.
inline int aimbot_mount_step(AimbotMountState* s, int instant, const int cam[2], int fullP, int fullY, int* dP,
                             int* dY) {
  if (instant == 1) return 1;
  if (s->valid && s->holds < AIMBOT_MOUNT_HOLDS_MAX &&
      (aimbot_mount_unshown(s->add[0], aim_wrap16(cam[0] - s->cam[0])) ||
       aimbot_mount_unshown(s->add[1], aim_wrap16(cam[1] - s->cam[1])))) {
    ++s->holds;
    *dP = *dY = 0;
    return 0;
  }
  *dP = aimbot_mount_slower(aimbot_mount_scale(fullP), *dP);
  *dY = aimbot_mount_slower(aimbot_mount_scale(fullY), *dY);
  s->valid = 1;
  s->holds = 0;
  s->add[0] = *dP;
  s->add[1] = *dY;
  s->cam[0] = cam[0];
  s->cam[1] = cam[1];
  return 1;
}

// The hold rule (AA's aim.cpp, bind_hold_is_active): Hold key off aims whenever the switch is on; on, only while a
// live binding is down, and the key is read only while the game is in front.
inline int aimbot_key_held(int hold, int vk, int front, int keyDown) {
  if (!hold) return 1;
  return front && bind_hold_is_active(vk, keyDown != 0);
}
// The DIMOUSESTATE button a mouse binding means (rgbButtons: 0 left, 1 right, 2 middle, 3 X1, 4 X2), or -1. The
// engine's mouse is an exclusive DirectInput device, which may keep the buttons from GetAsyncKeyState.
inline int aimbot_vk_di_button(int vk) {
  switch (vk) {
    case VK_BIND_RBUTTON:  return 1;
    case VK_BIND_MBUTTON:  return 2;
    case VK_BIND_XBUTTON1: return 3;
    case VK_BIND_XBUTTON2: return 4;
    default:               return -1;
  }
}
// The lock is kept while the target is live, alive, not known hidden and inside the cone; else it drops at once.
inline int aimbot_keep(int live, int alive, int vis, int inCone) {
  return live && alive && vis != AIM_VIS_NO && inCone;
}

// The aimbot holds back while the held gun is completely out of ammo (outOfAmmo 1); the fists never, nor an unread
// answer. It locks again by itself once ammo returns or another weapon is held.
inline int aimbot_out_of_ammo(int kind, int outOfAmmo) { return aim_gun_kind(kind) && outOfAmmo > 0; }
// Why the aimbot may not lock this frame, in order; null when it may. noAmmo: aimbot_out_of_ammo.
inline const char* aimbot_hold_why(int on, int engaged, int panelOpen, int gate, int noAmmo) {
  if (!on) return "switched off";
  if (!engaged) return "key up";
  if (panelOpen) return "panel open";
  if (gate != AIM_GO) return aim_gate_name(gate);
  if (noAmmo) return "out of ammo";
  return 0;
}

// --- The glue (aimbot.cpp), game thread only ----------------------------------------------------------------------
// The switch is on, usable, and the hold rule passes this frame: aim_frame then picks a target for it.
int aimbot_engaged(void);
// Once a frame after aim_frame and before tbot_frame and pehook_frame: the lock, the subscription, the counts.
void aimbot_frame(int inLevel, int panelOpen);
// The target the aimbot holds (the Triggerbot's Aimbot mode): null until the handler has kept it through an event,
// and from the moment it drops it.
const void* aimbot_lock(void);
// Null while usable; else why the Aimbot is unavailable.
const char* aimbot_refused(void);

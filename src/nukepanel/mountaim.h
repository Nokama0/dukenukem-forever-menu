#pragma once
// Silent aim on mounted guns (docs/re/turrets.md section 8). The shot runs inside an event that passes our dnGame slot
// (FireEffects on the MG42 and the launcher, FireDown.BeginState and AnimCallback_AnimEnd on the SuperTurret). Before
// it, CalcedViewRotation and RotateYaw are set toward the target; after it they are put back.
#include <string.h>
#include <stdlib.h>
#include "aim.h"

// The hooks that enclose a shot.
enum { MS_HOOK_FIREEFFECTS, MS_HOOK_BEGINSTATE, MS_HOOK_ANIMEND, MS_HOOK_COUNT };
inline const char* ms_hook_label(int h) {
  static const char* const k[MS_HOOK_COUNT] = { "FireEffects", "FireDown.BeginState", "AnimCallback_AnimEnd" };
  return h >= 0 && h < MS_HOOK_COUNT ? k[h] : "?";
}
// Which gun a hook brackets: FireEffects runs PerformFire on the MG42 and the launcher (which inherits it); the
// SuperTurret's FireShot runs from FireDown.BeginState and from the player's anim end while in FireDown.
inline int ms_hook_brackets(int hook, int tur) {
  if (hook == MS_HOOK_FIREEFFECTS) return tur == AIM_TUR_MG42 || tur == AIM_TUR_LAUNCHER;
  return (hook == MS_HOOK_BEGINSTATE || hook == MS_HOOK_ANIMEND) && tur == AIM_TUR_SUPER;
}
// The launcher reads CalcedViewRotation whole; the MG42 and the SuperTurret take only its pitch, their yaw from the
// muzzle bone (RotateYaw).
inline int ms_uses_yaw_bone(int tur) { return tur == AIM_TUR_MG42 || tur == AIM_TUR_SUPER; }

// The rotator to write: cur with pitch (and, whole, yaw) moved the short way to the wanted values; roll kept.
inline void ms_rot_write(const int cur[3], int wantP, int wantY, int whole, int out[3]) {
  out[0] = cur[0] + aim_wrap16(wantP - cur[0]);
  out[1] = whole ? cur[1] + aim_wrap16(wantY - cur[1]) : cur[1];
  out[2] = cur[2];
}

// RotateYaw that turns the muzzle from muzzleYaw to wantYaw: the bone's world yaw follows RotateYaw one to one
// (aim_gun_view). The game never holds RotateYaw outside [MinYaw, MaxYaw] with bClampYaw: a value past a limit by
// more than MS_YAW_SLACK is not redirected (0), one within the slack is clamped to it.
enum { MS_YAW_SLACK = 64 };
inline int ms_yaw_clamp(int v, const AimTurretLimits& l, int* out) {
  if (l.clampYaw) {
    if (v < l.minYaw - MS_YAW_SLACK || v > l.maxYaw + MS_YAW_SLACK) return 0;
    if (v < l.minYaw) v = l.minYaw;
    if (v > l.maxYaw) v = l.maxYaw;
  }
  *out = v;
  return 1;
}
inline int ms_yaw_plan(int rotY, int muzzleYaw, int wantYaw, const AimTurretLimits& l, int* out) {
  return ms_yaw_clamp(rotY + aim_wrap16(wantYaw - muzzleYaw), l, out);
}
// A second step after the first UpdateRotations, by the measured slope (secant): only while the residual is above
// MS_YAW_TOL and the slope is plausible (0.5..2 bone units per RotateYaw unit, either sign). 0: no second step.
enum { MS_YAW_TOL = 32 };
inline int ms_yaw_secant(int rot0, int ym0, int rot1, int ym1, int wantYaw, const AimTurretLimits& l, int* out) {
  const int residual = aim_wrap16(wantYaw - ym1), dr = rot1 - rot0, dm = aim_wrap16(ym1 - ym0);
  if (abs(residual) <= MS_YAW_TOL || dr == 0) return 0;
  const double k = (double)dm / (double)dr;
  if (!(fabs(k) >= 0.5 && fabs(k) <= 2.0)) return 0;
  int v = 0;
  if (!ms_yaw_clamp(rot1 + (int)floor(residual / k + 0.5), l, &v) || v == rot1) return 0;   // clamped back: no step
  *out = v;
  return 1;
}

// One bracket: the after-call of the call that opened it closes it (hook, UFunction, gun); a hook raised inside writes
// nothing (nested), and the same UFunction raised again inside is counted in depth, so its after-call does not close
// the outer one. Values go back only while still ours. An unclosed bracket ends at the next frame start (leftOpen).
struct MsBracket { int open, hook, yaw, depth; const void* fn; const void* gun; int savedRot[3], wroteRot[3], savedYaw,
                   wroteYaw; unsigned opened, closed, nested, leftOpen, rotNotOurs, yawNotOurs; };
// Before the shot: 1 to write (cur saved), 0 nested inside an open bracket.
inline int ms_open(MsBracket* b, int hook, const void* fn, const void* gun, const int curRot[3], const int wrRot[3],
                   int yaw, int curYaw, int wrYaw) {
  if (b->open) {
    b->nested++;
    if (hook == b->hook && fn == b->fn) b->depth++;
    return 0;
  }
  b->open = 1;
  b->hook = hook;
  b->fn = fn;
  b->depth = 0;
  b->gun = gun;
  memcpy(b->savedRot, curRot, sizeof b->savedRot);
  memcpy(b->wroteRot, wrRot, sizeof b->wroteRot);
  b->yaw = yaw;
  b->savedYaw = curYaw;
  b->wroteYaw = wrYaw;
  b->opened++;
  return 1;
}
// Whether RotateYaw must still be written after the plan's later steps (a secant step moved it).
inline void ms_yaw_wrote(MsBracket* b, int wrYaw) { b->wroteYaw = wrYaw; }
// What to put back (*rot, *yawBack set when that value is still ours). Shared by the close and the frame start.
struct MsRestore { int rot, yaw; int rotV[3], yawV; };
inline MsRestore ms_restore_of(MsBracket* b, const int nowRot[3], int rotRead, int nowYaw, int yawRead) {
  MsRestore r = { 0, 0, { 0, 0, 0 }, 0 };
  if (rotRead && memcmp(nowRot, b->wroteRot, sizeof b->wroteRot) == 0) {
    r.rot = 1;
    memcpy(r.rotV, b->savedRot, sizeof r.rotV);
  } else {
    b->rotNotOurs++;
  }
  if (b->yaw) {
    if (yawRead && nowYaw == b->wroteYaw) {
      r.yaw = 1;
      r.yawV = b->savedYaw;
    } else {
      b->yawNotOurs++;
    }
  }
  return r;
}
// After the shot, by the hook that opened it: 1 closed (restore as *r says), 0 not this hook's bracket.
inline int ms_close(MsBracket* b, int hook, const void* fn, const void* gun, const int nowRot[3], int rotRead,
                    int nowYaw, int yawRead, MsRestore* r) {
  if (!b->open || b->hook != hook || b->fn != fn || b->gun != gun) return 0;
  if (b->depth > 0) {   // the nested call's own after-call
    b->depth--;
    return 0;
  }
  *r = ms_restore_of(b, nowRot, rotRead, nowYaw, yawRead);
  b->open = 0;
  b->closed++;
  return 1;
}
// At the frame start: a bracket still open (its after-handler never ran) is closed now.
inline int ms_frame_start(MsBracket* b, const int nowRot[3], int rotRead, int nowYaw, int yawRead, MsRestore* r) {
  if (!b->open) return 0;
  *r = ms_restore_of(b, nowRot, rotRead, nowYaw, yawRead);
  b->open = 0;
  b->leftOpen++;
  return 1;
}

// A shot ran inside the bracket: FireEffects steps MuzzleIndex (MG42, launcher), FireShot flips bFireLeft
// (SuperTurret). -1 unread before or after: unknown.
inline int ms_shot_seen(int tur, int muzzleBefore, int muzzleAfter, int leftBefore, int leftAfter) {
  if (tur == AIM_TUR_SUPER) return leftBefore < 0 || leftAfter < 0 ? -1 : leftBefore != leftAfter;
  return muzzleBefore == -9 || muzzleAfter == -9 ? -1 : muzzleBefore != muzzleAfter;
}

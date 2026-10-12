#pragma once
// The free camera's pure rules (freecam.md 1-3, 6): the pose and flight, the view the PlayerCalcView override writes,
// the pause, the forced cvars, how a flight ends, the Copy view line. Glue: freecam.cpp. Axes as view.h: X forward at
// yaw 0, Y right, Z up; a rotator is Pitch, Yaw, Roll at 65536 a turn, yaw growing toward +Y. The pose holds degrees.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "config.h"    // FREECAM_SPEED_*
#include "cvars.h"     // cvar_bits, cvar_step: the forced cvars
#include "ui/bind.h"   // BIND_ROW_*
#include "fov.h"       // FOV_MIN_DEG, FOV_MAX_DEG: the wheel's range

// The movement keys, one bit each, read once a frame by freecam.cpp.
enum { FCK_FWD = 1, FCK_BACK = 2, FCK_LEFT = 4, FCK_RIGHT = 8,
       FCK_UP = 16, FCK_DOWN = 32, FCK_FAST = 64, FCK_SLOW = 128 };

// Short of vertical, so the yaw stays defined and the camera never flips over.
const float FREECAM_PITCH_MAX = 89.f;
const float FREECAM_FAST_MULT = 4.f;
const float FREECAM_SLOW_MULT = 0.25f;
// Degrees per DirectInput count: counts, not time, so turning does not depend on the frame rate (DNMP's 0.1).
const float FREECAM_SENS_DEG = 0.1f;
// A stall (a load, a breakpoint) must not fling the camera across the level.
const float FREECAM_DT_MAX = 0.25f;
// esp_layout.h's UE1 convention: 52.5 UU to the metre.
const float FREECAM_UU_PER_METRE = 52.5f;

struct FreecamPose { float eye[3]; float yaw, pitch; };

inline float freecam_rad(float deg) { return deg * 0.017453292519943f; }

inline void freecam_axes(const FreecamPose& p, float fwd[3], float right[3], float up[3]) {
  const float sy = sinf(freecam_rad(p.yaw)), cy = cosf(freecam_rad(p.yaw));
  const float sp = sinf(freecam_rad(p.pitch)), cp = cosf(freecam_rad(p.pitch));
  fwd[0] = cp * cy;   fwd[1] = cp * sy;   fwd[2] = sp;
  right[0] = -sy;     right[1] = cy;      right[2] = 0.f;
  up[0] = -sp * cy;   up[1] = -sp * sy;   up[2] = cp;
}

inline float freecam_wrap180(float deg) {
  deg = fmodf(deg, 360.f);
  if (deg > 180.f) deg -= 360.f;
  else if (deg <= -180.f) deg += 360.f;
  return deg;
}

inline float freecam_clamp_pitch(float deg) {
  return deg > FREECAM_PITCH_MAX ? FREECAM_PITCH_MAX : deg < -FREECAM_PITCH_MAX ? -FREECAM_PITCH_MAX : deg;
}

// DirectInput counts: lX right turns right, lY grows toward the player (mouse moved down) and looks down.
inline void freecam_look(FreecamPose* p, int dx, int dy, float degPerCount) {
  p->yaw = freecam_wrap180(p->yaw + (float)dx * degPerCount);
  p->pitch = freecam_clamp_pitch(p->pitch - (float)dy * degPerCount);
}

// Both held cancel out (x4 * x0.25), as the siblings' keys do.
inline float freecam_speed_mult(int keys) {
  float m = 1.f;
  if (keys & FCK_FAST) m *= FREECAM_FAST_MULT;
  if (keys & FCK_SLOW) m *= FREECAM_SLOW_MULT;
  return m;
}

// Flight speed in UU per second: a percentage of 8 m/s, clamped to the row's range.
inline float freecam_speed_uu(int pct) {
  if (pct < FREECAM_SPEED_MIN_PCT) pct = FREECAM_SPEED_MIN_PCT;
  if (pct > FREECAM_SPEED_MAX_PCT) pct = FREECAM_SPEED_MAX_PCT;
  return (float)FREECAM_SPEED_BASE_M * FREECAM_UU_PER_METRE * (float)pct / 100.f;
}

inline float freecam_dt(double dt) {
  return dt < 0.0 ? 0.f : dt > (double)FREECAM_DT_MAX ? FREECAM_DT_MAX : (float)dt;
}

// Forward follows the look (pitch included), strafe the level right axis, Up/Down world Z. Opposing keys cancel.
inline void freecam_move(FreecamPose* p, int keys, float speedUU, float dt) {
  float f[3], r[3], u[3];
  freecam_axes(*p, f, r, u);
  const float s = speedUU * freecam_speed_mult(keys) * dt;
  const float fw = (float)((keys & FCK_FWD) != 0) - (float)((keys & FCK_BACK) != 0);
  const float sd = (float)((keys & FCK_RIGHT) != 0) - (float)((keys & FCK_LEFT) != 0);
  const float vt = (float)((keys & FCK_UP) != 0) - (float)((keys & FCK_DOWN) != 0);
  for (int k = 0; k < 3; ++k) p->eye[k] += s * (fw * f[k] + sd * r[k]);
  p->eye[2] += s * vt;
}

// Rotator units to degrees in -180..180 (the engine may hand back 64000 for a slight look down).
inline float freecam_deg_from_rot(int units) {
  int u = units & 0xFFFF;
  if (u >= 32768) u -= 65536;
  return (float)u * (360.f / 65536.f);
}
// Degrees to rotator units, written as 0..65535 as the engine keeps them.
inline int freecam_rot_from_deg(float deg) {
  const float u = deg * (65536.f / 360.f);
  const int r = (int)(u < 0.f ? u - 0.5f : u + 0.5f);
  return r & 0xFFFF;
}

// The engine's own view (PlayerCalcView's results) as a pose: its roll is dropped, the camera flies level.
inline FreecamPose freecam_pose_from_view(const float loc[3], const int rot[3]) {
  FreecamPose p;
  for (int k = 0; k < 3; ++k) p.eye[k] = loc[k];
  p.pitch = freecam_clamp_pitch(freecam_deg_from_rot(rot[0]));
  p.yaw = freecam_deg_from_rot(rot[1]);
  return p;
}
inline void freecam_view_from_pose(const FreecamPose& p, float loc[3], int rot[3]) {
  for (int k = 0; k < 3; ++k) loc[k] = p.eye[k];
  rot[0] = freecam_rot_from_deg(p.pitch);
  rot[1] = freecam_rot_from_deg(p.yaw);
  rot[2] = 0;
}

// Steering: a seeded flight with the panel shut and the game window in front.
inline int freecam_steers(int flying, int seeded, int panelOpen, int front) {
  return flying && seeded && !panelOpen && front;
}

// The movement keys, fixed (DNMP's), indexed by FCK_ bit: W, S, A, D, Space, Ctrl, Shift, Alt.
const int FREECAM_MOVE_VK[8] = { 'W', 'S', 'A', 'D', 0x20, 0x11, 0x10, 0x12 };

// The camera's own keys may not take a movement key: bound there, one press would also fly the camera. Neither may
// the Game speed key, read before the camera's keys (input.cpp).
inline bool freecam_bind_refused(int row, int vk) {
  if (row != BIND_ROW_CAMERA && row != BIND_ROW_CAMCOPY && row != BIND_ROW_CAMPASTE && row != BIND_ROW_TELEPORT &&
      row != BIND_ROW_PHOTO && row != BIND_ROW_GAMESPEED)
    return false;
  for (int i = 0; i < 8; ++i)
    if (FREECAM_MOVE_VK[i] == vk) return true;
  return false;
}

// The Debug readout's line: the pose in UU and degrees. Truncated to the buffer, never overrun.
inline void freecam_readout(const FreecamPose& p, char* out, int cap) {
  if (!out || cap <= 0) return;
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "cam x=%.0f y=%.0f z=%.0f yaw=%.1f pitch=%.1f", p.eye[0], p.eye[1],
              p.eye[2], p.yaw, p.pitch);
}
// The wheel's field of view in flight (DNMP's freecamfov.h). The frame's FOV is Pawn.FovAngle (+0x1274, read by
// URender::CreateMasterFrame 0x1027A760). The engine writes it itself; paused, nothing does, so the ConsoleFOV modifier
// would not show. The PlayerCalcView after-call writes it every flown frame once the wheel has turned.
enum { FCF_STEP_DEG = 2, FCF_NOTCH = 120, FCF_FOVANGLE_OFS = 0x1274 };
// Whole notches in the wheel's lZ (120 a notch); a part notch stays in *residue for the next frame.
inline int fcf_take_notches(int* residue, int delta) {
  *residue += delta;
  const int n = *residue / FCF_NOTCH;
  *residue -= n * FCF_NOTCH;
  return n;
}
// Away from you (positive) narrows, like a zoom-in; within the Custom FOV slider's range (DNMP).
inline float fcf_deg_after(float deg, int notches) {
  const float v = deg - (float)FCF_STEP_DEG * (float)notches;
  return v < (float)FOV_MIN_DEG ? (float)FOV_MIN_DEG : v > (float)FOV_MAX_DEG ? (float)FOV_MAX_DEG : v;
}
// At the flight's end the value before the wheel goes back only while ours is still in FovAngle; else the engine's
// own newer value stays.
inline int fcf_restore(int owned, float current, float ours) { return owned && current == ours; }
// The reflected Pawn.FovAngle must be the dword CreateMasterFrame reads, or the wheel stays off.
inline int fcf_offset_ok(int ofs) { return ofs == FCF_FOVANGLE_OFS; }
// The Debug readout's line with the field of view.
inline void freecam_readout_fov(const FreecamPose& p, float fovDeg, char* out, int cap) {
  if (!out || cap <= 0) return;
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "cam x=%.0f y=%.0f z=%.0f yaw=%.1f pitch=%.1f fov=%.1f", p.eye[0], p.eye[1],
              p.eye[2], p.yaw, p.pitch, fovDeg);
}
const int FREECAM_READOUT_MARGIN = 8;
// DNMP's C_PLAYER cyan for the marker on Duke, AA's light grey for the readout.
const unsigned FREECAM_MARKER_RGBA = 0xFF4FD8E8u;
const unsigned FREECAM_READOUT_RGBA = 0xFFC8C8C8u;
const int FREECAM_CROSS_ARM = 7, FREECAM_CROSS_GAP = 3, FREECAM_CROSS_T = 2;
const int FREECAM_MARKER_HALF = 6;   // the diamond spans 2*this + 1 rows
inline int freecam_marker_row_halfwidth(int row) {
  const int d = row - FREECAM_MARKER_HALF;
  const int w = FREECAM_MARKER_HALF - (d < 0 ? -d : d);
  return w < 0 ? 0 : w;
}

// --- Copy view / Paste view: our own tag, in this engine's units (DNMP's campos.h shape) --------------------------
// "dnf cam": DNMP's "dnmp cam" and Megaton's "cam x=" lines are another engine's units and are refused.
const float CAMPOS_MAX_UNITS = 1048576.f;   // a sanity bound, far past any level

// False, with `out` left empty, when the whole line does not fit: half a pose would parse to somewhere else.
inline bool campos_format(const FreecamPose& p, char* out, int cap) {
  if (!out || cap <= 0) return false;
  out[0] = 0;
  const int n = _snprintf_s(out, (size_t)cap, _TRUNCATE, "dnf cam x=%.1f y=%.1f z=%.1f yaw=%.2f pitch=%.2f",
                            p.eye[0], p.eye[1], p.eye[2], p.yaw, p.pitch);
  if (n < 0) { out[0] = 0; return false; }
  return true;
}

// The tag anywhere in the text, then five finite numbers in range. On refusal `io` is not touched.
inline bool campos_accept(const char* text, FreecamPose* io) {
  if (!text || !io) return false;
  const char* p = strstr(text, "dnf cam x=");
  if (!p) return false;
  float x, y, z, yaw, pitch;
  if (sscanf_s(p, "dnf cam x=%f y=%f z=%f yaw=%f pitch=%f", &x, &y, &z, &yaw, &pitch) != 5) return false;
  const float v[5] = { x, y, z, yaw, pitch };
  for (int i = 0; i < 5; ++i)
    if (!isfinite(v[i])) return false;
  for (int i = 0; i < 3; ++i)
    if (fabsf(v[i]) > CAMPOS_MAX_UNITS) return false;
  if (fabsf(yaw) > 360.f || fabsf(pitch) > FREECAM_PITCH_MAX) return false;
  io->eye[0] = x; io->eye[1] = y; io->eye[2] = z;
  io->yaw = freecam_wrap180(yaw);
  io->pitch = pitch;
  return true;
}

// Pause the game (docs/re/pause.md, freecam.md section 3): ForcePause_NoFade / ForceUnPause on the player. Only a pause
// we made is undone; the game's own is waited out, never taken over. held: we paused; confirmed: the level has read
// paused since our call; blocked: a level change or a refusing level ended it.
struct FcPause { int held, confirmed, blocked, wasArmed; unsigned level; };
enum { FCP_NONE = 0, FCP_PAUSE, FCP_REPAUSE, FCP_UNPAUSE, FCP_LET_GO, FCP_REFUSED };

// Once a frame. armed: flying with the row on. level: world_level_serial, 0 before any level. hasPlayer: the player was
// read this frame. paused: the level reads paused now. A world fault drops the player with the serial unchanged: the
// pause stays held, nothing called, until the player is back (undone through it) or a new serial says the level went.
inline int fcp_step(FcPause* s, int armed, unsigned level, int hasPlayer, int paused) {
  if (armed && !s->wasArmed) s->blocked = 0;
  s->wasArmed = armed;
  if (s->held && level != s->level) {   // the pause went with its level
    s->held = s->confirmed = 0;
    s->blocked = 1;
    return FCP_LET_GO;
  }
  if (s->held && !hasPlayer) return FCP_NONE;
  if (armed && !s->blocked && level && hasPlayer) {
    if (!s->held) {
      if (paused) return FCP_NONE;
      s->held = 1;
      s->confirmed = 0;
      s->level = level;
      return FCP_PAUSE;
    }
    if (paused) { s->confirmed = 1; return FCP_NONE; }
    if (!s->confirmed) {   // our call did not pause (DukeIntro, the change room): not asked again this flight
      s->held = 0;
      s->blocked = 1;
      return FCP_REFUSED;
    }
    s->confirmed = 0;      // the game's menu closed, or a focus resume: unpaused under us
    return FCP_REPAUSE;
  }
  if (!s->held) return FCP_NONE;
  s->held = s->confirmed = 0;
  return paused ? FCP_UNPAUSE : FCP_LET_GO;
}
// No level means no player (the first tests' form).
inline int fcp_step(FcPause* s, int armed, unsigned level, int paused) {
  return fcp_step(s, armed, level, level != 0, paused);
}

inline const char* fcp_act_text(int act) {
  switch (act) {
    case FCP_PAUSE: return "paused";
    case FCP_REPAUSE: return "paused again: the game unpaused under the flight";
    case FCP_UNPAUSE: return "unpaused";
    case FCP_LET_GO: return "let go: the game already runs, or the level changed";
    case FCP_REFUSED: return "refused: the level did not pause; not asked again this flight";
    default: return "none";
  }
}

// The cvars a flight forces to 0 (cvars.h's rule, non-saved variables only): r_UsePausedFrameCache, r_DrawHUD
// (Screenshot mode), r_DrawDistanceFog (No distance fog) and, for No culling, r_UseFarPlane, r_Occlude,
// r_UseAntiPortals (freecam.md 6, 1d, 8). All are Engine cvars with flags 0. Fullbright is RendMap (fc_hold_step).
enum { FC_CVAR_FRAMECACHE = 0, FC_CVAR_DRAWHUD, FC_CVAR_COUNT, FC_CVAR_FOG = FC_CVAR_COUNT, FC_CVAR_FARPLANE,
       FC_CVAR_OCCLUDE, FC_CVAR_ANTIPORTALS, FC_CVAR_ALL };
inline const wchar_t* freecam_cvar_name(int which) {
  static const wchar_t* const k[FC_CVAR_ALL] = { L"r_UsePausedFrameCache", L"r_DrawHUD", L"r_DrawDistanceFog",
                                                 L"r_UseFarPlane", L"r_Occlude", L"r_UseAntiPortals" };
  return k[which < 0 ? 0 : which >= FC_CVAR_ALL ? FC_CVAR_ALL - 1 : which];
}
inline bool freecam_cvar_is_float(int which) { (void)which; return false; }
// Fullbright: the game's unlit view mode: PlayerPawn.RendMap (+0x1734) is the view mode the console's RMODE n writes;
// UViewport::IsLit (0x10212220) lights only 5 and 8, so 6 (UE1's REN_PlainTex) draws unlit. Held for the whole flight.
// RendMap is saved, so freecam_rendmap_carried undoes a checkpoint's copy.
const int FREECAM_RENDMAP_UNLIT = 6, FREECAM_RENDMAP_LIT = 5;
// On a new level or a newly read player: RendMap 6 that we do not hold for this level came from a save and goes back
// to 5. Any other value is the game's own and stays; -1 (unreadable) is never touched.
inline int freecam_rendmap_carried(int value, int held, unsigned holdLevel, unsigned level) {
  return value == FREECAM_RENDMAP_UNLIT && !(held && holdLevel == level);
}

// A held engine value (RendMap) by cvars.h's rule with `forced` in place: a value the game set meanwhile is the one
// given back; a new level drops the hold (its player starts with its own); without a player nothing is written and the
// hold waits. current: the value now, -1 unreadable. Returns cvar_step's act.
inline int fc_hold_step(CvarHold* h, unsigned* holdLevel, int want, unsigned level, int hasPlayer, int current,
                        int forced, int* write) {
  if (h->held && level != *holdLevel) {
    *h = cvar_hold_init();
    return CVAR_NONE;
  }
  if (!hasPlayer || current < 0) return CVAR_NONE;
  if (want && !h->held) *holdLevel = level;
  return cvar_step(h, want, current, forced, write);
}

// The stuck-frame net (freecam.md section 7), the last resort behind d3dcap's render-target heal. While Fullbright holds
// RendMap, this many frames in a row whose Lock cleared another target than the back buffer (rstall.h rst_lock_verdict)
// give RendMap back at once and drop Fullbright for the rest of the flight.
enum { FREECAM_STUCK_FRAMES = 3 };
struct FcStuck { int frames, tripped; };
// delay: the frames in a row (at least 1); the hidden ini key [freecam] fullbright_net_delay sets it for a research gate.
inline int fc_stuck_step_n(FcStuck* s, int held, int bbBound, int delay) {
  if (!held) {
    s->frames = s->tripped = 0;
    return 0;
  }
  if (bbBound) { s->frames = 0; return 0; }
  if (s->tripped) return 0;
  if (++s->frames >= (delay < 1 ? 1 : delay)) { s->tripped = 1; return 1; }
  return 0;
}
inline int fc_stuck_step(FcStuck* s, int held, int bbBound) {
  return fc_stuck_step_n(s, held, bbBound, FREECAM_STUCK_FRAMES);
}
// Fullbright holds RendMap while flying with the row on, unless the net tripped in this flight.
inline int freecam_bright_wanted(int flying, int row, int blocked) { return flying && row && !blocked; }
// Fullbright's probe (freecam.md 7f): while RendMap 6 holds, what the centre ray meets and how it is drawn, so a gate
// can tell the surfaces RendMap reaches from those it cannot. A sample every FB_PROBE_MS, a line when the hit
// (actor, BSP surface, material) changes, at most FB_PROBE_CAP lines a flight.
const unsigned FB_PROBE_MS = 500;
const int FB_PROBE_CAP = 64;
struct FbProbe { unsigned lastMs; int sampled; const void* actor; int surf; const void* mat; int lines; };
inline void fbp_flight(FbProbe* p) {
  p->lastMs = 0; p->sampled = 0; p->actor = 0; p->surf = -2; p->mat = 0; p->lines = 0;
}
inline int fbp_due(FbProbe* p, int active, unsigned nowMs) {
  if (!active) return 0;
  if (p->sampled && nowMs - p->lastMs < FB_PROBE_MS) return 0;   // unsigned: a wrapped tick count still counts up
  p->sampled = 1;
  p->lastMs = nowMs;
  return 1;
}
inline int fbp_take(FbProbe* p, const void* actor, int surf, const void* mat) {
  if (actor == p->actor && surf == p->surf && mat == p->mat) return 0;
  p->actor = actor; p->surf = surf; p->mat = mat;
  if (p->lines >= FB_PROBE_CAP) return 0;
  ++p->lines;
  return 1;
}
// The ray stops on invisible collision (material NoRenderGFX, measured 2026-10-11): it is traced again from
// FB_PROBE_STEP uu past that hit, at most FB_PROBE_SKIPS times, so the line names the visible surface behind it.
const int FB_PROBE_SKIPS = 4;
const float FB_PROBE_STEP = 4.f;
// matName, noRender: name indices (0 unresolved); skips: re-traces made so far.
inline int fbp_skip(int matName, int noRender, int skips) {
  return noRender && matName == noRender && skips < FB_PROBE_SKIPS;
}
inline void fbp_past(const float at[3], const float dir[3], float out[3]) {
  for (int k = 0; k < 3; ++k) out[k] = at[k] + dir[k] * FB_PROBE_STEP;
}
// The draw path the class chain names: a BumpShader or LayerMaterial is deferred; anything else is forward only when
// both classes resolved, else unknown.
enum { FBP_NO_MATERIAL = -1, FBP_FORWARD = 0, FBP_DEFERRED = 1, FBP_UNKNOWN = 2 };
inline int fbp_path(int haveMat, int bumpOk, int bumpIsa, int layerOk, int layerIsa) {
  if (!haveMat) return FBP_NO_MATERIAL;
  if ((bumpOk && bumpIsa) || (layerOk && layerIsa)) return FBP_DEFERRED;
  return bumpOk && layerOk ? FBP_FORWARD : FBP_UNKNOWN;
}
// fbprobe.cpp: the flight's start resets the probe; the frame samples while Fullbright holds RendMap.
void fbprobe_flight(void);
void fbprobe_frame(void* player, int active, const FreecamPose& pose);
// The Fullbright row's hint. RendMap 6 outside the world is safe since d3dcap's render-target heal
// (freecam.md section 7).
inline const char* freecam_bright_hint(void) {
  return "The game's unlit view mode while flying: surfaces without their lighting. The game's view mode comes back "
         "after.";
}

// Our pause holds while flying with the row on, and during a photo.
inline int freecam_pause_armed(int flying, int row, int photo) { return flying && (row || photo); }

inline int freecam_cvar_forced(int which, int flying, int shot) {
  if (!flying) return 0;
  return which == FC_CVAR_FRAMECACHE ? 1 : which == FC_CVAR_DRAWHUD ? (shot != 0) : 0;
}
// The same with a photo capturing: the HUD goes for its tiles as in Screenshot mode (TakeMegaShot zeroes r_DrawHUD).
inline int freecam_cvar_forced(int which, int flying, int shot, int photo) {
  return freecam_cvar_forced(which, flying, shot || photo);
}
// Every one: the fog with No distance fog; the far plane with it or No culling; occlusion and anti-portals with No
// culling. Fullbright forces no cvar (it holds RendMap). Each is forced to 0.
inline int freecam_cvar_forced(int which, int flying, int shot, int photo, int noFog, int noCull) {
  if (!flying) return 0;
  switch (which) {
    case FC_CVAR_FOG: return noFog != 0;
    case FC_CVAR_FARPLANE: return noFog || noCull;
    case FC_CVAR_OCCLUDE: case FC_CVAR_ANTIPORTALS: return noCull != 0;
    default: return freecam_cvar_forced(which, flying, shot, photo);
  }
}
inline int freecam_cvar_forced(int which, int flying, int shot, int photo, int noFog) {
  return freecam_cvar_forced(which, flying, shot, photo, noFog, 0);
}

// --- The frame's clear in flight (the smear where nothing draws) ---------------------------------------------
// UD3DRenderDevice9::Lock (D3DDrv9 0x1002E540) clears ZBUFFER | STENCIL each frame, and TARGET only when the camera is
// outside the world, so where nothing draws the last frame stays: in flight the first Clear after Present takes it.
inline unsigned freecam_clear_flags(int flying, int firstSincePresent, unsigned count, int rects, unsigned flags) {
  if (!flying || !firstSincePresent || count || rects) return flags;
  if ((flags & 7u) != 6u) return flags;
  return flags | 1u;
}

// No culling's frame zone (freecam.md section 8e). The zone walk draws only zones its mask reaches from the frame's
// zone (FSceneNode +0x18). Frame zone 0 is the engine's outside-the-world state (no zone culling); it is written in a
// before-call handler on PlayerPawn.PreRender (the level via viewport->Actor +0x7C -> +0x6C8).
enum { FC_FRAME_VIEWPORT = 0x0, FC_FRAME_LEVEL = 0x4, FC_FRAME_ZONE = 0x18, FC_LEVELINFO_ULEVEL = 0x6C8 };
// Why a frame was or was not written, counted per reason (600 a window skipped, all in one count).
enum { FZ_WRITE = 0, FZ_OFF, FZ_UNRESOLVED, FZ_NO_FRAME, FZ_NO_VIEWPORT, FZ_VIEWPORT, FZ_LEVEL, FZ_ZONE0, FZ_COUNT };
inline int freecam_zone_reason(int flying, int noCull, int resolved, const void* frameVp, const void* vp,
                               const void* frameLevel, const void* level, int zone) {
  if (!flying || !noCull) return FZ_OFF;
  if (!resolved) return FZ_UNRESOLVED;
  if (!vp) return FZ_NO_VIEWPORT;
  if (frameVp != vp) return FZ_VIEWPORT;
  if (!level || frameLevel != level) return FZ_LEVEL;
  if (!zone) return FZ_ZONE0;
  return FZ_WRITE;
}
inline const char* freecam_zone_reason_text(int r) {
  switch (r) {
    case FZ_WRITE: return "written";
    case FZ_OFF: return "off";
    case FZ_UNRESOLVED: return "Canvas.Frame, Player or Level unresolved";
    case FZ_NO_FRAME: return "frame null";
    case FZ_NO_VIEWPORT: return "no viewport on Duke";
    case FZ_VIEWPORT: return "viewport not Duke's";
    case FZ_LEVEL: return "level mismatch";
    case FZ_ZONE0: return "zone already 0";
    default: return "?";
  }
}
inline int freecam_zone_write(int flying, int noCull, const void* frameVp, const void* vp, const void* frameLevel,
                              const void* level, int zone) {
  return freecam_zone_reason(flying, noCull, 1, frameVp, vp, frameLevel, level, zone) == FZ_WRITE;
}

// How a flight ends besides its key: the switch off, no player (menu, a load) or a new level. Not a focus loss: an
// alt-tab keeps the flight, its pause and its cvars. While the window is away the camera reads no keys or mouse
// (freecam_steers) and the cursor hold lets go (input.cpp).
enum { FCE_NONE = 0, FCE_KEY, FCE_SWITCH_OFF, FCE_NO_PLAYER, FCE_LEVEL, FCE_REFUSED };
inline int freecam_end_reason(int flying, int switchOn, int hasPlayer, int levelChanged) {
  if (!flying) return FCE_NONE;
  if (!switchOn) return FCE_SWITCH_OFF;
  if (!hasPlayer) return FCE_NO_PLAYER;
  if (levelChanged) return FCE_LEVEL;
  return FCE_NONE;
}
inline const char* freecam_end_text(int why) {
  switch (why) {
    case FCE_KEY: return "the Camera key";
    case FCE_SWITCH_OFF: return "the Free camera switch went off";
    case FCE_NO_PLAYER: return "no player (a menu or a load)";
    case FCE_LEVEL: return "a new level";
    case FCE_REFUSED: return "the camera became unavailable";
    default: return "none";
  }
}

// --- The glue (freecam.cpp), game thread only ------------------------------------------------------------------
// Frame path, after input_frame and world_frame and before pehook_frame: the key, the flight, the pause, the cvars.
// inLevel: world_frame's answer this frame.
void freecam_frame(int inLevel);
// Frame path, after the ESP and before the panel: crosshair, the marker on Duke, the readout.
void freecam_draw(int W, int H);
// Whether freecam_draw has anything to draw this frame (frame.cpp opens a scene for it).
int freecam_wants_draw(void);
// A flight is on: the input gate holds, and the aim features stand down.
int freecam_flying(void);
// Our own pause holds the level: world_frame still reads it (the ESP from the camera).
int freecam_pause_holding(void);
// Flying in Screenshot mode: none of our overlays draw.
int freecam_screenshot(void);
// Null while usable; else why the camera is unavailable.
const char* freecam_refused(void);
// The flying camera's pose: 1 while flying and seeded (the teleport's ray and Duke's new facing), else 0.
int freecam_pose_now(FreecamPose* out);
// Fullbright's RendMap given back now (a teleport may land on a checkpoint, which saves); taken again next frame.
void freecam_rendmap_release(void* player);
// The Camera key, from the WndProc while flying: RendMap goes back at once, before the frame path ends the flight, so
// a frame path that has stopped cannot keep it.
void freecam_camera_key_now(void);
// The wheel's field of view while it owns FovAngle in this flight (the ESP's projection); 0 otherwise.
int freecam_fov_now(float* deg);
// Why Fullbright is off for this flight (the stuck-frame net), or null.
const char* freecam_bright_refused(void);
// Fullbright holds RendMap now (the brightness lift draws over those frames, fblift.h).
int freecam_bright_holding(void);

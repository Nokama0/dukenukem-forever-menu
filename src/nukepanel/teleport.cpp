// The free camera's teleport (teleport.h; freecam.md 4). Script calls on Duke by ProcessEvent (scall), game thread:
// Actor.TraceEx from the camera, Actor.FindSpot for the fit (moves nothing), Pawn.PawnSetLocation (Teleporter.Accept's
// call). Then Velocity, ViewRotation and MoveTimer are set as the Teleporter sets them, found by reflection.
#include <windows.h>
#include <string.h>
#include "teleport.h"
#include "freecam.h"
#include "scall.h"
#include "scallparms.h"
#include "world.h"
#include "view.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "uwalk.h"
#include "aim.h"          // AIM_TF_BLOCKING: the player-blocking trace flags
#include "aimbot.h"       // aimbot_vk_di_button
#include "dimouse.h"
#include "input.h"
#include "canvas.h"
#include "config.h"
#include "photo.h"        // photo_capturing
#include "log.h"

// STraceHitResult (Engine.u, declaration order): Actor +0, Location +4, Normal +16 (vectors of 12). aim.cpp reads
// Location at +4 the same way; a normal that is not a unit vector is refused by tp_place.
enum { HR_LOCATION = 4, HR_NORMAL = 16 };

struct Plan {
  int have, reason, hasHit;
  float hit[3], pos[3], radius, height;
};
static Plan s_plan;

// Duke's fields, per class and level.
enum { F_RADIUS, F_HEIGHT, F_MOUNT, F_VELOCITY, F_VIEWROT, F_MOVETIMER, F_LOCATION, F_COUNT };
static const wchar_t* const kFieldNames[F_COUNT] = { L"CollisionRadius", L"CollisionHeight", L"MountParent",
                                                     L"Velocity", L"ViewRotation", L"MoveTimer", L"Location" };
static const int kFieldTypes[F_COUNT] = { UW_FLOAT, UW_FLOAT, UW_OBJECT, UW_STRUCT, UW_STRUCT, UW_FLOAT, UW_STRUCT };
static int s_ofs[F_COUNT];
static const void* s_cls;
static unsigned s_clsLevel;
static int s_fieldsOk;

static int s_mouseDown;
static unsigned s_frames, s_casts, s_byReason[TP_REASON_COUNT], s_presses, s_teleports, s_moveRefused, s_faults;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static bool resolve_fields(void* player) {
  const unsigned level = world_level_serial();
  const void* cls = 0;
  __try {
    cls = uobj_class(player);
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
  if (cls == s_cls && level == s_clsLevel) return s_fieldsOk != 0;
  s_cls = cls;
  s_clsLevel = level;
  s_fieldsOk = 1;
  for (int i = 0; i < F_COUNT; i++) {
    const int n = uname_find(kFieldNames[i]);
    UProp p = { 0, 0, UW_OTHER, 0, 0, 0 };
    __try {
      if (n) p = uprop_get(cls, n);
    } __except (av_only(GetExceptionCode())) {
      p.ok = 0;
    }
    const int sizeOk = kFieldTypes[i] != UW_STRUCT || p.elemSize >= 12;
    s_ofs[i] = p.offset;
    if (!p.ok || p.type != kFieldTypes[i] || !sizeOk) {
      s_fieldsOk = 0;
      np_log("teleport: Duke's %ls did not resolve (type %d, size %d); the teleport is unavailable", kFieldNames[i],
             p.type, p.elemSize);
    }
  }
  if (s_fieldsOk)
    np_log("teleport: Duke's fields: radius +0x%x height +0x%x MountParent +0x%x Velocity +0x%x ViewRotation +0x%x "
           "MoveTimer +0x%x Location +0x%x", s_ofs[F_RADIUS], s_ofs[F_HEIGHT], s_ofs[F_MOUNT], s_ofs[F_VELOCITY],
           s_ofs[F_VIEWROT], s_ofs[F_MOVETIMER], s_ofs[F_LOCATION]);
  return s_fieldsOk != 0;
}

static bool read_duke(const void* player, float* radius, float* height, int* mounted) {
  __try {
    memcpy(radius, (const char*)player + s_ofs[F_RADIUS], 4);
    memcpy(height, (const char*)player + s_ofs[F_HEIGHT], 4);
    *mounted = uw_ptr(player, s_ofs[F_MOUNT]) != 0;
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

// Actor.TraceEx on Duke (native 736), Duke himself ignored. 1 called (*hit: something met), 0 refused, -1 unresolved.
static int trace(void* player, const float start[3], const float end[3], int* hit, float at[3], float n[3]) {
  *hit = 0;
  const ScallFn f = scall_find(player, L"TraceEx");
  static const void* s_fn;
  static ScallParm s_start, s_end, s_flags, s_hr, s_ign, s_rv;
  if (!f.ok) return -1;
  if (f.fn != s_fn) {
    ScallParm a, b, fl, hr, ig, rv;
    if (!scall_parm(f, L"TraceStart", L"StructProperty", &a) || a.size != 12 ||
        !scall_parm(f, L"TraceEnd", L"StructProperty", &b) || b.size != 12 ||
        !scall_parm(f, L"TraceFlags", L"StructProperty", &fl) || fl.size != 4 ||
        !scall_parm(f, L"TraceHitResult", L"StructProperty", &hr) || hr.size < HR_NORMAL + 12 ||
        !scall_parm(f, L"IgnoreActor", L"ObjectProperty", &ig) || ig.size != 4 ||
        !scall_parm(f, L"ReturnValue", L"ObjectProperty", &rv) || rv.size != 4)
      return -1;
    s_start = a; s_end = b; s_flags = fl; s_hr = hr; s_ign = ig; s_rv = rv;
    s_fn = f.fn;
  }
  unsigned char parms[192];
  memset(parms, 0, sizeof parms);   // Extent 0: a line
  const unsigned flags = AIM_TF_BLOCKING;
  if (f.parmsSize > sizeof parms || !scallparms_put(parms, f.parmsSize, s_start.offset, start, 12) ||
      !scallparms_put(parms, f.parmsSize, s_end.offset, end, 12) ||
      !scallparms_put(parms, f.parmsSize, s_flags.offset, &flags, 4) ||
      !scallparms_put(parms, f.parmsSize, s_ign.offset, &player, 4))
    return -1;
  if (!scall_call(player, f, parms, sizeof parms)) return 0;
  const void* rv;
  memcpy(&rv, parms + s_rv.offset, 4);   // read after the call that filled it
  *hit = rv != 0;
  memcpy(at, parms + s_hr.offset + HR_LOCATION, 12);
  memcpy(n, parms + s_hr.offset + HR_NORMAL, 12);
  return 1;
}

// Actor.FindSpot(out AdjustedLocation, bCheckActors, bAssumeFit) on Duke (native 636): 1 fits (*io adjusted),
// 0 no room, -1 unresolved or refused.
static int find_spot(void* player, float io[3]) {
  const ScallFn f = scall_find(player, L"FindSpot");
  ScallParm loc, act, rv;
  if (!f.ok || !scall_parm(f, L"AdjustedLocation", L"StructProperty", &loc) || loc.size != 12 ||
      !scall_parm(f, L"bCheckActors", L"BoolProperty", &act) ||
      !scall_parm(f, L"ReturnValue", L"BoolProperty", &rv))
    return -1;
  unsigned char parms[96];
  memset(parms, 0, sizeof parms);   // bAssumeFit false, no override radius or height
  const unsigned one = 1;
  if (f.parmsSize > sizeof parms || !scallparms_put(parms, f.parmsSize, loc.offset, io, 12) ||
      !scallparms_put(parms, f.parmsSize, act.offset, &one, act.size < 4 ? act.size : 4))
    return -1;
  if (!scall_call(player, f, parms, sizeof parms)) return -1;
  unsigned fit = 0;
  memcpy(&fit, parms + rv.offset, rv.size < 4 ? rv.size : 4);
  if (fit) memcpy(io, parms + loc.offset, 12);
  return fit ? 1 : 0;
}

// Pawn.PawnSetLocation(NewLocation, bNoFail=false) (native 864): 1 moved, 0 the level had no room, -1 unresolved.
static int set_location(void* player, const float pos[3]) {
  const ScallFn f = scall_find(player, L"PawnSetLocation");
  ScallParm loc, rv;
  if (!f.ok || !scall_parm(f, L"NewLocation", L"StructProperty", &loc) || loc.size != 12) return -1;
  const int hasRv = scall_parm(f, L"ReturnValue", L"BoolProperty", &rv);
  unsigned char parms[64];
  memset(parms, 0, sizeof parms);
  if (f.parmsSize > sizeof parms || !scallparms_put(parms, f.parmsSize, loc.offset, pos, 12)) return -1;
  if (!scall_call(player, f, parms, sizeof parms)) return -1;
  unsigned moved = 1;
  if (hasRv) memcpy(&moved, parms + rv.offset, rv.size < 4 ? rv.size : 4);
  return moved ? 1 : 0;
}

static void cast(void* player, const FreecamPose& pose) {
  s_plan.have = 1;
  s_plan.hasHit = 0;
  int mounted = 0;
  if (!resolve_fields(player) || !read_duke(player, &s_plan.radius, &s_plan.height, &mounted)) {
    s_plan.reason = TP_UNAVAILABLE;
    return;
  }
  float f[3], r[3], u[3], end[3], at[3], n[3];
  freecam_axes(pose, f, r, u);
  for (int k = 0; k < 3; ++k) end[k] = pose.eye[k] + f[k] * TP_RAY_LEN;
  int hit = 0;
  if (trace(player, pose.eye, end, &hit, at, n) <= 0) {
    s_plan.reason = TP_UNAVAILABLE;
    return;
  }
  if (hit) {
    s_plan.hasHit = 1;
    memcpy(s_plan.hit, at, sizeof at);
  }
  TpPlace p = tp_place(hit, at, n, f, s_plan.radius, s_plan.height);
  if (p.reason == TP_OK && p.drop) {
    float below[3] = { p.pos[0], p.pos[1], p.pos[2] - TP_DROP_LEN }, fat[3], fn[3];
    int fhit = 0;
    if (trace(player, p.pos, below, &fhit, fat, fn) <= 0) {
      s_plan.reason = TP_UNAVAILABLE;
      return;
    }
    p = tp_drop(p, fhit, fat, fn, s_plan.height);
  }
  int fits = 0;
  if (p.reason == TP_OK && !mounted) {
    const int fs = find_spot(player, p.pos);
    if (fs < 0) {
      s_plan.reason = TP_UNAVAILABLE;
      return;
    }
    fits = fs;
  }
  s_plan.reason = tp_verdict(p.reason, mounted, fits);
  memcpy(s_plan.pos, p.pos, sizeof p.pos);
}

// Velocity 0, ViewRotation the camera's yaw and pitch, MoveTimer -1: Teleporter.Accept's own writes after the move.
static void after_move(void* player, const FreecamPose& pose) {
  const float zero[3] = { 0.f, 0.f, 0.f }, timer = -1.f;
  // The camera's yaw, a level pitch: the camera's could pass the game's view limit; Rotation follows from the next
  // UpdateRotation (the Teleporter's own SetRotation is not called).
  const int rot[3] = { 0, freecam_rot_from_deg(pose.yaw), 0 };
  __try {
    memcpy((char*)player + s_ofs[F_VELOCITY], zero, 12);
    memcpy((char*)player + s_ofs[F_VIEWROT], rot, 12);
    memcpy((char*)player + s_ofs[F_MOVETIMER], &timer, 4);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
  }
}

static bool duke_location(const void* player, float out[3]) {
  __try {
    memcpy(out, (const char*)player + s_ofs[F_LOCATION], 12);
    return true;
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

static void teleport_now(void* player, const FreecamPose& pose) {
  if (s_plan.reason != TP_OK) {
    np_log("teleport: refused, %s", tp_reason_text(s_plan.reason));
    return;
  }
  float from[3] = { 0.f, 0.f, 0.f }, to[3] = { 0.f, 0.f, 0.f };
  duke_location(player, from);
  freecam_rendmap_release(player);   // a checkpoint at the destination saves inside the move
  const int moved = set_location(player, s_plan.pos);
  if (moved <= 0) {
    ++s_moveRefused;
    np_log("teleport: PawnSetLocation to (%.1f,%.1f,%.1f) %s", s_plan.pos[0], s_plan.pos[1], s_plan.pos[2],
           moved < 0 ? "did not resolve or was refused" : "found no room: Duke stays");
    return;
  }
  after_move(player, pose);
  duke_location(player, to);
  ++s_teleports;
  np_log("teleport: #%u (%.1f,%.1f,%.1f) -> (%.1f,%.1f,%.1f) asked (%.1f,%.1f,%.1f), facing yaw %.1f pitch %.1f%s",
         s_teleports, from[0], from[1], from[2], to[0], to[1], to[2], s_plan.pos[0], s_plan.pos[1], s_plan.pos[2],
         pose.yaw, pose.pitch, world_paused_now() ? " (paused: physics resumes on unpause)" : "");
}

// A Teleport key bound to a mouse button: its edge is polled, as the camera's own keys are (freecam.cpp).
static int mouse_edge(void) {
  const int vk = config_bind_get(BIND_ROW_TELEPORT);
  int down = 0;
  if (bind_is_mouse(vk) && input_game_front() && !input_panel_open()) {
    down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const int b = aimbot_vk_di_button(vk);
    if (!down && b >= 0) down = dimouse_button(b);
  }
  const int edge = down && !s_mouseDown;
  s_mouseDown = down;
  return edge;
}

void teleport_frame(void) {
  const int presses = input_teleport_take() + mouse_edge();
  s_plan.have = 0;
  FreecamPose pose;
  if (!freecam_pose_now(&pose)) {
    if (presses) np_log("teleport: refused, the camera is not flying yet");
    return;
  }
  if (photo_capturing()) {
    if (presses) np_log("teleport: refused, a photo is capturing");
    return;
  }
  // No casts unless the marker shows or the key asks.
  if (!presses && !g_cfg.teleportMarker) return;
  void* player = world_player();
  if (!player) {
    s_plan.have = 1;
    s_plan.reason = TP_NO_PLAYER;
  } else {
    cast(player, pose);
  }
  ++s_casts;
  ++s_byReason[s_plan.reason];
  if (presses) {
    s_presses += (unsigned)presses;
    if (player) teleport_now(player, pose);
    else np_log("teleport: refused, %s", tp_reason_text(TP_NO_PLAYER));
  }
  if (++s_frames < 600) return;
  s_frames = 0;
  np_log("teleport: 600 frames: casts=%u ok=%u nohit=%u badnormal=%u nofloor=%u noroom=%u mounted=%u noplayer=%u "
         "unavailable=%u | presses=%u teleports=%u move refused=%u faults=%u", s_casts, s_byReason[TP_OK],
         s_byReason[TP_NO_HIT], s_byReason[TP_BAD_NORMAL], s_byReason[TP_NO_FLOOR], s_byReason[TP_NO_ROOM],
         s_byReason[TP_MOUNTED], s_byReason[TP_NO_PLAYER], s_byReason[TP_UNAVAILABLE], s_presses, s_teleports,
         s_moveRefused, s_faults);
  s_casts = 0;
  memset(s_byReason, 0, sizeof s_byReason);
}

// One quad of projected corners, as two triangles; skipped unless all four are in front of the camera.
static void quad(const float (*c)[2], const int* ok, int a, int b, int d, int e, unsigned colour) {
  if (!ok[a] || !ok[b] || !ok[d] || !ok[e]) return;
  canvas_tri((int)c[a][0], (int)c[a][1], (int)c[b][0], (int)c[b][1], (int)c[d][0], (int)c[d][1], colour);
  canvas_tri((int)c[a][0], (int)c[a][1], (int)c[d][0], (int)c[d][1], (int)c[e][0], (int)c[e][1], colour);
}

// DNMP's marker: Duke's box where he would land (four sides and the cap), or a red footprint at the hit.
void teleport_draw(int W, int H) {
  if (!s_plan.have || !g_cfg.teleportMarker) return;
  const WView v = world_view();
  const bool ok = s_plan.reason == TP_OK;
  if (!ok && !s_plan.hasHit) return;
  const float* base = ok ? s_plan.pos : s_plan.hit;
  const float r = s_plan.radius, zlo = ok ? base[2] - s_plan.height : base[2], zhi = ok ? base[2] + s_plan.height : base[2];
  // Corners 0-3 at the bottom, 4-7 at the top, counter-clockwise from (-r, -r).
  static const float kSx[4] = { -1.f, 1.f, 1.f, -1.f }, kSy[4] = { -1.f, -1.f, 1.f, 1.f };
  float c[8][2];
  int in[8];
  for (int i = 0; i < 8; ++i)
    in[i] = view_w2s(v, base[0] + kSx[i & 3] * r, base[1] + kSy[i & 3] * r, i < 4 ? zlo : zhi, W, H, &c[i][0], &c[i][1]);
  if (!ok) {
    quad(c, in, 0, 1, 2, 3, TP_REFUSED_RGBA);
    return;
  }
  quad(c, in, 0, 1, 5, 4, TP_MARK_RGBA);
  quad(c, in, 1, 2, 6, 5, TP_MARK_RGBA);
  quad(c, in, 2, 3, 7, 6, TP_MARK_RGBA);
  quad(c, in, 3, 0, 4, 7, TP_MARK_RGBA);
  quad(c, in, 4, 5, 6, 7, TP_MARK_RGBA);
}

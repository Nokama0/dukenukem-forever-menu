// Aimbot (aimbot.h). Per frame: hold rule, lock (aim.cpp's shared target), subscription. In the dnGame ProcessEvent
// slot, on the player's TriggerViewRotationAdjust (after raw mouse): the turn onto the target is added to
// PitchAdjust/YawAdjust, found by name, checked against UpdateRotation's native offsets. Writes only the event's Parms.
#include <windows.h>
#include <string.h>
#include "aimbot.h"
#include "aim.h"
#include "spawnbox.h"   // spawnbox_helper_event
#include "pehook.h"
#include "scall.h"
#include "world.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "input.h"
#include "dimouse.h"
#include "config.h"
#include "freecam.h"   // freecam_flying: the aimbot stands down in flight
#include "log.h"

// UpdateRotation's native reads (Engine.dll!0x1026ACA0): ViewRotation +0x1258, ViewRotationMode +0x1FB0, the pitch band
// +0x1FD4 < Pitch < +0x1FD8, looking up clamped to +0x1FD4. In this build the names run the other way: MinViewPitch is
// +0x1FD4 (the up limit), MaxViewPitch +0x1FD8 (the down limit). Reflection must agree, or the Aimbot is refused.
enum { NATIVE_VIEWROT = 0x1258, NATIVE_MODE = 0x1FB0, NATIVE_MAXUP = 0x1FD4, NATIVE_MINDOWN = 0x1FD8 };

static int s_sub = -1;
static const char* s_why;
static const void* s_cls;
static unsigned s_level;
static int s_ofsCalcRot, s_ofsCalcLoc;
// The target held this frame; the handler drops it at once when it stops being one (aim_track).
static const void* s_lock;
static int s_held;              // the handler has turned on s_lock at least once since it was taken
static unsigned s_limitsLevel;  // the level whose pitch limits were logged
static int s_lockClsName;
// The UFunctions whose parameters were found (a state override may alternate with the class's own: rawmouse.cpp).
struct FnParms { const void* fn; unsigned pitchOfs, yawOfs, dtOfs; };
enum { FN_CACHE = 2 };
static FnParms s_fns[FN_CACHE];
static int s_fnN, s_fnNext;

static int s_lockLogs;
static const int LOGS_PER_LEVEL = 32;
// Mounted: the first turns after each lock, per level (2026-10-11: on the SuperTurret the lock never brought the
// crosshair onto a dropship). s_turnN: this lock's lines so far.
static int s_turnLogs, s_turnN;
static const int TURN_LOGS_PER_LEVEL = 48, TURN_LOGS_PER_LOCK = 3;
// Mounted: the damped step's state (aimbot_mount_step), from each lock; events held, per 600 frames.
static AimbotMountState s_mstep;
static unsigned s_mountHolds;
// Counted per 600 frames: frames locked, events on the player while locked, events turned, locks, releases by the
// frame (key up, no target, gate) and by the handler (the target lost mid-turn), turn units added, faults.
static unsigned s_frames, s_lockFrames, s_events, s_turned, s_locks, s_releases, s_lost, s_faults;
// Counted per 600 frames: frames held back by an empty gun (aimbot_out_of_ammo), and the locks it released.
static unsigned s_noAmmoFrames, s_noAmmoReleases;
// Counted per 600 frames: frames on a mounted turret, and the player's events meanwhile, locked or not: the proof
// that UpdateRotation still raises TriggerViewRotationAdjust while mounted (docs/re/turrets.md section 2).
static unsigned s_mountFrames, s_mountEvents;
static double s_unitsPitch, s_unitsYaw;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static void refuse(const char* why) {
  if (s_why) return;
  s_why = why;
  np_log("aimbot: unavailable: %s", why);
}

static const void* class_of(const void* obj) {
  __try {
    return uobj_class(obj);
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
}

static int class_name_of(const void* o) {
  __try {
    return o && uobj_valid(o) ? uobj_name(uobj_class(o)) : 0;
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
}

static UProp prop(const void* cls, const wchar_t* name) {
  const int n = uname_find(name);
  __try {
    if (n) return uprop_get(cls, n);
  } __except (av_only(GetExceptionCode())) {
  }
  UProp none = { 0, 0, UW_OTHER, 0, 0, 0 };
  return none;
}

// The player's fields, once per class and level. A mismatch with a native offset refuses the Aimbot.
static const char* resolve_player(const void* cls) {
  const UProp vr = prop(cls, L"ViewRotation"), cr = prop(cls, L"CalcedViewRotation");
  const UProp cl = prop(cls, L"CalcedViewLocation"), mode = prop(cls, L"ViewRotationMode");
  const UProp up = prop(cls, L"MinViewPitch"), down = prop(cls, L"MaxViewPitch");
  np_log("aimbot: player class %p: ViewRotation +0x%x CalcedViewRotation +0x%x CalcedViewLocation +0x%x "
         "ViewRotationMode +0x%x MinViewPitch (up limit) +0x%x MaxViewPitch (down limit) +0x%x", cls, vr.offset, cr.offset, cl.offset,
         mode.offset, up.offset, down.offset);
  if (!vr.ok || vr.type != UW_STRUCT || vr.elemSize != 12 || vr.offset != NATIVE_VIEWROT)
    return "ViewRotation is not where UpdateRotation reads it";
  if (!mode.ok || mode.type != UW_BYTE || mode.offset != NATIVE_MODE)
    return "ViewRotationMode is not where UpdateRotation reads it";
  if (!up.ok || up.type != UW_INT || up.offset != NATIVE_MAXUP || !down.ok || down.type != UW_INT ||
      down.offset != NATIVE_MINDOWN)
    return "MinViewPitch or MaxViewPitch is not where UpdateRotation reads the pitch limits";
  if (!cr.ok || cr.type != UW_STRUCT || cr.elemSize != 12 || !cl.ok || cl.type != UW_STRUCT || cl.elemSize != 12)
    return "CalcedViewRotation or CalcedViewLocation did not resolve";
  s_ofsCalcRot = cr.offset;
  s_ofsCalcLoc = cl.offset;
  return 0;
}

struct PlayerView { int vr[3], calc[3]; float eye[3]; int maxUp, minDown; unsigned char mode; };

static bool read_view(const void* player, PlayerView* r) {
  __try {
    const char* p = (const char*)player;
    memcpy(r->vr, p + NATIVE_VIEWROT, 12);
    memcpy(r->calc, p + s_ofsCalcRot, 12);
    memcpy(r->eye, p + s_ofsCalcLoc, 12);
    memcpy(&r->maxUp, p + NATIVE_MAXUP, 4);
    memcpy(&r->minDown, p + NATIVE_MINDOWN, 4);
    r->mode = *(const unsigned char*)(p + NATIVE_MODE);
    return true;
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

static unsigned short parms_size(const void* func) {
  __try {
    return *(const unsigned short*)((const char*)func + 0x92);   // UFunction ParmsSize (scall.cpp)
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
}

// The cached layout for func, finding it the first time; null, with the Aimbot refused, when it does not resolve.
static const FnParms* find_parms(const void* func) {
  for (int i = 0; i < s_fnN; i++)
    if (s_fns[i].fn == func) return &s_fns[i];
  ScallFn f = { (void*)func, parms_size(func), 1 };
  ScallParm p, y, dt;
  const int ok = f.parmsSize && scall_parm(f, L"PitchAdjust", L"IntProperty", &p) &&
                 scall_parm(f, L"YawAdjust", L"IntProperty", &y) &&
                 scall_parm(f, L"DeltaTime", L"FloatProperty", &dt) && p.size == 4 && y.size == 4 && dt.size == 4;
  np_log("aimbot: TriggerViewRotationAdjust %p ParmsSize %u: PitchAdjust +%u YawAdjust +%u DeltaTime +%u%s", func,
         (unsigned)f.parmsSize, ok ? p.offset : 0, ok ? y.offset : 0, ok ? dt.offset : 0,
         ok ? "" : ": parameters not found, nothing written");
  if (!ok) {
    refuse("TriggerViewRotationAdjust's parameters were not found");
    return 0;
  }
  FnParms& e = s_fns[s_fnN < FN_CACHE ? s_fnN++ : (s_fnNext++ % FN_CACHE)];
  e.fn = func;
  e.pitchOfs = p.offset;
  e.yawOfs = y.offset;
  e.dtOfs = dt.offset;
  return &e;
}

static void release(const char* why) {
  if (!s_lock) return;
  s_lock = 0;
  s_held = 0;
  memset(&s_mstep, 0, sizeof s_mstep);
  ++s_releases;
  if (s_lockLogs < LOGS_PER_LEVEL) {
    s_lockLogs++;
    np_log("aimbot: released %ls (%s)", s_lockClsName ? uname_str(s_lockClsName) : L"?", why);
  }
}

static void lock_on(const void* target) {
  if (target == s_lock) return;
  if (s_lock) release("another target");
  s_lock = target;
  s_held = 0;
  s_turnN = 0;
  memset(&s_mstep, 0, sizeof s_mstep);
  s_lockClsName = class_name_of(target);
  ++s_locks;
  if (s_lockLogs < LOGS_PER_LEVEL) {
    s_lockLogs++;
    np_log("aimbot: lock on %ls (%s, smoothing %d %%, Hold key %s)", s_lockClsName ? uname_str(s_lockClsName) : L"?",
           aim_pick_label(g_cfg.aimTarget), g_cfg.aimSmooth, g_cfg.aimHold ? "on" : "off");
  }
  if (!aim_is_ship(target)) spawnbox_helper_event(target, "aimbot lock", 1);   // its mounted collision, if any
}

// After rawmouse's handler: PitchAdjust/YawAdjust already hold the turn of this event (the game's, or raw mouse's);
// ours is added to it, never put in its place.
static void on_event(void* obj, void* func, void* parms) {
  if (obj && aim_mounted() && obj == world_player()) ++s_mountEvents;
  if (!s_lock || !s_cls || s_why || !parms || obj != world_player()) return;
  ++s_events;
  if (input_panel_open() || freecam_flying()) return;
  const FnParms* fp = find_parms(func);
  if (!fp) return;
  PlayerView v;
  if (!read_view(obj, &v)) {
    ++s_faults;
    return;
  }
  if (s_limitsLevel != s_level) {   // The live limits, once a level
    s_limitsLevel = s_level;
    np_log("aimbot: pitch limits up (MinViewPitch) %d, down (MaxViewPitch) %d", v.maxUp, v.minDown);
  }
  float pt[3];
  const char* why = 0;
  if (!aim_track(s_lock, v.eye, v.calc[0], v.calc[1], pt, &why)) {
    ++s_lost;
    release(why);
    return;
  }
  s_held = 1;
  int wantP, wantY;
  aim_rotator_to(pt[0] - v.eye[0], pt[1] - v.eye[1], pt[2] - v.eye[2], &wantP, &wantY);
  const AimbotView av = { v.vr[0], v.calc[0], v.calc[1], v.maxUp, v.minDown, v.mode == 2, v.mode == 1 };
  int adjP, adjY, dP, dY;
  float dt;   // UpdateRotation's own dt, capped there at 1/30 s
  memcpy(&adjP, (const char*)parms + fp->pitchOfs, 4);
  memcpy(&adjY, (const char*)parms + fp->yawOfs, 4);
  memcpy(&dt, (const char*)parms + fp->dtOfs, 4);
  aimbot_turn(av, adjP, adjY, wantP, wantY, g_cfg.aimSmooth, (double)dt, &dP, &dY);
  if (aim_mounted()) {
    // The camera is the player's free look and the remapper's ViewOffset only the gun's queue (aim.md section 9):
    // on a rate-limited gun a damped step from the camera, held while the camera has not shown the last one; an
    // instant gun snaps as before (aimbot_mount_step).
    const int cam[2] = { v.calc[0], v.calc[1] };
    int fullP = 0, fullY = 0;
    aimbot_turn(av, adjP, adjY, wantP, wantY, 0, (double)dt, &fullP, &fullY);
    const int added = aimbot_mount_step(&s_mstep, aim_mount_instant(), cam, fullP, fullY, &dP, &dY);
    if (!added) ++s_mountHolds;
    if (s_turnN < TURN_LOGS_PER_LOCK && s_turnLogs < TURN_LOGS_PER_LEVEL) {
      s_turnN++;
      s_turnLogs++;
      int pend[3], gv[2];
      aim_mount_view_offset(pend);
      const int fromGun = aim_mount_gun_view(gv);
      np_log("aimbot: mounted turn %d on %ls: want %d,%d; rendered %d,%d, ViewRotation pitch %d; adjust in %d,%d; "
             "the gun %d,%d (%s), ViewOffset pending %d,%d; %s %d,%d (dt %.4f)", s_turnN,
             s_lockClsName ? uname_str(s_lockClsName) : L"?", wantP, wantY, v.calc[0], v.calc[1], v.vr[0], adjP,
             adjY, gv[0], gv[1], fromGun ? "its rotation" : "the camera", pend[0], pend[1],
             added ? "added" : "held, the camera has not shown the last step;", dP, dY, (double)dt);
    }
  }
  if (!dP && !dY) return;
  adjP += dP;
  adjY += dY;
  memcpy((char*)parms + fp->pitchOfs, &adjP, 4);
  memcpy((char*)parms + fp->yawOfs, &adjY, 4);
  ++s_turned;
  s_unitsPitch += dP < 0 ? -(double)dP : dP;
  s_unitsYaw += dY < 0 ? -(double)dY : dY;
}

int aimbot_engaged(void) {
  if (!g_cfg.aimbotOn || aimbot_refused()) return 0;
  const int vk = config_bind_get(BIND_ROW_AIMBOT);
  const int front = input_game_front();
  int down = 0;
  if (g_cfg.aimHold && front && bind_key_is_live(vk)) {
    down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    // The engine's exclusive DirectInput mouse may keep a mouse button from GetAsyncKeyState: its own read too.
    const int b = aimbot_vk_di_button(vk);
    if (!down && b >= 0) down = dimouse_button(b);
  }
  return aimbot_key_held(g_cfg.aimHold, vk, front, down);
}

void aimbot_frame(int inLevel, int panelOpen) {
  if (s_sub < 0) s_sub = pehook_subscribe(L"TriggerViewRotationAdjust", on_event, "aimbot");
  if (!s_why && s_sub < 0) refuse("the ProcessEvent slot's subscriber table is full");
  if (!s_why && pehook_refused()) refuse(pehook_refused());
  const unsigned level = world_level_serial();
  void* player = world_player();
  if (level != s_level) {
    // The last level's pawns are stale: forgotten without being read.
    s_level = level;
    s_lock = 0;
    s_held = 0;
    s_cls = 0;
    s_lockLogs = s_turnLogs = 0;
  }
  const int on = g_cfg.aimbotOn && !aimbot_refused();
  const void* cls = player ? class_of(player) : 0;
  if (on && inLevel && cls && cls != s_cls) {
    const char* why = resolve_player(cls);
    if (why) refuse(why);
    else s_cls = cls;
  }
  const int engaged = on && s_cls && aimbot_engaged();
  const int gate = aim_gate_now();
  const int noAmmo = aimbot_out_of_ammo(aim_weapon().kind, aim_out_of_ammo());
  const char* why = aimbot_hold_why(on, engaged, panelOpen, gate, noAmmo);
  const void* target = !why && inLevel ? aim_target_shared() : 0;
  if (why && noAmmo && strcmp(why, "out of ammo") == 0) {
    ++s_noAmmoFrames;
    if (s_lock) ++s_noAmmoReleases;
  }
  if (target) lock_on(target);
  else release(why ? why : "no target");
  if (s_lock) ++s_lockFrames;
  if (aim_mounted()) ++s_mountFrames;
  if (s_sub >= 0) pehook_enable(s_sub, on && s_cls);
  uname_dlog_flush();
  if (++s_frames < 600) return;
  s_frames = 0;
  if (g_cfg.aimbotOn || s_locks || s_turned)
    np_log("aimbot: 600 frames: %s; locked %u frame(s), %u lock(s), %u release(s) of which %u lost mid-turn; %u "
           "event(s) while locked, %u turned (|pitch| %.0f, |yaw| %.0f units); out of ammo %u frame(s), %u "
           "release(s); mounted %u frame(s), %u event(s) while mounted, %u step(s) held; %u fault(s)",
           g_cfg.aimbotOn ? (aimbot_refused() ? "unavailable" : "on") : "off", s_lockFrames, s_locks, s_releases,
           s_lost, s_events, s_turned, s_unitsPitch, s_unitsYaw, s_noAmmoFrames, s_noAmmoReleases, s_mountFrames,
           s_mountEvents, s_mountHolds, s_faults);
  s_lockFrames = s_locks = s_releases = s_lost = s_events = s_turned = s_faults = 0;
  s_noAmmoFrames = s_noAmmoReleases = s_mountFrames = s_mountEvents = s_mountHolds = 0;
  s_unitsPitch = s_unitsYaw = 0.0;
}

const void* aimbot_lock(void) { return s_held ? s_lock : 0; }

const char* aimbot_refused(void) { return s_why ? s_why : aim_refused(); }

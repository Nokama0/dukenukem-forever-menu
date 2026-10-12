// Raw mouse input (rawmouse.h). APlayerPawn::UpdateRotation (Engine.dll!0x1026ACA0) builds Pitch/Yaw from aLookUp/aTurn
// (skipping an axis ViewRotationMode, +0x1FB0, locks) and raises TriggerViewRotationAdjust; our slot sees it first.
// Fields found by reflection, checked against PlayerInput's offsets (0x10262470). Writes only the event's Parms.
#include <windows.h>
#include <string.h>
#include "rawmouse.h"
#include "pehook.h"
#include "dimouse.h"
#include "scall.h"
#include "world.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "input.h"
#include "config.h"
#include "freecam.h"   // freecam_flying: in flight the counts are the camera's
#include "log.h"

// PlayerInput's native reads (0x10262470): MouseSensitivity +0x1CA0, CurrentFOV +0x1724; UpdateRotation's
// ViewRotationMode +0x1FB0. Reflection must agree, or the option is refused.
enum { NATIVE_SENS = 0x1CA0, NATIVE_FOV = 0x1724, NATIVE_MODE = 0x1FB0, NATIVE_PLAYER = 0x12B8 };
// The mouse invert PlayerInput tests without a gamepad: byte +0x1CC0 & 2 (it flips aLookUp's sign bit).
enum { NATIVE_INVERT = 0x1CC0, NATIVE_INVERT_BIT = 2 };
// The console-typing flag PlayerInput skips the mouse on: [[Player]+0x34]+0x660 bit 0 (Player = the viewport).
enum { VP_CONSOLE = 0x34, CONSOLE_TYPING = 0x660 };

static int s_sub = -1;
static RawCarry s_carry;
static const char* s_refused;
static const void* s_cls;
static unsigned s_level;
static int s_ofsSens, s_ofsFov, s_ofsMode, s_ofsInvert, s_ofsPlayer;
static unsigned s_maskInvert;
static int s_mirror;
// The UFunctions whose parameters were found, and where Pitch and Yaw sit in each block. Two: a state override
// alternating with the class's own must not re-find (and re-log) every event.
struct FnParms { const void* fn; unsigned pitchOfs, yawOfs; };
enum { FN_CACHE = 2 };
static FnParms s_fns[FN_CACHE];
static int s_fnN, s_fnNext;
static int s_engaged;
static unsigned s_eventsThisFrame, s_frames, s_events, s_discards, s_blocked, s_faults;
static long s_countsX, s_countsY;
// The scale check (qol.md section 2 Gate): the game's own PitchAdjust/YawAdjust, read before ours replace them, beside
// ours. Below the cap with no aim assist the ratio should be 1.00.
static double s_gameYaw, s_gamePitch, s_ourYaw, s_ourPitch;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static const void* class_of(const void* obj) {
  __try {
    return uobj_class(obj);
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

// The player's fields, once per class and level. A mismatch with a native offset refuses the option.
static const char* resolve_player(const void* cls) {
  const UProp sens = prop(cls, L"MouseSensitivity"), fov = prop(cls, L"CurrentFOV");
  const UProp mode = prop(cls, L"ViewRotationMode"), inv = prop(cls, L"bInvertMouse"), pl = prop(cls, L"Player");
  np_log("rawmouse: player class %p: MouseSensitivity +0x%x CurrentFOV +0x%x ViewRotationMode +0x%x bInvertMouse "
         "+0x%x/0x%x Player +0x%x", cls, sens.offset, fov.offset, mode.offset, inv.offset, inv.bitmask, pl.offset);
  if (!sens.ok || sens.type != UW_FLOAT || sens.offset != NATIVE_SENS) return "MouseSensitivity is not where PlayerInput reads it";
  if (!fov.ok || fov.type != UW_FLOAT || fov.offset != NATIVE_FOV) return "CurrentFOV is not where PlayerInput reads it";
  if (!mode.ok || mode.type != UW_BYTE || mode.offset != NATIVE_MODE)
    return "ViewRotationMode is not where UpdateRotation reads it";
  if (!inv.ok || inv.type != UW_BOOL || inv.offset != NATIVE_INVERT || inv.bitmask != NATIVE_INVERT_BIT)
    return "bInvertMouse is not where PlayerInput reads it";
  if (!pl.ok || pl.type != UW_OBJECT || pl.offset != NATIVE_PLAYER) return "PlayerPawn.Player is not where PlayerInput reads it";
  s_ofsSens = sens.offset;
  s_ofsFov = fov.offset;
  s_ofsMode = mode.offset;
  s_ofsInvert = inv.offset;
  s_maskInvert = inv.bitmask;
  s_ofsPlayer = pl.offset;
  return 0;
}

// LevelInfo.MirrorModeEnabled (native ALevelInfo::MirrorModeEnabled 0x1027B9E0: not MP, not My Digs, the mirror
// bit) through the engine's own event path, once per level: PlayerInput flips aTurn when it is set.
static int query_mirror(void* player) {
  const void* cls = class_of(player);
  const UProp lv = cls ? prop(cls, L"Level") : UProp{ 0, 0, UW_OTHER, 0, 0, 0 };
  void* level = 0;
  if (lv.ok && lv.type == UW_OBJECT) {
    __try {
      memcpy(&level, (const char*)player + lv.offset, sizeof level);
    } __except (av_only(GetExceptionCode())) {
      level = 0;
    }
  }
  const ScallFn f = level ? scall_find(level, L"MirrorModeEnabled") : ScallFn{ 0, 0, 0 };
  ScallParm rv;
  unsigned char parms[64] = {};
  if (!f.ok || f.parmsSize > sizeof parms || !scall_parm(f, L"ReturnValue", L"BoolProperty", &rv) ||
      !scall_call(level, f, parms, sizeof parms)) {
    np_log("rawmouse: LevelInfo.MirrorModeEnabled not called (level %p); mirror mode taken as off", level);
    return 0;
  }
  unsigned v = 0;
  memcpy(&v, parms + rv.offset, rv.size < 4 ? rv.size : 4);
  np_log("rawmouse: mirror mode %s (level %p)", v ? "on: yaw flipped" : "off", level);
  return v != 0;
}

struct PlayerRead { float sens, fov; unsigned char mode; unsigned invert; int typing; };

static bool read_player(const void* player, PlayerRead* r) {
  __try {
    memcpy(&r->sens, (const char*)player + s_ofsSens, 4);
    memcpy(&r->fov, (const char*)player + s_ofsFov, 4);
    r->mode = *((const unsigned char*)player + s_ofsMode);
    r->invert = *(const unsigned*)((const char*)player + s_ofsInvert) & s_maskInvert;
    const char* vp = *(const char* const*)((const char*)player + s_ofsPlayer);
    const char* con = vp ? *(const char* const*)(vp + VP_CONSOLE) : 0;
    r->typing = con ? (*(const unsigned char*)(con + CONSOLE_TYPING) & 1) : 0;
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

// The cached layout for func, finding it the first time; null, with the option refused, when it does not resolve.
static const FnParms* find_parms(const void* func) {
  for (int i = 0; i < s_fnN; i++)
    if (s_fns[i].fn == func) return &s_fns[i];
  ScallFn f = { (void*)func, parms_size(func), 1 };
  ScallParm p, y, r, dt;
  const int ok = f.parmsSize && scall_parm(f, L"PitchAdjust", L"IntProperty", &p) &&
                 scall_parm(f, L"YawAdjust", L"IntProperty", &y) && scall_parm(f, L"RollAdjust", L"IntProperty", &r) &&
                 scall_parm(f, L"DeltaTime", L"FloatProperty", &dt) && p.size == 4 && y.size == 4;
  np_log("rawmouse: TriggerViewRotationAdjust %p ParmsSize %u: PitchAdjust +%u YawAdjust +%u RollAdjust +%u DeltaTime "
         "+%u%s", func, (unsigned)f.parmsSize, ok ? p.offset : 0, ok ? y.offset : 0, ok ? r.offset : 0,
         ok ? dt.offset : 0, ok ? "" : ": parameters not found, nothing written");
  if (!ok) {
    s_refused = "TriggerViewRotationAdjust's parameters were not found";
    return 0;
  }
  FnParms& e = s_fns[s_fnN < FN_CACHE ? s_fnN++ : (s_fnNext++ % FN_CACHE)];
  e.fn = func;
  e.pitchOfs = p.offset;
  e.yawOfs = y.offset;
  return &e;
}

static void on_event(void* obj, void* func, void* parms) {
  if (!g_cfg.rawMouse || freecam_flying() || s_refused || !s_cls || !parms || obj != world_player()) return;
  const FnParms* fp = find_parms(func);
  if (!fp) return;
  if (!dimouse_tracking_mouse()) {   // no counts to give: the game's own turn stands
    static int s_untrackedLogged;
    if (!s_untrackedLogged++) np_log("rawmouse: the engine's mouse is not tracked; the game's own turn passes through");
    return;
  }
  ++s_eventsThisFrame;
  ++s_events;
  RawIn in;
  dimouse_counts_take(&in.dx, &in.dy);
  PlayerRead pr;
  if (!read_player(obj, &pr)) { ++s_faults; return; }
  in.sens = pr.sens;
  in.fov = pr.fov;
  in.invert = pr.invert != 0;
  in.mirror = s_mirror;
  in.lockPitch = pr.mode == 2;
  in.lockYaw = pr.mode == 1;
  in.blocked = input_panel_open() || pr.typing;
  if (in.blocked) ++s_blocked;
  s_countsX += in.dx < 0 ? -in.dx : in.dx;
  s_countsY += in.dy < 0 ? -in.dy : in.dy;
  int pitch, yaw, gamePitch, gameYaw;
  memcpy(&gamePitch, (const char*)parms + fp->pitchOfs, 4);
  memcpy(&gameYaw, (const char*)parms + fp->yawOfs, 4);
  raw_turn(&s_carry, in, &pitch, &yaw);
  s_gameYaw += gameYaw < 0 ? -(double)gameYaw : gameYaw;
  s_gamePitch += gamePitch < 0 ? -(double)gamePitch : gamePitch;
  s_ourYaw += yaw < 0 ? -(double)yaw : yaw;
  s_ourPitch += pitch < 0 ? -(double)pitch : pitch;
  memcpy((char*)parms + fp->pitchOfs, &pitch, 4);
  memcpy((char*)parms + fp->yawOfs, &yaw, 4);
}

void rawmouse_frame(int inLevel) {
  if (s_sub < 0) s_sub = pehook_subscribe(L"TriggerViewRotationAdjust", on_event, "rawmouse");
  if (!s_refused && pehook_refused()) s_refused = pehook_refused();
  // Stands down in flight: the free camera takes the counts (freecam_frame), and Duke's own turn reads zeros.
  const int flying = freecam_flying();
  pehook_enable(s_sub, g_cfg.rawMouse && !s_refused && !flying);
  // Counts no event took this frame (paused, a menu, the option off) are dropped, never turned later.
  if (!s_eventsThisFrame && !flying) {
    int dx, dy;
    dimouse_counts_take(&dx, &dy);
    if (dx || dy) ++s_discards;
    s_carry = raw_carry_init();
  }
  s_eventsThisFrame = 0;
  const int on = g_cfg.rawMouse && !s_refused;
  if (on != s_engaged) {
    s_engaged = on;
    s_carry = raw_carry_init();
    np_log("rawmouse: %s", on ? "on: the view turns by the mouse's counts" : "off: the game's own mouse path");
  }
  void* player = world_player();
  const void* cls = player ? class_of(player) : 0;
  const unsigned level = world_level_serial();
  // query_mirror makes a script call, so nothing while paused (the next in-level frame resolves it).
  if (on && cls && inLevel && (cls != s_cls || level != s_level)) {
    s_level = level;
    const char* why = resolve_player(cls);
    if (why) {
      s_refused = why;
      np_log("rawmouse: unavailable: %s", why);
      s_cls = 0;
    } else {
      s_cls = cls;
      s_mirror = query_mirror(player);
    }
  }
  if (++s_frames < 600) return;
  s_frames = 0;
  if (on || s_events)
    np_log("rawmouse: 600 frames: %u event(s), counts |x| %ld |y| %ld, %u blocked (panel or console), %u frame(s) "
           "of counts dropped, %u fault(s)", s_events, s_countsX, s_countsY, s_blocked, s_discards, s_faults);
  if (s_ourYaw || s_gameYaw || s_ourPitch || s_gamePitch)
    np_log("rawmouse: game yaw %.0f pitch %.0f, ours %.0f %.0f, counts %ld %ld, G/O yaw %.3f pitch %.3f", s_gameYaw,
           s_gamePitch, s_ourYaw, s_ourPitch, s_countsX, s_countsY, s_ourYaw ? s_gameYaw / s_ourYaw : 0.0,
           s_ourPitch ? s_gamePitch / s_ourPitch : 0.0);
  s_events = s_blocked = s_discards = 0;
  s_countsX = s_countsY = 0;
  s_gameYaw = s_gamePitch = s_ourYaw = s_ourPitch = 0.0;
}

const char* rawmouse_refused(void) { return s_refused; }

// The free camera's glue (freecam.h holds the rules; freecam.md the levers). View: PlayerCalcView's two out-parameters
// are overwritten after the engine's own code has written them, via the dnGame ProcessEvent slot (after phase). Input:
// input.cpp's flight gate. Pause: ForcePause_NoFade/ForceUnPause. Cvars: cvars.h's rule. Game thread only.
#include <windows.h>
#include <string.h>
#include "freecam.h"
#include "pehook.h"
#include "scall.h"
#include "cvars.h"
#include "world.h"
#include "view.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "uwalk.h"
#include "input.h"
#include "dimouse.h"
#include "aimbot.h"       // aimbot_vk_di_button: a mouse-bound key's DirectInput button
#include "clipboard.h"
#include "canvas.h"
#include "esp_layout.h"   // esp_draw_label, esp_label_x, ESP_LABEL_GAP
#include "font.h"
#include "config.h"
#include "teleport.h"   // teleport_draw: the landing marker under the crosshair
#include "photo.h"      // photo_busy, photo_capturing: every tile from one pose, frozen
#include "d3dcap.h"     // the frame clear in flight, the frame trace
#include "d3dtrace.h"
#include "rtclear.h"    // d3dres_bucket: the feedback target's surface
#include "fblift.h"     // the brightness lift's cost, in the 600-frame line
#include "log.h"
#include "mpgate.h"

static int s_sub = -1;
static const char* s_refused;
static int s_flying, s_seeded;
static FreecamPose s_pose;
static unsigned s_level;        // world_level_serial when the flight started
static unsigned s_focusLosses;  // input_focus_losses as last seen: a change while flying starts the focus watch
static LARGE_INTEGER s_qpf, s_lastQpc;
static int s_mouseKeyDown[3];   // Camera, Copy view, Paste view bound to a mouse button: last frame's state

// The wheel's field of view (freecam.h fcf_*): Pawn.FovAngle by reflection, used only at CreateMasterFrame's +0x1274.
// Owned from the flight's first notch to its end; the value before it goes back at the end.
static const void* s_fovCls;
static int s_fovOfs = -1;
static int s_fovOwned, s_fovResidue;
static float s_fovDeg, s_fovSaved, s_fovWritten;
static unsigned s_fovNotches, s_fovLogs, s_fovFaults;

// PlayerCalcView's out-parameters, found by name per UFunction (a state override has its own block).
struct CalcParms { const void* fn; unsigned locOfs, rotOfs; int ok; };
static CalcParms s_calc[2];
static int s_calcN, s_calcNext;

static FcPause s_fp;
static int s_cvFound;
static CvarRef s_cv[FC_CVAR_ALL];
static CvarHold s_cvHold[FC_CVAR_ALL];

// Per 600 frames: the overrides written, frames flown and steered; cumulative: flights, pause acts, cvar writes.
static unsigned s_frames, s_overrides, s_flownFrames, s_steerFrames;
static long s_countsX, s_countsY;
static unsigned s_flights, s_pauses, s_repauses, s_unpauses, s_letGo, s_pauseRefused, s_cvForces, s_cvRestores;
static unsigned s_copies, s_pastes, s_pastesRefused, s_markerFaults;
static unsigned s_noOverrideFrames;   // flown frames since the last override: the slot never fired
// Measurement only: frames left to log the pause after an alt-tab during a flight, and again after the
// window is back in front.
static int s_focusWatch, s_backWatch, s_awaitReturn, s_wasFront = 1;
// The camera's zone (Actor.GetPointRegion on Duke at the eye), every 30 flown frames, logged on a change with the
// frame clear's counts so the gate sees where the engine clears and where we do.
static int s_zoneName = -1, s_zoneLeaf = -2, s_zoneNum = -2, s_zoneFrames, s_zoneLines;
static unsigned s_clFirsts, s_clAdded, s_clEngine;
// The frame trace (the smear, freecam.md section 1d): armed at a flight's first seeded frame and every 600 flown
// frames after; each logs one whole engine frame.
static int s_traceArmed, s_traceFrames;
static unsigned s_traces;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static void log_pose(const char* what) {
  np_log("freecam: %s eye=(%.1f,%.1f,%.1f) yaw=%.1f pitch=%.1f", what, s_pose.eye[0], s_pose.eye[1], s_pose.eye[2],
         s_pose.yaw, s_pose.pitch);
}

static bool read_fov(const void* player, float* out) {
  if (s_fovOfs < 0) return false;
  __try {
    memcpy(out, (const char*)player + s_fovOfs, 4);
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_fovFaults;
    return false;
  }
}

// A dword the engine writes itself (CalculateFOV, SetFOVAngle).
static bool write_fov(void* player, float v) {
  if (s_fovOfs < 0) return false;
  __try {
    memcpy((char*)player + s_fovOfs, &v, 4);
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_fovFaults;
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

static const CalcParms* calc_parms(const void* func) {
  for (int i = 0; i < s_calcN; i++)
    if (s_calc[i].fn == func) return &s_calc[i];
  ScallFn f = { (void*)func, parms_size(func), 1 };
  ScallParm loc, rot;
  const int ok = f.parmsSize && scall_parm(f, L"CameraLocation", L"StructProperty", &loc) &&
                 scall_parm(f, L"CameraRotation", L"StructProperty", &rot) && loc.size == 12 && rot.size == 12;
  np_log("freecam: PlayerCalcView %p ParmsSize %u: CameraLocation +%u CameraRotation +%u%s", func,
         (unsigned)f.parmsSize, ok ? loc.offset : 0, ok ? rot.offset : 0, ok ? "" : ": not found, nothing written");
  CalcParms& e = s_calc[s_calcN < 2 ? s_calcN++ : (s_calcNext++ % 2)];
  e.fn = func;
  e.locOfs = ok ? loc.offset : 0;
  e.rotOfs = ok ? rot.offset : 0;
  e.ok = ok;
  return &e;
}

// After the native: CameraLocation and CameraRotation are final and about to be copied into CalcedView* and the
// renderer (SetupViewInfo 0x10226AD0). Only the player's own event; only the event's own Parms are written.
static void on_calcview(void* obj, void* func, void* parms) {
  if (!s_flying || !parms || obj != world_player()) return;
  const CalcParms* cp = calc_parms(func);
  if (!cp->ok) {
    s_refused = "PlayerCalcView's parameters were not found";
    return;
  }
  char* const p = (char*)parms;
  if (!s_seeded) {
    float loc[3];
    int rot[3];
    memcpy(loc, p + cp->locOfs, sizeof loc);
    memcpy(rot, p + cp->rotOfs, sizeof rot);
    s_pose = freecam_pose_from_view(loc, rot);
    s_seeded = 1;
    np_log("freecam: seeded from the engine's view: loc=(%.1f,%.1f,%.1f) rot=(%d,%d,%d)", loc[0], loc[1], loc[2],
           rot[0], rot[1], rot[2]);
  }
  float loc[3];
  int rot[3];
  freecam_view_from_pose(s_pose, loc, rot);
  memcpy(p + cp->locOfs, loc, sizeof loc);
  memcpy(p + cp->rotOfs, rot, sizeof rot);
  // After the tick's CalculateFOV and before CreateMasterFrame reads it: this frame, paused or not, has our FOV.
  if (s_fovOwned && write_fov(obj, s_fovDeg)) s_fovWritten = s_fovDeg;
  ++s_overrides;
  s_noOverrideFrames = 0;
}

// No culling's frame zone (freecam.h freecam_zone_write, freecam.md section 8e). PreRender's Canvas parameter, Canvas.Frame
// (an IntProperty holding the FSceneNode, expected at +0x98: UCanvas::Update 0x1021A890), Duke's Player (the viewport)
// and XLevel, all by reflection; every engine read and the one write under a guard. Game thread (StartRendering).
static int s_zoneSub = -1;
static const void* s_zoneFn;
static ScallParm s_zoneCanvas;
static const void* s_zoneCanvasCls;
static int s_zoneFrameOfs, s_zonePlayerOfs, s_zoneLevelOfs, s_zoneUlevelOfs = -1;
static const void* s_zonePlayerCls;
static const void* s_zoneLevelCls;
static unsigned s_zoneBy[FZ_COUNT], s_zoneFaults;
static int s_zoneLast;          // the zone replaced last
static unsigned s_zoneFlight;   // the flight whose values were logged

static int prop_ofs(const void* cls, const wchar_t* name, int type) {
  const int n = uname_find(name);
  UProp p = { 0, 0, UW_OTHER, 0, 0, 0 };
  __try {
    if (n) p = uprop_get(cls, n);
  } __except (av_only(GetExceptionCode())) {
    p.ok = 0;
  }
  return p.ok && p.type == type ? p.offset : -1;
}

static void on_prerender(void* obj, void* func, void* parms) {
  if (!s_flying || !g_cfg.freecamNoCull || !parms || obj != world_player()) return;
  if (func != s_zoneFn) {
    ScallFn f = { func, 0, 1 };
    __try {
      f.parmsSize = *(const unsigned short*)((const char*)func + 0x92);
    } __except (av_only(GetExceptionCode())) {
      return;
    }
    if (!scall_parm(f, L"Canvas", L"ObjectProperty", &s_zoneCanvas) || s_zoneCanvas.size != 4) {
      ++s_zoneBy[FZ_UNRESOLVED];
      return;
    }
    s_zoneFn = func;
  }
  const void* canvas;
  memcpy(&canvas, (const char*)parms + s_zoneCanvas.offset, 4);
  if (!canvas) { ++s_zoneBy[FZ_NO_FRAME]; return; }
  __try {
    const void* ccls = uobj_class(canvas);
    if (ccls != s_zoneCanvasCls) {
      s_zoneCanvasCls = ccls;
      s_zoneFrameOfs = prop_ofs(ccls, L"Frame", UW_INT);
      np_log("freecam: Canvas.Frame +0x%x (0x98 read in UCanvas::Update)", s_zoneFrameOfs);
    }
    const void* pcls = uobj_class(obj);
    if (pcls != s_zonePlayerCls) {
      s_zonePlayerCls = pcls;
      s_zonePlayerOfs = prop_ofs(pcls, L"Player", UW_OBJECT);
      s_zoneLevelOfs = prop_ofs(pcls, L"Level", UW_OBJECT);
      np_log("freecam: zone check fields: Player +0x%x, Level +0x%x (CreateMasterFrame reads +0x7c)", s_zonePlayerOfs,
             s_zoneLevelOfs);
    }
    const char* levelInfo = s_zoneLevelOfs >= 0 ? *(const char* const*)((const char*)obj + s_zoneLevelOfs) : 0;
    if (levelInfo) {
      const void* lcls = uobj_class(levelInfo);
      if (lcls != s_zoneLevelCls) {
        s_zoneLevelCls = lcls;
        // Reflection first; else the offset CreateMasterFrame itself reads (freecam.h FC_LEVELINFO_ULEVEL).
        const int x = prop_ofs(lcls, L"XLevel", UW_OBJECT);
        s_zoneUlevelOfs = x >= 0 ? x : FC_LEVELINFO_ULEVEL;
        np_log("freecam: LevelInfo's ULevel at +0x%x (%s)", s_zoneUlevelOfs, x >= 0 ? "LevelInfo.XLevel" :
               "CreateMasterFrame's +0x6c8, XLevel not reflected");
      }
    }
    const int resolved = s_zoneFrameOfs >= 0 && s_zonePlayerOfs >= 0 && s_zoneLevelOfs >= 0;
    char* frame = resolved ? *(char* const*)((const char*)canvas + s_zoneFrameOfs) : 0;
    if (resolved && !frame) { ++s_zoneBy[FZ_NO_FRAME]; return; }
    const void* vp = resolved ? *(const void* const*)((const char*)obj + s_zonePlayerOfs) : 0;
    const void* lv = levelInfo ? *(const void* const*)(levelInfo + s_zoneUlevelOfs) : 0;
    const void* fvp = frame ? *(const void* const*)(frame + FC_FRAME_VIEWPORT) : 0;
    const void* flv = frame ? *(const void* const*)(frame + FC_FRAME_LEVEL) : 0;
    const int zone = frame ? *(const int*)(frame + FC_FRAME_ZONE) : 0;
    const int why = freecam_zone_reason(1, 1, resolved, fvp, vp, flv, lv, zone);
    if (s_zoneFlight != s_flights) {   // once a flight: what was compared
      s_zoneFlight = s_flights;
      np_log("freecam: zone check, flight %u: frame %p viewport %p (Duke's %p) level %p (Duke's %p) zone %d: %s",
             s_flights, frame, fvp, vp, flv, lv, zone, freecam_zone_reason_text(why));
    }
    ++s_zoneBy[why];
    if (why != FZ_WRITE) return;
    *(int*)(frame + FC_FRAME_ZONE) = 0;
    s_zoneLast = zone;
  } __except (av_only(GetExceptionCode())) {
    ++s_zoneFaults;
  }
}

// Pawn.FovAngle's offset on the player's class, -1 unless it is CreateMasterFrame's dword (logged once a class).
static int fov_offset(void* player) {
  const void* cls = 0;
  __try {
    cls = uobj_class(player);
  } __except (av_only(GetExceptionCode())) {
    return -1;
  }
  if (cls != s_fovCls) {
    s_fovCls = cls;
    const int ofs = prop_ofs(cls, L"FovAngle", UW_FLOAT);
    s_fovOfs = fcf_offset_ok(ofs) ? ofs : -1;
    np_log("freecam: Pawn.FovAngle +0x%x (%s)", ofs, s_fovOfs >= 0 ? "CreateMasterFrame's +0x1274: the wheel sets it"
                                                                   : "not +0x1274: the wheel's FOV stays off");
  }
  return s_fovOfs;
}

// The wheel while steering: whole notches change the camera's FOV; the first one in a flight takes FovAngle over.
// Drained every frame so no notch turned outside a flight lands in one.
static void fov_wheel_frame(void* player, int steering) {
  const int dz = dimouse_wheel_take();
  if (!s_flying || !steering || !player) {
    s_fovResidue = 0;
    return;
  }
  const int n = fcf_take_notches(&s_fovResidue, dz);
  if (!n || fov_offset(player) < 0) return;
  if (!s_fovOwned) {
    float cur;
    if (!read_fov(player, &cur)) return;
    s_fovSaved = s_fovDeg = s_fovWritten = cur;
    s_fovOwned = 1;
  }
  const float was = s_fovDeg;
  s_fovDeg = fcf_deg_after(s_fovDeg, n);
  s_fovNotches += (unsigned)(n < 0 ? -n : n);
  if (++s_fovLogs <= 32) np_log("freecam: wheel %+d notch(es): fov %.1f -> %.1f", n, was, s_fovDeg);
}

// The flight's end: FovAngle back to the value before the wheel while ours is still in it; a new level or a lost
// player keeps nothing of ours to give back.
static void fov_release(int why) {
  if (!s_fovOwned) return;
  s_fovOwned = 0;
  s_fovResidue = 0;
  void* player = world_player();
  float cur = 0.f;
  if (why == FCE_LEVEL || why == FCE_NO_PLAYER || !player || !read_fov(player, &cur)) {
    np_log("freecam: wheel fov: nothing given back (%s); %u notch(es) this session", freecam_end_text(why),
           s_fovNotches);
    return;
  }
  if (fcf_restore(1, cur, s_fovWritten)) {
    const bool ok = write_fov(player, s_fovSaved);
    np_log("freecam: wheel fov: FovAngle %.1f -> %.1f %s", cur, s_fovSaved, ok ? "given back" : "NOT written");
  } else {
    np_log("freecam: wheel fov: FovAngle %.1f is the engine's own (ours %.1f): left", cur, s_fovWritten);
  }
}

// What the Debug readout shows: the wheel's FOV, else FovAngle as it is.
static bool fov_display(float* deg) {
  if (s_fovOwned) { *deg = s_fovDeg; return true; }
  void* player = world_player();
  return player && fov_offset(player) >= 0 && read_fov(player, deg);
}

// Fullbright's RendMap (freecam.h fc_hold_step, FREECAM_RENDMAP_UNLIT). PlayerPawn.RendMap by reflection (an
// IntProperty: RMODE's dword store), read and written under a guard: a value the engine writes itself (RMODE,
// SetCameraStyle(0)).
static CvarHold s_rendHold;
static unsigned s_rendLevel;
static FcStuck s_stuck;
static int s_brightBlocked;   // the stuck-frame net tripped in this flight
static unsigned s_stuckTrips;
static const void* s_rendCls;
static int s_rendOfs = -1, s_rendType;
static unsigned s_rendWrites;

static int read_rendmap(void* player) {
  __try {
    const void* cls = uobj_class(player);
    if (cls != s_rendCls) {
      s_rendCls = cls;
      s_rendOfs = prop_ofs(cls, L"RendMap", UW_INT);
      s_rendType = UW_INT;
      if (s_rendOfs < 0) {
        s_rendOfs = prop_ofs(cls, L"RendMap", UW_BYTE);
        s_rendType = UW_BYTE;
      }
      np_log("freecam: PlayerPawn.RendMap +0x%x (%s; RMODE writes +0x1734)", s_rendOfs,
             s_rendOfs < 0 ? "not found" : s_rendType == UW_INT ? "int" : "byte");
    }
    if (s_rendOfs < 0) return -1;
    const char* p = (const char*)player + s_rendOfs;
    return s_rendType == UW_INT ? *(const int*)p : *(const unsigned char*)p;
  } __except (av_only(GetExceptionCode())) {
    return -1;
  }
}

static bool write_rendmap(void* player, int v) {
  if (s_rendOfs < 0) return false;
  __try {
    char* p = (char*)player + s_rendOfs;
    if (s_rendType == UW_INT) *(int*)p = v; else *(unsigned char*)p = (unsigned char)v;
    return true;
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

static void rendmap_step(void* player, int want, const char* why) {
  const int current = player ? read_rendmap(player) : -1;
  int w = 0;
  const int act = fc_hold_step(&s_rendHold, &s_rendLevel, want, world_level_serial(), player != 0, current,
                               FREECAM_RENDMAP_UNLIT, &w);
  if (act == CVAR_NONE) return;
  const bool ok = write_rendmap(player, w);
  if (++s_rendWrites <= 64)
    np_log("freecam: Fullbright: RendMap %d -> %d %s (%s)", current, w, ok ? "written" : "NOT written",
           act == CVAR_RESTORE ? why : "set");
}

static int bright_row(void) { return g_cfg.freecamBright; }

static void rendmap_frame(void* player) {
  const int ok = d3dcap_lock_on_bb_take();   // -1 unknown: never counts as stuck
  if (fc_stuck_step_n(&s_stuck, s_rendHold.held, ok != 0, g_cfg.freecamNetDelay)) {
    s_brightBlocked = 1;
    ++s_stuckTrips;
    np_log("freecam: Fullbright: %d frames whose Lock missed the back buffer: the engine stays on another target in "
           "RendMap %d; given back now and Fullbright off for this flight (trip %u)", g_cfg.freecamNetDelay,
           FREECAM_RENDMAP_UNLIT, s_stuckTrips);
    rendmap_step(player, 0, "given back: the stuck-frame net");
    return;
  }
  rendmap_step(player, freecam_bright_wanted(s_flying, bright_row(), s_brightBlocked),
               "given back: the flight or the row ended");
}

// A checkpoint in an unpaused flight saves RendMap 6 with the pawn (freecam.h freecam_rendmap_carried): once per level
// and per player, before the frame's hold step, a 6 we do not hold goes back to 5 through the same write.
static unsigned s_carryLevel;
static void* s_carryPlayer;
static void rendmap_carried(void* player) {
  const unsigned level = world_level_serial();
  if (!player || (level == s_carryLevel && player == s_carryPlayer)) return;
  s_carryLevel = level;
  s_carryPlayer = player;
  const int v = read_rendmap(player);
  if (!freecam_rendmap_carried(v, s_rendHold.held, s_rendLevel, level)) return;
  const bool ok = write_rendmap(player, FREECAM_RENDMAP_LIT);
  np_log("freecam: Fullbright: RendMap %d -> %d %s (carried from a save, level %u)", v, FREECAM_RENDMAP_LIT,
         ok ? "written" : "NOT written", level);
}

int freecam_bright_holding(void) { return s_rendHold.held; }

void freecam_camera_key_now(void) {
  if (s_flying && s_rendHold.held) rendmap_step(world_player(), 0, "given back at the Camera key");
}

const char* freecam_bright_refused(void) {
  return s_brightBlocked ? "Off for this flight: the game stopped drawing in its unlit mode here. The next flight "
                           "tries again." : 0;
}

// Before a teleport (teleport.cpp): a checkpoint at the destination saves at once (TriggerCheckpoint.Trigger calls
// SaveGame inside the move's touch), and RendMap is a saved PlayerPawn property. Fullbright takes it again next frame.
void freecam_rendmap_release(void* player) { rendmap_step(player, 0, "given back before a teleport"); }

static void start(void) {
  s_flying = 1;
  s_brightBlocked = 0;   // a new flight tries Fullbright again
  fbprobe_flight();
  s_traceFrames = 0;   // a trace at this flight's first seeded frame
  s_seeded = 0;
  s_level = world_level_serial();
  s_focusLosses = input_focus_losses();
  s_noOverrideFrames = 0;
  ++s_flights;
  input_set_flight(1);
  np_log("freecam: on (flight %u), waiting for PlayerCalcView to seed from; pause row %d, screenshot mode %d, "
         "speed %d%%", s_flights, g_cfg.freecamPause, g_cfg.freecamShot, g_cfg.freecamSpeedPct);
}

static void end(int why) {
  if (!s_flying) return;
  s_flying = 0;
  input_set_flight(0);
  fov_release(why);
  char what[96];
  _snprintf_s(what, sizeof what, _TRUNCATE, "off (%s), camera handed back at", freecam_end_text(why));
  if (s_seeded) log_pose(what);
  else np_log("freecam: off (%s) before PlayerCalcView seeded it", freecam_end_text(why));
  d3dcap_watch_log("the flight's end");
  s_seeded = 0;
}

// A camera key bound to a mouse button: under the shared DirectInput mouse its messages may be held back from the
// window, so its edge is polled, as the Aimbot button is.
static int mouse_key_edge(int slot, int vk) {
  int down = 0;
  if (bind_is_mouse(vk) && input_game_front() && !input_panel_open()) {
    down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const int b = aimbot_vk_di_button(vk);
    if (!down && b >= 0) down = dimouse_button(b);
  }
  const int edge = down && !s_mouseKeyDown[slot];
  s_mouseKeyDown[slot] = down;
  return edge;
}

static int read_keys(void) {
  int keys = 0;
  for (int i = 0; i < 8; ++i)
    if (GetAsyncKeyState(FREECAM_MOVE_VK[i]) & 0x8000) keys |= 1 << i;
  return keys;
}

// Copy view / Paste view: on the game thread, which owns the window the clipboard calls name. A refused paste
// leaves the camera where it is.
static void view_keys(int copies, int pastes) {
  if (!s_flying || !s_seeded || photo_capturing()) return;
  if (copies) {
    char line[128];
    const bool ok = campos_format(s_pose, line, sizeof line) && clipboard_write_text(line);
    ++s_copies;
    np_log("freecam: copy view %s (%d bytes)", ok ? "on the clipboard" : "FAILED", (int)strlen(line));
  }
  if (pastes) {
    char text[256];
    const bool read = clipboard_read_text(text, sizeof text);
    FreecamPose p = s_pose;
    if (read && campos_accept(text, &p)) {
      s_pose = p;
      ++s_pastes;
      log_pose("paste view, flown to");
    } else {
      // Never the text itself: the clipboard may hold anything the player copied.
      ++s_pastesRefused;
      np_log("freecam: paste view refused: %d byte(s) of text, dnf tag %s", read ? (int)strlen(text) : 0,
             read && strstr(text, "dnf cam x=") ? "present (a field missing or out of range)" : "absent");
    }
  }
}

// ForcePause_NoFade / ForceUnPause on the player by ProcessEvent (pause.md); neither has parameters.
static bool call_player(void* player, const wchar_t* fn) {
  const ScallFn f = player ? scall_find(player, fn) : ScallFn{ 0, 0, 0 };
  unsigned char parms[64] = {};
  return f.ok && f.parmsSize <= sizeof parms && scall_call(player, f, parms, sizeof parms) != 0;
}

static void pause_frame(void* player) {
  const unsigned level = world_level_serial();   // unchanged by a world fault's drop
  const int paused = world_paused_now();
  // A photo freezes the world for its capture even with the row off (DNMP's rule).
  const int act = fcp_step(&s_fp, freecam_pause_armed(s_flying, g_cfg.freecamPause, photo_busy()),
                           level, player != 0, paused);
  // Held through a drop: logged once per drop, so a stranded pause would show.
  static int s_dropLogged;
  if (s_fp.held && !player && !s_dropLogged++) np_log("freecam: pause held while the player is not read (level %u)", level);
  if (player) s_dropLogged = 0;
  if (act == FCP_NONE) return;
  bool called = true;
  if (act == FCP_PAUSE || act == FCP_REPAUSE) {
    called = call_player(player, L"ForcePause_NoFade");
    if (act == FCP_PAUSE) ++s_pauses; else ++s_repauses;
  } else if (act == FCP_UNPAUSE) {
    called = call_player(player, L"ForceUnPause");
    ++s_unpauses;
  } else if (act == FCP_LET_GO) {
    ++s_letGo;
  } else {
    ++s_pauseRefused;
  }
  // Capped: a level that unpauses every frame must not log every frame.
  if (act != FCP_REPAUSE || s_repauses <= 32)
    np_log("freecam: pause %s (level %u, paused %d)%s; pauses %u repauses %u unpauses %u let go %u refused %u",
           fcp_act_text(act), level, paused, called ? "" : ": the call was NOT made", s_pauses, s_repauses,
           s_unpauses, s_letGo, s_pauseRefused);
}

// One cvar change, with its values as the variable reads them (a FLOAT as a float).
static void log_cvar(int i, const char* what, int from, int to, const char* tail) {
  if (freecam_cvar_is_float(i))
    np_log("freecam: %ls %s: %g -> %g%s", freecam_cvar_name(i), what, (double)cvar_float(from), (double)cvar_float(to),
           tail);
  else np_log("freecam: %ls %s: %d -> %d%s", freecam_cvar_name(i), what, from, to, tail);
}

// Found on the first flight; a variable the registry refuses (saved, or another type) is never written.
static void cvars_frame(void) {
  if (!s_cvFound) {
    if (!s_flying) return;
    s_cvFound = 1;
    for (int i = 0; i < FC_CVAR_ALL; i++) {
      const char* why = 0;
      s_cv[i] = cvar_find(freecam_cvar_name(i), &why);
      if (s_cv[i].ok && (s_cv[i].isFloat != 0) != freecam_cvar_is_float(i)) {
        s_cv[i].ok = 0;
        why = freecam_cvar_is_float(i) ? "not a FLOAT" : "a FLOAT, not a UBOOL";
      }
      s_cvHold[i] = cvar_hold_init();
      if (!s_cv[i].ok) np_log("freecam: %ls not used: %s", freecam_cvar_name(i), why ? why : "not found");
    }
  }
  for (int i = 0; i < FC_CVAR_ALL; i++) {
    if (!s_cv[i].ok) continue;
    const int cur = cvar_get(s_cv[i]);
    const int force = freecam_cvar_forced(i, s_flying, g_cfg.freecamShot, photo_capturing(), g_cfg.freecamNoFog,
                                          g_cfg.freecamNoCull);
    const int wasHeld = s_cvHold[i].held;
    int w = 0;
    const int act = cvar_step(&s_cvHold[i], force, cur, 0, &w);   // every forced cvar holds 0
    if (act == CVAR_FORCE) {
      cvar_set(s_cv[i], w);
      ++s_cvForces;
      if (!wasHeld || s_cvForces <= 16) log_cvar(i, "set", cur, w, wasHeld ? " (again)" : "");
    } else if (act == CVAR_RESTORE) {
      cvar_set(s_cv[i], w);
      ++s_cvRestores;
      log_cvar(i, "given back", cur, w, "");
    } else if (wasHeld && !s_cvHold[i].held) {
      log_cvar(i, "released, the game's own", cur, cur, "");
    }
  }
}

// Actor.GetPointRegion(TestPoint) -> PointRegion {Zone +0, iLeaf +4, iZone +8} (Engine.u, native 729) on Duke.
static void zone_probe(void* player) {
  const ScallFn f = scall_find(player, L"GetPointRegion");
  ScallParm tp, rv;
  if (!f.ok || !scall_parm(f, L"TestPoint", L"StructProperty", &tp) || tp.size != 12 ||
      !scall_parm(f, L"ReturnValue", L"StructProperty", &rv) || rv.size < 12)
    return;
  unsigned char parms[64] = {};
  if (f.parmsSize > sizeof parms || rv.offset + 12 > f.parmsSize) return;
  memcpy(parms + tp.offset, s_pose.eye, 12);
  if (!scall_call(player, f, parms, sizeof parms)) return;
  const void* zone;
  int leaf, num, name = 0;
  memcpy(&zone, parms + rv.offset, 4);
  memcpy(&leaf, parms + rv.offset + 4, 4);
  memcpy(&num, parms + rv.offset + 8, 4);
  __try {
    name = zone ? uobj_name(zone) : 0;
  } __except (av_only(GetExceptionCode())) {
    name = 0;
  }
  unsigned firsts, added, engine, last;
  int hooked;
  d3dcap_clear_take(&firsts, &added, &engine, &last, &hooked);
  s_clFirsts += firsts;
  s_clAdded += added;
  s_clEngine += engine;
  if (name == s_zoneName && leaf == s_zoneLeaf && num == s_zoneNum) return;
  s_zoneName = name;
  s_zoneLeaf = leaf;
  s_zoneNum = num;
  if (++s_zoneLines > 64) return;
  np_log("freecam: camera zone %ls (iZone %d, iLeaf %d) at (%.0f,%.0f,%.0f); frame clear %s: since the last line %u "
         "first clears, %u given the colour by us, %u cleared by the engine itself (camera outside the world), last "
         "flags 0x%x", name ? uname_str(name) : L"none", num, leaf, s_pose.eye[0], s_pose.eye[1], s_pose.eye[2],
         hooked ? "hooked" : "NOT hooked", s_clFirsts, s_clAdded, s_clEngine, last);
  s_clFirsts = s_clAdded = s_clEngine = 0;
}

// Motion-blur feedback target surface, read-only. Engine.dll's FAuxRenderTarget RVA 0x649B2C (vtable RVA 0x4947EC):
// GetCacheId (0x1028A3F0) gives the 64-bit id at +8. PlayerPawn.Player +0x60 = RenDev (D3DDrv9 UD3DRenderDevice9),
// +0xD4 = FD3DDevice; its hash at +0x1A5D8 + bucket*4 (nodes +0x20; key lo +8, hi +0xC; flags +0x10): surface +0x28.
static const void* feedback_surface(void* player) {
  static int s_playerName;
  const HMODULE eng = GetModuleHandleW(L"Engine.dll"), drv = GetModuleHandleW(L"D3DDrv9.dll");
  if (!eng || !drv || !player) return 0;
  if (!s_playerName) s_playerName = uname_find(L"Player");
  if (!s_playerName) return 0;
  const char* obj = (const char*)eng + 0x649B2C;
  __try {
    if (*(const void* const*)obj != (const char*)eng + 0x4947EC) return 0;
    const unsigned lo = *(const unsigned*)(obj + 8), hi = *(const unsigned*)(obj + 12);
    if (!lo && !hi) return 0;
    const UProp pl = uprop_get(uobj_class(player), s_playerName);
    if (!pl.ok || pl.type != UW_OBJECT) return 0;
    const char* vp = *(const char* const*)((const char*)player + pl.offset);
    const char* rd = vp ? *(const char* const*)(vp + 0x60) : 0;
    if (!rd || *(const void* const*)rd != (const char*)drv + 0x66FAC) return 0;
    const char* dev = *(const char* const*)(rd + 0xD4);
    if (!dev) return 0;
    const char* node = *(const char* const*)(dev + 0x1A5D8 + d3dres_bucket(lo) * 4);
    for (int n = 0; node && n < 64; ++n, node = *(const char* const*)(node + 0x20))
      if (*(const unsigned*)(node + 8) == lo && *(const unsigned*)(node + 12) == hi && *(const unsigned*)(node + 16) == 0)
        return *(const void* const*)(node + 0x28);
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
  return 0;
}

// Every 60 flown frames, and logged when it changes: the fill never touches that surface.
static void feedback_frame(void* player) {
  static int s_frames;
  static const void* s_last = (const void*)1;
  if (!s_flying || ++s_frames < 60) return;
  s_frames = 0;
  const void* surf = feedback_surface(player);
  d3dcap_rtclear_keep(surf);
  if (surf != s_last) {
    s_last = surf;
    np_log("freecam: motion-blur feedback target's surface %p%s", surf, surf ? ", never filled" : " (not found yet)");
  }
}

static const char* tr_kind(int k) {
  return k == DTR_RT ? "SetRenderTarget" : k == DTR_CLEAR ? "Clear" : k == DTR_STRETCH ? "StretchRect" : "ColorFill";
}

static void trace_frame(void) {
  const DtrEntry* e;
  int n, dropped;
  const void *bb, *rt0;
  unsigned bw, bh, bf;
  if (d3dcap_trace_take(&e, &n, &dropped, &bb, &rt0, &bw, &bh, &bf)) {
    ++s_traces;
    np_log("freecam: frame trace %u: %d call(s) kept, %d past the cap; back buffer %p %ux%u %s(%u); render target 0 "
           "at the start %p; camera zone iZone %d iLeaf %d at (%.0f,%.0f,%.0f)", s_traces, n, dropped, bb, bw, bh,
           dtr_fmt_name(bf), bf, rt0, s_zoneNum, s_zoneLeaf, s_pose.eye[0], s_pose.eye[1], s_pose.eye[2]);
    for (int i = 0; i < n; ++i)
      np_log("freecam: trace %u #%02d %s a=%p%s b=%p %ux%u %s(%u) flags 0x%x colour 0x%08x idx %u", s_traces, i,
             tr_kind(e[i].kind), e[i].a, e[i].a && e[i].a == bb ? " (back buffer)" : "", e[i].b, e[i].w, e[i].h,
             dtr_fmt_name(e[i].fmt), e[i].fmt, e[i].flags, e[i].colour, e[i].idx);
    d3dcap_trace_done();
    s_traceArmed = 0;
  }
  if (!s_flying || !s_seeded || !d3dcap_trace_hooked()) return;
  if (s_traceFrames == 0 && !s_traceArmed) d3dcap_record_flight();   // a normal flown frame, for the freeze's record
  if (!s_traceArmed && s_traces < 8 && (s_traceFrames == 0 || s_traceFrames >= 600)) {   // 8 traces a session
    d3dcap_trace_arm();
    s_traceArmed = 1;
    s_traceFrames = 0;
  }
  ++s_traceFrames;
}

static int s_mpStreak;

void freecam_frame(int inLevel) {
  if (s_sub < 0) {
    s_sub = pehook_subscribe_after(L"PlayerCalcView", on_calcview, "freecam");
    if (s_sub < 0 && !s_refused) s_refused = "the ProcessEvent slot's subscriber table is full";
  }
  if (!s_refused && pehook_refused()) s_refused = pehook_refused();
  if (!s_qpf.QuadPart) { QueryPerformanceFrequency(&s_qpf); QueryPerformanceCounter(&s_lastQpc); }
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  const float dt = freecam_dt((double)(now.QuadPart - s_lastQpc.QuadPart) / (double)s_qpf.QuadPart);
  s_lastQpc = now;

  void* player = world_player();
  int presses = input_camera_take() + mouse_key_edge(0, config_bind_get(BIND_ROW_CAMERA));
  const int copies = input_camcopy_take() + mouse_key_edge(1, config_bind_get(BIND_ROW_CAMCOPY));
  const int pastes = input_campaste_take() + mouse_key_edge(2, config_bind_get(BIND_ROW_CAMPASTE));
  if (s_flying) {
    // Multiplayer must read on several frames running (mpgate.h): one unread frame in single player ends nothing.
    s_mpStreak = mp_streak(s_mpStreak, mp_refused() != 0);
    const int why = s_refused || mp_sustained(s_mpStreak) ? FCE_REFUSED
                              : freecam_end_reason(1, g_cfg.freecamOn, player != 0, world_level_serial() != s_level);
    if (why) {
      end(why);
      presses = 0;   // the press that came with the end is spent on it
    }
  }
  if ((presses & 1) && s_flying) {
    end(FCE_KEY);
  } else if ((presses & 1) && g_cfg.freecamOn) {
    if (freecam_refused()) np_log("freecam: not started: %s", freecam_refused());
    else if (!player || !inLevel) np_log("freecam: not started: no level view (a menu, a load or the game's pause)");
    else start();
  }

  // Drained every flown frame, steering or not, so no stale turn lands later; raw mouse takes them otherwise.
  int dx = 0, dy = 0;
  if (s_flying) dimouse_counts_take(&dx, &dy);
  s_countsX += dx < 0 ? -dx : dx;
  s_countsY += dy < 0 ? -dy : dy;
  const int steering = freecam_steers(s_flying, s_seeded, input_panel_open(), input_game_front()) && !photo_capturing();
  fov_wheel_frame(player, steering);
  if (steering) {
    freecam_look(&s_pose, dx, dy, FREECAM_SENS_DEG);
    freecam_move(&s_pose, read_keys(), freecam_speed_uu(g_cfg.freecamSpeedPct), dt);
    ++s_steerFrames;
  }
  view_keys(copies, pastes);
  // After the end checks: every way a flight ends gives the pause, RendMap and the cvars back this same frame.
  pause_frame(player);
  rendmap_carried(player);
  rendmap_frame(player);
  fbprobe_frame(player, s_flying && s_seeded && s_rendHold.held, s_pose);
  // An alt-tab keeps the flight. The watch logs our pause and the level's around it, so a gate shows
  // the game's own focus pause absorbed under ours and our re-pause after its return; a pause we did not make is never
  // released (fcp_step).
  const unsigned losses = input_focus_losses();
  if (losses != s_focusLosses) {
    s_focusLosses = losses;
    if (s_flying) {
      s_focusWatch = 8;
      s_awaitReturn = 1;
      np_log("freecam: focus lost while flying; the flight, its pause (held %d) and its cvars are kept", s_fp.held);
    }
  }
  const int front = input_game_front();
  if (front && !s_wasFront && s_awaitReturn) { s_backWatch = 8; s_awaitReturn = 0; }
  s_wasFront = front;
  if (s_focusWatch > 0 || s_backWatch > 0) {
    np_log("freecam: focus watch (%s): paused=%d front=%d held=%d keep running=%d pause row=%d",
           s_focusWatch > 0 ? "after the alt-tab" : "after the return", world_paused_now(), front, s_fp.held,
           g_cfg.keepRunning, g_cfg.freecamPause);
    if (s_focusWatch > 0) --s_focusWatch;
    else --s_backWatch;
  }
  cvars_frame();
  d3dcap_set_flight_clear(s_flying);
  trace_frame();
  feedback_frame(player);
  if (s_flying) d3dcap_rtclear_report();
  if (s_flying && player && ++s_zoneFrames >= 30) {
    s_zoneFrames = 0;
    zone_probe(player);
  }
  pehook_enable(s_sub, s_flying);
  if (s_zoneSub < 0) s_zoneSub = pehook_subscribe(L"PreRender", on_prerender, "freecam zone");
  pehook_enable(s_zoneSub, s_flying && g_cfg.freecamNoCull);
  if (s_flying) {
    ++s_flownFrames;
    if (++s_noOverrideFrames == 120)
      np_log("freecam: 120 frames flown without a PlayerCalcView override (slot %s): the view is not ours",
             pehook_refused() ? pehook_refused() : "installed");
  }
  if (++s_frames < 600) return;
  s_frames = 0;
  {
    unsigned any = s_zoneFaults;
    for (int r = 0; r < FZ_COUNT; ++r) any |= s_zoneBy[r];
    if (any)
      np_log("freecam: whole level: %u frame(s) forced (zone %d replaced last); skipped: %u unresolved, %u frame null, "
             "%u no viewport, %u viewport not Duke's, %u level mismatch, %u zone already 0; %u fault(s)",
             s_zoneBy[FZ_WRITE], s_zoneLast, s_zoneBy[FZ_UNRESOLVED], s_zoneBy[FZ_NO_FRAME], s_zoneBy[FZ_NO_VIEWPORT],
             s_zoneBy[FZ_VIEWPORT], s_zoneBy[FZ_LEVEL], s_zoneBy[FZ_ZONE0], s_zoneFaults);
    memset(s_zoneBy, 0, sizeof s_zoneBy);
  }
  {
    unsigned heals, fails;
    d3dcap_heal_take(&heals, &fails);
    if (heals || fails)
      np_log("freecam: 600 frames: render target 0 set back to the back buffer at Present %u time(s), %u refused "
             "(Fullbright %d)", heals, fails, s_rendHold.held);
  }
  RtcStats rs;
  d3dcap_rtclear_take(&rs);
  if (s_flying || rs.frames)
    np_log("freecam: render-target fill (%s): %u frame(s), %u target(s) filled (%.2f a frame), %u refused by the "
           "device, %u learned, %u kept as history, %u feedback-target bind(s) kept, %u left to the engine's own clear, "
           "%u past the full table, %u of another format; fill submission %.3f ms a frame (CPU, not GPU)",
           rs.hooked ? "hooked" : "NOT hooked", rs.frames, rs.fills, rs.frames ? (double)rs.fills / rs.frames : 0.0,
           rs.fillFails, rs.learned, rs.history, rs.kept, rs.engine, rs.full, rs.format,
           rs.frames ? rs.fillUs / 1000.0 / rs.frames : 0.0);
  unsigned liftFrames;
  double liftMs;
  fblift_take(&liftFrames, &liftMs);
  if (s_flying || s_flownFrames)
    np_log("freecam: 600 frames: flying %d seeded %d, %u flown, %u steered, %u override(s), counts |x| %ld |y| %ld; "
           "totals flights %u copies %u pastes %u (refused %u) cvar forces %u restores %u marker faults %u; wheel fov %s "
           "%.1f, %u notch(es), %u fault(s); brightness lift %d%%: %u frame(s), %.3f ms a frame (CPU)%s",
           s_flying, s_seeded, s_flownFrames, s_steerFrames, s_overrides, s_countsX, s_countsY, s_flights, s_copies,
           s_pastes, s_pastesRefused, s_cvForces, s_cvRestores, s_markerFaults, s_fovOwned ? "owned" : "off",
           (double)s_fovDeg, s_fovNotches, s_fovFaults, g_cfg.freecamLift, liftFrames,
           liftFrames ? liftMs / liftFrames : 0.0, fblift_refused() ? ", off for the session" : "");
  s_flownFrames = s_steerFrames = s_overrides = 0;
  s_countsX = s_countsY = 0;
}

// A rect with a 1 px C_BEVELLO keyline, so the cyan reads on a bright wall.
static void keyed_rect(int x, int y, int w, int h, unsigned colour) {
  canvas_rect(x - 1, y - 1, w + 2, h + 2, C_BEVELLO);
  canvas_rect(x, y, w, h, colour);
}

static void draw_crosshair(int cx, int cy) {
  const int a = FREECAM_CROSS_ARM, g = FREECAM_CROSS_GAP, t = FREECAM_CROSS_T;
  keyed_rect(cx - g - a, cy - t / 2, a, t, FREECAM_MARKER_RGBA);
  keyed_rect(cx + g, cy - t / 2, a, t, FREECAM_MARKER_RGBA);
  keyed_rect(cx - t / 2, cy - g - a, t, a, FREECAM_MARKER_RGBA);
  keyed_rect(cx - t / 2, cy + g, t, a, FREECAM_MARKER_RGBA);
}

// Duke's Location by reflection, once a frame under one guard.
static bool player_location(float out[3]) {
  static int s_locName;
  void* player = world_player();
  if (!s_locName && player) s_locName = uname_find(L"Location");
  const int n = player ? s_locName : 0;
  if (!n) return false;
  __try {
    const UProp p = uprop_get(uobj_class(player), n);
    if (!p.ok || p.type != UW_STRUCT || p.elemSize < 12) return false;
    memcpy(out, (const char*)player + p.offset, 12);
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_markerFaults;
    return false;
  }
}

// Duke through this frame's view, which is the camera's (CalcedView* is written from our override).
static void draw_duke_marker(int W, int H) {
  float at[3], sx, sy;
  if (!player_location(at) || !view_w2s(world_view(), at[0], at[1], at[2], W, H, &sx, &sy)) return;
  if (sx < 0.f || sx > (float)W || sy < 0.f || sy > (float)H) return;
  const int cx = (int)(sx + 0.5f), cy = (int)(sy + 0.5f);
  const int rows = FREECAM_MARKER_HALF * 2 + 1;
  for (int r = 0; r < rows; ++r) {
    const int hw = freecam_marker_row_halfwidth(r);
    canvas_rect(cx - hw - 1, cy - FREECAM_MARKER_HALF + r - 1, hw * 2 + 3, 3, C_BEVELLO);
  }
  for (int r = 0; r < rows; ++r) {
    const int hw = freecam_marker_row_halfwidth(r);
    canvas_rect(cx - hw, cy - FREECAM_MARKER_HALF + r, hw * 2 + 1, 1, FREECAM_MARKER_RGBA);
  }
  static const char kLabel[] = "You";
  esp_draw_label(esp_label_x(cx, 0, canvas_text_width(kLabel, FONT_SMALL)), cy - FREECAM_MARKER_HALF - ESP_LABEL_GAP,
                 kLabel, FREECAM_MARKER_RGBA, FONT_SMALL);
}

int freecam_wants_draw(void) { return s_flying && s_seeded && !g_cfg.freecamShot; }

void freecam_draw(int W, int H) {
  if (!freecam_wants_draw()) return;
  canvas_open(W, H);
  draw_crosshair(W / 2, H / 2);
  teleport_draw(W, H);
  if (g_cfg.freecamMarker) draw_duke_marker(W, H);
  if (g_cfg.freecamDebug) {
    char line[112];
    float fov;
    if (fov_display(&fov)) freecam_readout_fov(s_pose, fov, line, sizeof line);
    else freecam_readout(s_pose, line, sizeof line);
    // Under the Level stats line when it shows (levelstats.cpp: one FONT_UI line at the same margin).
    const int y = FREECAM_READOUT_MARGIN + (g_cfg.levelStats ? 22 : 0);
    esp_draw_label(FREECAM_READOUT_MARGIN, y, line, FREECAM_READOUT_RGBA, FONT_SMALL);
  }
}

int freecam_flying(void) { return s_flying; }
int freecam_fov_now(float* deg) {
  if (!s_flying || !s_fovOwned) return 0;
  *deg = s_fovDeg;
  return 1;
}
int freecam_pause_holding(void) { return s_fp.held; }
int freecam_screenshot(void) { return s_flying && g_cfg.freecamShot; }
const char* freecam_refused(void) { return s_refused ? s_refused : mp_refused(); }
int freecam_pose_now(FreecamPose* out) {
  if (!s_flying || !s_seeded) return 0;
  *out = s_pose;
  return 1;
}

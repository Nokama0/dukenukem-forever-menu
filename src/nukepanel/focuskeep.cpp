// Keep running when alt-tabbed (focuskeep.h). Flag: dnCommon.dll's g_IgnoreFocusChanges, read by Viewport_FocusPause
// (Engine.dll!0x102BB250) to skip the focus-loss pause. Sound: ViewportWndProc (0x102BC410) calls SetMasterVolume(0.0)
// on focus loss; the audio subsystem is [[[viewport+0x18]+0x28]+0x4C], vtable +0xD8 a __thiscall (float).
#include <windows.h>
#include <string.h>
#include "focuskeep.h"
#include "engine.h"
#include "config.h"
#include "input.h"
#include "world.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "log.h"

enum { VP_CLIENT = 0x18, CLIENT_ENGINE = 0x28, ENGINE_AUDIO = 0x4C, AUDIO_SETMASTERVOLUME = 0xD8 };
typedef void (__fastcall* SetMasterVolumeFn)(void* self, void* edx, float v);

static CvarHold s_hold;
static const char* s_refused;
static bool s_checked;
static void* s_audio;
static SetMasterVolumeFn s_setVol;
static unsigned s_audioLevel;
static const char* s_audioWhy;
static int s_nPlayer;
// Counted per 600 frames: volume restores, focus losses with no audio subsystem known.
static unsigned s_frames, s_restores, s_noAudio, s_restoresLogged;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static bool read_ptr(const void* base, int ofs, void** out) {
  *out = 0;
  if (!base) return false;
  __try {
    memcpy(out, (const char*)base + ofs, sizeof *out);
    return *out != 0;
  } __except (av_only(GetExceptionCode())) {
    *out = 0;
    return false;
  }
}

static const void* class_of(const void* obj) {
  __try {
    return uobj_class(obj);
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
}

static UProp prop_get(const void* cls, int nameIdx) {
  __try {
    return uprop_get(cls, nameIdx);
  } __except (av_only(GetExceptionCode())) {
    UProp none = { 0, 0, UW_OTHER, 0, 0, 0 };
    return none;
  }
}

// The player's viewport (PlayerPawn.Player, by reflection), then the WndProc's own chain to the audio subsystem.
// Its SetMasterVolume must be code in MilesAudio.dll, the subsystem qol.md read.
static const char* resolve_audio(void* player) {
  if (!s_nPlayer) s_nPlayer = uname_find(L"Player");
  const void* cls = class_of(player);
  const UProp p = cls && s_nPlayer ? prop_get(cls, s_nPlayer) : UProp{ 0, 0, UW_OTHER, 0, 0, 0 };
  if (!p.ok || p.type != UW_OBJECT) return "PlayerPawn.Player did not resolve";
  void *vp, *client, *eng, *audio, *vt, *fn;
  if (!read_ptr(player, p.offset, &vp)) return "the player has no viewport";
  if (!read_ptr(vp, VP_CLIENT, &client) || !read_ptr(client, CLIENT_ENGINE, &eng) ||
      !read_ptr(eng, ENGINE_AUDIO, &audio))
    return "no audio subsystem behind the viewport";
  if (!read_ptr(audio, 0, &vt) || !read_ptr(vt, AUDIO_SETMASTERVOLUME, &fn)) return "the audio vtable is unreadable";
  HMODULE mod = 0;
  const HMODULE miles = GetModuleHandleW(L"MilesAudio.dll");
  if (!miles || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)fn, &mod) ||
      mod != miles)
    return "SetMasterVolume is not in MilesAudio.dll";
  s_audio = audio;
  s_setVol = (SetMasterVolumeFn)fn;
  np_log("focus: audio subsystem %p (viewport %p client %p engine %p) SetMasterVolume %p in MilesAudio.dll %p", audio,
         vp, client, eng, fn, (void*)miles);
  return 0;
}

static void audio_frame(void) {
  const unsigned level = world_level_serial();
  if (level == s_audioLevel && (s_setVol || s_audioWhy)) return;
  void* player = world_player();
  if (!player) return;
  s_audioLevel = level;
  s_audio = 0;
  s_setVol = 0;
  const char* why = resolve_audio(player);
  if (why && why != s_audioWhy) np_log("focus: audio not found (%s): the sound stops while alt-tabbed", why);
  s_audioWhy = why;
}

void focuskeep_frame(void) {
  volatile long* const flag = engine_exports()->ignoreFocusChanges;
  if (!s_checked) {
    s_checked = true;
    s_hold = cvar_hold_init();
    if (!flag) s_refused = "the engine's focus flag (g_IgnoreFocusChanges) was not found";
    np_log("focus: keep running %s", s_refused ? s_refused : "available");
  }
  if (!flag) return;
  const int wasHeld = s_hold.held;
  const int cur = (int)*flag;
  int w = 0;
  const int act = focus_flag_step(&s_hold, g_cfg.keepRunning != 0, input_game_front(), cur, &w);
  if (act != CVAR_NONE) InterlockedExchange(flag, (long)w);
  if (!wasHeld && s_hold.held)
    np_log("focus: keep running engaged: g_IgnoreFocusChanges %d -> 1%s", cur, act == CVAR_NONE ? " (already)" : "");
  else if (wasHeld && !s_hold.held)
    np_log("focus: keep running released: g_IgnoreFocusChanges %d -> %d%s", cur, act == CVAR_RESTORE ? w : cur,
           act == CVAR_RESTORE ? ", the engine's value" : " (the engine's own; left)");
  if (s_hold.held) audio_frame();
  if (++s_frames < 600) return;
  s_frames = 0;
  if (s_hold.held || s_restores || s_noAudio)
    np_log("focus: 600 frames: keep running %s, volume restores %u, focus losses without audio %u",
           s_hold.held ? "on" : "off", s_restores, s_noAudio);
  s_restores = s_noAudio = 0;
}

static bool set_volume(float v) {
  __try {
    s_setVol(s_audio, 0, v);
    return true;
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

void focuskeep_after_focus_loss(void) {
  volatile long* const flag = engine_exports()->ignoreFocusChanges;
  if (!s_hold.held || !flag || *flag != 1) return;
  if (!s_setVol) { ++s_noAudio; return; }
  const bool ok = set_volume(1.0f);   // the value the engine writes every focused tick (ULevel::Tick, 0x102424C3)
  ++s_restores;
  if (!ok) {
    np_log("focus: SetMasterVolume faulted; not called again until the next level");
    s_setVol = 0;
    s_audioWhy = "SetMasterVolume faulted";
  } else if (++s_restoresLogged <= 8) {
    np_log("focus: lost focus: master volume back to 1.0 (%u this session)", s_restoresLogged);
  }
}

const char* focuskeep_refused(void) { return s_refused; }

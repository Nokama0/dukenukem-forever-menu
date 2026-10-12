// Post-processing switches (postfx.h), game thread: each variable found once through the registry, then forced to its
// row's value while the switch is off and given back when it turns on (cvars.h's rule). Variables are globals, not per
// level; a value the game resets is forced again next frame. The game's options (UClient config, qol.md 4) grey rows.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "postfx.h"
#include "cvars.h"
#include "world.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "log.h"

static bool s_found;
static CvarRef s_var[PFX_COUNT][PFX_MAX_VARS];
static CvarHold s_hold[PFX_COUNT][PFX_MAX_VARS];
static const char* s_why[PFX_COUNT];
static CvarRef s_uberPost, s_drawUber;   // read only, for the per-level line
static int s_photo;   // postfx_set_photo
// Work counted per 600 frames: forces (the first and any after the game wrote its own value), restores.
static unsigned s_frames, s_forces, s_restores, s_reforcesLogged;

// The client's own options. Known only once reflection agrees with the offsets qol.md read natively.
enum { VP_CLIENT = 0x18, NATIVE_PPMB = 0xA4, NATIVE_GRAIN = 0xB4, NATIVE_POST = 0xB8 };
struct ClientOpt { const wchar_t* name; int native; int ofs; unsigned mask; int known, on; };
static ClientOpt s_opt[3] = { { L"PerPixelMotionBlur", NATIVE_PPMB }, { L"AllowFilmGrain", NATIVE_GRAIN },
                              { L"AllowPostEffects", NATIVE_POST } };
enum { OPT_PPMB, OPT_GRAIN, OPT_POST };
static void* s_client;
static unsigned s_optLevel, s_optFrames;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
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

static void* read_ptr(const void* base, int ofs) {
  void* p = 0;
  if (!base) return 0;
  __try {
    memcpy(&p, (const char*)base + ofs, sizeof p);
  } __except (av_only(GetExceptionCode())) {
    p = 0;
  }
  return p;
}

static const void* class_of(const void* obj) {
  __try {
    return uobj_class(obj);
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
}

static void find_all(void) {
  s_found = true;
  cvars_log_all();
  int ok = 0;
  for (int i = 0; i < PFX_COUNT; i++) {
    const PostFx& fx = postfx_info(i);
    s_why[i] = 0;
    for (int v = 0; v < fx.nvars; v++) {
      const char* why = 0;
      s_var[i][v] = cvar_find(fx.var[v].cvar, &why);
      s_hold[i][v] = cvar_hold_init();
      if (s_var[i][v].ok && s_var[i][v].isFloat != fx.var[v].isFloat) {
        why = fx.var[v].isFloat ? "a variable is not a FLOAT" : "a variable is a FLOAT";
        s_var[i][v].ok = 0;
        np_log("postfx: %s: %ls refused: %s", fx.label, fx.var[v].cvar, why);
      }
      if (!s_var[i][v].ok && !s_why[i]) s_why[i] = why ? why : "a variable was not found";
    }
    ok += !s_why[i];
  }
  s_uberPost = cvar_find(L"r_TestUberPost", 0);
  s_drawUber = cvar_find(L"r_DrawUberPost", 0);
  np_log("postfx: %d/%d effect rows usable", ok, PFX_COUNT);
}

// The client behind the player's viewport, and its three options, once per level; then their values every 30
// frames (they change only from the game's Video menu).
static void client_frame(void) {
  const unsigned level = world_level_serial();
  void* player = world_player();
  if (level != s_optLevel && player) {
    s_optLevel = level;
    s_client = 0;
    for (int k = 0; k < 3; k++) s_opt[k].known = 0;
    const void* pcls = class_of(player);
    const UProp pl = pcls ? prop(pcls, L"Player") : UProp{ 0, 0, UW_OTHER, 0, 0, 0 };
    void* client = pl.ok && pl.type == UW_OBJECT ? read_ptr(read_ptr(player, pl.offset), VP_CLIENT) : 0;
    const void* ccls = client ? class_of(client) : 0;
    for (int k = 0; ccls && k < 3; k++) {
      const UProp p = prop(ccls, s_opt[k].name);
      s_opt[k].ofs = p.offset;
      s_opt[k].mask = p.bitmask;
      s_opt[k].known = p.ok && p.type == UW_BOOL && p.offset == s_opt[k].native && p.bitmask;
      if (!s_opt[k].known) np_log("postfx: client %ls not where qol.md read it (+0x%x); its row is not greyed",
                                  s_opt[k].name, p.offset);
    }
    s_client = ccls ? client : 0;
    s_optFrames = 30;
  }
  if (!s_client || ++s_optFrames < 30) return;
  s_optFrames = 0;
  int on[3] = { 0, 0, 0 };
  bool ok = true;
  __try {
    for (int k = 0; k < 3; k++)
      if (s_opt[k].known) on[k] = (*(const unsigned*)((const char*)s_client + s_opt[k].ofs) & s_opt[k].mask) != 0;
  } __except (av_only(GetExceptionCode())) {
    ok = false;
  }
  if (!ok) {
    np_log("postfx: the client's options faulted; rows are not greyed until the next level");
    s_client = 0;
    for (int k = 0; k < 3; k++) s_opt[k].known = 0;
    return;
  }
  static unsigned s_loggedLevel;   // the measurement line: once per level, and on any change
  bool changed = false;
  for (int k = 0; k < 3; k++) {
    changed |= s_opt[k].on != on[k];
    s_opt[k].on = on[k];
  }
  if (s_loggedLevel != s_optLevel || changed) {
    s_loggedLevel = s_optLevel;
    np_log("postfx: client PerPixelMotionBlur %d AllowFilmGrain %d AllowPostEffects %d; r_TestUberPost %d "
           "r_DrawUberPost %d", on[0], on[1], on[2], cvar_get(s_uberPost), cvar_get(s_drawUber));
  }
}

static void log_value(char* out, int cap, const CvarRef& c, int v) {
  if (c.isFloat) _snprintf_s(out, cap, _TRUNCATE, "%g", (double)cvar_float(v));
  else _snprintf_s(out, cap, _TRUNCATE, "%d", v);
}

void postfx_frame(void) {
  if (!s_found) find_all();
  client_frame();
  int held = 0;
  for (int i = 0; i < PFX_COUNT; i++) {
    if (s_why[i]) continue;
    const PostFx& fx = postfx_info(i);
    const int off = g_cfg.*(fx.field) == 0 || (s_photo && pfx_photo_forces(i));
    int rowHeld = 0;
    // Each variable of the row on its own hold: the game may reset one and not the other.
    for (int v = 0; v < fx.nvars; v++) {
      const CvarRef& c = s_var[i][v];
      CvarHold& h = s_hold[i][v];
      const int forced = fx.var[v].forced;
      const int cur = cvar_get(c);
      const int wasHeld = h.held;
      int w = 0;
      const int act = cvar_step(&h, off, cur, forced, &w);
      char a[24], b[24];
      log_value(a, sizeof a, c, cur);
      log_value(b, sizeof b, c, w);
      if (act == CVAR_FORCE) {
        cvar_set(c, w);
        ++s_forces;
        // A re-force means the game wrote its own value while ours was in place: the first eight are logged.
        if (!wasHeld) np_log("postfx: %s off: %ls %s -> %s", fx.label, fx.var[v].cvar, a, b);
        else if (++s_reforcesLogged <= 8)
          np_log("postfx: %s: the game set %ls to %s; forced to %s again", fx.label, fx.var[v].cvar, a, b);
      } else if (wasHeld && !h.held) {
        if (act == CVAR_RESTORE) {
          cvar_set(c, w);
          ++s_restores;
          np_log("postfx: %s on: %ls %s -> %s, the game's value", fx.label, fx.var[v].cvar, a, b);
        } else {
          np_log("postfx: %s on: %ls %s is the game's own; left", fx.label, fx.var[v].cvar, a);
        }
      } else if (!wasHeld && h.held) {
        np_log("postfx: %s off: %ls already %s", fx.label, fx.var[v].cvar, a);
      }
      rowHeld |= h.held;
    }
    held += rowHeld;
  }
  if (++s_frames < 600) return;
  s_frames = 0;
  if (held || s_forces || s_restores)
    np_log("postfx: 600 frames: %d effect(s) off, %u force(s), %u restore(s)", held, s_forces, s_restores);
  s_forces = s_restores = 0;
}

const char* postfx_refused(int i) {
  if (i < 0 || i >= PFX_COUNT || !s_found) return 0;
  if (s_why[i]) return s_why[i];
  const int g = postfx_info(i).game;
  if (g == PFX_GAME_NONE) return 0;
  const ClientOpt& o = s_opt[g == PFX_GAME_PIXELMOTIONBLUR ? OPT_PPMB : OPT_GRAIN];
  return pfx_game_off(o.known && s_client, o.on, cvar_get(s_var[i][0]), s_hold[i][0].held)
             ? "Off in the game's options." : 0;
}

void postfx_set_photo(int on) {
  on = on ? 1 : 0;
  if (on == s_photo) return;
  s_photo = on;
  np_log("postfx: photo %s: screen-space effects %s", on ? "capturing" : "done", on ? "forced off" : "as the rows set");
}

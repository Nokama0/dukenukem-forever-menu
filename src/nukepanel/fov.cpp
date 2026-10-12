// Custom field of view through the game's own 'ConsoleFOV' scale modifier on the player (fov.h): SetScaleModifier
// to apply, ClearScaleModifier to release, HasScaleModifier + GetScaleModifierTarget to read what is in place.
// None reaches SaveConfig (docs/re/player-options.md). Game thread; engine reads under an access-violation guard.
#include <windows.h>
#include <string.h>
#include "fov.h"
#include "optrestore.h"
#include "scall.h"
#include "scallparms.h"
#include "world.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "config.h"
#include "log.h"

static const int PARMS_MAX = 128;   // SetScaleModifier's ten parameters fit in well under this

static const void* s_checkedCls;     // the player class the path was checked on
static const char* s_refused;
static ScallFn s_set, s_clear, s_has, s_target;
static ScallParm s_setGroup, s_setId, s_setTarget, s_clrGroup, s_clrId, s_hasGroup, s_hasId, s_hasRet;
static ScallParm s_tgtGroup, s_tgtId, s_tgtRet;
static int s_nFov, s_nConsole, s_nDefFov;
static UProp s_defFov;

static OptRestore s_r;
static double s_stock;
static bool s_haveStock;
static unsigned s_frames, s_nextCheck, s_level, s_appliesThisLevel;
static float s_lastWant;
static int s_lastOn;
static bool s_carriedChecked;    // this level's one look for a ConsoleFOV a save carried (while off)

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

// uprop walks the class's fields: guarded as world.cpp's reads are. A fault reads as unresolved.
static UProp prop_get(const void* cls, int nameIdx) {
  __try {
    return uprop_get(cls, nameIdx);
  } __except (av_only(GetExceptionCode())) {
    UProp none = { 0, 0, UW_OTHER, 0, 0, 0 };
    return none;
  }
}

static bool read_float(const void* obj, int ofs, float* out) {
  __try {
    memcpy(out, (const char*)obj + ofs, 4);
    return true;
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

static bool names_parm(const ScallFn& f, const wchar_t* group, const wchar_t* id, ScallParm* g, ScallParm* i) {
  return scall_parm(f, group, L"NameProperty", g) && g->size == 4 && scall_parm(f, id, L"NameProperty", i) &&
         i->size == 4;
}

// Every function and parameter the path uses, by reflection, before anything is called (Review Focus 4).
static const char* check(void* player, const void* cls) {
  if (!s_nFov) s_nFov = uname_find(L"FOV");
  if (!s_nConsole) s_nConsole = uname_find(L"ConsoleFOV");
  if (!s_nDefFov) s_nDefFov = uname_find(L"DefaultFOV");
  if (!s_nFov || !s_nConsole || !s_nDefFov) return "the names FOV, ConsoleFOV or DefaultFOV are not in the game";
  s_set = scall_find(player, L"SetScaleModifier");
  s_clear = scall_find(player, L"ClearScaleModifier");
  s_has = scall_find(player, L"HasScaleModifier");
  s_target = scall_find(player, L"GetScaleModifierTarget");
  if (!s_set.ok || !s_clear.ok || !s_has.ok || !s_target.ok)
    return "the game's scale-modifier functions were not found";
  const bool parms =
    s_set.parmsSize <= PARMS_MAX && s_clear.parmsSize <= PARMS_MAX && s_has.parmsSize <= PARMS_MAX &&
    s_target.parmsSize <= PARMS_MAX &&
    names_parm(s_set, L"ScaleGroupIdentifier", L"ScaleIdentifier", &s_setGroup, &s_setId) &&
    scall_parm(s_set, L"ScaleTarget", L"FloatProperty", &s_setTarget) && s_setTarget.size == 4 &&
    names_parm(s_clear, L"ScaleGroupIdentifier", L"ScaleModifierIdentifier", &s_clrGroup, &s_clrId) &&
    names_parm(s_has, L"ScaleGroupIdentifier", L"ScaleIdentifier", &s_hasGroup, &s_hasId) &&
    scall_parm(s_has, L"ReturnValue", L"BoolProperty", &s_hasRet) && s_hasRet.size == 4 &&
    names_parm(s_target, L"ScaleGroupIdentifier", L"ScaleIdentifier", &s_tgtGroup, &s_tgtId) &&
    scall_parm(s_target, L"ReturnValue", L"FloatProperty", &s_tgtRet) && s_tgtRet.size == 4;
  if (!parms) return "the scale-modifier functions' parameters differ from build 244's";
  s_defFov = prop_get(cls, s_nDefFov);
  if (!s_defFov.ok || s_defFov.type != UW_FLOAT) return "PlayerPawn.DefaultFOV did not resolve";
  np_log("fov: path checked on player class %p: SetScaleModifier ParmsSize=%u target +0x%x, ClearScaleModifier, "
         "HasScaleModifier, GetScaleModifierTarget; DefaultFOV +0x%x", cls, (unsigned)s_set.parmsSize,
         s_setTarget.offset, s_defFov.offset);
  return 0;
}

// The parms block of a ('FOV', 'ConsoleFOV') call, every other parameter zero (what the FOV exec passes).
static bool fill_names(const ScallFn& f, const ScallParm& g, const ScallParm& i, unsigned char* parms) {
  memset(parms, 0, PARMS_MAX);
  return scallparms_put(parms, f.parmsSize, g.offset, &s_nFov, 4) &&
         scallparms_put(parms, f.parmsSize, i.offset, &s_nConsole, 4);
}

static bool set_mod(void* player, float target) {
  unsigned char parms[PARMS_MAX];
  if (!fill_names(s_set, s_setGroup, s_setId, parms) ||
      !scallparms_put(parms, s_set.parmsSize, s_setTarget.offset, &target, 4))
    return false;
  return scall_call(player, s_set, parms, sizeof parms) != 0;
}

static bool clear_mod(void* player) {
  unsigned char parms[PARMS_MAX];
  return fill_names(s_clear, s_clrGroup, s_clrId, parms) && scall_call(player, s_clear, parms, sizeof parms) != 0;
}

// What is in place: the modifier's target while active, else FOV_ABSENT.
static bool read_mod(void* player, float* cur) {
  unsigned char parms[PARMS_MAX];
  if (!fill_names(s_has, s_hasGroup, s_hasId, parms) || !scall_call(player, s_has, parms, sizeof parms)) return false;
  int active;
  memcpy(&active, parms + s_hasRet.offset, 4);
  float target = 0.f;
  if (active) {
    if (!fill_names(s_target, s_tgtGroup, s_tgtId, parms) || !scall_call(player, s_target, parms, sizeof parms))
      return false;
    memcpy(&target, parms + s_tgtRet.offset, 4);
  }
  *cur = fov_current(active != 0, target);
  return true;
}

void fov_frame(void) {
  ++s_frames;
  void* player = world_player();
  if (!player) return;
  const void* cls = class_of(player);
  if (!cls) return;
  const unsigned level = world_level_serial();
  const bool newLevel = level != s_level;
  if (newLevel) {
    if (s_appliesThisLevel) np_log("fov: level ended with %u apply(s)", s_appliesThisLevel);
    s_level = level;
    s_r = optrestore_level(s_r, level);
    s_nextCheck = 0;
    s_appliesThisLevel = 0;
    s_carriedChecked = false;
    if (s_refused) s_checkedCls = 0;   // a refusal (a fault included) is checked again once per level
  }
  if (cls != s_checkedCls) {
    s_checkedCls = cls;
    const char* why = check(player, cls);
    if (why && why != s_refused) np_log("fov: unavailable: %s; nothing is called", why);
    s_refused = why;
  }
  if (s_refused) return;
  // uprop's cache is per level: resolved again here, a cache hit after the first frame.
  s_defFov = prop_get(cls, s_nDefFov);
  float def = 0.f;
  s_haveStock = s_defFov.ok && s_defFov.type == UW_FLOAT && read_float(player, s_defFov.offset, &def) &&
                def > 1.f && def < 179.f;
  if (s_haveStock) s_stock = def;

  const int on = g_cfg.fovOn != 0;
  // The level's first frame only: on, ours overwrites a carried one; turned off later, the release clears ours.
  const bool firstCheck = !s_carriedChecked;
  s_carriedChecked = true;
  float carried;
  if (firstCheck && !on && !s_r.applied && read_mod(player, &carried)) {
    if (fov_clear_carried(on, firstCheck, carried != FOV_ABSENT)) {
      const bool ok = clear_mod(player);
      np_log("fov: cleared a ConsoleFOV modifier carried by the save (target %.4f, %s)", carried,
             ok ? "called" : "REFUSED");
    }
  }
  const float want = on && s_haveStock ? fov_ratio(fov_want_deg(g_cfg.fovDeg, s_stock), s_stock) : 0.f;
  if (on && want == 0.f) return;   // no usable DefaultFOV this frame
  if (on == s_lastOn && want == s_lastWant && s_frames < s_nextCheck) return;
  s_lastOn = on;
  s_lastWant = want;
  s_nextCheck = s_frames + FOV_CHECK_FRAMES;
  if (!on && !s_r.applied) return;   // nothing of ours to release: no calls while off

  float cur;
  if (!read_mod(player, &cur)) return;
  const int act = opt_step(s_r, on, cur, want);
  if (act == OPT_APPLY) {
    const bool lost = s_r.applied && cur != s_r.ours;
    s_r = fov_apply(s_r, level, cur, want);
    const bool ok = set_mod(player, want);
    // A level that keeps clearing it would log every check: the first eight per level; the total at the level's
    // end and at release.
    if (++s_appliesThisLevel <= 8)
      np_log("fov: %s: SetScaleModifier('FOV', 'ConsoleFOV', %.4f) = %.1f deg over DefaultFOV %.1f (%s); "
             "ConsoleFOV before ours %s %.4f; apply %u this level", lost ? "re-applied" : "on", want, want * s_stock,
             s_stock, ok ? "called" : "REFUSED", cur == FOV_ABSENT ? "absent" : "target", cur, s_appliesThisLevel);
  } else if (act == OPT_RELEASE) {
    float back;
    if (optrestore_release(s_r, cur, &back)) {
      const bool ok = clear_mod(player);
      np_log("fov: off: ClearScaleModifier('FOV', 'ConsoleFOV') (%s); the game's FOV is DefaultFOV %.1f; %u "
             "apply(s) this level", ok ? "called" : "REFUSED", s_stock, s_appliesThisLevel);
    } else {
      np_log("fov: off: ConsoleFOV is %s %.4f, not ours (%.4f): left; %u apply(s) this level",
             cur == FOV_ABSENT ? "absent" : "target", cur, s_r.ours, s_appliesThisLevel);
    }
    s_r = optrestore_level(optrestore_init(), level);
  }
}

bool fov_stock(double* deg) {
  if (deg) *deg = s_stock;
  return s_haveStock;
}

const char* fov_refused(void) { return s_refused; }

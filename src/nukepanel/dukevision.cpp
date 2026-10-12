// Clean Duke Vision. The pass runs at TriggerViewRotationAdjust in the dnGame ProcessEvent slot: that event is raised
// from the player's tick, before ADukeHUD::DrawHUDEffects draws the array, so an effect added by the key press is gone
// before it is drawn (Present is after). All reads are reflection; the only change is the game's RemoveHudEffect.
#include <windows.h>
#include <string.h>
#include "dukevision.h"
#include "pehook.h"
#include "world.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "scall.h"
#include "scallparms.h"
#include "config.h"
#include "log.h"
#include "mpgate.h"

static const int PROP_INNER = 0x78;   // UArrayProperty::Inner, UStructProperty::Struct (noshake.cpp)

static int s_sub = -1;
static const char* s_refused;
static unsigned s_level;
static int s_engaged;
static int s_live;          // on, in a level and not paused: the hook may call
static int s_hookPassed;    // the hook read the array since the last Present
// Resolved per level: the player's MyHUD, and the DukeHUD class's array, entry name and function.
static const void* s_playerCls;
static int s_myHudOfs;
static const void* s_hudCls;
static const void* s_otherHudCls;   // a HUD that is not a DukeHUD: skipped, logged once per level
static int s_arrOfs, s_elem, s_nameOfs;
static ScallFn s_fn;
static ScallParm s_parm;
static int s_targets[DV_TARGET_COUNT];
static int s_loggedFirst;
static unsigned s_frames, s_passes, s_removed, s_faults, s_over, s_callRefused;

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

static UProp prop(const void* owner, const wchar_t* name) {
  const int n = uname_find(name);
  __try {
    if (n) return uprop_get(owner, n);
  } __except (av_only(GetExceptionCode())) {
  }
  UProp none = { 0, 0, UW_OTHER, 0, 0, 0 };
  return none;
}

// The name index of a field's own class (IntProperty, NameProperty, ...), 0 when unreadable.
static int class_name_of(const void* field) {
  __try {
    return field && uobj_valid(field) ? uobj_name(uobj_class(field)) : 0;
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
}

// cls is DukeHUD or derives from it, by the super chain's names (the class object is never looked up).
static bool is_dukehud(const void* cls) {
  const int want = uname_find(L"DukeHUD");
  if (!want) return false;
  __try {
    for (int i = 0; cls && i < 32; i++, cls = uw_ptr(cls, US_SUPER))
      if (uobj_name(cls) == want) return true;
  } __except (av_only(GetExceptionCode())) {
  }
  return false;
}

static bool inner_struct(const void* arrField, const void** strct, int* elem) {
  __try {
    const void* inner = uw_ptr(arrField, PROP_INNER);
    if (!inner || !uobj_valid(inner) || uobj_name(uobj_class(inner)) != uname_find(L"StructProperty")) return false;
    *elem = *(const unsigned short*)((const char*)inner + UP_ELEMSIZE);
    *strct = uw_ptr(inner, PROP_INNER);
    return *strct && uobj_valid(*strct);
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

static const char* resolve_hud(void* hud, const void* cls) {
  DvResolve r = { 1, 0, 0, 0, 0, 0, 0 };
  const UProp arr = prop(cls, L"HUDEffects");
  r.effects = arr.ok && arr.type == UW_ARRAY && arr.field;
  const void* strct = 0;
  int elem = 0;
  r.effectsOfStruct = r.effects && inner_struct(arr.field, &strct, &elem) && elem > 0;
  UProp nm = { 0, 0, UW_OTHER, 0, 0, 0 };
  if (r.effectsOfStruct) nm = prop(strct, L"Name");
  r.nameField = nm.ok && nm.elemSize == 4 && nm.offset >= 0 && nm.offset + 4 <= elem &&
                class_name_of(nm.field) == uname_find(L"NameProperty");
  const ScallFn f = scall_find(hud, L"RemoveHudEffect");
  ScallParm pp = { 0, 0 };
  r.removeFn = f.ok;
  r.effectNameParm = f.ok && scall_parm(f, L"EffectName", L"NameProperty", &pp) && pp.size == 4;
  r.parmsSize = f.ok ? f.parmsSize : 0;
  np_log("dukevision: DukeHUD %p: HUDEffects +0x%x, entry 0x%x, Name +0x%x; RemoveHudEffect %p ParmsSize %d, "
         "EffectName +0x%x", cls, arr.offset, elem, nm.offset, f.fn, r.parmsSize, pp.offset);
  const char* why = dv_refusal(r);
  if (why) return why;
  s_arrOfs = arr.offset;
  s_elem = elem;
  s_nameOfs = nm.offset;
  s_fn = f;
  s_parm = pp;
  return 0;
}

// The player's HUD, if it is in this frame's object table.
static void* hud_of(const void* player) {
  __try {
    void* hud = *(void* const*)((const char*)player + s_myHudOfs);
    return hud && uobj_valid(hud) ? hud : 0;
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
}

// One guarded read of the array: its count and each entry's name. -1 on a fault or an array that fails its checks.
static int read_names(const void* player, void** hudOut, int* names, int* total) {
  __try {
    void* hud = *(void* const*)((const char*)player + s_myHudOfs);
    if (!hud || !uobj_valid(hud) || uobj_class(hud) != s_hudCls) return 0;
    const unsigned char* data = *(const unsigned char* const*)((const char*)hud + s_arrOfs);
    const int count = *(const int*)((const char*)hud + s_arrOfs + 4);
    if (count < 0 || (count && !data)) return -1;
    *total = count;
    const int n = count < DV_MAX_EFFECTS ? count : DV_MAX_EFFECTS;
    for (int i = 0; i < n; i++) memcpy(&names[i], data + i * s_elem + s_nameOfs, 4);
    *hudOut = hud;
    return n;
  } __except (av_only(GetExceptionCode())) {
    return -1;
  }
}

static const wchar_t* target_str(int name) {
  for (int t = 0; t < DV_TARGET_COUNT; t++)
    if (s_targets[t] == name) return DV_TARGETS[t];
  return L"?";
}

static void pass(void* player, const char* where) {
  if (!s_hudCls || !player) return;
  int names[DV_MAX_EFFECTS], total = 0;
  void* hud = 0;
  const int n = read_names(player, &hud, names, &total);
  ++s_passes;
  if (n < 0) { ++s_faults; return; }
  if (total > DV_MAX_EFFECTS) ++s_over;
  int plan[DV_MAX_EFFECTS];
  const int k = dv_plan(names, n, s_targets, plan, DV_MAX_EFFECTS);
  for (int i = 0; i < k; i++) {
    unsigned char parms[DV_PARMS_MAX] = {};
    if (!scallparms_put(parms, s_fn.parmsSize, s_parm.offset, &plan[i], 4) ||
        !scall_call(hud, s_fn, parms, sizeof parms)) {
      ++s_callRefused;
      continue;
    }
    ++s_removed;
    if (!s_loggedFirst) {
      s_loggedFirst = 1;
      np_log("dukevision: removed %ls at %s (HUDEffects held %d)", target_str(plan[i]), where, total);
    }
  }
}

static void on_event(void* obj, void* func, void* parms) {
  (void)func;
  (void)parms;
  if (!s_live || obj != world_player()) return;
  pass(obj, "TriggerViewRotationAdjust");
  s_hookPassed = 1;
}

// Player class, then HUD class, resolved when either changes; a part missing on a DukeHUD refuses the row.
static void resolve(void* player) {
  const void* pcls = class_of(player);
  if (!pcls) return;
  if (pcls != s_playerCls) {
    const UProp mh = prop(pcls, L"MyHUD");
    if (!mh.ok || mh.type != UW_OBJECT) {
      DvResolve r = { 0, 0, 0, 0, 0, 0, 0 };
      s_refused = dv_refusal(r);
      np_log("dukevision: unavailable: %s", s_refused);
      return;
    }
    s_playerCls = pcls;
    s_myHudOfs = mh.offset;
    s_hudCls = 0;
  }
  void* hud = hud_of(player);
  const void* hcls = hud ? class_of(hud) : 0;
  if (!hcls || hcls == s_hudCls || hcls == s_otherHudCls) return;
  if (!is_dukehud(hcls)) {
    s_otherHudCls = hcls;
    np_log("dukevision: the player's HUD %p (class %p) is not a DukeHUD: nothing to remove", hud, hcls);
    return;
  }
  const char* why = resolve_hud(hud, hcls);
  if (why) {
    s_refused = why;
    np_log("dukevision: unavailable: %s", why);
    return;
  }
  s_hudCls = hcls;
  for (int t = 0; t < DV_TARGET_COUNT; t++) s_targets[t] = uname_find(DV_TARGETS[t]);
  np_log("dukevision: target names %d %d %d (0: not in the name table yet)", s_targets[0], s_targets[1],
         s_targets[2]);
}

void dukevision_frame(int inLevel) {
  if (s_sub < 0) s_sub = pehook_subscribe(L"TriggerViewRotationAdjust", on_event, "dukevision");
  const unsigned level = world_level_serial();
  if (level != s_level) {
    s_level = level;
    s_playerCls = s_hudCls = s_otherHudCls = 0;   // re-resolved for the new level's player and HUD
    s_loggedFirst = 0;
  }
  void* player = world_player();
  const int want = g_cfg.cleanDukeVision != 0;
  if (want && player && !s_refused) resolve(player);
  const int on = dv_on(want, s_refused, mp_refused());   // off in multiplayer (mpgate.h)
  if (on != s_engaged) {
    s_engaged = on;
    np_log("dukevision: %s", on ? "on: Duke Vision's HUD effects are removed as they appear" :
           "off: nothing restored; the next Duke Vision activation adds the effects again");
  }
  // No call while paused or loading: inLevel is 0 then.
  s_live = on && inLevel && s_hudCls && player;
  pehook_enable(s_sub, s_live);
  if (s_live && !s_hookPassed) pass(player, "Present");
  s_hookPassed = 0;
  if (++s_frames < 600) return;
  s_frames = 0;
  if (on || s_removed)
    np_log("dukevision: 600 frames: %u removal(s), %u pass(es), %u fault(s), %u call(s) refused, %u pass(es) over "
           "%d effects", s_removed, s_passes, s_faults, s_callRefused, s_over, DV_MAX_EFFECTS);
  s_removed = s_passes = s_faults = s_callRefused = s_over = 0;
}

const char* dukevision_refused(void) { return s_refused ? s_refused : mp_refused(); }

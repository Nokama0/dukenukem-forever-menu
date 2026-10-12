// Perfect accuracy (accuracy.h): the held weapon's WeaponConfig class default gets 0 in its four spreads through
// GlobalSetProperty (bNoSave 1) and the old values return when the switch goes off. The separate No recoil switch
// zeroes the view kick the same way (docs/re/accuracy.md). Game thread; engine reads under an access-violation guard.
#include <windows.h>
#include <string.h>
#include "accuracy.h"
#include "optrestore.h"
#include "engine.h"
#include "world.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "config.h"
#include "log.h"
#include "mpgate.h"

// One patched class. r[k] keeps level 0 for good: a class default outlives levels (acc_keep_record).
struct AccRec { const void* cls; int clsName; OptRestore r[ACC_PROPS]; unsigned loggedLevel; };
static AccRec s_rec[ACC_MAX_CLASSES];
static int s_recN;

static const char* s_refused;
static bool s_checked;
static int s_nWeapon, s_nWeaponConfig, s_nProps[ACC_PROPS];
static unsigned s_level, s_checkLevel;
static bool s_fullLogged;

// Fire probe (accuracy.h): one window, a snapshot of the shot's spreads, and the classes logged per switch state.
struct ProbeSeen { int clsName; unsigned mask; };
static ProbeSeen s_seen[ACC_MAX_CLASSES];
static int s_seenN;
static AccProbe s_probe;
struct ProbeShot { int clsName; int state; float spread[ACC_PROPS]; float zoom; int kicks, kickN; int mag[4]; int ok; };
static ProbeShot s_shot;
static int s_nShots, s_nShake, s_nZoom;
static bool s_probeChecked, s_probeOff;

// No recoil: one patched class, a record per kick (magnitudes are small ints, exact as floats below 2^24).
struct RkRec { const void* cls; int clsName; int n; OptRestore r[RK_MAX_KICKS]; unsigned loggedLevel; };
static RkRec s_rk[ACC_MAX_CLASSES];
static int s_rkN;
static const char* s_rkRefused;
static int s_nKicks, s_nShakeMag;
static bool s_rkFullLogged;
// UArrayProperty::Inner (world.cpp UAP_INNER, read by CopyCompleteValue Engine.dll!0x1004c756) and
// UStructProperty::Struct, both +0x78: UStructProperty::ImportText (0x1004E090) reads this+0x78, then its +0x58
// Children.
static const int UAP_INNER = 0x78, USP_STRUCT = 0x78;
// An element larger than this is not a shake struct: nothing is read or written.
static const int RK_ELEM_MAX = 0x200;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static bool obj_ok(const void* p) {
  __try {
    return uobj_valid(p);
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

static int name_of(const void* p) {
  __try {
    return uobj_valid(p) ? uobj_name(p) : 0;
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
}

// The player's held weapon's config class: Pawn.Weapon (object), then Weapon.WeaponConfig (class), which must be
// WeaponConfig or a subclass of it (by name along the super chain). Null when any link is missing.
static const void* held_config(const void* player) {
  __try {
    const UProp wp = uprop_get(uobj_class(player), s_nWeapon);
    if (!wp.ok || wp.type != UW_OBJECT) return 0;
    const void* weapon = uw_ptr(player, wp.offset);
    if (!weapon || !uobj_valid(weapon)) return 0;
    const UProp cp = uprop_get(uobj_class(weapon), s_nWeaponConfig);
    if (!cp.ok || cp.type != UW_CLASS) return 0;
    const void* cfg = uw_ptr(weapon, cp.offset);
    if (!cfg || !uobj_valid(cfg)) return 0;
    const void* c = cfg;
    for (int i = 0; c && i < 64; i++, c = uw_ptr(c, US_SUPER))
      if (uobj_name(c) == s_nWeaponConfig) return cfg;
    return 0;
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
}

// The four spreads on the class default: the properties and their current values. ok[k] = 0 for one that did not
// resolve as a 4-byte float.
static bool read_spreads(const void* cls, UProp* props, float* vals, int* ok, void** cdo) {
  __try {
    *cdo = engine_get_default_object((void*)cls);
    if (!*cdo) return false;
    for (int k = 0; k < ACC_PROPS; k++) {
      props[k] = uprop_get(cls, s_nProps[k]);
      ok[k] = props[k].ok && props[k].type == UW_FLOAT && props[k].elemSize == 4 && props[k].field;
      vals[k] = 0.f;
      if (ok[k]) memcpy(&vals[k], (const char*)*cdo + props[k].offset, 4);
    }
    return true;
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

static const char* check(void) {
  s_nWeapon = uname_find(L"Weapon");
  s_nWeaponConfig = uname_find(L"WeaponConfig");
  for (int k = 0; k < ACC_PROPS; k++) s_nProps[k] = uname_find(ACC_PROP_NAMES[k]);
  if (!s_nWeapon || !s_nWeaponConfig || !s_nProps[0] || !s_nProps[1] || !s_nProps[2] || !s_nProps[3])
    return "the names Weapon, WeaponConfig or a spread property are not in the game";
  const EngineExports* e = engine_exports();
  if (!e->globalSetProperty || !e->getDefaultObject) return "GlobalSetProperty or GetDefaultObject is not exported";
  return 0;
}

static AccRec* record_for(const void* cls, int clsName) {
  for (int i = 0; i < s_recN; i++)
    if (s_rec[i].cls == cls && s_rec[i].clsName == clsName) return &s_rec[i];
  if (s_recN >= ACC_MAX_CLASSES) {
    if (!s_fullLogged) np_log("accuracy: %d classes patched already; %ls left alone", s_recN, uname_str(clsName));
    s_fullLogged = true;
    return 0;
  }
  AccRec& r = s_rec[s_recN++];
  r.cls = cls;
  r.clsName = clsName;
  for (int k = 0; k < ACC_PROPS; k++) r.r[k] = optrestore_init();
  r.loggedLevel = 0;
  return &r;
}

static void patch(AccRec* rec, unsigned level) {
  UProp props[ACC_PROPS];
  float vals[ACC_PROPS];
  int ok[ACC_PROPS];
  void* cdo = 0;
  if (!read_spreads(rec->cls, props, vals, ok, &cdo)) return;
  int wrote = 0;
  for (int k = 0; k < ACC_PROPS; k++) {
    if (!ok[k] || opt_step(rec->r[k], 1, vals[k], 0.f) != OPT_APPLY) continue;
    rec->r[k] = optrestore_apply(rec->r[k], 0, vals[k], 0.f);
    if (vals[k] != 0.f) {
      engine_set_default_property(L"0", (void*)rec->cls, (void*)props[k].field, props[k].offset);
      wrote++;
    }
  }
  if (rec->loggedLevel != level) {
    rec->loggedLevel = level;
    np_log("accuracy: %ls: HorizShotError %.9g VertShotError %.9g ShrunkHorizShotError %.9g ShrunkVertShotError "
           "%.9g -> 0 (GlobalSetProperty on the class default, bNoSave=1; %d written now)", uname_str(rec->clsName),
           rec->r[0].saved, rec->r[1].saved, rec->r[2].saved, rec->r[3].saved, wrote);
  }
}

static void restore(AccRec* rec) {
  UProp props[ACC_PROPS];
  float vals[ACC_PROPS];
  int ok[ACC_PROPS];
  void* cdo = 0;
  if (!read_spreads(rec->cls, props, vals, ok, &cdo)) {
    np_log("accuracy: %ls: class default unreadable; nothing restored", uname_str(rec->clsName));
    return;
  }
  float back[ACC_PROPS] = { 0.f, 0.f, 0.f, 0.f };
  int restored = 0, left = 0;
  for (int k = 0; k < ACC_PROPS; k++) {
    if (!ok[k] || !rec->r[k].applied) continue;
    if (!optrestore_release(rec->r[k], vals[k], &back[k])) {
      left++;
      continue;
    }
    if (back[k] == vals[k]) continue;   // it was 0 before ours: nothing to write
    wchar_t text[32];
    acc_value_text(back[k], text, 32);
    engine_set_default_property(text, (void*)rec->cls, (void*)props[k].field, props[k].offset);
    restored++;
  }
  np_log("accuracy: %ls: restored HorizShotError %.9g VertShotError %.9g ShrunkHorizShotError %.9g "
         "ShrunkVertShotError %.9g (%d written, %d changed by the game since and left)", uname_str(rec->clsName),
         back[0], back[1], back[2], back[3], restored, left);
}

// The player's shot counter and view kick (PlayerPawn.AccuracyShots, PlayerPawn.CurrentShake: Pitch, Yaw).
static bool read_kick(const void* player, int* shots, int* pitch, int* yaw) {
  __try {
    const void* pc = uobj_class(player);
    const UProp sp = uprop_get(pc, s_nShots);
    const UProp kp = uprop_get(pc, s_nShake);
    if (!sp.ok || sp.type != UW_INT || !kp.ok || kp.type != UW_STRUCT || kp.elemSize != 12) return false;
    *shots = uw_int(player, sp.offset);
    *pitch = uw_int(player, kp.offset);
    *yaw = uw_int(player, kp.offset + 4);
    return true;
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

// ZoomErrorPct on the class default.
static float read_zoom(const void* cls, const void* cdo) {
  float zoom = -1.f;
  __try {
    const UProp zp = uprop_get(cls, s_nZoom);
    if (zp.ok && zp.type == UW_FLOAT && zp.elemSize == 4) memcpy(&zoom, (const char*)cdo + zp.offset, 4);
  } __except (av_only(GetExceptionCode())) {
  }
  return zoom;
}

// The held class's ViewKicks on its default: the ShakeMagnitude property (found in the element struct by name),
// the array's block and count, the element size (the Inner property's ElementSize) and the first n magnitudes.
struct RkView { void* cdo; const void* field; unsigned data; int total, n, elemSize, fieldOfs; int mag[RK_MAX_KICKS]; };
static bool read_kicks(const void* cls, RkView* v) {
  __try {
    v->cdo = engine_get_default_object((void*)cls);
    if (!v->cdo) return false;
    const UProp ap = uprop_get(cls, s_nKicks);
    if (!ap.ok || ap.type != UW_ARRAY || !ap.field) return false;
    const void* inner = uw_ptr(ap.field, UAP_INNER);
    if (!inner || !uobj_valid(inner)) return false;
    const void* st = uw_ptr(inner, USP_STRUCT);
    if (!st || !uobj_valid(st)) return false;
    v->elemSize = *(const unsigned short*)((const char*)inner + UP_ELEMSIZE);
    const UProp mp = uprop_get(st, s_nShakeMag);
    if (!mp.ok || mp.type != UW_INT || mp.elemSize != 4 || !mp.field) return false;
    if (v->elemSize <= 0 || v->elemSize > RK_ELEM_MAX || mp.offset < 0 || mp.offset + 4 > v->elemSize) return false;
    v->field = mp.field;
    v->fieldOfs = mp.offset;
    const void* data = uw_ptr(v->cdo, ap.offset);
    v->data = (unsigned)(uintptr_t)data;
    v->total = uw_int(v->cdo, ap.offset + 4);
    v->n = data ? rk_kick_count(v->total) : 0;
    if (v->n && !engine_readable(data, (size_t)(v->n * v->elemSize))) return false;
    for (int i = 0; i < v->n; i++) v->mag[i] = uw_int(data, i * v->elemSize + v->fieldOfs);
    return true;
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

static const char* rk_check(void) {
  s_nKicks = uname_find(L"ViewKicks");
  s_nShakeMag = uname_find(L"ShakeMagnitude");
  if (!s_nWeapon || !s_nWeaponConfig || !s_nKicks || !s_nShakeMag)
    return "the names Weapon, WeaponConfig, ViewKicks or ShakeMagnitude are not in the game";
  const EngineExports* e = engine_exports();
  if (!e->globalSetProperty || !e->getDefaultObject) return "GlobalSetProperty or GetDefaultObject is not exported";
  return 0;
}

static RkRec* rk_record_for(const void* cls, int clsName) {
  int added = 0;
  const int i = acc_slot(s_rk, &s_rkN, ACC_MAX_CLASSES, cls, clsName, &added);
  if (i < 0) {
    if (cls && !s_rkFullLogged) np_log("recoil: %d classes patched already; %ls left alone", s_rkN, uname_str(clsName));
    s_rkFullLogged = cls != 0;
    return 0;
  }
  if (added) {
    s_rk[i].n = 0;
    for (int k = 0; k < RK_MAX_KICKS; k++) s_rk[i].r[k] = optrestore_init();
    s_rk[i].loggedLevel = 0;
  }
  return &s_rk[i];
}

static void rk_list(const int* m, int n, char* out, int cap) {
  int at = 0;
  out[0] = 0;
  for (int i = 0; i < n && at < cap - 1; i++) {
    const int w = _snprintf_s(out + at, (size_t)(cap - at), _TRUNCATE, i ? ",%d" : "%d", m[i]);
    if (w < 0) break;
    at += w;
  }
  if (!n) strcpy_s(out, (size_t)cap, "none");
}

// Each kick's magnitude to 0 by GlobalSetProperty through the ShakeMagnitude IntProperty: its ImportText writes the
// int at the address it is given (UIntProperty::ImportText 0x1004BD00), here default + offset = the element's field.
static void rk_patch(RkRec* rec, unsigned level) {
  RkView v;
  if (!read_kicks(rec->cls, &v)) return;
  int wrote = 0;
  for (int i = 0; i < v.n; i++) {
    if (opt_step(rec->r[i], 1, (float)v.mag[i], 0.f) != OPT_APPLY) continue;
    rec->r[i] = optrestore_apply(rec->r[i], 0, (float)v.mag[i], 0.f);
    if (v.mag[i] != 0) {
      // The offset is from the class default: valid only because engine_set_default_property passes bAllInstances 0.
      engine_set_default_property(L"0", (void*)rec->cls, (void*)v.field,
                                  rk_field_offset((unsigned)(uintptr_t)v.cdo, v.data, i, v.elemSize, v.fieldOfs));
      wrote++;
    }
  }
  if (v.n > rec->n) rec->n = v.n;
  if (acc_log_due(rec->loggedLevel, level)) {
    rec->loggedLevel = level;
    int saved[RK_MAX_KICKS];
    for (int i = 0; i < rec->n; i++) saved[i] = (int)rec->r[i].saved;
    RkView now;
    const bool reread = read_kicks(rec->cls, &now);
    char was[96], is[96];
    rk_list(saved, rec->n, was, sizeof was);
    rk_list(now.mag, reread ? now.n : 0, is, sizeof is);
    np_log("recoil: %ls: ViewKicks %d, ShakeMagnitude %s -> %s (GlobalSetProperty through the ShakeMagnitude "
           "property, bNoSave=1, kick 0 at default%+d; %d written now; element %d bytes, field +%d)",
           uname_str(rec->clsName), v.total, was, is,
           v.n ? rk_field_offset((unsigned)(uintptr_t)v.cdo, v.data, 0, v.elemSize, v.fieldOfs) : 0, wrote,
           v.elemSize, v.fieldOfs);
  }
}

static void rk_restore(RkRec* rec) {
  RkView v;
  if (!read_kicks(rec->cls, &v)) {
    np_log("recoil: %ls: class default unreadable; nothing restored", uname_str(rec->clsName));
    return;
  }
  int back[RK_MAX_KICKS];
  int restored = 0, left = 0;
  const int n = rec->n < v.n ? rec->n : v.n;
  for (int i = 0; i < n; i++) {
    back[i] = v.mag[i];
    if (!rec->r[i].applied) continue;
    float b = 0.f;
    if (!optrestore_release(rec->r[i], (float)v.mag[i], &b)) {
      left++;
      continue;
    }
    back[i] = (int)b;
    if (back[i] == v.mag[i]) continue;   // it was 0 before ours: nothing to write
    wchar_t text[16];
    rk_int_text(back[i], text, 16);
    engine_set_default_property(text, (void*)rec->cls, (void*)v.field,
                                rk_field_offset((unsigned)(uintptr_t)v.cdo, v.data, i, v.elemSize, v.fieldOfs));
    restored++;
  }
  char list[96];
  rk_list(back, n, list, sizeof list);
  np_log("recoil: %ls: restored ShakeMagnitude %s (%d written, %d changed by the game since and left)",
         uname_str(rec->clsName), list, restored, left);
}

// Logs once per weapon class and switch state what PerformFire read for a shot (the class default's spreads and
// kick magnitudes) and the view kick around it, so a launch shows the random cone and the kick before and after.
static void probe(const void* player) {
  if (!s_probeChecked) {
    s_probeChecked = true;
    s_nShots = uname_find(L"AccuracyShots");
    s_nShake = uname_find(L"CurrentShake");
    s_nZoom = uname_find(L"ZoomErrorPct");
    s_probeOff = !s_nShots || !s_nShake || !s_nZoom;
    if (s_probeOff) np_log("accuracy: fire probe off: AccuracyShots, CurrentShake or ZoomErrorPct not found");
  }
  if (s_probeOff) return;
  int shots = 0, pitch = 0, yaw = 0;
  if (!read_kick(player, &shots, &pitch, &yaw)) return;
  const int closed = acc_probe_step(&s_probe, shots, pitch, yaw);
  if (s_probe.open && s_probe.frames == 1) {   // the shot frame: what the class default holds now
    s_shot.ok = 0;
    s_shot.state = (g_cfg.accuracyOn ? 1 : 0) + (g_cfg.recoilOn ? 2 : 0);
    const void* cfg = held_config(player);
    s_shot.clsName = cfg ? name_of(cfg) : 0;
    UProp props[ACC_PROPS];
    int ok[ACC_PROPS];
    void* cdo = 0;
    if (cfg && read_spreads(cfg, props, s_shot.spread, ok, &cdo)) {
      s_shot.zoom = read_zoom(cfg, cdo);
      RkView v;
      s_shot.kicks = -1;
      s_shot.kickN = 0;
      if (!s_rkRefused && read_kicks(cfg, &v)) {
        s_shot.kicks = v.total;
        s_shot.kickN = v.n < 4 ? v.n : 4;
        for (int i = 0; i < s_shot.kickN; i++) s_shot.mag[i] = v.mag[i];
      }
      s_shot.ok = ok[0] && ok[1] && ok[2] && ok[3];
    }
  }
  if (!closed || !s_shot.ok) return;
  ProbeSeen* seen = 0;
  for (int i = 0; i < s_seenN && !seen; i++)
    if (s_seen[i].clsName == s_shot.clsName) seen = &s_seen[i];
  if (!seen && s_seenN < ACC_MAX_CLASSES) {
    seen = &s_seen[s_seenN++];
    seen->clsName = s_shot.clsName;
    seen->mask = 0;
  }
  if (!seen || !acc_probe_claim(&seen->mask, s_shot.state)) return;
  char mags[64];
  rk_list(s_shot.mag, s_shot.kickN, mags, sizeof mags);
  np_log("accuracy: fire probe %ls (accuracy %s, no recoil %s): the shot read HorizShotError %.9g VertShotError %.9g "
         "ShrunkHorizShotError %.9g ShrunkVertShotError %.9g ZoomErrorPct %.9g, ViewKicks %d ShakeMagnitude %s; "
         "view kick (CurrentShake, in the aim) pitch %d yaw %d at the shot, peak over %d frames pitch %d yaw %d "
         "(%.2f / %.2f deg)", uname_str(s_shot.clsName), (s_shot.state & 1) ? "on" : "off",
         (s_shot.state & 2) ? "on" : "off", s_shot.spread[0], s_shot.spread[1], s_shot.spread[2], s_shot.spread[3],
         s_shot.zoom, s_shot.kicks, mags, s_probe.firstPitch, s_probe.firstYaw, ACC_PROBE_FRAMES, s_probe.peakPitch,
         s_probe.peakYaw, acc_rot_deg(s_probe.peakPitch), acc_rot_deg(s_probe.peakYaw));
}

// Drops a record whose class went with the old level; a class still loaded keeps our values (and its record).
template <class R>
static void keep_loaded(R* recs, int* n, const char* tag) {
  int kept = 0;
  for (int i = 0; i < *n; i++) {
    const bool valid = obj_ok(recs[i].cls);
    if (acc_keep_record(valid, valid ? name_of(recs[i].cls) : 0, recs[i].clsName)) recs[kept++] = recs[i];
    else np_log("%s: %ls unloaded with the level; its record is dropped", tag, uname_str(recs[i].clsName));
  }
  *n = kept;
}

void accuracy_frame(void) {
  void* player = world_player();
  if (!player) return;
  const unsigned level = world_level_serial();
  const bool newLevel = level != s_level;
  if (level != s_checkLevel) {
    s_checkLevel = level;
    if (s_refused || s_rkRefused) s_checked = false;   // a refusal is checked again once per level, logged once
  }
  if (!s_checked) {
    s_checked = true;
    const char* why = check();
    if (why && why != s_refused) np_log("accuracy: unavailable: %s; nothing is written", why);
    s_refused = why;
    const char* rkWhy = rk_check();
    if (rkWhy && rkWhy != s_rkRefused) np_log("recoil: unavailable: %s; nothing is written", rkWhy);
    s_rkRefused = rkWhy;
  }
  if (s_refused && s_rkRefused) return;
  if (newLevel) {
    s_level = level;
    keep_loaded(s_rec, &s_recN, "accuracy");
    keep_loaded(s_rk, &s_rkN, "recoil");
    s_probe = acc_probe_init();   // a new pawn: its shot counter starts over
  }
  if (!s_refused) probe(player);
  // Every frame while on: a weapon switch patches the new class, and a game rewrite of a default is undone.
  // Outside standalone play both levers read as off: what was patched is put back (mpgate.h).
  const bool mpOff = mp_refused() != 0;
  const bool accOn = g_cfg.accuracyOn && !mpOff, rkOn = g_cfg.recoilOn && !mpOff;
  const bool want = (accOn && !s_refused) || (rkOn && !s_rkRefused);
  const void* cfg = want ? held_config(player) : 0;
  const int cfgName = cfg ? name_of(cfg) : 0;
  if (!s_refused) {
    if (accOn) {
      AccRec* rec = cfg ? record_for(cfg, cfgName) : 0;
      if (rec) patch(rec, level);
    } else if (s_recN) {
      for (int i = 0; i < s_recN; i++)
        if (obj_ok(s_rec[i].cls) && name_of(s_rec[i].cls) == s_rec[i].clsName) restore(&s_rec[i]);
      s_recN = 0;
      s_fullLogged = false;
    }
  }
  if (!s_rkRefused) {
    if (rkOn) {
      RkRec* rec = cfg ? rk_record_for(cfg, cfgName) : 0;
      if (rec) rk_patch(rec, level);
    } else if (s_rkN) {
      for (int i = 0; i < s_rkN; i++)
        if (obj_ok(s_rk[i].cls) && name_of(s_rk[i].cls) == s_rk[i].clsName) rk_restore(&s_rk[i]);
      s_rkN = 0;
      s_rkFullLogged = false;
    }
  }
}

const char* accuracy_refused(void) { return s_refused ? s_refused : mp_refused(); }
const char* recoil_refused(void) { return s_rkRefused ? s_rkRefused : mp_refused(); }

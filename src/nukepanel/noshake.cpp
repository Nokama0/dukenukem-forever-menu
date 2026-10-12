// No screen shake (noshake.h), game thread. PlayerPawn.ActiveShakes is a dnArray {data, count} of SActiveViewShake,
// laid out as UpdateViewShake (0x10267CB0) and ProcessShake (0x10265AB0) read it: array +0x1574, entry 0x48,
// ShakeName +8, Magnitude +0x18, FullMagnitude +0x1C, Limit +0x20, Result +0x24. Zeroed entries rest until retriggered.
#include <windows.h>
#include <string.h>
#include "noshake.h"
#include "pehook.h"
#include "world.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "config.h"
#include "log.h"

enum { NATIVE_ARRAY = 0x1574, NATIVE_ELEM = 0x48, NATIVE_NAME = 0x8, NATIVE_MAG = 0x18, NATIVE_FULL = 0x1C,
       NATIVE_LIMIT = 0x20, NATIVE_RESULT = 0x24 };
static const int PROP_INNER = 0x78;   // UArrayProperty::Inner, UStructProperty::Struct

struct Layout { int ok; int arr, elem, name, mag, full, limit, result; };
static Layout s_l;
static const void* s_cls;
static unsigned s_level;
static const char* s_refused;
static int s_sub = -1;
static int s_engaged;

// One record per zeroed entry, keyed by its name and its rank among the entries of that name.
struct Rec { int name, ord, seen; ShakeHold h; };
static Rec s_rec[NS_MAX_ENTRIES];
static int s_recN;
// Each name's verdict for the session, logged once: a weapon kick (kept) or not (zeroed). qol.md counts dozens of
// shake sources; past the cap a name is looked up on every pass and counted.
struct Kind { int name, kick; };
enum { MAX_NAMES = 256 };
static Kind s_kind[MAX_NAMES];
static int s_kindN;
static unsigned s_frames, s_zeroed, s_restored, s_faults, s_capped, s_namesOver;

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

static bool inner_of(const void* propField, const void** inner, const void** strct, int* elem, int* innerCls) {
  __try {
    *inner = uw_ptr(propField, PROP_INNER);
    if (!*inner || !uobj_valid(*inner)) return false;
    *innerCls = uobj_name(uobj_class(*inner));
    *elem = *(const unsigned short*)((const char*)*inner + UP_ELEMSIZE);
    *strct = uw_ptr(*inner, PROP_INNER);
    return *strct && uobj_valid(*strct);
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

static const char* resolve(const void* cls) {
  const UProp arr = prop(cls, L"ActiveShakes");
  if (!arr.ok || arr.type != UW_ARRAY || !arr.field) return "PlayerPawn.ActiveShakes did not resolve";
  const void *inner = 0, *strct = 0;
  int elem = 0, innerCls = 0;
  if (!inner_of(arr.field, &inner, &strct, &elem, &innerCls) || innerCls != uname_find(L"StructProperty"))
    return "ActiveShakes is not an array of a struct";
  const UProp nm = prop(strct, L"ShakeName"), mag = prop(strct, L"Magnitude"), full = prop(strct, L"FullMagnitude");
  const UProp lim = prop(strct, L"Limit"), res = prop(strct, L"Result");
  np_log("noshake: ActiveShakes +0x%x, entry 0x%x (struct %p): ShakeName +0x%x Magnitude +0x%x FullMagnitude +0x%x "
         "Limit +0x%x Result +0x%x size %d", arr.offset, elem, strct, nm.offset, mag.offset, full.offset, lim.offset,
         res.offset, res.elemSize);
  if (!nm.ok || nm.elemSize != 4 || !mag.ok || mag.type != UW_INT || !full.ok || full.type != UW_INT || !lim.ok ||
      lim.type != UW_INT || !res.ok || res.type != UW_STRUCT || res.elemSize != 12)
    return "SActiveViewShake's fields did not resolve";
  if (arr.offset != NATIVE_ARRAY || elem != NATIVE_ELEM || nm.offset != NATIVE_NAME || mag.offset != NATIVE_MAG ||
      full.offset != NATIVE_FULL || lim.offset != NATIVE_LIMIT || res.offset != NATIVE_RESULT)
    return "the shake layout differs from what UpdateViewShake reads";
  Layout l = { 1, arr.offset, elem, nm.offset, mag.offset, full.offset, lim.offset, res.offset };
  s_l = l;
  return 0;
}

// The verdict for a name, cached; a new name is logged once (the hook may run this: np_log is safe there).
static int is_kick(int name) {
  for (int i = 0; i < s_kindN; i++)
    if (s_kind[i].name == name) return s_kind[i].kick;
  const wchar_t* s = uname_str(name);
  const int kick = ns_is_kick(s);
  if (s_kindN < MAX_NAMES) {
    s_kind[s_kindN].name = name;
    s_kind[s_kindN].kick = kick;
    ++s_kindN;
    np_log("noshake: shake '%ls' %s", s && *s ? s : L"?", kick ? "kept (a weapon kick, No recoil's)" :
           "zeroed while No screen shake is on");
  } else {
    ++s_namesOver;   // not cached, not logged: looked up again each pass
  }
  return kick;
}

static Rec* rec_for(int name, int ord, bool create) {
  for (int i = 0; i < s_recN; i++)
    if (s_rec[i].name == name && s_rec[i].ord == ord) return &s_rec[i];
  if (!create || s_recN >= NS_MAX_ENTRIES) return 0;
  Rec& r = s_rec[s_recN++];
  memset(&r, 0, sizeof r);
  r.name = name;
  r.ord = ord;
  return &r;
}

// The array {data, count}, read once per pass.
static bool read_array(const void* player, unsigned char** data, int* count) {
  __try {
    *data = *(unsigned char* const*)((const char*)player + s_l.arr);
    *count = *(const int*)((const char*)player + s_l.arr + 4);
    return true;
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

static bool read_entry(const unsigned char* e, int* name, int* v) {
  __try {
    memcpy(name, e + s_l.name, 4);
    memcpy(&v[0], e + s_l.mag, 4);
    memcpy(&v[1], e + s_l.full, 4);
    memcpy(&v[2], e + s_l.limit, 4);
    memcpy(&v[3], e + s_l.result, 12);
    return true;
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

static bool write_entry(unsigned char* e, const int* v) {
  __try {
    memcpy(e + s_l.mag, &v[0], 4);
    memcpy(e + s_l.full, &v[1], 4);
    memcpy(e + s_l.limit, &v[2], 4);
    memcpy(e + s_l.result, &v[3], 12);
    return true;
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

// on: zero every non-kick entry; off: give back what the records hold, then forget them.
static void pass(void* player, int on) {
  unsigned char* data;
  int count;
  if (!s_l.ok || !player || !read_array(player, &data, &count)) return;
  if (count < 0 || (count && !data)) return;
  if (count > NS_MAX_ENTRIES) { ++s_capped; count = NS_MAX_ENTRIES; }
  for (int i = 0; i < s_recN; i++) s_rec[i].seen = 0;
  int names[NS_MAX_ENTRIES];
  for (int i = 0; i < count; i++) {
    unsigned char* e = data + i * s_l.elem;
    int name, v[NS_VALS], w[NS_VALS];
    if (!read_entry(e, &name, v)) { ++s_faults; return; }
    names[i] = name;
    if (is_kick(name)) continue;
    int ord = 0;
    for (int j = 0; j < i; j++) ord += names[j] == name;
    Rec* r = rec_for(name, ord, on != 0);
    if (!r) continue;
    r->seen = 1;
    const int act = ns_step(&r->h, on, v, w);
    if (act == CVAR_NONE) continue;
    if (!write_entry(e, w)) { ++s_faults; return; }
    if (act == CVAR_FORCE) ++s_zeroed;
    else ++s_restored;
  }
  if (!on) { s_recN = 0; return; }
  // Entries that ended (StopShake) take their records with them.
  int k = 0;
  for (int i = 0; i < s_recN; i++)
    if (s_rec[i].seen) s_rec[k++] = s_rec[i];
  s_recN = k;
}

static void on_event(void* obj, void* func, void* parms) {
  (void)func;
  (void)parms;
  if (s_engaged && s_cls && obj == world_player()) pass(obj, 1);
}

void noshake_frame(void) {
  if (s_sub < 0) s_sub = pehook_subscribe(L"TriggerViewRotationAdjust", on_event, "noshake");
  void* player = world_player();
  const void* cls = player ? class_of(player) : 0;
  const unsigned level = world_level_serial();
  if (level != s_level) {
    s_level = level;
    s_recN = 0;   // another pawn's entries: nothing of the old level is written back
  }
  const int want = g_cfg.noShake != 0;
  if (want && cls && cls != s_cls && !s_refused) {
    const char* why = resolve(cls);
    if (why) {
      s_refused = why;
      np_log("noshake: unavailable: %s", why);
    } else {
      s_cls = cls;
    }
  }
  const int on = want && !s_refused && s_cls;
  if (on != s_engaged) {
    if (!on && player) pass(player, 0);
    np_log("noshake: %s; %u entr%s given back", on ? "on: shakes other than weapon kicks are zeroed" : "off",
           s_restored, s_restored == 1 ? "y" : "ies");
    s_restored = 0;
    s_engaged = on;
    if (!on) s_recN = 0;
  }
  pehook_enable(s_sub, on);
  // Ticks without PlayerMove (vehicles, cutscenes) raise no event: this pass covers them a frame late.
  if (on && player) pass(player, 1);
  if (++s_frames < 600) return;
  s_frames = 0;
  if (on || s_zeroed)
    np_log("noshake: 600 frames: %u zeroing(s), %d entr%s held, %u fault(s), %u pass(es) over %d entries, %d name(s) "
           "known, %u lookup(s) past the %d-name cap", s_zeroed, s_recN, s_recN == 1 ? "y" : "ies", s_faults, s_capped,
           NS_MAX_ENTRIES, s_kindN, s_namesOver, MAX_NAMES);
  s_zeroed = s_faults = s_capped = s_namesOver = 0;
}

// Without the slot hook the Present pass still runs (a frame late), so only a layout refusal disables the row.
const char* noshake_refused(void) { return s_refused; }

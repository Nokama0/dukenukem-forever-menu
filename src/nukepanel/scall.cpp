// FindFunction + ProcessEvent (docs/re/engine-map.md section 3). Engine reads run under an access-violation guard
// as world.cpp's do; the ProcessEvent call itself does not: a fault inside the engine's script must not be hidden.
#include <windows.h>
#include <wchar.h>
#include "scall.h"
#include "scallparms.h"
#include "engine.h"
#include "uobj.h"
#include "uname.h"
#include "log.h"

// UFunction FunctionFlags and ParmsSize (u16), as ProcessEvent reads them (engine-map.md section 1).
static const int UFN_FLAGS = 0x88, UFN_PARMSSIZE = 0x92;

// str: the caller's constant string; clsName: the class's own name, so a class address reused by another class
// finds again, a cached miss included.
struct Entry { const void* cls; int clsName; const wchar_t* str; ScallFn f; };
static const int CACHE_N = 32;   // a handful of functions on a few classes
static Entry s_cache[CACHE_N];
static int s_cacheN, s_cacheNext;

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

// obj's class and that class's name; false when a read faulted.
static bool class_of(const void* obj, const void** cls, int* clsName) {
  __try {
    *cls = uobj_class(obj);
    *clsName = *cls && uobj_valid(*cls) ? uobj_name(*cls) : 0;
    return *cls != 0;
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

// The function's Outer (its declaring class) is obj's class or one of its supers. A state's function has the UState
// as its Outer, and the state the declaring class as its own (scall_find_state).
static bool fn_in_chain(const void* obj, const void* fn) {
  __try {
    const void* outer = uobj_outer(fn);
    if (!outer) return false;
    const void* cls = uobj_class(obj);
    if (uobj_class_isa(cls, outer)) return true;
    const void* stateOuter = uobj_valid(outer) ? uobj_outer(outer) : 0;
    return stateOuter && uobj_class_isa(cls, stateOuter);
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

// FindFunction on obj; *faulted is set when a read faulted (a stale object), which is not cached.
static ScallFn resolve(void* obj, int nameIdx, int global, bool* faulted) {
  ScallFn r = { 0, 0, 0 };
  *faulted = false;
  __try {
    void* fn = engine_find_function(obj, nameIdx, global);
    if (fn && uobj_valid(fn)) {
      r.fn = fn;
      r.parmsSize = *(const unsigned short*)((const char*)fn + UFN_PARMSSIZE);
      r.ok = 1;
    }
  } __except (av_only(GetExceptionCode())) {
    *faulted = true;
    r.fn = 0;
    r.ok = 0;
  }
  return r;
}

static void remember(const void* cls, int clsName, const wchar_t* str, const ScallFn& f) {
  Entry& e = s_cache[s_cacheN < CACHE_N ? s_cacheN++ : (s_cacheNext++ % CACHE_N)];
  e.cls = cls;
  e.clsName = clsName;
  e.str = str;
  e.f = f;
}

ScallFn scall_find(void* obj, const wchar_t* name) {
  ScallFn none = { 0, 0, 0 };
  if (!engine_exports()->findFunction || !obj || !name || !obj_ok(obj)) return none;
  const void* cls = 0;
  int clsName = 0;
  if (!class_of(obj, &cls, &clsName)) return none;
  for (int i = 0; i < s_cacheN; i++) {
    Entry& e = s_cache[i];
    if (e.cls != cls || e.clsName != clsName || !e.str || wcscmp(e.str, name) != 0) continue;
    if (!e.f.ok || obj_ok(e.f.fn)) return e.f;
    e.cls = 0;   // its UFunction left the table: find again
    break;
  }
  const int nameIdx = uname_find(name);
  if (!nameIdx) {
    np_log("scall: no name %ls in the name table (class %p); cached", name, cls);
    remember(cls, clsName, name, none);
    return none;
  }
  bool faulted;
  const ScallFn r = resolve(obj, nameIdx, 1, &faulted);
  if (faulted) {
    np_log("scall: FindFunction(%ls) faulted on %p", name, obj);
    return none;
  }
  if (!r.ok) np_log("scall: %ls not found on %p's class", name, obj);
  remember(cls, clsName, name, r);
  return r;
}

ScallFn scall_find_state(void* obj, const wchar_t* name) {
  ScallFn none = { 0, 0, 0 };
  if (!engine_exports()->findFunction || !obj || !name || !obj_ok(obj)) return none;
  const int nameIdx = uname_find(name);
  if (!nameIdx) return none;
  bool faulted;
  const ScallFn r = resolve(obj, nameIdx, 0, &faulted);
  return faulted ? none : r;
}

int scall_call(void* obj, const ScallFn& f, void* parms, unsigned cap) {
  const EngineExports* e = engine_exports();
  if (!f.ok || !obj || !e->gIsScriptable || !*e->gIsScriptable) return 0;
  if (!scall_parms_fit(f.parmsSize, parms, cap)) {
    static int s_logged;   // the first few: a caller's block too small for the function it found
    if (s_logged < 8) {
      s_logged++;
      np_log("scall: refused: %u-byte parms for a %u-byte block (fn %p)", (unsigned)f.parmsSize, cap, f.fn);
    }
    return 0;
  }
  if (!obj_ok(obj) || !obj_ok(f.fn) || !fn_in_chain(obj, f.fn)) return 0;
  engine_process_event(obj, f.fn, parms);
  return 1;
}

static int find_parm(const ScallFn& f, int nameIdx, int clsIdx, ScallParm* out) {
  __try {
    const void* p = uw_ptr(f.fn, US_CHILDREN);
    for (int n = 0; p && n < 32; n++, p = uw_ptr(p, UF_NEXT)) {
      if (!uobj_valid(p)) return 0;
      if (uobj_name(p) != nameIdx) continue;
      if (uobj_name(uobj_class(p)) != clsIdx) return 0;
      const unsigned ofs = (unsigned)uw_int(p, UP_OFFSET);
      const unsigned size = *(const unsigned short*)((const char*)p + UP_ELEMSIZE);
      const unsigned dim = *(const unsigned short*)((const char*)p + UP_ARRAYDIM);
      if (dim != 1 || !size || ofs >= f.parmsSize || size > (unsigned)f.parmsSize - ofs) return 0;
      out->offset = ofs;
      out->size = size;
      return 1;
    }
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
  return 0;
}

int scall_parm(const ScallFn& f, const wchar_t* name, const wchar_t* propClass, ScallParm* out) {
  if (!f.ok || !name || !propClass || !out || !obj_ok(f.fn)) return 0;
  const int nameIdx = uname_find(name), clsIdx = uname_find(propClass);
  if (!nameIdx || !clsIdx) return 0;
  return find_parm(f, nameIdx, clsIdx, out);
}

static int find_parms_of(const ScallFn& f, int clsIdx, ScallParm* out, int cap) {
  int n = 0;
  __try {
    const void* p = uw_ptr(f.fn, US_CHILDREN);
    for (int k = 0; p && k < 32; k++, p = uw_ptr(p, UF_NEXT)) {
      if (!uobj_valid(p)) return -1;
      if (uobj_name(uobj_class(p)) != clsIdx) continue;
      const unsigned ofs = (unsigned)uw_int(p, UP_OFFSET);
      const unsigned size = *(const unsigned short*)((const char*)p + UP_ELEMSIZE);
      const unsigned dim = *(const unsigned short*)((const char*)p + UP_ARRAYDIM);
      if (dim != 1 || !size || ofs >= f.parmsSize || size > (unsigned)f.parmsSize - ofs) continue;   // a local
      if (n < cap) {
        int j = n;
        for (; j > 0 && out[j - 1].offset > ofs; j--) out[j] = out[j - 1];
        out[j].offset = ofs;
        out[j].size = size;
      }
      n++;
    }
  } __except (av_only(GetExceptionCode())) {
    return -1;
  }
  return n;
}

int scall_parms_of(const ScallFn& f, const wchar_t* propClass, ScallParm* out, int cap) {
  if (!f.ok || !propClass || !out || cap < 0 || !obj_ok(f.fn)) return -1;
  const int clsIdx = uname_find(propClass);
  if (!clsIdx) return -1;
  return find_parms_of(f, clsIdx, out, cap);
}

// One function's line and its parameters (Children: UStruct +0x58, UField::Next +0x2C), through deferred name
// tokens: names are fetched outside the guard (uname.h).
static void log_fn(void* player, const wchar_t* name) {
  const ScallFn f = scall_find(player, name);
  if (!f.ok) return;
  __try {
    const unsigned flags = *(const unsigned*)((const char*)f.fn + UFN_FLAGS);
    uname_dlog("scall: %s.%s fn=%p ParmsSize=%u flags=0x%08x", uname_tok(uobj_name(uobj_outer(f.fn))),
               uname_tok(uobj_name(f.fn)), f.fn, (unsigned)f.parmsSize, flags);
    const void* p = uw_ptr(f.fn, US_CHILDREN);
    for (int n = 0; p && n < 16; n++, p = uw_ptr(p, UF_NEXT)) {
      if (!uobj_valid(p)) {
        uname_dlog("scall:   parameter %d: %p not in the object table", n, p);
        break;
      }
      uname_dlog("scall:   %s %s +0x%x size=%u", uname_tok(uobj_name(uobj_class(p))), uname_tok(uobj_name(p)),
                 (unsigned)uw_int(p, UP_OFFSET), (unsigned)*(const unsigned short*)((const char*)p + UP_ELEMSIZE));
    }
  } __except (av_only(GetExceptionCode())) {
    uname_dlog("scall: reading %p's parameters faulted", f.fn);
  }
}

void scall_selfcheck(void* player) {
  static bool s_done;
  if (s_done || !player) return;
  s_done = true;
  const EngineExports* e = engine_exports();
  void* slot1 = 0;
  __try {
    slot1 = (*(void* const* const*)player)[1];
  } __except (av_only(GetExceptionCode())) {
    slot1 = 0;
  }
  np_log("scall: self-check on player %p: vtable slot 1 = %p (ProcessEvent export %p)", player, slot1,
         e->processEvent);
  // FOV, game speed (the per-tick event that applies DebugSlomo), pause (research only).
  static const wchar_t* const FNS[] = { L"SetScaleModifier", L"ClearScaleModifier", L"HasScaleModifier",
                                        L"GetScaleModifierTarget", L"UpdateGameSpeed",
                                        L"ForcePause_NoFade", L"ForceUnPause" };
  for (int i = 0; i < (int)(sizeof FNS / sizeof FNS[0]); i++) log_fn(player, FNS[i]);
  uname_dlog_flush();
}

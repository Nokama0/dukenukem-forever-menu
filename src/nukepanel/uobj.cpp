// The object table snapshot (docs/re/engine-map.md section 1): GObjObjects is a dnArray {data +0, count +4,
// max +8}; slots may be null (the free list). Game thread only: the engine adds objects on this thread.
#include <windows.h>
#include "uobj.h"
#include "uname.h"
#include "engine.h"
#include "log.h"

static void* const* s_data;
static int s_count;
static void* const* s_checkedData;   // the span last checked readable, and the answer
static int s_checkedMax;
static bool s_checkedOk;
static bool s_badLogged;

bool uobj_snapshot(void) {
  s_data = 0;
  s_count = 0;
  const EngineExports* e = engine_exports();
  if (!e->gObjObjects) return false;
  const char* arr = (const char*)e->gObjObjects;
  void* const* data = *(void* const* const*)arr;
  const int count = *(const int*)(arr + 4);
  const int max = *(const int*)(arr + 8);
  if (!data || count <= 0 || max < count || max > UOBJ_MAX_COUNT) {
    if (!s_badLogged) {
      s_badLogged = true;
      np_log("uobj: object table unusable: data=%p count=%d max=%d", (const void*)data, count, max);
    }
    return false;
  }
  if (data != s_checkedData || max != s_checkedMax) {
    s_checkedData = data;
    s_checkedMax = max;
    s_checkedOk = engine_readable(data, (size_t)max * sizeof(void*));
    np_log("uobj: object table data=%p count=%d max=%d readable=%d", (const void*)data, count, max,
           s_checkedOk ? 1 : 0);
  }
  if (!s_checkedOk) return false;
  s_data = data;
  s_count = count;
  return true;
}

int uobj_count(void) { return s_count; }

const void* uobj_at(int i) { return i >= 0 && i < s_count ? s_data[i] : 0; }

bool uobj_at_is(int idx, const void* p) { return p && idx >= 0 && idx < s_count && s_data[idx] == p; }

bool uobj_valid(const void* p) {
  const uintptr_t a = (uintptr_t)p;
  if (a < 0x10000 || (a & 3)) return false;
  return uobj_at_is(uw_int(p, UO_INDEX), p);
}

static int is_property(const void* field, void* propertyClass) {
  return uobj_class_isa(uobj_class(field), propertyClass);
}

const UwLayout* uobj_layout(const void* propertyClass) {
  static UwLayout plain = { US_SUPER, US_CHILDREN, UF_NEXT, UO_NAME, UP_OFFSET, 0, 0 };
  static UwLayout props = { US_SUPER, US_CHILDREN, UF_NEXT, UO_NAME, UP_OFFSET, is_property, 0 };
  if (!propertyClass) return &plain;
  props.ctx = (void*)propertyClass;
  return &props;
}

int uobj_find_classes(const wchar_t* const* names, const void** out, int n) {
  const int classIdx = uname_find(L"Class");
  int want[16];
  if (n > 16) n = 16;
  for (int k = 0; k < n; k++) {
    out[k] = 0;
    want[k] = uname_find(names[k]);
  }
  if (!classIdx) return 0;
  int found = 0;
  for (int i = 0; i < s_count && found < n; i++) {
    const void* o = s_data[i];
    if (!o) continue;
    const int nm = uobj_name(o);
    for (int k = 0; k < n; k++) {
      if (out[k] || !want[k] || nm != want[k]) continue;
      const void* c = uobj_class(o);
      if (c && uobj_name(c) == classIdx) {
        out[k] = o;
        found++;
      }
      break;
    }
  }
  return found;
}

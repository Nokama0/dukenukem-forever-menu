#pragma once
// Pure walks over the engine's reflection records (docs/re/engine-map.md): IsA over
// the SuperField chain, a property by dnName over Children/Next then the super chain, and the pawn list. The
// records are read through a layout of offsets, so the suite runs the same walks over synthetic structs.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Offsets of the fields each walk reads. isProp, when set, filters Children to property fields (a function or
// struct may share a property's name); the suite leaves it null.
struct UwLayout {
  int superOfs, childrenOfs, nextOfs, nameOfs, offsetOfs;
  int (*isProp)(const void* field, void* ctx);
  void* ctx;
};

inline const void* uw_ptr(const void* p, int ofs) { return *(const void* const*)((const char*)p + ofs); }
inline int uw_int(const void* p, int ofs) { return *(const int*)((const char*)p + ofs); }

// cls derives from (or is) target within cap steps. A cyclic chain ends at the cap with false.
inline int uw_isa_l(const UwLayout* L, const void* cls, const void* target, int cap) {
  for (int i = 0; cls && i < cap; i++) {
    if (cls == target) return 1;
    cls = uw_ptr(cls, L->superOfs);
  }
  return 0;
}

struct UWProp { int ok; int offset; const void* field; };

// Field chains are long (every property and function a class declares), so the per-class cap is generous; the
// super chain is capped by `depth`. NAME_None (0) never matches.
const int UW_FIELD_CAP = 4096;
inline UWProp uw_find_prop_l(const UwLayout* L, const void* cls, int name, int depth) {
  UWProp r = { 0, 0, 0 };
  if (!name) return r;
  for (int d = 0; cls && d < depth; d++, cls = uw_ptr(cls, L->superOfs)) {
    const void* f = uw_ptr(cls, L->childrenOfs);
    for (int n = 0; f && n < UW_FIELD_CAP; n++, f = uw_ptr(f, L->nextOfs)) {
      if (uw_int(f, L->nameOfs) != name) continue;
      if (L->isProp && !L->isProp(f, L->ctx)) continue;
      r.ok = 1;
      r.offset = uw_int(f, L->offsetOfs);
      r.field = f;
      return r;
    }
  }
  return r;
}

enum { UW_STOP_END, UW_STOP_FOREIGN, UW_STOP_CYCLE, UW_STOP_CAP };
inline const char* uw_stop_name(int stop) {
  switch (stop) {
    case UW_STOP_END:     return "end";
    case UW_STOP_FOREIGN: return "foreign";
    case UW_STOP_CYCLE:   return "cycle";
    default:              return "cap";
  }
}

struct UWList { int count; int stop; };
struct UwListOps {
  const void* (*next)(void* ctx, const void* p);
  int (*valid)(void* ctx, const void* p);   // in this frame's object snapshot (and the right class)
  void* ctx;
};

// Follows next from head into out[0..cap). Stops at null, a pointer that fails valid (never dereferenced: a pawn
// destroyed mid-frame), a repeat, or the cap. The repeat check is quadratic, fine for a few hundred pawns.
inline UWList uw_list(const void* head, const UwListOps* ops, const void** out, int cap) {
  UWList r = { 0, UW_STOP_END };
  for (const void* p = head; p; p = ops->next(ops->ctx, p)) {
    if (r.count >= cap) { r.stop = UW_STOP_CAP; return r; }
    if (!ops->valid(ops->ctx, p)) { r.stop = UW_STOP_FOREIGN; return r; }
    for (int i = 0; i < r.count; i++)
      if (out[i] == p) { r.stop = UW_STOP_CYCLE; return r; }
    out[r.count++] = p;
  }
  return r;
}

// A property's value type, from its class name (IntProperty, FloatProperty, ...).
enum { UW_OTHER, UW_INT, UW_FLOAT, UW_BOOL, UW_BYTE, UW_OBJECT, UW_CLASS, UW_STRUCT, UW_STR, UW_ARRAY };

// Health as an int whatever the property's type: DNF's RenderActor.Health is a FloatProperty (Engine.u), a
// subclass port might use an int. Floats truncate toward zero and clamp (a float-to-int overflow is undefined).
inline int uw_read_health(const void* obj, int ofs, int type) {
  const char* p = (const char*)obj + ofs;
  if (type == UW_INT) { int v; memcpy(&v, p, 4); return v; }
  if (type == UW_FLOAT) {
    float f; memcpy(&f, p, 4);
    if (!(f == f)) return 0;   // NaN
    if (f >= 2147483520.f) return 0x7FFFFFFF;
    if (f <= -2147483520.f) return -0x7FFFFFFF;
    return (int)f;
  }
  return 0;
}

// Alive from the raw value: a float health in (0, 1) truncates to 0 but the pawn is still alive.
inline int uw_health_alive(const void* obj, int ofs, int type) {
  const char* p = (const char*)obj + ofs;
  if (type == UW_INT) { int v; memcpy(&v, p, 4); return v > 0; }
  if (type == UW_FLOAT) { float f; memcpy(&f, p, 4); return f > 0.f; }   // NaN compares false
  return 0;
}

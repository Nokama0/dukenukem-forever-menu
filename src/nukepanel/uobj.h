#pragma once
// The object table (UObject::GObjObjects) as a per-frame snapshot, and the UObject/UStruct header reads.
// Game thread only. Offsets: docs/re/engine-map.md section 1.
#include "uwalk.h"

// UObject header (engine-map.md section 1, each read by an exported accessor).
const int UO_INDEX = 0x04, UO_OUTER = 0x18, UO_NAME = 0x20, UO_CLASS = 0x24;
// UStruct SuperField and Children, UField Next (engine-map.md section 1).
const int US_SUPER = 0x28, UF_NEXT = 0x2C, US_CHILDREN = 0x58;
// UProperty Offset, ArrayDim (u16), ElementSize (u16), and UBoolProperty BitMask (engine-map.md section 1).
const int UP_OFFSET = 0x38, UP_ARRAYDIM = 0x3C, UP_ELEMSIZE = 0x3E, UBP_BITMASK = 0x78;
// The ceiling a sane table stays under; a larger count is garbage, not a level.
const int UOBJ_MAX_COUNT = 4 * 1024 * 1024;

// Takes this frame's {data, count}. The array span is checked readable only when its data or max changes
// (a guarded read costs ~12 us). False when the table is unavailable or fails its checks.
bool uobj_snapshot(void);
int uobj_count(void);
const void* uobj_at(int i);              // a snapshot slot, null when free or out of range

// In this frame's snapshot: p->Index maps back to p. p's Index is read before p is validated, so call it only inside
// an access-violation __try: unguarded, a freed actor would crash the game instead of failing the check.
bool uobj_valid(const void* p);
// The same check without touching p: slot idx of the snapshot still holds p.
bool uobj_at_is(int idx, const void* p);

inline const void* uobj_class(const void* p) { return uw_ptr(p, UO_CLASS); }
inline const void* uobj_outer(const void* p) { return uw_ptr(p, UO_OUTER); }
inline int uobj_name(const void* p) { return uw_int(p, UO_NAME); }

// The engine layout for uwalk's walks; Children is filtered to fields whose class derives from `propertyClass`.
const UwLayout* uobj_layout(const void* propertyClass);
inline bool uobj_class_isa(const void* cls, const void* target) {
  return uw_isa_l(uobj_layout(0), cls, target, 64) != 0;
}

// One table pass: for each name, the object of that name whose own class is named "Class". Unfound stay null.
// Returns how many were found.
int uobj_find_classes(const wchar_t* const* names, const void** out, int n);

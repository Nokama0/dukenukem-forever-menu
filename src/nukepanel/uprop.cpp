// The property resolver over uwalk's chain walk, with the engine layout from uobj.h. Read only.
#include <windows.h>
#include "uprop.h"
#include "uobj.h"
#include "uname.h"

static const void* s_propertyClass;

struct CacheEntry { const void* cls; int name; UProp p; };
// Two-way sets by (class, name): a level uses a few dozen pawn classes times a dozen properties, and a collision
// in a direct-mapped table would re-walk Children every frame.
static const int CACHE_SETS = 512;
static CacheEntry s_cache[CACHE_SETS][2];
static unsigned char s_victim[CACHE_SETS];   // the way the next miss in this set replaces
static const void* s_logged[128];   // fields logged this level (hits), and misses as (cls ^ name) keys
static int s_loggedN;

static const wchar_t* const TYPE_NAMES[] = { L"IntProperty", L"FloatProperty", L"BoolProperty", L"ByteProperty",
                                             L"ObjectProperty", L"ClassProperty", L"StructProperty", L"StrProperty",
                                             L"ArrayProperty" };
static const int TYPE_IDS[] = { UW_INT, UW_FLOAT, UW_BOOL, UW_BYTE, UW_OBJECT, UW_CLASS, UW_STRUCT, UW_STR, UW_ARRAY };
static const int TYPE_N = (int)(sizeof TYPE_IDS / sizeof TYPE_IDS[0]);
static int s_typeIdx[TYPE_N];

void uprop_set_property_class(const void* propertyClass) { s_propertyClass = propertyClass; }

void uprop_reset(void) {
  memset(s_cache, 0, sizeof s_cache);
  memset(s_victim, 0, sizeof s_victim);
  s_loggedN = 0;
}

static int type_of(const void* field) {
  const int cn = uobj_name(uobj_class(field));
  for (int i = 0; i < TYPE_N; i++) {
    if (!s_typeIdx[i]) s_typeIdx[i] = uname_find(TYPE_NAMES[i]);
    if (s_typeIdx[i] && s_typeIdx[i] == cn) return TYPE_IDS[i];
  }
  return UW_OTHER;
}

static bool log_once(const void* key) {
  for (int i = 0; i < s_loggedN; i++)
    if (s_logged[i] == key) return false;
  if (s_loggedN >= 128) return false;   // full: stay quiet rather than log every frame
  s_logged[s_loggedN++] = key;
  return true;
}

static UProp resolve(const void* cls, int nameIdx) {
  UProp r = { 0, 0, UW_OTHER, 0, 0, 0 };
  if (!cls || !nameIdx || !s_propertyClass) return r;
  const UWProp w = uw_find_prop_l(uobj_layout(s_propertyClass), cls, nameIdx, 64);
  if (!w.ok || w.offset < 0 || w.offset >= 0x100000) {
    if (log_once((const void*)((uintptr_t)cls ^ (uintptr_t)nameIdx)))
      uname_dlog("uprop: %s missing on class %s", uname_tok(nameIdx), uname_tok(uobj_name(cls)));
    return r;
  }
  r.ok = 1;
  r.offset = w.offset;
  r.field = w.field;
  r.type = type_of(w.field);
  r.elemSize = *(const unsigned short*)((const char*)w.field + UP_ELEMSIZE);
  if (r.type == UW_BOOL) r.bitmask = *(const unsigned*)((const char*)w.field + UBP_BITMASK);
  if (log_once(w.field)) {
    // The declaring class is the property's Outer.
    const void* outer = uobj_outer(w.field);
    uname_dlog("uprop: %s.%s +0x%x %s size=%d dim=%d", outer ? uname_tok(uobj_name(outer)) : "?",
               uname_tok(nameIdx), (unsigned)r.offset, uname_tok(uobj_name(uobj_class(w.field))), r.elemSize,
               (int)*(const unsigned short*)((const char*)w.field + UP_ARRAYDIM));
  }
  return r;
}

UProp uprop_get(const void* cls, int nameIdx) {
  const unsigned h = (unsigned)(((uintptr_t)cls >> 4) * 31u + (unsigned)nameIdx) % CACHE_SETS;
  CacheEntry* set = s_cache[h];
  for (int w = 0; w < 2; w++)
    if (cls && set[w].cls == cls && set[w].name == nameIdx) return set[w].p;
  const UProp r = resolve(cls, nameIdx);
  if (!s_propertyClass) return r;   // not cached: a later call with the class set must resolve again
  // Filled only after resolve returns, so a fault inside it leaves no half-written entry.
  CacheEntry& e = set[s_victim[h]];
  s_victim[h] ^= 1;
  e.p = r;
  e.name = nameIdx;
  e.cls = cls;
  return r;
}

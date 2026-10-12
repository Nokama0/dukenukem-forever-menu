// Breakables glue (breakables.h). worldscan's pass keeps the purposeful ones (a padlock, a known stash, or an Event
// set); each frame reads which are still intact under one access-violation guard and names them outside it (names
// take the engine's lock). Read only, game thread.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "breakables.h"
#include "world.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "progress.h"   // pg_field_in
#include "espnames.h"   // esp_wide_to_ascii
#include "config.h"
#include "log.h"
#include "lvnames.h"

static WsCache s_row;
static const char* const kRowNames[1] = { "Breakables" };

enum { N_LOCATION, N_RADIUS, N_HEIGHT, N_DELETEME, N_HIDDEN, N_NODAMAGE, N_HEALTHPREFAB, N_DECOSTATE, N_BROKEN,
       N_PARTS, N_EVENT, N_SPAWNEDINV, N_SNACKS, N_COUNT };
// Actor, RenderActor (Engine.u), dnDecoration and dnDestructibleBuilding (dnGame.u), Biology_Generic_GasPod
// (dnDecorations.u).
static const wchar_t* const kProps[N_COUNT] = { L"Location", L"CollisionRadius", L"CollisionHeight", L"bDeleteMe",
                                                L"bHidden", L"bNoDamage", L"HealthPrefab", L"DecoState", L"bBroken",
                                                L"Parts", L"Event", L"SpawnedInventory", L"SpawnedSnacks" };
static int s_n[N_COUNT];
static int s_ruleName[BK_CLASS_COUNT];
static int s_stashEvent[BK_STASH_COUNT];
static LnKeys s_lnKeys;   // stash Events then class names, in that order

static unsigned s_calls, s_frames, s_shown, s_stashes, s_gone, s_faults, s_emptyCrates, s_itemCrates;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

// --- worldscan's client ------------------------------------------------------------------------------------------
static int bk_wanted(void) { return g_cfg.espBreakables; }

static int bk_resolve(void) {
  for (int k = 0; k < N_COUNT; ++k)
    if (!s_n[k]) s_n[k] = uname_find(kProps[k]);
  // A stash Event is a map's name and moves when the level reloads (2026-10-11: catfishCounter 79488 -> 86824): every
  // key is looked up again before each pass, and a lookup that finds nothing is absent.
  const char* names[BK_STASH_COUNT + BK_CLASS_COUNT];
  for (int i = 0; i < BK_STASH_COUNT; ++i) names[i] = BK_STASHES[i].event;
  for (int r = 0; r < BK_CLASS_COUNT; ++r) names[BK_STASH_COUNT + r] = BK_CLASSES[r].name;
  static_assert(BK_STASH_COUNT + BK_CLASS_COUNT <= LN_MAX, "lvnames.h keeps LN_MAX keys");
  ln_refresh(&s_lnKeys, names, BK_STASH_COUNT + BK_CLASS_COUNT, ln_find_ascii, 0);
  memcpy(s_stashEvent, s_lnKeys.cur, sizeof s_stashEvent);
  memcpy(s_ruleName, s_lnKeys.cur + BK_STASH_COUNT, sizeof s_ruleName);
  ln_report("breakables", &s_lnKeys, names, BK_STASH_COUNT + BK_CLASS_COUNT);
  int got = 0;
  for (int r = 0; r < BK_CLASS_COUNT; ++r) got += s_ruleName[r] != 0;
  return got;
}

static int bk_class_rule(const void* cls) {
  int chain[32];
  int n = 0;
  for (const void* c = cls; c && n < 32; c = uw_ptr(c, US_SUPER)) chain[n++] = uobj_name(c);
  const int r = it_rule_of_chain(chain, n, s_ruleName, BK_CLASS_COUNT);
  return r;
}

// Under worldscan's guard. Every known row has its Event set, so it is matched by that name alone (map and instance
// names change on a checkpoint load).
static int bk_row_of(const void* o, const void* cls, int* rule) {
  if (*rule < 0 || *rule >= BK_CLASS_COUNT) return -1;
  const int kind = BK_CLASSES[*rule].kind;
  int ev = 0;
  const UProp e = uprop_get(cls, s_n[N_EVENT]);
  if (e.ok && e.elemSize == 4 && pg_field_in(e.offset, 4)) ev = uw_int(o, e.offset);
  if (!ev && kind != BK_PADLOCK) return -1;
  const int stash = bk_stash_of(ev, s_stashEvent, BK_STASH_COUNT);
  if (!bk_purposeful(kind, ev != 0, stash)) return -1;
  *rule = bk_rule_pack(kind, stash);
  return 0;
}

const WsClient g_breakScan = { "breakables", 1, &s_row, kRowNames, bk_wanted, bk_resolve, bk_class_rule, bk_row_of };

// --- Markers -----------------------------------------------------------------------------------------------------
enum { BK_CHAIN = 8 };
struct Raw { int clsName, kind, stash, holds, chainN, chain[BK_CHAIN]; float x, y, z, radius, height; MbCorners box; };
static Raw s_raw[WS_MAX];
static BkMarker s_markers[WS_MAX];

static int read_bool(const void* o, const void* cls, int k) {
  const UProp p = uprop_get(cls, s_n[k]);
  return p.ok && p.type == UW_BOOL && pg_field_in(p.offset, 4) && (uw_int(o, p.offset) & (int)p.bitmask) != 0;
}

// A byte property, or def when it does not resolve.
static int read_byte(const void* o, const void* cls, int k, int def) {
  const UProp p = uprop_get(cls, s_n[k]);
  if (!p.ok || p.type != UW_BYTE || !pg_field_in(p.offset, 1)) return def;
  return *((const unsigned char*)o + p.offset);
}

static bool read_entry(const WsEntry& en, Raw* r) {
  const void* o = en.obj;
  const void* cls = uobj_class(o);
  r->kind = bk_rule_kind(en.rule);
  r->stash = bk_rule_stash(en.rule);
  r->clsName = uobj_name(cls);
  // The class chain's name indexes (no names lock here, under the guard); the family check reads them after it.
  r->chainN = 0;
  r->holds = 0;
  if (r->kind == BK_DECO) {   // a crate's rule needs the chain and whether it spawns pickups
    for (int k = N_SPAWNEDINV; k <= N_SNACKS; ++k) {
      const UProp a = uprop_get(cls, s_n[k]);
      if (a.ok && a.type == UW_ARRAY && pg_field_in(a.offset, 8)) {
        const int num = uw_int(o, a.offset + 4);
        r->holds |= num > 0 && num <= 64;
      }
    }
  }
  if (r->kind == BK_DECO)
    for (const void* c = cls; c && r->chainN < BK_CHAIN; c = uw_ptr(c, US_SUPER)) r->chain[r->chainN++] = uobj_name(c);
  int parts = -1;
  if (r->kind == BK_BUILDING) {
    const UProp p = uprop_get(cls, s_n[N_PARTS]);
    if (p.ok && p.type == UW_ARRAY && pg_field_in(p.offset, 8)) parts = uw_int(o, p.offset + 4);
  }
  if (!bk_alive(r->kind, read_bool(o, cls, N_DELETEME), read_bool(o, cls, N_HIDDEN), read_bool(o, cls, N_NODAMAGE),
                read_byte(o, cls, N_HEALTHPREFAB, -1), read_byte(o, cls, N_DECOSTATE, 0),
                r->kind == BK_GASPOD ? read_bool(o, cls, N_BROKEN) : 0, parts))
    return false;
  const UProp loc = uprop_get(cls, s_n[N_LOCATION]);
  if (!loc.ok || loc.type != UW_STRUCT || loc.elemSize < 12 || !pg_field_in(loc.offset, 12)) return false;
  float xyz[3];
  memcpy(xyz, (const char*)o + loc.offset, sizeof xyz);
  r->x = xyz[0];
  r->y = xyz[1];
  r->z = xyz[2];
  r->radius = r->height = 0.f;
  const UProp rad = uprop_get(cls, s_n[N_RADIUS]), hgt = uprop_get(cls, s_n[N_HEIGHT]);
  if (rad.ok && rad.type == UW_FLOAT && pg_field_in(rad.offset, 4)) memcpy(&r->radius, (const char*)o + rad.offset, 4);
  if (hgt.ok && hgt.type == UW_FLOAT && pg_field_in(hgt.offset, 4)) memcpy(&r->height, (const char*)o + hgt.offset, 4);
  modelbox_read(o, cls, &r->box);
  return r->x == r->x && r->y == r->y && r->z == r->z;
}

static int collect(void) {
  int n = 0;
  __try {
    for (int i = 0; i < s_row.n; ++i) {
      if (read_entry(s_row.e[i], &s_raw[n])) n++;
      else s_gone++;
    }
  } __except (av_only(GetExceptionCode())) {
    s_faults++;
    return 0;
  }
  return n;
}

// The short label per class name index, so the names lock is taken once per class. Names are append-only, so an
// entry never goes stale.
struct LabelSlot { int idx; char s[48]; };
static LabelSlot s_labels[64];
static const char* short_label(int kind, int nameIdx) {
  LabelSlot& l = s_labels[(unsigned)nameIdx % 64];
  if (l.idx == nameIdx && nameIdx && l.s[0]) return l.s;
  char cls[64];
  esp_wide_to_ascii(uname_str(nameIdx), cls, sizeof cls);
  bk_label(kind, -1, cls, l.s, sizeof l.s);
  l.idx = nameIdx;
  return l.s;
}

// A plain decoration with an Event shows only in a breakable family (items.md section 5), and a crate only when it
// holds items. Outside any guard: the names lock is taken once per leaf class, cached by its name index (names are
// append-only).
struct FamilySlot { int idx; int ok; int crate; };
static FamilySlot s_family[128];
static const FamilySlot& family_of(const Raw& r) {
  FamilySlot& f = s_family[(unsigned)r.clsName % 128];
  if (f.idx == r.clsName && r.clsName) return f;
  int ok = 0, crate = BK_CR_NONE;
  for (int i = 0; i < r.chainN; ++i) {
    char a[96];
    if (!esp_wide_to_ascii(uname_str(r.chain[i]), a, sizeof a)) continue;
    if (bk_family_ok(a)) ok = 1;
    const int c = bk_crate_class(a);
    if (c > crate) crate = c;
  }
  f.idx = r.clsName;
  f.ok = ok;
  f.crate = crate;
  return f;
}
static bool in_family(const Raw& r, int* crate) {
  *crate = BK_CR_NONE;
  if (r.kind != BK_DECO) return true;
  const FamilySlot& f = family_of(r);
  *crate = f.crate;
  return r.stash >= 0 || f.ok != 0;
}

int breakables_markers(const BkMarker** out) {
  *out = s_markers;
  int shown = 0;
  if (g_cfg.espBreakables && world_view().ok && world_player() && worldscan_ready()) {
    modelbox_frame();
    const int n = collect();
    for (int i = 0; i < n; ++i) {
      const Raw& r = s_raw[i];
      int crate;
      if (!in_family(r, &crate)) continue;   // an Event outside the breakable families: a light, switch or cup
      if (!bk_crate_shown(crate, bk_crate_holds(r.holds, r.stash))) {
        (crate == BK_CR_ITEM ? s_itemCrates : s_emptyCrates)++;   // the Items row marks item crates
        continue;
      }
      BkMarker& m = s_markers[shown++];
      if (r.stash >= 0 || r.kind == BK_PADLOCK) bk_label(r.kind, r.stash, 0, m.label, sizeof m.label);
      else if (crate == BK_CR_CRATE) bk_crate_label(m.label, sizeof m.label);
      else strcpy_s(m.label, short_label(r.kind, r.clsName));
      m.x = r.x;
      m.y = r.y;
      m.z = r.z;
      m.radius = r.radius;
      m.height = r.height;
      m.box = r.box;
      m.stash = bk_stash_is_path(r.stash) ? -1 : r.stash;   // a path breakable keeps the breakable colour
      s_stashes += m.stash >= 0;
    }
    s_frames++;
    s_shown += (unsigned)shown;
    uname_dlog_flush();
  }
  if (++s_calls % 600 == 0 && s_frames) {
    np_log("breakables: frames=%u cached=%d shown avg %.1f (stashes %.1f) gone=%u faults=%u; crates hidden: empty %.1f, item crates %.1f (avg per frame)",
           s_frames, s_row.n, (double)s_shown / s_frames, (double)s_stashes / s_frames, s_gone, s_faults,
           (double)s_emptyCrates / s_frames, (double)s_itemCrates / s_frames);
    s_frames = s_shown = s_stashes = s_gone = s_faults = s_emptyCrates = s_itemCrates = 0;
  }
  return shown;
}

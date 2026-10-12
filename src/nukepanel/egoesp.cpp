// Ego boosts ESP glue (egoesp.h holds the rules): one object-table scan per level into a cache of award givers,
// each entry validated against the frame's snapshot, then this frame's givers whose award progress.cpp has not
// seen found. Read only, game thread, every engine read under one access-violation guard (world.cpp's rule).
#include <windows.h>
#include <string.h>
#include "egoesp.h"
#include "modelbox.h"
#include "world.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "mswindow.h"
#include "log.h"
#include "lvnames.h"

enum { N_LOCATION, N_RADIUS, N_HEIGHT, N_LEVEL, N_DELETEME, N_BOOSTGIVES, N_BOOSTID, N_CANSWITCH, N_CPU, N_KEYBOARDS,
       N_HIDDEN, N_EGOENABLED, N_TAG, N_TPAWARDS, N_TRIGGERPLAYER, N_CHEATED, N_COUNT };
// Actor (Engine.u), Settings_Workout_PunchKick_EgoBooster, Electronics_Generic_Computer_Monitor (CPU) and _CPU
// (Keyboards) (dnDecorations.u), TriggerPlayer (dnGame.u; the last entry is its class name).
static const wchar_t* const kProps[N_COUNT] = { L"Location", L"CollisionRadius", L"CollisionHeight", L"Level",
                                                L"bDeleteMe", L"bGivePermanentEgoAward", L"PermanentEgoAwardID",
                                                L"bCanSwitch", L"CPU", L"Keyboards", L"bHidden",
                                                L"EgoCapAwardsEnabled", L"Tag", L"EgoCapAwards", L"TriggerPlayer", L"bHasCheatedThisLevel" };
static int s_n[N_COUNT];
// For the rule set of the last build (eg_rules: base or DLC), each sized for the base set, the larger.
static int s_ruleName[EG_CLASS_COUNT];   // each rule's class name index, 0 until the name exists
static int s_evCls[EG_EVENT_COUNT], s_evTag[EG_EVENT_COUNT];   // the event rules' class and Tag name indices
static int s_tpName, s_tpAwards;   // the set's TriggerPlayer class and its award list's property
// UArrayProperty::Inner (+0x78, collect.cpp's citation): EgoCapAwards' element size is checked through it.
static const int UAP_INNER = 0x78;

// Class -> giver rule (-1 none), event rules on its chain, TriggerPlayer or not; direct-mapped by class pointer and
// cleared at each build (classes may be reused).
struct ClsSlot { const void* cls; int rule; unsigned evMask; int tp; };
static ClsSlot s_clsCache[256];

static EgCache s_cache;
static const void* s_cacheLevel;
static EgMarker s_markers[EG_MAX];

static unsigned s_calls, s_builds, s_frames, s_shown, s_pruned, s_faults, s_unwired;
static double s_buildMs;
static unsigned s_closed;   // frames with the markers hidden because the player cannot earn awards
static int s_lastOpen = -1;
static DWORD s_retryAt;   // after a fault: no rebuild before this tick, so a bad object never scans every frame
static MsWindow s_cost;
static LARGE_INTEGER s_qpf;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static double ms_since(const LARGE_INTEGER& t0) {
  LARGE_INTEGER t1;
  QueryPerformanceCounter(&t1);
  if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
  return (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)s_qpf.QuadPart;
}

// An event Tag and the map-package classes are a map's names and move when the level reloads (2026-10-11): every build
// looks them all up again, and a lookup that finds nothing is absent.
static LnKeys s_lnKeys, s_lnDlc;   // class names, event classes, event Tags (DLC: then its TriggerPlayer and list)
static void resolve_names(int dlc) {
  for (int k = 0; k < N_COUNT; ++k)
    if (!s_n[k]) s_n[k] = uname_find(kProps[k]);
  const EgRuleSet& set = eg_rules(dlc);
  enum { NK = EG_CLASS_COUNT + 2 * EG_EVENT_COUNT + 2 };
  static_assert(NK <= LN_MAX, "lvnames.h keeps LN_MAX keys");
  const char* names[NK];
  int nk = 0;
  for (int r = 0; r < set.nCls; ++r) names[nk++] = set.cls[r].name;
  for (int k = 0; k < set.nEv; ++k) names[nk++] = set.ev[k].cls;
  for (int k = 0; k < set.nEv; ++k) names[nk++] = set.ev[k].tag;
  // The DLC's TriggerPlayer class and its list's name come with its packages: looked up with the rest.
  if (dlc) { names[nk++] = set.tpClass; names[nk++] = set.tpAwards; }
  LnKeys* keys = dlc ? &s_lnDlc : &s_lnKeys;
  ln_refresh(keys, names, nk, ln_find_ascii, 0);
  memset(s_ruleName, 0, sizeof s_ruleName);
  memset(s_evCls, 0, sizeof s_evCls);
  memset(s_evTag, 0, sizeof s_evTag);
  memcpy(s_ruleName, keys->cur, sizeof(int) * set.nCls);
  memcpy(s_evCls, keys->cur + set.nCls, sizeof(int) * set.nEv);
  memcpy(s_evTag, keys->cur + set.nCls + set.nEv, sizeof(int) * set.nEv);
  s_tpName = dlc ? keys->cur[nk - 2] : s_n[N_TRIGGERPLAYER];
  s_tpAwards = dlc ? keys->cur[nk - 1] : s_n[N_TPAWARDS];
  ln_report(dlc ? "egoesp dlc" : "egoesp", keys, names, nk);
}

// The nearest giver class of the cache's set along cls's super chain (-1 for none), the event rules whose class is on
// it, and whether it is the set's TriggerPlayer.
static const ClsSlot& class_info(const void* cls) {
  ClsSlot& s = s_clsCache[((uintptr_t)cls >> 4) % 256];
  if (s.cls == cls && cls) return s;
  const EgRuleSet& set = eg_rules(s_cache.dlc);
  int chain[32];
  int n = 0;
  for (const void* c = cls; c && n < 32; c = uw_ptr(c, US_SUPER)) chain[n++] = uobj_name(c);
  int tp = 0;
  for (int d = 0; d < n && s_tpName; ++d) tp |= chain[d] == s_tpName;
  s.cls = cls;
  s.rule = eg_class_rule(chain, n, s_ruleName, set.nCls);
  s.evMask = eg_event_mask_n(chain, n, s_evCls, set.nEv);
  s.tp = tp;
  return s;
}

// A TriggerPlayer's award list (EgoCapAwards, the DLC's EgoCapAwardsDLC03: array<byte>) into the level's listed set.
static void note_trigger_player(const void* o, const void* cls) {
  const UProp p = uprop_get(cls, s_tpAwards);
  if (!p.ok || p.type != UW_ARRAY || !p.field || !pg_field_in(p.offset, 8)) return;
  const void* inner = uw_ptr(p.field, UAP_INNER);
  if (!inner || *(const unsigned short*)((const char*)inner + UP_ELEMSIZE) != 1) return;
  const unsigned char* data = (const unsigned char*)uw_ptr(o, p.offset);
  const int n = uw_int(o, p.offset + 4);
  if (data && n > 0 && n <= 64) eg_note_listed_n(s_cache.listed, data, n, eg_rules(s_cache.dlc).awards);
}

// Every live actor of a giver class or an event rule whose Level is this level (class defaults have none, so they
// never count), and the awards this level's TriggerPlayers list.
static bool build_body(const void* level) {
  const int n = uobj_count();
  for (int i = 0; i < n; ++i) {
    const void* o = uobj_at(i);
    if (!o) continue;
    const void* cls = uobj_class(o);
    if (!cls) continue;
    const ClsSlot ci = class_info(cls);
    if (ci.rule < 0 && !ci.evMask && !ci.tp) continue;
    const UProp lv = uprop_get(cls, s_n[N_LEVEL]);
    if (!lv.ok || lv.type != UW_OBJECT || !pg_field_in(lv.offset, 4) || uw_ptr(o, lv.offset) != level) continue;
    if (ci.rule >= 0) {
      eg_cache_add(&s_cache, o, i, ci.rule);
    } else if (ci.tp) {
      note_trigger_player(o, cls);
    } else {
      const UProp tg = uprop_get(cls, s_n[N_TAG]);
      const int tag = tg.ok && tg.elemSize == 4 && pg_field_in(tg.offset, 4) ? uw_int(o, tg.offset) : 0;
      const EgRuleSet& set = eg_rules(s_cache.dlc);
      const int k = eg_event_pick_n(ci.evMask, tag, s_evTag, set.nEv);
      if (k >= 0) eg_cache_add(&s_cache, o, i, set.nCls + k);
    }
  }
  return true;
}

// One line per event rule this level touches (an actor of it, or a TriggerPlayer listing its award): resolved, or why
// not. A level with neither gets one summary line.
static void log_events(unsigned serial) {
  const EgRuleSet& set = eg_rules(s_cache.dlc);
  char listed[128];
  int o = 0;
  listed[0] = 0;
  for (int id = 0; id < PG_AWARDS && o < (int)sizeof listed - 4; ++id)
    if (s_cache.listed[id]) o += _snprintf_s(listed + o, sizeof listed - o, _TRUNCATE, " %d", id);
  int touched = 0;
  for (int k = 0; k < set.nEv; ++k) {
    const EgEventRule& r = set.ev[k];
    int actors = 0;
    for (int i = 0; i < s_cache.n; ++i) actors += eg_rule_event_in(set, s_cache.e[i].rule) == k;
    const int lists = s_cache.listed[r.id];
    if (!actors && !lists) continue;
    touched++;
    char label[40];
    eg_label_in(s_cache.dlc, r.id, label, sizeof label);
    const char* why = actors && lists ? "marked while unfound"
                      : !s_evTag[k] ? "not resolved: its Tag name is not loaded"
                      : !actors ? "not resolved: no such actor in this level"
                                : "not resolved: no TriggerPlayer here lists the award";
    np_log("egoesp: level serial %u: event award %d %s (%s %s Tag '%s'): %d actor(s), %s", serial, r.id, label, r.map,
           r.cls, r.tag, actors, why);
  }
  if (!touched)
    np_log("egoesp: level serial %u: no event-award markers here (%s list:%s)", serial, set.tpClass,
           listed[0] ? listed : " none");
}

static void build(const void* level, unsigned serial, int dlc) {
  eg_cache_reset(&s_cache, serial);
  s_cache.dlc = dlc;
  s_cacheLevel = level;
  memset(s_clsCache, 0, sizeof s_clsCache);
  resolve_names(dlc);
  const EgRuleSet& set = eg_rules(dlc);
  LARGE_INTEGER t0;
  QueryPerformanceCounter(&t0);
  bool ok = false;
  __try {
    ok = build_body(level);
  } __except (av_only(GetExceptionCode())) {
    ok = false;
  }
  s_buildMs = ms_since(t0);
  s_builds++;
  if (ok) s_retryAt = 0;
  if (!ok) {
    s_faults++;
    s_cache.n = 0;
    s_cache.built = 0;
    s_retryAt = GetTickCount() + 1000;
  }
  int perRule[EG_CLASS_COUNT] = {};
  for (int i = 0; i < s_cache.n; ++i)
    if (s_cache.e[i].rule < set.nCls) perRule[s_cache.e[i].rule]++;
  char list[512];
  int o = 0;
  list[0] = 0;
  for (int r = 0; r < set.nCls && o < (int)sizeof list - 1; ++r) {
    if (!perRule[r]) continue;
    const int w = _snprintf_s(list + o, sizeof list - o, _TRUNCATE, " %s x%d", set.cls[r].name, perRule[r]);
    if (w < 0) break;
    o += w;
  }
  int names = 0;
  for (int r = 0; r < set.nCls; ++r) names += s_ruleName[r] != 0;
  np_log("egoesp: level serial %u: %s rules, %d giver(s) in %d objects, %.2f ms%s%s; %d/%d class names loaded, %s %s;%s",
         serial, dlc ? "DLC" : "base", s_cache.n, uobj_count(), s_buildMs, s_cache.overflow ? ", OVERFLOW" : "",
         ok ? "" : ", FAULTED", names, set.nCls, set.tpClass, s_tpName ? "resolved" : "absent", list[0] ? list : " none");
  log_events(serial);
}

static bool slot_alive(void*, int slot, const void* obj) { return uobj_at_is(slot, obj); }

static bool read_bool(const void* o, const UProp& p) {
  return p.ok && p.type == UW_BOOL && pg_field_in(p.offset, 4) && (uw_int(o, p.offset) & (int)p.bitmask) != 0;
}

// One giver's marker, or false when it gives nothing now, its awards are found, or it is being destroyed.
static bool marker_of(const EgEntry& e, const unsigned char found[PG_AWARDS], EgMarker* m) {
  const void* o = e.obj;
  const void* cls = uobj_class(o);
  if (read_bool(o, uprop_get(cls, s_n[N_DELETEME]))) return false;
  int ids[EG_IDS];
  int n = 0;
  const EgRuleSet& set = eg_rules(s_cache.dlc);
  const int ev = eg_rule_event_in(set, e.rule);
  if (ev >= 0) {
    const int hidden = set.ev[ev].skipHidden ? read_bool(o, uprop_get(cls, s_n[N_HIDDEN])) : 0;
    ids[0] = eg_event_id(set.ev[ev], s_cache.listed, hidden);
    n = ids[0] >= 0 ? 1 : 0;
  } else {
    if (e.rule < 0 || e.rule >= set.nCls) return false;
    const EgClassRule& r = set.cls[e.rule];
    int gives = 0, boosterId = -1, canSwitch = 0, wired = 0;
    if (r.kind == EG_BOOSTER) {
      gives = read_bool(o, uprop_get(cls, s_n[N_BOOSTGIVES]));
      const UProp id = uprop_get(cls, s_n[N_BOOSTID]);
      if (id.ok && id.type == UW_BYTE && pg_field_in(id.offset, 1)) boosterId = *((const unsigned char*)o + id.offset);
    } else if (r.kind == EG_MONITOR) {
      canSwitch = read_bool(o, uprop_get(cls, s_n[N_CANSWITCH]));
      const UProp cp = uprop_get(cls, s_n[N_CPU]);
      const void* cpu = cp.ok && cp.type == UW_OBJECT && pg_field_in(cp.offset, 4) ? uw_ptr(o, cp.offset) : 0;
      const bool hasCpu = cpu && uobj_valid(cpu);
      int keyboards = 0;
      if (hasCpu) {
        const UProp kb = uprop_get(uobj_class(cpu), s_n[N_KEYBOARDS]);
        if (kb.ok && kb.type == UW_ARRAY && pg_field_in(kb.offset, 8)) keyboards = uw_int(cpu, kb.offset + 4);
      }
      wired = eg_monitor_wired(hasCpu, keyboards, read_bool(o, uprop_get(cls, s_n[N_HIDDEN])));
      s_unwired += !wired;
    }
    n = eg_giver_ids(r, gives, boosterId, canSwitch, wired, ids);
  }
  const int id = eg_unfound(ids, n, found);
  if (id < 0) return false;
  const UProp loc = uprop_get(cls, s_n[N_LOCATION]);
  if (!loc.ok || loc.type != UW_STRUCT || loc.elemSize < 12 || !pg_field_in(loc.offset, 12)) return false;
  float xyz[3];
  memcpy(xyz, (const char*)o + loc.offset, sizeof xyz);
  const UProp rad = uprop_get(cls, s_n[N_RADIUS]), hgt = uprop_get(cls, s_n[N_HEIGHT]);
  m->x = xyz[0];
  m->y = xyz[1];
  m->z = xyz[2];
  m->radius = 0.f;
  m->height = 0.f;
  if (rad.ok && rad.type == UW_FLOAT && pg_field_in(rad.offset, 4)) memcpy(&m->radius, (const char*)o + rad.offset, 4);
  if (hgt.ok && hgt.type == UW_FLOAT && pg_field_in(hgt.offset, 4)) memcpy(&m->height, (const char*)o + hgt.offset, 4);
  m->id = id;
  m->dlc = s_cache.dlc;
  modelbox_read(o, cls, &m->box);
  return m->x == m->x && m->y == m->y && m->z == m->z;
}

static int collect(const unsigned char found[PG_AWARDS]) {
  int n = 0;
  __try {
    for (int i = 0; i < s_cache.n; ++i)
      if (marker_of(s_cache.e[i], found, &s_markers[n])) n++;
  } __except (av_only(GetExceptionCode())) {
    s_faults++;
    s_cache.built = 0;   // rebuilt after the back-off
    s_retryAt = GetTickCount() + 1000;
    return 0;
  }
  return n;
}

static const void* player_level(const void* player) {
  __try {
    const UProp lp = uprop_get(uobj_class(player), s_n[N_LEVEL]);
    if (!lp.ok || lp.type != UW_OBJECT || !pg_field_in(lp.offset, 4)) return 0;
    const void* level = uw_ptr(player, lp.offset);
    return level && uobj_valid(level) ? level : 0;
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
}

// DukePlayer.EgoCapAwardsEnabled: *resolved 0 when the player's class has no such bool.
static int player_awards_enabled(const void* player, int* resolved) {
  *resolved = 0;
  __try {
    const UProp p = uprop_get(uobj_class(player), s_n[N_EGOENABLED]);
    if (!p.ok || p.type != UW_BOOL || !pg_field_in(p.offset, 4)) return 0;
    *resolved = 1;
    return (uw_int(player, p.offset) & (int)p.bitmask) != 0;
  } __except (av_only(GetExceptionCode())) {
    *resolved = 0;
    return 0;
  }
}

// PlayerPawn.bHasCheatedThisLevel: -1 when the player's class has no such bool or the read faults.
static int player_cheated(const void* player) {
  __try {
    const UProp p = uprop_get(uobj_class(player), s_n[N_CHEATED]);
    if (!p.ok || p.type != UW_BOOL || !pg_field_in(p.offset, 4)) return -1;
    return (uw_int(player, p.offset) & (int)p.bitmask) != 0;
  } __except (av_only(GetExceptionCode())) {
    return -1;
  }
}

int egoesp_markers(const EgMarker** out) {
  *out = s_markers;
  LARGE_INTEGER t0;
  QueryPerformanceCounter(&t0);
  int n = 0;
  const void* player = world_player();
  if (world_view().ok && player) {   // world_frame's gate: a level, a view, not paused
    if (!s_n[N_LEVEL]) resolve_names(0);
    const void* level = player_level(player);
    const unsigned serial = world_level_serial();
    // DLC mode (progress.cpp: the map's DLC03_ prefix or the DLC player): the DLC rules alone, never the base ones.
    const int dlc = progress_is_dlc() ? 1 : 0;
    if (level && (eg_cache_due(s_cache, serial, 1) || level != s_cacheLevel || dlc != s_cache.dlc) &&
        (!s_retryAt || (int)(GetTickCount() - s_retryAt) >= 0))
      build(level, serial, dlc);
    s_pruned += (unsigned)eg_cache_prune(&s_cache, slot_alive, 0);
    unsigned char found[PG_AWARDS] = {};
    for (int i = 0; i < eg_rules(s_cache.dlc).awards; ++i)
      found[i] = (unsigned char)(s_cache.dlc ? progress_dlc_award_found(i) : progress_award_found(i));
    int resolved;
    const int enabled = player_awards_enabled(player, &resolved);
    const int cheated = player_cheated(player);
    const int open = eg_awards_open_in(s_cache.dlc, resolved, enabled, cheated) ? 1 : 0;
    if (open != s_lastOpen) {   // the level's own switch (Map00: TriggerPlayer_Ego_Enable/_Disable), once per edge
      s_lastOpen = open;
      np_log("egoesp: awards %s (%s rules; EgoCapAwardsEnabled %s, cheated this level %s)%s",
             open ? "open: markers shown" : "closed: markers hidden", s_cache.dlc ? "DLC: only the cheat flag gates"
                                                                                  : "base",
             resolved ? (enabled ? "1" : "0") : "unresolved", cheated < 0 ? "unread" : (cheated ? "yes" : "no"),
             !open ? (cheated == 1 ? ": cheated this level" : ": EgoCapAwardsEnabled clear") : "");
    }
    if (!open) s_closed++;
    modelbox_frame();
    n = open && level && s_cache.built ? collect(found) : 0;
    s_frames++;
    s_shown += (unsigned)n;
    ms_window_add(&s_cost, ms_since(t0));
  }
  if (++s_calls % 600 == 0 && s_frames) {
    np_log("egoesp: frames=%u givers=%d unfound avg %.1f closed=%u pruned=%u builds=%u (last %.2f ms) faults=%u "
           "unwired monitor reads=%u cost=%.3f/%.3fms", s_frames, s_cache.n, (double)s_shown / s_frames, s_closed,
           s_pruned, s_builds, s_buildMs, s_faults, s_unwired, ms_window_avg(s_cost), s_cost.max);
    ms_window_reset(&s_cost);
    s_frames = s_shown = s_pruned = s_closed = s_unwired = 0;
  }
  return n;
}

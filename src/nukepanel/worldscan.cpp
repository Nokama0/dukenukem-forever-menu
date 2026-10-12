// The shared world scan (worldscan.h): egoesp's per-level pass, generalised to several clients and repeated every
// WS_RESCAN_MS for mid-level spawns. Read only, game thread; every engine read of a pass runs under one
// access-violation guard (world.cpp's rule), and names are logged outside it.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "worldscan.h"
#include "items.h"
#include "breakables.h"
#include "collect.h"
#include "aircraft.h"
#include "achhelp.h"
#include "world.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "progress.h"   // pg_field_in
#include "mswindow.h"
#include "log.h"

static const WsClient* const kClients[] = { &g_itemsScan, &g_breakScan, &g_collectScan, &g_aircraftScan, &g_achScan };
enum { WS_CLIENTS = (int)(sizeof kClients / sizeof kClients[0]), CLS_SLOTS = 1024 };

// Class -> rule per client, direct-mapped by class pointer and checked by class name, so a class address reused by
// another class is looked up again. Cleared on a level change and when a client's rule names grow.
struct ClsSlot { const void* cls; int clsName, rule; };
static ClsSlot s_cls[WS_CLIENTS][CLS_SLOTS];
static int s_resolved[WS_CLIENTS];
static int s_nLevel;

static WsClock s_clock;
static DWORD s_retryAt;   // after a fault: no pass before this tick
static unsigned s_loggedSerial, s_overSerial;
static int s_loggedAny;
static unsigned s_frames, s_scans, s_pruned, s_overflow, s_faults;
static double s_lastMs;
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

static void clear_all(void) {
  for (int c = 0; c < WS_CLIENTS; ++c)
    for (int r = 0; r < kClients[c]->rows; ++r) ws_cache_clear(&kClients[c]->caches[r]);
}

static const void* player_level(const void* player) {
  if (!s_nLevel) s_nLevel = uname_find(L"Level");
  __try {
    const UProp lp = uprop_get(uobj_class(player), s_nLevel);
    if (!lp.ok || lp.type != UW_OBJECT || !pg_field_in(lp.offset, 4)) return 0;
    const void* level = uw_ptr(player, lp.offset);
    return level && uobj_valid(level) ? level : 0;
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
}

static int rule_of(int c, const void* cls) {
  const int nm = uobj_name(cls);
  ClsSlot& s = s_cls[c][((uintptr_t)cls >> 4) % CLS_SLOTS];
  if (s.cls == cls && s.clsName == nm) return s.rule;
  const int rule = kClients[c]->class_rule(cls);
  s.cls = cls;
  s.clsName = nm;
  s.rule = rule;
  return rule;
}

// Every object of a client class whose Level is this level (class defaults have none, so they never count).
static bool scan_body(const void* level, const bool* want) {
  const int n = uobj_count();
  for (int i = 0; i < n; ++i) {
    const void* o = uobj_at(i);
    if (!o) continue;
    const void* cls = uobj_class(o);
    if (!cls) continue;
    int inLevel = 0;   // 0 unread, 1 this level's, -1 not
    for (int c = 0; c < WS_CLIENTS && inLevel >= 0; ++c) {
      if (!want[c]) continue;
      const int rule = rule_of(c, cls);
      if (rule < 0) continue;
      if (!inLevel) {
        const UProp lv = uprop_get(cls, s_nLevel);
        inLevel = lv.ok && lv.type == UW_OBJECT && pg_field_in(lv.offset, 4) && uw_ptr(o, lv.offset) == level ? 1 : -1;
        if (inLevel < 0) break;
      }
      int kept = rule;
      const int row = kClients[c]->row_of(o, cls, &kept);
      if (row >= 0 && row < kClients[c]->rows) ws_cache_add(&kClients[c]->caches[row], o, i, kept);
    }
  }
  return true;
}

// " items: Items and pickups 7, Ammo crates 4, Quest items 0" for each wanted client, into out.
static void row_counts(char* out, int cap, const bool* want) {
  int o = 0;
  out[0] = 0;
  for (int c = 0; c < WS_CLIENTS; ++c) {
    if (want && !want[c]) continue;
    const WsClient* k = kClients[c];
    int w = _snprintf_s(out + o, cap - o, _TRUNCATE, " %s:", k->name);
    if (w < 0) return;
    o += w;
    for (int r = 0; r < k->rows; ++r) {
      w = _snprintf_s(out + o, cap - o, _TRUNCATE, "%s %s %d%s", r ? "," : "", k->rowNames[r], k->caches[r].n,
                      k->caches[r].overflow ? " (OVERFLOW)" : "");
      if (w < 0) return;
      o += w;
    }
    if (k->note && o < cap - 1) {
      k->note(out + o, cap - o);
      o += (int)strlen(out + o);
    }
  }
}

static void scan(const void* level, unsigned serial, unsigned now) {
  bool want[WS_CLIENTS];
  if (!s_nLevel) s_nLevel = uname_find(L"Level");
  for (int c = 0; c < WS_CLIENTS; ++c) {
    want[c] = kClients[c]->wanted() != 0;
    const int got = kClients[c]->resolve();
    if (got != s_resolved[c]) {   // a rule name appeared: a class cached as "none" may now match
      memset(s_cls[c], 0, sizeof s_cls[c]);
      s_resolved[c] = got;
    }
  }
  clear_all();
  LARGE_INTEGER t0;
  QueryPerformanceCounter(&t0);
  bool ok = false;
  __try {
    ok = s_nLevel && scan_body(level, want);
  } __except (av_only(GetExceptionCode())) {
    ok = false;
  }
  s_lastMs = ms_since(t0);
  ms_window_add(&s_cost, s_lastMs);
  s_scans++;
  int over = 0;
  for (int c = 0; c < WS_CLIENTS; ++c)
    for (int r = 0; r < kClients[c]->rows; ++r) over += kClients[c]->caches[r].overflow;
  s_overflow += (unsigned)over;
  if (ok) {
    ws_scan_done(&s_clock, serial, level, now);
    s_retryAt = 0;
  } else {
    s_faults++;
    clear_all();
    s_clock.built = 0;
    s_retryAt = now + 1000;
  }
  uname_dlog_flush();   // uprop's lines, outside the guard
  // The first pass of each level, a fault and the level's first overflow are logged; the rest go in the 600-frame
  // line.
  const bool firstOver = over && s_overSerial != serial;
  if (firstOver) s_overSerial = serial;
  if (serial != s_loggedSerial || !s_loggedAny || !ok || firstOver) {
    s_loggedSerial = serial;
    s_loggedAny = 1;
    char rows[512];
    row_counts(rows, sizeof rows, want);
    np_log("worldscan: level serial %u: %d objects, %.2f ms%s%s;%s", serial, uobj_count(), s_lastMs,
           over ? ", OVERFLOW" : "", ok ? "" : ", FAULTED", rows[0] ? rows : " no row on");
  }
}

static bool slot_alive(void*, int slot, const void* obj) { return uobj_at_is(slot, obj); }

void worldscan_frame(int inLevel) {
  const unsigned serial = world_level_serial();
  const void* player = world_player();
  const void* level = player ? player_level(player) : 0;
  if (ws_level_moved(s_clock, serial, level)) {   // nothing of the old level is read again
    clear_all();
    memset(s_cls, 0, sizeof s_cls);
    s_clock.built = 0;
    s_retryAt = 0;
  }
  bool any = false;
  int periodic = 0;
  for (int c = 0; c < WS_CLIENTS; ++c) {
    if (!kClients[c]->wanted()) continue;
    any = true;
    if (!kClients[c]->oncePerLevel || (kClients[c]->again && kClients[c]->again())) periodic = 1;
  }
  const unsigned now = GetTickCount();
  if (!any && s_clock.built) {   // every row off: switched on again, a pass runs at once
    clear_all();
    s_clock.built = 0;
  }
  if (any && ws_scan_due(s_clock, serial, level, inLevel, now, periodic) &&
      (!s_retryAt || (int)(now - s_retryAt) >= 0))
    scan(level, serial, now);
  // Freed or reused slots go before anything reads them (uobj_at_is touches no object).
  if (inLevel && s_clock.built)
    for (int c = 0; c < WS_CLIENTS; ++c)
      for (int r = 0; r < kClients[c]->rows; ++r)
        s_pruned += (unsigned)ws_cache_prune(&kClients[c]->caches[r], slot_alive, 0);
  if (++s_frames % 600 != 0) return;
  if (any || s_scans) {
    char rows[512];
    row_counts(rows, sizeof rows, 0);
    np_log("worldscan: scans=%u (last %.2f ms, avg %.2f, max %.2f) pruned=%u overflow=%u faults=%u;%s", s_scans,
           s_lastMs, ms_window_avg(s_cost), s_cost.max, s_pruned, s_overflow, s_faults, rows);
  }
  ms_window_reset(&s_cost);
  s_scans = s_pruned = s_overflow = s_faults = 0;
}

bool worldscan_ready(void) { return s_clock.built && s_clock.serial == world_level_serial(); }

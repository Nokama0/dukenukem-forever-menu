// Hostile aircraft glue (aircraft.h; aircraft.md), game thread only. worldscan keeps the level's dropships, gunships
// and fighters; a frame reads each cached ship's flags, Health, Location under one guard, then boxes live hostile ones
// outside that guard via RenderActor.GetRenderBoundingBox (scall): the cylinder under-covers a 1000-unit dropship.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "aircraft.h"
#include "world.h"
#include "scall.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "progress.h"   // pg_field_in
#include "config.h"
#include "log.h"
#include "lvnames.h"

static WsCache s_row;
static const char* const kRowNames[1] = { "Aircraft" };

enum { N_LOCATION, N_RADIUS, N_HEIGHT, N_DELETEME, N_HIDDEN, N_ENEMY, N_DECOSTATE, N_HEALTH, N_HEALTHCAP, N_NODAMAGE,
       N_HEALTHPREFAB, N_VULNERABLE, N_COUNT };
// Actor, RenderActor (Engine.u), dnDecoration (dnGame.u), dnFlybyPlane (bIsVulnerable).
static const wchar_t* const kProps[N_COUNT] = { L"Location", L"CollisionRadius", L"CollisionHeight", L"bDeleteMe",
                                                L"bHidden", L"bEnemy", L"DecoState", L"Health", L"HealthCap",
                                                L"bNoDamage", L"HealthPrefab", L"bIsVulnerable" };
static int s_n[N_COUNT];
static int s_ruleName[AC_CLASS_COUNT];
static LnKeys s_lnKeys;

// At most AC_BOX_CALLS ProcessEvent box calls a frame, rotating; a ship past them keeps its last box offset.
enum { AC_BOX_CALLS = 8, AC_SEEN = 64, AC_LOGS_PER_LEVEL = 32 };

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

// --- worldscan's client ------------------------------------------------------------------------------------------
// The aim's last-resort pass needs them too, so any targeting feature keeps the scan running.
static int ac_wanted(void) { return g_cfg.espOn || g_cfg.silentOn || g_cfg.aimbotOn || g_cfg.tbotOn; }

static int ac_resolve(void) {
  for (int k = 0; k < N_COUNT; ++k)
    if (!s_n[k]) s_n[k] = uname_find(kProps[k]);
  // The ships' classes come from map packages, whose names move when the level reloads (2026-10-11): looked up again
  // each pass, absent when not found.
  const char* names[AC_CLASS_COUNT];
  for (int r = 0; r < AC_CLASS_COUNT; ++r) names[r] = AC_CLASSES[r].name;
  static_assert(AC_CLASS_COUNT <= LN_MAX, "lvnames.h keeps LN_MAX keys");
  ln_refresh(&s_lnKeys, names, AC_CLASS_COUNT, ln_find_ascii, 0);
  memcpy(s_ruleName, s_lnKeys.cur, sizeof s_ruleName);
  ln_report("aircraft", &s_lnKeys, names, AC_CLASS_COUNT);
  int got = 0;
  for (int r = 0; r < AC_CLASS_COUNT; ++r) got += s_ruleName[r] != 0;
  return got;
}

static int ac_class_rule(const void* cls) {
  int chain[32];
  int n = 0;
  for (const void* c = cls; c && n < 32; c = uw_ptr(c, US_SUPER)) chain[n++] = uobj_name(c);
  for (int i = 0; i < n; ++i)   // nearest class first, as it_rule_of_chain
    for (int r = 0; r < AC_CLASS_COUNT; ++r)
      if (chain[i] && chain[i] == s_ruleName[r]) return r;
  return -1;
}

// The wasps are matched only to be dropped here (aircraft.md section 3).
static int ac_row_of(const void*, const void*, int* rule) {
  const int kind = ac_kind_of_rule(*rule);
  return kind < 0 || kind == AC_WASP ? -1 : 0;
}

static int s_frameHostile, s_frameDamageable, s_frameDrawn;
static void ac_note(char* out, int cap) {
  _snprintf_s(out, cap, _TRUNCATE, ", hostile %d (damageable %d), drawn %d", s_frameHostile, s_frameDamageable,
              s_frameDrawn);
}

const WsClient g_aircraftScan = { "aircraft", 1, &s_row, kRowNames, ac_wanted, ac_resolve, ac_class_rule, ac_row_of,
                                  0, 0, ac_note };

// --- Per frame ---------------------------------------------------------------------------------------------------
// One cached ship as read under the guard; -1 is a field that did not resolve.
struct Raw { const void* obj; int kind, clsName, enemy, hidden, deleteMe, deco, noDamage, prefab, vuln;
             float loc[3], health, cap, radius, height; };
static Raw s_raw[WS_MAX];
// Per ship this level: the most Health seen, the last box offset and half extents, what was logged.
struct Seen { const void* obj; float maxHealth; int boxOk; float off[3], h[3]; int logged, boxLogged; };
static Seen s_seen[AC_SEEN];
static int s_seenN, s_logs;
static AcShip s_ships[AC_MAX];
static int s_nShips, s_next;
static unsigned s_serial;
static int s_refusedLogged;
// Counted per 600 frames.
static unsigned s_calls, s_frames, s_cachedSum, s_hostileSum, s_damageableSum, s_boxes, s_badBox, s_refused, s_faults;
static double s_boxMs;
static LARGE_INTEGER s_qpf;

static int rd_bool(const void* o, const void* cls, int k) {
  const UProp p = uprop_get(cls, s_n[k]);
  if (!p.ok || p.type != UW_BOOL || !pg_field_in(p.offset, 4)) return -1;
  return (uw_int(o, p.offset) & (int)p.bitmask) != 0;
}
static int rd_byte(const void* o, const void* cls, int k) {
  const UProp p = uprop_get(cls, s_n[k]);
  if (!p.ok || p.type != UW_BYTE || !pg_field_in(p.offset, 1)) return -1;
  return *((const unsigned char*)o + p.offset);
}
// A float or int property as a float; def when it does not resolve.
static float rd_num(const void* o, const void* cls, int k, float def) {
  const UProp p = uprop_get(cls, s_n[k]);
  if (!p.ok || !pg_field_in(p.offset, 4)) return def;
  if (p.type == UW_FLOAT) { float f; memcpy(&f, (const char*)o + p.offset, 4); return f; }
  if (p.type == UW_INT) return (float)uw_int(o, p.offset);
  return def;
}

static bool read_entry(const WsEntry& e, Raw* r) {
  const void* o = e.obj;
  const void* cls = uobj_class(o);
  r->obj = o;
  r->kind = ac_kind_of_rule(e.rule);
  r->clsName = uobj_name(cls);
  r->enemy = rd_bool(o, cls, N_ENEMY);
  r->hidden = rd_bool(o, cls, N_HIDDEN);
  r->deleteMe = rd_bool(o, cls, N_DELETEME);
  r->deco = rd_byte(o, cls, N_DECOSTATE);
  r->noDamage = rd_bool(o, cls, N_NODAMAGE);
  r->prefab = rd_byte(o, cls, N_HEALTHPREFAB);
  r->vuln = rd_bool(o, cls, N_VULNERABLE);
  r->health = rd_num(o, cls, N_HEALTH, 0.f);
  r->cap = rd_num(o, cls, N_HEALTHCAP, 0.f);
  r->radius = rd_num(o, cls, N_RADIUS, 0.f);
  r->height = rd_num(o, cls, N_HEIGHT, 0.f);
  const UProp loc = uprop_get(cls, s_n[N_LOCATION]);
  if (!loc.ok || loc.type != UW_STRUCT || loc.elemSize < 12 || !pg_field_in(loc.offset, 12)) return false;
  memcpy(r->loc, (const char*)o + loc.offset, 12);
  return r->loc[0] == r->loc[0] && r->loc[1] == r->loc[1] && r->loc[2] == r->loc[2];
}

// Every cached ship under one guard: the array is validated once a frame, never one guarded read per ship.
static int collect(void) {
  int n = 0;
  __try {
    for (int i = 0; i < s_row.n; ++i)
      if (read_entry(s_row.e[i], &s_raw[n])) n++;
  } __except (av_only(GetExceptionCode())) {
    s_faults++;
    return 0;
  }
  return n;
}

// RenderActor.GetRenderBoundingBox(out mins, out maxs) (native 838; spawnbox.cpp's call). 1 called.
static int render_box(void* actor, float mn[3], float mx[3]) {
  const ScallFn f = scall_find(actor, L"GetRenderBoundingBox");
  ScallParm a, b;
  if (!f.ok || !scall_parm(f, L"mins", L"StructProperty", &a) || a.size != 12 ||
      !scall_parm(f, L"maxs", L"StructProperty", &b) || b.size != 12) {
    if (!s_refusedLogged) {
      s_refusedLogged = 1;
      np_log("aircraft: RenderActor.GetRenderBoundingBox(mins, maxs) did not resolve (logged once); ships keep their "
             "collision cylinder");
    }
    return 0;
  }
  unsigned char parms[64];
  memset(parms, 0, sizeof parms);
  if (f.parmsSize > sizeof parms) return 0;
  LARGE_INTEGER t0, t1;
  QueryPerformanceCounter(&t0);
  const int called = scall_call(actor, f, parms, sizeof parms);
  QueryPerformanceCounter(&t1);
  if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
  s_boxMs += (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)s_qpf.QuadPart;
  if (!called) {
    s_refused++;
    return 0;
  }
  memcpy(mn, parms + a.offset, 12);
  memcpy(mx, parms + b.offset, 12);
  return 1;
}

static Seen* seen_of(const void* obj) {
  for (int i = 0; i < s_seenN; ++i)
    if (s_seen[i].obj == obj) return &s_seen[i];
  if (s_seenN >= AC_SEEN) return 0;
  Seen& s = s_seen[s_seenN++];
  memset(&s, 0, sizeof s);
  s.obj = obj;
  return &s;
}

static void new_level(unsigned serial) {
  s_serial = serial;
  s_seenN = 0;
  s_nShips = 0;
  s_next = 0;
  s_logs = 0;
}

void aircraft_frame(int inLevel) {
  const unsigned serial = world_level_serial();
  if (serial != s_serial) new_level(serial);
  s_nShips = 0;
  s_frameHostile = s_frameDamageable = s_frameDrawn = 0;
  if (inLevel && ac_wanted() && world_player() && worldscan_ready()) {
    const int n = collect();
    s_frames++;
    s_cachedSum += (unsigned)s_row.n;
    int calls = 0;
    if (s_next >= n) s_next = 0;
    for (int j = 0; j < n && s_nShips < AC_MAX; ++j) {
      const Raw& r = s_raw[(s_next + j) % n];
      Seen* sn = seen_of(r.obj);
      const int hostile = ac_hostile_live(r.kind, r.enemy, r.hidden, r.deleteMe, r.deco, r.health, r.noDamage, r.vuln);
      if (sn && !sn->logged && s_logs < AC_LOGS_PER_LEVEL) {   // the evidence for a ship that never shows
        sn->logged = 1;
        s_logs++;
        np_log("aircraft: %ls (%s): Health %.0f, HealthCap %.0f, DecoState %d, bEnemy %d, bHidden %d, bNoDamage %d, "
               "HealthPrefab %d, bIsVulnerable %d -> %s", uname_str(r.clsName), ac_label(r.kind), r.health, r.cap,
               r.deco, r.enemy, r.hidden, r.noDamage, r.prefab, r.vuln, hostile ? "hostile and live" : "not shown");
      }
      if (!hostile) continue;
      if (sn && r.health > sn->maxHealth) sn->maxHealth = r.health;
      AcShip& s = s_ships[s_nShips++];
      s.obj = r.obj;
      s.kind = r.kind;
      s.clsName = r.clsName;
      s.damageable = ac_damageable(r.kind, r.noDamage, r.prefab, r.vuln);
      memcpy(s.loc, r.loc, sizeof s.loc);
      s.health = r.health;
      s.cap = ac_health_cap(r.cap, sn ? sn->maxHealth : r.health);
      float mn[3], mx[3];
      if (calls < AC_BOX_CALLS && sn) {
        ++calls;
        ++s_boxes;
        const int ok = render_box((void*)r.obj, mn, mx) && ac_box_ok(mn, mx);
        if (!ok) s_badBox++;
        sn->boxOk = ok;
        for (int k = 0; ok && k < 3; ++k) {
          sn->off[k] = (mn[k] + mx[k]) * 0.5f - r.loc[k];
          sn->h[k] = (mx[k] - mn[k]) * 0.5f;
        }
        if (ok && !sn->boxLogged && s_logs < AC_LOGS_PER_LEVEL) {
          sn->boxLogged = 1;
          s_logs++;
          np_log("aircraft: %ls render box %.0f x %.0f x %.0f, centre %.0f,%.0f,%.0f off Location; cylinder r %.0f "
                 "h %.0f", uname_str(r.clsName), 2.f * sn->h[0], 2.f * sn->h[1], 2.f * sn->h[2], sn->off[0],
                 sn->off[1], sn->off[2], r.radius, r.height);
        }
      }
      s.boxOk = sn && sn->boxOk;
      for (int k = 0; k < 3; ++k) {
        s.off[k] = s.boxOk ? sn->off[k] : 0.f;
        s.c[k] = r.loc[k] + s.off[k];
      }
      if (s.boxOk) {
        memcpy(s.h, sn->h, sizeof s.h);
      } else {
        s.h[0] = s.h[1] = r.radius > 0.f ? r.radius : 64.f;
        s.h[2] = r.height > 0.f ? r.height : 64.f;
      }
      s_frameHostile++;
      s_frameDamageable += s.damageable;
    }
    s_next = n ? (s_next + calls) % n : 0;
    s_hostileSum += (unsigned)s_frameHostile;
    s_damageableSum += (unsigned)s_frameDamageable;
  }
  if (++s_calls % 600 == 0 && s_frames) {
    np_log("aircraft: frames=%u cached avg %.1f, hostile avg %.1f (damageable %.1f); box calls %u (avg %.3f ms), "
           "unusable %u, refused %u; faults %u", s_frames, (double)s_cachedSum / s_frames,
           (double)s_hostileSum / s_frames, (double)s_damageableSum / s_frames, s_boxes,
           s_boxes ? s_boxMs / s_boxes : 0.0, s_badBox, s_refused, s_faults);
    s_frames = s_cachedSum = s_hostileSum = s_damageableSum = s_boxes = s_badBox = s_refused = s_faults = 0;
    s_boxMs = 0.0;
  }
}

int aircraft_ships(const AcShip** out) {
  *out = s_ships;
  return s_nShips;
}

const AcShip* aircraft_find(const void* obj) {
  for (int i = 0; obj && i < s_nShips; ++i)
    if (s_ships[i].obj == obj) return &s_ships[i];
  return 0;
}

void aircraft_drawn(int n) { s_frameDrawn = n; }

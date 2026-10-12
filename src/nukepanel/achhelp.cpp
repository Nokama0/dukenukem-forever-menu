// Achievements tab glue (achhelp.h holds the table and the rules). worldscan's pass keeps this level's targets and
// the Counters that count them; a 1 Hz read takes Steam's state, Progress.ini's counters and each Counter's
// NumToCount; each frame's markers read the targets under one access-violation guard. Read only, game thread.
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "achhelp.h"
#include "worldscan.h"
#include "world.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "progress.h"
#include "config.h"
#include "aim.h"   // aim_damage_type: the aim targets' type rule
#include "engine.h"   // engine_readable: a damage type list's block
#include "log.h"
#include "mpgate.h"

enum { AH_ROW_T0, AH_ROW_T1, AH_ROW_SINK, AH_SCAN_ROWS };
static WsCache s_rows[AH_SCAN_ROWS];
static const char* const kRowNames[AH_SCAN_ROWS] = { "Targets", "Targets (more)", "Counters" };

enum { N_LOCATION, N_RADIUS, N_HEIGHT, N_DELETEME, N_HIDDEN, N_HEALTH, N_NODAMAGE, N_GRABBED, N_QUEST, N_TAG, N_EVENT,
       N_NUMTOCOUNT, N_CHEATED, N_PREFAB, N_VULN, N_ACCEPTED, N_IGNORED, N_COUNT };
// Actor and RenderActor (Engine.u), Biology_Generic_Poop_A and Snack_Popcorn (dnDecorations.u), Counter (Engine.u),
// PlayerPawn (the cheat flag egoesp also reads), dnDecoration.HealthPrefab and dnFlybyPlane.bIsVulnerable (aircraft's),
// dnDecoration's two damage type lists (the aim's ah_aim_type_ok).
static const wchar_t* const kProps[N_COUNT] = { L"Location", L"CollisionRadius", L"CollisionHeight", L"bDeleteMe",
                                                L"bHidden", L"Health", L"bNoDamage", L"HasBeenGrabbed",
                                                L"bQuestItemHack", L"Tag", L"Event", L"NumToCount",
                                                L"bHasCheatedThisLevel", L"HealthPrefab", L"bIsVulnerable",
                                                L"DamageTypesAcceptedOnly", L"DamageTypesIgnored" };
static int s_n[N_COUNT];
static int s_kindCls[AK_COUNT], s_kindKey[AK_COUNT];

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static int find_ascii(const char* s) {
  wchar_t w[96];
  _snwprintf_s(w, 96, _TRUNCATE, L"%hs", s);
  return uname_find(w);
}

// --- Engine reads, each under the caller's guard ---------------------------------------------------------------
static int read_name(const void* o, const void* cls, int k) {
  const UProp p = uprop_get(cls, s_n[k]);
  if (!p.ok || p.elemSize != 4 || !pg_field_in(p.offset, 4)) return 0;
  return uw_int(o, p.offset);
}
// -1 when the bool does not resolve on this class.
static int read_bool(const void* o, const void* cls, int k) {
  const UProp p = uprop_get(cls, s_n[k]);
  if (!p.ok || p.type != UW_BOOL || !pg_field_in(p.offset, 4)) return -1;
  return (uw_int(o, p.offset) & (int)p.bitmask) != 0;
}
static int read_byte(const void* o, const void* cls, int k) {
  const UProp p = uprop_get(cls, s_n[k]);
  if (!p.ok || p.type != UW_BYTE || !pg_field_in(p.offset, 1)) return -1;
  return *((const unsigned char*)o + p.offset);
}
// A ship counts for Big Guns, Big Ships (and Baron von Nukem) only as the aircraft pass would let damage land.
static bool ship_counts(const void* o, const void* cls, int k) {
  const int ak = ah_ship_kind(k);
  return ak < 0 || ac_damageable(ak, read_bool(o, cls, N_NODAMAGE), read_byte(o, cls, N_PREFAB),
                                 read_bool(o, cls, N_VULN)) != 0;
}
// RenderActor.Health is a float (ubc.py); false when it does not resolve.
static bool read_health(const void* o, const void* cls, float* out) {
  const UProp p = uprop_get(cls, s_n[N_HEALTH]);
  if (!p.ok || p.type != UW_FLOAT || !pg_field_in(p.offset, 4)) return false;
  memcpy(out, (const char*)o + p.offset, 4);
  return true;
}

// --- worldscan's client ----------------------------------------------------------------------------------------
static int s_want;
static int ah_wanted(void) { return s_want; }
static int s_dlcRows;   // the DLC table's rows at the last read (ah_total for 57..60)
static char s_scanMap[64];   // the map the pass keeps kinds for (ah_kind_here)

// Before each pass, outside the guard: a key name (a map's Tag) exists only once its map is loaded.
static int find_name(const char* s, void* ctx) {
  (void)ctx;
  return find_ascii(s);
}

// The scan's filter, once per level serial and on each change of its map or names: the map, the kinds kept here
// (ah_kind_here) and every key's index (the checkpoint bug's evidence: an index that moved under a new serial).
static unsigned s_filterSerial;
static char s_filterMap[64];
static unsigned s_filterHere;
static int s_filterKey[AK_COUNT];
static void log_filter(void) {
  const unsigned serial = world_level_serial();
  unsigned here = 0;
  for (int k = 0; k < AK_COUNT; ++k) here |= ah_kind_here(k, s_scanMap) ? 1u << k : 0u;
  const bool keysMoved = memcmp(s_filterKey, s_kindKey, sizeof s_kindKey) != 0;
  if (serial == s_filterSerial && here == s_filterHere && !keysMoved && strcmp(s_filterMap, s_scanMap) == 0) return;
  char keys[400];
  int o = 0;
  keys[0] = 0;
  for (int k = 0; k < AK_COUNT; ++k) {
    if (!AH_KINDS[k].key || !(here >> k & 1u)) continue;
    const int w = _snprintf_s(keys + o, sizeof keys - o, _TRUNCATE, " %s=%d%s", AH_KINDS[k].key, s_kindKey[k],
                              s_filterKey[k] && s_filterKey[k] != s_kindKey[k] ? " (moved)" : "");
    if (w < 0) break;
    o += w;
  }
  np_log("achhelp: scan filter, level serial %u: map '%s', kinds here 0x%x, keys%s", serial, s_scanMap, here,
         keys[0] ? keys : " none");
  s_filterSerial = serial;
  s_filterHere = here;
  strncpy_s(s_filterMap, sizeof s_filterMap, s_scanMap, _TRUNCATE);
  memcpy(s_filterKey, s_kindKey, sizeof s_kindKey);
}

static int ah_resolve(void) {
  strncpy_s(s_scanMap, sizeof s_scanMap, progress_map_now(), _TRUNCATE);
  for (int k = 0; k < N_COUNT; ++k)
    if (!s_n[k]) s_n[k] = uname_find(kProps[k]);
  const int got = ah_resolve_names(find_name, 0, s_kindCls, s_kindKey);
  log_filter();
  return got;
}

static int ah_class_rule(const void* cls) {
  int chain[32];
  int n = 0;
  for (const void* c = cls; c && n < 32; c = uw_ptr(c, US_SUPER)) chain[n++] = uobj_name(c);
  const unsigned m = ah_class_mask(chain, n, s_kindCls);
  return m ? (int)m : -1;
}

// The kind this instance is (its Tag, Event or own name picks among its class's kinds); a quest popcorn is no food.
static int ah_row_of(const void* o, const void* cls, int* rule) {
  const unsigned mask = (unsigned)*rule;
  int needTag = 0, needEvent = 0;
  for (int k = 0; k < AK_COUNT; ++k)
    if (mask >> k & 1u) {
      needTag |= AH_KINDS[k].match == AH_MATCH_TAG;
      needEvent |= AH_KINDS[k].match == AH_MATCH_EVENT;
    }
  const int tag = needTag ? read_name(o, cls, N_TAG) : 0;
  const int ev = needEvent ? read_name(o, cls, N_EVENT) : 0;
  const int k = ah_pick_kind(mask, tag, ev, uobj_name(o), s_kindKey);
  if (k < 0 || !ah_kind_here(k, s_scanMap) || !ship_counts(o, cls, k)) return -1;
  if ((AH_KINDS[k].flags & AH_K_QUEST) && read_bool(o, cls, N_QUEST) == 1) return -1;
  *rule = k;
  if (AH_KINDS[k].flags & AH_K_SINK) return AH_ROW_SINK;
  return s_rows[AH_ROW_T0].n < WS_MAX ? AH_ROW_T0 : AH_ROW_T1;   // AH_TARGETS_MAX in all
}

const WsClient g_achScan = { "achhelp", AH_SCAN_ROWS, s_rows, kRowNames, ah_wanted, ah_resolve, ah_class_rule,
                             ah_row_of, 0, 0, 0 };

// --- State -----------------------------------------------------------------------------------------------------
// Steam, per id, for the session.
static int s_answered[AR_COUNT], s_earned[AR_COUNT], s_offMin[AR_COUNT], s_attrOk[AR_COUNT];
static uint32_t s_unlock[AR_COUNT];
static char s_name[AR_COUNT][64], s_desc[AR_COUNT][160];
// This level.
static unsigned s_serial;
static int s_serialSet, s_seen[AR_COUNT], s_progRaw[AR_COUNT], s_progOk[AR_COUNT], s_live[AR_COUNT];
static int s_ticked, s_levelLogged, s_logUsed, s_capLogged, s_cheated = -1, s_comp4[3], s_comp4Ok[3];
static int s_lastDone[AR_COUNT], s_lastEarned[AR_COUNT], s_lastLive[AR_COUNT], s_lastCheat;
struct Watch { int ok, hidden, noDamage; float health; };
static Watch s_watch[AK_COUNT], s_lastWatch[AK_COUNT];
static unsigned s_lastTickMs;
static int s_wasTab;
// The frame's rows and listing.
static int s_list[AH_ROWS_MAX], s_listN, s_rowN, s_anyOn, s_anyLevelOn, s_mydigs, s_inLevel, s_mapPending;
static AhRow s_out[AH_ROWS_MAX];
static char s_title[PG_TITLE_CAP];
// Per kind: the listed row (AH_TABLE index) whose switch marks it, and its label.
static int s_kindOwner[AK_COUNT];
static char s_kindLabel[AK_COUNT][64];
// Instruments.
static unsigned s_calls, s_frames, s_shown, s_faults, s_tickFaults;

static void aim_reset(void);

static void reset_level(unsigned serial) {
  aim_reset();
  s_serial = serial;
  s_serialSet = 1;
  memset(s_seen, 0, sizeof s_seen);
  memset(s_progOk, 0, sizeof s_progOk);
  memset(s_live, 0, sizeof s_live);
  memset(s_watch, 0, sizeof s_watch);
  s_ticked = s_levelLogged = s_logUsed = s_capLogged = 0;
  s_cheated = -1;
  for (int id = 0; id < AR_COUNT; ++id) {   // a row listed later logs its first values as changes
    s_lastDone[id] = -1;
    s_lastEarned[id] = s_lastLive[id] = 0;
  }
  memset(s_lastWatch, 0, sizeof s_lastWatch);
}

const char* achhelp_title(void) { return s_title; }
bool achhelp_mydigs(void) { return s_mydigs != 0; }
bool achhelp_in_level(void) { return s_inLevel != 0; }
bool achhelp_map_pending(void) { return s_mapPending != 0; }
int achhelp_cheated(void) { return s_cheated; }
int achhelp_rows(const AhRow** out) {
  *out = s_out;
  return s_rowN;
}
// Also while the map is pending: progress's 1 Hz refresh reads the map name again (a checkpoint's MapFileName).
bool achhelp_wants_progress(void) { return s_listN > 0 || s_mapPending; }
bool achhelp_any_on(void) { return s_anyOn != 0; }

// An any-level row is listed only from a pass, so its switch alone asks for one; its Steam state is read with the rows.
static bool any_level_row(const AhAch& a) { return a.maps && strcmp(a.maps, "*") == 0 && ah_has_switch(a); }

void achhelp_set_switch(int id, int on) {
  if (id < 0 || id >= AR_COUNT) return;
  g_cfg.achEsp[id] = on ? AH_MODE_ON : AH_MODE_OFF;
  g_cfgDirty = 1;
  np_log("achhelp: switch %d %s set %s", id, ar_name(id), on ? "on" : "off");
}

static bool earned(int id) { return s_answered[id] && s_earned[id]; }

// The most targets of each id one pass found (an any-level row is listed from this).
static void update_seen(void) {
  if (!worldscan_ready()) return;
  int per[AR_COUNT] = {};
  for (int r = AH_ROW_T0; r <= AH_ROW_T1; ++r)
    for (int i = 0; i < s_rows[r].n; ++i)
      for (int t = 0; t < AH_TABLE_N; ++t)
        if (ah_has_kind(AH_TABLE[t], s_rows[r].e[i].rule)) per[AH_TABLE[t].id]++;
  for (int id = 0; id < AR_COUNT; ++id)
    if (per[id] > s_seen[id]) s_seen[id] = per[id];
}

// --- The 1 Hz read ---------------------------------------------------------------------------------------------
// Each listed id's live targets, the watched kinds' values, and the Counters' NumToCount (by sink kind).
static void level_reads_body(int numToCount[AK_COUNT], int ntcOk[AK_COUNT]) {
  for (int r = AH_ROW_T0; r <= AH_ROW_T1; ++r)
    for (int i = 0; i < s_rows[r].n; ++i) {
      const void* o = s_rows[r].e[i].obj;
      const int k = s_rows[r].e[i].rule;
      const void* cls = uobj_class(o);
      float health = 0.f;
      const bool hOk = read_health(o, cls, &health);
      const int hidden = read_bool(o, cls, N_HIDDEN) == 1;
      const int live = ah_target_live(AH_KINDS[k].flags, read_bool(o, cls, N_DELETEME) == 1, hidden, hOk, health,
                                      (AH_KINDS[k].flags & AH_K_GRABBED) ? read_bool(o, cls, N_GRABBED) == 1 : 0);
      const int counts = live && ship_counts(o, cls, k);
      for (int t = 0; t < AH_TABLE_N; ++t)
        if (ah_has_kind(AH_TABLE[t], k)) s_live[AH_TABLE[t].id] += counts;
      if ((AH_KINDS[k].flags & AH_K_WATCH) && !s_watch[k].ok) {
        s_watch[k].ok = 1;
        s_watch[k].hidden = hidden;
        s_watch[k].noDamage = read_bool(o, cls, N_NODAMAGE);
        s_watch[k].health = hOk ? health : -1.f;
      }
    }
  for (int i = 0; i < s_rows[AH_ROW_SINK].n; ++i) {
    const void* o = s_rows[AH_ROW_SINK].e[i].obj;
    const int k = s_rows[AH_ROW_SINK].e[i].rule;
    const UProp p = uprop_get(uobj_class(o), s_n[N_NUMTOCOUNT]);
    if (k < 0 || k >= AK_COUNT || ntcOk[k] || !p.ok || p.type != UW_INT || !pg_field_in(p.offset, 4)) continue;
    numToCount[k] = uw_int(o, p.offset);
    ntcOk[k] = 1;
  }
}

static bool level_reads(int numToCount[AK_COUNT], int ntcOk[AK_COUNT]) {
  memset(s_live, 0, sizeof s_live);
  memset(s_watch, 0, sizeof s_watch);
  if (!worldscan_ready()) return false;
  __try {
    level_reads_body(numToCount, ntcOk);
  } __except (av_only(GetExceptionCode())) {
    s_tickFaults++;
    memset(s_live, 0, sizeof s_live);
    memset(ntcOk, 0, sizeof(int) * AK_COUNT);
    return false;
  }
  return true;
}

static int read_cheated(const void* player) {
  __try {
    return read_bool(player, uobj_class(player), N_CHEATED);
  } __except (av_only(GetExceptionCode())) {
    return -1;
  }
}

// The [ACH_EgoAwards] profile flags achievement 29 counts (progress.md section 5); -1 when a read is not allowed now.
static int ego_ever_found(void) {
  int found = 0;
  for (int i = 0; i < PG_AWARDS; ++i) {
    wchar_t key[48];
    _snwprintf_s(key, 48, _TRUNCATE, L"EGOAWARD_%hs", PG_AWARD_NAMES[i]);
    int v = 0;
    if (progress_profile_bool(L"ACH_EgoAwards", key, &v) < 0) return -1;
    found += v != 0;
  }
  return found;
}

static void log_change(const char* fmt, ...);

static void read_tick(int steam) {
  // Steam: the listed and any-level ids' state; their display strings once.
  int ids[AH_ROWS_MAX + AH_TABLE_N], nIds = 0;
  for (int i = 0; i < s_listN; ++i) ids[nIds++] = s_list[i];
  for (int t = 0; t < AH_TABLE_N; ++t)
    if (any_level_row(AH_TABLE[t]) && !s_seen[AH_TABLE[t].id]) ids[nIds++] = AH_TABLE[t].id;
  for (int i = 0; steam && i < nIds; ++i) {
    const int id = ids[i];
    int a = 0;
    uint32_t t = 0;
    const int r = achreset_state(id, &a, &t);
    if (r < 0) break;   // refused: nothing more to ask this second
    const int seenLocked = s_answered[id] && !s_earned[id];
    if (r > 0) {
      const int mode = ah_mode_after_steam(g_cfg.achEsp[id], seenLocked, a);
      if (mode != g_cfg.achEsp[id]) {
        log_change("%d %s earned in play: switch %s -> %s, its markers stop", id, ar_name(id),
                   ah_mode_text(g_cfg.achEsp[id]), ah_mode_text(mode));
        g_cfg.achEsp[id] = mode;
        g_cfgDirty = 1;
      }
    }
    s_answered[id] = r > 0;
    if (r > 0) {
      s_earned[id] = a;
      s_unlock[id] = t;
      s_offMin[id] = t ? achreset_offset_at(t) : 0;
    }
    if (r > 0 && !s_attrOk[id] && achreset_attr(id, "name", s_name[id], sizeof s_name[id])) {
      achreset_attr(id, "desc", s_desc[id], sizeof s_desc[id]);
      s_attrOk[id] = 1;
    }
  }
  int ntc[AK_COUNT] = {}, ntcOk[AK_COUNT] = {};
  level_reads(ntc, ntcOk);
  const PgTotals tot = progress_totals();
  const PgDlcTotals dlcTot = progress_dlc_totals();
  s_dlcRows = dlcTot.chapters;
  int ego = -2;
  for (int i = 0; i < s_listN; ++i) {
    const int id = s_list[i];
    const AhAch* a = ah_find(id);
    s_progOk[id] = 0;
    if (!a) continue;
    switch (a->prog) {
      case AH_PROG_LEVEL:
        s_progOk[id] = a->sink >= 0 && ntcOk[a->sink];
        s_progRaw[id] = s_progOk[id] ? ntc[a->sink] : 0;
        break;
      case AH_PROG_PROFILE: {
        wchar_t key[48];
        _snwprintf_s(key, 48, _TRUNCATE, L"%hs", ar_name(id));
        int v = 0;
        const int r = progress_profile_int(L"Achievements", key, &v);   // a missing key is a count of 0
        s_progOk[id] = r >= 0;
        s_progRaw[id] = v;
        break;
      }
      case AH_PROG_HELMETS: s_progOk[id] = progress_flags_read(); s_progRaw[id] = tot.profile.helmets; break;
      case AH_PROG_PHONES: s_progOk[id] = progress_flags_read(); s_progRaw[id] = tot.profile.phones; break;
      case AH_PROG_EGO:
        if (ego == -2) ego = ego_ever_found();
        s_progOk[id] = ego >= 0;
        s_progRaw[id] = ego >= 0 ? ego : 0;
        break;
      // The DLC's: progress.cpp's read of [ACH_EgoAwards] DLC03EGOAWARD_* and [DLCProgress] (dlc.md sections 2, 3).
      case AH_PROG_EGO_DLC: s_progOk[id] = progress_flags_read(); s_progRaw[id] = dlcTot.ever; break;
      case AH_PROG_DLC_DONE: {
        const int d = ah_dlc_diff(id);
        s_progOk[id] = progress_flags_read() && d >= 0;
        s_progRaw[id] = d >= 0 ? dlcTot.doneAt[d] : 0;
        break;
      }
      default: break;
    }
  }
  const void* player = world_player();
  s_cheated = player ? read_cheated(player) : -1;
  // Gunslinger's open point (achievements.md, Open questions): completion index 4 per part, once per level.
  if (!s_ticked)
    for (int p = 0; p < 3; ++p) {
      wchar_t key[32];
      _snwprintf_s(key, 32, _TRUNCATE, L"GameCompletion_4_%d", p);
      s_comp4Ok[p] = progress_profile_int(L"Progress", key, &s_comp4[p]) >= 0;
    }
  s_ticked = 1;
}

// --- The instrument --------------------------------------------------------------------------------------------
// The source and its raw value; "unread" in place of the value until a read lands.
static const char* prog_source(const AhAch& a, char* buf, int cap) {
  if (a.prog != AH_PROG_NONE && !s_progOk[a.id]) {
    static const char* const kSrc[] = { "none", "Counter NumToCount", "[Achievements] counter", "helmets", "phones",
                                        "[ACH_EgoAwards]", "[ACH_EgoAwards] DLC03", "[DLCProgress]" };
    _snprintf_s(buf, (size_t)cap, _TRUNCATE, "%s unread", kSrc[a.prog]);
    return buf;
  }
  switch (a.prog) {
    case AH_PROG_LEVEL: _snprintf_s(buf, (size_t)cap, _TRUNCATE, "Counter '%s' NumToCount=%d", AH_KINDS[a.sink].key,
                                    s_progRaw[a.id]); break;
    case AH_PROG_PROFILE: _snprintf_s(buf, (size_t)cap, _TRUNCATE, "[Achievements] %s=%d", ar_name(a.id),
                                      s_progRaw[a.id]); break;
    case AH_PROG_HELMETS: _snprintf_s(buf, (size_t)cap, _TRUNCATE, "helmets %d", s_progRaw[a.id]); break;
    case AH_PROG_PHONES: _snprintf_s(buf, (size_t)cap, _TRUNCATE, "phones %d", s_progRaw[a.id]); break;
    case AH_PROG_EGO: _snprintf_s(buf, (size_t)cap, _TRUNCATE, "[ACH_EgoAwards] %d", s_progRaw[a.id]); break;
    case AH_PROG_EGO_DLC: _snprintf_s(buf, (size_t)cap, _TRUNCATE, "[ACH_EgoAwards] DLC03 %d", s_progRaw[a.id]); break;
    case AH_PROG_DLC_DONE: _snprintf_s(buf, (size_t)cap, _TRUNCATE, "[DLCProgress] difficulty %d: %d chapters",
                                       ah_dlc_diff(a.id), s_progRaw[a.id]); break;
    default: _snprintf_s(buf, (size_t)cap, _TRUNCATE, "none"); break;
  }
  return buf;
}

static const char* steam_word(int id) { return !s_answered[id] ? "unanswered" : s_earned[id] ? "earned" : "locked"; }

// What the level lists, once its first read is in: map, rows, target counts, progress and its source, Steam state.
// Long levels continue on further achhelp: lines.
static void log_level(const char* map, int chapter) {
  char line[900];
  int o = _snprintf_s(line, sizeof line, _TRUNCATE, "achhelp: level serial %u map '%s' chapter %d%s: %d row(s), "
                      "cheated %d, GameCompletion_4 %s%d/%d/%d;", s_serial, map, chapter,
                      s_mydigs ? " (Duke's digs: nothing awards)" : "", s_listN, s_cheated,
                      s_comp4Ok[0] ? "" : "(unread) ", s_comp4[0], s_comp4[1], s_comp4[2]);
  for (int i = 0; i < s_listN; ++i) {
    const AhAch* a = ah_find(s_list[i]);
    if (!a) continue;
    char src[96], piece[220], watch[96] = "";
    for (int j = 0; j < 3; ++j) {
      const int k = a->kinds[j];
      if (k < 0 || !s_watch[k].ok) continue;
      _snprintf_s(watch, sizeof watch, _TRUNCATE, ", %s Health %.1f bNoDamage %d bHidden %d", AH_KINDS[k].cls,
                  (double)s_watch[k].health, s_watch[k].noDamage, s_watch[k].hidden);
    }
    const int pc = _snprintf_s(piece, sizeof piece, _TRUNCATE, " %d %s: targets %d live %d, %s, %s, mode %s%s;", a->id,
                               a->name, s_seen[a->id], s_live[a->id], prog_source(*a, src, sizeof src),
                               steam_word(a->id), ah_mode_text(g_cfg.achEsp[a->id]), watch);
    if (pc < 0) continue;
    if (o + pc >= (int)sizeof line - 1) {
      np_log("%s", line);
      o = _snprintf_s(line, sizeof line, _TRUNCATE, "achhelp: (serial %u, continued)", s_serial);
    }
    o += _snprintf_s(line + o, sizeof line - o, _TRUNCATE, "%s", piece);
  }
  np_log("%s", line);
}

static void log_change(const char* fmt, ...) {
  if (!ah_log_take(&s_logUsed)) {
    if (!s_capLogged) {
      s_capLogged = 1;
      np_log("achhelp: %d change lines this level; the rest are not logged", AH_LOG_CAP);
    }
    return;
  }
  char b[512];
  va_list ap;
  va_start(ap, fmt);
  _vsnprintf_s(b, sizeof b, _TRUNCATE, fmt, ap);
  va_end(ap);
  np_log("achhelp: %s", b);
}

static int done_of(const AhAch& a) {
  char t[16];
  return ah_progress_text(a.prog, ah_total(a, s_dlcRows), s_progRaw[a.id], s_progOk[a.id], t, sizeof t);
}

static void remember(void) {
  for (int i = 0; i < s_listN; ++i) {
    const AhAch* a = ah_find(s_list[i]);
    if (!a) continue;
    s_lastDone[a->id] = done_of(*a);
    s_lastEarned[a->id] = earned(a->id);
    s_lastLive[a->id] = s_live[a->id];
  }
  memcpy(s_lastWatch, s_watch, sizeof s_watch);
  s_lastCheat = s_cheated;
}

// Every change of a progress value, of Steam state, of a live target count, of a watched value and of the cheat
// flag (section 4: the can count's timing, the barrel's Health, the blimp after the Cycloid, vending-machine Noms).
static void log_changes(void) {
  for (int i = 0; i < s_listN; ++i) {
    const AhAch* a = ah_find(s_list[i]);
    if (!a) continue;
    const int id = a->id;
    const int d = done_of(*a);
    if (d != s_lastDone[id]) {
      char src[96];
      char was[12] = "unread", now[12] = "unread";
      if (s_lastDone[id] >= 0) _snprintf_s(was, sizeof was, _TRUNCATE, "%d", s_lastDone[id]);
      if (d >= 0) _snprintf_s(now, sizeof now, _TRUNCATE, "%d", d);
      log_change("%d %s progress %s -> %s of %d (%s)", id, a->name, was, now, ah_total(*a, s_dlcRows),
                 prog_source(*a, src, sizeof src));
    }
    if ((earned(id) ? 1 : 0) != s_lastEarned[id])
      log_change("%d %s Steam %s -> %s", id, a->name, s_lastEarned[id] ? "earned" : "locked", steam_word(id));
    if (s_live[id] != s_lastLive[id]) log_change("%d %s live targets %d -> %d", id, a->name, s_lastLive[id], s_live[id]);
  }
  for (int k = 0; k < AK_COUNT; ++k) {
    const Watch& w = s_watch[k];
    const Watch& l = s_lastWatch[k];
    if (w.ok != l.ok || w.hidden != l.hidden || w.noDamage != l.noDamage || w.health != l.health)
      log_change("%s %s: Health %.1f bNoDamage %d bHidden %d (was %s%.1f/%d/%d)", AH_KINDS[k].cls,
                 w.ok ? "read" : "gone", (double)w.health, w.noDamage, w.hidden, l.ok ? "" : "unread ",
                 (double)l.health, l.noDamage, l.hidden);
  }
  if (s_cheated != s_lastCheat) log_change("bHasCheatedThisLevel %d -> %d", s_lastCheat, s_cheated);
  remember();
}

// --- Aim targets (achhelp.h): the live AIM kinds of the rows that are on, for aim.cpp's last-resort pass ---------
static AhAimTarget s_aim[AH_AIM_MAX];
static int s_aimN;
static unsigned s_aimKinds, s_aimLastKinds;
// Per 600 built frames: targets offered, of them refused by the gun's damage type; faults.
static unsigned s_aimFrames, s_aimOffered, s_aimTypeOut, s_aimFaults;
static bool read_place(const void* o, const void* cls, AhMarker* m);   // the markers' read (below)

static void aim_reset(void) {
  s_aimN = 0;
  s_aimKinds = s_aimLastKinds = 0;
}

// A class list of dnDecoration (a TArray of class pointers, count at +4): its count, 0 when the class declares none,
// -1 unread. *match: an entry equals wtype (exact) or is a parent of it (ClassIsChildOf). The block is validated once.
static int read_type_list(const void* o, const void* cls, int k, const void* wtype, int exact, int* match) {
  *match = 0;
  const UProp p = uprop_get(cls, s_n[k]);
  if (!p.ok) return 0;
  if (p.type != UW_ARRAY || !pg_field_in(p.offset, 8)) return -1;
  const int n = uw_int(o, p.offset + 4);
  if (n < 0 || n > 64) return -1;
  if (!n) return 0;
  const void* const* data = (const void* const*)uw_ptr(o, p.offset);
  if (!data || !engine_readable(data, (size_t)n * 4)) return -1;
  for (int i = 0; i < n && wtype; ++i) {
    const void* e = data[i];
    if (e && uobj_valid(e) && (exact ? e == wtype : uobj_class_isa(wtype, e))) *match = 1;
  }
  return n;
}

// The model box's centre and half extents from the marker's corners (ac_box_ok), else the collision cylinder's.
static void aim_box(const AhMarker& m, AhAimTarget* t) {
  float mn[3] = { m.x, m.y, m.z }, mx[3] = { m.x, m.y, m.z };
  if (m.box.ok) {
    for (int k = 0; k < 3; ++k) mn[k] = mx[k] = m.box.p[0][k];
    for (int c = 1; c < 8; ++c)
      for (int k = 0; k < 3; ++k) {
        if (m.box.p[c][k] < mn[k]) mn[k] = m.box.p[c][k];
        if (m.box.p[c][k] > mx[k]) mx[k] = m.box.p[c][k];
      }
  }
  t->boxOk = m.box.ok && ac_box_ok(mn, mx);
  const float cyl[3] = { m.radius, m.radius, m.height };
  for (int k = 0; k < 3; ++k) {
    t->c[k] = t->boxOk ? 0.5f * (mn[k] + mx[k]) : t->loc[k];
    t->h[k] = t->boxOk ? 0.5f * (mx[k] - mn[k]) : cyl[k];
    t->off[k] = t->c[k] - t->loc[k];
  }
}

static int aim_body(unsigned kinds, const void* wtype) {
  if (wtype && !uobj_valid(wtype)) wtype = 0;   // last frame's read: unknown rather than stale
  int n = 0;
  for (int r = AH_ROW_T0; r <= AH_ROW_T1; ++r)
    for (int i = 0; i < s_rows[r].n && n < AH_AIM_MAX; ++i) {
      const int k = s_rows[r].e[i].rule;
      if (k < 0 || k >= AK_COUNT || !(kinds >> k & 1u)) continue;
      const void* o = s_rows[r].e[i].obj;
      const void* cls = uobj_class(o);
      float health = 0.f;
      const bool hOk = read_health(o, cls, &health);
      if (!ah_aim_target_ok(AH_KINDS[k].flags, read_bool(o, cls, N_DELETEME) == 1, read_bool(o, cls, N_HIDDEN) == 1,
                            hOk, health, read_bool(o, cls, N_NODAMAGE)))
        continue;
      AhMarker m;
      if (!read_place(o, cls, &m)) continue;
      int am = 0, im = 0;
      const int an = read_type_list(o, cls, N_ACCEPTED, wtype, 0, &am);
      const int in = read_type_list(o, cls, N_IGNORED, wtype, 1, &im);
      AhAimTarget& t = s_aim[n++];
      memset(&t, 0, sizeof t);
      t.obj = o;
      t.kind = k;
      t.id = s_kindOwner[k] >= 0 ? AH_TABLE[s_kindOwner[k]].id : -1;
      t.clsName = uobj_name(cls);
      t.typeOk = ah_aim_type_ok(an, in, am, im, wtype != 0);
      t.loc[0] = m.x;
      t.loc[1] = m.y;
      t.loc[2] = m.z;
      t.radius = m.radius;
      t.health = health;
      aim_box(m, &t);
    }
  return n;
}

// After build_rows (the kinds' owners): only while a targeting feature is on and a listed row's switch says so.
static void build_aim(int inLevel) {
  s_aimN = 0;
  const bool want = inLevel && (g_cfg.silentOn || g_cfg.aimbotOn || g_cfg.tbotOn) && world_player() && !s_mapPending &&
                    !s_mydigs;
  int earnedNow[AR_COUNT];
  for (int id = 0; id < AR_COUNT; ++id) earnedNow[id] = earned(id);
  s_aimKinds = want ? ah_aim_kinds(progress_map_now(), g_cfg.achEsp, earnedNow) : 0;
  if (s_aimKinds != s_aimLastKinds) {
    log_change("aim targets %s (kinds 0x%x)", s_aimKinds ? "on: the aim takes them when no pawn or ship is picked" :
               "off", s_aimKinds);
    s_aimLastKinds = s_aimKinds;
  }
  if (!s_aimKinds || !worldscan_ready()) return;
  modelbox_frame();
  const void* wtype = aim_damage_type();
  __try {
    s_aimN = aim_body(s_aimKinds, wtype);
  } __except (av_only(GetExceptionCode())) {
    s_aimFaults++;
    s_aimN = 0;
  }
  uname_dlog_flush();
  s_aimOffered += (unsigned)s_aimN;
  for (int i = 0; i < s_aimN; ++i) s_aimTypeOut += (unsigned)!s_aim[i].typeOk;
  if (++s_aimFrames < 600) return;
  np_log("achhelp: aim targets, 600 frames: offered avg %.2f, refused by the gun's damage type %u, faults %u",
         (double)s_aimOffered / s_aimFrames, s_aimTypeOut, s_aimFaults);
  s_aimFrames = s_aimOffered = s_aimTypeOut = s_aimFaults = 0;
}

int achhelp_aim_targets(const AhAimTarget** out) {
  *out = s_aim;
  return s_aimN;
}

const AhAimTarget* achhelp_aim_find(const void* obj) {
  for (int i = 0; obj && i < s_aimN; ++i)
    if (s_aim[i].obj == obj) return &s_aim[i];
  return 0;
}

// --- The frame -------------------------------------------------------------------------------------------------
static void build_rows(void) {
  AhSortKey keys[AH_ROWS_MAX];
  AhRow rows[AH_ROWS_MAX];
  s_anyOn = 0;
  for (int k = 0; k < AK_COUNT; ++k) s_kindOwner[k] = -1;
  for (int i = 0; i < s_listN; ++i) {
    const int id = s_list[i];
    const AhAch* a = ah_find(id);
    AhRow& r = rows[i];
    memset(&r, 0, sizeof r);
    r.id = id;
    r.group = a->scope;
    r.hasSwitch = ah_has_switch(*a);
    r.mode = g_cfg.achEsp[id];
    r.answered = s_answered[id];
    r.earned = earned(id);
    r.unlock = s_unlock[id];
    r.offMin = s_offMin[id];
    r.espOn = r.hasSwitch && ah_esp_on(r.mode, r.earned);
    r.missable = a->missable;
    strncpy_s(r.name, sizeof r.name, s_attrOk[id] && s_name[id][0] ? s_name[id] : a->name, _TRUNCATE);
    strncpy_s(r.desc, sizeof r.desc, s_attrOk[id] && s_desc[id][0] ? s_desc[id] : a->note, _TRUNCATE);
    ah_progress_text(a->prog, ah_total(*a, s_dlcRows), s_progRaw[id], s_progOk[id], r.progress, sizeof r.progress);
    r.note = a->note;
    keys[i].group = r.group;
    keys[i].earned = r.earned;
    keys[i].order = i;
    s_anyOn |= r.espOn;
    // The first listed row with its switch on owns a kind's markers (beer serves two achievements).
    for (int j = 0; r.espOn && j < 3; ++j) {
      const int k = a->kinds[j];
      if (k < 0 || s_kindOwner[k] >= 0) continue;
      s_kindOwner[k] = ah_table_index(id);
      ah_marker_label(r.name, AH_KINDS[k].noun, s_progOk[id] ? r.progress : "", s_kindLabel[k],
                      sizeof s_kindLabel[k]);
    }
  }
  int idx[AH_ROWS_MAX];
  ah_sort(keys, s_listN, idx);
  for (int i = 0; i < s_listN; ++i) s_out[i] = rows[idx[i]];
  s_rowN = s_listN;
}

void achhelp_frame(int inLevel, int tabOpen, unsigned nowMs) {
  const unsigned serial = world_level_serial();
  if (!s_serialSet || serial != s_serial) reset_level(serial);
  const void* player = world_player();
  const bool tabEdge = tabOpen && !s_wasTab;
  s_wasTab = tabOpen;
  if (!player) {
    s_listN = s_rowN = s_anyOn = s_anyLevelOn = s_mydigs = s_inLevel = s_mapPending = 0;
    s_title[0] = 0;
    s_want = 0;
    s_aimN = 0;
    return;
  }
  const char* map = progress_map_now();
  // A DLC level takes its chapter, title and ego total from the DLC table (progress.cpp's DLC mode).
  const int dlc = progress_is_dlc();
  const int chapter = dlc ? progress_dlc_chapter_now() : progress_chapter_now();
  int nch = 0;
  const PgChapter* ch = dlc ? progress_dlc_chapters(&nch) : progress_chapters(&nch);
  const bool inChapter = chapter >= 0 && chapter < nch;
  const AhLevel lv = { map, inChapter ? ch[chapter].helmTotal : 0, inChapter ? ch[chapter].phoneTotal : 0,
                       inChapter ? ch[chapter].egoTotal : 0, dlc };
  strncpy_s(s_title, sizeof s_title, inChapter ? ch[chapter].title : "", _TRUNCATE);
  s_mydigs = ah_is_mydigs(map);
  s_inLevel = map[0] != 0;
  s_mapPending = ah_map_pending(map);
  update_seen();
  s_listN = s_mapPending ? 0 : ah_list(lv, s_seen, s_list, AH_ROWS_MAX);
  s_anyLevelOn = 0;
  for (int t = 0; t < AH_TABLE_N && !s_mydigs && !s_mapPending; ++t)
    if (any_level_row(AH_TABLE[t])) s_anyLevelOn |= ah_esp_on(g_cfg.achEsp[AH_TABLE[t].id], earned(AH_TABLE[t].id)) ? 1 : 0;
  s_want = ah_scan_wanted(tabOpen, inLevel, s_anyOn, s_anyLevelOn);
  const bool due = tabEdge || !s_ticked || nowMs - s_lastTickMs >= 1000u;
  if (inLevel && (s_listN || s_anyLevelOn) && due && (worldscan_ready() || !s_want)) {
    s_lastTickMs = nowMs;
    // Steam state while the tab shows or a switch is on; a switch reads on until Steam says earned.
    read_tick(tabOpen || s_anyOn || s_anyLevelOn || !s_levelLogged);
    if (!s_levelLogged) {
      s_levelLogged = 1;
      build_rows();
      log_level(map, chapter);
      remember();
    } else {
      log_changes();
    }
  } else if (inLevel && !s_listN && s_mydigs && !s_levelLogged && map[0]) {
    s_levelLogged = 1;
    log_level(map, chapter);
  }
  build_rows();
  build_aim(inLevel);
}

// --- Markers ---------------------------------------------------------------------------------------------------
static AhMarker s_markers[AH_TARGETS_MAX];

static bool read_place(const void* o, const void* cls, AhMarker* m) {
  const UProp loc = uprop_get(cls, s_n[N_LOCATION]);
  if (!loc.ok || loc.type != UW_STRUCT || loc.elemSize < 12 || !pg_field_in(loc.offset, 12)) return false;
  float xyz[3];
  memcpy(xyz, (const char*)o + loc.offset, sizeof xyz);
  m->x = xyz[0];
  m->y = xyz[1];
  m->z = xyz[2];
  m->radius = m->height = 0.f;
  const UProp rad = uprop_get(cls, s_n[N_RADIUS]), hgt = uprop_get(cls, s_n[N_HEIGHT]);
  if (rad.ok && rad.type == UW_FLOAT && pg_field_in(rad.offset, 4)) memcpy(&m->radius, (const char*)o + rad.offset, 4);
  if (hgt.ok && hgt.type == UW_FLOAT && pg_field_in(hgt.offset, 4)) memcpy(&m->height, (const char*)o + hgt.offset, 4);
  modelbox_read(o, cls, &m->box);
  return m->x == m->x && m->y == m->y && m->z == m->z;
}

static int markers_body(void) {
  int n = 0;
  for (int r = AH_ROW_T0; r <= AH_ROW_T1; ++r)
    for (int i = 0; i < s_rows[r].n && n < AH_TARGETS_MAX; ++i) {
      const int k = s_rows[r].e[i].rule;
      if (k < 0 || k >= AK_COUNT || s_kindOwner[k] < 0) continue;
      const void* o = s_rows[r].e[i].obj;
      const void* cls = uobj_class(o);
      const int flags = AH_KINDS[k].flags;
      float health = 0.f;
      const bool hOk = (flags & AH_K_ALIVE) && read_health(o, cls, &health);
      if (!ah_target_live(flags, read_bool(o, cls, N_DELETEME) == 1, read_bool(o, cls, N_HIDDEN) == 1, hOk, health,
                          (flags & AH_K_GRABBED) ? read_bool(o, cls, N_GRABBED) == 1 : 0) ||
          !ship_counts(o, cls, k))
        continue;
      AhMarker& m = s_markers[n];
      if (!read_place(o, cls, &m)) continue;
      strcpy_s(m.label, sizeof m.label, s_kindLabel[k]);
      n++;
    }
  return n;
}

int achhelp_markers(const AhMarker** out) {
  *out = s_markers;
  int n = 0;
  if (s_anyOn && !mp_refused() && world_view().ok && world_player() && worldscan_ready()) {
    modelbox_frame();
    __try {
      n = markers_body();
    } __except (av_only(GetExceptionCode())) {
      s_faults++;
      n = 0;
    }
    uname_dlog_flush();
    s_frames++;
    s_shown += (unsigned)n;
  }
  if (++s_calls % 600 == 0 && s_frames) {
    np_log("achhelp: frames=%u targets=%d+%d counters=%d rows=%d shown avg %.2f faults=%u read faults=%u", s_frames,
           s_rows[AH_ROW_T0].n, s_rows[AH_ROW_T1].n, s_rows[AH_ROW_SINK].n, s_rowN, (double)s_shown / s_frames,
           s_faults, s_tickFaults);
    s_frames = s_shown = s_faults = s_tickFaults = 0;
  }
  return n;
}

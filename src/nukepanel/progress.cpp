// Progress glue (progress.h holds the rules): Progress.ini through the game's config cache (engine.h's Get reads), the
// chapter table and titles from UDukeSceneChapterSelect's class default, ego-cap numbers from DukePlayer's, difficulty
// names from LCDHelper's. Read only, game thread, at most once a second, once a level until Progress.ini answers a Get.
#include <windows.h>
#include <string.h>
#include <wchar.h>
#include "progress.h"
#include "engine.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "world.h"
#include "log.h"

static const wchar_t kFile[] = L"Progress.ini";
// UArrayProperty::Inner and UStructProperty::Struct, both +0x78 (accuracy.cpp's citations); a dnString is
// {data, count with the terminator, max}.
static const int UAP_INNER = 0x78, USP_STRUCT = 0x78, STR_SIZE = 12;
// A class default that does not read after this many tries keeps the built-in copy (logged once).
static const int READ_TRIES = 10;

enum { N_CHAPTERS, N_URL, N_AWARDS, N_CHAPTERNAMES, N_EGOCAP, N_MAXEGOCAP, N_BONUSEGO, N_CAPAWARDS, N_DIFF0,
       N_COUNT = N_DIFF0 + PG_DIFFS };
static const wchar_t* const kNames[N_COUNT] = { L"Chapters", L"URL", L"Awards", L"ChapterNames", L"EgoCap",
                                                L"MaxEgoCap", L"PreorderBonusEgo", L"EgoCapAwards",
                                                L"PieceOfCakeDifficultyString", L"LetsRockDifficultyString",
                                                L"ComeGetSomeDifficultyString", L"DamnImGoodDifficultyString" };
enum { CLS_SELECT, CLS_DUKE, CLS_LCD, CLS_COUNT };
static const wchar_t* const kClassNames[CLS_COUNT] = { L"UDukeSceneChapterSelect", L"DukePlayer", L"LCDHelper" };
static int s_n[N_COUNT];

static PgTable s_table;
static bool s_init;
static PgFlags s_flags;
static PgChapter s_ch[PG_CHAPTERS_MAX];
static int s_chN;
static PgTotals s_tot;
static float s_caps[PG_AWARDS], s_base = PG_CAP_BASE, s_max = PG_CAP_MAX, s_bonus = PG_PREORDER_EGO;
static bool s_capsFromGame;
static int s_chapterNow = -1;
static const void* s_chapterPlayer;   // the player and level serial s_chapterNow was taken for
static unsigned s_chapterSerial;
static char s_mapNow[64];             // the map name that chapter was taken from
static int s_flagsRead;               // s_flags came from a read of Progress.ini, not the zeroed start
static const char* s_note;
static char s_diffNames[PG_DIFFS][PG_TITLE_CAP];
static const char* s_diffPtrs[PG_DIFFS];
static bool s_diffFromGame;
static PgGate s_gate;

// The DLC (docs/re/dlc.md): its class is loaded only once the DLC is entered and MapInfo.CampaignList is empty until
// the DLC is mounted, so both are looked for apart from the base classes and the built-in table serves meanwhile.
enum { ND_CAMPAIGN, ND_CAPS, ND_MAPAWARDS, ND_MAPNAME, ND_AUTO, ND_PLAYERCLS, ND_COUNT };
static const wchar_t* const kDlcNames[ND_COUNT] = { L"CampaignList", L"DLC03EgoCapAwards", L"DLC03MapEgoCapAwards",
                                                    L"MapName", L"AutoEgoCapAward", L"DukePlayerDLC03" };
static int s_dn[ND_COUNT];
enum { DCLS_PLAYER, DCLS_MAPINFO, DCLS_COUNT };
static const wchar_t* const kDlcClassNames[DCLS_COUNT] = { L"DukePlayerDLC03", L"MapInfo" };
static const void* s_dcls[DCLS_COUNT];
static unsigned s_dlcNextScanMs, s_dlcScanMs, s_dlcOrderNextMs;
static bool s_dlcScanLogged, s_dlcEmptyLogged, s_dlcOrderLogged, s_dlcCapsLogged;
static PgTable s_dlcTable;
static char s_dlcUrls[PG_CHAPTERS_MAX][PG_URL_CAP];
static int s_dlcUrlN, s_dlcOrderRead, s_dlcOrderTries, s_dlcCapTries;
static PgMapAwards s_dlcAw;
static float s_dlcCaps[PG_DLC_AWARDS], s_dlcAuto = PG_DLC_AUTO_EGO;
static bool s_dlcCapsFromGame;
static int s_dlcNow, s_dlcChapterNow = -1, s_playerDlc = -1;
static PgDlcFlags s_dlcFlags;
static PgChapter s_dlcCh[PG_CHAPTERS_MAX];
static int s_dlcChN;
static PgDlcTotals s_dlcTot;

static int s_haveRead, s_force, s_wasTab;
static unsigned s_lastMs, s_nextClassMs, s_scanMs;
static const void* s_cls[CLS_COUNT];
static int s_tableTries, s_capTries, s_diffTries;
static bool s_scanStreakLogged;

// Instruments: reads, their cost, and what changed.
static unsigned s_reads, s_slowSkips, s_faults, s_loadSkips;
static double s_msMax, s_probeMax;
static int s_loggedFound = -1, s_loggedDone = -1, s_loggedChapter = -2;
static bool s_cfgLogged, s_loadLogged, s_slowLogged, s_tableLogged, s_capsLogged, s_diffLogged, s_missLogged;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static double ms_since(const LARGE_INTEGER& t0) {
  LARGE_INTEGER t1, f;
  QueryPerformanceCounter(&t1);
  QueryPerformanceFrequency(&f);
  return (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
}

static void init_once(void) {
  if (s_init) return;
  s_init = true;
  pg_table_builtin(&s_table);
  pg_dlc_table_builtin(&s_dlcTable);
  memcpy(s_caps, PG_CAP_AWARDS, sizeof s_caps);
  memcpy(s_dlcCaps, PG_DLC_CAP_AWARDS, sizeof s_dlcCaps);
  for (int d = 0; d < PG_DIFFS; ++d) {
    strcpy_s(s_diffNames[d], PG_TITLE_CAP, PG_DIFF_NAMES[d]);
    s_diffPtrs[d] = s_diffNames[d];
  }
  s_chN = pg_build_chapters(s_table, s_flags, s_ch);
  s_tot = pg_totals(s_flags, s_caps, s_base, s_max, s_bonus, 0, 0.f);
  s_dlcChN = pg_build_dlc_chapters(s_dlcTable, s_dlcFlags, s_dlcCh);
  s_dlcTot = pg_dlc_totals(s_dlcFlags, s_dlcChN, 0, s_dlcCaps, s_base, s_dlcAuto, s_max, s_bonus, 0, 0.f);
}

// --- Progress.ini through the cache -------------------------------------------------------------------
// Keys the cache answered this read (pg_gate_note).
static int s_foundKeys;
static int get_bool(const wchar_t* sec, const wchar_t* key) {
  int v = 0;
  const int found = engine_config_get_bool(sec, key, &v, kFile);
  s_foundKeys += found != 0;
  return pg_bool_from_get(found, v);
}
static unsigned get_int(const wchar_t* sec, const wchar_t* key) {
  int v = 0;
  const int found = engine_config_get_int(sec, key, &v, kFile);
  s_foundKeys += found != 0;
  return pg_int_from_get(found, v);
}

// 1 when read; 0 when the first Get was slow (the file is not cached: the rest would each touch the disk).
static int read_flags_body(PgFlags* f, double* probeMs) {
  memset(f, 0, sizeof *f);
  s_foundKeys = 0;
  wchar_t key[48];
  for (int i = 0; i < PG_AWARDS; ++i) {
    _snwprintf_s(key, 48, _TRUNCATE, L"EGOAWARD_%hs", PG_AWARD_NAMES[i]);
    LARGE_INTEGER t0;
    if (!i) QueryPerformanceCounter(&t0);
    f->award[i] = (unsigned char)get_bool(L"EgoAwards", key);
    if (!i) {
      *probeMs = ms_since(t0);
      if (!pg_probe_fast(*probeMs)) return 0;
    }
  }
  for (int d = 0; d < PG_DIFFS; ++d)
    for (int p = 0; p < PG_PARTS; ++p) {
      _snwprintf_s(key, 48, _TRUNCATE, L"GameCompletion_%d_%d", d, p);
      f->completion[d][p] = get_int(L"Progress", key);
    }
  static const wchar_t* const kHelmet[PG_HELMETS] = { L"HelmetA", L"HelmetB", L"HelmetC" };
  for (int i = 0; i < PG_HELMETS; ++i) f->helmet[i] = (unsigned char)get_bool(L"Achievements", kHelmet[i]);
  for (int i = 0; i < PG_PHONES; ++i) {
    _snwprintf_s(key, 48, _TRUNCATE, L"PhoneMessage%d", i + 1);   // UpdatePhoneMessagesAchievement's int-to-string
    f->phone[i] = (unsigned char)get_bool(L"Achievements", key);
  }
  f->bonusEgo = get_int(L"PreorderBonus", L"BonusEgo") != 0;   // HasEnteredPreorderBonusEgoCode: value != 0
  return 1;
}

// -1 on a fault inside the game's cache.
static int read_flags(PgFlags* f, double* probeMs) {
  __try {
    return read_flags_body(f, probeMs);
  } __except (av_only(GetExceptionCode())) {
    return -1;
  }
}

// The DLC's keys (dlc.md sections 2, 3): its 19 awards this playthrough and ever, and each table row's completion per
// difficulty by URL, as DukePlayerDLC03.HasDLCMapCompletion builds the key. 1 read, -1 on a fault.
static int read_dlc_flags_body(const PgTable& t, PgDlcFlags* f) {
  memset(f, 0, sizeof *f);
  wchar_t key[64];
  for (int i = 0; i < PG_DLC_AWARDS; ++i) {
    _snwprintf_s(key, 64, _TRUNCATE, L"DLC03EGOAWARD_%hs", PG_DLC_AWARD_NAMES[i]);
    f->award[i] = (unsigned char)get_bool(L"EgoAwards", key);
    f->ever[i] = (unsigned char)get_bool(L"ACH_EgoAwards", key);
  }
  for (int c = 0; c < t.n && c < PG_CHAPTERS_MAX; ++c)
    for (int d = 0; d < PG_DIFFS; ++d) {
      _snwprintf_s(key, 64, _TRUNCATE, L"GameCompletion_%hs_%d", t.url[c], d);
      f->done[c][d] = (unsigned char)get_bool(L"DLCProgress", key);
    }
  return 1;
}

static int read_dlc_flags(const PgTable& t, PgDlcFlags* f) {
  __try {
    return read_dlc_flags_body(t, f);
  } __except (av_only(GetExceptionCode())) {
    return -1;
  }
}

// --- Class defaults -------------------------------------------------------------------------------------
static void read_str(const char* s, char* out, int cap) {
  out[0] = 0;
  const wchar_t* data = (const wchar_t*)uw_ptr(s, 0);
  int count = uw_int(s, 4);
  if (!data || count <= 0) return;
  if (count > cap) count = cap;
  if (engine_readable(data, (size_t)count * sizeof(wchar_t))) pg_ascii(data, count, out, cap);
}

static const void* array_inner(const UProp& p) {
  if (!p.ok || p.type != UW_ARRAY || !p.field) return 0;
  const void* inner = uw_ptr(p.field, UAP_INNER);
  return inner && uobj_valid(inner) ? inner : 0;
}
static int elem_size(const void* prop) { return *(const unsigned short*)((const char*)prop + UP_ELEMSIZE); }

// UDukeSceneChapterSelect.Chapters (SChapterEntry {URL, ..., Awards, ...}) and ChapterNames on the class default.
static const char* read_table_body(PgTable* t) {
  const void* cls = s_cls[CLS_SELECT];
  const void* cdo = engine_get_default_object((void*)cls);
  if (!cdo) return "no class default";
  const UProp ap = uprop_get(cls, s_n[N_CHAPTERS]);
  const void* inner = array_inner(ap);
  const void* st = inner ? uw_ptr(inner, USP_STRUCT) : 0;
  if (!st || !uobj_valid(st)) return "Chapters is not an array of structs";
  const int es = elem_size(inner);
  const UProp up = uprop_get(st, s_n[N_URL]);
  const UProp aw = uprop_get(st, s_n[N_AWARDS]);
  const void* awInner = array_inner(aw);
  if (!up.ok || up.type != UW_STR || !awInner || elem_size(awInner) != 1 || es <= 0 || es > 0x400 ||
      up.offset + STR_SIZE > es || aw.offset + STR_SIZE > es)
    return "SChapterEntry.URL or Awards did not resolve as a string and a byte array";
  const char* data = (const char*)uw_ptr(cdo, ap.offset);
  const int count = uw_int(cdo, ap.offset + 4);
  if (!data || count < 1 || count > PG_CHAPTERS_MAX) return "Chapters count out of range";
  if (!engine_readable(data, (size_t)count * es)) return "Chapters data unreadable";
  memset(t, 0, sizeof *t);
  t->n = count;
  t->fromGame = 1;
  for (int i = 0; i < count; ++i) {
    const char* e = data + (size_t)i * es;
    read_str(e + up.offset, t->url[i], PG_URL_CAP);
    const unsigned char* ad = (const unsigned char*)uw_ptr(e, aw.offset);
    const int an = uw_int(e, aw.offset + 4);
    if (an < 0 || an > 255) return "an entry's Awards count is out of range";
    t->awardN[i] = an;
    const int k = an < PG_CH_AWARDS_MAX ? an : PG_CH_AWARDS_MAX;
    if (k && (!ad || !engine_readable(ad, (size_t)k))) return "an entry's Awards data is unreadable";
    if (k) memcpy(t->awards[i], ad, (size_t)k);
  }
  // Titles: the localised ChapterNames by index; a missing one falls back to the built-in English, then the URL.
  const UProp np = uprop_get(cls, s_n[N_CHAPTERNAMES]);
  const void* nInner = array_inner(np);
  int titles = 0;
  if (nInner && elem_size(nInner) == STR_SIZE) {
    const char* nd = (const char*)uw_ptr(cdo, np.offset);
    const int nc = uw_int(cdo, np.offset + 4);
    if (nd && nc > 0 && nc <= 64 && engine_readable(nd, (size_t)nc * STR_SIZE))
      for (int i = 0; i < count && i < nc; ++i) {
        read_str(nd + (size_t)i * STR_SIZE, t->title[i], PG_TITLE_CAP);
        titles += t->title[i][0] != 0;
      }
  }
  t->titlesFromGame = titles == count;
  for (int i = 0; i < count; ++i)
    if (!t->title[i][0]) strcpy_s(t->title[i], PG_TITLE_CAP, i < PG_CHAPTERS ? PG_BUILTIN[i].title : t->url[i]);
  return 0;
}

static const char* read_table(PgTable* t) {
  __try {
    return read_table_body(t);
  } __except (av_only(GetExceptionCode())) {
    return "access violation";
  }
}

// The offset is bounded (pg_field_in) and the four bytes checked readable: once per default, once a second on the
// player.
static bool read_float(const void* cls, const void* obj, int name, float* out) {
  const UProp p = uprop_get(cls, name);
  if (!p.ok || p.type != UW_FLOAT || p.elemSize != 4 || !pg_field_in(p.offset, 4)) return false;
  if (!engine_readable((const char*)obj + p.offset, 4)) return false;
  memcpy(out, (const char*)obj + p.offset, 4);
  return *out == *out;
}

// DukePlayer's default EgoCap (Pawn's), MaxEgoCap, PreorderBonusEgo and the 35 EgoCapAwards.
static const char* read_caps_body(float* caps, float* base, float* max, float* bonus) {
  const void* cls = s_cls[CLS_DUKE];
  const void* cdo = engine_get_default_object((void*)cls);
  if (!cdo) return "no class default";
  if (!read_float(cls, cdo, s_n[N_EGOCAP], base) || !read_float(cls, cdo, s_n[N_MAXEGOCAP], max) ||
      !read_float(cls, cdo, s_n[N_BONUSEGO], bonus))
    return "EgoCap, MaxEgoCap or PreorderBonusEgo did not resolve as a float";
  const UProp ap = uprop_get(cls, s_n[N_CAPAWARDS]);
  const void* inner = array_inner(ap);
  if (!inner || elem_size(inner) != 4) return "EgoCapAwards is not a float array";
  const void* data = uw_ptr(cdo, ap.offset);
  const int count = uw_int(cdo, ap.offset + 4);
  if (!data || count != PG_AWARDS || !engine_readable(data, PG_AWARDS * 4)) return "EgoCapAwards count is not 35";
  memcpy(caps, data, PG_AWARDS * 4);
  for (int i = 0; i < PG_AWARDS; ++i)
    if (!(caps[i] >= 0.f && caps[i] < 100.f)) return "an EgoCapAwards value is out of range";
  return 0;
}

static const char* read_caps(float* caps, float* base, float* max, float* bonus) {
  __try {
    return read_caps_body(caps, base, max, bonus);
  } __except (av_only(GetExceptionCode())) {
    return "access violation";
  }
}

// LCDHelper's four localised "Difficulty: <name>" strings on its class default, the prefix stripped.
static const char* read_diffs_body(char names[PG_DIFFS][PG_TITLE_CAP]) {
  const void* cls = s_cls[CLS_LCD];
  const void* cdo = engine_get_default_object((void*)cls);
  if (!cdo) return "no class default";
  for (int d = 0; d < PG_DIFFS; ++d) {
    const UProp p = uprop_get(cls, s_n[N_DIFF0 + d]);
    if (!p.ok || p.type != UW_STR || !pg_field_in(p.offset, STR_SIZE)) return "a difficulty string did not resolve";
    if (!engine_readable((const char*)cdo + p.offset, STR_SIZE)) return "a difficulty string is unreadable";
    char raw[64];
    read_str((const char*)cdo + p.offset, raw, sizeof raw);
    if (!pg_diff_label(raw, names[d], PG_TITLE_CAP)) return "a difficulty string is empty";
  }
  return 0;
}

static const char* read_diffs(char names[PG_DIFFS][PG_TITLE_CAP]) {
  __try {
    return read_diffs_body(names);
  } __except (av_only(GetExceptionCode())) {
    return "access violation";
  }
}

static void class_defaults(unsigned nowMs) {
  const bool tableDue = !s_table.fromGame && s_tableTries < READ_TRIES;
  const bool capsDue = !s_capsFromGame && s_capTries < READ_TRIES;
  const bool diffsDue = !s_diffFromGame && s_diffTries < READ_TRIES;
  if (!tableDue && !capsDue && !diffsDue) return;
  for (int k = 0; k < N_COUNT; ++k)
    if (!s_n[k]) s_n[k] = uname_find(kNames[k]);
  const bool missing = !s_cls[CLS_SELECT] || !s_cls[CLS_DUKE] || !s_cls[CLS_LCD];
  if (missing && (int)(nowMs - s_nextClassMs) >= 0 && uobj_count() > 0) {
    const void* found[CLS_COUNT] = { 0, 0, 0 };
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    uobj_find_classes(kClassNames, found, CLS_COUNT);
    int fresh = 0, all = 1;
    for (int k = 0; k < CLS_COUNT; ++k) {
      if (!s_cls[k] && found[k]) { s_cls[k] = found[k]; fresh++; }
      all &= s_cls[k] != 0;
    }
    // A scan that leaves a class missing backs off (5 s doubling to 5 min); logged the first time and on a find.
    s_scanMs = all ? 0 : pg_scan_backoff(s_scanMs);
    s_nextClassMs = nowMs + s_scanMs;
    if (fresh || !s_scanStreakLogged)
      np_log("progress: class scan %.2f ms: UDukeSceneChapterSelect=%p DukePlayer=%p LCDHelper=%p%s", ms_since(t0),
             s_cls[CLS_SELECT], s_cls[CLS_DUKE], s_cls[CLS_LCD],
             all ? "" : "; later scans back off, logged only on a find");
    s_scanStreakLogged = true;
  }
  if (tableDue && s_cls[CLS_SELECT] && s_n[N_CHAPTERS] && s_n[N_URL] && s_n[N_AWARDS]) {
    static PgTable t;
    const char* why = read_table(&t);
    s_tableTries++;
    if (!why) {
      int differ = t.n != PG_CHAPTERS;
      for (int i = 0; i < t.n && i < PG_CHAPTERS; ++i)
        differ += t.awardN[i] != PG_BUILTIN[i].n || memcmp(t.awards[i], PG_BUILTIN[i].ids, (size_t)PG_BUILTIN[i].n) ||
                  _stricmp(t.url[i], PG_BUILTIN[i].url) != 0;
      s_table = t;
      np_log("progress: chapter table from UDukeSceneChapterSelect's default: %d chapters, titles %s, %d entr%s "
             "differ from progress.md's table", t.n, t.titlesFromGame ? "localised" : "partly built-in", differ,
             differ == 1 ? "y" : "ies");
    } else if (s_tableTries >= READ_TRIES && !s_tableLogged) {
      s_tableLogged = true;
      np_log("progress: chapter table not read (%s) after %d tries; using progress.md's copy", why, s_tableTries);
    }
  }
  if (capsDue && s_cls[CLS_DUKE] && s_n[N_EGOCAP] && s_n[N_CAPAWARDS]) {
    float caps[PG_AWARDS], base = 0.f, max = 0.f, bonus = 0.f;
    const char* why = read_caps(caps, &base, &max, &bonus);
    s_capTries++;
    if (!why) {
      memcpy(s_caps, caps, sizeof s_caps);
      s_base = base;
      s_max = max;
      s_bonus = bonus;
      s_capsFromGame = true;
      float sum = 0.f;
      for (int i = 0; i < PG_AWARDS; ++i) sum += caps[i];
      np_log("progress: ego cap defaults from DukePlayer: base %.1f max %.1f preorder +%.1f, awards sum %.1f%s", base,
             max, bonus, sum, memcmp(caps, PG_CAP_AWARDS, sizeof caps) ? " (differs from progress.md)" : "");
    } else if (s_capTries >= READ_TRIES && !s_capsLogged) {
      s_capsLogged = true;
      np_log("progress: ego cap defaults not read (%s) after %d tries; using progress.md's values", why, s_capTries);
    }
  }
  if (diffsDue && s_cls[CLS_LCD] && s_n[N_DIFF0] && s_n[N_DIFF0 + 1] && s_n[N_DIFF0 + 2] && s_n[N_DIFF0 + 3]) {
    char names[PG_DIFFS][PG_TITLE_CAP];
    const char* why = read_diffs(names);
    s_diffTries++;
    if (!why) {
      memcpy(s_diffNames, names, sizeof s_diffNames);
      s_diffFromGame = true;
      np_log("progress: difficulty names from LCDHelper: %s | %s | %s | %s", names[0], names[1], names[2], names[3]);
    } else if (s_diffTries >= READ_TRIES && !s_diffLogged) {
      s_diffLogged = true;
      np_log("progress: difficulty names not read (%s) after %d tries; using engine.int's English", why, s_diffTries);
    }
  }
}

// --- The DLC's table: MapInfo.CampaignList and DukePlayerDLC03's defaults ---------------------------------
static const char kEmpty[] = "CampaignList holds no DLC03_ map (the DLC is not mounted yet)";

// The URLs of MapInfo.CampaignList on its class default (FMapInfoCampaign {.., URL, ..}), DLC03_ maps only.
static const char* read_campaign_body(char urls[PG_CHAPTERS_MAX][PG_URL_CAP], int* n) {
  *n = 0;
  const void* cls = s_dcls[DCLS_MAPINFO];
  const void* cdo = engine_get_default_object((void*)cls);
  if (!cdo) return "no class default";
  const UProp ap = uprop_get(cls, s_dn[ND_CAMPAIGN]);
  const void* inner = array_inner(ap);
  const void* st = inner ? uw_ptr(inner, USP_STRUCT) : 0;
  if (!st || !uobj_valid(st) || !pg_field_in(ap.offset, 8)) return "CampaignList is not an array of structs";
  const int es = elem_size(inner);
  const UProp up = uprop_get(st, s_n[N_URL]);
  if (!up.ok || up.type != UW_STR || es <= 0 || es > 0x400 || up.offset + STR_SIZE > es)
    return "CampaignList's URL did not resolve as a string";
  const char* data = (const char*)uw_ptr(cdo, ap.offset);
  const int count = uw_int(cdo, ap.offset + 4);
  if (count == 0) return kEmpty;
  if (!data || count < 0 || count > 64) return "CampaignList count out of range";
  if (!engine_readable(data, (size_t)count * es)) return "CampaignList data unreadable";
  for (int i = 0; i < count && *n < PG_CHAPTERS_MAX; ++i) {
    read_str(data + (size_t)i * es + up.offset, urls[*n], PG_URL_CAP);
    if (pg_is_dlc_map(urls[*n])) (*n)++;
  }
  return *n ? 0 : kEmpty;
}

static const char* read_campaign(char urls[PG_CHAPTERS_MAX][PG_URL_CAP], int* n) {
  __try {
    return read_campaign_body(urls, n);
  } __except (av_only(GetExceptionCode())) {
    *n = 0;
    return "access violation";
  }
}

// DukePlayerDLC03's DLC03EgoCapAwards (19 floats) and DLC03MapEgoCapAwards ({MapName, Awards}), then AutoEgoCapAward
// on its own: *autoOk 0 keeps the built-in 30 without losing the rest.
static const char* read_dlc_caps_body(float* caps, float* autoEgo, int* autoOk, PgMapAwards* aw) {
  *autoOk = 0;
  const void* cls = s_dcls[DCLS_PLAYER];
  const void* cdo = engine_get_default_object((void*)cls);
  if (!cdo) return "no class default";
  const UProp cp = uprop_get(cls, s_dn[ND_CAPS]);
  const void* cInner = array_inner(cp);
  if (!cInner || elem_size(cInner) != 4 || !pg_field_in(cp.offset, 8)) return "DLC03EgoCapAwards is not a float array";
  const void* cd = uw_ptr(cdo, cp.offset);
  if (!cd || uw_int(cdo, cp.offset + 4) != PG_DLC_AWARDS || !engine_readable(cd, PG_DLC_AWARDS * 4))
    return "DLC03EgoCapAwards count is not 19";
  memcpy(caps, cd, PG_DLC_AWARDS * 4);
  for (int i = 0; i < PG_DLC_AWARDS; ++i)
    if (!(caps[i] >= 0.f && caps[i] < 100.f)) return "a DLC03EgoCapAwards value is out of range";
  const UProp ap = uprop_get(cls, s_dn[ND_MAPAWARDS]);
  const void* inner = array_inner(ap);
  const void* st = inner ? uw_ptr(inner, USP_STRUCT) : 0;
  if (!st || !uobj_valid(st) || !pg_field_in(ap.offset, 8)) return "DLC03MapEgoCapAwards is not an array of structs";
  const int es = elem_size(inner);
  const UProp mp = uprop_get(st, s_dn[ND_MAPNAME]);
  const UProp aa = uprop_get(st, s_n[N_AWARDS]);
  const void* aaInner = array_inner(aa);
  if (!mp.ok || mp.type != UW_STR || !aaInner || elem_size(aaInner) != 1 || es <= 0 || es > 0x400 ||
      mp.offset + STR_SIZE > es || aa.offset + STR_SIZE > es)
    return "DLC03MapEgoCapAwards' MapName or Awards did not resolve as a string and a byte array";
  const char* data = (const char*)uw_ptr(cdo, ap.offset);
  const int count = uw_int(cdo, ap.offset + 4);
  if (!data || count < 1 || count > PG_CHAPTERS_MAX) return "DLC03MapEgoCapAwards count out of range";
  if (!engine_readable(data, (size_t)count * es)) return "DLC03MapEgoCapAwards data unreadable";
  memset(aw, 0, sizeof *aw);
  aw->n = count;
  for (int i = 0; i < count; ++i) {
    const char* e = data + (size_t)i * es;
    read_str(e + mp.offset, aw->map[i], PG_URL_CAP);
    const unsigned char* ad = (const unsigned char*)uw_ptr(e, aa.offset);
    const int an = uw_int(e, aa.offset + 4);
    if (an < 0 || an > 255) return "an entry's Awards count is out of range";
    aw->awardN[i] = an;
    const int k = an < PG_CH_AWARDS_MAX ? an : PG_CH_AWARDS_MAX;
    if (k && (!ad || !engine_readable(ad, (size_t)k))) return "an entry's Awards data is unreadable";
    if (k) memcpy(aw->awards[i], ad, (size_t)k);
    for (int j = 0; j < k; ++j)
      if (aw->awards[i][j] >= PG_DLC_AWARDS) return "an award id is past the DLC enum";
  }
  *autoOk = s_dn[ND_AUTO] && read_float(cls, cdo, s_dn[ND_AUTO], autoEgo) && *autoEgo >= 0.f && *autoEgo < 100.f;
  return 0;
}

static const char* read_dlc_caps(float* caps, float* autoEgo, int* autoOk, PgMapAwards* aw) {
  __try {
    return read_dlc_caps_body(caps, autoEgo, autoOk, aw);
  } __except (av_only(GetExceptionCode())) {
    return "access violation";
  }
}

static void dlc_table_rebuild(void) {
  pg_dlc_table_build(&s_dlcTable, s_dlcOrderRead ? s_dlcUrls : 0, s_dlcUrlN, s_dlcCapsFromGame ? &s_dlcAw : 0);
  int differ = s_dlcTable.n != PG_DLC_CHAPTERS;
  for (int i = 0; i < s_dlcTable.n && i < PG_DLC_CHAPTERS; ++i)
    differ += s_dlcTable.awardN[i] != PG_DLC_BUILTIN[i].n ||
              memcmp(s_dlcTable.awards[i], PG_DLC_BUILTIN[i].ids, (size_t)PG_DLC_BUILTIN[i].n) ||
              _stricmp(s_dlcTable.url[i], PG_DLC_BUILTIN[i].url) != 0;
  np_log("progress: dlc table: %d chapters, order %s, awards %s, %d entr%s differ from dlc.md's table", s_dlcTable.n,
         s_dlcOrderRead ? "from MapInfo.CampaignList" : "built-in",
         s_dlcCapsFromGame ? "from DukePlayerDLC03's default" : "built-in", differ, differ == 1 ? "y" : "ies");
}

// Read until both are in: the class scan backs off as the base one does, an empty CampaignList is tried again every
// 5 s (it fills when the DLC mounts), any other failure READ_TRIES times.
static void dlc_defaults(unsigned nowMs) {
  const bool orderDue = !s_dlcOrderRead && s_dlcOrderTries < READ_TRIES;
  const bool capsDue = !s_dlcCapsFromGame && s_dlcCapTries < READ_TRIES;
  if (!orderDue && !capsDue) return;
  for (int k = 0; k < ND_COUNT; ++k)
    if (!s_dn[k]) s_dn[k] = uname_find(kDlcNames[k]);
  if (!s_n[N_URL]) s_n[N_URL] = uname_find(kNames[N_URL]);
  if (!s_n[N_AWARDS]) s_n[N_AWARDS] = uname_find(kNames[N_AWARDS]);
  if ((!s_dcls[DCLS_PLAYER] || !s_dcls[DCLS_MAPINFO]) && (int)(nowMs - s_dlcNextScanMs) >= 0 && uobj_count() > 0) {
    const void* found[DCLS_COUNT] = { 0, 0 };
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    uobj_find_classes(kDlcClassNames, found, DCLS_COUNT);
    int fresh = 0;
    for (int k = 0; k < DCLS_COUNT; ++k)
      if (!s_dcls[k] && found[k]) { s_dcls[k] = found[k]; fresh++; }
    const bool all = s_dcls[DCLS_PLAYER] && s_dcls[DCLS_MAPINFO];
    s_dlcScanMs = all ? 0 : pg_scan_backoff(s_dlcScanMs);
    s_dlcNextScanMs = nowMs + s_dlcScanMs;
    if (fresh || !s_dlcScanLogged)
      np_log("progress: dlc class scan %.2f ms: DukePlayerDLC03=%p MapInfo=%p%s", ms_since(t0), s_dcls[DCLS_PLAYER],
             s_dcls[DCLS_MAPINFO], all ? "" : "; the DLC class loads when the DLC is entered, later scans back off");
    s_dlcScanLogged = true;
  }
  bool rebuilt = false;
  if (orderDue && s_dcls[DCLS_MAPINFO] && s_dn[ND_CAMPAIGN] && s_n[N_URL] && (int)(nowMs - s_dlcOrderNextMs) >= 0) {
    static char urls[PG_CHAPTERS_MAX][PG_URL_CAP];
    int n = 0;
    const char* why = read_campaign(urls, &n);
    if (!why) {
      memcpy(s_dlcUrls, urls, sizeof s_dlcUrls);
      s_dlcUrlN = n;
      s_dlcOrderRead = 1;
      rebuilt = true;
    } else if (why == kEmpty) {
      s_dlcOrderNextMs = nowMs + PG_SCAN_MIN_MS;
      if (!s_dlcEmptyLogged) {
        s_dlcEmptyLogged = true;
        np_log("progress: dlc: %s; the built-in order serves, tried again every 5 s (logged once)", why);
      }
    } else if (++s_dlcOrderTries >= READ_TRIES && !s_dlcOrderLogged) {
      s_dlcOrderLogged = true;
      np_log("progress: dlc: CampaignList not read (%s) after %d tries; using dlc.md's order", why, s_dlcOrderTries);
    }
  }
  if (capsDue && s_dcls[DCLS_PLAYER] && s_dn[ND_CAPS] && s_dn[ND_MAPAWARDS] && s_dn[ND_MAPNAME] && s_n[N_AWARDS]) {
    float caps[PG_DLC_AWARDS], autoEgo = 0.f;
    int autoOk = 0;
    static PgMapAwards aw;
    const char* why = read_dlc_caps(caps, &autoEgo, &autoOk, &aw);
    s_dlcCapTries++;
    if (!why) {
      memcpy(s_dlcCaps, caps, sizeof s_dlcCaps);
      if (autoOk) s_dlcAuto = autoEgo;
      else np_log("progress: dlc: AutoEgoCapAward did not read as a float; keeping dlc.md's %.1f", (double)s_dlcAuto);
      s_dlcAw = aw;
      s_dlcCapsFromGame = true;
      rebuilt = true;
      float sum = 0.f;
      for (int i = 0; i < PG_DLC_AWARDS; ++i) sum += caps[i];
      np_log("progress: dlc ego defaults from DukePlayerDLC03: auto +%.1f, awards sum %.1f%s, %d map entries", s_dlcAuto,
             sum, memcmp(caps, PG_DLC_CAP_AWARDS, sizeof caps) ? " (differs from dlc.md)" : "", aw.n);
    } else if (s_dlcCapTries >= READ_TRIES && !s_dlcCapsLogged) {
      s_dlcCapsLogged = true;
      np_log("progress: dlc ego defaults not read (%s) after %d tries; using dlc.md's values", why, s_dlcCapTries);
    }
  }
  if (rebuilt) dlc_table_rebuild();
}

// --- The level: Pawn.EgoCap and the map's package name --------------------------------------------------
static bool live_body(const void* player, float* cap, int* mapName, int* clsName) {
  *mapName = 0;
  *clsName = uobj_name(uobj_class(player));
  const void* top = player;
  for (int i = 0; i < 8; ++i) {
    const void* o = uobj_outer(top);
    if (!o || !uobj_valid(o)) break;
    top = o;
  }
  if (top != player) *mapName = uobj_name(top);
  return s_n[N_EGOCAP] && read_float(uobj_class(player), player, s_n[N_EGOCAP], cap);
}

static bool read_live(const void* player, float* cap, int* mapName, int* clsName) {
  __try {
    return live_body(player, cap, mapName, clsName);
  } __except (av_only(GetExceptionCode())) {
    *mapName = *clsName = 0;
    return false;
  }
}

// The map's chapter for this player and level (base and DLC tables); -1 without a player or for a map in no chapter.
static void update_chapter(const void* player, float* cap, bool* capOk) {
  int mapName = 0, clsName = 0;
  float c = 0.f;
  const bool ok = player && read_live(player, &c, &mapName, &clsName);
  if (cap) *cap = c;
  if (capOk) *capOk = ok;
  int chapter = -1, dlcChapter = -1, dlc = 0, playerDlc = -1;
  if (player && mapName) {
    char pkg[64], eng[64];
    pg_ascii(uname_str(mapName), 63, pkg, sizeof pkg);   // outside the guard: it takes the names lock
    world_level_map_file(eng, sizeof eng);
    // The DLC class's name exists only while its package is loaded: looked up each time, absent when not found.
    const int dlcCls = uname_find(kDlcNames[ND_PLAYERCLS]);
    playerDlc = clsName ? (dlcCls && clsName == dlcCls) : -1;
    const char* picked = pg_pick_map_name(s_table, pkg, eng, &s_dlcTable);
    chapter = pg_chapter_of_map(s_table, picked);
    dlc = pg_dlc_mode(picked, playerDlc);
    dlcChapter = dlc ? pg_chapter_of_map(s_dlcTable, picked) : -1;
    strncpy_s(s_mapNow, sizeof s_mapNow, picked[0] ? picked : pkg, _TRUNCATE);   // an empty MapFileName: the package
    // Again when the chapter moves within a level: MapFileName may still be empty on the first call.
    if (world_level_serial() != s_chapterSerial || player != s_chapterPlayer || chapter != s_chapterNow ||
        dlc != s_dlcNow || dlcChapter != s_dlcChapterNow)
      np_log("progress: level package '%s', LevelInfo.MapFileName '%s', chapter %d; DLC mode %d (player class "
             "DukePlayerDLC03 %s), DLC chapter %d", pkg, eng, chapter, dlc,
             playerDlc < 0 ? "unread" : playerDlc ? "yes" : "no", dlcChapter);
  }
  if (!player || !mapName) s_mapNow[0] = 0;
  s_dlcNow = dlc;
  s_dlcChapterNow = dlcChapter;
  s_playerDlc = playerDlc;
  s_chapterNow = chapter;
  s_chapterPlayer = player;
  s_chapterSerial = world_level_serial();
}

// --- The DLC instrument ---------------------------------------------------------------------------------
// One line per DLC level at its first read with flags: the campaign, the chapter, where the table came from, the ticks
// and awards read, and Pawn.EgoCap against PreGameInit's formula (dlc.md open 2: 70 expected with nothing found).
static unsigned s_dlcLogSerial;
static int s_dlcLogged;
static void log_dlc_level(bool capOk, float cap) {
  if (!s_dlcNow || !s_flagsRead || (s_dlcLogged && world_level_serial() == s_dlcLogSerial)) return;
  s_dlcLogged = 1;
  s_dlcLogSerial = world_level_serial();
  int ticks = 0;
  for (int d = 0; d < PG_DIFFS; ++d) ticks += s_dlcTot.doneAt[d];
  float found = 0.f;
  for (int i = 0; i < PG_DLC_AWARDS; ++i)
    if (s_dlcFlags.award[i]) found += s_dlcCaps[i];
  const float bonus = s_flags.bonusEgo ? s_bonus : 0.f;
  const float formula = s_base + bonus + s_dlcAuto + found;
  const int c = s_dlcChapterNow;
  np_log("progress: dlc level serial %u: campaign The Doctor Who Cloned Me, chapter %d '%s' (map '%s'), table order %s, "
         "awards %s, caps %s; ticks %d of %d read, ego awards %d/%d (ever %d/%d); Pawn.EgoCap %s%.1f against the formula "
         "%.1f (base %.1f + preorder %.1f + auto %.1f + found caps %.1f), max %d", s_dlcLogSerial, c,
         c >= 0 && c < s_dlcChN ? s_dlcCh[c].title : "none", s_mapNow,
         (s_dlcTable.fromGame & PG_SRC_ORDER) ? "game" : "built-in",
         (s_dlcTable.fromGame & PG_SRC_AWARDS) ? "game" : "built-in", s_dlcCapsFromGame ? "game" : "built-in", ticks,
         s_dlcChN * PG_DIFFS, s_dlcTot.run.awardsFound, s_dlcTot.run.awardsTotal, s_dlcTot.ever, s_dlcTot.everTotal,
         capOk ? "" : "(unread) ", (double)cap, (double)formula, (double)s_base, (double)bonus, (double)s_dlcAuto,
         (double)found, s_dlcTot.run.capMax);
}

// --- The refresh ----------------------------------------------------------------------------------------
int progress_refresh(unsigned nowMs) {
  if (!pg_refresh_due(s_haveRead, s_lastMs, nowMs, s_force)) return 0;
  s_force = 0;
  s_haveRead = 1;
  s_lastMs = nowMs;
  init_once();
  LARGE_INTEGER t0;
  QueryPerformanceCounter(&t0);
  class_defaults(nowMs);
  dlc_defaults(nowMs);
  double probeMs = 0.0;
  if (!engine_config_ready()) {
    s_note = "The game's config cache is not available; see nukepanel.log.";
    if (!s_cfgLogged) {
      s_cfgLogged = true;
      np_log("progress: GConfig unavailable; rows show 0 until it reads");
    }
  } else if (!pg_cfg_read_allowed(engine_level_loading())) {
    s_loadSkips++;   // the last read stays on show; the next refresh retries
    if (!s_loadLogged) {
      s_loadLogged = true;
      np_log("progress: config reads paused while a level loads (g_LoadingOnThread); showing the last read (logged "
             "once, counted as load skips)");
    }
  } else if (!pg_gate_read(&s_gate, world_level_serial())) {
    // No Get has found a key yet and this level was probed: Progress.ini is not in the cache (pg_gate_read).
  } else {
    PgFlags f;
    const int r = read_flags(&f, &probeMs);
    if (probeMs > s_probeMax) s_probeMax = probeMs;
    pg_gate_note(&s_gate, r > 0 ? s_foundKeys : 0);
    if (r > 0 && !s_gate.seenFound) {
      s_flags = f;
      s_flagsRead = 1;
      s_note = "Progress.ini holds no progress yet (a new profile?); read again at the next level.";
      if (!s_missLogged) {
        s_missLogged = true;
        np_log("progress: no Progress.ini key found (file missing or a new profile; first Get %.2f ms); reading once "
               "per level until one is (logged once)", probeMs);
      }
    } else if (r > 0) {
      s_flags = f;
      s_flagsRead = 1;
      s_note = 0;
    }
    if (r > 0) {
      static PgDlcFlags df;
      if (read_dlc_flags(s_dlcTable, &df) > 0) s_dlcFlags = df;
      else {
        s_faults++;
        np_log("progress: access violation inside the DLC config read (#%u)", s_faults);
      }
    } else if (r == 0) {
      s_slowSkips++;
      s_note = "Progress.ini is not in the game's cache yet; showing the last read.";
      if (!s_slowLogged) {
        s_slowLogged = true;
        np_log("progress: the first config read took %.2f ms (Progress.ini not cached?); the rest skipped, retried "
               "%s (logged once)", probeMs, s_gate.seenFound ? "each second" : "at the next level");
      }
    } else {
      s_faults++;
      s_note = "Reading Progress.ini faulted; see nukepanel.log.";
      np_log("progress: access violation inside the config cache read (#%u)", s_faults);
    }
  }
  float cap = 0.f;
  bool capOk = false;
  update_chapter(world_player(), &cap, &capOk);
  const int chapter = s_chapterNow;
  s_chN = pg_build_chapters(s_table, s_flags, s_ch);
  s_tot = pg_totals(s_flags, s_caps, s_base, s_max, s_bonus, capOk, cap);
  s_dlcChN = pg_build_dlc_chapters(s_dlcTable, s_dlcFlags, s_dlcCh);
  s_dlcTot = pg_dlc_totals(s_dlcFlags, s_dlcChN, s_flags.bonusEgo, s_dlcCaps, s_base, s_dlcAuto, s_max, s_bonus,
                           capOk && s_dlcNow, cap);
  log_dlc_level(capOk, cap);
  const double ms = ms_since(t0);
  if (ms > s_msMax) s_msMax = ms;
  s_reads++;
  int done = 0;
  for (int i = 0; i < s_chN; ++i)
    for (int d = 0; d < PG_DIFFS; ++d) done += s_ch[i].done[d];
  if (s_reads == 1 || s_tot.run.awardsFound != s_loggedFound || done != s_loggedDone || chapter != s_loggedChapter ||
      s_reads % 300 == 0) {
    s_loggedFound = s_tot.run.awardsFound;
    s_loggedDone = done;
    s_loggedChapter = chapter;
    np_log("progress: read #%u %.3f ms (max %.3f, probe max %.3f, slow skips %u, load skips %u): awards %d/35, "
           "chapter ticks %d, "
           "helmets %d/3, phones %d/12, preorder ego %d, cap %d/%d now %s%d, chapter now %d, table %s, caps %s",
           s_reads, ms, s_msMax, s_probeMax, s_slowSkips, s_loadSkips, s_tot.run.awardsFound, done,
           s_tot.profile.helmets,
           s_tot.profile.phones, s_flags.bonusEgo, s_tot.run.capPermanent, s_tot.run.capMax, capOk ? "" : "(none) ",
           s_tot.run.capNow, chapter,
           s_table.fromGame ? "game" : "built-in", s_capsFromGame ? "game" : "built-in");
  }
  return 1;
}

void progress_frame(int tabOpen, int overlayOn, unsigned nowMs) {
  if (tabOpen && !s_wasTab) s_force = 1;   // the tab's opening edge reads at once
  s_wasTab = tabOpen;
  // A new level or player (or none) moves the chapter on this frame, not at the next 1 Hz read.
  const void* player = world_player();
  const bool moved = player != s_chapterPlayer || (player && world_level_serial() != s_chapterSerial);
  if (!(tabOpen || overlayOn) || !progress_refresh(nowMs)) {
    if (moved) {
      init_once();
      update_chapter(player, 0, 0);
    }
  }
}

const PgChapter* progress_chapters(int* n) {
  init_once();
  if (n) *n = s_chN;
  return s_ch;
}

PgTotals progress_totals(void) {
  init_once();
  return s_tot;
}

int progress_award_found(int id) { return pg_award_found(s_flags, id); }
int progress_dlc_award_found(int id) { return pg_dlc_award_found(s_dlcFlags, id); }

const PgChapter* progress_dlc_chapters(int* n) {
  init_once();
  if (n) *n = s_dlcChN;
  return s_dlcCh;
}

PgDlcTotals progress_dlc_totals(void) {
  init_once();
  return s_dlcTot;
}
int progress_helmet_found(int letter) { return pg_helmet_found(s_flags, letter); }
int progress_phone_found(int id) { return pg_phone_found(s_flags, id); }

int progress_chapter_now(void) { return s_chapterNow; }
int progress_is_dlc(void) { return s_dlcNow; }
int progress_dlc_chapter_now(void) { return s_dlcChapterNow; }

const char* progress_note(void) { return s_note; }

const char* progress_map_now(void) { return s_mapNow; }
int progress_flags_read(void) { return s_flagsRead; }

static int profile_get_body(const wchar_t* sec, const wchar_t* key, int* value, int isBool) {
  return isBool ? engine_config_get_bool(sec, key, value, kFile) : engine_config_get_int(sec, key, value, kFile);
}

static int profile_get(const wchar_t* sec, const wchar_t* key, int* value, int isBool) {
  *value = 0;
  if (!engine_config_ready() || !pg_cfg_read_allowed(engine_level_loading()) || !s_gate.seenFound) return -1;
  int found;
  __try {
    found = profile_get_body(sec, key, value, isBool);
  } __except (av_only(GetExceptionCode())) {
    *value = 0;
    return -1;
  }
  if (!found) *value = 0;
  return found ? 1 : 0;
}

int progress_profile_int(const wchar_t* sec, const wchar_t* key, int* value) { return profile_get(sec, key, value, 0); }
int progress_profile_bool(const wchar_t* sec, const wchar_t* key, int* value) { return profile_get(sec, key, value, 1); }

const char* progress_diff_name(int d) {
  init_once();
  return s_diffNames[d >= 0 && d < PG_DIFFS ? d : 0];
}

const char* const* progress_diff_names(void) {
  init_once();
  return s_diffPtrs;
}

#pragma once
// Progress (docs/re/progress.md): ego awards and chapter completion from the
// game's own Progress.ini through its config cache, the per-chapter award table from the Chapter Select class
// default. Read only. The rules are pure for the suite; progress.cpp reads the engine, menu.cpp draws.
#include <stdio.h>
#include <string.h>
#include <wchar.h>

enum { PG_AWARDS = 35, PG_CHAPTERS = 24, PG_CHAPTERS_MAX = 32, PG_CH_AWARDS_MAX = 32, PG_DIFFS = 4, PG_PARTS = 3,
       PG_COMPLETION_LEVELS = 23, PG_HELMETS = 3, PG_PHONES = 12, PG_TITLE_CAP = 40, PG_URL_CAP = 16 };

// Engine.u Object.EEgoAwardID in id order; the [EgoAwards] key is "EGOAWARD_" + the name (HasDiscoveredEgoAward:
// GetEnum of the id).
static const char* const PG_AWARD_NAMES[PG_AWARDS] = {
  "Toilet", "PokerWin", "SlotsWin", "WorkoutBench", "WorkoutBenchMaxLift", "AirHockeyWin", "PayBinoculars",
  "CopyMachine", "GloryHole", "PinballHighScore", "HandWeight", "Calendar", "PaperAirPlane", "Frisbee",
  "PornMagazineA", "PornMagazineB", "PornMagazineC", "SmokeCigar", "PunchingBag", "SpeedBag", "WallBoobs", "Mirror",
  "Microwave", "PoolWin", "BasketBall", "ComputerPorn", "BookSigning", "DefeatBattlelord1", "DefeatAlienQueen",
  "AlienAbortion", "DukePorno", "TipStripper", "DefeatBattlelord2", "DefeatOctaking", "DefeatLeech" };

// DukePlayer defaults (dnGame.u, progress.md "Ego cap"): EgoCapAwards per id (sum 60), Pawn.EgoCap 40, MaxEgoCap 100,
// PreorderBonusEgo 5. The fallback when the class default does not read.
static const float PG_CAP_AWARDS[PG_AWARDS] = { 1, 1, 2, 1, 2, 3, 1, 1, 2, 3, 2, 1, 1, 1, 1, 1, 1, 1,
                                                1, 1, 1, 1, 2, 2, 1, 1, 2, 3, 4, 1, 2, 1, 4, 3, 4 };
const float PG_CAP_BASE = 40.f, PG_CAP_MAX = 100.f, PG_PREORDER_EGO = 5.f;

// The parts of each chapter, bit = chapter: mapNN, mapNNb, mapNNc (dnGame.dll!0x10097734, set in
// execHasCompletionForLevel 0x1003E630).
static const unsigned PG_PART_MASK[PG_PARTS] = { 0x7FFFFFu, 0x3B58D0u, 0x810u };

// UDukeSceneChapterSelect.Chapters as the package holds it (progress.md section 1): the fallback while the class
// default is unread, and the suite's oracle. 65 entries, 34 distinct ids (3 is in none).
struct PgBuiltin { const char* url; const char* title; int n; unsigned char ids[14]; };
static const PgBuiltin PG_BUILTIN[PG_CHAPTERS] = {
  { "map00", "Duke Lives", 3, { 21, 0, 23 } },
  { "map01", "Damn! It's Late...", 4, { 21, 0, 26, 22 } },
  { "map02", "The Duke Cave", 7, { 21, 10, 4, 9, 19, 18, 24 } },
  { "map03", "Mothership Battle", 0, { 0 } },
  { "map04", "The Lady Killer", 1, { 2 } },
  { "map05", "Vegas in Ruin", 1, { 27 } },
  { "map06", "The Duke Dome", 1, { 7 } },
  { "map07", "The Hive", 1, { 20 } },
  { "map08", "Queen Bitch", 5, { 10, 4, 18, 20, 28 } },
  { "map09", "Duke Nukem's Titty City", 14, { 21, 22, 9, 1, 2, 0, 7, 23, 24, 5, 29, 8, 30, 31 } },
  { "map10", "Crash Course", 1, { 13 } },
  { "map11", "The Duke Burger", 5, { 0, 21, 17, 14, 24 } },
  { "map12", "The Mighty Foot", 3, { 25, 13, 6 } },
  { "map13", "Ghost Town", 1, { 16 } },
  { "map14", "Highway Battle", 0, { 0 } },
  { "map15", "Dam Top", 2, { 12, 32 } },
  { "map16", "The Shrunk Machine", 2, { 12, 11 } },
  { "map17", "The Forkstop", 2, { 15, 11 } },
  { "map18", "Generator Room", 3, { 0, 21, 33 } },
  { "map19", "Underground", 4, { 20, 12, 16, 25 } },
  { "map20", "The Clarifier", 2, { 14, 17 } },
  { "map21", "Blowin' the Dam", 3, { 15, 12, 34 } },
  { "map22", "Final Battle", 0, { 0 } },
  { "map23", "Press Conference", 0, { 0 } },
};

// The chapter table: awardN is the entry's Awards.Num() as the game counts it, awards the first PG_CH_AWARDS_MAX ids.
struct PgTable {
  int n, fromGame, titlesFromGame;
  char url[PG_CHAPTERS_MAX][PG_URL_CAP];
  char title[PG_CHAPTERS_MAX][PG_TITLE_CAP];
  int awardN[PG_CHAPTERS_MAX];
  unsigned char awards[PG_CHAPTERS_MAX][PG_CH_AWARDS_MAX];
};
inline void pg_table_builtin(PgTable* t) {
  memset(t, 0, sizeof *t);
  t->n = PG_CHAPTERS;
  for (int i = 0; i < PG_CHAPTERS; ++i) {
    strcpy_s(t->url[i], PG_URL_CAP, PG_BUILTIN[i].url);
    strcpy_s(t->title[i], PG_TITLE_CAP, PG_BUILTIN[i].title);
    t->awardN[i] = PG_BUILTIN[i].n;
    memcpy(t->awards[i], PG_BUILTIN[i].ids, (size_t)PG_BUILTIN[i].n);
  }
}

// What Progress.ini says; every field 0 for a key the cache does not have.
struct PgFlags {
  unsigned char award[PG_AWARDS];
  unsigned completion[PG_DIFFS][PG_PARTS];   // [Progress] GameCompletion_<diff>_<part>
  unsigned char helmet[PG_HELMETS];          // [Achievements] HelmetA..C
  unsigned char phone[PG_PHONES];            // [Achievements] PhoneMessage1..12
  int bonusEgo;                              // [PreorderBonus] BonusEgo
};

// A Get's result as the game's callers take it: GetConfigBool's Found stays 0 on a miss and the award counts when
// it is nonzero (HasDiscoveredEgoAward); execHasCompletionForLevel zeroes the word when GetInt misses.
inline int pg_bool_from_get(int found, int value) { return found && value != 0 ? 1 : 0; }
inline unsigned pg_int_from_get(int found, int value) { return found ? (unsigned)value : 0u; }

// The game-thread config reads pause while g_LoadingOnThread is set: the loading thread writes the unlocked cache
// then (RescanDLC's ResetDLC and the Big Package re-apply, big-package.md section 6).
inline bool pg_cfg_read_allowed(int loading) { return loading == 0; }

inline int pg_award_found(const PgFlags& f, int id) { return id >= 0 && id < PG_AWARDS && f.award[id] ? 1 : 0; }

// HasCompletionForLevel(diff, level): levels 0..22 only; complete when every part whose mask has the level's bit
// has it in that difficulty's word.
inline int pg_level_done(const PgFlags& f, int diff, int level) {
  if (diff < 0 || diff >= PG_DIFFS || level < 0 || level >= PG_COMPLETION_LEVELS) return 0;
  const unsigned bit = 1u << level;
  for (int p = 0; p < PG_PARTS; ++p)
    if ((PG_PART_MASK[p] & bit) && !(f.completion[diff][p] & bit)) return 0;
  return 1;
}

// Helmets and phone messages per chapter (progress.md section 6, "Per chapter"): fixed in the map packages, keyed
// by the chapter's URL so a table read from the game lines up. helmets: bit = letter (A..C); phones: bit = id - 1.
struct PgCollectChapter { const char* url; unsigned helmets, phones; };
static const PgCollectChapter PG_COLLECT[] = {
  { "map00", 0, 1u << 0 },                                // phone 1
  { "map05", 1u << 0, 0 },                                // helmet A
  { "map06", 0, 1u << 1 },                                // phone 2
  { "map07", 1u << 1, 0 },                                // helmet B
  { "map09", 0, (1u << 2) | (1u << 3) | (1u << 4) },      // phones 3, 4, 5
  { "map11", 0, 1u << 5 },                                // phone 6
  { "map12", 0, 1u << 6 },                                // phone 7 (Map12B)
  { "map16", 0, 1u << 7 },                                // phone 8
  { "map18", 0, 1u << 8 },                                // phone 9
  { "map20", 0, (1u << 9) | (1u << 10) | (1u << 11) },    // phones 10, 11, 12 (Map20B)
  { "map21", 1u << 2, 0 },                                // helmet C
};
enum { PG_COLLECT_COUNT = (int)(sizeof PG_COLLECT / sizeof PG_COLLECT[0]) };

inline int pg_helmet_found(const PgFlags& f, int letter) {
  return letter >= 0 && letter < PG_HELMETS && f.helmet[letter] ? 1 : 0;
}
// id 1..12, as the [Achievements] key PhoneMessage<id>.
inline int pg_phone_found(const PgFlags& f, int id) { return id >= 1 && id <= PG_PHONES && f.phone[id - 1] ? 1 : 0; }

struct PgChapter {
  char title[PG_TITLE_CAP]; char url[PG_URL_CAP]; int egoFound, egoTotal; int done[PG_DIFFS];
  int helmFound, helmTotal, phoneFound, phoneTotal;   // profile; 0/0 for a chapter with none
};

// A chapter's helmets and phones found and in all, by its URL.
inline void pg_collect_counts(const char* url, const PgFlags& f, int* hFound, int* hTotal, int* pFound, int* pTotal) {
  *hFound = *hTotal = *pFound = *pTotal = 0;
  for (int k = 0; url && k < PG_COLLECT_COUNT; ++k) {
    if (_stricmp(url, PG_COLLECT[k].url) != 0) continue;
    for (int l = 0; l < PG_HELMETS; ++l)
      if (PG_COLLECT[k].helmets & (1u << l)) { (*hTotal)++; *hFound += pg_helmet_found(f, l); }
    for (int i = 0; i < PG_PHONES; ++i)
      if (PG_COLLECT[k].phones & (1u << i)) { (*pTotal)++; *pFound += pg_phone_found(f, i + 1); }
  }
}

// One row per table entry, as CheckEgoAwardAndSetDifficulty counts: every entry of Awards whose id is discovered
// (an id listed twice counts twice; one the game never grants stays unfound), over Awards.Num(); completion by
// chapter index.
inline int pg_build_chapters(const PgTable& t, const PgFlags& f, PgChapter* out) {
  const int n = t.n < PG_CHAPTERS_MAX ? t.n : PG_CHAPTERS_MAX;
  for (int i = 0; i < n; ++i) {
    PgChapter& c = out[i];
    memset(&c, 0, sizeof c);
    strcpy_s(c.title, PG_TITLE_CAP, t.title[i]);
    strcpy_s(c.url, PG_URL_CAP, t.url[i]);
    c.egoTotal = t.awardN[i];
    const int k = t.awardN[i] < PG_CH_AWARDS_MAX ? t.awardN[i] : PG_CH_AWARDS_MAX;
    for (int j = 0; j < k; ++j) c.egoFound += pg_award_found(f, t.awards[i][j]);
    for (int d = 0; d < PG_DIFFS; ++d) c.done[d] = pg_level_done(f, d, i);
    pg_collect_counts(c.url, f, &c.helmFound, &c.helmTotal, &c.phoneFound, &c.phoneTotal);
  }
  return n < 0 ? 0 : n;
}

// The totals split by scope (progress.md 5): New Game clears [EgoAwards], so awards and their cap are per playthrough;
// helmets and phones live in profile sections nothing clears. Ego cap: permanent = GetDefaultEgoCap() + found awards'
// caps, max = GetMaxEgoCap(); both add the preorder bonus when [PreorderBonus] BonusEgo is set. now: Pawn.EgoCap.
struct PgRunTotals { int awardsFound, awardsTotal, capPermanent, capMax, capNow, capNowOk; };
struct PgProfileTotals { int helmets, phones; };
struct PgTotals { PgRunTotals run; PgProfileTotals profile; };
inline int pg_round(float v) { return v != v ? 0 : (int)(v + (v < 0.f ? -0.5f : 0.5f)); }
inline PgRunTotals pg_run_totals_n(const unsigned char* award, int n, int bonusEgo, const float* caps, float base,
                                   float max, float bonus, int nowOk, float now) {
  PgRunTotals t;
  memset(&t, 0, sizeof t);
  t.awardsTotal = n;
  float perm = base + (bonusEgo ? bonus : 0.f);
  const float top = max + (bonusEgo ? bonus : 0.f);
  for (int i = 0; i < n; ++i)
    if (award[i]) { t.awardsFound++; perm += caps[i]; }
  t.capPermanent = pg_round(perm < top ? perm : top);
  t.capMax = pg_round(top);
  t.capNowOk = nowOk;
  t.capNow = nowOk ? pg_round(now) : 0;
  return t;
}
inline PgRunTotals pg_run_totals(const PgFlags& f, const float caps[PG_AWARDS], float base, float max, float bonus,
                                 int nowOk, float now) {
  return pg_run_totals_n(f.award, PG_AWARDS, f.bonusEgo, caps, base, max, bonus, nowOk, now);
}
inline PgProfileTotals pg_profile_totals(const PgFlags& f) {
  PgProfileTotals t;
  memset(&t, 0, sizeof t);
  for (int i = 0; i < PG_HELMETS; ++i) t.helmets += f.helmet[i] != 0;
  for (int i = 0; i < PG_PHONES; ++i) t.phones += f.phone[i] != 0;
  return t;
}
inline PgTotals pg_totals(const PgFlags& f, const float caps[PG_AWARDS], float base, float max, float bonus, int nowOk,
                          float now) {
  PgTotals t;
  t.run = pg_run_totals(f, caps, base, max, bonus, nowOk, now);
  t.profile = pg_profile_totals(f);
  return t;
}

// Reads at most once a second, plus at once when forced (the tab opening) or before the first read.
const unsigned PG_REFRESH_MS = 1000;
inline int pg_refresh_due(int haveRead, unsigned lastMs, unsigned nowMs, int forced) {
  return forced || !haveRead || nowMs - lastMs >= PG_REFRESH_MS;
}
// A cached Get costs microseconds (progress.md "Reading found state"); one this slow went to the disk (Progress.ini
// not cached), so the rest of that refresh is skipped.
const double PG_PROBE_SLOW_MS = 5.0;
inline int pg_probe_fast(double ms) { return ms < PG_PROBE_SLOW_MS; }

// progress.md "Reading found state": a Get on a file the cache does not hold goes to the disk (FConfigCacheIni_Find
// then FileSize), so until one Get has returned found the keys are read once per level, never each second.
struct PgGate { int seenFound, probed; unsigned level; };
inline int pg_gate_read(PgGate* g, unsigned levelSerial) {
  if (g->seenFound) return 1;
  if (g->probed && g->level == levelSerial) return 0;
  g->probed = 1;
  g->level = levelSerial;
  return 1;
}
inline void pg_gate_note(PgGate* g, int foundKeys) {
  if (foundKeys > 0) g->seenFound = 1;
}

// A class scan that finds nothing waits 5 s, doubling to 5 min (a class may load later; the scan walks the whole
// object table).
const unsigned PG_SCAN_MIN_MS = 5000, PG_SCAN_MAX_MS = 300000;
inline unsigned pg_scan_backoff(unsigned ms) {
  if (ms < PG_SCAN_MIN_MS) return PG_SCAN_MIN_MS;
  return ms >= PG_SCAN_MAX_MS / 2 ? PG_SCAN_MAX_MS : ms * 2;
}

// A property read stays inside the object: offset and size from reflection, against a ceiling no DNF object
// reaches (a larger offset is garbage, not a property).
const int PG_OBJ_SPAN_MAX = 0x10000;
inline bool pg_field_in(int offset, int size) {
  return offset >= 0 && size > 0 && offset <= PG_OBJ_SPAN_MAX - size;
}

// The map's name without a path or an extension.
inline void pg_map_key(const char* in, char* out, int cap) {
  out[0] = 0;
  if (!in) return;
  const char* s = in;
  for (const char* p = in; *p; ++p)
    if (*p == '/' || *p == '\\') s = p + 1;
  int n = 0;
  while (s[n] && s[n] != '.' && n < cap - 1) { out[n] = s[n]; ++n; }
  out[n] = 0;
}

// The whole name equal to an entry's URL first (a DLC map, DLC03_MapNN: dlc.md section 1), then
// SPSetEgoAwardsForLevel's match (Engine.dll!0x10365E20): the map name lower-cased and cut to 5 characters against
// each entry's URL, so map04b is chapter 4 and Map_mydigs none (-1).
inline int pg_chapter_of_map(const PgTable& t, const char* map) {
  if (!map) return -1;
  char full[64];
  pg_map_key(map, full, sizeof full);
  for (int i = 0; full[0] && i < t.n && i < PG_CHAPTERS_MAX; ++i)
    if (_stricmp(t.url[i], full) == 0) return i;
  char key[6];
  int n = 0;
  for (; n < 5 && map[n]; ++n) key[n] = (char)(map[n] >= 'A' && map[n] <= 'Z' ? map[n] + 32 : map[n]);
  key[n] = 0;
  if (n < 5) return -1;
  for (int i = 0; i < t.n && i < PG_CHAPTERS_MAX; ++i)
    if (_stricmp(t.url[i], key) == 0) return i;
  return -1;
}

// The map name to resolve a chapter from: the level package's name when it is a chapter map, else LevelInfo.MapFileName
// (a checkpoint loads from a package named cpNN, so its name is no chapter). Never null; "" when neither is given.
// t2: a second table whose maps count as chapter maps too (the DLC's).
inline const char* pg_pick_map_name(const PgTable& t, const char* pkg, const char* engine, const PgTable* t2 = 0) {
  if (pkg && (pg_chapter_of_map(t, pkg) >= 0 || (t2 && pg_chapter_of_map(*t2, pkg) >= 0))) return pkg;
  return engine ? engine : "";
}

// --- The Doctor Who Cloned Me (docs/re/dlc.md) -----------------------------------------------------------------
enum { PG_DLC_AWARDS = 19, PG_DLC_CHAPTERS = 14 };
// DukePlayerDLC03.EDLC03EgoAwardID in id order; the key is "DLC03EGOAWARD_" + the name (dlc.md section 3).
static const char* const PG_DLC_AWARD_NAMES[PG_DLC_AWARDS] = {
  "AmericanFlag", "ArcadeGame", "ArmWrestling", "ComputerPorn", "DartBoard", "DeadLift", "DefeatProton", "FilmReel",
  "HotSauce", "Pinball", "PornMagazineE", "PornMagazineD", "PullUp", "SpeedBag", "WorkoutBench", "HandWeight",
  "BasketBall", "Toilet", "Mirror" };
// DukePlayerDLC03 defaults: DLC03EgoCapAwards (sum 35) and AutoEgoCapAward, which PreGameInit adds on top of the
// base cap; the fallback when the class default does not read.
static const float PG_DLC_CAP_AWARDS[PG_DLC_AWARDS] = { 2, 3, 2, 1, 2, 2, 4, 2, 1, 3, 1, 1, 2, 1, 3, 2, 1, 1, 1 };
const float PG_DLC_AUTO_EGO = 30.f;
// MapInfo.CampaignList's order (= unlock order) with DLC03MapEgoCapAwards per map (dlc.md section 1).
static const PgBuiltin PG_DLC_BUILTIN[PG_DLC_CHAPTERS] = {
  { "DLC03_Map00", "Rude Awakening", 1, { 17 } },
  { "DLC03_Map04", "Going Down", 1, { 10 } },
  { "DLC03_Map05", "In Security", 2, { 8, 3 } },
  { "DLC03_Map06", "The Clone Carousel", 1, { 11 } },
  { "DLC03_Map07", "Me, Myself, and I", 9, { 15, 13, 5, 12, 14, 2, 7, 16, 18 } },
  { "DLC03_Map10", "Meat Grinder", 1, { 3 } },
  { "DLC03_Map11", "The Doctor Is In", 2, { 6, 18 } },
  { "DLC03_Map12", "Magic Carpet Ride", 2, { 4, 10 } },
  { "DLC03_Map13", "Drunken Crane Master", 2, { 3, 8 } },
  { "DLC03_Map01", "All Your Base...", 3, { 11, 15, 13 } },
  { "DLC03_Map02", "Tunnel Vision", 0, { 0 } },
  { "DLC03_Map03", "The Burning Bush", 5, { 3, 17, 1, 9, 18 } },
  { "DLC03_Map14", "Teleportation Spire", 0, { 0 } },
  { "DLC03_Map15", "Duke Side of the Moon", 1, { 0 } },
};

// DukePlayerDLC03.DLC03MapEgoCapAwards as read: {MapName, Awards} per map.
struct PgMapAwards {
  int n;
  char map[PG_CHAPTERS_MAX][PG_URL_CAP];
  int awardN[PG_CHAPTERS_MAX];
  unsigned char awards[PG_CHAPTERS_MAX][PG_CH_AWARDS_MAX];
};
// PgTable.fromGame bits for the DLC table: the order came from CampaignList, the awards from the DLC class.
enum { PG_SRC_ORDER = 1, PG_SRC_AWARDS = 2 };
// The order from CampaignList's URLs when read (nUrls > 0), else the built-in; each chapter's awards from the game's
// list by map name when read (a map it does not list has none, as the game counts), else the built-in by URL. Titles
// are the built-in English by URL (the game's come from Localize), else the URL.
inline void pg_dlc_table_build(PgTable* t, const char (*urls)[PG_URL_CAP], int nUrls, const PgMapAwards* aw) {
  memset(t, 0, sizeof *t);
  const bool order = urls && nUrls > 0, awards = aw && aw->n > 0;
  t->n = order ? (nUrls < PG_CHAPTERS_MAX ? nUrls : PG_CHAPTERS_MAX) : PG_DLC_CHAPTERS;
  t->fromGame = (order ? PG_SRC_ORDER : 0) | (awards ? PG_SRC_AWARDS : 0);
  for (int i = 0; i < t->n; ++i) {
    strcpy_s(t->url[i], PG_URL_CAP, order ? urls[i] : PG_DLC_BUILTIN[i].url);
    int b = -1;
    for (int k = 0; k < PG_DLC_CHAPTERS && b < 0; ++k)
      if (_stricmp(PG_DLC_BUILTIN[k].url, t->url[i]) == 0) b = k;
    strcpy_s(t->title[i], PG_TITLE_CAP, b >= 0 ? PG_DLC_BUILTIN[b].title : t->url[i]);
    if (awards) {
      for (int k = 0; k < aw->n && k < PG_CHAPTERS_MAX; ++k)
        if (_stricmp(aw->map[k], t->url[i]) == 0) {
          t->awardN[i] = aw->awardN[k];
          const int m = aw->awardN[k] < PG_CH_AWARDS_MAX ? aw->awardN[k] : PG_CH_AWARDS_MAX;
          memcpy(t->awards[i], aw->awards[k], (size_t)(m > 0 ? m : 0));
          break;
        }
    } else if (b >= 0) {
      t->awardN[i] = PG_DLC_BUILTIN[b].n;
      memcpy(t->awards[i], PG_DLC_BUILTIN[b].ids, (size_t)PG_DLC_BUILTIN[b].n);
    }
  }
}
inline void pg_dlc_table_builtin(PgTable* t) { pg_dlc_table_build(t, 0, 0, 0); }

// DLC mode: the map starts DLC03_ or the player is a DukePlayerDLC03
// (playerIsDlc 1; -1 unread counts as not).
inline bool pg_is_dlc_map(const char* map) {
  char key[64];
  pg_map_key(map, key, sizeof key);
  return _strnicmp(key, "DLC03_", 6) == 0;
}
inline bool pg_dlc_mode(const char* map, int playerIsDlc) { return playerIsDlc == 1 || pg_is_dlc_map(map); }

// What Progress.ini says for the DLC (dlc.md sections 2, 3); every field 0 for a key the cache does not have.
struct PgDlcFlags {
  unsigned char award[PG_DLC_AWARDS];              // [EgoAwards] DLC03EGOAWARD_<name>: this playthrough
  unsigned char ever[PG_DLC_AWARDS];               // [ACH_EgoAwards] DLC03EGOAWARD_<name>: profile, achievement 61
  unsigned char done[PG_CHAPTERS_MAX][PG_DIFFS];   // [DLCProgress] GameCompletion_<URL>_<d>, by table row
};
inline int pg_dlc_award_found(const PgDlcFlags& f, int id) { return id >= 0 && id < PG_DLC_AWARDS && f.award[id] ? 1 : 0; }

// UDukeSceneChapterSelectDLC03's count: each table entry whose id is found, over Awards.Num(); ticks by table row.
inline int pg_build_dlc_chapters(const PgTable& t, const PgDlcFlags& f, PgChapter* out) {
  const int n = t.n < PG_CHAPTERS_MAX ? (t.n > 0 ? t.n : 0) : PG_CHAPTERS_MAX;
  for (int i = 0; i < n; ++i) {
    PgChapter& c = out[i];
    memset(&c, 0, sizeof c);
    strcpy_s(c.title, PG_TITLE_CAP, t.title[i]);
    strcpy_s(c.url, PG_URL_CAP, t.url[i]);
    c.egoTotal = t.awardN[i];
    const int k = t.awardN[i] < PG_CH_AWARDS_MAX ? t.awardN[i] : PG_CH_AWARDS_MAX;
    for (int j = 0; j < k; ++j) c.egoFound += pg_dlc_award_found(f, t.awards[i][j]);
    for (int d = 0; d < PG_DIFFS; ++d) c.done[d] = f.done[i][d] != 0;
  }
  return n;
}

// The DLC's run totals: PreGameInit's cap is the base cap (and the preorder bonus) + AutoEgoCapAward + the found
// awards' caps, against DukePlayer's MaxEgoCap (dlc.md section 3; whether the engine holds it at 100 is open 2). The
// profile: the ever-found count achievement 61 waits on, and the chapters finished per difficulty (57..60).
struct PgDlcTotals { PgRunTotals run; int ever, everTotal, doneAt[PG_DIFFS], chapters; };
inline PgDlcTotals pg_dlc_totals(const PgDlcFlags& f, int chapters, int bonusEgo, const float caps[PG_DLC_AWARDS],
                                 float base, float autoEgo, float max, float bonus, int nowOk, float now) {
  PgDlcTotals t;
  memset(&t, 0, sizeof t);
  t.run = pg_run_totals_n(f.award, PG_DLC_AWARDS, bonusEgo, caps, base + autoEgo, max, bonus, nowOk, now);
  t.everTotal = PG_DLC_AWARDS;
  for (int i = 0; i < PG_DLC_AWARDS; ++i) t.ever += f.ever[i] != 0;
  t.chapters = chapters < PG_CHAPTERS_MAX ? (chapters > 0 ? chapters : 0) : PG_CHAPTERS_MAX;
  for (int i = 0; i < t.chapters; ++i)
    for (int d = 0; d < PG_DIFFS; ++d) t.doneAt[d] += f.done[i][d] != 0;
  return t;
}

// The Progress tab's Base / DLC switch: it opens on the campaign in play, then holds the choice.
enum { PG_VIEW_BASE, PG_VIEW_DLC };
inline int pg_view_pick(int view, int opening, int isDlc) {
  return opening ? (isDlc ? PG_VIEW_DLC : PG_VIEW_BASE) : view;
}

// An engine string for the panel's fonts (0x20..0x7E): anything else becomes '?'. n: characters to take at most.
inline void pg_ascii(const wchar_t* s, int n, char* out, int cap) {
  int o = 0;
  for (int i = 0; s && i < n && s[i] && o < cap - 1; ++i) out[o++] = s[i] >= 0x20 && s[i] <= 0x7E ? (char)s[i] : '?';
  out[o] = 0;
}

// The DIFFICULTY column holds the four pips under a caption that is wider than they are; it takes the wider of the two.
inline int pg_diff_col_w(int pipsW, int captionW) { return captionW > pipsW ? captionW : pipsW; }
// Hover tip for the caption: the pips are per-difficulty chapter completion, which was read as a found-everything count.
static const char PG_DIFF_TIP[] =
    "Chapter completed on each difficulty, left to right: Piece of Cake, Let's Rock, Come Get Some, Damn I'm Good.";

// The tab's readouts: "N/M" ("-" for a chapter with no awards), the cap "permanent/max".
inline void pg_ratio(char* out, int cap, int found, int total) {
  if (total <= 0) strcpy_s(out, (size_t)cap, "-");
  else _snprintf_s(out, (size_t)cap, _TRUNCATE, "%d/%d", found, total);
}

// The game's difficulty names, index = difficulty (System\engine.int [LCDHelper] PieceOfCakeDifficultyString ..
// DamnImGoodDifficultyString, "Difficulty: " stripped): the fallback while LCDHelper's default is unread.
static const char* const PG_DIFF_NAMES[PG_DIFFS] = { "Piece of Cake", "Let's Rock", "Come Get Some",
                                                     "Damn, I'm Good" };

// "Difficulty: Let's Rock" -> "Let's Rock": the text after the first ':' (any language's label), blanks trimmed;
// the whole string when there is none. False (and "") when nothing is left.
inline bool pg_diff_label(const char* s, char* out, int cap) {
  out[0] = 0;
  if (!s) return false;
  const char* colon = strchr(s, ':');
  const char* p = colon ? colon + 1 : s;
  while (*p == ' ' || *p == '\t') ++p;
  strncpy_s(out, (size_t)cap, p, _TRUNCATE);
  for (size_t n = strlen(out); n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t'); --n) out[n - 1] = 0;
  return out[0] != 0;
}

// "finished on Piece of Cake and Come Get Some", "not finished yet", or no record (chapter 23).
inline void pg_done_text(const PgChapter& c, int level, const char* const names[PG_DIFFS], char* out, int cap) {
  if (level >= PG_COMPLETION_LEVELS) { strcpy_s(out, (size_t)cap, "the game keeps no completion for it"); return; }
  int which[PG_DIFFS], m = 0;
  for (int d = 0; d < PG_DIFFS; ++d)
    if (c.done[d]) which[m++] = d;
  if (!m) { strcpy_s(out, (size_t)cap, "not finished yet"); return; }
  int len = _snprintf_s(out, (size_t)cap, _TRUNCATE, "finished on %s", names[which[0]]);
  for (int i = 1; i < m && len >= 0; ++i) {
    const int r = _snprintf_s(out + len, (size_t)(cap - len), _TRUNCATE, "%s%s", i == m - 1 ? " and " : ", ",
                              names[which[i]]);
    len = r < 0 ? -1 : len + r;
  }
}

// --- The glue (progress.cpp), game thread only ---------------------------------------------------------
// Once a frame: reads when the Progress tab is open (at once on its opening edge) or the Level stats overlay wants it,
// at most once a second.
void progress_frame(int tabOpen, int overlayOn, unsigned nowMs);
// One read if pg_refresh_due; 1 when it read. Timed in the log.
int progress_refresh(unsigned nowMs);
// The rows of the last read (the built-in table and no flags before one).
const PgChapter* progress_chapters(int* n);
PgTotals progress_totals(void);
// Award id found per the last read (for the ego-boost ESP).
int progress_award_found(int id);
// Helmet letter 0..2 and phone id 1..12 found per the last read (for the collectibles ESP).
int progress_helmet_found(int letter);
int progress_phone_found(int id);
// The current map's chapter index, or -1 (no level, or a map in no chapter); updated on the frame the level or the
// player changes.
int progress_chapter_now(void);
// The current map's name as the chapter was taken from (pg_pick_map_name): "" without a level.
const char* progress_map_now(void);
// DLC mode for the current level (pg_dlc_mode) and its chapter in the DLC table, or -1; updated with the chapter.
int progress_is_dlc(void);
int progress_dlc_chapter_now(void);
// The DLC's rows and totals of the last read (its built-in table and no flags before one), and a DLC award id found.
const PgChapter* progress_dlc_chapters(int* n);
PgDlcTotals progress_dlc_totals(void);
int progress_dlc_award_found(int id);
// 1 once a read of Progress.ini has landed: before it, the helmet and phone counts are the zeroed start, not values.
int progress_flags_read(void);
// One Progress.ini value through the same gate as the tab's reads (config ready, no level loading, a key found
// before): 1 found, 0 missing, -1 not read now. For the Achievements tab's counters and flags.
int progress_profile_int(const wchar_t* sec, const wchar_t* key, int* value);
int progress_profile_bool(const wchar_t* sec, const wchar_t* key, int* value);
// The game's name of difficulty d (LCDHelper's localised label, else PG_DIFF_NAMES), and all four.
const char* progress_diff_name(int d);
const char* const* progress_diff_names(void);
// Null while the config cache reads; else why the tab's numbers are not the game's.
const char* progress_note(void);

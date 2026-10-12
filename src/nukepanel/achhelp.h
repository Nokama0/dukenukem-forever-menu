#pragma once
// The Achievements tab (facts docs/re/achievements.md sections 2, 3, 4):
// which achievements this level can award, their targets by class, Tag, Event or instance name, and the rules the
// tab and the markers share. Pure for the suite; achhelp.cpp reads the engine and Steam, menu.cpp draws.
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "achreset.h"   // AR_COUNT, ar_name, ar_time_from_unix
#include "modelbox.h"   // MbCorners
#include "aircraft.h"   // AC_* kinds: the ships count as the aircraft pass judges them

enum { AH_SCOPE_LEVEL, AH_SCOPE_COLLECTED, AH_SCOPE_COUNTER, AH_GROUPS };
// Progress: a level Counter's NumToCount, the profile counter [Achievements] <API name> in Progress.ini, or the
// helmet, phone and ever-found ego flags progress.cpp and achhelp.cpp read; the DLC's ever-found ego flags and its
// chapters finished on the row's difficulty (dlc.md sections 2, 3).
enum { AH_PROG_NONE, AH_PROG_LEVEL, AH_PROG_PROFILE, AH_PROG_HELMETS, AH_PROG_PHONES, AH_PROG_EGO, AH_PROG_EGO_DLC,
       AH_PROG_DLC_DONE };
// nukepanel.ini [achievements] ach.esp.<id>: auto is on while unearned.
enum { AH_MODE_AUTO, AH_MODE_ON, AH_MODE_OFF };
enum { AH_ROWS_MAX = 48, AH_TARGETS_MAX = 256, AH_LOG_CAP = 40 };

// --- Target kinds ----------------------------------------------------------------------------------------------
enum { AH_MATCH_CLASS, AH_MATCH_TAG, AH_MATCH_EVENT, AH_MATCH_NAME };
// EXACT: the actor's own class only. ALIVE: gone once Health <= 0. GRABBED: gone once HasBeenGrabbed. QUEST: a popcorn
// with bQuestItemHack is no food. SINK: a Counter read for progress, never marked. WATCH: Health, bNoDamage and bHidden
// go in the instrument. INVISIBLE: hidden by nature. AIM: a last-resort aim target while its row is on.
enum { AH_K_EXACT = 1, AH_K_ALIVE = 2, AH_K_GRABBED = 4, AH_K_QUEST = 8, AH_K_SINK = 16, AH_K_WATCH = 32,
       AH_K_INVISIBLE = 64, AH_K_AIM = 128 };
// map: an instance name belongs to one map package (as breakables' stashes); null for any map.
struct AhKind { const char* cls; int match; const char* key; int flags; const char* noun; const char* map; };
enum { AK_POO, AK_CALENDAR, AK_AIRHOCKEY, AK_PINBALL, AK_WHITEBOARD, AK_GOLDPISTOL, AK_CAN, AK_CAN_COUNTER, AK_CATFISH,
       AK_CATFISH_COUNTER, AK_BLIMP, AK_TALENT, AK_ABORT, AK_BARREL, AK_BARREL_GOAL, AK_PINBALL2, AK_SPIDERBOT,
       AK_BS3000, AK_MONSTERTRUCK, AK_FORKLIFT, AK_FOOD, AK_BEER, AK_STEROIDS, AK_HOLODUKE, AK_FIGHTER, AK_GUNSHIP,
       AK_DROPSHIP, AK_MOONBUGGY, AK_COUNT };
static_assert(AK_COUNT <= 31, "a class mask is a positive int (worldscan's rule)");
// Classes from the map packages (achievements.md section 3) and their super chains in the extracted packages
// (tools/ubc.py).
static const AhKind AH_KINDS[AK_COUNT] = {
  { "Biology_Generic_Poop_A", AH_MATCH_CLASS, 0, AH_K_GRABBED, 0 },
  { "Details_Generic_Calendar", AH_MATCH_CLASS, 0, 0, 0 },
  { "AirHockeyTable", AH_MATCH_CLASS, 0, 0, 0 },
  { "dnControl_Pinball", AH_MATCH_CLASS, 0, AH_K_EXACT, 0 },          // not the DLC's Pinball2
  { "dnControl_Whiteboard", AH_MATCH_CLASS, 0, AH_K_EXACT, 0 },       // the DLC03 subclass awards nothing
  { "PistolPickup_Gold", AH_MATCH_CLASS, 0, 0, 0 },
  { "Details_Generic_DrinkCan", AH_MATCH_TAG, "StripClubBeer", 0, "can" },
  { "Counter", AH_MATCH_TAG, "StripClubBeerDrank", AH_K_SINK, 0 },
  { "Biology_Generic_Catfish", AH_MATCH_EVENT, "catfishCounter", AH_K_ALIVE | AH_K_AIM, "catfish" },
  { "Counter", AH_MATCH_TAG, "catfishCounter", AH_K_SINK, 0 },
  { "dnDecoration", AH_MATCH_TAG, "blimp", AH_K_EXACT | AH_K_ALIVE | AH_K_WATCH, 0 },
  { "Workman", AH_MATCH_TAG, "Talent_Backstage", AH_K_ALIVE, 0 },
  { "Switches_Generic_PushSwitch_B", AH_MATCH_TAG, "SwitchAlienAbortReset01", 0, 0 },
  { "Barrels_Generic_Clean01", AH_MATCH_TAG, "CompanionBarrel", AH_K_WATCH, "barrel" },
  { "KBoxSensor", AH_MATCH_NAME, "KBoxSensor___1", AH_K_INVISIBLE, "goal", "Map17B" },
  { "dnControl_Pinball2", AH_MATCH_CLASS, 0, 0, 0 },
  { "DrProton_SpiderBot", AH_MATCH_CLASS, 0, AH_K_ALIVE, 0 },
  // The visible cabinet the player looks at and uses, not its invisible Trigger (dlc.md section 5).
  { "dnDecoration", AH_MATCH_TAG, "BS3000_ArcadeMachine", AH_K_EXACT, "arcade machine" },
  { "Veh_MonsterTruck", AH_MATCH_CLASS, 0, AH_K_EXACT, 0 },
  { "Veh_Forklift", AH_MATCH_CLASS, 0, 0, 0 },
  { "Snacks_Generic", AH_MATCH_CLASS, 0, AH_K_QUEST, "food" },
  { "FortyOunceBeerPickup", AH_MATCH_CLASS, 0, 0, "beer" },
  { "SteroidsPickup", AH_MATCH_CLASS, 0, 0, "steroids" },
  { "HolodukePickup", AH_MATCH_CLASS, 0, 0, 0 },
  { "dnFlybyPlane_Fighter", AH_MATCH_CLASS, 0, AH_K_ALIVE, "fighter" },
  { "dnAlienGunship", AH_MATCH_CLASS, 0, AH_K_ALIVE, "gunship" },
  { "dnAlienDropship", AH_MATCH_CLASS, 0, AH_K_ALIVE, "dropship" },
  { "Veh_MoonBuggy", AH_MATCH_CLASS, 0, 0, 0 },
};

// --- The table -------------------------------------------------------------------------------------------------
// The DLC's 14 campaign maps (dlc.md section 1): its enemy and weapon counters can be earned in any of them.
#define AH_DLC_MAPS "DLC03_Map00 DLC03_Map01 DLC03_Map02 DLC03_Map03 DLC03_Map04 DLC03_Map05 DLC03_Map06 " \
                    "DLC03_Map07 DLC03_Map10 DLC03_Map11 DLC03_Map12 DLC03_Map13 DLC03_Map14 DLC03_Map15"
// maps: package names, space-separated; "*" any level, listed once the scan has seen a target (counters with
// nothing placed stay out). A collected row is listed by its chapter's helmet, phone or ego totals instead. name and
// note are the fallback for Steam's schema (achievements.md section 2); note also says how.
struct AhAch {
  int id, scope;
  const char* maps;
  signed char kinds[3];   // -1 for none
  int prog, sink, total, missable;
  const char* name;
  const char* note;
};
static const AhAch AH_TABLE[] = {
  { 17, AH_SCOPE_LEVEL, "Map00 Map11 Map13 DLC03_Map10 DLC03_Map11", { AK_POO, -1, -1 }, AH_PROG_PROFILE, -1, 1, 0,
    "Turd Burglar", "Pick up a piece of poo (hold Use)." },
  { 20, AH_SCOPE_LEVEL, "Map16 Map17B", { AK_CALENDAR, -1, -1 }, AH_PROG_NONE, -1, 0, 0, "I Need a Date",
    "Look through six months on one calendar." },
  { 23, AH_SCOPE_LEVEL, "Map09", { AK_AIRHOCKEY, -1, -1 }, AH_PROG_NONE, -1, 0, 0, "Air-Duke",
    "Win air hockey 7-0." },
  { 24, AH_SCOPE_LEVEL, "Map02 Map09", { AK_PINBALL, -1, -1 }, AH_PROG_NONE, -1, 0, 0, "Balls of Steel",
    "Score 1,000,000 at pinball." },
  { 28, AH_SCOPE_LEVEL, "Map00 Map09 Map16B Map19 Map20", { AK_WHITEBOARD, -1, -1 }, AH_PROG_NONE, -1, 0, 0,
    "Drawrings", "Draw on a whiteboard with the pen." },
  { 30, AH_SCOPE_LEVEL, "Map02", { AK_GOLDPISTOL, -1, -1 }, AH_PROG_NONE, -1, 0, 1, "Gunslinger",
    "Take the gold pistol and still hold it at the end of every later level." },
  { 35, AH_SCOPE_LEVEL, "Map22", { -1, -1, -1 }, AH_PROG_NONE, -1, 0, 0, "Special Thanks",
    "Watch the end credits through; skipping them does not count." },
  { 38, AH_SCOPE_LEVEL, "Map09", { AK_CAN, -1, -1 }, AH_PROG_LEVEL, AK_CAN_COUNTER, 9, 0, "Party Animal",
    "Drink all nine beers in the strip club." },
  { 39, AH_SCOPE_LEVEL, "Map21", { AK_CATFISH, -1, -1 }, AH_PROG_LEVEL, AK_CATFISH_COUNTER, 15, 0, "Pescaphobe",
    "Kill all 15 catfish." },
  { 40, AH_SCOPE_LEVEL, "Map00", { AK_BLIMP, -1, -1 }, AH_PROG_NONE, -1, 0, 1, "Sunday, Black Sunday",
    "Destroy the blimp with the Devastator before the Cycloid falls." },
  { 41, AH_SCOPE_LEVEL, "Map01", { AK_TALENT, -1, -1 }, AH_PROG_NONE, -1, 0, 1, "Nobody Likes a Whiner",
    "Punch out the whining talent backstage before the studio door closes." },
  { 42, AH_SCOPE_LEVEL, "Map09", { AK_ABORT, -1, -1 }, AH_PROG_NONE, -1, 0, 0, "Lots of Whacking",
    "Score 85 or more at the alien abortion machine." },
  { 43, AH_SCOPE_LEVEL, "Map17B", { AK_BARREL, AK_BARREL_GOAL, -1 }, AH_PROG_NONE, -1, 0, 1, "Companion Barrel",
    "Carry the tagged barrel into the box by the exit hallway." },
  { 44, AH_SCOPE_LEVEL, "Map00", { -1, -1, -1 }, AH_PROG_NONE, -1, 0, 0, "One-Eyed Freak", "Defeat the Cycloid." },
  { 45, AH_SCOPE_LEVEL, "Map05", { -1, -1, -1 }, AH_PROG_NONE, -1, 0, 0, "Pit Champion", "Defeat the Battlelord." },
  { 46, AH_SCOPE_LEVEL, "Map08", { -1, -1, -1 }, AH_PROG_NONE, -1, 0, 0, "Not Bad for a Human",
    "Defeat the Alien Queen." },
  { 47, AH_SCOPE_LEVEL, "Map15", { -1, -1, -1 }, AH_PROG_NONE, -1, 0, 0, "A Good Dam Fight",
    "Defeat the Battlelord on the dam." },
  { 48, AH_SCOPE_LEVEL, "Map18", { -1, -1, -1 }, AH_PROG_NONE, -1, 0, 0, "Octacide", "Defeat the Octaking." },
  { 49, AH_SCOPE_LEVEL, "Map21", { -1, -1, -1 }, AH_PROG_NONE, -1, 0, 0, "Beating the One-Eyed Worm",
    "Defeat the Energy Leech." },
  { 54, AH_SCOPE_LEVEL, "DLC03_Map03", { AK_PINBALL2, -1, -1 }, AH_PROG_NONE, -1, 0, 0, "Scientits",
    "Score 1,000,000 at the science pinball." },
  { 56, AH_SCOPE_LEVEL, "DLC03_Map11", { AK_SPIDERBOT, -1, -1 }, AH_PROG_NONE, -1, 0, 0, "Heart to Heart",
    "Finish Doctor Proton." },
  { 62, AH_SCOPE_LEVEL, "DLC03_Map03", { AK_BS3000, -1, -1 }, AH_PROG_NONE, -1, 0, 0, "Hedonist, not Misogynist",
    "Look at and use the BS3000 arcade machine." },
  { 29, AH_SCOPE_COLLECTED, 0, { -1, -1, -1 }, AH_PROG_EGO, -1, 35, 0, "I Am All That Is Man",
    "Find every ego award; the Ego boosts row (Visuals) marks them." },
  { 36, AH_SCOPE_COLLECTED, 0, { -1, -1, -1 }, AH_PROG_HELMETS, -1, 3, 0, "Bucket Head",
    "Find the three hidden helmets; the Helmets row (Visuals) marks them." },
  { 37, AH_SCOPE_COLLECTED, 0, { -1, -1, -1 }, AH_PROG_PHONES, -1, 12, 0, "Call Waiting",
    "Hear the twelve phone messages; the Phone messages row (Visuals) marks them." },
  { 61, AH_SCOPE_COLLECTED, 0, { -1, -1, -1 }, AH_PROG_EGO_DLC, -1, 19, 0, "I Am All That Is Man - Again!",
    "Find every DLC ego award; the Ego boosts row (Visuals) marks them." },
  { 57, AH_SCOPE_COLLECTED, 0, { -1, -1, -1 }, AH_PROG_DLC_DONE, -1, 14, 0, "Another Piece of Cake",
    "Finish every DLC chapter on Piece of Cake or harder." },
  { 58, AH_SCOPE_COLLECTED, 0, { -1, -1, -1 }, AH_PROG_DLC_DONE, -1, 14, 0, "Let's Rock Out",
    "Finish every DLC chapter on Let's Rock or harder." },
  { 59, AH_SCOPE_COLLECTED, 0, { -1, -1, -1 }, AH_PROG_DLC_DONE, -1, 14, 0, "Come Get a Little More",
    "Finish every DLC chapter on Come Get Some or harder." },
  { 60, AH_SCOPE_COLLECTED, 0, { -1, -1, -1 }, AH_PROG_DLC_DONE, -1, 14, 0, "Damn, I'm REALLY Good",
    "Finish every DLC chapter on Damn, I'm Good." },
  { 7, AH_SCOPE_COUNTER, "Map12 Map12B Map13 Map14 Map14B Map15", { AK_MONSTERTRUCK, -1, -1 }, AH_PROG_PROFILE, -1,
    15, 0, "Road Rage", "Run over 15 enemies with the monster truck." },
  { 8, AH_SCOPE_COUNTER, "Map17 Map17B", { AK_FORKLIFT, -1, -1 }, AH_PROG_PROFILE, -1, 6, 0, "Fork the Pork",
    "Kill 6 enemies with the forklift." },
  { 12, AH_SCOPE_COUNTER, "Map01 Map08 Map09 Map19", { AK_FOOD, -1, -1 }, AH_PROG_PROFILE, -1, 10, 0, "Noms",
    "Eat 10 food items." },
  { 14, AH_SCOPE_COUNTER, "*", { AK_BEER, AK_STEROIDS, -1 }, AH_PROG_NONE, -1, 0, 0, "Substance Abuser",
    "Be drunk and on steroids at the same time." },
  { 19, AH_SCOPE_COUNTER, "*", { AK_FIGHTER, -1, -1 }, AH_PROG_PROFILE, -1, 20, 0, "Baron von Nukem",
    "Shoot down 20 alien fighters." },
  { 22, AH_SCOPE_COUNTER, "*", { AK_GUNSHIP, AK_DROPSHIP, -1 }, AH_PROG_PROFILE, -1, 5, 0, "Big Guns, Big Ships",
    "Destroy 5 gunships or dropships that are fighting you." },
  { 25, AH_SCOPE_COUNTER, "*", { AK_BEER, -1, -1 }, AH_PROG_NONE, -1, 0, 0, "Flagon of Chuckles", "Drink a beer." },
  { 26, AH_SCOPE_COUNTER, "*", { AK_STEROIDS, -1, -1 }, AH_PROG_NONE, -1, 0, 0, "Juiced", "Take steroids." },
  { 27, AH_SCOPE_COUNTER, "*", { AK_HOLODUKE, -1, -1 }, AH_PROG_NONE, -1, 0, 0, "He's Got a Hologram!",
    "Use the Holoduke." },
  { 53, AH_SCOPE_COUNTER, "DLC03_Map15", { AK_MOONBUGGY, -1, -1 }, AH_PROG_PROFILE, -1, 30, 0, "Bloody Red Rover",
    "Run over 30 enemies with the moon buggy." },
  // Enemies and a weapon, nothing placed to mark (dlc.md section 5).
  { 50, AH_SCOPE_COUNTER, AH_DLC_MAPS, { -1, -1, -1 }, AH_PROG_PROFILE, -1, 10, 0, "Bubble Buster",
    "Kill 10 expanded enemies with melee (the metal robots do not count)." },
  { 51, AH_SCOPE_COUNTER, AH_DLC_MAPS, { -1, -1, -1 }, AH_PROG_NONE, -1, 0, 0, "Threesome",
    "Kill three enemies within three seconds with the Impregnader." },
  { 52, AH_SCOPE_COUNTER, AH_DLC_MAPS, { -1, -1, -1 }, AH_PROG_PROFILE, -1, 20, 0, "Passive Aggressive",
    "Let enemies kill 20 of another faction without Duke landing the blow." },
};
enum { AH_TABLE_N = (int)(sizeof AH_TABLE / sizeof AH_TABLE[0]) };
// A row's total: 57..60 count the DLC table's rows (HasFullDLC03Completion walks CampaignList), the rest their own.
inline int ah_total(const AhAch& a, int dlcChapters) {
  return a.prog == AH_PROG_DLC_DONE && dlcChapters > 0 ? dlcChapters : a.total;
}
static_assert(AH_TABLE_N <= AH_ROWS_MAX, "every row fits the tab");

inline const AhAch* ah_find(int id) {
  for (int i = 0; i < AH_TABLE_N; ++i)
    if (AH_TABLE[i].id == id) return &AH_TABLE[i];
  return 0;
}
inline int ah_table_index(int id) {
  const AhAch* a = ah_find(id);
  return a ? (int)(a - AH_TABLE) : -1;
}
inline bool ah_has_kind(const AhAch& a, int kind) {
  return kind >= 0 && (a.kinds[0] == kind || a.kinds[1] == kind || a.kinds[2] == kind);
}
// The aircraft pass's kind for a ship target (ac_damageable then decides: Map11C's NeverBreak dummy dropships and
// bNoDamage ships never count), -1 for the rest.
inline int ah_ship_kind(int k) {
  return k == AK_FIGHTER ? AC_FIGHTER : k == AK_GUNSHIP ? AC_GUNSHIP : k == AK_DROPSHIP ? AC_DROPSHIP : -1;
}

// --- The level match -------------------------------------------------------------------------------------------
// The map's name without a path or an extension: a package name, or LevelInfo.MapFileName for a checkpoint's cpNN.
inline void ah_map_key(const char* in, char* out, int cap) {
  if (!out || cap <= 0) return;
  out[0] = 0;
  if (!in) return;
  const char* s = in;
  for (const char* p = in; *p; ++p)
    if (*p == '/' || *p == '\\') s = p + 1;
  int n = 0;
  while (s[n] && s[n] != '.' && n < cap - 1) { out[n] = s[n]; ++n; }
  out[n] = 0;
}
// One of the space-separated names, case-insensitive.
inline bool ah_map_in(const char* list, const char* map) {
  char key[64];
  ah_map_key(map, key, sizeof key);
  if (!list || !key[0]) return false;
  const int kn = (int)strlen(key);
  for (const char* p = list; *p;) {
    while (*p == ' ') ++p;
    int n = 0;
    while (p[n] && p[n] != ' ') ++n;
    if (n == kn && _strnicmp(p, key, (size_t)n) == 0) return true;
    p += n;
  }
  return false;
}
// The native award refuses there (IsMyDigs, achievements.md section 1).
inline bool ah_is_mydigs(const char* map) {
  char key[64];
  ah_map_key(map, key, sizeof key);
  return _stricmp(key, "Map_mydigs") == 0;
}

// A checkpoint loads from a package named cpNN, and LevelInfo.MapFileName can be empty on its first frame: the map
// is not known yet, so it is read again (at most at 1 Hz) and nothing is listed or logged meanwhile.
inline bool ah_map_pending(const char* map) {
  char key[64];
  ah_map_key(map, key, sizeof key);
  return !key[0] || ((key[0] == 'c' || key[0] == 'C') && (key[1] == 'p' || key[1] == 'P') && key[2] >= '0' &&
                     key[2] <= '9');
}
// A pass keeps a kind only where one of its rows (as a target or the Counter it reads) can be listed: this map's
// targets only, and an instance name only in its own map.
inline bool ah_kind_here(int k, const char* map) {
  if (k < 0 || k >= AK_COUNT || !map || !map[0] || ah_is_mydigs(map)) return false;
  if (AH_KINDS[k].map && !ah_map_in(AH_KINDS[k].map, map)) return false;
  for (int i = 0; i < AH_TABLE_N; ++i) {
    const AhAch& a = AH_TABLE[i];
    if (!ah_has_kind(a, k) && a.sink != k) continue;
    if ((a.maps && strcmp(a.maps, "*") == 0) || ah_map_in(a.maps, map)) return true;
  }
  return false;
}

// The level as the tab sees it: its map name and its chapter's helmet, phone and ego-item totals (progress.h); dlc:
// a DLC level, whose ego totals are the DLC's.
struct AhLevel { const char* map; int helmTotal, phoneTotal, egoTotal, dlc; };
// Rows 57..60: the difficulty whose DLC ticks they count, else -1.
inline int ah_dlc_diff(int id) { return id >= 57 && id <= 60 ? id - 57 : -1; }
// seen: per id, the most targets one scan of this level found.
inline bool ah_listed(const AhAch& a, const AhLevel& lv, const int seen[AR_COUNT]) {
  if (!lv.map || ah_is_mydigs(lv.map)) return false;
  if (a.scope == AH_SCOPE_COLLECTED) {
    if (a.prog == AH_PROG_DLC_DONE) return lv.dlc != 0;
    if (a.prog == AH_PROG_EGO_DLC) return lv.dlc && lv.egoTotal > 0;
    if (lv.dlc) return false;   // 29, helmets and phones are the base game's
    return a.prog == AH_PROG_HELMETS ? lv.helmTotal > 0 : a.prog == AH_PROG_PHONES ? lv.phoneTotal > 0
                                                                                   : lv.egoTotal > 0;
  }
  if (a.maps && strcmp(a.maps, "*") == 0) return seen && a.id >= 0 && a.id < AR_COUNT && seen[a.id] > 0;
  return ah_map_in(a.maps, lv.map);
}
// The ids listed for this level, in table order. Returns the count.
inline int ah_list(const AhLevel& lv, const int seen[AR_COUNT], int* ids, int cap) {
  int n = 0;
  for (int i = 0; i < AH_TABLE_N && n < cap; ++i)
    if (ah_listed(AH_TABLE[i], lv, seen)) ids[n++] = AH_TABLE[i].id;
  return n;
}

// --- Matching an actor -----------------------------------------------------------------------------------------
// The kinds whose class is on this super chain (name indices, nearest first; an unresolved 0 never matches). An
// EXACT kind matches the actor's own class only.
inline unsigned ah_class_mask(const int* chain, int n, const int kindCls[AK_COUNT]) {
  unsigned m = 0;
  for (int k = 0; k < AK_COUNT; ++k) {
    if (!kindCls[k]) continue;
    const int depth = (AH_KINDS[k].flags & AH_K_EXACT) ? (n > 0 ? 1 : 0) : n;
    for (int i = 0; chain && i < depth; ++i)
      if (chain[i] == kindCls[k]) { m |= 1u << k; break; }
  }
  return m;
}
// The kinds' class and key name indices, looked up again before every pass: a checkpoint load gives a map's names new
// indices, and an index kept from the first load matched nothing. find: the name's index, 0 when absent. Returns how
// many class names exist.
inline int ah_resolve_names(int (*find)(const char* s, void* ctx), void* ctx, int cls[AK_COUNT], int key[AK_COUNT]) {
  int got = 0;
  for (int k = 0; k < AK_COUNT; ++k) {
    cls[k] = find(AH_KINDS[k].cls, ctx);
    key[k] = AH_KINDS[k].key ? find(AH_KINDS[k].key, ctx) : 0;
    got += cls[k] != 0;
  }
  return got;
}
// The first candidate whose key the instance carries (its Tag, Event or own name), or -1.
inline int ah_pick_kind(unsigned mask, int tag, int event, int inst, const int kindKey[AK_COUNT]) {
  for (int k = 0; k < AK_COUNT; ++k) {
    if (!(mask >> k & 1u)) continue;
    const int m = AH_KINDS[k].match;
    if (m == AH_MATCH_CLASS) return k;
    const int have = m == AH_MATCH_TAG ? tag : m == AH_MATCH_EVENT ? event : inst;
    if (kindKey[k] && have == kindKey[k]) return k;
  }
  return -1;
}
// Marked while it exists: not being destroyed, shown (unless hidden by nature), alive for a kind that dies (once its
// Health reads), not yet grabbed for the poo.
inline bool ah_target_live(int flags, int deleteMe, int hidden, int healthOk, float health, int grabbed) {
  if (deleteMe) return false;
  if (hidden && !(flags & AH_K_INVISIBLE)) return false;
  if ((flags & AH_K_ALIVE) && healthOk && !(health > 0.f)) return false;
  if ((flags & AH_K_GRABBED) && grabbed) return false;
  return true;
}

// --- Switches --------------------------------------------------------------------------------------------------
// Bosses are marked by the Enemies row, helmets, phones and ego by their Visuals rows: no switch here.
inline bool ah_has_switch(const AhAch& a) { return a.scope != AH_SCOPE_COLLECTED && a.kinds[0] >= 0; }
inline bool ah_esp_on(int mode, int earned) { return mode == AH_MODE_ON || (mode == AH_MODE_AUTO && !earned); }
// The pass and the markers follow these switches alone, as the Ego and Helmets rows stand alone:
// the tab shows, or in a level a listed row's switch is on, or an any-level row's would be once listed.
inline bool ah_scan_wanted(int tabOpen, int inLevel, int listedOn, int anyLevelOn) {
  return tabOpen || (inLevel && (listedOn || anyLevelOn));
}
inline bool ah_mode_parse(const char* s, int* out) {
  if (!s) return false;
  if (_stricmp(s, "auto") == 0) { *out = AH_MODE_AUTO; return true; }
  if (_stricmp(s, "on") == 0) { *out = AH_MODE_ON; return true; }
  if (_stricmp(s, "off") == 0) { *out = AH_MODE_OFF; return true; }
  return false;
}
// Earned in play: a row Steam answered locked that now answers earned drops a switch left on
// back to auto, so its markers stop; off stays off. A row first answered earned keeps its mode.
inline int ah_mode_after_steam(int mode, int seenLocked, int nowEarned) {
  return mode == AH_MODE_ON && seenLocked && nowEarned ? AH_MODE_AUTO : mode;
}
inline const char* ah_mode_text(int mode) { return mode == AH_MODE_ON ? "on" : mode == AH_MODE_OFF ? "off" : "auto"; }

// --- Aim targets -----------------------------------------------------------------------------------------------
// The AIM kinds of the rows listed in this map by name (not "*") whose switch resolves on, the markers' own rule:
// Pescaphobe's catfish in Map21. mode, earned: per id (g_cfg.achEsp, Steam).
inline unsigned ah_aim_kinds(const char* map, const int mode[AR_COUNT], const int earned[AR_COUNT]) {
  if (!map || !mode || !earned || ah_is_mydigs(map)) return 0;
  unsigned m = 0;
  for (int i = 0; i < AH_TABLE_N; ++i) {
    const AhAch& a = AH_TABLE[i];
    if (!ah_has_switch(a) || !a.maps || strcmp(a.maps, "*") == 0 || !ah_map_in(a.maps, map)) continue;
    if (a.id < 0 || a.id >= AR_COUNT || !ah_esp_on(mode[a.id], earned[a.id])) continue;
    for (int j = 0; j < 3; ++j) {
      const int k = a.kinds[j];
      if (k >= 0 && k < AK_COUNT && (AH_KINDS[k].flags & AH_K_AIM)) m |= 1u << k;
    }
  }
  return m;
}
// Last resort, after the pawns and the ships: any hostile pawn candidate this frame, hidden or
// not, holds the achievement targets back, so the bot never shoots fish during a fight; a picked ship wins too.
// pawnCands < 0: unknown (no pick ran this frame), refused.
inline bool ah_aim_pass(int pawnCands, int shipPicked) { return pawnCands == 0 && !shipPicked; }
// A target the aim may take: an AIM kind the marker would show, its Health read and above 0 (a dead fish lingers a
// frame before Destroyed), and not bNoDamage (-1 unread passes).
inline bool ah_aim_target_ok(int flags, int deleteMe, int hidden, int healthOk, float health, int noDamage) {
  if (!(flags & AH_K_AIM) || !ah_target_live(flags, deleteMe, hidden, healthOk, health, 0)) return false;
  return healthOk && health > 0.f && noDamage <= 0;   // NaN: no
}
// dnDecoration.TakeDamage (dnGame.u): a non-empty DamageTypesAcceptedOnly needs an entry the held gun's type is a child
// of (ClassIsChildOf); DamageTypesIgnored refuses an equal type. An unknown type passes empty lists only; a list count
// < 0 is unread and refuses.
inline bool ah_aim_type_ok(int acceptedN, int ignoredN, int acceptedMatch, int ignoredMatch, int typeKnown) {
  if (acceptedN < 0 || ignoredN < 0) return false;
  if (!typeKnown) return acceptedN == 0 && ignoredN == 0;
  return (acceptedN == 0 || acceptedMatch) && !ignoredMatch;
}

// --- The rows --------------------------------------------------------------------------------------------------
// Earned rows sort to the bottom of their group; otherwise table order.
struct AhSortKey { int group, earned, order; };
inline bool ah_before(const AhSortKey& a, const AhSortKey& b) {
  if (a.group != b.group) return a.group < b.group;
  if ((a.earned != 0) != (b.earned != 0)) return !a.earned;
  return a.order < b.order;
}
inline void ah_sort(const AhSortKey* k, int n, int* idx) {
  for (int i = 0; i < n; ++i) {
    int j = i;
    while (j > 0 && ah_before(k[i], k[idx[j - 1]])) { idx[j] = idx[j - 1]; --j; }
    idx[j] = i;
  }
}

// "3/9": done of total. A level Counter counts down (NumToCount), the profile flags and counters up. -1 and "" for no
// progress; -1 and "..." while unread (never 0/3 before Progress.ini answers).
inline int ah_progress_text(int prog, int total, int raw, int ok, char* out, int cap) {
  if (cap > 0) out[0] = 0;
  if (prog == AH_PROG_NONE || total <= 0) return -1;
  if (!ok) {
    strncpy_s(out, (size_t)cap, "...", _TRUNCATE);
    return -1;
  }
  int done = prog == AH_PROG_LEVEL ? total - raw : raw;
  done = done < 0 ? 0 : done > total ? total : done;
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "%d/%d", done, total);
  return done;
}
// "Party Animal: can (3/9)", "Turd Burglar".
inline void ah_marker_label(const char* name, const char* noun, const char* prog, char* out, int cap) {
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "%s%s%s%s%s%s", name ? name : "", noun ? ": " : "", noun ? noun : "",
              prog && prog[0] ? " (" : "", prog && prog[0] ? prog : "", prog && prog[0] ? ")" : "");
}
// "Earned 2011-06-14" in local time; "Earned" when Steam keeps no time.
inline void ah_earned_text(uint32_t unlock, int offMin, char* out, int cap) {
  if (!unlock) { strncpy_s(out, (size_t)cap, "Earned", _TRUNCATE); return; }
  const ArTime t = ar_time_from_unix((int64_t)unlock, offMin);
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "Earned %04d-%02d-%02d", t.y, t.mo, t.d);
}
inline void ah_count_text(int earned, int listed, char* out, int cap) {
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "%d of %d earned", earned, listed);
}
// The instrument's per-level budget of change lines.
inline bool ah_log_take(int* used) {
  if (*used >= AH_LOG_CAP) return false;
  ++*used;
  return true;
}

// --- The glue (achhelp.cpp), game thread ---------------------------------------------------------------------
struct WsClient;
extern const WsClient g_achScan;   // worldscan.cpp's client
// One row of the tab, sorted (group, unearned first). name and desc are Steam's, else the table's.
struct AhRow {
  int id, group, hasSwitch, mode, espOn, missable;
  int answered, earned, offMin;   // Steam: answered false while it has no state for the id
  uint32_t unlock;
  char name[64], desc[160];
  char progress[16];
  const char* note;
};
// Once a frame after progress_frame: the level, its rows, progress and Steam state at 1 Hz. tabOpen: the tab shows.
void achhelp_frame(int inLevel, int tabOpen, unsigned nowMs);
// The rows of this level, sorted; 0 outside a level. Valid until the next achhelp_frame.
int achhelp_rows(const AhRow** out);
// The chapter's title ("" outside one), Duke's digs, and the cheat flag (1 cheated, 0 not, -1 unread).
const char* achhelp_title(void);
bool achhelp_mydigs(void);
// A player in a level whose map name is known; pending while it reads cpNN or nothing (ah_map_pending).
bool achhelp_in_level(void);
bool achhelp_map_pending(void);
int achhelp_cheated(void);
// The row's switch: stores on or off for the id (nukepanel.ini ach.esp.<id>).
void achhelp_set_switch(int id, int on);
// A listed row's switch resolves on: the markers draw (the frame needs a scene).
bool achhelp_any_on(void);
// This frame's markers: gold, labelled; only while a listed row's switch resolves on.
struct AhMarker { float x, y, z, radius, height; char label[64]; MbCorners box; };
int achhelp_markers(const AhMarker** out);
// Progress.ini's helmets, phones and ego flags are read by progress_frame while this is true.
bool achhelp_wants_progress(void);
// The aim's achievement targets this frame (ah_aim_kinds, ah_aim_target_ok), built at the end of achhelp_frame. c, h:
// the model box's centre and half extents (the collision cylinder's when boxOk is 0), off = c - loc so a fresh Location
// moves it. typeOk: ah_aim_type_ok against the held gun's damage type as aim.cpp read it last frame.
enum { AH_AIM_MAX = 32 };
struct AhAimTarget { const void* obj; int kind, id, clsName, boxOk, typeOk; float loc[3], off[3], c[3], h[3], radius,
                     health; };
int achhelp_aim_targets(const AhAimTarget** out);
// This frame's target for obj, or null (not one, or no longer live).
const AhAimTarget* achhelp_aim_find(const void* obj);

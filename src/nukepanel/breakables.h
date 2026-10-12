#pragma once
// Breakables ESP (items.md sections 3 and 5): only breakables that do something when destroyed. Padlocks, the known
// Stash and Path rows (keyed on the Event), and any damageable breakable whose Event is set; a crate only when it holds
// items. Physical-only breakables never show. Pure rules; breakables.cpp reads worldscan's cache, esp.cpp draws.
#include <stdio.h>
#include <string.h>
#include "items.h"   // it_rule_of_chain, it_append

enum { BK_PADLOCK, BK_GASPOD, BK_BUILDING, BK_DECO };

// Nearest class on the super chain wins, so the padlock and gas pod classes match before dnDecoration. Breakable
// glass is left out: it fires ShatteredEvent, and no SP glass opens anything (items.md section 5).
struct BkClassRule { const char* name; int kind; };
static const BkClassRule BK_CLASSES[] = {
  { "Details_Padlock", BK_PADLOCK },
  { "Biology_Generic_GasPod", BK_GASPOD },
  { "dnDestructibleBuilding", BK_BUILDING },
  { "dnDecoration", BK_DECO },
};
enum { BK_CLASS_COUNT = (int)(sizeof BK_CLASSES / sizeof BK_CLASSES[0]) };

// The breakables that lead to something (items.md section 5), by the Event they fire. "Stash" is optional loot, "Path"
// a breakable on the route forward. The key is the Event, not the map and instance name: a checkpoint loads the level
// from a package named cpNN, so a map-name match failed after every reload. Each Event is unique across the SP maps.
struct BkStash { const char* event; const char* opens; int path; };
static const BkStash BK_STASHES[] = {
  { "TreasureChest_Dispatcher", "treasure chest", 0 },                     // Map21 padlock
  { "SecretSteroids_RoundRobin", "steroids", 0 },                          // Map11C: a crate and an ItemCrate
  { "SecretSteroids_Momentum", "steroids", 0 },                            // Map11C crate
  { "TriggerKarmaState_RailgunPickup_Mining_02", "railgun shack", 1 },     // Map14B: the truck drives through it
  { "Counter_BathroomCloset", "bathroom closet", 0 },                      // Map11 boards (probable)
  { "Floor5_Jumppad1_Open_Relay", "jump pad", 1 },                         // Map07B pod: the way up to floor 5
};
enum { BK_STASH_COUNT = (int)(sizeof BK_STASHES / sizeof BK_STASHES[0]) };
inline bool bk_stash_is_path(int stash) { return stash >= 0 && stash < BK_STASH_COUNT && BK_STASHES[stash].path != 0; }

// The row this Event is (the name index of its Event), or -1. NAME_None never matches.
inline int bk_stash_of(int event, const int* events, int n) {
  if (!event) return -1;
  for (int i = 0; i < n; ++i)
    if (events[i] == event) return i;
  return -1;
}

// The breakable families of items.md section 5, by class-name prefix along the chain. A decoration outside them
// (lights, switches, cups, fish) fires Events too but opens nothing: never marked.
static const char* const BK_FAMILIES[] = { "Barrels_", "Cannister_", "Containers_Crates_",
                                           "Details_Generic_DestructibleWoodenBoard", "Vents_Generic_BreakableGrate",
                                           "Biology_Generic_GasPod", "dnDestructibleBuilding", "Details_Padlock" };
inline bool bk_family_ok(const char* cls) {
  if (!cls || !cls[0]) return false;
  for (int i = 0; i < (int)(sizeof BK_FAMILIES / sizeof BK_FAMILIES[0]); ++i)
    if (strncmp(cls, BK_FAMILIES[i], strlen(BK_FAMILIES[i])) == 0) return true;
  return false;
}

inline bool bk_purposeful(int kind, int eventSet, int stash) { return kind == BK_PADLOCK || stash >= 0 || eventSet; }

// A crate (Containers_Crates_) is marked only when it holds items: a pickup spawned on destruction (SpawnedInventory
// or SpawnedSnacks set). An item crate is never marked here: the Items row names it by its contents. Surveyed
// 2026-10-10 (docs/re/items.md section 2): in SP only ItemCrates hold items, so today no crate shows in Breakables.
enum { BK_CR_NONE, BK_CR_CRATE, BK_CR_ITEM };
inline int bk_crate_class(const char* cls) {
  if (!cls) return BK_CR_NONE;
  if (strncmp(cls, "Containers_Crates_ItemCrate", 27) == 0) return BK_CR_ITEM;
  return strncmp(cls, "Containers_Crates_", 18) == 0 ? BK_CR_CRATE : BK_CR_NONE;
}
inline bool bk_crate_shown(int crateClass, int holdsItems) {
  return crateClass == BK_CR_ITEM ? false : crateClass == BK_CR_CRATE ? holdsItems != 0 : true;
}
// A known stash leads to a pickup (items.md section 5), so its crate counts as holding items.
inline int bk_crate_holds(int spawnsPickup, int stash) { return spawnsPickup != 0 || stash >= 0; }
inline void bk_crate_label(char* out, int cap) { if (out && cap > 0) strcpy_s(out, cap, "Crate: items"); }

// A cache entry's rule: the kind and the stash, packed (worldscan keeps one int per entry).
inline int bk_rule_pack(int kind, int stash) { return kind | ((stash + 1) << 8); }
inline int bk_rule_kind(int rule) { return rule & 0xFF; }
inline int bk_rule_stash(int rule) { return (rule >> 8) - 1; }

// Intact and damageable. Decorations: DecoState 0 (Alive), not bNoDamage, HealthPrefab not NeverBreak (0); a gas
// pod not bBroken; a building while it has Parts. -1 is a field that did not resolve: counted as alive.
inline bool bk_alive(int kind, int deleteMe, int hidden, int noDamage, int healthPrefab, int decoState, int broken,
                     int parts) {
  if (deleteMe || hidden) return false;
  if (kind == BK_BUILDING) return parts != 0;
  if (decoState > 0 || noDamage || healthPrefab == 0) return false;
  return kind != BK_GASPOD || !broken;
}

// "Stash: <what it opens>", "Path: <what it opens>", "Padlock", else the class's short name: no "dn" prefix, no "Generic" part, '_' as a space.
inline void bk_label(int kind, int stash, const char* cls, char* out, int cap) {
  if (!out || cap <= 0) return;
  int o = 0;
  out[0] = 0;
  if (stash >= 0 && stash < BK_STASH_COUNT) {
    it_append(out, cap, &o, BK_STASHES[stash].path ? "Path: " : "Stash: ");
    it_append(out, cap, &o, BK_STASHES[stash].opens);
    return;
  }
  if (kind == BK_PADLOCK) { it_append(out, cap, &o, "Padlock"); return; }
  const char* s = cls ? cls : "";
  if (s[0] == 'd' && s[1] == 'n' && s[2] >= 'A' && s[2] <= 'Z') s += 2;
  while (*s && o < cap - 1) {
    const char* e = strchr(s, '_');
    const int len = e ? (int)(e - s) : (int)strlen(s);
    if (len && !(len == 7 && _strnicmp(s, "Generic", 7) == 0)) {
      if (o && o < cap - 1) out[o++] = ' ';
      for (int i = 0; i < len && o < cap - 1; ++i) out[o++] = s[i];
      out[o] = 0;
    }
    if (!e) break;
    s = e + 1;
  }
  if (!out[0]) it_append(out, cap, &o, "Breakable");
}

// --- The glue (breakables.cpp), game thread ---------------------------------------------------------------------
struct BkMarker { float x, y, z, radius, height; int stash; char label[48]; MbCorners box; };
extern const WsClient g_breakScan;   // worldscan.cpp's client
// This frame's live purposeful breakables while the row is on; 0 without a level view. Valid until the next call.
int breakables_markers(const BkMarker** out);

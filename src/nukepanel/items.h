#pragma once
// Items and pickups, Ammo crates and Quest items ESP (evidence
// docs/re/items.md sections 1, 2 and 4). Pure rules for the suite; items.cpp reads the engine from worldscan's
// caches and asks the game's own Inventory.CanPickupClass for Hide what you can't take; esp.cpp draws.
#include <stdio.h>
#include <string.h>
#include "worldscan.h"
#include "modelbox.h"   // MbCorners

enum { IT_ROW_ITEMS, IT_ROW_AMMO, IT_ROW_QUEST, IT_ROWS };
enum { IT_PICKUP, IT_CRATE, IT_AMMOCRATE, IT_AMMOPILE, IT_QUEST };

// By class name along the super chain, nearest first, so the quest pickups match before Engine.Pickup. Item crates
// share the Items row (E2); MP_* pickups are never placed in single player.
struct ItClassRule { const char* name; int kind; };
static const ItClassRule IT_CLASSES[] = {
  { "Containers_Crates_ItemCrate", IT_CRATE },
  { "Containers_Crates_InfiniteAmmo", IT_AMMOCRATE },
  { "Containers_Pile_InfiniteAmmo", IT_AMMOPILE },
  { "BatteryPickup", IT_QUEST },
  { "GasCanPickup", IT_QUEST },
  { "EnergyCellPickup", IT_QUEST },
  // The DLC's own quest pickups, not subclasses of the base ones (dlc.md section 5).
  { "GasCanPickup2", IT_QUEST },
  { "MoonBuggy_PowerCellPickup", IT_QUEST },
  { "Pickup", IT_PICKUP },
};
enum { IT_CLASS_COUNT = (int)(sizeof IT_CLASSES / sizeof IT_CLASSES[0]) };

// The first rule whose class name is on the chain (chain[0] the object's own class), or -1. NAME_None never matches.
inline int it_rule_of_chain(const int* chain, int n, const int* ruleNames, int rules) {
  for (int i = 0; chain && i < n; ++i)
    for (int r = 0; ruleNames && r < rules; ++r)
      if (chain[i] && chain[i] == ruleNames[r]) return r;
  return -1;
}
inline int it_row_of_kind(int kind) {
  switch (kind) {
    case IT_PICKUP: case IT_CRATE: return IT_ROW_ITEMS;
    case IT_AMMOCRATE: case IT_AMMOPILE: return IT_ROW_AMMO;
    case IT_QUEST: return IT_ROW_QUEST;
    default: return -1;
  }
}

// Still there to take: not being destroyed, not hidden (3 SP pickups are placed hidden), not in the player's hands
// (a grabbed quest item), and a crate intact (dnDecoration.DecoState 0; pickups have no DecoState).
inline bool it_present(int kind, int deleteMe, int hidden, int carried, int decoState) {
  if (deleteMe || hidden || carried) return false;
  return kind != IT_CRATE || decoState == 0;
}

// Hide what you can't take: Inventory.CanPickupClass(InvClass, Pawn), the game's own rule (items.md section 1).
// Only a refusal hides; a failed or unmade call shows. A crate hides only when every content class is refused.
enum { IT_ANS_UNKNOWN, IT_ANS_TAKE, IT_ANS_REFUSED };
inline int it_answer(int called, int ret) { return !called ? IT_ANS_UNKNOWN : ret ? IT_ANS_TAKE : IT_ANS_REFUSED; }
inline bool it_hide_pickup(int hideOn, int ans) { return hideOn && ans == IT_ANS_REFUSED; }
inline bool it_hide_crate(int hideOn, const int* ans, int n) {
  if (!hideOn || n <= 0) return false;
  for (int i = 0; i < n; ++i)
    if (ans[i] != IT_ANS_REFUSED) return false;
  return true;
}

// The answers per inventory class for this level, asked again every IT_ASK_MS while Hide is on. A class seen
// while the table is full stays unanswered (shown) and is counted.
enum { IT_ANS_MAX = 32, IT_ASK_MS = 250, IT_CRATE_MAX = 8 };
struct ItAnswers { int n, overflow; const void* cls[IT_ANS_MAX]; int ans[IT_ANS_MAX]; };
inline void it_answers_reset(ItAnswers* t) { memset(t, 0, sizeof *t); }
inline int it_answer_of(ItAnswers* t, const void* cls) {
  if (!cls) return IT_ANS_UNKNOWN;
  for (int i = 0; i < t->n; ++i)
    if (t->cls[i] == cls) return t->ans[i];
  if (t->n >= IT_ANS_MAX) { t->overflow++; return IT_ANS_UNKNOWN; }
  t->cls[t->n] = cls;
  t->ans[t->n++] = IT_ANS_UNKNOWN;
  return IT_ANS_UNKNOWN;
}
inline void it_answer_put(ItAnswers* t, const void* cls, int ans) {
  for (int i = 0; i < t->n; ++i)
    if (t->cls[i] == cls) { t->ans[i] = ans; return; }
}

inline void it_append(char* out, int cap, int* o, const char* s) {
  for (; s && *s && *o < cap - 1; ++s) out[(*o)++] = *s;
  out[*o] = 0;
}

// A name from a class when the game gives none (ammo, quest and jetpack pickups have no ItemName): a table hit,
// else the class name less a trailing "Pickup", '_' as a space and a space before an inner capital.
inline void it_class_label(const char* cls, char* out, int cap) {
  static const char* const kTable[][2] = { { "gascan", "Gas can" }, { "gascan2", "Gas can" },
                                           { "moonbuggy_powercell", "Power cell" } };
  if (!out || cap <= 0) return;
  out[0] = 0;
  if (!cls) return;
  int o = 0;
  for (int i = 0; i < (int)(sizeof kTable / sizeof kTable[0]); ++i)
    if (_stricmp(cls, kTable[i][0]) == 0) { it_append(out, cap, &o, kTable[i][1]); return; }
  int n = (int)strlen(cls);
  if (n > 6 && strcmp(cls + n - 6, "Pickup") == 0) n -= 6;
  for (int i = 0; i < n && o < cap - 1; ++i) {
    const char c = cls[i], p = i ? cls[i - 1] : 0;
    if (i && c >= 'A' && c <= 'Z' && ((p >= 'a' && p <= 'z') || (p >= '0' && p <= '9'))) {
      if (o >= cap - 2) break;
      out[o++] = ' ';
    }
    out[o++] = c == '_' ? ' ' : c;
  }
  out[o] = 0;
}

// "Crate: Pipebomb x2" (E2): each content once in first-seen order, with its count past one; "Crate" unread.
inline void it_crate_label(const char* const* names, int n, char* out, int cap) {
  if (!out || cap <= 0) return;
  int o = 0;
  out[0] = 0;
  it_append(out, cap, &o, n > 0 ? "Crate: " : "Crate");
  int listed = 0;
  for (int i = 0; i < n; ++i) {
    const char* s = names[i] && names[i][0] ? names[i] : "?";
    bool seen = false;
    for (int j = 0; j < i && !seen; ++j) seen = strcmp(names[j] && names[j][0] ? names[j] : "?", s) == 0;
    if (seen) continue;
    int k = 0;
    for (int j = i; j < n; ++j) k += strcmp(names[j] && names[j][0] ? names[j] : "?", s) == 0;
    if (listed++) it_append(out, cap, &o, ", ");
    it_append(out, cap, &o, s);
    if (k > 1) {
      char num[16];
      _snprintf_s(num, sizeof num, _TRUNCATE, " x%d", k);
      it_append(out, cap, &o, num);
    }
  }
}

// --- The glue (items.cpp), game thread ----------------------------------------------------------------------
struct ItMarker { float x, y, z, radius, height; int row; char label[64]; MbCorners box; };
extern const WsClient g_itemsScan;   // worldscan.cpp's client
// After worldscan_frame: asks CanPickupClass for each class on screen, every IT_ASK_MS while Hide is on.
void items_frame(int inLevel);
// This frame's markers of the rows switched on; 0 without a level view. Valid until the next call.
int items_markers(const ItMarker** out);

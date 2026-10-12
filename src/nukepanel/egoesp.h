#pragma once
// Ego boosts ESP (evidence docs/re/progress.md section 2): every actor of an
// award-giver class whose award is not yet found, labelled with the award's name. Pure rules for the suite;
// egoesp.cpp keeps the per-level cache and reads the engine, esp.cpp draws.
#include <string.h>
#include "progress.h"   // PG_AWARDS, PG_AWARD_NAMES
#include "modelbox.h"   // MbCorners

enum { EG_FIXED, EG_BOOSTER, EG_MONITOR };
enum { EG_MAX = 64, EG_IDS = 2 };

// Giver classes (progress.md 2) (subclasses match). Boosters take the instance's PermanentEgoAwardID; monitors give
// only while they can switch screens. Left out: TriggerPlayer (event awards: EG_EVENTS below),
// dnControl_AirHockeyPlayer (spawned for the marked table), CigarAttachment (CigarPickup is the only source).
struct EgClassRule { const char* name; int kind, id0, id1; };
static const EgClassRule EG_CLASSES[] = {
  { "dnControl_Bathroom_UrinationReceptacle", EG_FIXED, 0, -1 },
  { "dnControl_CoinOp_Poker", EG_FIXED, 1, -1 },
  { "dnControl_CoinOp_SlotMachine", EG_FIXED, 2, -1 },
  { "dnControl_WorkoutBench", EG_FIXED, 3, 4 },
  { "AirHockeyTable", EG_FIXED, 5, -1 },
  { "dnControl_CameraViewer_PayBinoculars", EG_FIXED, 6, -1 },
  { "dnControl_Copier", EG_FIXED, 7, -1 },
  { "dnControl_GloryHole", EG_FIXED, 8, -1 },
  { "dnControl_Pinball", EG_FIXED, 9, -1 },
  { "Settings_Workout_HandWeights", EG_FIXED, 10, -1 },
  { "Details_Generic_Calendar", EG_FIXED, 11, -1 },
  { "Details_Generic_PaperAirplane", EG_FIXED, 12, -1 },
  { "Details_Generic_Frisbee", EG_FIXED, 13, -1 },
  { "Details_Generic_PornMag_A", EG_FIXED, 14, -1 },
  { "Details_Generic_PornMag_B", EG_FIXED, 15, -1 },
  { "Details_Generic_PornMag_C", EG_FIXED, 16, -1 },
  { "CigarPickup", EG_FIXED, 17, -1 },
  { "Settings_Workout_PunchKick_EgoBooster", EG_BOOSTER, -1, -1 },
  { "Biology_Generic_WallBoobs", EG_FIXED, 20, -1 },
  { "dnBreakableGlass_Mirror_Usable", EG_FIXED, 21, -1 },
  { "Electronics_Generic_Microwave", EG_FIXED, 22, -1 },
  { "Furniture_Generic_PoolTable_A", EG_FIXED, 23, -1 },
  { "Settings_Workout_Basketball_Hoop", EG_FIXED, 24, -1 },
  { "Electronics_Generic_Computer_Monitor", EG_MONITOR, 25, -1 },
};
enum { EG_CLASS_COUNT = (int)(sizeof EG_CLASSES / sizeof EG_CLASSES[0]) };

// A monitor grants 25 only from SwitchScreens, and only Keyboard.KeyPress reaches it: the keyboard's CPU (the one
// whose FindComponents gave the monitor its CPU) switches all its monitors. Using the monitor itself only toggles
// its power (progress.md section 2). Hidden: nothing on screen to mark.
inline int eg_monitor_wired(int hasCpu, int cpuKeyboards, int hidden) {
  return hasCpu && cpuKeyboards > 0 && !hidden ? 1 : 0;
}

// Book signing, the bosses, the alien abortion, the porno and the stripper's tip (26..34): granted by a map
// TriggerPlayer, never by a giver class.
inline bool eg_event_award(int id) { return id >= 26 && id < PG_AWARDS; }

// The event awards a player starts by using something, marked on that actor: class (super chain) and Tag, read from
// the map packages (progress.md section 7). The bosses (27, 28, 32..34) have nothing to use. skipHidden: the book
// signing swaps a mounted book in the kid's hand for the signing table once the kid hands it over.
struct EgEventRule { int id; const char* cls; const char* tag; int skipHidden; const char* map; };
static const EgEventRule EG_EVENTS[] = {
  { 26, "dnControl_BookSigning", "dnControl_BookSigning_Mounted", 1, "Map01" },
  { 26, "dnControl_BookSigning", "dnControl_BookSigning", 1, "Map01" },
  { 29, "Switches_Generic_PushSwitch_B", "SwitchAlienAbortReset01", 0, "Map09" },
  { 30, "Electronics_Generic_DVDPlayer", "PornoDVDPlayer", 0, "Map09" },
  { 31, "LookInfoActor", "ShakeItBabyLookInfo", 0, "Map09" },
};
enum { EG_EVENT_COUNT = (int)(sizeof EG_EVENTS / sizeof EG_EVENTS[0]) };

// A cache entry's rule past the giver classes is event rule rule - EG_CLASS_COUNT; -1 for a giver class.
inline int eg_rule_event(int rule) {
  const int k = rule - EG_CLASS_COUNT;
  return k >= 0 && k < EG_EVENT_COUNT ? k : -1;
}
// The event rules (nEv of them) whose class is on this super chain (name indices; an unresolved class name, 0, never
// matches).
inline unsigned eg_event_mask_n(const int* chain, int n, const int* evCls, int nEv) {
  unsigned m = 0;
  for (int k = 0; k < nEv; ++k)
    for (int i = 0; evCls[k] && i < n; ++i)
      if (chain[i] == evCls[k]) { m |= 1u << k; break; }
  return m;
}
inline unsigned eg_event_mask(const int* chain, int n, const int evCls[EG_EVENT_COUNT]) {
  return eg_event_mask_n(chain, n, evCls, EG_EVENT_COUNT);
}
// The first rule of the mask whose Tag is this actor's, or -1.
inline int eg_event_pick_n(unsigned mask, int tag, const int* evTag, int nEv) {
  for (int k = 0; tag && k < nEv; ++k)
    if ((mask >> k & 1u) && evTag[k] == tag) return k;
  return -1;
}
inline int eg_event_pick(unsigned mask, int tag, const int evTag[EG_EVENT_COUNT]) {
  return eg_event_pick_n(mask, tag, evTag, EG_EVENT_COUNT);
}
// A TriggerPlayer's award list into the level's listed set, ids below `awards` only.
inline void eg_note_listed_n(unsigned char listed[PG_AWARDS], const unsigned char* list, int n, int awards) {
  for (int i = 0; list && i < n; ++i)
    if (list[i] < awards && list[i] < PG_AWARDS) listed[list[i]] = 1;
}
inline void eg_note_listed(unsigned char listed[PG_AWARDS], const unsigned char* awards, int n) {
  eg_note_listed_n(listed, awards, n, PG_AWARDS);
}
// The award an event actor names: its id while a TriggerPlayer in this level lists it (Map_mydigs has the same Alien
// Abortion switch and grants nothing) and, for a skipHidden rule, while it is not hidden; else -1.
inline int eg_event_id(const EgEventRule& r, const unsigned char listed[PG_AWARDS], int hidden) {
  if (r.id < 0 || r.id >= PG_AWARDS || !listed[r.id] || (r.skipHidden && hidden)) return -1;
  return r.id;
}

// The awards one giver can grant now: the class's fixed ids; a booster's own id while bGivePermanentEgoAward; a
// monitor's only while bCanSwitch and eg_monitor_wired. Event and out-of-range ids are dropped.
inline int eg_giver_ids(const EgClassRule& r, int boosterGives, int boosterId, int canSwitch, int wired,
                        int ids[EG_IDS]) {
  int cand[EG_IDS] = { r.id0, r.id1 };
  if (r.kind == EG_BOOSTER) { cand[0] = boosterGives ? boosterId : -1; cand[1] = -1; }
  if (r.kind == EG_MONITOR && !(canSwitch && wired)) cand[0] = cand[1] = -1;
  int n = 0;
  for (int i = 0; i < EG_IDS; ++i)
    if (cand[i] >= 0 && cand[i] < PG_AWARDS && !eg_event_award(cand[i])) ids[n++] = cand[i];
  return n;
}

// The id a marker names: the first of the giver's awards still unfound, or -1 (all found: no marker).
inline int eg_unfound(const int* ids, int n, const unsigned char found[PG_AWARDS]) {
  for (int i = 0; i < n; ++i)
    if (ids[i] >= 0 && ids[i] < PG_AWARDS && !found[ids[i]]) return ids[i];
  return -1;
}

// "PinballHighScore" -> "Pinball High Score": the enum name with a space before each inner capital. dlc: the DLC's
// enum (EDLC03EgoAwardID).
inline void eg_label_in(int dlc, int id, char* out, int cap) {
  out[0] = 0;
  if (id < 0 || id >= (dlc ? PG_DLC_AWARDS : PG_AWARDS) || cap < 2) return;
  const char* s = dlc ? PG_DLC_AWARD_NAMES[id] : PG_AWARD_NAMES[id];
  int o = 0;
  for (int i = 0; s[i] && o < cap - 1; ++i) {
    if (i && s[i] >= 'A' && s[i] <= 'Z' && s[i - 1] >= 'a' && s[i - 1] <= 'z') {
      if (o >= cap - 2) break;
      out[o++] = ' ';
    }
    out[o++] = s[i];
  }
  out[o] = 0;
}
inline void eg_label(int id, char* out, int cap) { eg_label_in(0, id, out, cap); }

// A decoration's collision cylinder can be 0 (no collision) or a whole room: the marker box keeps 8..128 UU.
inline float eg_clamp_extent(float v) { return !(v >= 8.f) ? 8.f : v > 128.f ? 128.f : v; }

// --- The DLC's givers (docs/re/dlc.md sections 3 and 6) ---------------------------------------------------------
// Its own rule set: the base rules match the DLC subclasses with base ids (a DLC toilet is a urination receptacle),
// and a base giver in a DLC level awards nothing. Not markable: 6 (the boss kill) and Map13's computer (open 4).
static const EgClassRule EG_DLC_CLASSES[] = {
  { "dnControl_Bathroom_Toilet_DLC03", EG_FIXED, 17, -1 },
  { "dnControl_Bathroom_Urinal_DLC03", EG_FIXED, 17, -1 },
  { "dnBreakableGlass_Mirror_Usable_DLC03", EG_FIXED, 18, -1 },
  { "Settings_Workout_HandWeight_DLC03", EG_FIXED, 15, -1 },
  { "Settings_Workout_PunchingBag_DLC03", EG_FIXED, 13, -1 },
  { "dnControl_WorkoutBench_DLC03", EG_FIXED, 14, -1 },
  { "Settings_Workout_Basketball_Hoop_DLC03", EG_FIXED, 16, -1 },
  { "dnControl_Pinball2", EG_FIXED, 9, -1 },
  { "Details_Generic_PornMag_D", EG_FIXED, 11, -1 },
  { "Details_Generic_PornMag_E", EG_FIXED, 10, -1 },
  { "Electronics_Generic_Computer_Monitor_DLC03", EG_MONITOR, 3, -1 },
  { "dnControl_Deadlift", EG_FIXED, 5, -1 },
  { "dnControl_PullupBar", EG_FIXED, 12, -1 },
  { "dnControl_HandSizeFlag", EG_FIXED, 0, -1 },
  { "Details_Generic_DartBoard", EG_FIXED, 4, -1 },
  { "Details_HotSauce", EG_FIXED, 8, -1 },
  { "ArmwrestleClone", EG_FIXED, 2, -1 },
};
enum { EG_DLC_CLASS_COUNT = (int)(sizeof EG_DLC_CLASSES / sizeof EG_DLC_CLASSES[0]) };
// Class and Tag from the map packages (tools/tagprops.py): the Nuke Tank cabinet (not the BS3000 one 190 units away),
// and the film reel and its projector in Map07 (the reel skipped once hidden).
static const EgEventRule EG_DLC_EVENTS[] = {
  { 1, "dnControl_ArcadeMachine", "NukeTankArcadeMachine", 0, "DLC03_Map03" },
  { 7, "Electronics_Generic_Projector", "Projector_Unit", 0, "DLC03_Map07" },
  { 7, "Electronics_Generic_Projector_Reel", "Electronics_Generic_Projector_Reel", 1, "DLC03_Map07" },
};
enum { EG_DLC_EVENT_COUNT = (int)(sizeof EG_DLC_EVENTS / sizeof EG_DLC_EVENTS[0]) };
static_assert(EG_DLC_CLASS_COUNT <= EG_CLASS_COUNT && EG_DLC_EVENT_COUNT <= EG_EVENT_COUNT,
              "egoesp.cpp sizes its name arrays for the base set");

// One rule set: the giver classes, the event rules, the award count, and the TriggerPlayer class whose list (by its
// property name) gates the event markers.
struct EgRuleSet {
  const EgClassRule* cls; int nCls;
  const EgEventRule* ev; int nEv;
  int awards;
  const char* tpClass; const char* tpAwards;
};
inline const EgRuleSet& eg_rules(int dlc) {
  static const EgRuleSet kBase = { EG_CLASSES, EG_CLASS_COUNT, EG_EVENTS, EG_EVENT_COUNT, PG_AWARDS, "TriggerPlayer",
                                   "EgoCapAwards" };
  static const EgRuleSet kDlc = { EG_DLC_CLASSES, EG_DLC_CLASS_COUNT, EG_DLC_EVENTS, EG_DLC_EVENT_COUNT, PG_DLC_AWARDS,
                                  "TriggerPlayerDLC03", "EgoCapAwardsDLC03" };
  return dlc ? kDlc : kBase;
}
// A cache entry's event rule in this set, or -1 for a giver class.
inline int eg_rule_event_in(const EgRuleSet& s, int rule) {
  const int k = rule - s.nCls;
  return k >= 0 && k < s.nEv ? k : -1;
}
// The nearest giver rule along a super chain (name indices, the class itself first), or -1; 0 never matches.
inline int eg_class_rule(const int* chain, int n, const int* ruleNames, int rules) {
  for (int d = 0; chain && d < n; ++d)
    for (int r = 0; r < rules; ++r)
      if (ruleNames[r] && ruleNames[r] == chain[d]) return r;
  return -1;
}

// The per-level cache: one object-table scan per level (rebuilt when world_level_serial moves or after a drop),
// each entry checked every frame against the snapshot slot it was found in.
struct EgEntry { const void* obj; int slot, rule; };
// listed: the awards this level's TriggerPlayers grant (EgoCapAwards), which gates the event markers.
// dlc: the rule set the cache was built with.
struct EgCache { unsigned serial; int built, n, overflow, dlc; EgEntry e[EG_MAX]; unsigned char listed[PG_AWARDS]; };
inline void eg_cache_reset(EgCache* c, unsigned serial) {
  memset(c, 0, sizeof *c);
  c->serial = serial;
  c->built = 1;
}
inline bool eg_cache_due(const EgCache& c, unsigned serial, int inLevel) {
  return inLevel && (!c.built || c.serial != serial);
}
inline bool eg_cache_add(EgCache* c, const void* obj, int slot, int rule) {
  if (c->n >= EG_MAX) { c->overflow++; return false; }
  EgEntry& e = c->e[c->n++];
  e.obj = obj;
  e.slot = slot;
  e.rule = rule;
  return true;
}
// Drops the entries alive() refuses (freed or reused slots), keeping the order; how many went.
inline int eg_cache_prune(EgCache* c, bool (*alive)(void* ctx, int slot, const void* obj), void* ctx) {
  int k = 0;
  for (int i = 0; i < c->n; ++i)
    if (alive(ctx, c->e[i].slot, c->e[i].obj)) c->e[k++] = c->e[i];
  const int dropped = c->n - k;
  c->n = k;
  return dropped;
}

// Markers only while the player can earn awards: GivePermanentEgoCapAward grants nothing while
// DukePlayer.EgoCapAwardsEnabled is clear (autowin.md section 2) or PlayerPawn.bHasCheatedThisLevel is set
// (NoteDiscoveredEgoAward returns early). Unresolved flags: shown; cheated -1 = unread = not cheated.
inline bool eg_awards_open(int resolved, int enabled, int cheated) { return (!resolved || enabled) && cheated != 1; }
// The DLC's giver returns only on bHasCheatedThisLevel: EgoCapAwardsEnabled is no gate there (dlc.md section 3).
inline bool eg_awards_open_in(int dlc, int resolved, int enabled, int cheated) {
  return dlc ? cheated != 1 : eg_awards_open(resolved, enabled, cheated);
}

// --- The glue (egoesp.cpp), game thread, after world_frame ---------------------------------------------
struct EgMarker { float x, y, z, radius, height; int id, dlc; MbCorners box; };   // dlc: id is a DLC award
// This frame's unfound givers (the cache rebuilt on a new level); 0 without a level view. Markers stay valid
// until the next call.
int egoesp_markers(const EgMarker** out);

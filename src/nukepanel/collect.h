#pragma once
// Helmets and Phone messages ESP (progress.md 6): map events DukePlayer catches by name; found state = the profile's
// [Achievements] flags that progress.cpp reads. Phones by the two digits of StartPlayingMessagesEvent, helmets by the
// letter of the ACH_DiscoveredHelmetX event in a Dispatcher's OutEvents, marked on the Trigger that fires it.
#include <stdio.h>
#include <string.h>
#include "progress.h"   // PG_HELMETS, PG_PHONES
#include "modelbox.h"   // MbCorners
#include "worldscan.h"  // WsClient

enum { CL_DESKPHONE, CL_DISPATCHER, CL_TRIGGER, CL_CLASS_COUNT };
// Nearest class on the super chain wins (it_rule_of_chain). Every Trigger subclass is read for the join; only one
// whose Event or LookUseEvents names a helmet dispatcher's Tag is kept.
static const char* const CL_CLASSES[CL_CLASS_COUNT] = { "Electronics_Generic_DeskPhone", "Dispatcher", "Trigger" };

// worldscan rows of this client: the phones, the helmet dispatchers, the triggers joined to them.
enum { CL_ROW_PHONE, CL_ROW_DISP, CL_ROW_TRIG, CL_ROWS };
// A static name array read at most this far (Trigger.LookUseEvents is name[16]); OutEvents is capped the same.
enum { CL_NAMES_MAX = 16 };

// "ACH_PhoneMessage_07" (DukePlayer.InitializeAchievements' event names, two digits), id 1..12; false otherwise.
inline bool cl_phone_event_name(int id, char* out, int cap) {
  if (!out || cap <= 0) return false;
  out[0] = 0;
  if (id < 1 || id > PG_PHONES) return false;
  return _snprintf_s(out, (size_t)cap, _TRUNCATE, "ACH_PhoneMessage_%02d", id) > 0;
}
// "ACH_DiscoveredHelmetA" for letter 0..2.
inline bool cl_helmet_event_name(int letter, char* out, int cap) {
  if (!out || cap <= 0) return false;
  out[0] = 0;
  if (letter < 0 || letter >= PG_HELMETS) return false;
  return _snprintf_s(out, (size_t)cap, _TRUNCATE, "ACH_DiscoveredHelmet%c", 'A' + letter) > 0;
}

// The slot of a name index in a table of resolved names, or -1. NAME_None (0) never matches: an unset event, or a
// name that did not resolve.
inline int cl_slot_of_name(int nameIdx, const int* table, int n) {
  if (!nameIdx || !table) return -1;
  for (int i = 0; i < n; ++i)
    if (table[i] == nameIdx) return i;
  return -1;
}
// A desk phone's id 1..12 from its StartPlayingMessagesEvent, or 0 (a decoy: no event, or another one).
inline int cl_phone_id(int eventIdx, const int phoneNames[PG_PHONES]) {
  return cl_slot_of_name(eventIdx, phoneNames, PG_PHONES) + 1;
}
// The helmet letter a Dispatcher fires: the first of its OutEvents that is ACH_DiscoveredHelmetX, or -1.
inline int cl_letter_of_events(const int* events, int n, const int helmetNames[PG_HELMETS]) {
  for (int i = 0; events && i < n; ++i) {
    const int s = cl_slot_of_name(events[i], helmetNames, PG_HELMETS);
    if (s >= 0) return s;
  }
  return -1;
}

// The helmet dispatchers' Tags this level, learned by the pass that meets them. A Trigger met earlier in the same
// pass joins on the next one (collect.cpp asks worldscan for it).
struct ClTags { int tag[PG_HELMETS]; };
inline void cl_tags_clear(ClTags* t) { memset(t, 0, sizeof *t); }
// 1 when this letter's tag is new or changed.
inline int cl_tags_learn(ClTags* t, int letter, int tag) {
  if (letter < 0 || letter >= PG_HELMETS || !tag || t->tag[letter] == tag) return 0;
  t->tag[letter] = tag;
  return 1;
}
// The letter whose dispatcher this Trigger fires (its Event, or one of its LookUseEvents for a look trigger), or -1.
// *viaLook says the player must also look (helmet C: TT_PlayerProximityAndLook).
inline int cl_trigger_letter(const ClTags& t, int event, const int* look, int nLook, int* viaLook) {
  if (viaLook) *viaLook = 0;
  const int e = cl_slot_of_name(event, t.tag, PG_HELMETS);
  if (e >= 0) return e;
  for (int i = 0; look && i < nLook; ++i) {
    const int s = cl_slot_of_name(look[i], t.tag, PG_HELMETS);
    if (s >= 0) {
      if (viaLook) *viaLook = 1;
      return s;
    }
  }
  return -1;
}
// A trigger entry keeps its letter and whether it needs a look.
inline int cl_trig_rule(int letter, int viaLook) { return letter | (viaLook ? 0x10 : 0); }
inline int cl_trig_letter(int rule) { return rule & 0x0F; }
inline int cl_trig_look(int rule) { return (rule & 0x10) != 0; }

// Markers: only while the profile flag is false (nothing on the actor records a find, progress.md section 6). A
// phone also not while hidden (phone 7 until its Map12B office is shown); a Trigger is hidden by nature, so the
// helmet rule never reads it.
inline bool cl_phone_marker(int found, int hidden, int deleteMe) { return !found && !hidden && !deleteMe; }
inline bool cl_helmet_marker(int found, int deleteMe) { return !found && !deleteMe; }

// One marker per helmet: its first live trigger, else its dispatcher (the trigger join failed or has not run yet).
// trig/disp: each entry's letter, -1 for one that is gone. pick: per letter the trigger index, or -2 - the
// dispatcher index, or -1 for none.
inline void cl_pick_helmets(const int* trig, int nTrig, const int* disp, int nDisp, int pick[PG_HELMETS]) {
  for (int l = 0; l < PG_HELMETS; ++l) pick[l] = -1;
  for (int i = 0; trig && i < nTrig; ++i)
    if (trig[i] >= 0 && trig[i] < PG_HELMETS && pick[trig[i]] == -1) pick[trig[i]] = i;
  for (int i = 0; disp && i < nDisp; ++i)
    if (disp[i] >= 0 && disp[i] < PG_HELMETS && pick[disp[i]] == -1) pick[disp[i]] = -2 - i;
}

// "Helmet A", "Helmet C (look)", "Phone message 7".
inline void cl_helmet_label(int letter, int look, char* out, int cap) {
  if (!out || cap <= 0) return;
  out[0] = 0;
  if (letter < 0 || letter >= PG_HELMETS) return;
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "Helmet %c%s", 'A' + letter, look ? " (look)" : "");
}
inline void cl_phone_label(int id, char* out, int cap) {
  if (!out || cap <= 0) return;
  out[0] = 0;
  if (id < 1 || id > PG_PHONES) return;
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "Phone message %d", id);
}

// --- The glue (collect.cpp), game thread ---------------------------------------------------------------------
enum { CL_MARK_HELMET, CL_MARK_PHONE };
struct ClMarker { float x, y, z, radius, height; int kind; char label[24]; MbCorners box; };
extern const WsClient g_collectScan;   // worldscan.cpp's client
// This frame's unfound helmets and phones for the rows that are on; 0 without a level view. Valid until the next
// call.
int collect_markers(const ClMarker** out);

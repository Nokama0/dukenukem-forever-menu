// Helmets and Phone messages glue (collect.h). worldscan's pass keeps the message phones, the helmet dispatchers and
// the triggers that fire them, once per level (nothing of theirs spawns); each frame reads which are still unfound
// and, for a phone, not hidden, under one access-violation guard. Read only, game thread.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "collect.h"
#include "items.h"   // it_rule_of_chain
#include "world.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "progress.h"
#include "config.h"
#include "log.h"
#include "lvnames.h"

static WsCache s_rows[CL_ROWS];
static const char* const kRowNames[CL_ROWS] = { "Phones", "Helmet dispatchers", "Helmet triggers" };

enum { N_LOCATION, N_RADIUS, N_HEIGHT, N_DELETEME, N_HIDDEN, N_PHONEEVENT, N_OUTEVENTS, N_TAG, N_EVENT, N_LOOKEVENTS,
       N_COUNT };
// Actor and Triggers (Engine.u), Dispatcher (Engine.u), Electronics_Generic_DeskPhone (dnDecorations.u).
static const wchar_t* const kProps[N_COUNT] = { L"Location", L"CollisionRadius", L"CollisionHeight", L"bDeleteMe",
                                                L"bHidden", L"StartPlayingMessagesEvent", L"OutEvents", L"Tag",
                                                L"Event", L"LookUseEvents" };
static int s_n[N_COUNT];
static int s_ruleName[CL_CLASS_COUNT];
static int s_phoneName[PG_PHONES], s_helmName[PG_HELMETS];
static LnKeys s_lnKeys;   // phone Events, helmet Events, then class names
// UArrayProperty::Inner (+0x78, progress.cpp's citation): OutEvents' element size is checked through it.
static const int UAP_INNER = 0x78;

static ClTags s_tags;            // this level's helmet dispatcher Tags
static unsigned s_tagSerial;
static int s_newTags;            // the pass now running learned a tag: one more pass joins earlier triggers

static unsigned s_calls, s_frames, s_helmShown, s_phoneShown, s_hiddenPhones, s_fallback, s_faults;
static unsigned s_logSerial;
static int s_logCounts = -1;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

// --- worldscan's client ------------------------------------------------------------------------------------------
static int cl_wanted(void) { return g_cfg.espHelmets || g_cfg.espPhones; }
static int cl_again(void) { return s_newTags; }

// Before each pass, outside the guard. A new level forgets the old level's tags.
static int cl_resolve(void) {
  const unsigned serial = world_level_serial();
  if (serial != s_tagSerial) {
    cl_tags_clear(&s_tags);
    s_tagSerial = serial;
  }
  s_newTags = 0;
  for (int k = 0; k < N_COUNT; ++k)
    if (!s_n[k]) s_n[k] = uname_find(kProps[k]);
  // The ACH_ Events are a map's names and move when the level reloads (2026-10-11): all looked up again each pass.
  enum { NK = PG_PHONES + PG_HELMETS + CL_CLASS_COUNT };
  static char buf[PG_PHONES + PG_HELMETS][32];
  const char* names[NK];
  for (int i = 0; i < PG_PHONES; ++i) {
    if (!cl_phone_event_name(i + 1, buf[i], sizeof buf[i])) buf[i][0] = 0;
    names[i] = buf[i];
  }
  for (int l = 0; l < PG_HELMETS; ++l) {
    if (!cl_helmet_event_name(l, buf[PG_PHONES + l], sizeof buf[0])) buf[PG_PHONES + l][0] = 0;
    names[PG_PHONES + l] = buf[PG_PHONES + l];
  }
  for (int r = 0; r < CL_CLASS_COUNT; ++r) names[PG_PHONES + PG_HELMETS + r] = CL_CLASSES[r];
  static_assert(NK <= LN_MAX, "lvnames.h keeps LN_MAX keys");
  ln_refresh(&s_lnKeys, names, NK, ln_find_ascii, 0);
  memcpy(s_phoneName, s_lnKeys.cur, sizeof s_phoneName);
  memcpy(s_helmName, s_lnKeys.cur + PG_PHONES, sizeof s_helmName);
  memcpy(s_ruleName, s_lnKeys.cur + PG_PHONES + PG_HELMETS, sizeof s_ruleName);
  ln_report("collect", &s_lnKeys, names, NK);
  int got = 0;
  for (int r = 0; r < CL_CLASS_COUNT; ++r) got += s_ruleName[r] != 0;
  return got;
}

static int cl_class_rule(const void* cls) {
  int chain[32];
  int n = 0;
  for (const void* c = cls; c && n < 32; c = uw_ptr(c, US_SUPER)) chain[n++] = uobj_name(c);
  return it_rule_of_chain(chain, n, s_ruleName, CL_CLASS_COUNT);
}

// A name property's index (a dnName is 4 bytes here: breakables reads Event the same way), 0 when it does not resolve.
static int read_name(const void* o, const void* cls, int k) {
  const UProp p = uprop_get(cls, s_n[k]);
  if (!p.ok || p.elemSize != 4 || !pg_field_in(p.offset, 4)) return 0;
  return uw_int(o, p.offset);
}

// A name array, dynamic (OutEvents) or static (LookUseEvents name[16]), into out; how many.
static int read_names(const void* o, const void* cls, int k, int* out, int cap) {
  const UProp p = uprop_get(cls, s_n[k]);
  if (!p.ok) return 0;
  if (p.type == UW_ARRAY) {
    const void* inner = p.field ? uw_ptr(p.field, UAP_INNER) : 0;
    if (!inner || *(const unsigned short*)((const char*)inner + UP_ELEMSIZE) != 4 || !pg_field_in(p.offset, 8))
      return 0;
    const int* data = (const int*)uw_ptr(o, p.offset);
    int n = uw_int(o, p.offset + 4);
    if (!data || n <= 0) return 0;
    if (n > cap) n = cap;
    memcpy(out, data, (size_t)n * 4);
    return n;
  }
  if (p.elemSize != 4) return 0;
  int n = *(const unsigned short*)((const char*)p.field + UP_ARRAYDIM);
  if (n > cap) n = cap;
  if (n <= 0 || !pg_field_in(p.offset, 4 * n)) return 0;
  memcpy(out, (const char*)o + p.offset, (size_t)n * 4);
  return n;
}

// Under worldscan's guard. A trigger is read only once a helmet tag is known: most levels have none.
static int cl_row_of(const void* o, const void* cls, int* rule) {
  int names[CL_NAMES_MAX];
  switch (*rule) {
    case CL_DESKPHONE: {
      const int id = cl_phone_id(read_name(o, cls, N_PHONEEVENT), s_phoneName);
      if (!id) return -1;   // a decoy phone
      *rule = id;
      return CL_ROW_PHONE;
    }
    case CL_DISPATCHER: {
      const int n = read_names(o, cls, N_OUTEVENTS, names, CL_NAMES_MAX);
      const int letter = cl_letter_of_events(names, n, s_helmName);
      if (letter < 0) return -1;
      s_newTags |= cl_tags_learn(&s_tags, letter, read_name(o, cls, N_TAG));
      *rule = letter;
      return CL_ROW_DISP;
    }
    case CL_TRIGGER: {
      if (!s_tags.tag[0] && !s_tags.tag[1] && !s_tags.tag[2]) return -1;
      int look = 0;
      const int n = read_names(o, cls, N_LOOKEVENTS, names, CL_NAMES_MAX);
      const int letter = cl_trigger_letter(s_tags, read_name(o, cls, N_EVENT), names, n, &look);
      if (letter < 0) return -1;
      *rule = cl_trig_rule(letter, look);
      return CL_ROW_TRIG;
    }
    default:
      return -1;
  }
}

const WsClient g_collectScan = { "collect", CL_ROWS, s_rows, kRowNames, cl_wanted, cl_resolve, cl_class_rule,
                                 cl_row_of, 1, cl_again };

// --- Markers -----------------------------------------------------------------------------------------------------
static ClMarker s_markers[PG_HELMETS + WS_MAX];

static int read_bool(const void* o, const void* cls, int k) {
  const UProp p = uprop_get(cls, s_n[k]);
  return p.ok && p.type == UW_BOOL && pg_field_in(p.offset, 4) && (uw_int(o, p.offset) & (int)p.bitmask) != 0;
}

// Place and box of a live actor; false while it is being destroyed or its Location does not read.
static bool read_place(const void* o, ClMarker* m) {
  const void* cls = uobj_class(o);
  if (read_bool(o, cls, N_DELETEME)) return false;
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

static int collect_body(int helmets, int phones, const int helmFound[PG_HELMETS]) {
  int n = 0;
  if (phones) {
    const WsCache& c = s_rows[CL_ROW_PHONE];
    for (int i = 0; i < c.n; ++i) {
      const int id = c.e[i].rule;
      const int hidden = read_bool(c.e[i].obj, uobj_class(c.e[i].obj), N_HIDDEN);
      s_hiddenPhones += hidden && !progress_phone_found(id);
      if (!cl_phone_marker(progress_phone_found(id), hidden, 0)) continue;
      ClMarker& m = s_markers[n];
      if (!read_place(c.e[i].obj, &m)) continue;
      m.kind = CL_MARK_PHONE;
      cl_phone_label(id, m.label, sizeof m.label);
      n++;
      s_phoneShown++;
    }
  }
  if (helmets) {
    // Each entry's letter while it is live; per letter its first trigger, else its dispatcher.
    const WsCache& t = s_rows[CL_ROW_TRIG];
    const WsCache& d = s_rows[CL_ROW_DISP];
    static ClMarker tm[WS_MAX], dm[WS_MAX];
    int tl[WS_MAX], dl[WS_MAX];
    for (int i = 0; i < t.n; ++i) tl[i] = read_place(t.e[i].obj, &tm[i]) ? cl_trig_letter(t.e[i].rule) : -1;
    for (int i = 0; i < d.n; ++i) dl[i] = read_place(d.e[i].obj, &dm[i]) ? d.e[i].rule : -1;
    int pick[PG_HELMETS];
    cl_pick_helmets(tl, t.n, dl, d.n, pick);
    for (int l = 0; l < PG_HELMETS; ++l) {
      if (pick[l] == -1 || !cl_helmet_marker(helmFound[l], 0)) continue;
      const bool viaTrig = pick[l] >= 0;
      ClMarker& m = s_markers[n++];
      m = viaTrig ? tm[pick[l]] : dm[-2 - pick[l]];
      m.kind = CL_MARK_HELMET;
      cl_helmet_label(l, viaTrig && cl_trig_look(t.e[pick[l]].rule), m.label, sizeof m.label);
      s_helmShown++;
      s_fallback += !viaTrig;
    }
  }
  return n;
}

static int collect(int helmets, int phones) {
  int found[PG_HELMETS];
  for (int l = 0; l < PG_HELMETS; ++l) found[l] = progress_helmet_found(l);
  __try {
    return collect_body(helmets, phones, found);
  } __except (av_only(GetExceptionCode())) {
    s_faults++;
    return 0;
  }
}

// What the level holds, on its first frame and when a later pass changes it (the helmet join's second pass).
static void log_level(void) {
  const unsigned serial = world_level_serial();
  const int counts = s_rows[CL_ROW_PHONE].n | s_rows[CL_ROW_DISP].n << 8 | s_rows[CL_ROW_TRIG].n << 16;
  if (serial == s_logSerial && counts == s_logCounts) return;
  s_logSerial = serial;
  s_logCounts = counts;
  char ids[96];
  int o = 0;
  ids[0] = 0;
  for (int i = 0; i < s_rows[CL_ROW_PHONE].n && o < (int)sizeof ids - 4; ++i)
    o += _snprintf_s(ids + o, sizeof ids - o, _TRUNCATE, " %d", s_rows[CL_ROW_PHONE].e[i].rule);
  char helm[32];
  o = 0;
  helm[0] = 0;
  for (int i = 0; i < s_rows[CL_ROW_DISP].n && o < (int)sizeof helm - 3; ++i)
    o += _snprintf_s(helm + o, sizeof helm - o, _TRUNCATE, " %c", 'A' + s_rows[CL_ROW_DISP].e[i].rule);
  int joined = 0;
  for (int i = 0; i < s_rows[CL_ROW_TRIG].n; ++i) joined |= 1 << cl_trig_letter(s_rows[CL_ROW_TRIG].e[i].rule);
  np_log("collect: level serial %u: phones %d (ids%s), helmet dispatchers %d (%s), triggers %d (joined A%d B%d C%d)",
         serial, s_rows[CL_ROW_PHONE].n, ids[0] ? ids : " none", s_rows[CL_ROW_DISP].n, helm[0] ? helm + 1 : "none",
         s_rows[CL_ROW_TRIG].n, joined & 1, (joined >> 1) & 1, (joined >> 2) & 1);
}

int collect_markers(const ClMarker** out) {
  *out = s_markers;
  int shown = 0;
  if ((g_cfg.espHelmets || g_cfg.espPhones) && world_view().ok && world_player() && worldscan_ready()) {
    modelbox_frame();
    shown = collect(g_cfg.espHelmets, g_cfg.espPhones);
    uname_dlog_flush();
    log_level();
    s_frames++;
  }
  if (++s_calls % 600 == 0 && s_frames) {
    np_log("collect: frames=%u phones=%d dispatchers=%d triggers=%d shown avg helmets %.2f phones %.2f, hidden "
           "unheard phone frames=%u, helmets at their dispatcher=%u, faults=%u", s_frames, s_rows[CL_ROW_PHONE].n,
           s_rows[CL_ROW_DISP].n, s_rows[CL_ROW_TRIG].n, (double)s_helmShown / s_frames,
           (double)s_phoneShown / s_frames, s_hiddenPhones, s_fallback, s_faults);
    s_frames = s_helmShown = s_phoneShown = s_hiddenPhones = s_fallback = s_faults = 0;
  }
  return shown;
}

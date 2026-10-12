// Items glue (items.h). worldscan fills the row caches; each frame reads the surviving entries under one guard, then
// names them and applies Hide what you can't take outside it (names take the engine's lock). The one call is
// Inventory.CanPickupClass (Engine.dll 0x100CFC60), static and side-effect free, via ProcessEvent.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "items.h"
#include "world.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "engine.h"
#include "scall.h"
#include "scallparms.h"
#include "progress.h"   // pg_field_in
#include "espnames.h"   // esp_wide_to_ascii
#include "config.h"
#include "log.h"
#include "lvnames.h"

static WsCache s_rows[IT_ROWS];
static const char* const kRowNames[IT_ROWS] = { "Items and pickups", "Ammo crates", "Quest items" };

enum { N_LOCATION, N_RADIUS, N_HEIGHT, N_DELETEME, N_HIDDEN, N_DECOSTATE, N_SPAWNED, N_ITEMNAME, N_INVTYPE,
       N_CARRIED, N_INVHEAD, N_COUNT };
// Actor and Pawn (Engine.u), dnDecoration (dnGame.u: DecoState, SpawnedInventory), Pickup (ItemName, InventoryType).
static const wchar_t* const kProps[N_COUNT] = { L"Location", L"CollisionRadius", L"CollisionHeight", L"bDeleteMe",
                                                L"bHidden", L"DecoState", L"SpawnedInventory", L"ItemName",
                                                L"InventoryType", L"CarriedActor", L"InventoryListHead" };
static int s_n[N_COUNT];
static int s_ruleName[IT_CLASS_COUNT];
static LnKeys s_lnKeys;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

// --- worldscan's client ------------------------------------------------------------------------------------------
static int it_wanted(void) { return g_cfg.espItems || g_cfg.espAmmoCrates || g_cfg.espQuest; }

static int it_resolve(void) {
  for (int k = 0; k < N_COUNT; ++k)
    if (!s_n[k]) s_n[k] = uname_find(kProps[k]);
  // Pickup classes can come from a map's package, whose names move when the level reloads (2026-10-11): looked up
  // again each pass, absent when not found.
  const char* names[IT_CLASS_COUNT];
  for (int r = 0; r < IT_CLASS_COUNT; ++r) names[r] = IT_CLASSES[r].name;
  static_assert(IT_CLASS_COUNT <= LN_MAX, "lvnames.h keeps LN_MAX keys");
  ln_refresh(&s_lnKeys, names, IT_CLASS_COUNT, ln_find_ascii, 0);
  memcpy(s_ruleName, s_lnKeys.cur, sizeof s_ruleName);
  ln_report("items", &s_lnKeys, names, IT_CLASS_COUNT);
  int got = 0;
  for (int r = 0; r < IT_CLASS_COUNT; ++r) got += s_ruleName[r] != 0;
  return got;
}

static int it_class_rule(const void* cls) {
  int chain[32];
  int n = 0;
  for (const void* c = cls; c && n < 32; c = uw_ptr(c, US_SUPER)) chain[n++] = uobj_name(c);
  return it_rule_of_chain(chain, n, s_ruleName, IT_CLASS_COUNT);
}

static int it_row_of(const void*, const void*, int* rule) {
  return *rule >= 0 && *rule < IT_CLASS_COUNT ? it_row_of_kind(IT_CLASSES[*rule].kind) : -1;
}

const WsClient g_itemsScan = { "items", IT_ROWS, s_rows, kRowNames, it_wanted, it_resolve, it_class_rule, it_row_of };

// --- Per level: class names and CanPickupClass answers -------------------------------------------------------------
// A pickup class's label and inventory class, from its class default (ItemName, InventoryType). Direct-mapped by
// class pointer, checked by class name, cleared per level.
struct ClassInfo { const void* cls; int clsName; const void* inv; char label[48]; };
static ClassInfo s_info[64];
static ItAnswers s_ans;
static unsigned s_serial;
static unsigned s_askMs;
static int s_asked;
static int s_answerLogs;
static const char* s_askWhy;   // why CanPickupClass cannot be called (logged once); items then show

// Counted per 600 frames: frames with markers read, markers per row, entries gone or hidden by a refusal, and the
// asks (answered take or refused, or not answered).
static unsigned s_frames, s_calls, s_shown[IT_ROWS], s_gone, s_hidden, s_asks, s_take, s_refused, s_unanswered;
static unsigned s_faults;

static void new_level(unsigned serial) {
  s_serial = serial;
  memset(s_info, 0, sizeof s_info);
  it_answers_reset(&s_ans);
  s_asked = 0;
  s_answerLogs = 0;
}

static bool read_bool(const void* o, int k) {
  const UProp p = uprop_get(uobj_class(o), s_n[k]);
  return p.ok && p.type == UW_BOOL && pg_field_in(p.offset, 4) && (uw_int(o, p.offset) & (int)p.bitmask) != 0;
}

// The class default's ItemName (a dnString: data, count with the terminator) and InventoryType; false on a fault.
static bool read_class(const void* cls, wchar_t* item, int cap, const void** inv, int* invName) {
  item[0] = 0;
  *inv = 0;
  *invName = 0;
  __try {
    const void* cdo = engine_get_default_object((void*)cls);
    if (!cdo) return true;
    const UProp in = uprop_get(cls, s_n[N_ITEMNAME]);
    if (in.ok && in.type == UW_STR && pg_field_in(in.offset, 12)) {
      const wchar_t* d = (const wchar_t*)uw_ptr(cdo, in.offset);
      const int num = uw_int(cdo, in.offset + 4);
      if (d && num > 1 && num <= 256) {
        int i = 0;
        for (; i < num - 1 && i < cap - 1 && d[i]; ++i) item[i] = d[i];
        item[i] = 0;
      }
    }
    const UProp it = uprop_get(cls, s_n[N_INVTYPE]);
    if (it.ok && (it.type == UW_CLASS || it.type == UW_OBJECT) && pg_field_in(it.offset, 4)) {
      const void* c = uw_ptr(cdo, it.offset);
      if (c && uobj_valid(c)) {
        *inv = c;
        *invName = uobj_name(c);
      }
    }
    return true;
  } __except (av_only(GetExceptionCode())) {
    s_faults++;
    return false;
  }
}

// Outside every guard (uname_str takes NamesCS). Null when the class could not be read.
static const ClassInfo* class_info(const void* cls, int clsName) {
  ClassInfo& ci = s_info[((uintptr_t)cls >> 4) % 64];
  if (ci.cls == cls && ci.clsName == clsName && cls) return &ci;
  wchar_t item[48];
  const void* inv;
  int invName;
  if (!cls || !read_class(cls, item, 48, &inv, &invName)) return 0;
  ci.cls = cls;
  ci.clsName = clsName;
  ci.inv = inv;
  ci.label[0] = 0;
  if (!esp_wide_to_ascii(item, ci.label, sizeof ci.label)) {
    // No ItemName (ammo, quest and jetpack pickups): the inventory class's name, else the pickup class's.
    char a[64];
    esp_wide_to_ascii(uname_str(invName ? invName : clsName), a, sizeof a);
    it_class_label(a, ci.label, sizeof ci.label);
  }
  if (!ci.label[0]) strcpy_s(ci.label, "Pickup");
  return &ci;
}

// --- Inventory.CanPickupClass(InvClass, Other) -> bool ---------------------------------------------------------
static void refuse_ask(const char* why) {
  if (s_askWhy == why) return;
  s_askWhy = why;
  np_log("items: Hide what you can't take shows everything: %s", why);
}

// The player's first inventory item: the call needs an Inventory to run on (the function is static and reads no
// `this`). Null when it holds none.
static void* inventory_head(const void* player) {
  __try {
    const UProp p = uprop_get(uobj_class(player), s_n[N_INVHEAD]);
    if (!p.ok || p.type != UW_OBJECT || !pg_field_in(p.offset, 4)) return 0;
    void* inv = (void*)uw_ptr(player, p.offset);
    return inv && uobj_valid(inv) ? inv : 0;
  } __except (av_only(GetExceptionCode())) {
    s_faults++;
    return 0;
  }
}

static bool class_live(const void* cls, int* name) {
  __try {
    if (!uobj_valid(cls)) return false;
    *name = uobj_name(cls);
    return true;
  } __except (av_only(GetExceptionCode())) {
    s_faults++;
    return false;
  }
}

static int ask(void* ctx, const void* inv, void* pawn) {
  if (!ctx) {
    refuse_ask("the player holds no inventory item to call CanPickupClass on");
    return IT_ANS_UNKNOWN;
  }
  const ScallFn f = scall_find(ctx, L"CanPickupClass");
  if (!f.ok) {
    refuse_ask("Inventory.CanPickupClass did not resolve; see nukepanel.log");
    return IT_ANS_UNKNOWN;
  }
  static const void* s_fn;
  static ScallParm s_pc, s_po, s_pr;
  if (f.fn != s_fn) {
    ScallParm pc, po, pr;
    if (!scall_parm(f, L"InvClass", L"ClassProperty", &pc) || pc.size != 4 ||
        !scall_parm(f, L"Other", L"ObjectProperty", &po) || po.size != 4 ||
        !scall_parm(f, L"ReturnValue", L"BoolProperty", &pr) || pr.size != 4) {
      refuse_ask("CanPickupClass's InvClass, Other or ReturnValue did not resolve; see nukepanel.log");
      return IT_ANS_UNKNOWN;
    }
    s_pc = pc;
    s_po = po;
    s_pr = pr;
    s_fn = f.fn;
    np_log("items: CanPickupClass ParmsSize=%u InvClass +%u Other +%u ReturnValue +%u", (unsigned)f.parmsSize,
           pc.offset, po.offset, pr.offset);
  }
  unsigned char parms[64];
  memset(parms, 0, sizeof parms);
  if (f.parmsSize > sizeof parms) {
    refuse_ask("CanPickupClass's parameters are larger than expected");
    return IT_ANS_UNKNOWN;
  }
  if (!scallparms_put(parms, f.parmsSize, s_pc.offset, &inv, 4) ||
      !scallparms_put(parms, f.parmsSize, s_po.offset, &pawn, 4) || !scall_call(ctx, f, parms, sizeof parms))
    return IT_ANS_UNKNOWN;
  int r;
  memcpy(&r, parms + s_pr.offset, 4);
  s_askWhy = 0;
  return it_answer(1, r != 0);
}

void items_frame(int inLevel) {
  const unsigned serial = world_level_serial();
  if (serial != s_serial) new_level(serial);
  if (!g_cfg.espItems || !g_cfg.espHideTaken) {   // switched on again: every class starts unanswered (shown)
    if (s_ans.n) it_answers_reset(&s_ans);
    s_asked = 0;
    return;
  }
  void* player = world_player();
  const unsigned now = GetTickCount();
  if (!inLevel || !player || !ws_period_due(s_askMs, s_asked, now, IT_ASK_MS)) return;
  s_askMs = now;
  s_asked = 1;
  if (!s_n[N_INVHEAD]) s_n[N_INVHEAD] = uname_find(kProps[N_INVHEAD]);
  void* ctx = inventory_head(player);
  for (int i = 0; i < s_ans.n; ++i) {
    int name = 0;
    const int was = s_ans.ans[i];
    const int a = class_live(s_ans.cls[i], &name) ? ask(ctx, s_ans.cls[i], player) : IT_ANS_UNKNOWN;
    s_ans.ans[i] = a;
    s_asks++;
    if (a == IT_ANS_TAKE) s_take++;
    else if (a == IT_ANS_REFUSED) s_refused++;
    else s_unanswered++;
    if (a != was && s_answerLogs < 32) {   // each change, so a gate shows when ammo filled or emptied
      s_answerLogs++;
      np_log("items: CanPickupClass(%ls) -> %s", name ? uname_str(name) : L"?",
             a == IT_ANS_TAKE ? "take" : a == IT_ANS_REFUSED ? "refused: hidden" : "not answered: shown");
    }
  }
  uname_dlog_flush();
}

// --- Markers -----------------------------------------------------------------------------------------------------
// One entry as read under the guard: what it is, where, and a crate's contents.
struct Raw {
  const void* cls;
  int clsName, kind, row, nContent;
  float x, y, z, radius, height;
  MbCorners box;
  const void* content[IT_CRATE_MAX];
  int contentName[IT_CRATE_MAX];
};
static Raw s_raw[IT_ROWS * WS_MAX];
static ItMarker s_markers[IT_ROWS * WS_MAX];

static bool read_entry(const WsEntry& e, int row, const void* carried, Raw* r) {
  const void* o = e.obj;
  const void* cls = uobj_class(o);
  r->cls = cls;
  r->clsName = uobj_name(cls);
  r->kind = IT_CLASSES[e.rule].kind;
  r->row = row;
  r->nContent = 0;
  int deco = 0;
  if (r->kind == IT_CRATE) {
    const UProp ds = uprop_get(cls, s_n[N_DECOSTATE]);
    if (ds.ok && ds.type == UW_BYTE && pg_field_in(ds.offset, 1)) deco = *((const unsigned char*)o + ds.offset);
  }
  if (!it_present(r->kind, read_bool(o, N_DELETEME), read_bool(o, N_HIDDEN), o == carried, deco)) return false;
  const UProp loc = uprop_get(cls, s_n[N_LOCATION]);
  if (!loc.ok || loc.type != UW_STRUCT || loc.elemSize < 12 || !pg_field_in(loc.offset, 12)) return false;
  float xyz[3];
  memcpy(xyz, (const char*)o + loc.offset, sizeof xyz);
  r->x = xyz[0];
  r->y = xyz[1];
  r->z = xyz[2];
  r->radius = r->height = 0.f;
  const UProp rad = uprop_get(cls, s_n[N_RADIUS]), hgt = uprop_get(cls, s_n[N_HEIGHT]);
  if (rad.ok && rad.type == UW_FLOAT && pg_field_in(rad.offset, 4)) memcpy(&r->radius, (const char*)o + rad.offset, 4);
  if (hgt.ok && hgt.type == UW_FLOAT && pg_field_in(hgt.offset, 4)) memcpy(&r->height, (const char*)o + hgt.offset, 4);
  modelbox_read(o, cls, &r->box);
  if (r->kind == IT_CRATE) {   // the instance's SpawnedInventory: pickup classes spawned when it breaks
    const UProp sp = uprop_get(cls, s_n[N_SPAWNED]);
    if (sp.ok && sp.type == UW_ARRAY && pg_field_in(sp.offset, 8)) {
      const void* const* data = (const void* const*)uw_ptr(o, sp.offset);
      const int num = uw_int(o, sp.offset + 4);
      for (int k = 0; data && num > 0 && num <= 64 && k < num && r->nContent < IT_CRATE_MAX; ++k) {
        const void* c = data[k];
        if (!c || !uobj_valid(c)) continue;
        r->content[r->nContent] = c;
        r->contentName[r->nContent++] = uobj_name(c);
      }
    }
  }
  return r->x == r->x && r->y == r->y && r->z == r->z;
}

static int collect(const void* player) {
  int n = 0;
  __try {
    const void* carried = 0;
    const UProp ca = uprop_get(uobj_class(player), s_n[N_CARRIED]);
    if (ca.ok && ca.type == UW_OBJECT && pg_field_in(ca.offset, 4)) carried = uw_ptr(player, ca.offset);
    const int on[IT_ROWS] = { g_cfg.espItems, g_cfg.espAmmoCrates, g_cfg.espQuest };
    for (int row = 0; row < IT_ROWS; ++row) {
      if (!on[row]) continue;
      for (int i = 0; i < s_rows[row].n; ++i) {
        if (read_entry(s_rows[row].e[i], row, carried, &s_raw[n])) n++;
        else s_gone++;
      }
    }
  } __except (av_only(GetExceptionCode())) {
    s_faults++;
    return 0;
  }
  return n;
}

// The label and the hide rule, outside the guard.
static bool label_of(const Raw& r, int hideOn, char* out, int cap) {
  switch (r.kind) {
    case IT_AMMOCRATE: strcpy_s(out, cap, "Ammo crate"); return true;
    case IT_AMMOPILE: strcpy_s(out, cap, "Ammo pile"); return true;
    case IT_CRATE: {
      const char* names[IT_CRATE_MAX];
      int ans[IT_CRATE_MAX];
      for (int k = 0; k < r.nContent; ++k) {
        const ClassInfo* ci = class_info(r.content[k], r.contentName[k]);
        names[k] = ci ? ci->label : "?";
        ans[k] = ci && hideOn ? it_answer_of(&s_ans, ci->inv) : IT_ANS_UNKNOWN;
      }
      if (it_hide_crate(hideOn, ans, r.nContent)) return false;
      it_crate_label(names, r.nContent, out, cap);
      return true;
    }
    default: {
      const ClassInfo* ci = class_info(r.cls, r.clsName);
      if (r.kind == IT_PICKUP && ci && hideOn && it_hide_pickup(hideOn, it_answer_of(&s_ans, ci->inv))) return false;
      strcpy_s(out, cap, ci ? ci->label : "Pickup");
      return true;
    }
  }
}

int items_markers(const ItMarker** out) {
  *out = s_markers;
  const void* player = world_player();
  int shown = 0;
  if (world_view().ok && player && worldscan_ready() && it_wanted()) {
    if (world_level_serial() != s_serial) new_level(world_level_serial());
    modelbox_frame();
    const int n = collect(player);
    const int hideOn = g_cfg.espItems && g_cfg.espHideTaken;
    for (int i = 0; i < n; ++i) {
      ItMarker& m = s_markers[shown];
      if (!label_of(s_raw[i], hideOn, m.label, sizeof m.label)) {
        s_hidden++;
        continue;
      }
      m.x = s_raw[i].x;
      m.y = s_raw[i].y;
      m.z = s_raw[i].z;
      m.radius = s_raw[i].radius;
      m.height = s_raw[i].height;
      m.box = s_raw[i].box;
      m.row = s_raw[i].row;
      s_shown[m.row]++;
      shown++;
    }
    s_frames++;
    uname_dlog_flush();
  }
  if (++s_calls % 600 == 0 && s_frames) {
    np_log("items: frames=%u shown avg items %.1f ammo crates %.1f quest %.1f; gone=%u hidden=%u; asks=%u take=%u "
           "refused=%u unanswered=%u; classes=%d (overflow %d) faults=%u", s_frames,
           (double)s_shown[IT_ROW_ITEMS] / s_frames, (double)s_shown[IT_ROW_AMMO] / s_frames,
           (double)s_shown[IT_ROW_QUEST] / s_frames, s_gone, s_hidden, s_asks, s_take, s_refused, s_unanswered,
           s_ans.n, s_ans.overflow, s_faults);
    s_frames = s_gone = s_hidden = s_asks = s_take = s_refused = s_unanswered = s_faults = 0;
    memset(s_shown, 0, sizeof s_shown);
  }
  return shown;
}

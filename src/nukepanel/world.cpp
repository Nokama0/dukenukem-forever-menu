// The per-frame world read: the player, its level, the pawn list and the view, through the
// object table and properties by name. Read only; game thread (the Present thread).
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "world.h"
#include "mpgate.h"
#include "progress.h"   // pg_ascii
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "engine.h"
#include "log.h"
#include "mswindow.h"
#include "freecam.h"   // freecam_pause_holding: the camera's own pause still reads the world

enum { C_PROPERTY, C_PAWN, C_PLAYERPAWN, C_BASEAI, C_LEVELINFO, C_COUNT };
static const wchar_t* const CLASS_NAMES[C_COUNT] = { L"Property", L"Pawn", L"PlayerPawn", L"BaseAI", L"LevelInfo" };
static const void* s_cls[C_COUNT];

// Classes (Engine.u): Actor (Location, CollisionRadius/Height, Level), RenderActor (Health, HealthCap), Pawn (Faction,
// NextPawn), LevelInfo (PawnList), PlayerPawn (rest). No FOVAngle: DefaultFOV is the fallback. Optional: DefaultFOV,
// Pauser, bPaused, AIActor.Relations/Relationships, PrimaryTargetActor, ControlX, MyHUD.BossMeterActor/bDrawBossMeter.
enum { N_LOCATION, N_RADIUS, N_HEIGHT, N_LEVEL, N_HEALTH, N_HEALTHCAP, N_FACTION, N_NEXTPAWN, N_PAWNLIST, N_PLAYER,
       N_VIEWLOC, N_VIEWROT, N_CURFOV, N_DEFFOV, N_PAUSER, N_BPAUSED, N_RELATIONSHIPS, N_RELATIONS, N_PRIMARYTARGET,
       N_CONTROLX, N_MYHUD, N_BOSSMETERACTOR, N_BDRAWBOSSMETER, N_CXPARMS, N_MYCONTROL, N_ISCONTROLLED, N_MAPFILE, N_NETMODE, N_COUNT };
static const wchar_t* const PROP_NAMES[N_COUNT] = {
  L"Location", L"CollisionRadius", L"CollisionHeight", L"Level", L"Health", L"HealthCap", L"Faction", L"NextPawn",
  L"PawnList", L"Player", L"CalcedViewLocation", L"CalcedViewRotation", L"CurrentFOV", L"DefaultFOV", L"Pauser",
  L"bPaused", L"Relationships", L"Relations", L"PrimaryTargetActor", L"ControlX", L"MyHUD", L"BossMeterActor",
  L"bDrawBossMeter", L"ControlXParms", L"MyControl", L"IsControlled", L"MapFileName", L"NetMode" };
static int s_n[N_COUNT];
static bool name_optional(int k) {
  return k == N_DEFFOV || k == N_PAUSER || k == N_BPAUSED || k == N_RELATIONSHIPS || k == N_RELATIONS ||
         k == N_PRIMARYTARGET || k == N_CONTROLX || k == N_MYHUD || k == N_BOSSMETERACTOR || k == N_BDRAWBOSSMETER ||
         k == N_CXPARMS || k == N_MYCONTROL || k == N_ISCONTROLLED || k == N_MAPFILE || k == N_NETMODE;
}

// The current state: UObject.StateFrame at +0x0C, FStateFrame.StateNode at +0x14. From the binary: FindObjectField
// (Engine.dll!0x1003D3D0) starts a function lookup at this->StateFrame(+0x0C)->State(+0x14) (engine-map.md 3).
// No property names them, so they are fixed offsets; the state is then checked against the object snapshot.
static const int UO_STATEFRAME = 0x0C, SF_STATENODE = 0x14;

// One SRelationship in a relationship array {data +0, count +4} (dnArray): class at +0, EAIAttitude byte at +4, 12
// bytes apart. From the binary: dnAI.dll!0x1003a9c0 (FindRelationship) steps by 0xC comparing [elem] with the class;
// 0x1003ae60 (GetAttitudeTowards) reads byte [elem+4]. Element size is checked per array by reflection (UAP_INNER).
static const int REL_STRIDE = 0xC, REL_ATTITUDE = 4, REL_MAX = 64;
// UArrayProperty::Inner, the element's property: UArrayProperty::CopyCompleteValue (Engine.dll!0x1004c756) reads
// [this+0x78] and the element size as its word at +0x3E (UP_ELEMSIZE).
static const int UAP_INNER = 0x78;

static const int WORLD_MAX_PAWNS = 512;
static WPawn s_pawns[WORLD_MAX_PAWNS];
static const void* s_walk[WORLD_MAX_PAWNS];
static int s_pawnCount, s_walked, s_lastStop;
static WView s_view;
static int s_viewW, s_viewH;       // this frame's back buffer, for the effective-FOV log line

static const void* s_player;
static int s_playerSlot;
static const void* s_level;
// world_level_serial: bumped for each new (level, player) pair; the same pair found again after a drop is not new.
static unsigned s_levelSerial;
static const void* s_serialLevel;
static const void* s_serialPlayer;
static unsigned s_frames;          // world_frame calls
static unsigned s_nextScan;        // no player held: the frame of the next table scan
static unsigned s_scanInterval = WORLD_SCAN_MIN;
static unsigned s_nextClassScan;
static unsigned s_pauseUntil;      // after a fault
static int s_faults;
static int s_fovSource;            // 0 none, 1 CurrentFOV, 2 DefaultFOV: logged when it changes
static bool s_exportsLogged, s_setupLogged, s_setupFailLogged, s_wasScriptable, s_cachesDirty;

// Cost of the table passes, for the 600-frame line: scans this window, the last and the window's longest, and the
// pawn-count pass.
static unsigned s_scans;
static double s_scanLastMs, s_scanMaxMs, s_countMs;
static LARGE_INTEGER s_qpf;

static const void* s_factions[16];   // R3: distinct factions seen this level
static int s_factionN;

// The pause read: logged on every change; a level with neither property logs that once.
static int s_paused = -1, s_pauseMissingLogged;
static int s_pausedNow;   // this frame's read, 0 when world_body stopped before it
static volatile LONG s_mpFlag;   // 1 while the held level is not standalone (mp_refused): any thread reads it
static int s_netMode = MP_NET_UNREAD, s_mpLogged = -2;
static int s_mapName;     // the level's package (the map) name index, 0 before a level
// (class, attitude, targets player) triples whose attitude line was logged this level (world_att_log_new).
static WAttKey s_attLogged[WATT_LOG_MAX];
static int s_attLoggedN;
// Living pawns of the last walk and of this one; a pawn alive there and dead here has its state read once.
static WAliveSet s_alive[2];
static int s_aliveCur;
static bool s_aliveDirty;
static unsigned s_downProbes, s_downs;   // states read on Health reaching 0, and knockdowns among them; cumulative
// The watch lines (world_watch), at most WATCH_PER_WINDOW per 600-frame line and WATCH_TOTAL per level.
static const int WATCH_PER_WINDOW = 4, WATCH_TOTAL = 400;
static int s_watchLogged;
static const void* s_bossMeter;          // this walk's boss-meter actor, null without one
static int s_bossMeterLogged;            // per level: the boss meter's actor logged when it changes (16 at most)
static const void* s_bossMeterLast;
static int s_downLogged;                 // per-level cap on the probe lines
static WDownSet s_down;                  // candidates for the finish-me stun (world.h)
static WAliveSet s_host[2];              // hostile living pawns of the last walk and this one (candidate admission)
static unsigned s_downedFrames;          // pawn-frames shown as downed, counted per 600 frames
static const void* s_finCls;             // dnControl_FinishingMove, found lazily once a candidate exists
static unsigned s_nextFinScan;
// UStructProperty::Struct (+0x78): the SControlX struct's own properties name MyControl and IsControlled
// (autoexec.cpp rd_cx, noshake.cpp UStructProperty::DestroyValue 0x1004ABD0).
static const int PROP_STRUCT = 0x78;
// The first 8 candidates per level are traced for 10 s after the knockdown frame: one line per change of
// (state, MyControl class, IsControlled, Health, downed).
enum { TRACE_MAX = 8, TRACE_MS = 10000 };
struct DownTrace { const void* p; unsigned startMs; int state, cls, ctl, health, downed; };
static DownTrace s_trace[TRACE_MAX];
static int s_traceN;
// Relationship arrays checked readable (data pointer and byte length), cleared per level: one VirtualQuery per
// array, never per frame (a guarded read ~12 us).
struct RelSpan { const void* data; int len; };
static RelSpan s_relOk[64];
static unsigned s_relBad;          // arrays refused (count out of range or unreadable), cumulative
static unsigned s_relMiss;         // relationship-span cache misses (one VirtualQuery each), this 600-frame window
// Per level: which relationship source first served (WREL_SRC_*), and a wrong element size per list, logged once.
static int s_relSrcLogged[3], s_relLayoutLogged[2];
static WFovLog s_fovLog;           // the effective-FOV line's change and settle rule (world.h)
static MsWindow s_cost;            // world_body's milliseconds over the 600-frame window

// The fault the filter accepted, logged by the handler.
static DWORD s_faultCode;
static void* s_faultAddr;
static ULONG_PTR s_faultData;

// IsA answers per (class, target), cleared per level and on drop: a table scan meets each class thousands of times.
struct IsaSlot { const void* cls; const void* target; int yes; };
static IsaSlot s_isa[1024];

static double ms_since(LARGE_INTEGER t0) {
  LARGE_INTEGER t1;
  QueryPerformanceCounter(&t1);
  if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
  return (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)s_qpf.QuadPart;
}

static bool class_isa(const void* cls, const void* target) {
  if (!cls || !target) return false;
  IsaSlot& s = s_isa[(((uintptr_t)cls >> 3) ^ ((uintptr_t)target >> 6)) & 1023];
  if (s.cls != cls || s.target != target) {
    const int yes = uobj_class_isa(cls, target) ? 1 : 0;   // before the key: a fault leaves no false entry
    s.yes = yes;
    s.target = target;
    s.cls = cls;
  }
  return s.yes != 0;
}

// Class pointers of an unloaded level can be reused by the next one's classes: nothing cached may outlive a drop.
static void clear_caches(void) {
  if (!s_cachesDirty) return;
  uprop_reset();
  memset(s_isa, 0, sizeof s_isa);
  s_cachesDirty = false;
}

static bool prop_is(const UProp& p, int type) { return p.ok && p.type == type; }
static bool prop_ref(const UProp& p) { return p.ok && (p.type == UW_OBJECT || p.type == UW_CLASS); }
// Vector and Rotator are three 4-byte fields.
static bool prop_vec(const UProp& p) { return prop_is(p, UW_STRUCT) && p.elemSize >= 12; }
static float rd_float(const void* o, int ofs) {
  float f;
  memcpy(&f, (const char*)o + ofs, 4);
  return f;
}

static const char* obj_tok(const void* o) {
  if (!o) return "None";
  return uobj_valid(o) ? uname_tok(uobj_name(o)) : "?";
}

static void clear_alive(void) {
  if (!s_aliveDirty) return;
  walive_clear(&s_alive[0]);
  walive_clear(&s_alive[1]);
  walive_clear(&s_host[0]);
  walive_clear(&s_host[1]);
  s_aliveDirty = false;
}

static void drop(void) {
  s_player = 0;
  s_level = 0;
  s_pawnCount = 0;
  s_walked = 0;
  s_view.ok = 0;
  s_netMode = MP_NET_UNREAD;
  InterlockedExchange(&s_mpFlag, 0);   // no level: nothing to gate
  clear_alive();
  clear_caches();
}

// Names are looked up every frame until found, classes every 120 frames (names exist once the packages load).
static bool ensure_setup(void) {
  bool names = true;
  for (int k = 0; k < N_COUNT; k++) {
    if (!s_n[k]) s_n[k] = uname_find(PROP_NAMES[k]);
    if (!s_n[k] && !name_optional(k)) names = false;
  }
  bool classes = true;
  for (int k = 0; k < C_COUNT; k++) classes = classes && s_cls[k];
  if (!classes && s_frames >= s_nextClassScan) {
    s_nextClassScan = s_frames + 120;
    uobj_find_classes(CLASS_NAMES, s_cls, C_COUNT);
    classes = true;
    // BaseAI is logged but optional: hostility now comes from faction attitudes (docs/re/factions.md).
    for (int k = 0; k < C_COUNT; k++) classes = classes && (s_cls[k] || k == C_BASEAI);
    if (classes) uprop_set_property_class(s_cls[C_PROPERTY]);
    if ((!classes || !names) && !s_setupFailLogged) {
      s_setupFailLogged = true;
      char miss[768];   // every name and class missing fits (about 400 characters)
      int o = 0;
      for (int k = 0; k < N_COUNT + C_COUNT; k++) {
        const bool name = k < N_COUNT;
        if (name ? s_n[k] != 0 || name_optional(k) : s_cls[k - N_COUNT] != 0 || k - N_COUNT == C_BASEAI) continue;
        const int n = _snprintf(miss + o, sizeof miss - 1 - o, name ? " name:%ls" : " class:%ls",
                                name ? PROP_NAMES[k] : CLASS_NAMES[k - N_COUNT]);
        if (n < 0) break;   // truncated: _snprintf returns -1
        o += n;
      }
      miss[o] = 0;
      np_log("world: setup incomplete, missing%s (retried every 120 frames; logged once)", miss);
    }
  }
  const bool ok = names && classes;
  if (ok && !s_setupLogged) {
    s_setupLogged = true;
    np_log("world: classes Property=%p Pawn=%p PlayerPawn=%p BaseAI=%p LevelInfo=%p (by name in the object table)",
           s_cls[C_PROPERTY], s_cls[C_PAWN], s_cls[C_PLAYERPAWN], s_cls[C_BASEAI], s_cls[C_LEVELINFO]);
  }
  return ok;
}

// The local player: the PlayerPawn whose Player (its viewport) is set. Scanned from the table while none is held,
// at an interval that backs off from 30 to 600 frames.
static bool find_player(void) {
  if (s_player && uobj_at_is(s_playerSlot, s_player)) {
    const UProp p = uprop_get(uobj_class(s_player), s_n[N_PLAYER]);
    if (prop_is(p, UW_OBJECT) && uw_ptr(s_player, p.offset)) return true;
  }
  if (s_player) {
    np_log("world: player %p lost", s_player);
    drop();
  }
  if (s_frames < s_nextScan) return false;
  s_nextScan = s_frames + s_scanInterval;
  s_scanInterval = world_scan_backoff(s_scanInterval);
  // Each scan starts from empty caches: classes may have been freed and reused since the last one.
  clear_caches();
  s_cachesDirty = true;
  LARGE_INTEGER t0;
  QueryPerformanceCounter(&t0);
  const int n = uobj_count();
  int candidates = 0;
  for (int i = 0; i < n; i++) {
    const void* o = uobj_at(i);
    if (!o) continue;
    const void* c = uobj_class(o);
    if (!class_isa(c, s_cls[C_PLAYERPAWN])) continue;
    const UProp p = uprop_get(c, s_n[N_PLAYER]);
    if (!prop_is(p, UW_OBJECT) || !uw_ptr(o, p.offset)) continue;
    if (!candidates++) {
      s_player = o;
      s_playerSlot = i;
    }
  }
  s_scanLastMs = ms_since(t0);
  if (s_scanLastMs > s_scanMaxMs) s_scanMaxMs = s_scanLastMs;
  s_scans++;
  if (!s_player) return false;
  uname_dlog("world: player %p (%s) slot %d by object-table scan: PlayerPawn with Player set, %d candidate(s), "
             "%d objects, %.2f ms", s_player, uname_tok(uobj_name(uobj_class(s_player))), s_playerSlot,
             candidates, n, s_scanLastMs);
  return true;
}

static void on_level(const void* level) {
  s_level = level;
  uprop_reset();
  memset(s_isa, 0, sizeof s_isa);
  s_factionN = 0;
  s_fovSource = 0;
  s_attLoggedN = 0;
  s_downLogged = 0;
  s_watchLogged = 0;
  memset(&s_down, 0, sizeof s_down);
  s_finCls = 0;
  s_traceN = 0;
  s_bossMeterLogged = 0;
  s_bossMeterLast = 0;
  clear_alive();
  s_paused = -1;
  s_pauseMissingLogged = 0;
  memset(s_relOk, 0, sizeof s_relOk);
  memset(s_relSrcLogged, 0, sizeof s_relSrcLogged);
  memset(s_relLayoutLogged, 0, sizeof s_relLayoutLogged);
  if (!level) {
    np_log("world: player %p has no level", s_player);
    return;
  }
  s_scanInterval = WORLD_SCAN_MIN;
  if (level != s_serialLevel || s_player != s_serialPlayer) {
    s_serialLevel = level;
    s_serialPlayer = s_player;
    ++s_levelSerial;
  }
  const void* top = level;
  for (int i = 0; i < 8 && uobj_outer(top); i++) top = uobj_outer(top);
  s_mapName = uobj_name(top);
  uname_dlog("world: level %p (%s in %s) player %p (%s)", level, uname_tok(uobj_name(level)),
             uname_tok(uobj_name(top)), s_player, uname_tok(uobj_name(uobj_class(s_player))));
}

// LevelInfo.NetMode (a ByteProperty), re-read each frame through the cached property: a mode unread in a held level
// stays MP_NET_UNREAD and gates as multiplayer. One line when the mode changes.
static void read_net_mode(void) {
  int mode = MP_NET_UNREAD;
  const UProp p = s_n[N_NETMODE] ? uprop_get(uobj_class(s_level), s_n[N_NETMODE]) : UProp();
  if (prop_is(p, UW_BYTE) && pg_field_in(p.offset, 1)) {
    mode = *((const unsigned char*)s_level + p.offset);
    static bool offsetLogged;
    if (!offsetLogged) {
      offsetLogged = true;
      np_log("world: LevelInfo.NetMode at +0x%X (%s IsMP's +0x918)", p.offset, p.offset == 0x918 ? "equals" : "DIFFERS from");
    }
  }
  s_netMode = mode;
  InterlockedExchange(&s_mpFlag, mp_active(1, mode) ? 1 : 0);
  if (mode != s_mpLogged) {
    s_mpLogged = mode;
    np_log("world: NetMode %d%s: %s", mode, mode == MP_NET_UNREAD ? " (unread)" : "",
           mp_active(1, mode) ? "multiplayer, the panel's gameplay features refuse" : "standalone");
  }
}

static WView read_view(const void* pcls) {
  WView v = { 0, 0, 0, 0, 0, 0, 0.f, 0, 1.f };
  const UProp vl = uprop_get(pcls, s_n[N_VIEWLOC]);
  const UProp vr = uprop_get(pcls, s_n[N_VIEWROT]);
  if (!prop_vec(vl) || !prop_vec(vr)) return v;
  float loc[3];
  int rot[3];
  memcpy(loc, (const char*)s_player + vl.offset, sizeof loc);
  memcpy(rot, (const char*)s_player + vr.offset, sizeof rot);   // Rotator: Pitch, Yaw, Roll (view.h)
  int src = 0;
  float fov = 0.f;
  const UProp cf = uprop_get(pcls, s_n[N_CURFOV]);
  if (prop_is(cf, UW_FLOAT)) {
    fov = rd_float(s_player, cf.offset);
    if (fov > 1.f && fov < 179.f) src = 1;
  }
  if (!src && s_n[N_DEFFOV]) {
    const UProp df = uprop_get(pcls, s_n[N_DEFFOV]);
    if (prop_is(df, UW_FLOAT)) {
      fov = rd_float(s_player, df.offset);
      if (fov > 1.f && fov < 179.f) src = 2;
    }
  }
  if (src != s_fovSource) {
    s_fovSource = src;
    np_log("world: view CalcedViewLocation/CalcedViewRotation, fov %s = %.1f",
           src == 1 ? "CurrentFOV" : src == 2 ? "DefaultFOV" : "unusable", fov);
  }
  if (!src) return v;
  v.x = loc[0];
  v.y = loc[1];
  v.z = loc[2];
  v.pitch = rot[0];
  v.yaw = rot[1];
  v.roll = rot[2];
  float wheel;
  if (freecam_fov_now(&wheel)) fov = wheel;   // the free camera's wheel FOV: the frame is drawn with it
  v.fovDeg = fov;
  // view_effective_hfov applies it as GetFOV does; a value outside a sane band is not the cvar.
  const float* as = engine_exports()->aspectScale;
  const float scale = as ? *as : 1.f;
  v.aspectScale = scale > 0.1f && scale < 10.f ? scale : 1.f;
  // On a new scale, back buffer or FOV source at once; a zoom only once it settles (world.h).
  if (world_fov_log_step(&s_fovLog, v.aspectScale, s_viewW, s_viewH, src, fov)) {
    np_log("world: r_AspectScale %.3f (raw %.3f%s); effective hfov %.1f at %dx%d", v.aspectScale, scale,
           as ? "" : ", storage unreadable", view_effective_hfov(fov, s_viewW, s_viewH, v.aspectScale), s_viewW,
           s_viewH);
  }
  v.ok = 1;
  return v;
}

// Paused when the pause menu (or anything else) has set LevelInfo.Pauser or bPaused (world.h). Logged on change.
static int read_paused(void) {
  const void* lc = uobj_class(s_level);
  const UProp pz = uprop_get(lc, s_n[N_PAUSER]);
  const UProp bp = uprop_get(lc, s_n[N_BPAUSED]);
  const int hasPauser = prop_is(pz, UW_STR) && pz.elemSize >= 8;   // {data, count, max}
  const int hasB = prop_is(bp, UW_BOOL) && bp.bitmask != 0;
  const int count = hasPauser ? uw_int(s_level, pz.offset + 4) : 0;
  const int bit = hasB ? (uw_int(s_level, bp.offset) & (int)bp.bitmask) != 0 : 0;
  if (!hasPauser && !hasB && !s_pauseMissingLogged) {
    s_pauseMissingLogged = 1;
    np_log("world: neither LevelInfo.Pauser nor bPaused resolved; drawing through pauses");
  }
  const int paused = world_paused(hasPauser, count, hasB, bit);
  if (paused != s_paused) {
    s_paused = paused;
    np_log("world: paused=%d (Pauser=%d bPaused=%d) PauserCount=%d", paused, hasPauser && count > 1, bit, count);
  }
  return paused;
}

static int isa_cb(void*, const void* cls, const void* ancestor) { return class_isa(cls, ancestor) ? 1 : 0; }

static bool rel_span_ok(const void* data, int len) {
  RelSpan& s = s_relOk[((uintptr_t)data >> 4) & 63];
  if (s.data == data && s.len >= len) return true;
  s_relMiss++;
  if (!engine_readable(data, (size_t)len)) return false;
  s.data = data;
  s.len = len;
  return true;
}

// One relationship array of pawn p, by property name: 1 when it resolved as an array of 12-byte elements (the
// Inner property's ElementSize, by reflection), its entries in out[0..*n). An array too long or unreadable counts
// in relBad and reads as resolved but empty.
static int read_rel_array(const void* p, const void* c, int which, WRel* out, int* n) {
  *n = 0;
  const UProp rp = uprop_get(c, s_n[which]);
  if (!prop_is(rp, UW_ARRAY) || !rp.field) return 0;
  const void* inner = uw_ptr(rp.field, UAP_INNER);
  const int innerOk = inner && uobj_valid(inner);
  const int elemSize = innerOk ? *(const unsigned short*)((const char*)inner + UP_ELEMSIZE) : 0;
  if (!world_rel_layout_ok(innerOk, elemSize)) {
    int& logged = s_relLayoutLogged[which == N_RELATIONS ? 0 : 1];
    if (!logged) {
      logged = 1;
      uname_dlog("world: %s on %s: element size %d (inner %s), not %d; not read", uname_tok(s_n[which]),
                 uname_tok(uobj_name(c)), elemSize, innerOk ? "ok" : "invalid", WREL_ELEM_SIZE);
    }
    return 0;
  }
  const char* data = (const char*)uw_ptr(p, rp.offset);
  const int count = uw_int(p, rp.offset + 4);
  if (!data || count <= 0) return 1;
  if (count > REL_MAX || !rel_span_ok(data, count * REL_STRIDE)) {
    s_relBad++;
    return 1;
  }
  for (int i = 0; i < count; i++) {
    const void* cls = uw_ptr(data, i * REL_STRIDE);
    out[i].cls = cls && uobj_valid(cls) ? cls : 0;
    out[i].attitude = *(const unsigned char*)(data + i * REL_STRIDE + REL_ATTITUDE);
  }
  *n = count;
  return 1;
}

// The pawn's attitude toward the player's faction, as GetAttitudeTowards(self = this pawn, subject = the player)
// finds it: from the runtime list FindRelationship walks (AIActor.Relations, +0x188C), else from the class
// defaults (docs/re/factions.md). -1 for none. Each source is logged the first time it serves in a level.
static int read_attitude(const void* p, const void* c, const void* playerFaction) {
  WRel rt[REL_MAX], df[REL_MAX];
  int nrt = 0, ndf = 0, defOk = 0;
  const int rtOk = read_rel_array(p, c, N_RELATIONS, rt, &nrt);
  if (!(rtOk && nrt > 0)) defOk = read_rel_array(p, c, N_RELATIONSHIPS, df, &ndf);
  const int src = world_rel_source(rtOk, nrt, defOk, ndf);
  if (src != WREL_SRC_NONE && !s_relSrcLogged[src]) {
    s_relSrcLogged[src] = 1;
    uname_dlog("world: relationships from %s first served %s (Relations %s, %d entries)",
               src == WREL_SRC_RUNTIME ? "Relations (runtime list)" : "Relationships (class defaults)",
               uname_tok(uobj_name(c)), rtOk ? "resolved" : "unresolved", nrt);
  }
  if (src == WREL_SRC_RUNTIME) return world_attitude(rt, nrt, playerFaction, isa_cb, 0);
  if (src == WREL_SRC_DEFAULTS) return world_attitude(df, ndf, playerFaction, isa_cb, 0);
  return -1;
}

// The attitude table, once per (class, attitude, targets player) per level.
static void note_attitude(const void* cls, const void* faction, const void* playerFaction, int attitude,
                          int targets) {
  if (!world_att_log_new(s_attLogged, &s_attLoggedN, WATT_LOG_MAX, cls, attitude, targets)) return;
  uname_dlog("world: attitude %s (%s)->%s = %s (%d)%s", uname_tok(uobj_name(cls)), obj_tok(faction),
             obj_tok(playerFaction), world_attitude_name(attitude), attitude, targets ? ", targets player" : "");
}

// AIActor.PrimaryTargetActor: the target the AI itself fights by, where a script's forced SetTarget lands and what
// every cycloid attack aims at (factions.md "Cycloid"). Null for a class without it (non-AI pawns) or no target.
static const void* read_target(const void* p, const void* c) {
  if (!s_n[N_PRIMARYTARGET]) return 0;
  const UProp pt = uprop_get(c, s_n[N_PRIMARYTARGET]);
  const void* t = prop_is(pt, UW_OBJECT) ? uw_ptr(p, pt.offset) : 0;
  return t && uobj_valid(t) ? t : 0;
}

// DukeHUD.BossMeterActor while bDrawBossMeter (DukeHUD.AddBossMeter sets both, RemoveBossMeter clears both), read
// through the player's MyHUD once per walk. Null without a HUD, a meter or the properties. Logged on change.
static const void* read_boss_meter(void) {
  if (!s_n[N_MYHUD] || !s_n[N_BOSSMETERACTOR] || !s_n[N_BDRAWBOSSMETER]) return 0;
  const UProp hp = uprop_get(uobj_class(s_player), s_n[N_MYHUD]);
  const void* hud = prop_is(hp, UW_OBJECT) ? uw_ptr(s_player, hp.offset) : 0;
  if (!hud || !uobj_valid(hud)) return 0;
  const void* hc = uobj_class(hud);
  const UProp ba = uprop_get(hc, s_n[N_BOSSMETERACTOR]), bd = uprop_get(hc, s_n[N_BDRAWBOSSMETER]);
  if (!prop_is(ba, UW_OBJECT) || !prop_is(bd, UW_BOOL) || !bd.bitmask) return 0;
  const void* a = (uw_int(hud, bd.offset) & (int)bd.bitmask) ? uw_ptr(hud, ba.offset) : 0;
  if (a && !uobj_valid(a)) a = 0;
  if (a != s_bossMeterLast && s_bossMeterLogged < 16) {
    s_bossMeterLogged++;
    uname_dlog("world: boss meter %s", a ? uname_tok(uobj_name(uobj_class(a))) : "off");
  }
  s_bossMeterLast = a;
  return a;
}

// Health reached 0 this frame: in state ControlX, or as the boss meter's actor, the pawn is knocked down, not dead
// (ControlX's TakeDamage is empty and a timeout restores Health). The state frame is a separate allocation: one
// guarded read, on this frame only.
static int read_knockdown(const void* p, const void* c, int bossMeter) {
  s_downProbes++;
  const char* frame = (const char*)uw_ptr(p, UO_STATEFRAME);
  const void* state = frame && engine_readable(frame + SF_STATENODE, 4) ? uw_ptr(frame, SF_STATENODE) : 0;
  if (state && !uobj_valid(state)) state = 0;
  const int down = world_knock_rule(state && s_n[N_CONTROLX] && uobj_name(state) == s_n[N_CONTROLX], bossMeter);
  s_downs += (unsigned)down;
  if (s_downLogged < 16) {
    s_downLogged++;
    uname_dlog("world: %s Health reached 0 in state %s%s: %s", uname_tok(uobj_name(c)),
               state ? uname_tok(uobj_name(state)) : "(unreadable)", bossMeter ? " (boss meter)" : "",
               down ? "knockdown, no kill" : "kill");
  }
  return down;
}

// The pawn's current state name index (0 unreadable): one guarded read of its state frame.
static int state_name_idx(const void* p) {
  const char* frame = (const char*)uw_ptr(p, UO_STATEFRAME);
  const void* state = frame && engine_readable(frame + SF_STATENODE, 4) ? uw_ptr(frame, SF_STATENODE) : 0;
  return state && uobj_valid(state) ? uobj_name(state) : 0;
}

// AIActor.ControlXParms.MyControl (null unless a live object) and .IsControlled, through the SControlX struct's own
// properties, each offset checked against the struct's element size. 1 read, 0 the layout did not resolve.
static int read_cx(const void* p, const void** myControl, int* isControlled) {
  *myControl = 0;
  *isControlled = 0;
  if (!s_n[N_CXPARMS] || !s_n[N_MYCONTROL] || !s_n[N_ISCONTROLLED]) return 0;
  const UProp pi = uprop_get(uobj_class(p), s_n[N_CXPARMS]);
  if (!prop_is(pi, UW_STRUCT) || !pi.field || pi.elemSize <= 0) return 0;
  const void* strct = uw_ptr(pi.field, PROP_STRUCT);
  if (!strct || !uobj_valid(strct)) return 0;
  const UProp mc = uprop_get(strct, s_n[N_MYCONTROL]), ic = uprop_get(strct, s_n[N_ISCONTROLLED]);
  if (!prop_is(mc, UW_OBJECT) || mc.offset < 0 || mc.offset + 4 > pi.elemSize) return 0;
  if (!prop_is(ic, UW_BOOL) || !ic.bitmask || ic.offset < 0 || ic.offset + 4 > pi.elemSize) return 0;
  const void* m = uw_ptr(p, pi.offset + mc.offset);
  *myControl = m && uobj_valid(m) ? m : 0;
  *isControlled = (uw_int(p, pi.offset + ic.offset) & (int)ic.bitmask) != 0;
  return 1;
}

static int is_finishing(const void* myControl) {
  return myControl && s_finCls && class_isa(uobj_class(myControl), s_finCls);
}

// One candidate this frame: its stun object read, the downed rule applied, the trace line written on a change.
static int down_step(const void* p, const void* c, int alive, int health) {
  const int i = wdown_index(&s_down, p);
  const void* mc = 0;
  int ctl = 0, fin = 0;
  if (!alive) {
    read_cx(p, &mc, &ctl);
    fin = is_finishing(mc);
  }
  const unsigned age = s_frames - s_down.born[i];
  if (fin) s_down.seen[i] = 1;
  const int downed = world_downed(alive, fin);
  const int keep = world_down_keep(alive, fin, age, s_down.seen[i]);
  for (int t = 0; t < s_traceN; t++) {
    DownTrace& d = s_trace[t];
    if (d.p != p || GetTickCount() - d.startMs > (unsigned)TRACE_MS) continue;
    const int st = state_name_idx(p), cl = mc ? uobj_name(uobj_class(mc)) : 0;
    if (st != d.state || cl != d.cls || ctl != d.ctl || health != d.health || downed != d.downed) {
      d.state = st; d.cls = cl; d.ctl = ctl; d.health = health; d.downed = downed;
      uname_dlog("world: downtrace %s +%ums state %s MyControl %s IsControlled %d Health %d downed %d kept %d",
                 uname_tok(uobj_name(c)), GetTickCount() - d.startMs, st ? uname_tok(st) : "?",
                 mc ? uname_tok(cl) : "None", ctl, health, downed, keep);
    }
  }
  if (!keep) wdown_remove(&s_down, p);
  return downed;
}

static const void* pawn_next(void*, const void* p) {
  const UProp np = uprop_get(uobj_class(p), s_n[N_NEXTPAWN]);
  return prop_is(np, UW_OBJECT) ? uw_ptr(p, np.offset) : 0;
}

static int pawn_valid(void*, const void* p) {
  return uobj_valid(p) && class_isa(uobj_class(p), s_cls[C_PAWN]);
}

// R3: each distinct faction once per level, with the first pawn class seen carrying it.
static void note_faction(const void* faction, const void* cls, const void* playerFaction) {
  for (int i = 0; i < s_factionN; i++)
    if (s_factions[i] == faction) return;
  if (s_factionN >= 16) return;
  s_factions[s_factionN++] = faction;
  uname_dlog("world: faction %s sample %s (player faction %s)", obj_tok(faction), uname_tok(uobj_name(cls)),
             obj_tok(playerFaction));
}

static void read_pawns(const void* playerFaction) {
  // Not walked yet: an unresolved PawnList leaves the walk incomplete, so the damage tracker holds.
  s_walked = 0;
  s_lastStop = UW_STOP_FOREIGN;
  const UProp pl = uprop_get(uobj_class(s_level), s_n[N_PAWNLIST]);
  if (!prop_is(pl, UW_OBJECT)) return;
  const UwListOps ops = { pawn_next, pawn_valid, 0 };
  const UWList l = uw_list(uw_ptr(s_level, pl.offset), &ops, s_walk, WORLD_MAX_PAWNS);
  s_walked = l.count;
  s_lastStop = l.stop;
  const WAliveSet* before = &s_alive[s_aliveCur];
  const WAliveSet* hostBefore = &s_host[s_aliveCur];
  s_aliveCur ^= 1;
  WAliveSet* now = &s_alive[s_aliveCur];
  walive_clear(now);
  WAliveSet* hostNow = &s_host[s_aliveCur];
  walive_clear(hostNow);
  s_aliveDirty = true;
  s_bossMeter = read_boss_meter();
  // The 600-frame line's frame (world_frame logs after this walk): the watch lines for it.
  const bool watchFrame = s_frames % 600 == 0;
  int watched = 0;
  for (int i = 0; i < l.count; i++) {
    const void* p = s_walk[i];
    if (p == s_player) continue;
    const void* c = uobj_class(p);
    const UProp loc = uprop_get(c, s_n[N_LOCATION]);
    if (!prop_vec(loc)) continue;
    const UProp rad = uprop_get(c, s_n[N_RADIUS]), hgt = uprop_get(c, s_n[N_HEIGHT]);
    const UProp hp = uprop_get(c, s_n[N_HEALTH]), hpc = uprop_get(c, s_n[N_HEALTHCAP]);
    const UProp fac = uprop_get(c, s_n[N_FACTION]);
    WPawn& w = s_pawns[s_pawnCount++];
    float xyz[3];
    memcpy(xyz, (const char*)p + loc.offset, sizeof xyz);
    w.obj = p;
    w.x = xyz[0];
    w.y = xyz[1];
    w.z = xyz[2];
    w.radius = prop_is(rad, UW_FLOAT) ? rd_float(p, rad.offset) : 0.f;
    w.height = prop_is(hgt, UW_FLOAT) ? rd_float(p, hgt.offset) : 0.f;
    w.health = hp.ok ? uw_read_health(p, hp.offset, hp.type) : 0;
    w.healthCap = hpc.ok ? uw_read_health(p, hpc.offset, hpc.type) : 0;
    w.faction = prop_ref(fac) ? uw_ptr(p, fac.offset) : 0;
    w.nameIdx = uobj_name(c);
    w.alive = hp.ok ? uw_health_alive(p, hp.offset, hp.type) : 0;
    w.attitude = read_attitude(p, c, playerFaction);
    w.target = read_target(p, c);
    w.targetsPlayer = w.target && w.target == s_player;
    w.bossMeter = s_bossMeter && p == s_bossMeter;
    world_classify(w.alive, w.attitude, w.targetsPlayer, w.bossMeter, &w.enemy, &w.ally, &w.neutral);
    if (w.alive) walive_add(now, p);
    w.knockdown = world_knock_probe(w.alive, walive_has(before, p)) ? read_knockdown(p, c, w.bossMeter) : 0;
    // A kill by bullet or melee on a Pig Cop, Enforcer or Assault Trooper can start the finish-me stun, which keeps
    // Health 0 while the enemy kneels: a hostile pawn reaching 0 is a candidate, shown while MyControl is a
    // finishing move (world.h).
    if (w.alive && w.enemy) walive_add(hostNow, p);
    if (world_down_admit(w.alive, walive_has(before, p), walive_has(hostBefore, p))) {
      wdown_add(&s_down, p, s_frames);
      if (s_traceN < TRACE_MAX) {
        const DownTrace t = { p, GetTickCount(), -1, -1, -1, -1, -1 };
        s_trace[s_traceN++] = t;
      }
    }
    w.downed = 0;
    if (s_down.n && wdown_has(&s_down, p)) {
      if (!s_finCls && s_frames >= s_nextFinScan) {
        static const wchar_t* const kFin[1] = { L"dnControl_FinishingMove" };
        s_nextFinScan = s_frames + 120;
        uobj_find_classes(kFin, &s_finCls, 1);
      }
      wdown_touch(&s_down, p, s_frames);
      w.downed = down_step(p, c, w.alive, w.health);
      s_downedFrames += (unsigned)w.downed;
    }
    if (watchFrame && world_watch(w.attitude, w.target != 0, w.bossMeter) && watched < WATCH_PER_WINDOW &&
        s_watchLogged < WATCH_TOTAL) {
      watched++;
      s_watchLogged++;
      uname_dlog("world: watch %s attitude %s alive=%d target %s%s%s", uname_tok(uobj_name(c)),
                 world_attitude_name(w.attitude), w.alive,
                 w.target ? uname_tok(uobj_name(uobj_class(w.target))) : "None",
                 w.targetsPlayer ? " (the player)" : "", w.bossMeter ? ", boss meter" : "");
    }
    note_faction(w.faction, c, playerFaction);
    note_attitude(c, w.faction, playerFaction, w.attitude, w.targetsPlayer);
  }
  // A complete walk drops candidates that left the world; a cut-short one proves nothing.
  if (l.stop == UW_STOP_END && s_down.n) wdown_prune(&s_down, s_frames);
}

// R2: live Pawn instances in the table whose Level is this level (the total beside the walk). Defaults have no
// Level, so they never count.
static int count_pawn_objs(void) {
  if (!s_level) return 0;
  int n = 0;
  const int count = uobj_count();
  for (int i = 0; i < count; i++) {
    const void* o = uobj_at(i);
    if (!o) continue;
    const void* c = uobj_class(o);
    if (!class_isa(c, s_cls[C_PAWN])) continue;
    const UProp lv = uprop_get(c, s_n[N_LEVEL]);
    if (prop_is(lv, UW_OBJECT) && uw_ptr(o, lv.offset) == s_level) n++;
  }
  return n;
}

static int world_body(int readPawns) {
  s_pawnCount = 0;
  s_view.ok = 0;
  const EngineExports* e = engine_exports();
  if (!e->gObjObjects || !e->nameFind || !e->nameStr || !e->names || !e->gIsScriptable) {
    if (!s_exportsLogged) {
      s_exportsLogged = true;
      np_log("world: unavailable: an Engine.dll or dnCommon.dll export is missing");
    }
    return 0;
  }
  // Level loads drop scriptability; nothing is read until it returns, and its return restarts the fast scans.
  const bool scriptable = *e->gIsScriptable != 0;
  if (scriptable && !s_wasScriptable) {
    s_scanInterval = WORLD_SCAN_MIN;
    s_nextScan = 0;
  }
  s_wasScriptable = scriptable;
  if (!scriptable || !uobj_snapshot()) {
    drop();
    return 0;
  }
  if (!ensure_setup()) return 0;
  s_cachesDirty = true;   // lookups from here on fill the caches
  if (!find_player()) return 0;
  const void* pcls = uobj_class(s_player);
  const UProp lp = uprop_get(pcls, s_n[N_LEVEL]);
  const void* level = prop_is(lp, UW_OBJECT) ? uw_ptr(s_player, lp.offset) : 0;
  if (level && (!uobj_valid(level) || !class_isa(uobj_class(level), s_cls[C_LEVELINFO]))) level = 0;
  if (level != s_level) on_level(level);
  if (s_level) read_net_mode();
  else {
    s_netMode = MP_NET_UNREAD;
    InterlockedExchange(&s_mpFlag, 0);
  }
  s_view = read_view(pcls);
  const int paused = s_level ? read_paused() : 0;
  s_pausedNow = paused;
  // The free camera's own pause is drawn through: the ESP shows the frozen world from the camera.
  if (!world_gate(s_player, s_level, s_view.ok, paused && !freecam_pause_holding())) {
    s_view.ok = 0;
    return 0;
  }
  if (!readPawns) {
    // A stale living set would read a pawn that died meanwhile as dying on the first walk back.
    clear_alive();
    s_walked = 0;
    s_lastStop = UW_STOP_END;   // nothing asked for, nothing missed
    return 1;
  }
  const UProp pf = uprop_get(pcls, s_n[N_FACTION]);
  read_pawns(prop_ref(pf) ? uw_ptr(s_player, pf.offset) : 0);
  return 1;
}

// Access violations only: anything else (a C++ throw, a breakpoint) is not ours to swallow. Name strings are not
// fetched inside the guard (uname.h). Name lookups by string (uname_find: dnName's find constructor) do run inside
// it, on our own constant strings only, so a fault while they hold the names lock is not expected.
static int fault_filter(const EXCEPTION_POINTERS* ep) {
  const EXCEPTION_RECORD* r = ep->ExceptionRecord;
  if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
  s_faultCode = r->ExceptionCode;
  s_faultAddr = r->ExceptionAddress;
  s_faultData = r->NumberParameters >= 2 ? r->ExceptionInformation[1] : 0;
  return EXCEPTION_EXECUTE_HANDLER;
}

static void log_fault(void) {
  char mod[MAX_PATH] = "?";
  HMODULE m = 0;
  uintptr_t base = 0;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCSTR)s_faultAddr, &m) && m) {
    base = (uintptr_t)m;
    if (!GetModuleFileNameA(m, mod, sizeof mod)) mod[0] = '?', mod[1] = 0;
  }
  const char* slash = strrchr(mod, '\\');
  np_log("world: fault #%d code=0x%08lX at %s+0x%x (%p) data=%p; reads paused for 600 frames", s_faults,
         (unsigned long)s_faultCode, slash ? slash + 1 : mod, (unsigned)((uintptr_t)s_faultAddr - base), s_faultAddr,
         (void*)s_faultData);
}

const char* mp_refused(void) { return s_mpFlag ? MP_WHY : 0; }

int world_paused_now(void) { return s_pausedNow; }
int world_map_name(void) { return s_mapName; }

// LevelInfo.MapFileName: the map the game itself passes to SPSetEgoAwardsForLevel (docs/re/progress.md section 3).
void world_level_map_file(char* out, int cap) {
  if (cap <= 0) return;
  out[0] = 0;
  if (!s_level || !s_n[N_MAPFILE]) return;
  __try {
    const UProp p = uprop_get(uobj_class(s_level), s_n[N_MAPFILE]);
    if (!prop_is(p, UW_STR) || p.elemSize < 8 || p.offset < 0 || p.offset > 0xFFF0) return;
    const wchar_t* data = (const wchar_t*)uw_ptr(s_level, p.offset);
    int count = uw_int(s_level, p.offset + 4);
    if (!data || count <= 0 || count > 63) return;
    if (engine_readable(data, (size_t)count * sizeof(wchar_t))) pg_ascii(data, count, out, cap);
  } __except (fault_filter(GetExceptionInformation())) {
    out[0] = 0;
  }
}

int world_frame(int W, int H, int readPawns) {
  s_viewW = W;
  s_viewH = H;
  ++s_frames;
  s_pausedNow = 0;
  int ok = 0;
  if (s_frames >= s_pauseUntil) {
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    // The backstop for a stale engine pointer: every pointer is checked against the snapshot first, but that
    // check itself reads p->Index (uobj.h).
    __try {
      ok = world_body(readPawns);
    } __except (fault_filter(GetExceptionInformation())) {
      ++s_faults;
      s_pauseUntil = s_frames + 600;
      s_cachesDirty = true;
      drop();
      if (s_faults <= 8) log_fault();
    }
    ms_window_add(&s_cost, ms_since(t0));
  }
  if (!ok) {
    s_pawnCount = 0;
    s_view.ok = 0;
  }
  if (s_frames % 600 == 0) {
    int enemies = 0, allies = 0, neutrals = 0, byTarget = 0;
    for (int i = 0; i < s_pawnCount; i++) {
      const WPawn& w = s_pawns[i];
      enemies += w.enemy;
      allies += w.ally;
      neutrals += w.neutral;
      // Enemies only by target or the boss meter: their attitude is not hostile (the Map00 Cycloid).
      byTarget += w.enemy && !(w.attitude >= 0 && w.attitude < WATT_NEUTRAL);
    }
    int pawnObjs = 0;
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    __try {
      pawnObjs = ok ? count_pawn_objs() : 0;
    } __except (fault_filter(GetExceptionInformation())) {
      pawnObjs = -1;
    }
    s_countMs = ms_since(t0);
    np_log("world: pawns=%d enemies=%d byTarget=%d allies=%d neutrals=%d paused=%d read=%d objects=%d pawnObjs=%d "
           "stop=%s scans=%u scanLast=%.2fms scanMax=%.2fms scanEvery=%u count=%.2fms faults=%d relBad=%u relMiss=%u "
           "downProbes=%u downs=%u downedFrames=%u (now %d) world=%.3f/%.3fms", ok ? s_walked : 0, enemies, byTarget, allies, neutrals,
           s_paused, readPawns, uobj_count(), pawnObjs, ok ? uw_stop_name(s_lastStop) : "none", s_scans, s_scanLastMs,
           s_scanMaxMs, s_scanInterval, s_countMs, s_faults, s_relBad, s_relMiss, s_downProbes, s_downs,
           s_downedFrames, s_down.n, ms_window_avg(s_cost), s_cost.max);
    s_scans = 0;
    s_scanMaxMs = 0.0;
    s_relMiss = 0;
    s_downedFrames = 0;
    ms_window_reset(&s_cost);
  }
  uname_dlog_flush();   // outside the guard: names are fetched under the engine's lock
  return ok;
}

int world_pawns(const WPawn** out) {
  if (out) *out = s_pawns;
  return s_pawnCount;
}

int world_pawns_complete(void) { return s_lastStop == UW_STOP_END; }

WView world_view(void) { return s_view; }

void* world_player(void) { return s_level ? (void*)s_player : 0; }

unsigned world_level_serial(void) { return s_levelSerial; }

// Drop-in enemies (spawnbox.h; spawns.md 4-5), game thread only. Once a frame over world.cpp's walked pawns: hostile
// pawns' rendered boxes via RenderActor.GetRenderBoundingBox (native 838, scall), round-robin, every frame for flagged
// ones, whose box esp.cpp, dmgnum.cpp, aim.cpp use. Tiny/zero collision: mounted collision (mountcol.h) each frame.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "spawnbox.h"
#include "aim.h"   // aim_damage_type: the held or mounted gun's damage type
#include "overkill.h"
#include "bloat.h"
#include "world.h"
#include "scall.h"
#include "scallparms.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "engine.h"
#include "log.h"

// The current state, as world.cpp reads it: UObject.StateFrame +0x0C, FStateFrame.StateNode +0x14 (world.cpp's
// justification: FindObjectField, Engine.dll!0x1003D3D0).
static const int UO_STATEFRAME = 0x0C, SF_STATENODE = 0x14;

enum { N_PHYSICS, N_MOUNTPARENT, N_CUTSCENE, N_CUROP, N_TELEPORTING, N_NODAMAGE, N_HIDDEN, N_TKSHIELD, N_DSI, N_REGION, N_ZONE, N_NEUTRAL, N_ABSORBERS,
       N_QCLASS, N_QTYPES, N_QNOT, N_DSCALE, N_EXPANDING, N_COLLIDE, N_PROJTARGET, N_BLOCKACT, N_BLOCKPLY,
       N_TRACESHOOT, N_SHOOTABLE, N_AIMASSIST, N_HEALTH, N_HEALTHMIN, N_ROLE, N_REMOTEROLE, N_HEALTHCAP,
       N_THISFRAME, N_DMGINFO, N_MDAMAGE, N_MDTYPE, N_MVALID, N_MTIME, N_LEVEL, N_GAMETIME, N_INVULN, N_GODMODE, N_GHOSTMODE, N_PROTEGO,
       N_MOUNTLIST, N_MOUNTED, N_CRADIUS, N_CHEIGHT, N_LOCATION, N_VELOCITY, N_MOUNTITEM, N_BLOATSTATE, N_CANBLOAT,
       N_STEROIDS, N_COUNT };
static const wchar_t* const NAMES[N_COUNT] = { L"Physics", L"MountParent", L"bAnimCutScene", L"CurrentOp", L"bTeleporting",
                                              L"bNoDamage", L"bHidden", L"TKShield", L"DamageScaleInfo", L"Region", L"Zone",
                                              L"bNeutralZone", L"DamageAbsorbers", L"QualifierClass",
                                              L"QualifierDamageTypes", L"QualifierNotDamageTypes", L"DamageScale", L"bExpanding",
                                              L"bCollideActors", L"bProjTarget", L"bBlockActors", L"bBlockPlayers",
                                              L"bTraceShootable", L"bShootable", L"bIgnoreAimAssist", L"Health", L"HealthMin", L"Role", L"RemoteRole", L"HealthCap",
                                              L"ThisFrameDamage", L"DamageInfo", L"Damage", L"DamageType", L"Valid", L"Time",
                                              L"Level", L"GameTimeSeconds", L"Invulnerable", L"bGodMode", L"bGhostMode",
                                              L"bProtectedEgoRecharge", L"MountedActorList", L"MountedActor",
                                              L"CollisionRadius", L"CollisionHeight", L"Location", L"Velocity",
                                              L"MountMeshItem", L"BloatState", L"bCanBloat", L"bOnSteroids" };
static int s_n[N_COUNT];

static unsigned s_level;
static SbLog s_log;
static SbLog s_pickLog;   // aim's picks in a cut-scene or animated away (spawnbox_log_pick)
static int s_refusedLogged;
static int s_next;   // the round-robin's next walk index
static SbFlags s_flags;
static SbCuts s_cuts;
static SbTeles s_teles;
static SbDmgs s_dmgs;
static int s_dmgLogs, s_dmgFlipLogs, s_dmgFailLogs, s_dmgTypeLogs;   // capped per level
// The held weapon's damage type this frame (aim_damage_type, last frame's weapon) and its class chain, rebuilt on change.
enum { DT_CHAIN = 16, DT_LIST = 16 };
static const void* s_wtype;
static const void* s_chain[DT_CHAIN];
static int s_chainN;
// The held gun deals ExpanderDamage (bloat.h): each hostile pawn's bloat is read every frame meanwhile. SecondBloat is
// declared by each DLC03 class itself; its name is looked up once a frame until it exists (per level).
static int s_bloatType, s_secondName, s_bloatLogs;
static BloatTable s_bloat;
static unsigned s_bloatReads, s_bloatImmune, s_bloatDone;   // pawn-frames, per 600 frames
static int s_nohitLogs;   // capped per level
static SbProbeCap s_probe;   // the per-shot damage-path probe's caps
static const void* s_probePawn;
static int s_probeShots;
enum { HIT_SLOTS = 8 };
static struct { const void* target; int cls, self; } s_hits[HIT_SLOTS];   // aim's last bullet-trace hit per target
static int s_hitNext;
static SbWatches s_watch;   // the invincible-window watch (spawns.md section 6)
static const void* s_roleSeen[SPAWNBOX_LINES_PER_LEVEL];
static int s_roleLogs;
static int s_teleStartLogs, s_teleEndLogs;   // capped per level
static unsigned s_frameNo;   // this frame's stamp for the flags' boxes (0: none yet)
// Counted per 600 frames: flags set, refused at the cap, cleared back near, cleared at 5 s or gone; frames boxed.
static unsigned s_flagSet, s_flagFull, s_flagNear, s_flagOld, s_flagFrames;
// Counted per 600 frames: hostile pawn-frames boxed, boxes unusable, far (> SPAWNBOX_FAR_UU), skipped-while-dropping
// pawn-frames, calls refused, faults; the box calls' time.
static unsigned s_teleStarts;   // teleports begun, counted per 600 frames
static unsigned s_frames, s_boxed, s_noBox, s_far, s_skipped, s_refused, s_faults;
static float s_maxDist;
static double s_callMs;
static LARGE_INTEGER s_qpf;
// Mounted collision (mountcol.h), per pawn read this frame: its helpers in aim order, the list's size and how many
// entries collide, the helper the aim chose and that one's velocity (lastDelta, lastDt: the latest frame, for the log).
// uc: the helpers' box centre (the tie-break's middle of the body); snapsLeft: snapshots left from the last lock.
struct McSet { const void* pawn; unsigned frame; int n, listN, colliding; McHelper h[MC_MAX]; const void* chosen;
               McVel vel; float v[3]; int vn; double lastT; float lastDelta, lastDt; unsigned snapMs; float uc[3];
               int snapsLeft; };
static McSet s_mc[MC_PAWNS];
static int s_mcSnaps, s_mcEventLogs;   // snapshots and lock/fire lines a level
// 10 snapshots per lock (the level's 10 ran out before the fight, 2026-10-10); MC_SNAPS_PER_LEVEL bounds them all.
enum { MC_SNAPS_PER_LOCK = 10, MC_SNAPS_PER_LEVEL = 60, MC_EVENT_LOGS_PER_LEVEL = 20 };
static unsigned s_mcFrames, s_mcHelpers, s_mcFull;   // counted per 600 frames: pawn-frames, their helpers, no slot

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

// An object's name index, 0 when it is not a valid object.
static int name_of_obj(const void* o) {
  __try {
    return o && uobj_valid(o) ? uobj_name(o) : 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

static int nm(int k) {
  if (!s_n[k]) s_n[k] = uname_find(NAMES[k]);
  return s_n[k];
}

static UProp prop_of(const void* o, int k) {
  UProp none = { 0, 0, UW_OTHER, 0, 0, 0 };
  const int n = nm(k);
  if (!o || !n) return none;
  __try {
    return uprop_get(uobj_class(o), n);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return none;
  }
}

// Byte, object (non-null: 1) and bool reads; -1 when the property does not resolve.
// A float property; false when it does not resolve or the read faults.
static bool rd_float(const void* o, int k, float* v) {
  const UProp p = prop_of(o, k);
  if (!p.ok || p.type != UW_FLOAT) return false;
  __try {
    memcpy(v, (const char*)o + p.offset, 4);
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

// The pawns whose health floor was logged this level (one line each).
static const void* s_floorSeen[SPAWNBOX_LINES_PER_LEVEL];
static int s_floorLogs;
static bool floor_new(const void* pawn) {
  for (int i = 0; i < s_floorLogs; i++)
    if (s_floorSeen[i] == pawn) return false;
  return true;
}

static bool role_new(const void* pawn) {
  for (int i = 0; i < s_roleLogs; i++)
    if (s_roleSeen[i] == pawn) return false;
  return true;
}

// The pawns whose health lock was logged this level (one line each).
static const void* s_lockSeen[SPAWNBOX_LINES_PER_LEVEL];
static int s_lockLogs;
static bool lock_new(const void* pawn) {
  for (int i = 0; i < s_lockLogs; i++)
    if (s_lockSeen[i] == pawn) return false;
  return true;
}

static int rd_byte(const void* o, int k) {
  const UProp p = prop_of(o, k);
  if (!p.ok || p.type != UW_BYTE) return -1;
  __try {
    return *((const unsigned char*)o + p.offset);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return -1;
  }
}

static int rd_set(const void* o, int k) {
  const UProp p = prop_of(o, k);
  if (!p.ok || (p.type != UW_OBJECT && p.type != UW_BOOL)) return -1;
  __try {
    if (p.type == UW_BOOL) return p.bitmask && (uw_int(o, p.offset) & (int)p.bitmask) != 0;
    return uw_ptr(o, p.offset) != 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return -1;
  }
}

// The state's name index, 0 when unreadable.
static int state_name(const void* o) {
  __try {
    const char* frame = (const char*)uw_ptr(o, UO_STATEFRAME);
    const void* st = frame ? uw_ptr(frame, SF_STATENODE) : 0;
    return st && uobj_valid(st) ? uobj_name(st) : 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

// RenderActor.GetRenderBoundingBox(out mins, out maxs) on the pawn (clsName: its class, for the log). 1 called.
static int render_box(void* pawn, int clsName, float mn[3], float mx[3]) {
  const ScallFn f = scall_find(pawn, L"GetRenderBoundingBox");
  ScallParm a, b;
  if (!f.ok || !scall_parm(f, L"mins", L"StructProperty", &a) || a.size != 12 ||
      !scall_parm(f, L"maxs", L"StructProperty", &b) || b.size != 12) {
    if (!s_refusedLogged) {
      s_refusedLogged = 1;
      np_log("spawnbox: RenderActor.GetRenderBoundingBox(mins, maxs) did not resolve on %ls (logged once); that "
             "class is not measured", uname_str(clsName));
    }
    return 0;
  }
  unsigned char parms[64];
  memset(parms, 0, sizeof parms);
  if (f.parmsSize > sizeof parms) return 0;
  LARGE_INTEGER t0, t1;
  QueryPerformanceCounter(&t0);
  const int called = scall_call(pawn, f, parms, sizeof parms);
  QueryPerformanceCounter(&t1);
  if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
  s_callMs += (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)s_qpf.QuadPart;
  if (!called) {
    ++s_refused;
    return 0;
  }
  memcpy(mn, parms + a.offset, 12);
  memcpy(mx, parms + b.offset, 12);
  return 1;
}

static void note(const WPawn& w, int kind, float dist, float dz) {
  const int st = state_name(w.obj);
  if (!spawnbox_log_new(&s_log, w.obj, st, kind)) return;
  const int phys = rd_byte(w.obj, N_PHYSICS), mount = rd_set(w.obj, N_MOUNTPARENT), cut = rd_set(w.obj, N_CUTSCENE);
  if (kind == SB_KIND_FAR)
    np_log("spawnbox: %ls state %ls phys %d mount %d cutscene %d offset %.0f dz %.0f (collision height %.0f)",
           uname_str(w.nameIdx), st ? uname_str(st) : L"?", phys, mount, cut, dist, dz, w.height);
  else
    np_log("spawnbox: skipped %ls state %ls phys %d mount %d cutscene %d attitude %d (not hostile while dropping)",
           uname_str(w.nameIdx), st ? uname_str(st) : L"?", phys, mount, cut, w.attitude);
}

// UStructProperty::Struct and UArrayProperty::Inner, both +0x78 (progress.cpp's and accuracy.cpp's citations).
static const int UP_INNER_OR_STRUCT = 0x78;
enum { DS_MAX = SB_DS_MAX_ENTRIES };

// The struct a StructProperty names, null when it is not a valid object.
static const void* struct_of(const void* prop) {
  const void* st = prop ? uw_ptr(prop, UP_INNER_OR_STRUCT) : 0;
  return st && uobj_valid(st) ? st : 0;
}

// Pawn.Region.Zone.bNeutralZone: Region is a PointRegion struct, Zone an object inside it. -1 when a step does not resolve.
static int rd_neutral(const void* pawn) {
  const UProp r = prop_of(pawn, N_REGION);
  __try {
    const void* st = r.ok && r.type == UW_STRUCT ? struct_of(r.field) : 0;
    if (!st || !nm(N_ZONE)) return -1;
    const UProp z = uprop_get(st, nm(N_ZONE));
    if (!z.ok || z.type != UW_OBJECT) return -1;
    const void* zone = uw_ptr(pawn, r.offset + z.offset);
    if (!zone) return 0;   // no zone: nothing neutral
    if (!uobj_valid(zone)) return -1;
    return rd_set(zone, N_NEUTRAL) > 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return -1;
  }
}

// Pawn.DamageAbsorbers count (a dynamic array, count at +4), -1 when unreadable.
static int rd_absorbers(const void* pawn) {
  const UProp a = prop_of(pawn, N_ABSORBERS);
  if (!a.ok || a.type != UW_ARRAY) return -1;
  __try {
    const int n = uw_int(pawn, a.offset + 4);
    return n >= 0 && n <= 4096 ? n : -1;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return -1;
  }
}

// Octabrain.AIScaleDamage is an empty override (dnAI.u): the game never applies DamageScaleInfo to an Octabrain or
// its subclasses, so their entries must not exclude them.
static int ignores_dscale(const void* pawn) {
  static int s_octa;   // 0 until the name exists (it loads with the class); looked up again on a new level serial
  static unsigned s_octaSerial;
  if (s_octaSerial != world_level_serial()) {
    s_octaSerial = world_level_serial();
    s_octa = 0;
  }
  if (s_octa <= 0) s_octa = uname_find(L"Octabrain");
  if (s_octa <= 0) return 0;
  const void* c = uobj_class(pawn);
  for (int i = 0; c && i < 32; i++, c = uw_ptr(c, US_SUPER))
    if (uobj_name(c) == s_octa) return 1;
  return 0;
}

// The weapon damage type's chain for PassDamageType: the class, then its supers. 0 entries when unset or unreadable.
static void build_chain(const void* t) {
  s_chainN = 0;
  s_bloatType = 0;
  if (!t) return;
  int names[DT_CHAIN];
  __try {
    for (const void* c = t; c && s_chainN < DT_CHAIN && uobj_valid(c); c = uw_ptr(c, US_SUPER)) {
      names[s_chainN] = uobj_name(c);
      s_chain[s_chainN++] = c;
    }
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    s_chainN = 0;
  }
  const wchar_t* str[DT_CHAIN];
  for (int i = 0; i < s_chainN; i++) str[i] = uname_str(names[i]);
  s_bloatType = bloat_type(str, s_chainN);
}

// A hostile pawn as the Expander sees it (bloat.h): bCanBloat, bOnSteroids, BloatState and its class's SecondBloat (-1
// when the class declares none), under one guard. A change is logged, capped a level.
static void bloat_read(const WPawn& w, unsigned now) {
  const int nb = nm(N_BLOATSTATE), nc = nm(N_CANBLOAT), ns = nm(N_STEROIDS);
  int can = -1, ster = -1, state = -1, second = -1;
  __try {
    const void* cls = uobj_class(w.obj);
    if (nb) {
      const UProp p = uprop_get(cls, nb);
      if (p.ok && p.type == UW_BYTE) state = *((const unsigned char*)w.obj + p.offset);
    }
    if (nc) {
      const UProp p = uprop_get(cls, nc);
      if (p.ok && p.type == UW_BOOL && p.bitmask) can = (uw_int(w.obj, p.offset) & (int)p.bitmask) != 0;
    }
    if (ns) {
      const UProp p = uprop_get(cls, ns);
      if (p.ok && p.type == UW_BOOL && p.bitmask) ster = (uw_int(w.obj, p.offset) & (int)p.bitmask) != 0;
    }
    if (s_secondName) {
      const UProp p = uprop_get(cls, s_secondName);
      if (p.ok && p.type == UW_BOOL && p.bitmask) second = (uw_int(w.obj, p.offset) & (int)p.bitmask) != 0;
    }
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return;
  }
  const int cb = bloat_can(can, ster), v = bloat_verdict(cb, state, second, 0);
  ++s_bloatReads;
  if (v == BL_IMMUNE) ++s_bloatImmune;
  else if (v == BL_DONE) ++s_bloatDone;
  if (bloat_note(&s_bloat, w.obj, now, cb, state, second) && s_bloatLogs < 16) {
    ++s_bloatLogs;
    np_log("spawnbox: Expander on %ls: bCanBloat %d, bOnSteroids %d, BloatState %d, SecondBloat %d%s -> %s",
           uname_str(w.nameIdx), can, ster, state, second, second < 0 ? " (none: one hit is all it takes)" : "",
           bloat_label(v));
  }
}

// One damage-type list of a DamageScaleInfo entry ({data, count} at p, *n its count): *in is PassDamageType's answer
// for the chain, names[] the elements' class names (0 for None or invalid). 0 when longer than DT_LIST or unreadable.
// Called under rd_dscale's guard.
static int rd_type_list(const char* p, const int* n, int* in, int names[DT_LIST]) {
  *in = 0;
  const void* const* data = (const void* const*)uw_ptr(p, 0);
  if (*n < 0 || *n > DT_LIST || (*n && (!data || !engine_readable(data, (size_t)*n * 4)))) return 0;
  const void* el[DT_LIST];
  for (int j = 0; j < *n; j++) {
    el[j] = data[j];
    names[j] = el[j] && uobj_valid(el[j]) ? uobj_name(el[j]) : 0;
  }
  *in = spawnbox_pass_type(s_chain, s_chainN, el, *n);
  return 1;
}

// " [A, B]" for a logged list (? for an element that is None or not a valid object).
static void list_str(wchar_t* buf, int cap, const int* names, int n, int read) {
  int at = _snwprintf(buf, cap, L"%ls", read ? L" [" : L" [unread");
  for (int j = 0; read && j < n && at > 0 && at < cap; j++) {
    const int k = _snwprintf(buf + at, cap - at, L"%ls%ls", j ? L", " : L"", names[j] ? uname_str(names[j]) : L"?");
    at = k < 0 ? cap : at + k;
  }
  if (at > 0 && at < cap) _snwprintf(buf + at, cap - at, L"]");
  buf[cap - 1] = 0;
}

// AIActor.DamageScaleInfo for the player as instigator with the held weapon's damage type (spawnbox_dscale_verdict;
// s_chain, empty when the type is unknown). logIt: log each entry with its type lists (the startup list).
// SB_DS_UNREAD when the array or the struct's members do not resolve or a bound fails.
static int rd_dscale(const void* pawn, int logIt, const wchar_t* who) {
  const UProp a = prop_of(pawn, N_DSI);
  if (!a.ok || a.type != UW_ARRAY || !a.field) return SB_DS_UNREAD;
  __try {
    if (ignores_dscale(pawn)) return SB_DS_OK;
    const void* player = world_player();
    const void* pcls = player ? uobj_class(player) : 0;
    const void* inner = uw_ptr(a.field, UP_INNER_OR_STRUCT);
    const void* st = inner && uobj_valid(inner) ? struct_of(inner) : 0;
    if (!pcls || !st) return SB_DS_UNREAD;
    const int es = *(const unsigned short*)((const char*)inner + UP_ELEMSIZE);
    const UProp qc = uprop_get(st, nm(N_QCLASS)), qt = uprop_get(st, nm(N_QTYPES)), qn = uprop_get(st, nm(N_QNOT)),
                sc = uprop_get(st, nm(N_DSCALE));
    if (!qc.ok || (qc.type != UW_CLASS && qc.type != UW_OBJECT) || !qt.ok || qt.type != UW_ARRAY || !qn.ok ||
        qn.type != UW_ARRAY || !sc.ok || sc.type != UW_FLOAT || es <= 0 || es > 0x400 || qc.offset < 0 ||
        qc.offset + 4 > es || qt.offset < 0 || qt.offset + 8 > es || qn.offset < 0 || qn.offset + 8 > es ||
        sc.offset < 0 || sc.offset + 4 > es) {
      if (s_dmgFailLogs++ < 2)
        np_log("spawnbox: DamageScaleInfo members did not resolve (QualifierClass %d, types %d, not-types %d, "
               "DamageScale %d, element %d)", qc.ok, qt.ok, qn.ok, sc.ok, es);
      return SB_DS_UNREAD;
    }
    const char* data = (const char*)uw_ptr(pawn, a.offset);
    const int n = uw_int(pawn, a.offset + 4);
    if (n < 0 || n > DS_MAX) return SB_DS_UNREAD;
    if (n == 0) return SB_DS_OK;
    if (!data || !engine_readable(data, (size_t)n * es)) return SB_DS_UNREAD;
    SbDsEntry e[DS_MAX];
    for (int i = 0; i < n; i++) {
      const char* p = data + (size_t)i * es;
      const void* q = uw_ptr(p, qc.offset);
      if (q && !uobj_valid(q)) return SB_DS_UNREAD;
      e[i].classMatch = !q || uobj_class_isa(pcls, q);
      e[i].nTypes = uw_int(p, qt.offset + 4);
      e[i].nNotTypes = uw_int(p, qn.offset + 4);
      memcpy(&e[i].scale, p + sc.offset, 4);
      int tn[DT_LIST], nn[DT_LIST];
      const int tRead = rd_type_list(p + qt.offset, &e[i].nTypes, &e[i].inTypes, tn);
      const int nRead = rd_type_list(p + qn.offset, &e[i].nNotTypes, &e[i].inNot, nn);
      e[i].typeKnown = s_chainN > 0 && tRead && nRead;   // an unreadable list: the type's answer is unknown
      if (logIt && s_dmgLogs < 16) {
        ++s_dmgLogs;
        wchar_t tl[256], nl[256];
        list_str(tl, 256, tn, e[i].nTypes, tRead);
        list_str(nl, 256, nn, e[i].nNotTypes, nRead);
        np_log("spawnbox: damage scale %ls [%d/%d]: QualifierClass %ls, %d type(s)%ls, %d not-type(s)%ls, DamageScale %.2f%s",
               who, i, n, q ? uname_str(uobj_name(q)) : L"None", e[i].nTypes, tl, e[i].nNotTypes, nl, e[i].scale,
               e[i].classMatch ? " (matches the player)" : "");
      }
    }
    return spawnbox_dscale_verdict(e, n);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return SB_DS_UNREAD;
  }
}

// The pawn's player-damage verdict and zone flag, read when first seen and every SPAWNBOX_DMG_REREAD frames; a change
// after the first read is logged (a script flipping it).
static void dmg_read(const WPawn& w, unsigned now) {
  int due = 0, v0 = SB_DS_UNREAD, n0 = -1;
  const void* t0 = 0;
  const int had = spawnbox_dmg_get(&s_dmgs, w.obj, now, &v0, &n0, &t0);
  const int at = spawnbox_dmg_slot(&s_dmgs, w.obj, now, s_frameNo, &due, s_wtype);
  if (at < 0 || !due) return;
  SbDmg& d = s_dmgs.d[at];
  const int first = d.verdict == SB_DS_UNREAD && d.neutral == -1;
  const int v = rd_dscale(w.obj, first, uname_str(w.nameIdx)), nz = rd_neutral(w.obj);
  // A weapon switch re-reads every pawn: a verdict it changes is the weapon's, logged apart from a script flip.
  if (!first && had && t0 != s_wtype) {
    if (v != d.verdict && s_dmgTypeLogs < SPAWNBOX_LINES_PER_LEVEL) {
      ++s_dmgTypeLogs;
      np_log("spawnbox: %ls with damage type %ls: DamageScaleInfo verdict %d -> %d%s", uname_str(w.nameIdx),
             s_chainN ? uname_str(name_of_obj(s_chain[0])) : L"unknown", d.verdict, v,
             v == SB_DS_TYPE_ZERO ? " (wrong damage type: left out)" : "");
    }
  } else if (!first && (v != d.verdict || nz != d.neutral) && s_dmgFlipLogs < SPAWNBOX_LINES_PER_LEVEL) {
    ++s_dmgFlipLogs;
    np_log("spawnbox: %ls player-damage flags changed: DamageScaleInfo verdict %d -> %d, bNeutralZone %d -> %d",
           uname_str(w.nameIdx), d.verdict, v, d.neutral, nz);
  }
  d.verdict = v;
  d.neutral = nz;
}

// A hostile pawn's teleport and no-damage signals (CurrentOp, bTeleporting, bHidden, bNoDamage), noted for
// spawnbox_excluded. Logs the first few starts and ends a level: which signals fired, and how long the pawn stayed out.
// While its window is open (spawnbox_watch_begin) every change of the tuple is logged too.
static void tele_read(const WPawn& w, unsigned now) {
  dmg_read(w, now);
  const int op = rd_byte(w.obj, N_CUROP), tele = rd_set(w.obj, N_TELEPORTING), hid = rd_set(w.obj, N_HIDDEN);
  float hp = 0.f, hpMin = 0.f;
  const int floored = rd_float(w.obj, N_HEALTH, &hp) && rd_float(w.obj, N_HEALTHMIN, &hpMin) &&
                      spawnbox_health_floored(hp, hpMin);
  if (floored && s_floorLogs < SPAWNBOX_LINES_PER_LEVEL && floor_new(w.obj)) {
    s_floorSeen[s_floorLogs++] = w.obj;
    np_log("spawnbox: %ls health held at its floor: Health %.1f, HealthMin %.1f (damage cannot lower it); left out",
           uname_str(w.nameIdx), hp, hpMin);
  }
  const int role = rd_byte(w.obj, N_ROLE);
  const int roleBlocks = spawnbox_role_blocks(role);
  if (roleBlocks && s_roleLogs < SPAWNBOX_LINES_PER_LEVEL && role_new(w.obj)) {
    s_roleSeen[s_roleLogs++] = w.obj;
    np_log("spawnbox: %ls Role %d RemoteRole %d: TakeDamage returns before any damage; left out", uname_str(w.nameIdx),
           role, rd_byte(w.obj, N_REMOTEROLE));
  }
  const int inv = rd_set(w.obj, N_INVULN), god = rd_set(w.obj, N_GODMODE), ghost = rd_set(w.obj, N_GHOSTMODE),
            pego = rd_set(w.obj, N_PROTEGO);
  const int locked = spawnbox_health_locked(inv, god, ghost, pego);
  if (locked && s_lockLogs < SPAWNBOX_LINES_PER_LEVEL && lock_new(w.obj)) {
    s_lockSeen[s_lockLogs++] = w.obj;
    np_log("spawnbox: %ls health locked: Invulnerable %d bGodMode %d bGhostMode %d bProtectedEgoRecharge %d (hits register, "
           "Health keeps); left out", uname_str(w.nameIdx), inv, god, ghost, pego);
  }
  const int nod = rd_set(w.obj, N_NODAMAGE);
  const int sig = spawnbox_tele_signals(op, tele, hid, nod > 0 || floored || roleBlocks || locked);
  int isNew = 0;
  const int at = spawnbox_watch_begin(&s_watch, w.obj, now, sig != 0, &isNew);
  if (at >= 0) {
    SbTuple t;
    t.state = state_name(w.obj);
    t.op = op;
    t.nodamage = nod;
    t.hidden = hid;
    t.teleporting = tele;
    t.shield = rd_set(w.obj, N_TKSHIELD);
    t.health = w.health;
    int dv = SB_DS_UNREAD, nz = -1;
    spawnbox_dmg_get(&s_dmgs, w.obj, now, &dv, &nz);
    t.dscale = dv;
    t.neutral = nz;
    t.absorbers = rd_absorbers(w.obj);
    t.expanding = rd_set(w.obj, N_EXPANDING);
    t.collide = rd_set(w.obj, N_COLLIDE);
    t.projtarget = rd_set(w.obj, N_PROJTARGET);
    t.role = role;
    unsigned since = 0;
    if (spawnbox_watch_change(&s_watch, at, isNew, t, now, &since))
      np_log("spawnbox: watch %ls +%u ms: state %ls op %d bNoDamage %d bHidden %d bTeleporting %d TKShield %d health %d "
             "DamageScaleInfo %d neutral zone %d absorbers %d bExpanding %d bCollideActors %d bProjTarget %d Role %d",
             uname_str(w.nameIdx), since, t.state ? uname_str(t.state) : L"?", t.op, t.nodamage, t.hidden, t.teleporting,
             t.shield, t.health, t.dscale, t.neutral, t.absorbers, t.expanding, t.collide, t.projtarget, t.role);
  }
  unsigned held = 0;
  const int ev = spawnbox_tele_note(&s_teles, w.obj, sig, now, &held);
  if (ev == SB_TELE_EV_START) {
    ++s_teleStarts;
    if (s_teleStartLogs < SPAWNBOX_LINES_PER_LEVEL) {
      ++s_teleStartLogs;
      np_log("spawnbox: teleporting %ls excluded: op %d (teleport %d), bTeleporting %d, bNoDamage %d, bHidden %d",
             uname_str(w.nameIdx), op, op == SB_AIOP_TELEPORT, tele, nod, hid);
    }
  } else if (ev == SB_TELE_EV_END && s_teleEndLogs < SPAWNBOX_LINES_PER_LEVEL) {
    ++s_teleEndLogs;
    np_log("spawnbox: teleporting %ls back as a target after %u ms", uname_str(w.nameIdx), held);
  }
}

// One pawn's box against its Location: -1 no usable box, 0 near, 1 far; c and h the box's centre and half extents.
static int measure(const WPawn& w, float c[3], float h[3], float* dist, float* dz) {
  const float loc[3] = { w.x, w.y, w.z };   // the walk's Location
  float mn[3], mx[3];
  if (!render_box((void*)w.obj, w.nameIdx, mn, mx)) return -1;
  ++s_boxed;
  if (!spawnbox_offset(mn, mx, loc, dist, dz)) {
    ++s_noBox;
    return -1;
  }
  for (int k = 0; k < 3; k++) {
    c[k] = (mn[k] + mx[k]) * 0.5f;
    h[k] = (mx[k] - mn[k]) * 0.5f;
  }
  if (*dist > s_maxDist) s_maxDist = *dist;
  return spawnbox_far(*dist) ? 1 : 0;
}

static int walk_index(const WPawn* pw, int n, const void* p) {
  for (int i = 0; p && i < n; i++)
    if (pw[i].obj == p) return i;
  return -1;
}

static const void* rd_obj(const void* o, int k);   // below, with the probe's reads

static double qpc_s(void) {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
  return (double)t.QuadPart / (double)s_qpf.QuadPart;
}

// A 12-byte struct property (a vector); false when it does not resolve or the read faults.
static bool rd_vec3(const void* o, int k, float out[3]) {
  const UProp p = prop_of(o, k);
  if (!p.ok || p.type != UW_STRUCT || p.elemSize < 12) return false;
  __try {
    memcpy(out, (const char*)o + p.offset, 12);
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

static int class_name(const void* o) {
  __try {
    return o && uobj_valid(o) ? uobj_name(uobj_class(o)) : 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

// o's TakeDamage is declared by o's own class (EnergyLeech_Boss_HurtRadiusSensor's forwards explosions to the boss):
// a damage taker. *decl: the declaring class.
static int own_take_damage(const void* o, const void** decl) {
  *decl = 0;
  const ScallFn f = scall_find((void*)o, L"TakeDamage");
  if (!f.ok) return 0;
  __try {
    *decl = uobj_outer(f.fn);
    return *decl && *decl == uobj_class(o);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    *decl = 0;
    return 0;
  }
}

// The set of pawn; add: take a slot not read since the last frame. Null when none.
static McSet* mc_find(const void* pawn, int add) {
  for (int i = 0; i < MC_PAWNS; i++)
    if (s_mc[i].pawn == pawn) return &s_mc[i];
  if (!add) return 0;
  for (int i = 0; i < MC_PAWNS; i++)
    if (!s_mc[i].pawn || s_frameNo - s_mc[i].frame > 1) {   // unsigned: safe across the wrap
      memset(&s_mc[i], 0, sizeof s_mc[i]);
      s_mc[i].pawn = pawn;
      return &s_mc[i];
    }
  return 0;
}

// The set read this frame with at least one helper.
static const McSet* mc_live(const void* pawn) {
  const McSet* s = pawn ? mc_find(pawn, 0) : 0;
  return s && s_frameNo && s->frame == s_frameNo && s->n > 0 ? s : 0;
}

static int mc_chosen(const McSet* s) {
  for (int i = 0; i < s->n; i++)
    if (s->h[i].obj == s->chosen) return i;
  return 0;
}

// The pawn's MountedActorList (Actor.SMountedActor, MountedActor by name; the array validated once), its entries whose
// MountParent is the pawn, kept when mc_is_helper; then sorted into aim order.
static void mc_read(McSet* s, const WPawn* pw, int np, const WPawn& w) {
  const int had = s->n;
  s->n = 0;
  s->listN = -1;
  s->colliding = 0;
  const void* objs[MC_LIST_MAX];
  int k = 0;
  const UProp a = prop_of(w.obj, N_MOUNTLIST);
  if (a.ok && a.type == UW_ARRAY && a.field && nm(N_MOUNTED)) {
    __try {
      const void* inner = uw_ptr(a.field, UP_INNER_OR_STRUCT);
      const void* st = inner && uobj_valid(inner) ? struct_of(inner) : 0;
      const int es = st ? *(const unsigned short*)((const char*)inner + UP_ELEMSIZE) : 0;
      UProp m = { 0, 0, UW_OTHER, 0, 0, 0 };
      if (st) m = uprop_get(st, nm(N_MOUNTED));
      const char* data = (const char*)uw_ptr(w.obj, a.offset);
      const int cnt = uw_int(w.obj, a.offset + 4);
      if (m.ok && m.type == UW_OBJECT && es > 0 && es <= 0x100 && m.offset >= 0 && m.offset + 4 <= es && cnt >= 0 &&
          cnt <= 4096) {
        s->listN = cnt;
        k = cnt < MC_LIST_MAX ? cnt : MC_LIST_MAX;
        if (k && (!data || !engine_readable(data, (size_t)k * es))) k = 0;
        for (int i = 0; i < k; i++) objs[i] = uw_ptr(data + (size_t)i * es, m.offset);
      }
    } __except (av_only(GetExceptionCode())) {
      ++s_faults;
      k = 0;
    }
  }
  McHelper all[MC_MAX];
  int na = 0;
  for (int i = 0; i < k && na < MC_MAX; i++) {
    const void* o = objs[i];
    const int cls = o && o != w.obj ? class_name(o) : 0;
    if (!cls || rd_obj(o, N_MOUNTPARENT) != w.obj) continue;
    const int col = rd_set(o, N_COLLIDE);
    if (col > 0) ++s->colliding;
    float r = 0.f, h = 0.f, c[3];
    if (!rd_float(o, N_CRADIUS, &r) || !rd_float(o, N_CHEIGHT, &h) || !rd_vec3(o, N_LOCATION, c) ||
        !mc_is_helper(col, r, h, walk_index(pw, np, o) >= 0))
      continue;
    McHelper& e = all[na++];
    e.obj = o;
    memcpy(e.c, c, sizeof c);
    e.r = r;
    e.h = h;
    const void* decl = 0;
    e.taker = own_take_damage(o, &decl);
    e.name = cls;
  }
  float uh[3];
  const int haveC = mc_union(all, na, s->uc, uh);   // the middle of the body, for the tie-break
  int ord[MC_MAX];
  const int no = mc_order(all, na, haveC ? s->uc : 0, ord);
  for (int i = 0; i < no; i++) s->h[i] = all[ord[i]];
  s->n = no;
  s->frame = s_frameNo;
  if ((had > 0) != (no > 0)) aim_vis_forget(w.obj);   // the cached verdict was for another aim point
}

// The chosen helper's (else the first's) position this frame into its velocity.
static void mc_track(McSet* s) {
  const double t = qpc_s();
  const float dt = s->lastT > 0.0 ? (float)(t - s->lastT) : 0.f;
  s->lastT = t;
  if (!s->n) {
    s->vn = 0;
    s->v[0] = s->v[1] = s->v[2] = 0.f;
    return;
  }
  const McHelper& e = s->h[mc_chosen(s)];
  if (s->vel.obj == e.obj) {
    const float d[3] = { e.c[0] - s->vel.last[0], e.c[1] - s->vel.last[1], e.c[2] - s->vel.last[2] };
    s->lastDelta = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    s->lastDt = dt;
  }
  s->vn = mc_vel_step(&s->vel, e.obj, e.c, dt, s->v);
}

// Actor.GetCollisionBounds(bLocalSpace=false): the actor's collision as a world box (for the log only). 1 read.
static int collision_bounds(const void* o, float c[3], float h[3]) {
  const ScallFn f = scall_find((void*)o, L"GetCollisionBounds");
  ScallParm ls, rv;
  if (!f.ok || !scall_parm(f, L"bLocalSpace", L"BoolProperty", &ls) || ls.size != 4 ||
      !scall_parm(f, L"ReturnValue", L"StructProperty", &rv) || rv.size < 24)
    return 0;
  unsigned char parms[64];
  memset(parms, 0, sizeof parms);
  if (f.parmsSize > sizeof parms || rv.offset + 24 > f.parmsSize || !scall_call((void*)o, f, parms, sizeof parms))
    return 0;
  float mn[3], mx[3], dist = 0.f, dz = 0.f;
  memcpy(mn, parms + rv.offset, 12);
  memcpy(mx, parms + rv.offset + 12, 12);
  const float origin[3] = { 0.f, 0.f, 0.f };
  if (!spawnbox_offset(mn, mx, origin, &dist, &dz)) return 0;
  for (int k = 0; k < 3; k++) {
    c[k] = (mn[k] + mx[k]) * 0.5f;
    h[k] = (mx[k] - mn[k]) * 0.5f;
  }
  return 1;
}

// Actor.MountMeshItem (a NameProperty: the bone it rides), null when unread.
static const wchar_t* mount_item(const void* o) {
  int bone = 0;
  const UProp mi = prop_of(o, N_MOUNTITEM);
  if (!mi.ok || mi.type != UW_OTHER || mi.elemSize != 4) return 0;
  __try {
    bone = uw_int(o, mi.offset);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
  return bone > 0 ? uname_str(bone) : 0;
}

// The measurement (spawns.md section 8): once a second while the pawn has helpers and is not hidden or invulnerable,
// MC_SNAPS_PER_LEVEL a level. The pawn's Location, cylinder, Velocity and render box; each helper's place, size, flags,
// bone, TakeDamage's class and collision bounds; the aim's helper and its velocity against its last frame's delta.
static void mc_snapshot(McSet* s, const WPawn& w, unsigned now) {
  if (s->snapsLeft <= 0 || s_mcSnaps >= MC_SNAPS_PER_LEVEL || (s->snapMs && now - s->snapMs < 1000) ||
      spawnbox_tele_sig(&s_teles, w.obj, now))
    return;
  s->snapMs = now ? now : 1;
  --s->snapsLeft;
  ++s_mcSnaps;
  float vel[3] = { 0.f, 0.f, 0.f }, bc[3] = { 0.f, 0.f, 0.f }, bh[3] = { 0.f, 0.f, 0.f };
  float uc[3] = { 0.f, 0.f, 0.f }, uh[3] = { 0.f, 0.f, 0.f };
  const int haveVel = rd_vec3(w.obj, N_VELOCITY, vel), box = spawnbox_render_box(w.obj, bc, bh);
  mc_union(s->h, s->n, uc, uh);
  const float bx = bc[0] - w.x, by = bc[1] - w.y, bz = bc[2] - w.z;
  const McHelper& ch = s->h[mc_chosen(s)];
  np_log("spawnbox: mounted collision of %ls: Location %.0f,%.0f,%.0f, collision radius %.0f height %.0f, Velocity %s"
         "%.0f,%.0f,%.0f; render box %s centre %.0f,%.0f,%.0f half %.0f,%.0f,%.0f (%.0f from Location); "
         "MountedActorList %d, colliding %d, helpers %d, their box centre %.0f,%.0f,%.0f half %.0f,%.0f,%.0f; aim at %ls "
         "(%s), its velocity %.0f,%.0f,%.0f from %d frame(s), last frame %.1f units in %.1f ms",
         uname_str(w.nameIdx), w.x, w.y, w.z, w.radius, w.height, haveVel ? "" : "unread ", vel[0], vel[1], vel[2],
         box ? "read" : "unread", bc[0], bc[1], bc[2], bh[0], bh[1], bh[2], sqrtf(bx * bx + by * by + bz * bz),
         s->listN, s->colliding, s->n, uc[0], uc[1], uc[2], uh[0], uh[1], uh[2], uname_str(ch.name),
         s->chosen == ch.obj ? "the probe's choice" : "first in aim order", s->v[0], s->v[1], s->v[2], s->vn,
         s->lastDelta, s->lastDt * 1000.f);
  for (int i = 0; i < s->n; i++) {
    const McHelper& e = s->h[i];
    const void* decl = 0;
    own_take_damage(e.obj, &decl);
    const wchar_t* boneName = mount_item(e.obj);
    float cc[3] = { 0.f, 0.f, 0.f }, chh[3] = { 0.f, 0.f, 0.f };
    const int cb = collision_bounds(e.obj, cc, chh);
    const float dx = e.c[0] - w.x, dy = e.c[1] - w.y, dz = e.c[2] - w.z;
    const int declName = decl ? name_of_obj(decl) : 0;
    np_log("spawnbox:   helper %d %ls at %.0f,%.0f,%.0f (%.0f from Location), radius %.0f height %.0f, bCollideActors %d "
           "bBlockActors %d bBlockPlayers %d bProjTarget %d bHidden %d, MountMeshItem %ls, TakeDamage of %ls%s; "
           "collision bounds %s centre %.0f,%.0f,%.0f half %.0f,%.0f,%.0f", i, uname_str(e.name), e.c[0], e.c[1], e.c[2],
           sqrtf(dx * dx + dy * dy + dz * dz), e.r, e.h, rd_set(e.obj, N_COLLIDE), rd_set(e.obj, N_BLOCKACT),
           rd_set(e.obj, N_BLOCKPLY), rd_set(e.obj, N_PROJTARGET), rd_set(e.obj, N_HIDDEN),
           boneName ? boneName : L"?", declName ? uname_str(declName) : L"?", e.taker ? " (its own: a taker)" : "",
           cb ? "read" : "unread", cc[0], cc[1], cc[2], chh[0], chh[1], chh[2]);
  }
}

// A hostile pawn with collision 0 or tiny: its helpers this frame, their motion, and the measurement.
static void mc_update(const WPawn* pw, int np, const WPawn& w, unsigned now) {
  McSet* s = mc_find(w.obj, 1);
  if (!s) {
    ++s_mcFull;
    return;
  }
  mc_read(s, pw, np, w);
  mc_track(s);
  if (!s->n) return;
  ++s_mcFrames;
  s_mcHelpers += (unsigned)s->n;
  mc_snapshot(s, w, now);
}

void spawnbox_frame(int on) {
  const unsigned level = world_level_serial();
  if (level != s_level) {
    s_level = level;
    memset(&s_log, 0, sizeof s_log);
    memset(&s_pickLog, 0, sizeof s_pickLog);
    memset(&s_flags, 0, sizeof s_flags);   // the last level's pawns: forgotten unread
    memset(&s_cuts, 0, sizeof s_cuts);
    memset(&s_teles, 0, sizeof s_teles);
    memset(&s_watch, 0, sizeof s_watch);
    memset(&s_dmgs, 0, sizeof s_dmgs);
    memset(&s_bloat, 0, sizeof s_bloat);
    s_secondName = s_bloatLogs = 0;
    s_nohitLogs = 0;
    s_floorLogs = s_roleLogs = s_lockLogs = 0;
    memset(&s_probe, 0, sizeof s_probe);
    memset(s_hits, 0, sizeof s_hits);
    s_probePawn = 0;
    s_dmgLogs = s_dmgFlipLogs = s_dmgFailLogs = s_dmgTypeLogs = 0;
    s_teleStartLogs = s_teleEndLogs = 0;
    memset(s_mc, 0, sizeof s_mc);
    s_mcSnaps = s_mcEventLogs = 0;
  }
  if (!on) {
    s_flags.n = s_cuts.n = s_teles.n = s_watch.n = s_dmgs.n = s_bloat.n = 0;
    memset(s_mc, 0, sizeof s_mc);
  }
  if (on) {
    if (++s_frameNo == 0) s_frameNo = 1;
    const unsigned now = GetTickCount();
    const void* wt = aim_damage_type();
    if (wt != s_wtype) {
      s_wtype = wt;
      build_chain(wt);
    }
    const WPawn* pw = 0;
    const int n = world_pawns(&pw);
    if (s_bloatType && !s_secondName) s_secondName = uname_find(L"SecondBloat");   // loads with the DLC03 classes
    // The skip check is two property reads: every pawn, every frame; a hostile pawn's teleport signals three.
    for (int i = 0; i < n; i++) {
      const WPawn& w = pw[i];
      if (w.obj && w.alive && w.enemy) tele_read(w, now);
      if (w.obj && w.alive && w.enemy && s_bloatType) bloat_read(w, now);
      if (w.obj && w.alive && w.enemy && mc_tiny(w.radius, w.height)) mc_update(pw, n, w, now);
      if (w.obj && w.alive && !w.enemy &&
          spawnbox_skipped(w.alive, w.enemy, rd_byte(w.obj, N_PHYSICS), rd_set(w.obj, N_MOUNTPARENT) > 0)) {
        ++s_skipped;
        note(w, SB_KIND_SKIPPED, 0.f, 0.f);
      }
    }
    // The flagged pawns: read every frame while their body stays away (spawnbox_flag_keep), at most SPAWNBOX_FLAGS.
    for (int f = s_flags.n - 1; f >= 0; f--) {
      SbFlag& fl = s_flags.f[f];
      const int i = walk_index(pw, n, fl.pawn);
      float c[3], h[3], dist = 0.f, dz = 0.f;
      const int m = i >= 0 && pw[i].alive && pw[i].enemy ? measure(pw[i], c, h, &dist, &dz) : -1;
      if (i >= 0) spawnbox_cut_note(&s_cuts, fl.pawn, rd_set(fl.pawn, N_CUTSCENE) > 0, now);
      if (!spawnbox_flag_keep(fl, m >= 0, m > 0, now)) {
        if (m == 0) ++s_flagNear;
        else ++s_flagOld;
        aim_vis_forget(fl.pawn);   // its aim point goes back to the body points
        spawnbox_flag_drop(&s_flags, f);
        continue;
      }
      memcpy(fl.c, c, sizeof c);
      memcpy(fl.h, h, sizeof h);
      fl.frame = s_frameNo;
      ++s_far;
      ++s_flagFrames;
      note(pw[i], SB_KIND_FAR, dist, dz);
    }
    // Detection: SPAWNBOX_CALLS_PER_FRAME ProcessEvent calls a frame, round-robin from where the last frame ended,
    // past the flagged pawns (read above). A far one is flagged, its box used from this frame.
    int calls = 0, j = 0;
    if (s_next >= n) s_next = 0;
    for (; j < n && calls < SPAWNBOX_CALLS_PER_FRAME; j++) {
      const WPawn& w = pw[(s_next + j) % n];
      if (!w.obj || !w.alive || !w.enemy || spawnbox_flag_find(&s_flags, w.obj) >= 0) continue;
      ++calls;
      spawnbox_cut_note(&s_cuts, w.obj, rd_set(w.obj, N_CUTSCENE) > 0, now);   // with the box, never more often
      float c[3], h[3], dist = 0.f, dz = 0.f;
      if (measure(w, c, h, &dist, &dz) <= 0) continue;
      ++s_far;
      note(w, SB_KIND_FAR, dist, dz);
      const int f = spawnbox_flag_set(&s_flags, w.obj, now);
      if (f < 0) {
        ++s_flagFull;
        continue;
      }
      ++s_flagSet;
      ++s_flagFrames;
      aim_vis_forget(w.obj);   // a verdict cached at Location no longer holds: the aim moves to the box
      memcpy(s_flags.f[f].c, c, sizeof c);
      memcpy(s_flags.f[f].h, h, sizeof h);
      s_flags.f[f].frame = s_frameNo;
    }
    s_next = n ? (s_next + j) % n : 0;
  }
  uname_dlog_flush();
  if (++s_frames < 600) return;
  s_frames = 0;
  if (s_boxed || s_skipped || s_refused || s_mcFrames)
    np_log("spawnbox: 600 frames: %u hostile box(es) (avg %.3f ms), %u unusable, %u far (> %d units; max %.0f), "
           "%u skipped while dropping; flags: %u set, %u refused at the cap, %u back near, %u at 5 s or gone, %u "
           "frame(s) boxed at the body, %d now; %d line(s) this level; %u teleport(s) begun; mounted collision %u pawn-frame(s) "
           "(%u helper(s), %u without a slot); %u call(s) refused, %u fault(s)", s_boxed,
           s_boxed ? s_callMs / s_boxed : 0.0, s_noBox, s_far, (int)SPAWNBOX_FAR_UU, s_maxDist, s_skipped,
           s_flagSet, s_flagFull, s_flagNear, s_flagOld, s_flagFrames, s_flags.n, s_log.lines, s_teleStarts, s_mcFrames,
           s_mcHelpers, s_mcFull, s_refused, s_faults);
  if (s_bloatReads)
    np_log("spawnbox: Expander, 600 frames: %u hostile pawn-frame(s) read, cannot bloat %u, fully expanded or popping %u "
           "(left out)", s_bloatReads, s_bloatImmune, s_bloatDone);
  s_bloatReads = s_bloatImmune = s_bloatDone = 0;
  s_boxed = s_noBox = s_far = s_skipped = s_refused = s_faults = s_teleStarts = 0;
  s_mcFrames = s_mcHelpers = s_mcFull = 0;
  s_flagSet = s_flagFull = s_flagNear = s_flagOld = s_flagFrames = 0;
  s_maxDist = 0.f;
  s_callMs = 0.0;
}

int spawnbox_excluded(const void* pawn) {
  const int sig = spawnbox_tele_sig(&s_teles, pawn, GetTickCount());
  int dv = SB_DS_UNREAD, nz = -1;
  const void* wt = 0;
  spawnbox_dmg_get(&s_dmgs, pawn, GetTickCount(), &dv, &nz, &wt);
  float c[3], h[3];
  int bc = 1, bs = -1, b2 = -1;
  const int bv = spawnbox_bloat(pawn, &bc, &bs, &b2) ? bloat_verdict(bc, bs, b2, 0) : BL_SHOOT;
  return spawnbox_exclusion(spawnbox_flag_find(&s_flags, pawn) >= 0, spawnbox_cut_has(&s_cuts, pawn, GetTickCount()),
                           (sig & SB_TELE_MOVING) != 0, (sig & SB_TELE_NODAMAGE) != 0, dv == SB_DS_ZERO, nz > 0,
                           overkill_doomed(pawn),
                           spawnbox_dmg_type_excludes(dv, wt, aim_damage_type()) || bv == BL_IMMUNE,
                           spawnbox_body(pawn, c, h), bv == BL_DONE);
}

int spawnbox_bloat(const void* pawn, int* can, int* state, int* second) {
  return s_bloatType && bloat_get(&s_bloat, pawn, GetTickCount(), can, state, second);
}

int spawnbox_in_cutscene(const void* pawn) { return spawnbox_cut_has(&s_cuts, pawn, GetTickCount()); }

void spawnbox_log_pick(const void* pawn, int usedBox) {
  const int cut = spawnbox_in_cutscene(pawn), f = spawnbox_flag_find(&s_flags, pawn);
  if (!pawn || (!cut && f < 0 && usedBox != MC_SRC_HELPERS)) return;
  const WPawn* pw = 0;
  const int n = world_pawns(&pw);
  const int i = walk_index(pw, n, pawn);
  if (i < 0) return;
  const int st = state_name(pawn);
  if (!spawnbox_log_new(&s_pickLog, pawn, st, usedBox)) return;
  float dist = -1.f, dz = 0.f;   // -1: not flagged, the body within SPAWNBOX_FAR_UU of Location
  if (f >= 0) {
    const float d[3] = { s_flags.f[f].c[0] - pw[i].x, s_flags.f[f].c[1] - pw[i].y, s_flags.f[f].c[2] - pw[i].z };
    dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    dz = d[2];
  }
  np_log("spawnbox: target %ls picked: cut-scene %d, state %ls, offset %.0f dz %.0f (-1: near), aim at %s",
         uname_str(pw[i].nameIdx), cut, st ? uname_str(st) : L"?", dist, dz,
         usedBox == MC_SRC_HELPERS ? "its mounted collision" : usedBox == MC_SRC_BOX ? "the render box" : "Location");
}

int spawnbox_body(const void* pawn, float centre[3], float half[3]) {
  const McSet* s = mc_live(pawn);
  float hc[3], hh[3];
  const int helpers = s && mc_union(s->h, s->n, hc, hh);
  const int f = spawnbox_flag_find(&s_flags, pawn);
  const int box = f >= 0 && s_frameNo && s_flags.f[f].frame == s_frameNo;
  const float* bc = box ? s_flags.f[f].c : hc;
  const float* bh = box ? s_flags.f[f].h : hh;
  return mc_body(helpers, hc, hh, box, bc, bh, centre, half) != MC_SRC_NONE;
}

int spawnbox_helpers(const void* pawn, McHelper out[MC_MAX], float centre[3], int* chosen) {
  const McSet* s = mc_live(pawn);
  if (!s) return 0;
  memcpy(out, s->h, sizeof(McHelper) * (size_t)s->n);
  if (centre) memcpy(centre, s->uc, 12);
  if (chosen) {
    *chosen = -1;
    for (int i = 0; s->chosen && i < s->n; i++)
      if (s->h[i].obj == s->chosen) *chosen = i;
  }
  return s->n;
}

void spawnbox_helper_event(const void* pawn, const char* event, int lock) {
  McSet* s = pawn ? mc_find(pawn, 0) : 0;
  if (!s || !s_frameNo || s->frame != s_frameNo || !s->n) return;
  if (lock) s->snapsLeft = MC_SNAPS_PER_LOCK;
  if (s_mcEventLogs >= MC_EVENT_LOGS_PER_LEVEL) return;
  ++s_mcEventLogs;
  const int i = mc_chosen(s);
  const McHelper& e = s->h[i];
  const wchar_t* bone = mount_item(e.obj);
  const float d[3] = { e.c[0] - s->uc[0], e.c[1] - s->uc[1], e.c[2] - s->uc[2] };
  np_log("spawnbox: %s on %ls at its mounted collision: helper %d of %d (%s) %ls MountMeshItem %ls at %.0f,%.0f,%.0f "
         "(%.0f from the helpers' centre), lead velocity %.0f,%.0f,%.0f (%.0f u/s) from %d move(s), last frame %.1f units "
         "in %.1f ms", event, uname_str(class_name(pawn)), i, s->n, s->chosen == e.obj ? "the probe's choice" :
         "first in aim order", uname_str(e.name), bone ? bone : L"?", e.c[0], e.c[1], e.c[2],
         sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), s->v[0], s->v[1], s->v[2],
         sqrtf(s->v[0] * s->v[0] + s->v[1] * s->v[1] + s->v[2] * s->v[2]), s->vn, s->lastDelta, s->lastDt * 1000.f);
}

void spawnbox_helper_choose(const void* pawn, const void* helper) {
  McSet* s = pawn ? mc_find(pawn, 0) : 0;
  if (s) s->chosen = helper;
}

int spawnbox_helper_point(const void* pawn, float out[3]) {
  const McSet* s = mc_live(pawn);
  if (!s) return 0;
  memcpy(out, s->h[mc_chosen(s)].c, 12);
  return 1;
}

int spawnbox_helper_vel(const void* pawn, float out[3]) {
  const McSet* s = mc_live(pawn);
  if (!s) return 0;
  memcpy(out, s->v, 12);
  return 1;
}

int spawnbox_render_box(const void* pawn, float centre[3], float half[3]) {
  int cls = 0;
  __try {
    cls = pawn && uobj_valid(pawn) ? uobj_name(uobj_class(pawn)) : 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
  }
  float mn[3], mx[3], dist = 0.f, dz = 0.f;
  const float origin[3] = { 0.f, 0.f, 0.f };   // spawnbox_offset only for its usable-box test here
  if (!cls || !render_box((void*)pawn, cls, mn, mx) || !spawnbox_offset(mn, mx, origin, &dist, &dz)) return 0;
  for (int k = 0; k < 3; k++) {
    centre[k] = (mn[k] + mx[k]) * 0.5f;
    half[k] = (mx[k] - mn[k]) * 0.5f;
  }
  return 1;
}

void spawnbox_note_hit(const void* target, int hitCls, int self) {
  int at = -1;
  for (int i = 0; i < HIT_SLOTS && at < 0; i++)
    if (s_hits[i].target == target) at = i;
  if (at < 0) {
    at = s_hitNext;
    s_hitNext = (s_hitNext + 1) % HIT_SLOTS;
  }
  s_hits[at].target = target;
  s_hits[at].cls = hitCls;
  s_hits[at].self = self;
}

// A struct member of o's struct property sk, by name: its address (readable) and property.
static bool member_of(const void* o, int sk, int mk, const char** addr, UProp* m) {
  const UProp a = prop_of(o, sk);
  if (!a.ok || a.type != UW_STRUCT || !nm(mk)) return false;
  __try {
    const void* st = struct_of(a.field);
    if (!st) return false;
    *m = uprop_get(st, nm(mk));
    if (!m->ok) return false;
    *addr = (const char*)o + a.offset + m->offset;
    return engine_readable(*addr, 4);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

// A struct member as a number (float, int or bool), -1 when it does not resolve.
static double member_num(const void* o, int sk, int mk) {
  const char* p = 0;
  UProp m;
  if (!member_of(o, sk, mk, &p, &m)) return -1.0;
  __try {
    if (m.type == UW_FLOAT) {
      float f;
      memcpy(&f, p, 4);
      return f;
    }
    if (m.type == UW_BOOL) return m.bitmask && (uw_int(p, 0) & (int)m.bitmask) != 0;
    if (m.type == UW_INT) return uw_int(p, 0);
    return -1.0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return -1.0;
  }
}

// A struct member's object or class, as its class name (None when null, ? when unreadable).
static const wchar_t* member_class(const void* o, int sk, int mk) {
  const char* p = 0;
  UProp m;
  if (!member_of(o, sk, mk, &p, &m) || (m.type != UW_OBJECT && m.type != UW_CLASS)) return L"?";
  __try {
    const void* v = uw_ptr(p, 0);
    if (!v) return L"None";
    if (!uobj_valid(v)) return L"?";
    return uname_str(m.type == UW_CLASS ? uobj_name(v) : uobj_name(uobj_class(v)));
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return L"?";
  }
}

// A property that is an object, read as a pointer (null when it does not resolve).
static const void* rd_obj(const void* o, int k) {
  const UProp p = prop_of(o, k);
  if (!p.ok || (p.type != UW_OBJECT && p.type != UW_CLASS)) return 0;
  __try {
    const void* v = uw_ptr(o, p.offset);
    return v && uobj_valid(v) ? v : 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

// One line of the target's damage-path state after a shot (spawns.md section 6): which stage the damage reaches.
static void probe_line(const WPawn& w, int shots) {
  float hp = -1.f, hpMin = -1.f, hpCap = -1.f, now = -1.f;
  rd_float(w.obj, N_HEALTH, &hp);
  rd_float(w.obj, N_HEALTHMIN, &hpMin);
  rd_float(w.obj, N_HEALTHCAP, &hpCap);
  const void* level = rd_obj(w.obj, N_LEVEL);
  if (level) rd_float(level, N_GAMETIME, &now);
  int hitCls = 0, self = 0;
  for (int i = 0; i < HIT_SLOTS; i++)
    if (s_hits[i].target == w.obj) {
      hitCls = s_hits[i].cls;
      self = s_hits[i].self;
    }
  np_log("spawnbox: probe %ls shot %d: Health %.1f HealthMin %.1f HealthCap %.1f Role %d RemoteRole %d "
         "ThisFrameDamage %.1f (%ls) DamageInfo valid %.0f damage %.1f time %.2f (%ls) now %.2f; trace hit %ls",
         uname_str(w.nameIdx), shots, hp, hpMin, hpCap, rd_byte(w.obj, N_ROLE), rd_byte(w.obj, N_REMOTEROLE),
         member_num(w.obj, N_THISFRAME, N_MDAMAGE), member_class(w.obj, N_THISFRAME, N_MDTYPE),
         member_num(w.obj, N_DMGINFO, N_MVALID), member_num(w.obj, N_DMGINFO, N_MDAMAGE),
         member_num(w.obj, N_DMGINFO, N_MTIME), member_class(w.obj, N_DMGINFO, N_MDTYPE), now,
         self ? L"the target" : hitCls ? uname_str(hitCls) : L"nothing");
}

void spawnbox_probe_feed(const void* pawn, int shots) {
  const WPawn* pw = 0;
  const int n = world_pawns(&pw);
  const int i = walk_index(pw, n, pawn);
  if (i < 0) return;
  const WPawn& w = pw[i];
  const unsigned now = GetTickCount();
  // A line each time AccuracyShots grows for the same target; the first sight only sets the baseline.
  if (pawn != s_probePawn) {
    s_probePawn = pawn;
    s_probeShots = shots;
  } else {
    if (shots > s_probeShots && shots - s_probeShots < 64 && spawnbox_probe_take(&s_probe, w.obj)) probe_line(w, shots);
    s_probeShots = shots;
  }
}

void spawnbox_log_nohit(const void* pawn, const char* point) {
  if (s_nohitLogs >= SPAWNBOX_LINES_PER_LEVEL || !pawn) return;
  ++s_nohitLogs;
  int cls = 0;
  __try {
    cls = uobj_name(uobj_class(pawn));
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
  }
  np_log("spawnbox: bullet trace hit nothing before %s of %ls: bCollideActors %d bBlockActors %d bBlockPlayers %d bProjTarget %d "
         "bHidden %d bExpanding %d bTraceShootable %d bShootable %d bIgnoreAimAssist %d",
         point, cls ? uname_str(cls) : L"?", rd_set(pawn, N_COLLIDE), rd_set(pawn, N_BLOCKACT), rd_set(pawn, N_BLOCKPLY),
         rd_set(pawn, N_PROJTARGET), rd_set(pawn, N_HIDDEN), rd_set(pawn, N_EXPANDING), rd_set(pawn, N_TRACESHOOT),
         rd_set(pawn, N_SHOOTABLE), rd_set(pawn, N_AIMASSIST));
}

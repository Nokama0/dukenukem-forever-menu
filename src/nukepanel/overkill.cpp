// Overkill glue (overkill.h; aim.md 7), game thread. Once a frame before the aim's pick: shots since the last frame are
// queued with the target locked then; an object-table pass (after a shot, every OK_SCAN_MS armed) finds the player's
// projectiles, giving each the queued lock and its damage. A target whose young projectiles carry its Health is doomed.
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "overkill.h"
#include "aim.h"
#include "aimbot.h"
#include "tbot.h"
#include "aircraft.h"
#include "achhelp.h"   // achhelp_aim_find: an achievement target's Health
#include "spawnbox.h"   // spawnbox_bloat: the Expander's targets
#include "bloat.h"
#include "turretwep.h"   // tw_muzzle_shots
#include "world.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "engine.h"
#include "config.h"
#include "log.h"

enum { N_ACCSHOTS, N_MUZZLE, N_INSTIGATOR, N_OWNER, N_DELETEME, N_LEVEL, N_DAMAGE, N_DAMAGECLASS, N_LOCATION,
       N_VELOCITY, N_WEAPON, N_DSCALING, N_GAME, N_DIFFICULTY, N_IGNOREDIFF, N_GAMETIME, N_COUNT };
static const wchar_t* const NAMES[N_COUNT] = { L"AccuracyShots", L"MuzzleIndex", L"Instigator", L"Owner", L"bDeleteMe",
                                               L"Level", L"Damage", L"DamageClass", L"Location", L"Velocity",
                                               L"Weapon", L"DamageScaling", L"Game", L"Difficulty",
                                               L"bIgnoreDifficultyDamageScaling", L"GameTimeSeconds" };
static int s_n[N_COUNT];

// How a projectile got its target.
enum { SRC_SHOT, SRC_LOCK, SRC_PATH, SRC_NONE };
static const char* const SRC_LABEL[4] = { "the shot's lock", "the lock now", "its path", "none" };

// The tracked class's layout, resolved by name once per class (ok: every field found).
struct Layout { const void* cls; int ok, level, inst, owner, del, damage, dmgCls, loc, vel; unsigned delMask; };
static Layout s_lay;
// bloat: an Expander blast in single player (ExpanderDamage; bloat.h), counted in bloat steps on a pawn; dmg stays its
// Damage. lob: a ballistic projectile (aim.md 13), its flight logged: first sight (first at t0), velocity then (vel0,
// haveVel), last position (last at t1; locOfs), the aim's solved shot (pred, predPitch, predT; havePred).
struct Proj { const void* obj; int slot, delOfs; unsigned delMask; const void* target; float dmg; unsigned firstMs; int bloat;
              int lob, locOfs, havePred, haveVel; float first[3], vel0[3], last[3], t0, t1, pred[3], predPitch, predT; };
static Proj s_proj[OK_PROJ_MAX];
static int s_nProj;
static OkMark s_marks[OK_MARKS];   // doomed targets only
static int s_nMarks;
static OkShots s_shots;
// The ballistic instrument: each ballistic shot's solved prediction (aim_lob_predict), oldest first, taken by the projectile the
// next pass finds; LOB_LOGS_PER_LEVEL shot lines and flight lines a level. s_gameTime: Level.GameTimeSeconds this frame (game speed).
struct LobPred { int ok; float p[3], pitch, t, eye[3]; unsigned ms; const void* target; };
static LobPred s_lobq[OK_SHOTS];
static int s_lobqN, s_lobLogs, s_lobShotLogs;
static float s_gameTime;
enum { LOB_LOGS_PER_LEVEL = 16 };

static unsigned s_level;
static int s_kind = AIMW_NONE;   // the held or mounted gun's kind this frame (ok_applies)
static int s_lastShots = -1, s_lastMuzzle, s_haveMuzzle;
static const void* s_muzzleGun;
static int s_scanned, s_projClsName;
static unsigned s_lastScanMs, s_activeMs;
static int s_activeSeen;   // ok_active: a shot, our press or a tracked projectile seen at s_activeMs
// Projectiles retired at the cap (slots freed), so a pass does not find them again as new. A ring.
struct Retired { const void* obj; int slot; };
static Retired s_retired[OK_PROJ_MAX];
static int s_retiredNext;
static const void* s_projCls;
static int s_projLogs, s_markLogs, s_clearLogs, s_classLogged;
// Counted per 600 frames.
static unsigned s_frames, s_armedFrames, s_shotsSeen, s_scans, s_found, s_bySrc[4], s_notMine, s_gone, s_full, s_marked,
    s_dead, s_gonePass, s_cap, s_short, s_faults, s_retiredN, s_markFull, s_landed;
static double s_scanMs, s_scanMax;
static unsigned s_misses;   // overkill_take_misses
static LARGE_INTEGER s_qpf;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static int nm(int k) {
  if (!s_n[k]) s_n[k] = uname_find(NAMES[k]);
  return s_n[k];
}

// Property k on cls's chain; ok 0 when the name or the property does not resolve. Caller holds the guard.
static UProp prop_g(const void* cls, int k) {
  UProp none = { 0, 0, UW_OTHER, 0, 0, 0 };
  if (!cls || !nm(k)) return none;
  return uprop_get(cls, nm(k));
}

// Guarded single reads on o (its own class): an object (validated), an int (int or byte), a float, a bool.
static const void* rd_obj(const void* o, int k) {
  if (!o) return 0;
  __try {
    const UProp p = prop_g(uobj_class(o), k);
    if (!p.ok || (p.type != UW_OBJECT && p.type != UW_CLASS)) return 0;
    const void* v = uw_ptr(o, p.offset);
    return v && uobj_valid(v) ? v : 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}
static int rd_int(const void* o, int k, int* out) {
  if (!o) return 0;
  __try {
    const UProp p = prop_g(uobj_class(o), k);
    if (p.ok && p.type == UW_INT) { *out = uw_int(o, p.offset); return 1; }
    if (p.ok && p.type == UW_BYTE) { *out = *((const unsigned char*)o + p.offset); return 1; }
    return 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}
static int rd_float(const void* o, int k, float* out) {
  if (!o) return 0;
  __try {
    const UProp p = prop_g(uobj_class(o), k);
    if (!p.ok || p.type != UW_FLOAT) return 0;
    memcpy(out, (const char*)o + p.offset, 4);
    return 1;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}
// A bool of o declared on cls's chain (o may be cls's default object): -1 unread.
static int rd_bool_on(const void* o, const void* cls, int k) {
  if (!o || !cls) return -1;
  __try {
    const UProp p = prop_g(cls, k);
    if (!p.ok || p.type != UW_BOOL || !p.bitmask) return -1;
    return (uw_int(o, p.offset) & (int)p.bitmask) != 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return -1;
  }
}

static int class_name_of(const void* o) {
  __try {
    return o && uobj_valid(o) ? uobj_name(uobj_class(o)) : 0;
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
}

// o's class or a super is named nameIdx (16 classes at most).
static int chain_has(const void* o, int nameIdx) {
  if (!o || !nameIdx) return 0;
  __try {
    int k = 0;
    for (const void* c = uobj_class(o); c && k < 16 && uobj_valid(c); c = uw_ptr(c, US_SUPER), k++)
      if (uobj_name(c) == nameIdx) return 1;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
  }
  return 0;
}

static const wchar_t* cls_str(const void* o) {
  const int c = class_name_of(o);
  return c ? uname_str(c) : o ? L"?" : L"None";
}

// Everything tracked is forgotten unread: a new level, or every targeting feature off.
static void forget(void) {
  s_nProj = s_nMarks = 0;
  memset(&s_shots, 0, sizeof s_shots);
  memset(s_retired, 0, sizeof s_retired);
  s_lastShots = -1;
  s_haveMuzzle = 0;
  s_muzzleGun = 0;
  s_activeSeen = 0;
}

static void new_level(unsigned level) {
  s_level = level;
  forget();
  s_scanned = 0;
  s_projCls = 0;
  memset(&s_lay, 0, sizeof s_lay);
  s_projLogs = s_markLogs = s_clearLogs = s_classLogged = 0;
  s_lobqN = s_lobLogs = s_lobShotLogs = 0;
}

// Shots since the last frame, each queued with the target locked now (the shot left toward it last tick). Only a
// projectile gun's shots spawn anything to match.
static int count_shots(void* player, unsigned now) {
  int n = 0;
  const void* gun = aim_mount_gun();
  if (gun) {   // MG42.FireEffects (the launcher's too) cycles MuzzleIndex 0..2 a shot; no RecordShot on a turret
    int mi = 0;
    if (rd_int(gun, N_MUZZLE, &mi)) {
      if (s_haveMuzzle && gun == s_muzzleGun) n = tw_muzzle_shots(s_lastMuzzle, mi);
      s_muzzleGun = gun;
      s_lastMuzzle = mi;
      s_haveMuzzle = 1;
    } else {
      s_haveMuzzle = 0;
    }
  } else {
    s_haveMuzzle = 0;
    s_muzzleGun = 0;
  }
  int acc = 0;
  if (rd_int(player, N_ACCSHOTS, &acc)) {   // RecordShot adds one a shot (aim.md section 1)
    const int d = s_lastShots >= 0 ? acc - s_lastShots : 0;
    if (!gun && d > 0 && d < 64) n += d;
    s_lastShots = acc;
  } else {
    s_lastShots = -1;
  }
  if (!ok_in_scope(s_kind)) return 0;
  const void* lock = ok_lock(tbot_target(), aimbot_lock(), aim_target_shared());
  for (int i = 0; i < n && i < OK_SHOTS; i++) ok_shot_push(&s_shots, lock, now);
  if (n > 0 && aim_proj_model().kind == AIM_PM_BALLISTIC) {   // the shot as the aim solves it now (logged when found)
    LobPred q;
    memset(&q, 0, sizeof q);
    q.ms = now;
    q.target = lock;
    q.ok = lock && aim_lob_predict(lock, q.p, &q.pitch, &q.t, q.eye);
    if (s_lobqN >= OK_SHOTS) {
      for (int i = 1; i < s_lobqN; i++) s_lobq[i - 1] = s_lobq[i];
      s_lobqN--;
    }
    s_lobq[s_lobqN++] = q;
  }
  s_shotsSeen += (unsigned)n;
  return n;
}

// The class's layout by name; ok when Level, Instigator, Owner, bDeleteMe and Damage resolve. Caller holds the guard.
static void layout_for(const void* cls) {
  memset(&s_lay, 0, sizeof s_lay);
  s_lay.cls = cls;
  const UProp lv = prop_g(cls, N_LEVEL), in = prop_g(cls, N_INSTIGATOR), ow = prop_g(cls, N_OWNER),
              dl = prop_g(cls, N_DELETEME), dm = prop_g(cls, N_DAMAGE), dc = prop_g(cls, N_DAMAGECLASS),
              lc = prop_g(cls, N_LOCATION), vl = prop_g(cls, N_VELOCITY);
  s_lay.ok = lv.ok && lv.type == UW_OBJECT && in.ok && in.type == UW_OBJECT && ow.ok && ow.type == UW_OBJECT &&
             dl.ok && dl.type == UW_BOOL && dl.bitmask && dm.ok && dm.type == UW_FLOAT;
  s_lay.level = lv.offset;
  s_lay.inst = in.offset;
  s_lay.owner = ow.offset;
  s_lay.del = dl.offset;
  s_lay.delMask = dl.bitmask;
  s_lay.damage = dm.offset;
  s_lay.dmgCls = dc.ok && (dc.type == UW_CLASS || dc.type == UW_OBJECT) ? dc.offset : -1;
  s_lay.loc = lc.ok && lc.type == UW_STRUCT && lc.elemSize >= 12 ? lc.offset : -1;
  s_lay.vel = vl.ok && vl.type == UW_STRUCT && vl.elemSize >= 12 ? vl.offset : -1;
}

// One new projectile as read in the pass.
struct Found { const void* obj; int slot; const void* inst; const void* owner; const void* dmgCls; float dmg, loc[3],
               vel[3]; int haveLoc; };
enum { FOUND_MAX = 16 };

static int tracked(const void* o, int slot) {
  for (int i = 0; i < s_nProj; i++)
    if (s_proj[i].obj == o) return 1;
  for (int i = 0; i < OK_PROJ_MAX; i++)
    if (s_retired[i].obj == o && s_retired[i].slot == slot) return 1;
  return 0;
}

// Every live object of the class, in this level, not destroyed and not yet tracked: under one guard.
static int scan_body(const void* level, const void* player, const void* weapon, const void* gun, Found* out,
                     int* notMine) {
  int nf = 0;
  const int n = uobj_count();
  for (int i = 0; i < n; i++) {
    const void* o = uobj_at(i);
    if (!o) continue;
    const void* cls = uobj_class(o);
    if (!cls) continue;
    if (cls != s_projCls) {   // one name compare per object until the class is known, then a pointer compare
      if (s_projCls || uobj_name(cls) != s_projClsName) continue;
      s_projCls = cls;
    }
    if (s_lay.cls != cls) layout_for(cls);
    if (!s_lay.ok || uw_ptr(o, s_lay.level) != level) continue;   // the class default has no Level
    if (uw_int(o, s_lay.del) & (int)s_lay.delMask) continue;
    if (tracked(o, i)) continue;
    const void* inst = uw_ptr(o, s_lay.inst);
    const void* owner = uw_ptr(o, s_lay.owner);
    if (!ok_is_mine(inst, owner, player, weapon, gun)) {
      ++*notMine;
      continue;
    }
    if (nf >= FOUND_MAX) break;
    Found& f = out[nf++];
    memset(&f, 0, sizeof f);
    f.obj = o;
    f.slot = i;
    f.inst = inst;
    f.owner = owner;
    memcpy(&f.dmg, (const char*)o + s_lay.damage, 4);
    f.dmgCls = s_lay.dmgCls >= 0 ? uw_ptr(o, s_lay.dmgCls) : 0;
    if (s_lay.loc >= 0 && s_lay.vel >= 0) {
      memcpy(f.loc, (const char*)o + s_lay.loc, 12);
      memcpy(f.vel, (const char*)o + s_lay.vel, 12);
      f.haveLoc = 1;
    }
  }
  return nf;
}

// The fallback when no lock was held: the nearest hostile pawn or ship the projectile's path over 3 s passes through:
// a straight line along its Velocity for a rocket, the stepped arc of the gun's model for the grenade (aim.md
// section 8), in 12 chords.
enum { PATH_SEGS = 12 };
static const void* path_target(const Found& f) {
  if (!f.haveLoc) return 0;
  float pts[PATH_SEGS + 1][3];
  const AimProjModel m = aim_proj_model();
  int nseg = 1;
  memcpy(pts[0], f.loc, sizeof pts[0]);
  if (m.kind == AIM_PM_BALLISTIC) {
    nseg = PATH_SEGS;
    for (int k = 1; k <= nseg; k++) aim_ballistic_pos(m, f.loc, f.vel, 3.f * k / nseg, pts[k]);
  } else {
    for (int i = 0; i < 3; i++) pts[1][i] = f.loc[i] + f.vel[i] * 3.f;
  }
  const void* best = 0;
  float bestD = 0.f;
  const WPawn* pw = 0;
  const int n = world_pawns(&pw);
  const AcShip* sh = 0;
  const int ns = aircraft_ships(&sh);
  for (int k = 0; k < nseg; k++) {
    for (int i = 0; i < n; i++) {
      if (!pw[i].alive || !pw[i].enemy || !aim_seg_cyl_hit(pts[k], pts[k + 1], pw[i].x, pw[i].y, pw[i].z,
                                                           pw[i].radius, pw[i].height))
        continue;
      const float dx = pw[i].x - f.loc[0], dy = pw[i].y - f.loc[1], dz = pw[i].z - f.loc[2];
      const float d = dx * dx + dy * dy + dz * dz;
      if (!best || d < bestD) { best = pw[i].obj; bestD = d; }
    }
    for (int i = 0; i < ns; i++) {
      const float r = sh[i].h[0] > sh[i].h[1] ? sh[i].h[0] : sh[i].h[1];
      if (!sh[i].damageable || !aim_seg_cyl_hit(pts[k], pts[k + 1], sh[i].c[0], sh[i].c[1], sh[i].c[2], r, sh[i].h[2]))
        continue;
      const float dx = sh[i].c[0] - f.loc[0], dy = sh[i].c[1] - f.loc[1], dz = sh[i].c[2] - f.loc[2];
      const float d = dx * dx + dy * dy + dz * dz;
      if (!best || d < bestD) { best = sh[i].obj; bestD = d; }
    }
  }
  return best;
}

// The player's side of the damage scale (ok_damage_scale), read once a pass.
struct Scale { float dScaling; int sp, difficulty; };
static Scale read_scale(void* player) {
  Scale s = { 0.f, 0, -1 };
  rd_float(player, N_DSCALING, &s.dScaling);
  const void* level = rd_obj(player, N_LEVEL);
  const void* game = level ? rd_obj(level, N_GAME) : 0;
  static int s_sp;
  if (!s_sp) s_sp = uname_find(L"dnSinglePlayer");
  if (game) {
    s.sp = chain_has(game, s_sp);
    rd_int(game, N_DIFFICULTY, &s.difficulty);
  }
  return s;
}

static void scan(void* player, unsigned now) {
  ++s_scans;
  s_scanned = 1;
  s_lastScanMs = now;
  const void* level = rd_obj(player, N_LEVEL);
  const void* weapon = rd_obj(player, N_WEAPON);
  const void* gun = aim_mount_gun();
  if (!level) return;
  Found f[FOUND_MAX];
  int nf = 0, notMine = 0;
  LARGE_INTEGER t0, t1;
  QueryPerformanceCounter(&t0);
  __try {
    nf = scan_body(level, player, weapon, gun, f, &notMine);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    nf = 0;
    s_projCls = 0;   // looked up again by name
    s_lay.cls = 0;
  }
  QueryPerformanceCounter(&t1);
  if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
  const double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)s_qpf.QuadPart;
  s_scanMs += ms;
  if (ms > s_scanMax) s_scanMax = ms;
  s_notMine += (unsigned)notMine;
  uname_dlog_flush();   // uprop's lines, outside the guard
  if (s_projCls && !s_classLogged) {
    s_classLogged = 1;
    np_log("overkill: tracking %ls (layout %s; Instigator or Owner the player, its weapon or the mounted gun); the pass "
           "took %.3f ms over %d objects", uname_str(s_projClsName), s_lay.ok ? "resolved" : "UNRESOLVED: none tracked",
           ms, uobj_count());
  }
  if (!nf) return;
  const Scale sc = read_scale(player);
  float defDmg = 0.f;
  void* def = 0;
  __try {
    def = s_projCls ? engine_get_default_object((void*)s_projCls) : 0;
    if (def && s_lay.ok) memcpy(&defDmg, (const char*)def + s_lay.damage, 4);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
  }
  for (int i = 0; i < nf; i++) {
    if (s_nProj >= OK_PROJ_MAX) {   // before the queue: a dropped projectile takes no shot's lock
      ++s_full;
      continue;
    }
    ++s_found;
    const void* target = 0;
    int src = SRC_SHOT;
    if (!ok_shot_take(&s_shots, now, &target) || !target) {
      target = ok_lock(tbot_target(), aimbot_lock(), aim_target_shared());
      src = SRC_LOCK;
    }
    if (!target) {
      target = path_target(f[i]);
      src = target ? SRC_PATH : SRC_NONE;
    }
    ++s_bySrc[src];
    // Projectile.DamageClass's default bIgnoreDifficultyDamageScaling, as dnSinglePlayer.ModifyDamage reads it.
    int dcName = 0, ignore = -1, dcNames[8], dcN = 0;
    void* dcDef = 0;
    __try {
      if (f[i].dmgCls && uobj_valid(f[i].dmgCls)) {
        dcName = uobj_name(f[i].dmgCls);
        dcDef = engine_get_default_object((void*)f[i].dmgCls);
        for (const void* c = f[i].dmgCls; c && dcN < 8 && uobj_valid(c); c = uw_ptr(c, US_SUPER)) dcNames[dcN++] = uobj_name(c);
      }
    } __except (av_only(GetExceptionCode())) {
      ++s_faults;
    }
    if (dcDef) ignore = rd_bool_on(dcDef, f[i].dmgCls, N_IGNOREDIFF);
    const wchar_t* dcStr[8];
    for (int k = 0; k < dcN; k++) dcStr[k] = uname_str(dcNames[k]);
    const int bloat = bloat_type(dcStr, dcN);
    const int instIsPlayer = f[i].inst == player;
    const float scale = ok_damage_scale(instIsPlayer, sc.dScaling, sc.sp, ignore, sc.difficulty);
    const float dmg = ok_proj_damage(f[i].dmg, defDmg) * scale;
    Proj& p = s_proj[s_nProj++];
    p.obj = f[i].obj;
    p.slot = f[i].slot;
    p.delOfs = s_lay.del;
    p.delMask = s_lay.delMask;
    p.target = target;
    p.dmg = dmg;
    p.firstMs = now;
    p.bloat = bloat && sc.sp;
    p.lob = aim_proj_model().kind == AIM_PM_BALLISTIC && f[i].haveLoc;
    p.locOfs = s_lay.loc;
    p.havePred = p.haveVel = 0;
    if (p.lob) {
      memcpy(p.first, f[i].loc, 12);
      memset(p.vel0, 0, sizeof p.vel0);
      memcpy(p.last, f[i].loc, 12);
      p.t0 = p.t1 = s_gameTime;
      while (s_lobqN > 0) {   // the oldest shot's prediction still young (OK_SHOT_MS) belongs to this projectile
        const LobPred q = s_lobq[0];
        for (int k = 1; k < s_lobqN; k++) s_lobq[k - 1] = s_lobq[k];
        s_lobqN--;
        if (now - q.ms >= OK_SHOT_MS) continue;
        p.havePred = q.ok;
        memcpy(p.pred, q.p, 12);
        p.predPitch = q.pitch;
        p.predT = q.t;
        // The spawn against the eye (AIM_LOB_SPAWN_DZ's measurement): the first-seen point is one pass after the spawn,
        // so its forward offset includes that pass's flight.
        if (q.ok && s_lobShotLogs < LOB_LOGS_PER_LEVEL) {
          s_lobShotLogs++;
          np_log("overkill: ballistic shot at %ls: launch pitch %.1f degrees (solved), flight %.2f s, predicted impact "
                 "%.0f,%.0f,%.0f; eye %.0f,%.0f,%.0f, first seen %.0f,%.0f,%.0f, offset from the eye %.1f,%.1f,%.1f "
                 "(horizontal %.1f, vertical %.1f)", cls_str(q.target), q.pitch, q.t, q.p[0], q.p[1], q.p[2], q.eye[0],
                 q.eye[1], q.eye[2], p.first[0], p.first[1], p.first[2], p.first[0] - q.eye[0], p.first[1] - q.eye[1],
                 p.first[2] - q.eye[2], sqrtf((p.first[0] - q.eye[0]) * (p.first[0] - q.eye[0]) +
                                              (p.first[1] - q.eye[1]) * (p.first[1] - q.eye[1])), p.first[2] - q.eye[2]);
        }
        break;
      }
    }
    if (s_projLogs < OK_LOGS_PER_LEVEL) {
      s_projLogs++;
      np_log("overkill: projectile %ls: Damage %.1f (class default %.1f) x %.2f = %.1f (Instigator %ls, Owner %ls; "
             "DamageScaling %.2f, single player %d, Difficulty %d, %ls bIgnoreDifficultyDamageScaling %d%s) -> %ls by %s",
             uname_str(s_projClsName), f[i].dmg, defDmg, scale, dmg, instIsPlayer ? L"the player" : cls_str(f[i].inst),
             cls_str(f[i].owner), sc.dScaling, sc.sp, sc.difficulty, dcName ? uname_str(dcName) : L"no DamageClass,", ignore,
             bloat && sc.sp ? "; a bloat type: no damage to a pawn, judged by bloat steps" : "", target ? cls_str(target) : L"no target", SRC_LABEL[src]);
    }
  }
}

// The tracked projectiles still in flight: the snapshot slot still theirs (a destroyed actor is only freed at the next
// garbage collection) and bDeleteMe clear. Under one guard.
static void log_lob(const Proj& p);
static void track(void) {
  int k = 0, ng = 0;
  const float g = aim_proj_model().gravity;   // the half step aim_fd_velocity puts back
  Proj gone[OK_PROJ_MAX];   // lobs that ended this frame, logged outside the guard
  __try {
    for (int i = 0; i < s_nProj; i++) {
      Proj& p = s_proj[i];
      if (!uobj_at_is(p.slot, p.obj) || (uw_int(p.obj, p.delOfs) & (int)p.delMask)) {
        ++s_gone;
        if (p.lob) gone[ng++] = p;
        continue;
      }
      if (p.lob && p.locOfs >= 0) {   // the last position seen before it detonates
        memcpy(p.last, (const char*)p.obj + p.locOfs, 12);
        p.t1 = s_gameTime;
        if (!p.haveVel) p.haveVel = aim_fd_velocity(p.first, p.last, p.t1 - p.t0, g, p.vel0);
      }
      s_proj[k++] = p;
    }
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    s_gone += (unsigned)(s_nProj - k);
    k = 0;   // unreadable: nothing is trusted
  }
  s_nProj = k;
  for (int i = 0; i < ng; i++) log_lob(gone[i]);
}

// One ballistic flight (aim.md section 13): where it was last seen (one frame before it detonated or stopped) against the
// aim's predicted impact, and against the model flown from its own first state for the same game time (gravity, clamp
// and drag alone, whatever the launch). Capped a level.
static void log_lob(const Proj& p) {
  if (s_lobLogs >= LOB_LOGS_PER_LEVEL) return;
  s_lobLogs++;
  const AimProjModel m = aim_proj_model();
  const float T = p.t1 - p.t0;
  float model[3] = { 0.f, 0.f, 0.f };
  const int haveModel = m.kind == AIM_PM_BALLISTIC && T > 0.f && p.haveVel;
  if (haveModel) aim_ballistic_pos(m, p.first, p.vel0, T, model);
  const float sp = sqrtf(p.vel0[0] * p.vel0[0] + p.vel0[1] * p.vel0[1] + p.vel0[2] * p.vel0[2]);
  const float hs = sqrtf(p.vel0[0] * p.vel0[0] + p.vel0[1] * p.vel0[1]);
  float dp[3], dm[3];
  for (int i = 0; i < 3; i++) {
    dp[i] = p.last[i] - p.pred[i];
    dm[i] = p.last[i] - model[i];
  }
  np_log("overkill: ballistic %ls ended after %.2f s (game time) last seen at %.0f,%.0f,%.0f; first seen at %.0f,%.0f,%.0f, "
         "speed %.0f, velocity pitch %.1f degrees (%s); predicted impact %s%.0f,%.0f,%.0f (%.0f units off; solved pitch %.1f, flight "
         "%.2f s); the model from its first state %.0f,%.0f,%.0f (%.0f units off; gravity %.0f, boost %.0f, terminal %.0f)",
         uname_str(s_projClsName), T, p.last[0], p.last[1], p.last[2], p.first[0], p.first[1], p.first[2], sp,
         atan2f(p.vel0[2], hs) * 57.29578f, p.haveVel ? "from the next Location" : "unread: seen once", p.havePred ? "" : "(none) ", p.pred[0], p.pred[1], p.pred[2],
         p.havePred ? sqrtf(dp[0] * dp[0] + dp[1] * dp[1] + dp[2] * dp[2]) : -1.f, p.predPitch, p.predT, model[0],
         model[1], model[2], haveModel ? sqrtf(dm[0] * dm[0] + dm[1] * dm[1] + dm[2] * dm[2]) : -1.f, m.gravity, m.boostZ,
         m.terminal);
}

// Health of a target: a walked pawn's (*isPawn set), a hostile live ship's or an achievement target's; alive 0 when it is
// none any more.
static int target_health(const void* t, float* health, int* isPawn) {
  const WPawn* pw = 0;
  const int n = world_pawns(&pw);
  *isPawn = 0;
  for (int i = 0; i < n; i++)
    if (pw[i].obj == t) {
      *health = (float)pw[i].health;
      *isPawn = 1;
      return pw[i].alive;
    }
  if (const AcShip* s = aircraft_find(t)) {
    *health = s->health;
    return s->health > 0.f;
  }
  if (const AhAimTarget* a = achhelp_aim_find(t)) {
    *health = a->health;
    return a->health > 0.f;
  }
  *health = 0.f;
  return 0;
}

static int mark_find(const void* t) {
  for (int i = 0; t && i < s_nMarks; i++)
    if (s_marks[i].target == t) return i;
  return -1;
}

// Every target with a mark or a projectile: its young projectiles' damage against its Health (ok_mark_step).
static void marks(unsigned now) {
  const void* seen[OK_PROJ_MAX + OK_MARKS];
  int ns = 0;
  for (int i = 0; i < s_nMarks; i++) seen[ns++] = s_marks[i].target;
  for (int i = 0; i < s_nProj; i++) {
    const void* t = s_proj[i].target;
    int dup = !t;
    for (int j = 0; j < ns && !dup; j++) dup = seen[j] == t;
    if (!dup) seen[ns++] = t;
  }
  for (int j = 0; j < ns; j++) {
    const void* t = seen[j];
    float health = 0.f;
    int isPawn = 0;
    const int alive = target_health(t, &health, &isPawn);
    int nYoung = 0, nOld = 0, nBloat = 0;
    float expected = 0.f;
    for (int i = 0; i < s_nProj; i++) {
      if (s_proj[i].target != t) continue;
      if (ok_young(s_proj[i].firstMs, now)) {
        ++nYoung;
        expected += ok_proj_counts_dmg(s_proj[i].dmg, s_proj[i].bloat, isPawn);
        nBloat += s_proj[i].bloat && isPawn;
      } else {
        ++nOld;
      }
    }
    // The Expander's blasts on a pawn: doomed when the pop is on its way (bloat_verdict with the young ones in flight),
    // or by the other projectiles' damage. Its bloat state is read for the landed test of a cleared Expander mark too.
    const int at = mark_find(t);
    OkMark m = { t, 0, 0, -1 };
    if (at >= 0) m = s_marks[at];
    int bCan = 1, bState = -1, bSecond = -1;
    const int bRead = (nBloat || m.steps >= 0) && spawnbox_bloat(t, &bCan, &bState, &bSecond);
    const float dmgOnly = expected;
    expected = ok_bloat_expected(expected, nBloat, bRead, bloat_verdict(bCan, bState, bSecond, nBloat) == BL_DONE, health);
    const int byBloat = expected > dmgOnly;
    int ev = ok_mark_step(&m, alive, health, expected, nYoung, nOld, now);
    if (ev == OK_EV_MARK) m.steps = byBloat ? bloat_steps(bState, bSecond) : -1;
    ev = ok_bloat_landed(ev, m.steps, bRead ? bloat_steps(bState, bSecond) : -1);
    if (ev == OK_EV_MARK && at < 0 && s_nMarks >= OK_MARKS) {   // no slot: not doomed, not logged again each frame
      ++s_markFull;
      ev = OK_EV_NONE;
    }
    if (m.doomed && at >= 0) {
      s_marks[at] = m;
    } else if (m.doomed && s_nMarks < OK_MARKS) {
      s_marks[s_nMarks++] = m;
    } else if (!m.doomed && at >= 0) {
      s_marks[at] = s_marks[--s_nMarks];
    }
    if (ev == OK_EV_MARK) {
      ++s_marked;
      if (s_markLogs < OK_LOGS_PER_LEVEL) {
        s_markLogs++;
        if (byBloat)
          np_log("overkill: doomed %ls by the Expander: BloatState %d, SecondBloat %d, %d blast(s) in flight (the pop is on "
                 "its way)", cls_str(t), bState, bSecond, nBloat);
        else
          np_log("overkill: doomed %ls: Health %.0f, %d projectile(s) in flight, expected damage %.0f", cls_str(t), health,
                 nYoung, expected);
      }
    } else if (ev != OK_EV_NONE) {
      if (ev == OK_EV_DEAD) ++s_dead;
      else if (ev == OK_EV_LANDED) ++s_landed;
      else {
        ++s_misses;
        if (ev == OK_EV_GONE) ++s_gonePass;
        else if (ev == OK_EV_CAP) ++s_cap;
        else ++s_short;
      }
      if (s_clearLogs < OK_LOGS_PER_LEVEL) {
        s_clearLogs++;
        static const char* const WHY[7] = { "", "", "dead", "alive, its projectiles gone", "alive, past the 2 s cap",
                                            "alive, the projectiles left fall short",
                                            "the Expander's blast landed: bloated or popping" };
        np_log("overkill: %ls mark cleared after %u ms: %s (Health %.0f, %d young and %d old projectile(s), expected "
               "%.0f)", alive ? cls_str(t) : L"target", now - m.since, WHY[ev], health, nYoung, nOld, expected);
      }
    }
  }
  // Past the cap a projectile no longer counts (CAP above has been given): its slot is freed and it is remembered as
  // retired, so a pass does not take it for a new one.
  int k = 0;
  for (int i = 0; i < s_nProj; i++) {
    if (ok_young(s_proj[i].firstMs, now)) {
      s_proj[k++] = s_proj[i];
      continue;
    }
    s_retired[s_retiredNext].obj = s_proj[i].obj;
    s_retired[s_retiredNext].slot = s_proj[i].slot;
    s_retiredNext = (s_retiredNext + 1) % OK_PROJ_MAX;
    ++s_retiredN;
  }
  s_nProj = k;
}

void overkill_frame(int inLevel) {
  const unsigned level = world_level_serial();
  if (level != s_level) new_level(level);
  void* player = world_player();
  const int feature = g_cfg.tbotOn || g_cfg.aimbotOn || g_cfg.silentOn;
  if (!feature) forget();   // switched on again, nothing from before counts
  if (feature && inLevel && player) {   // a pause or a load changes nothing: the marks wait
    const unsigned now = GetTickCount();
    s_kind = aim_weapon().kind;
    const void* lvl = rd_obj(player, N_LEVEL);   // game time for the lob instrument (the game-speed slider)
    if (!lvl || !rd_float(lvl, N_GAMETIME, &s_gameTime)) s_gameTime = 0.f;
    const int armed = ok_in_scope(s_kind) && aim_gate_now() == AIM_GO;
    if (armed) ++s_armedFrames;
    const int shots = count_shots(player, now);
    const int want = aim_proj_class_name();
    if (want != s_projClsName) {   // another gun: its class is looked up by name again
      s_projClsName = want;
      s_projCls = 0;
    }
    if (shots > 0 || tbot_pressing() || s_nProj > 0) {   // Idle passes find nothing new
      s_activeSeen = 1;
      s_activeMs = now;
    }
    if (ok_scan_due(armed && want, shots > 0, s_scanned, s_lastScanMs, now, ok_active(s_activeSeen, s_activeMs, now)))
      scan(player, now);
    track();
    marks(now);
  }
  if (++s_frames < 600) return;
  s_frames = 0;
  if (s_armedFrames || s_found || s_nProj || s_nMarks || s_landed)
    np_log("overkill: 600 frames: armed %u frame(s), shots %u; passes %u (avg %.3f ms, max %.3f ms); projectiles found "
           "%u (target by the shot's lock %u, the lock now %u, the path %u, none %u), not the player's %u, gone %u, "
           "table full %u, retired at the cap %u, tracked now %d; marks %u (table full %u), cleared dead %u, Expander blast landed %u, alive %u (gone %u, cap %u, short %u), doomed now %d; "
           "%u fault(s)", s_armedFrames, s_shotsSeen, s_scans, s_scans ? s_scanMs / s_scans : 0.0, s_scanMax, s_found,
           s_bySrc[SRC_SHOT], s_bySrc[SRC_LOCK], s_bySrc[SRC_PATH], s_bySrc[SRC_NONE], s_notMine, s_gone, s_full, s_retiredN,
           s_nProj, s_marked, s_markFull, s_dead, s_landed, s_gonePass + s_cap + s_short, s_gonePass, s_cap, s_short, s_nMarks, s_faults);
  s_armedFrames = s_shotsSeen = s_scans = s_found = s_notMine = s_gone = s_full = s_marked = s_dead = 0;
  s_gonePass = s_cap = s_short = s_faults = s_retiredN = s_markFull = s_landed = 0;
  memset(s_bySrc, 0, sizeof s_bySrc);
  s_scanMs = s_scanMax = 0.0;
}

int overkill_doomed(const void* target) { return ok_applies(s_kind, mark_find(target) >= 0); }

unsigned overkill_take_misses(void) {
  const unsigned m = s_misses;
  s_misses = 0;
  return m;
}

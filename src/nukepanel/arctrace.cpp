// Nuke Tank trace (arctrace.h; format: docs/re/arcade.md section 6). A session is one use of a dnControl_ArcadeMachine.
// The tagged actors and Dispatchers are found by Tag in one object-table pass per session (Tags move across level
// loads: lvnames.h); each frame reads them in one guarded pass. The writer is pbtrace.cpp's pattern. Game thread only.
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "arctrace.h"
#include "world.h"
#include "scall.h"
#include "scallparms.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "pehook.h"
#include "paths.h"
#include "lvnames.h"
#include "mpgate.h"
#include "gamespeed.h"
#include "config.h"
#include "log.h"

enum { N_INTERACTIVE, N_LEVEL, N_TIME, N_LOCATION, N_HIDDEN, N_HEALTH, N_TAG, N_VALUE, N_CMP, N_DISPATCHING, N_INDEX,
       N_LEFT, N_RIGHT, N_FIRE, N_INFIRE, N_INLEFT, N_INRIGHT, N_COUNT };
static const wchar_t* const NAMES[N_COUNT] = {
  L"InteractiveDecoration", L"Level", L"TimeSeconds", L"Location", L"bHidden", L"Health", L"Tag", L"Value",
  L"ComparisonValue", L"bDispatching", L"Index", L"Left", L"Right", L"Fire", L"bIDnControlFire", L"bIStrafeLeft",
  L"bIStrafeRight" };
static int s_n[N_COUNT];   // cleared per session: Left/Right/Fire are declared by a DLC package

// The tagged actors (arcade.md sections 2 and 4), in this order: the tank, the souls in AtFrame order, the lives,
// the score Variables (Variable.Value, Engine.u), the cabinet and the tank's mesh (header only).
enum { AK_TANK, AK_SOUL0, AK_LIFE0 = AK_SOUL0 + AT_SOULS, AK_VAR0 = AK_LIFE0 + 3, AK_MACHINE = AK_VAR0 + 5, AK_AVATAR,
       AK_COUNT };
enum { AT_LIVES = 3, AT_VARS = 5 };
static const char* const ACTOR_TAGS[AK_COUNT] = {
  "TankAvatarSoul", "Enemy01Soul", "Enemy02Soul", "Enemy03Soul", "Enemy04Soul", "Enemy05Soul", "Bomb01Soul",
  "Bomb02Soul", "BigBadSoul", "Life01Tank", "Life02Tank", "Life03Tank", "TotalScore", "Stage01Score", "Stage02Score",
  "Stage03Score", "BigBadSpawnScore", "NukeTankArcadeMachine", "TankAvatar" };
static_assert(AK_COUNT == 19 && AK_COUNT <= LN_MAX, "lvnames.h keeps LN_MAX keys");

// The Dispatchers watched, and the event row each start writes (kind, value): an enemy n (1-5), bomb 6-7 or the boss
// 8 killed or attacking, a life lost in lane n (0: NukePlayer), the stage, the boss's attack, the win, game over.
enum { DK_MENU, DK_ATTACK, DK_TOUCHDOWN, DK_KILL, DK_LIFE, DK_STAGE, DK_BOSS, DK_WIN, DK_GAMEOVER, DK_COUNT };
static const char* const DK_LABEL[DK_COUNT] = { "menu", "attack", "touch_down", "kill", "life_lost", "stage", "boss",
                                                "win", "game_over" };
struct DispTag { const char* tag; int kind, value; };
static const DispTag DISP_TAGS[] = {
  { "StartArcGameDispatcher", DK_MENU, 0 }, { "PlayGameDispatcher", DK_MENU, 1 }, { "InfScreenEndDispatcher", DK_MENU, 2 },
  { "EndArcGameDispatcher", DK_MENU, 3 },
  { "MoveEn01StartDispatcher", DK_ATTACK, 1 }, { "MoveEn02StartDispatcher", DK_ATTACK, 2 },
  { "MoveEn03StartDispatcher", DK_ATTACK, 3 }, { "MoveEn04StartDispatcher", DK_ATTACK, 4 },
  { "MoveEn05StartDispatcher", DK_ATTACK, 5 }, { "MoveBomb01StartDispatcher", DK_ATTACK, 6 },
  { "MoveBomb02StartDispatcher", DK_ATTACK, 7 },
  { "En01TouchDown", DK_TOUCHDOWN, 1 }, { "En02TouchDown", DK_TOUCHDOWN, 2 }, { "En03TouchDown", DK_TOUCHDOWN, 3 },
  { "En04TouchDown", DK_TOUCHDOWN, 4 }, { "En05TouchDown", DK_TOUCHDOWN, 5 }, { "BigBadTouchDown", DK_TOUCHDOWN, 8 },
  { "Enemy01DestroyedDispatcher", DK_KILL, 1 }, { "Enemy02DestroyedDispatcher", DK_KILL, 2 },
  { "Enemy03DestroyedDispatcher", DK_KILL, 3 }, { "Enemy04DestroyedDispatcher", DK_KILL, 4 },
  { "Enemy05DestroyedDispatcher", DK_KILL, 5 }, { "Bomb01DestroyedDispatcher", DK_KILL, 6 },
  { "Bomb02DestroyedDispatcher", DK_KILL, 7 }, { "BigBadDestroyedDispatcher", DK_KILL, 8 },
  { "En01NukeDispatcher", DK_LIFE, 1 }, { "En02NukeDispatcher", DK_LIFE, 2 }, { "En03NukeDispatcher", DK_LIFE, 3 },
  { "En04NukeDispatcher", DK_LIFE, 4 }, { "En05NukeDispatcher", DK_LIFE, 5 }, { "NukePlayer", DK_LIFE, 0 },
  { "StartStage02Dispatcher", DK_STAGE, 2 }, { "StartStage03Dispatcher", DK_STAGE, 3 },
  { "StartBigBadAttackDispatcher", DK_BOSS, 1 }, { "WinGameDispatcher", DK_WIN, 1 },
  { "GameOverDispatcher", DK_GAMEOVER, 1 } };
enum { DT_COUNT = (int)(sizeof DISP_TAGS / sizeof DISP_TAGS[0]), DISP_MAX = 64 };
static_assert(DT_COUNT == 36 && DT_COUNT <= LN_MAX, "lvnames.h keeps LN_MAX keys");

static LnKeys s_actorKeys, s_dispKeys;

// The pawn's input flags and the control event base each edge raises (pbtrace.h pb_flag_event).
enum { IN_FIRE, IN_LEFT, IN_RIGHT, IN_COUNT };
static const int IN_NAME[IN_COUNT] = { N_INFIRE, N_INLEFT, N_INRIGHT };
static const int IN_BASE[IN_COUNT] = { 14, 26, 29 };
static const char* const IN_LABEL[IN_COUNT] = { "bIDnControlFire", "bIStrafeLeft", "bIStrafeRight" };
static const int MACHINE_NAME[3] = { N_FIRE, N_LEFT, N_RIGHT };
static const char* const MACHINE_LABEL[3] = { "Fire", "Left", "Right" };

struct Ofs { int off; unsigned bit; };
struct Act { const void* obj; Ofs loc, hidden, health, value; };
struct Disp { const void* obj; int key; };

// What one frame reads.
struct Read { AtActor act[AK_COUNT]; int var[AT_VARS]; int machine[3], in[IN_COUNT]; float time;
              unsigned char dOn[DISP_MAX]; int dIndex[DISP_MAX]; };

struct Session {
  const void* ctrl;
  void* player;
  const void* level;
  Act act[AK_COUNT];
  Disp disp[DISP_MAX];
  int nDisp, dispFound[DT_COUNT];
  Ofs time, machine[3], in[IN_COUNT], dispOn, dispIndex;
  float laneY[AT_ENEMIES];
  int haveLane[AT_ENEMIES];
  Read was;
  int haveWas, stateName, stage, bossSeen, varStart[AT_VARS];
  unsigned frame, rows, dropped, capNoted, peExec, peCtrl, dk[DK_COUNT], kills[3], hits, livesLost, hiddenEdges,
      inputEdges, machineEdges, scoreRows, botRows;
  PbDtStats gameDt;
  LARGE_INTEGER t0;
  float time0, lastDt, gameOverAt;
  unsigned readAt;   // s_frameNo of the last frame read
  AtBot bot;
};
static Session s;
static int s_active;
static unsigned s_sessions, s_level;
static unsigned s_frameNo;    // arctrace_frame calls, for AtView.fresh
static const char* s_endWhy;  // the player left this frame: arctrace_frame_end closes the session

// pehook: the fire binds' execs on the player and ControlEvent on the machine. The key path is script to script and
// may never pass this slot: the polled flags are the record, the summary counts what the slot saw.
enum { PE_FIRE_ON, PE_FIRE_OFF, PE_CTRL, PE_COUNT };
static const wchar_t* const PE_NAMES[PE_COUNT] = { L"DnControlFireEnable", L"DnControlFireDisable", L"ControlEvent" };
static const char* const PE_LABELS[PE_COUNT] = { "DnControlFireEnable", "DnControlFireDisable", "ControlEvent" };
static int s_peSub[PE_COUNT] = { -1, -1, -1 };
static int s_peName[PE_COUNT];
static const void* s_ctrlFn;
static ScallParm s_ctrlParm;

// The trace file and its buffer (pbtrace.cpp's discipline).
static HANDLE s_file = INVALID_HANDLE_VALUE;
static const char* s_fileWhy;
static wchar_t s_path[MAX_PATH], s_dir[MAX_PATH];
static unsigned s_fileBytes, s_rotations, s_failStreak;
static char s_hdr[16384];
static int s_hdrUsed, s_hdrFlushed;
static char s_buf[64 * 1024];
static int s_used;
static unsigned s_lastFlushMs, s_flushes;
static double s_longestWriteMs;
static LARGE_INTEGER s_qpf;

static float s_dts[120];
static int s_dtAt;
static LARGE_INTEGER s_lastQpc;

static unsigned s_faults;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static int nm(int k) {
  if (!s_n[k]) s_n[k] = uname_find(NAMES[k]);
  return s_n[k];
}

static double ms_since(LARGE_INTEGER t0) {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return (double)(t.QuadPart - t0.QuadPart) * 1000.0 / (double)s_qpf.QuadPart;
}

static bool live(const void* p) {
  if (!p) return false;
  __try {
    return uobj_valid(p);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

static const void* class_of(const void* o) {
  if (!o) return 0;
  __try {
    return uobj_class(o);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

static int name_of(const void* o) {
  if (!o) return 0;
  __try {
    return uobj_name(o);
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
}

static UProp prop_on_class(const void* cls, int k) {
  UProp none = { 0, 0, UW_OTHER, 0, 0, 0 };
  const int n = nm(k);
  if (!cls || !n) return none;
  __try {
    return uprop_get(cls, n);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return none;
  }
}

// o's property k when it has type `type` (a bool: its bit too; a name: 4 bytes); off 0 when it does not resolve.
static Ofs resolve(const void* o, int k, int type) {
  Ofs r = { 0, 0 };
  const UProp p = prop_on_class(class_of(o), k);
  if (!p.ok || p.type != type || p.offset <= 0 || (type == UW_BOOL && !p.bitmask)) return r;
  if (type == UW_OTHER && p.elemSize != 4) return r;
  r.off = p.offset;
  r.bit = p.bitmask;
  return r;
}

static int rd_i(const void* o, Ofs f) { return f.off ? uw_int(o, f.off) : 0; }
static int rd_b(const void* o, Ofs f) { return f.off ? (uw_int(o, f.off) & (int)f.bit) != 0 : 0; }

// --- The file ------------------------------------------------------------------------------------------------------
// Opens s_path after moving an existing file at it to `keep`: reused only once renamed away, else a new
// arcade-trace-<tick>.csv beside it (CREATE_NEW: never truncates either).
static void open_into(const wchar_t* keep, const char* why) {
  const int existed = GetFileAttributesW(s_path) != INVALID_FILE_ATTRIBUTES;
  const int renamed = existed && keep && MoveFileExW(s_path, keep, MOVEFILE_REPLACE_EXISTING);
  const unsigned long err = existed && !renamed ? GetLastError() : 0;
  if (pb_open_choice(existed, renamed) == PB_OPEN_UNIQUE) {
    wchar_t u[MAX_PATH];
    if (at_unique_name(u, MAX_PATH, s_dir, GetTickCount())) {
      np_log("arcade: %s: %ls not renamed (error %lu): tracing into %ls instead, nothing truncated", why, s_path, err,
             u);
      wcscpy_s(s_path, MAX_PATH, u);
      s_file = CreateFileW(s_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    }
  } else {
    if (renamed) np_log("arcade: %s: the trace kept as %ls", why, keep);
    s_file = CreateFileW(s_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  }
  s_fileBytes = 0;
}

// Past PB_FILE_CAP the file starts over, the last one kept as <name>.old (one only), starting with this session's
// header rows so it parses on its own.
static void rotate(void) {
  CloseHandle(s_file);
  s_file = INVALID_HANDLE_VALUE;
  wchar_t old[MAX_PATH];
  ++s_rotations;
  open_into(_snwprintf_s(old, MAX_PATH, _TRUNCATE, L"%ls.old", s_path) > 0 ? old : 0, "trace size cap");
  if (s_file == INVALID_HANDLE_VALUE) {
    s_fileWhy = "the new trace file did not open after the size cap";
    np_log("arcade: no trace file after rotation %u: tracing stopped; sessions are still summarised here", s_rotations);
    return;
  }
  DWORD wrote = 0;
  if (s_hdrFlushed > 0 && WriteFile(s_file, s_hdr, (DWORD)s_hdrFlushed, &wrote, NULL)) s_fileBytes += wrote;
  np_log("arcade: trace file passed %u MB: a new %ls (rotation %u), %lu header byte(s) written again",
         PB_FILE_CAP >> 20, s_path, s_rotations, wrote);
}

static void flush(void) {
  if (!s_used) return;
  if (s_file != INVALID_HANDLE_VALUE && pb_rotate_due(s_fileBytes, (unsigned)s_used, PB_FILE_CAP)) rotate();
  if (s_file != INVALID_HANDLE_VALUE) {
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    DWORD wrote = 0;
    s_fileBytes += (unsigned)s_used;
    if (!WriteFile(s_file, s_buf, (DWORD)s_used, &wrote, NULL) || wrote != (DWORD)s_used) {
      if (!s_failStreak++)
        np_log("arcade: trace write of %d byte(s) failed (%lu written, error %lu); further failures are counted",
               s_used, wrote, GetLastError());
    } else if (s_failStreak) {
      np_log("arcade: trace writes recovered after %u failure(s)", s_failStreak);
      s_failStreak = 0;
    }
    const double ms = ms_since(t0);
    if (ms > s_longestWriteMs) s_longestWriteMs = ms;
    ++s_flushes;
  }
  s_used = 0;
  s_hdrFlushed = s_hdrUsed;
  s_lastFlushMs = GetTickCount();
}

// Started by the first session of the process (the last launch's file renamed .prev.csv), appended to by the next.
static void open_file(void) {
  if (s_file != INVALID_HANDLE_VALUE || s_fileWhy) return;
  if (!np_our_dir(s_dir, MAX_PATH) || _snwprintf_s(s_path, MAX_PATH, _TRUNCATE, L"%lsarcade-trace.csv", s_dir) < 0) {
    s_fileWhy = "the NukePanel folder's path did not resolve";
  } else {
    CreateDirectoryW(s_dir, NULL);
    wchar_t prev[MAX_PATH];
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    open_into(_snwprintf_s(prev, MAX_PATH, _TRUNCATE, L"%lsarcade-trace.prev.csv", s_dir) > 0 ? prev : 0,
              "the last launch's trace");
    const double ms = ms_since(t0);
    if (s_file == INVALID_HANDLE_VALUE) {
      s_fileWhy = "CreateFileW failed";
      np_log("arcade: %ls not opened (error %lu, %.1f ms)", s_path, GetLastError(), ms);
    } else {
      np_log("arcade: trace file %ls opened in %.1f ms", s_path, ms);
    }
  }
  if (s_fileWhy) np_log("arcade: no trace file: %s; sessions are still summarised here", s_fileWhy);
}

// One row into the buffer, under the session's row cap.
static void emit(const char* row, int n) {
  if (n <= 0) return;
  if (!pb_row_admit(s.rows)) {
    ++s.dropped;
    if (s.capNoted) return;
    s.capNoted = 1;
    static const char kCap[] = "E,0,0,0,cap_reached,200000,rows past this are dropped\n";
    row = kCap;
    n = (int)sizeof kCap - 1;
  } else {
    ++s.rows;
  }
  if (pb_flush_due(GetTickCount(), s_lastFlushMs, s_used, (int)sizeof s_buf, n)) flush();
  if (n > (int)sizeof s_buf - s_used) return;
  memcpy(s_buf + s_used, row, n);
  s_used += n;
}

static void ev(const char* name, int value, const char* detail) {
  char row[256];
  emit(row, pb_fmt_event(row, sizeof row, s.frame, s.was.time, ms_since(s.t0), name, value, detail));
}

static void header(const char* fmt, ...) {
  char row[512];
  va_list a;
  va_start(a, fmt);
  int n = _vsnprintf_s(row, sizeof row - 1, _TRUNCATE, fmt, a);
  va_end(a);
  if (n < 0) n = (int)strlen(row);
  row[n++] = '\n';
  emit(row, n);
  pb_hdr_append(s_hdr, (int)sizeof s_hdr, &s_hdrUsed, row, n);
}

// --- ProcessEvent handlers -----------------------------------------------------------------------------------------
static void on_pe(void* obj, void* func, void* parms) {
  if (!s_active) return;
  const int name = *(const int*)((const char*)func + UO_NAME);
  if (s_peName[PE_FIRE_ON] && (name == s_peName[PE_FIRE_ON] || name == s_peName[PE_FIRE_OFF])) {
    if (obj != s.player) return;
    ++s.peExec;
    const int on = name == s_peName[PE_FIRE_ON];
    ev("exec", on, PE_LABELS[on ? PE_FIRE_ON : PE_FIRE_OFF]);
    return;
  }
  if (!s_peName[PE_CTRL] || name != s_peName[PE_CTRL] || obj != s.ctrl) return;
  ++s.peCtrl;
  // The machine's own ControlEvent gives the event number; a state's override is recorded without it (-1).
  int v = -1;
  if (func == s_ctrlFn && parms) {
    v = 0;
    memcpy(&v, (const char*)parms + s_ctrlParm.offset, s_ctrlParm.size);
  }
  ev("control_event", v, func == s_ctrlFn ? "ControlEvent" : "ControlEvent (another function)");
}

static void subscribe_all(void) {
  for (int k = 0; k < PE_COUNT; k++)
    if (s_peSub[k] < 0) s_peSub[k] = pehook_subscribe(PE_NAMES[k], on_pe, "arcade");
}

static void enable_all(int on) {
  for (int k = 0; k < PE_COUNT; k++) {
    if (on && !s_peName[k]) s_peName[k] = uname_find(PE_NAMES[k]);
    pehook_enable(s_peSub[k], on);
  }
}

// Object.GetStateName() on the machine: the state's name index, 0 when not called.
static int state_name(const void* o) {
  static const void* s_fn;
  static ScallParm s_rv;
  static int s_ok;
  const ScallFn f = scall_find((void*)o, L"GetStateName");
  if (!f.ok) return 0;
  if (f.fn != s_fn) {
    s_fn = f.fn;
    s_ok = scall_parm(f, L"ReturnValue", L"NameProperty", &s_rv) && s_rv.size == 4;
  }
  unsigned char parms[64];
  if (!s_ok || f.parmsSize > sizeof parms) return 0;
  memset(parms, 0, sizeof parms);
  if (!scall_call((void*)o, f, parms, sizeof parms)) return 0;
  int n;
  memcpy(&n, parms + s_rv.offset, 4);
  return n;
}

// --- Session start: the Tag pass, offsets and the header ------------------------------------------------------------
static void refresh_keys(void) {
  ln_refresh(&s_actorKeys, ACTOR_TAGS, AK_COUNT, ln_find_ascii, 0);
  const char* dn[DT_COUNT];
  for (int i = 0; i < DT_COUNT; i++) dn[i] = DISP_TAGS[i].tag;
  ln_refresh(&s_dispKeys, dn, DT_COUNT, ln_find_ascii, 0);
  ln_report("arcade", &s_actorKeys, ACTOR_TAGS, AK_COUNT);
  ln_report("arcade dispatchers", &s_dispKeys, dn, DT_COUNT);
}

// One object-table pass: this level's actors whose Tag is a key (the first of each) and every Dispatcher whose Tag is a
// dispatcher key (WinGameDispatcher has two). Tag and Level are Actor's, read only on objects whose class is an Actor.
static void find_tagged(void) {
  static const wchar_t* const kCls[2] = { L"Actor", L"Dispatcher" };
  const void* cls[2] = { 0, 0 };
  if (!uobj_find_classes(kCls, cls, 2) || !cls[0]) {
    np_log("arcade: class Actor not found: no tagged actor resolves");
    return;
  }
  const UProp tp = prop_on_class(cls[0], N_TAG), lp = prop_on_class(cls[0], N_LEVEL);
  if (!tp.ok || tp.type != UW_OTHER || tp.elemSize != 4 || !lp.ok || lp.type != UW_OBJECT) {
    np_log("arcade: Actor.Tag or Actor.Level did not resolve: no tagged actor resolves");
    return;
  }
  if (cls[1]) {
    const UProp dp = prop_on_class(cls[1], N_DISPATCHING), ip = prop_on_class(cls[1], N_INDEX);
    if (dp.ok && dp.type == UW_BOOL && dp.bitmask && ip.ok && ip.type == UW_INT) {
      s.dispOn.off = dp.offset;
      s.dispOn.bit = dp.bitmask;
      s.dispIndex.off = ip.offset;
    }
  }
  // Classes seen in this pass: 0 not an Actor, 1 an Actor, 2 a Dispatcher. Direct-mapped by address.
  struct Kind { const void* cls; int kind; };
  static Kind cache[512];
  memset(cache, 0, sizeof cache);
  const int n = uobj_count();
  __try {
    for (int i = 0; i < n; i++) {
      const void* o = uobj_at(i);
      if (!o) continue;
      const void* c = uobj_class(o);
      if (!c) continue;
      Kind& k = cache[((uintptr_t)c >> 4) & 511];
      if (k.cls != c) {
        k.cls = c;
        k.kind = !uobj_class_isa(c, cls[0]) ? 0 : cls[1] && uobj_class_isa(c, cls[1]) ? 2 : 1;
      }
      if (!k.kind || uw_ptr(o, lp.offset) != s.level) continue;
      const int tag = uw_int(o, tp.offset);
      if (!tag) continue;
      if (k.kind == 2) {
        for (int d = 0; d < DT_COUNT; d++)
          if (tag == s_dispKeys.cur[d] && s.nDisp < DISP_MAX) {
            s.disp[s.nDisp].obj = o;
            s.disp[s.nDisp].key = d;
            ++s.nDisp;
            ++s.dispFound[d];
            break;
          }
        continue;
      }
      for (int a = 0; a < AK_COUNT; a++)
        if (tag == s_actorKeys.cur[a] && !s.act[a].obj) {
          s.act[a].obj = o;
          break;
        }
    }
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
  }
}

static bool rd_vec(const void* o, Ofs f, float v[3]) {
  if (!f.off) return false;
  __try {
    memcpy(v, (const char*)o + f.off, 12);
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

static int rd_int_guarded(const void* o, Ofs f, int* out) {
  if (!f.off) return 0;
  __try {
    *out = uw_int(o, f.off);
    return 1;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
}

// One header row per tagged actor, resolved or not; a Variable's row names the property its value is read from.
static void actor_headers(void) {
  for (int k = 0; k < AK_COUNT; k++) {
    Act& a = s.act[k];
    if (!a.obj) {
      header("H,actor,%s,resolved,0", ACTOR_TAGS[k]);
      continue;
    }
    float loc[3] = { 0, 0, 0 };
    const int locOk = rd_vec(a.obj, a.loc, loc);
    int hid = -1, w = 0;
    if (a.hidden.off && rd_int_guarded(a.obj, a.hidden, &w)) hid = (w & (int)a.hidden.bit) != 0;
    char hps[32] = "-";
    if (a.health.off && rd_int_guarded(a.obj, a.health, &w)) {
      float hp;
      memcpy(&hp, &w, 4);
      _snprintf_s(hps, sizeof hps, _TRUNCATE, "%.1f", hp);
    }
    const wchar_t* objName = uname_str(name_of(a.obj));
    const wchar_t* clsName = uname_str(name_of(class_of(a.obj)));
    if (k >= AK_VAR0 && k < AK_VAR0 + AT_VARS) {
      int v = 0, c = 0;
      const int vOk = a.value.off && rd_int_guarded(a.obj, a.value, &v);
      const Ofs cmp = resolve(a.obj, N_CMP, UW_INT);
      const int cOk = cmp.off && rd_int_guarded(a.obj, cmp, &c);
      header("H,variable,%s,resolved,%d,object,%ls,class,%ls,property,Value,offset,%d,value,%d,ComparisonValue,%d",
             ACTOR_TAGS[k], vOk, objName, clsName, a.value.off, vOk ? v : 0, cOk ? c : 0);
      continue;
    }
    header("H,actor,%s,resolved,%d,object,%ls,class,%ls,location,%.2f,%.2f,%.2f,bHidden,%d,Health,%s", ACTOR_TAGS[k],
           locOk, objName, clsName, loc[0], loc[1], loc[2], hid, hps);
  }
}

static void begin(void* player, const void* ctrl) {
  memset(&s, 0, sizeof s);
  memset(s_n, 0, sizeof s_n);
  s_hdrUsed = s_hdrFlushed = 0;
  s.ctrl = ctrl;
  s.player = player;
  s.stage = 1;
  QueryPerformanceCounter(&s.t0);
  ++s_sessions;
  open_file();
  refresh_keys();
  const Ofs lv = resolve(player, N_LEVEL, UW_OBJECT);
  if (lv.off) {
    __try {
      s.level = uw_ptr(player, lv.off);
    } __except (av_only(GetExceptionCode())) {
      ++s_faults;
    }
  }
  if (!live(s.level)) s.level = 0;
  if (s.level) {
    s.time = resolve(s.level, N_TIME, UW_FLOAT);
    find_tagged();
  }
  int unresolved = 0, found = 0;
  for (int k = 0; k < AK_COUNT; k++) {
    Act& a = s.act[k];
    if (!a.obj) continue;
    ++found;
    a.loc = resolve(a.obj, N_LOCATION, UW_STRUCT);
    a.hidden = resolve(a.obj, N_HIDDEN, UW_BOOL);
    if (k >= AK_SOUL0 && k < AK_SOUL0 + AT_SOULS) a.health = resolve(a.obj, N_HEALTH, UW_FLOAT);
    if (k == AK_TANK) a.health = resolve(a.obj, N_HEALTH, UW_FLOAT);
    if (k >= AK_VAR0 && k < AK_VAR0 + AT_VARS) {
      a.value = resolve(a.obj, N_VALUE, UW_INT);
      unresolved += !a.value.off;
    } else {
      unresolved += !a.loc.off + !a.hidden.off;
    }
  }
  for (int i = 0; i < 3; i++) s.machine[i] = resolve(ctrl, MACHINE_NAME[i], UW_BOOL);
  for (int i = 0; i < IN_COUNT; i++) s.in[i] = resolve(player, IN_NAME[i], UW_BOOL);
  for (int i = 0; i < 3; i++) unresolved += !s.machine[i].off;
  for (int i = 0; i < IN_COUNT; i++) unresolved += !s.in[i].off;
  unresolved += !s.time.off + !s.dispOn.off + !s.dispIndex.off;
  // The machine's own ControlEvent and its one parameter, so the handler compares a pointer only.
  s_ctrlFn = 0;
  const ScallFn cf = scall_find((void*)ctrl, L"ControlEvent");
  ScallParm cp;
  if (cf.ok && scall_parm(cf, L"ControlEvent", L"ByteProperty", &cp) && cp.size == 1) {
    s_ctrlFn = cf.fn;
    s_ctrlParm = cp;
  }
  const Ofs tagOfs = resolve(ctrl, N_TAG, UW_OTHER);
  int ctrlTag = 0;
  if (tagOfs.off) rd_int_guarded(ctrl, tagOfs, &ctrlTag);
  const int nukeTank = ctrlTag && ctrlTag == s_actorKeys.cur[AK_MACHINE];
  int dispTags = 0;
  for (int d = 0; d < DT_COUNT; d++) dispTags += s.dispFound[d] > 0;
  SYSTEMTIME st;
  GetLocalTime(&st);
  header("H,session,%u,map,%ls,control,%ls,tag,%ls,nukeTank,%d,cabinetByTag,%d,started,%04u-%02u-%02u %02u:%02u:%02u,"
         "fps,%.1f,actors,%d/%d,dispatchers,%d/%d tags %d object(s),unresolved,%d,gamespeed,%s,%d", s_sessions,
         uname_str(world_map_name()), uname_str(name_of(class_of(ctrl))), uname_str(ctrlTag), nukeTank,
         s.act[AK_MACHINE].obj == ctrl, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
         pb_fps(s_dts, 120), found, AK_COUNT, dispTags, DT_COUNT, s.nDisp, unresolved, g_gameSpeedOn ? "on" : "off",
         g_cfg.gameSpeedPct);
  actor_headers();
  for (int d = 0; d < DT_COUNT; d++)
    header("H,dispatcher,%s,kind,%s,%d,found,%d", DISP_TAGS[d].tag, DK_LABEL[DISP_TAGS[d].kind], DISP_TAGS[d].value,
           s.dispFound[d]);
  // The lanes: the enemy souls' Y at session start (each falls in Z only in its own lane, arcade.md section 2).
  char lanes[160];
  int o = _snprintf_s(lanes, sizeof lanes, _TRUNCATE, "H,lanes");
  for (int i = 0; i < AT_ENEMIES; i++) {
    float loc[3];
    const Act& a = s.act[AK_SOUL0 + i];
    s.haveLane[i] = a.obj && rd_vec(a.obj, a.loc, loc);
    if (s.haveLane[i]) s.laneY[i] = loc[1];
    if (o >= 0 && o < (int)sizeof lanes) {
      const int w = s.haveLane[i] ? _snprintf_s(lanes + o, sizeof lanes - o, _TRUNCATE, ",%.2f", s.laneY[i])
                                  : _snprintf_s(lanes + o, sizeof lanes - o, _TRUNCATE, ",");
      o = w < 0 ? -1 : o + w;
    }
  }
  header("%s", lanes);
  header("H,processevent,ControlEvent,%d,DnControlFireEnable,%d,DnControlFireDisable,%d", s_ctrlFn != 0,
         uname_find(PE_NAMES[PE_FIRE_ON]) != 0, uname_find(PE_NAMES[PE_FIRE_OFF]) != 0);
  header("H,columns,%s", AT_FRAME_COLUMNS);
  header("H,columns,%s", AT_EVENT_COLUMNS);
  const Act& tv = s.act[AK_VAR0];
  np_log("arcade: tracing %ls (%p, Tag %ls%s) on %ls: %d of %d tagged actor(s), %d of %d dispatcher tag(s) (%d "
         "object(s)), %d propert(ies) unresolved; score Variables read from Variable.Value (%s); ProcessEvent "
         "ControlEvent %s", uname_str(name_of(class_of(ctrl))), ctrl, uname_str(ctrlTag),
         nukeTank ? "" : ", not the Nuke Tank cabinet's", uname_str(world_map_name()), found, AK_COUNT, dispTags,
         DT_COUNT, s.nDisp, unresolved, tv.obj ? (tv.value.off ? "resolved" : "did not resolve") : "TotalScore not found",
         s_ctrlFn ? "watched" : "unresolved");
  s_active = 1;
  enable_all(1);
}

static const char* iv(char* b, int cap, int v) {
  if (v == AT_UNSET) _snprintf_s(b, cap, _TRUNCATE, "?");
  else _snprintf_s(b, cap, _TRUNCATE, "%d", v);
  return b;
}

static void end(const char* why) {
  if (!s_active) return;
  s_active = 0;
  enable_all(0);
  const double secs = ms_since(s.t0) / 1000.0;
  ev("session_end", s.haveWas && s.was.var[0] != AT_UNSET ? s.was.var[0] : 0, why);
  flush();
  char v[5][16], gained[16];
  for (int i = 0; i < AT_VARS; i++) iv(v[i], 16, s.haveWas ? s.was.var[i] : AT_UNSET);
  const int g = s.haveWas && s.was.var[0] != AT_UNSET && s.varStart[0] != AT_UNSET ? s.was.var[0] - s.varStart[0]
                                                                                    : AT_UNSET;
  iv(gained, 16, g);
  // The autoplay's part: presses and releases per input, whether they balance, what it won and lost while on.
  char bot[320] = "";
  if (s.bot.on) {
    const AtBot& b = s.bot;
    _snprintf_s(bot, sizeof bot, _TRUNCATE,
                "; autoplay: Fire %u press(es) / %u release(s), Left %u / %u, Right %u / %u, taken by the player "
                "%u/%u/%u, dropped %u/%u/%u (%s); while on: kills %u, lives lost %u, wins %u, game overs %u; result %s; "
                "%u bot row(s)", b.press[0], b.release[0], b.press[1], b.release[1], b.press[2], b.release[2],
                b.taken[0], b.taken[1], b.taken[2], b.dropped[0], b.dropped[1], b.dropped[2],
                at_bot_balanced(b) ? "balanced" : "UNBALANCED", b.kills, b.livesLost, b.wins, b.gameOvers,
                b.wins ? "won" : b.gameOvers ? "game over" : "unfinished", s.botRows);
  }
  np_log("arcade: %s after %.1f s (game time %.1f s, game speed %.2fx, frame delta mean %.2f max %.2f ms): kills by "
         "dispatch %u (enemies %u, bombs %u, boss %u), hits %u; TotalScore %s (gained %s), stage scores %s/%s/%s, "
         "BigBadSpawnScore %s; lives lost by dispatch %u, by the Life tanks %u; stage reached %d, boss %s, win %u, "
         "game over %u; dispatches menu %u, attack %u, touch-down %u, stage %u; edges: pawn input %u, machine %u, "
         "hidden %u, score rows %u; through ProcessEvent: exec %u, ControlEvent %u; %u trace row(s) (%u dropped at the "
         "cap), %u flush(es), longest write %.1f ms; %u fault(s)%s%s%s",
         why, secs, s.haveWas ? s.was.time - s.time0 : 0.f,
         pb_speed_ratio(s.haveWas ? s.was.time - s.time0 : 0.f, (float)secs), pb_dt_mean(s.gameDt), s.gameDt.max,
         s.dk[DK_KILL], s.kills[0], s.kills[1], s.kills[2], s.hits, v[0], gained, v[1], v[2], v[3], v[4],
         s.dk[DK_LIFE], s.livesLost, s.stage, s.bossSeen ? "seen" : "not seen", s.dk[DK_WIN], s.dk[DK_GAMEOVER],
         s.dk[DK_MENU], s.dk[DK_ATTACK], s.dk[DK_TOUCHDOWN], s.dk[DK_STAGE], s.inputEdges, s.machineEdges,
         s.hiddenEdges, s.scoreRows, s.peExec, s.peCtrl, s.rows, s.dropped, s_flushes, s_longestWriteMs, s_faults,
         s_fileWhy ? "; no file: " : "", s_fileWhy ? s_fileWhy : "", bot);
  s_faults = 0;
  s_flushes = 0;
  s_longestWriteMs = 0.0;
}

// --- Per frame -------------------------------------------------------------------------------------------------------
// One guarded pass over every tagged actor, the Variables, the machine's and the pawn's flags, the Dispatchers and
// the level's time. An actor that left the object table reads as missing.
static bool read_frame(Read* r) {
  memset(r, 0, sizeof *r);
  for (int i = 0; i < AT_VARS; i++) r->var[i] = AT_UNSET;
  __try {
    if (!uobj_valid(s.ctrl)) return false;
    for (int k = 0; k < AK_COUNT; k++) {
      const Act& a = s.act[k];
      AtActor& o = r->act[k];
      if (!a.obj || !uobj_valid(a.obj)) continue;
      if (k >= AK_VAR0 && k < AK_VAR0 + AT_VARS) {
        if (a.value.off) r->var[k - AK_VAR0] = uw_int(a.obj, a.value.off);
        continue;
      }
      if (!a.loc.off) continue;
      o.have = 1;
      memcpy(o.pos, (const char*)a.obj + a.loc.off, 12);
      o.hidden = rd_b(a.obj, a.hidden);
      if (a.health.off) {
        o.hasHealth = 1;
        memcpy(&o.health, (const char*)a.obj + a.health.off, 4);
        if (o.health != o.health) o.health = 0.f;   // NaN
      }
    }
    for (int i = 0; i < 3; i++) r->machine[i] = rd_b(s.ctrl, s.machine[i]);
    if (uobj_valid(s.player))
      for (int i = 0; i < IN_COUNT; i++) r->in[i] = rd_b(s.player, s.in[i]);
    if (s.dispOn.off && s.dispIndex.off)
      for (int d = 0; d < s.nDisp; d++)
        if (uobj_valid(s.disp[d].obj)) {
          r->dOn[d] = (unsigned char)rd_b(s.disp[d].obj, s.dispOn);
          r->dIndex[d] = rd_i(s.disp[d].obj, s.dispIndex);
        }
    if (s.level && s.time.off && uobj_valid(s.level)) memcpy(&r->time, (const char*)s.level + s.time.off, 4);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
  return true;
}

static int lives_of(const Read& r) {
  int have[AT_LIVES], hid[AT_LIVES];
  for (int i = 0; i < AT_LIVES; i++) {
    have[i] = r.act[AK_LIFE0 + i].have;
    hid[i] = r.act[AK_LIFE0 + i].hidden;
  }
  return at_lives(have, hid, AT_LIVES);
}

static void frame_events(const Read& r) {
  char d[128];
  for (int i = 0; i < IN_COUNT; i++) {
    const int n = pb_flag_event(IN_BASE[i], s.was.in[i], r.in[i]);
    if (n < 0) continue;
    ++s.inputEdges;
    _snprintf_s(d, sizeof d, _TRUNCATE, "%s %s", IN_LABEL[i], r.in[i] ? "pressed" : "released");
    ev("input", n, d);
  }
  for (int i = 0; i < 3; i++)
    if (r.machine[i] != s.was.machine[i]) {
      ++s.machineEdges;
      ev("machine", r.machine[i], MACHINE_LABEL[i]);
    }
  // The tank and the souls: hidden edges, and each Health drop (a hit) or rise (a reset).
  for (int k = AK_TANK; k < AK_SOUL0 + AT_SOULS; k++) {
    const AtActor &a = r.act[k], &w = s.was.act[k];
    if (a.have && w.have && a.hidden != w.hidden) {
      ++s.hiddenEdges;
      ev("hidden", a.hidden, ACTOR_TAGS[k]);
    }
    if (at_hit(w.hasHealth, w.health, a.hasHealth, a.health)) {
      ++s.hits;
      _snprintf_s(d, sizeof d, _TRUNCATE, "%s %.1f -> %.1f", ACTOR_TAGS[k], w.health, a.health);
      ev("hit", (int)a.health, d);
    } else if (w.hasHealth && a.hasHealth && a.health > w.health) {
      _snprintf_s(d, sizeof d, _TRUNCATE, "%s %.1f -> %.1f", ACTOR_TAGS[k], w.health, a.health);
      ev("health_reset", (int)a.health, d);
    }
  }
  const int lives = lives_of(r), livesWas = lives_of(s.was);
  if (lives != livesWas && lives != AT_UNSET && livesWas != AT_UNSET) {
    if (lives < livesWas) ++s.livesLost;
    _snprintf_s(d, sizeof d, _TRUNCATE, "Life tanks shown, was %d", livesWas);
    ev("lives", lives, d);
  }
  for (int i = 0; i < AT_VARS; i++)
    if (r.var[i] != s.was.var[i] && r.var[i] != AT_UNSET && s.was.var[i] != AT_UNSET) {
      ++s.scoreRows;
      _snprintf_s(d, sizeof d, _TRUNCATE, "%s %d", ACTOR_TAGS[AK_VAR0 + i], r.var[i]);
      ev("score", r.var[i] - s.was.var[i], d);
    }
  for (int i = 0; i < s.nDisp; i++) {
    if (!at_disp_started(s.was.dOn[i], s.was.dIndex[i], r.dOn[i], r.dIndex[i])) continue;
    const DispTag& t = DISP_TAGS[s.disp[i].key];
    ++s.dk[t.kind];
    if (t.kind == DK_KILL) ++s.kills[t.value <= AT_ENEMIES ? 0 : t.value <= AT_ENEMIES + AT_BOMBS ? 1 : 2];
    if (t.kind == DK_STAGE && t.value > s.stage) s.stage = t.value;
    if (t.kind == DK_BOSS) s.bossSeen = 1;
    if (t.kind == DK_MENU && t.value == 0) s.stage = 1;   // StartArcGameDispatcher: a new game
    if (t.kind == DK_GAMEOVER) s.gameOverAt = r.time;
    ev(DK_LABEL[t.kind], t.value, t.tag);
  }
}

static void step(void) {
  Read r;
  if (!read_frame(&r)) return;
  if (!s.haveWas) {
    s.was = r;   // no edges on the first frame
    s.time0 = r.time;
    memcpy(s.varStart, r.var, sizeof s.varStart);
  }
  AtFrame f;
  memset(&f, 0, sizeof f);
  f.frame = s.frame;
  f.gameTime = r.time;
  f.ms = ms_since(s.t0);
  f.dtMs = s.haveWas ? (double)(r.time - s.was.time) * 1000.0 : 0.0;
  pb_dt_add(&s.gameDt, (float)f.dtMs);
  f.tank = r.act[AK_TANK];
  f.lane = f.tank.have ? at_lane(s.laneY, s.haveLane, AT_ENEMIES, f.tank.pos[1], &f.laneOff) : -1;
  for (int i = 0; i < AT_SOULS; i++) f.soul[i] = r.act[AK_SOUL0 + i];
  f.lives = lives_of(r);
  f.total = r.var[0];
  for (int i = 0; i < 3; i++) f.stage[i] = r.var[1 + i];
  f.bigBad = r.var[4];
  f.fire = r.machine[0];
  f.left = r.machine[1];
  f.right = r.machine[2];
  f.inFire = r.in[IN_FIRE];
  f.inLeft = r.in[IN_LEFT];
  f.inRight = r.in[IN_RIGHT];
  if (s.haveWas) frame_events(r);
  const int st = state_name(s.ctrl);
  if (st && st != s.stateName) {
    char d[96];
    _snprintf_s(d, sizeof d, _TRUNCATE, "%ls", uname_str(st));
    ev("state", st, d);
    s.stateName = st;
  }
  char row[768];
  emit(row, at_fmt_frame(row, sizeof row, f));
  s.was = r;
  s.haveWas = 1;
  s.lastDt = (float)f.dtMs;
  s.readAt = s_frameNo;
  ++s.frame;
}

// The control the player uses when it is an arcade machine (dnControl_ArcadeMachine on its class chain), else null.
// Cached per class, cleared per level (the class is a DLC package's: its name moves across level loads).
static const void* arcade_in_use(void* player) {
  static const void* s_cls[8];
  static int s_is[8];
  static int s_name;
  static unsigned s_lv;
  if (s_lv != world_level_serial()) {
    s_lv = world_level_serial();
    memset(s_cls, 0, sizeof s_cls);
    s_name = 0;
    s_n[N_INTERACTIVE] = 0;
  }
  const Ofs f = resolve(player, N_INTERACTIVE, UW_OBJECT);
  if (!f.off) return 0;
  const void* ctrl = 0;
  __try {
    ctrl = uw_ptr(player, f.off);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
  if (!ctrl || !live(ctrl)) return 0;
  const void* cls = class_of(ctrl);
  if (!cls) return 0;
  const int slot = ((uintptr_t)cls >> 4) & 7;
  if (s_cls[slot] == cls) return s_is[slot] ? ctrl : 0;
  if (!s_name) s_name = uname_find(L"dnControl_ArcadeMachine");
  int is = 0;
  __try {
    const void* c = cls;
    for (int d = 0; c && !is && d < 32; d++, c = uw_ptr(c, US_SUPER)) is = s_name && uobj_name(c) == s_name;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
  s_cls[slot] = cls;
  s_is[slot] = is;
  return is ? ctrl : 0;
}

void arctrace_frame(int liveNow) {
  ++s_frameNo;
  if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  if (s_lastQpc.QuadPart) {
    s_dts[s_dtAt] = (float)((double)(now.QuadPart - s_lastQpc.QuadPart) * 1000.0 / (double)s_qpf.QuadPart);
    s_dtAt = (s_dtAt + 1) % 120;
  }
  s_lastQpc = now;
  subscribe_all();
  const unsigned level = world_level_serial();
  if (level != s_level) {
    // The autoplay forgets what it held with the level (the pawn went with it): dropped, not unbalanced.
    for (int k = 0; s_active && k < 3; k++) s.bot.dropped[k] += s.bot.held[k] != 0;
    end(s_endWhy ? s_endWhy : "level changed");   // the last level's pointers are stale: nothing read
    s_endWhy = 0;
    s_level = level;
  }
  void* player = world_player();
  if (liveNow && player && !mp_refused()) {
    const void* ctrl = arcade_in_use(player);
    if (s_active && ctrl != s.ctrl) {
      // The frame the player leaves: the machine is read once more; the session stays open for the autoplay's release
      // rows until arctrace_frame_end. Another machine starts next frame.
      step();
      s_endWhy = "left the machine";
    } else {
      if (!s_active && ctrl) begin(player, ctrl);
      if (s_active) step();
    }
  }
  if (pb_flush_due(GetTickCount(), s_lastFlushMs, s_used, (int)sizeof s_buf, 0)) flush();
}

void arctrace_frame_end(void) {
  if (!s_endWhy) return;
  end(s_endWhy);
  s_endWhy = 0;
}

int arctrace_view(AtView* v) {
  memset(v, 0, sizeof *v);
  if (!s_active) return 0;
  static int s_idle;
  if (!s_idle) s_idle = uname_find(L"idle");
  v->active = 1;
  v->attached = !s_endWhy;
  v->fresh = s.haveWas && s.readAt == s_frameNo;
  v->gameSecs = s.was.time;
  v->frameGameMs = s.lastDt;
  v->player = s.player;
  v->ctrl = s.ctrl;
  v->tank = s.was.act[AK_TANK];
  for (int i = 0; i < AT_SOULS; i++) v->soul[i] = s.was.act[AK_SOUL0 + i];
  memcpy(v->laneY, s.laneY, sizeof v->laneY);
  memcpy(v->haveLane, s.haveLane, sizeof v->haveLane);
  v->idle = s_idle && s.stateName == s_idle;
  v->inFire = s.was.in[IN_FIRE];
  v->inLeft = s.was.in[IN_LEFT];
  v->inRight = s.was.in[IN_RIGHT];
  v->gameOverAt = s.gameOverAt;
  v->kills = s.dk[DK_KILL];
  v->livesLost = s.dk[DK_LIFE];
  v->wins = s.dk[DK_WIN];
  v->gameOvers = s.dk[DK_GAMEOVER];
  return 1;
}

void arctrace_bot_row(const char* detail) {
  if (!s_active) return;
  ++s.botRows;
  ev("bot", 0, detail);
}

void arctrace_bot_counts(const AtBot& b) {
  if (s_active) s.bot = b;
}

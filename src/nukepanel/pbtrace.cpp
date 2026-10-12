// Pinball trace (pbtrace.h; format: minigame-autoplay.md 6). A session is one use of a dnControl_Pinball(2). Properties
// are resolved by name once per session and read each frame in one guarded pass; the trace goes out from a buffer at
// most once a second. Game thread only; the ProcessEvent handlers run on it too (pehook.h).
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "pbtrace.h"
#include "world.h"
#include "scall.h"
#include "scallparms.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "pehook.h"
#include "paths.h"
#include "gamespeed.h"
#include "config.h"
#include "log.h"

enum { N_INTERACTIVE, N_LEVEL, N_TIME, N_LOCATION, N_ROTATION, N_BALL, N_LFLIP, N_RFLIP, N_BUP, N_LBTN, N_RBTN,
       N_PLUNGER, N_SCORE, N_BALLS, N_MULT, N_GAMEOVER, N_GUTTER, N_TILTED, N_FIRE, N_MELEE, N_FORWARD, N_BACKWARD,
       N_LEFT, N_RIGHT, N_RADIUS, N_HEIGHT, N_DRAWSCALE, N_DRAWSCALE3D, N_COLLIDE, N_BLOCKKARMA, N_PHYSICS, N_COUNT };
static const wchar_t* const NAMES[N_COUNT] = {
  L"InteractiveDecoration", L"Level", L"TimeSeconds", L"Location", L"Rotation", L"PrimaryBall", L"LeftFlippers",
  L"RightFlippers", L"bUp", L"bLeftButton", L"bRightButton", L"bAllowPlunger", L"Score", L"BallsLeft",
  L"ScoreMultiplier", L"bGameOver", L"bBallInGutter", L"bTilted", L"bIDnControlFire", L"bIDnControlMelee",
  L"bIForward", L"bIBackward", L"bIStrafeLeft", L"bIStrafeRight", L"CollisionRadius", L"CollisionHeight",
  L"DrawScale", L"DrawScale3D", L"bCollideActors", L"bBlockKarma", L"Physics" };
static int s_n[N_COUNT];

// The pawn's input flags and the control event base each edge raises (pbtrace.h pb_flag_event).
enum { IN_FIRE, IN_MELEE, IN_FORWARD, IN_BACKWARD, IN_LEFT, IN_RIGHT, IN_COUNT };
static const int IN_NAME[IN_COUNT] = { N_FIRE, N_MELEE, N_FORWARD, N_BACKWARD, N_LEFT, N_RIGHT };
static const int IN_BASE[IN_COUNT] = { 14, 17, 20, 23, 26, 29 };
static const char* const IN_LABEL[IN_COUNT] = { "bIDnControlFire", "bIDnControlMelee", "bIForward", "bIBackward",
                                                "bIStrafeLeft", "bIStrafeRight" };

// UFunction ParmsSize (u16 at +0x92, scall.cpp UFN_PARMSSIZE).
static const int UFN_PARMSSIZE = 0x92;

// A property's offset (0 unresolved) and, for a bool, its bit.
struct Ofs { int off; unsigned bit; };
enum { FLIP_MAX = 4 };

// What one frame reads.
struct Read { int score, balls, mult, plunger, gameOver, gutter, tilted, lBtn, rBtn, in[IN_COUNT];
              unsigned lUp, rUp; const void* ball; float time; };

struct Session {
  const void* ctrl;
  void* player;
  const void* level;
  const void* ballCls;
  const void* ball;
  const void* flip[2][FLIP_MAX];
  int nFlip[2];
  float flipLoc[2][FLIP_MAX][2];   // table-local pivot and localYaw, read with the header
  int flipYaw[2][FLIP_MAX];
  Ofs score, balls, mult, plunger, gameOver, gutter, tilted, lBtn, rBtn, ballPtr, in[IN_COUNT], time, flipUp,
      ballLoc;
  float tableLoc[3];
  int tableRot[3];
  Read was;
  int haveWas;
  float pos[3], vel[3];
  int havePos;
  char vsrc;
  int stateName;
  unsigned frame, rows, dropped, capNoted, velK, velD, lost, saves, gameOverSeen, peExec, peCtrl, peAward;
  int score0;                                  // the score on the first frame read, or at a ResetTable
  PbDtStats gameDt;                            // game-time frame deltas
  float frameGameMs;                           // the last one
  float prevPos[3], prevVel[3];                // the ball one frame back: its last place before a drain
  double pressMs;                              // the last flipper press seen through ProcessEvent (bot or player)
  int pressSeen;
  unsigned readAt;                             // s_frameNo of the last frame read
  unsigned botRows, botHits[2], botMisses[2];
  LARGE_INTEGER t0;
  float time0;
};
static Session s;
static int s_active;
static unsigned s_sessions, s_level;
static unsigned s_frameNo;   // pbtrace_frame calls, for PbView.fresh
static const char* s_endWhy; // the player left this frame: pbtrace_frame_end closes the session

// pehook: the exec functions on the player, ControlEvent and AwardPoints on the control (minigame-autoplay.md
// section 2). A key's own path is script to script and may never pass ProcessEvent; the polled flags are the record.
enum { PE_FIRE_ON, PE_FIRE_OFF, PE_MELEE_ON, PE_MELEE_OFF, PE_CTRL, PE_AWARD, PE_COUNT };
static const wchar_t* const PE_NAMES[PE_COUNT] = { L"DnControlFireEnable", L"DnControlFireDisable",
                                                   L"DnControlMeleeEnable", L"DnControlMeleeDisable", L"ControlEvent",
                                                   L"AwardPoints" };
static const char* const PE_LABELS[PE_COUNT] = { "DnControlFireEnable", "DnControlFireDisable", "DnControlMeleeEnable",
                                                 "DnControlMeleeDisable", "ControlEvent", "AwardPoints" };
static int s_peSub[PE_COUNT] = { -1, -1, -1, -1, -1, -1 };
static int s_peName[PE_COUNT];
static const void* s_peFn[PE_COUNT];     // the control's ControlEvent / AwardPoints, resolved per session
static ScallParm s_peParm[PE_COUNT];

// The trace file and its buffer.
static HANDLE s_file = INVALID_HANDLE_VALUE;
static const char* s_fileWhy;
static wchar_t s_path[MAX_PATH], s_dir[MAX_PATH];
static unsigned s_fileBytes, s_rotations, s_failStreak;
// This session's header rows, to start a rotated file with; s_hdrFlushed of them already went to the file (the rest
// are still in s_buf and go to the new file with it).
static char s_hdr[8192];
static int s_hdrUsed, s_hdrFlushed;
static char s_buf[64 * 1024];
static int s_used;
static unsigned s_lastFlushMs, s_flushes;
static double s_longestWriteMs;
static LARGE_INTEGER s_qpf;

// Frame times for the header's frame rate, kept whether or not a session runs.
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

static int class_name_of(const void* o) {
  const void* c = class_of(o);
  if (!c) return 0;
  __try {
    return uobj_name(c);
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
}

// o's property k when it has type `type` (a bool: its bit too); off 0 when it does not resolve.
static Ofs resolve(const void* o, int k, int type) {
  Ofs r = { 0, 0 };
  const void* cls = class_of(o);
  const int n = nm(k);
  if (!cls || !n) return r;
  UProp p;
  __try {
    p = uprop_get(cls, n);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return r;
  }
  if (!p.ok || p.type != type || p.offset <= 0 || (type == UW_BOOL && !p.bitmask)) return r;
  r.off = p.offset;
  r.bit = p.bitmask;
  return r;
}

// Raw reads at a resolved offset; the caller holds the guard.
static int rd_i(const void* o, Ofs f) { return f.off ? uw_int(o, f.off) : 0; }
static int rd_b(const void* o, Ofs f) { return f.off ? (uw_int(o, f.off) & (int)f.bit) != 0 : 0; }

// --- The file ------------------------------------------------------------------------------------------------------
// Opens s_path after moving an existing file at it to `keep` (pb_open_choice): reused only once renamed away, else a
// new pinball-trace-<tick>.csv beside it (CREATE_NEW: never truncates either). Logs which happened.
static void open_into(const wchar_t* keep, const char* why) {
  const int existed = GetFileAttributesW(s_path) != INVALID_FILE_ATTRIBUTES;
  const int renamed = existed && keep && MoveFileExW(s_path, keep, MOVEFILE_REPLACE_EXISTING);
  const unsigned long err = existed && !renamed ? GetLastError() : 0;
  if (pb_open_choice(existed, renamed) == PB_OPEN_UNIQUE) {
    wchar_t u[MAX_PATH];
    if (pb_unique_name(u, MAX_PATH, s_dir, GetTickCount())) {
      np_log("pinball: %s: %ls not renamed (error %lu): tracing into %ls instead, nothing truncated", why, s_path, err,
             u);
      wcscpy_s(s_path, MAX_PATH, u);
      s_file = CreateFileW(s_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    }
  } else {
    if (renamed) np_log("pinball: %s: the trace kept as %ls", why, keep);
    s_file = CreateFileW(s_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  }
  s_fileBytes = 0;
}

// Past PB_FILE_CAP the file starts over, the last one kept as <name>.old (one only); the new file starts with this
// session's header rows already written, so it parses on its own.
static void rotate(void) {
  CloseHandle(s_file);
  s_file = INVALID_HANDLE_VALUE;
  wchar_t old[MAX_PATH];
  ++s_rotations;
  open_into(_snwprintf_s(old, MAX_PATH, _TRUNCATE, L"%ls.old", s_path) > 0 ? old : 0, "trace size cap");
  if (s_file == INVALID_HANDLE_VALUE) {
    s_fileWhy = "the new trace file did not open after the size cap";
    np_log("pinball: no trace file after rotation %u: tracing stopped; sessions are still summarised here",
           s_rotations);
    return;
  }
  DWORD wrote = 0;
  if (s_hdrFlushed > 0 && WriteFile(s_file, s_hdr, (DWORD)s_hdrFlushed, &wrote, NULL)) s_fileBytes += wrote;
  np_log("pinball: trace file passed %u MB: a new %ls (rotation %u), %lu header byte(s) written again",
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
      // Once per streak of failures; the streak's length is logged when a write succeeds again.
      if (!s_failStreak++)
        np_log("pinball: trace write of %d byte(s) failed (%lu written, error %lu); further failures are counted",
               s_used, wrote, GetLastError());
    } else if (s_failStreak) {
      np_log("pinball: trace writes recovered after %u failure(s)", s_failStreak);
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

// Started by the first session of the process (the last launch's file renamed .prev.csv), appended to by the
// next; kept open (the game exits without DLL_PROCESS_DETACH, and each flush is already in the file).
static void open_file(void) {
  if (s_file != INVALID_HANDLE_VALUE || s_fileWhy) return;
  wchar_t* dir = s_dir, *path = s_path;
  if (!np_our_dir(dir, MAX_PATH) || _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%lspinball-trace.csv", dir) < 0) {
    s_fileWhy = "the NukePanel folder's path did not resolve";
  } else {
    CreateDirectoryW(dir, NULL);
    // The last launch's trace is kept as pinball-trace.prev.csv (replacing an older one), never truncated.
    wchar_t prev[MAX_PATH];
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    open_into(_snwprintf_s(prev, MAX_PATH, _TRUNCATE, L"%lspinball-trace.prev.csv", dir) > 0 ? prev : 0,
              "the last launch's trace");
    const double ms = ms_since(t0);
    if (s_file == INVALID_HANDLE_VALUE) {
      s_fileWhy = "CreateFileW failed";
      np_log("pinball: %ls not opened (error %lu, %.1f ms)", path, GetLastError(), ms);
    } else {
      np_log("pinball: trace file %ls opened in %.1f ms", path, ms);
    }
  }
  if (s_fileWhy) np_log("pinball: no trace file: %s; sessions are still summarised here", s_fileWhy);
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
  for (int k = 0; k < PE_COUNT; k++) {
    if (!s_peName[k] || name != s_peName[k]) continue;
    if (k <= PE_MELEE_OFF) {
      if (obj != s.player) return;
      ++s.peExec;
      if (k == PE_FIRE_ON || k == PE_MELEE_ON) {
        s.pressMs = ms_since(s.t0);
        s.pressSeen = 1;
      }
      ev("exec", k == PE_FIRE_ON || k == PE_MELEE_ON, PE_LABELS[k]);
      return;
    }
    if (obj != s.ctrl || func != s_peFn[k] || !parms) return;
    int v = 0;
    memcpy(&v, (const char*)parms + s_peParm[k].offset, s_peParm[k].size);
    if (k == PE_CTRL) ++s.peCtrl;
    else ++s.peAward;
    ev(k == PE_CTRL ? "control_event" : "award", v, PE_LABELS[k]);
    return;
  }
}

static void subscribe_all(void) {
  for (int k = 0; k < PE_COUNT; k++)
    if (s_peSub[k] < 0) s_peSub[k] = pehook_subscribe(PE_NAMES[k], on_pe, "pinball");
}

static void enable_all(int on) {
  for (int k = 0; k < PE_COUNT; k++) {
    if (on && !s_peName[k]) s_peName[k] = uname_find(PE_NAMES[k]);
    pehook_enable(s_peSub[k], on);
  }
}

// The control's own ControlEvent / AwardPoints and their one parameter, so a handler compares a pointer only.
static void resolve_pe_fn(int k, const wchar_t* parm, const wchar_t* cls, unsigned size) {
  s_peFn[k] = 0;
  const ScallFn f = scall_find((void*)s.ctrl, PE_NAMES[k]);
  ScallParm p;
  if (f.ok && scall_parm(f, parm, cls, &p) && p.size == size) {
    s_peFn[k] = f.fn;
    s_peParm[k] = p;
  }
}

// --- Script calls (read-only natives) -----------------------------------------------------------------------------
// KarmaActor.KGetLinearVelocity(out OutLVelocity, BoneName) on the ball: Engine.dll!0x101b6100 reads the Meqon body's
// velocity, 0 without a body (world space). 1 with v filled.
static int k_velocity(const void* ball, float v[3]) {
  static const void* s_fn;
  static ScallParm s_out, s_rv;
  static int s_ok;
  const ScallFn f = scall_find((void*)ball, L"KGetLinearVelocity");
  if (!f.ok) return 0;
  if (f.fn != s_fn) {
    s_fn = f.fn;
    s_ok = scall_parm(f, L"OutLVelocity", L"StructProperty", &s_out) && s_out.size == 12 &&
           scall_parm(f, L"ReturnValue", L"BoolProperty", &s_rv) && s_rv.size == 4;
  }
  unsigned char parms[64];
  if (!s_ok || f.parmsSize > sizeof parms) return 0;
  memset(parms, 0, sizeof parms);   // BoneName None
  if (!scall_call((void*)ball, f, parms, sizeof parms)) return 0;
  int rv;
  memcpy(&rv, parms + s_rv.offset, 4);
  if (!rv) return 0;
  memcpy(v, parms + s_out.offset, 12);
  return 1;
}

// Object.GetStateName() on the control: the state's name index, 0 when not called.
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

// --- Session start: offsets and the header --------------------------------------------------------------------------
// A 12-byte struct property (a vector, or a rotator's three ints).
static bool rd_struct12(const void* o, int k, void* out) {
  const Ofs f = resolve(o, k, UW_STRUCT);
  if (!f.off) return false;
  __try {
    memcpy(out, (const char*)o + f.off, 12);
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}
static bool rd_vec(const void* o, int k, float v[3]) { return rd_struct12(o, k, v); }
static bool rd_rot(const void* o, int k, int r[3]) { return rd_struct12(o, k, r); }
static float rd_float(const void* o, int k) {
  const Ofs f = resolve(o, k, UW_FLOAT);
  float v = -1.f;
  if (!f.off) return v;
  __try {
    memcpy(&v, (const char*)o + f.off, 4);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
  }
  return v;
}
static int rd_bool(const void* o, int k) {
  const Ofs f = resolve(o, k, UW_BOOL);
  if (!f.off) return -1;
  __try {
    return rd_b(o, f);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return -1;
  }
}
static int rd_byte(const void* o, int k) {
  const Ofs f = resolve(o, k, UW_BYTE);
  if (!f.off) return -1;
  __try {
    return *((const unsigned char*)o + f.off);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return -1;
  }
}

// The flippers of an ArrayProperty of the control (TArray {data, count, max}), validated.
static int read_flippers(int k, const void** out) {
  const void* cls = class_of(s.ctrl);
  const int n = nm(k);
  UProp p;
  memset(&p, 0, sizeof p);
  if (cls && n) {
    __try {
      p = uprop_get(cls, n);
    } __except (av_only(GetExceptionCode())) {
      ++s_faults;
    }
  }
  if (!p.ok || p.type != UW_ARRAY) return -1;
  int count = 0;
  __try {
    const void* const* data = (const void* const*)uw_ptr(s.ctrl, p.offset);
    count = uw_int(s.ctrl, p.offset + 4);
    if (count < 0 || count > 64) return -1;
    for (int i = 0; i < count && i < FLIP_MAX; i++) out[i] = uobj_valid(data[i]) ? data[i] : 0;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return -1;
  }
  return count < FLIP_MAX ? count : FLIP_MAX;
}

// An actor's class, place and size for the header (-1 for what did not resolve); its table-local x, y and localYaw
// into loc2/yaw when given.
static void actor_header(const char* what, int index, const void* a, float* loc2 = 0, int* yaw = 0) {
  float loc[3] = { 0, 0, 0 }, ds3[3] = { -1, -1, -1 }, d[3], l[3];
  int rot[3] = { 0, 0, 0 };
  const int locOk = rd_vec(a, N_LOCATION, loc), rotOk = rd_rot(a, N_ROTATION, rot);
  rd_vec(a, N_DRAWSCALE3D, ds3);
  for (int i = 0; i < 3; i++) d[i] = loc[i] - s.tableLoc[i];
  pb_local(d, s.tableRot[0], s.tableRot[1], s.tableRot[2], l);
  header("H,%s,%d,class,%ls,location,%.3f,%.3f,%.3f,local,%.3f,%.3f,%.3f,rotation,%d,%d,%d,localYaw,%d,"
         "CollisionRadius,%.3f,CollisionHeight,%.3f,DrawScale,%.3f,DrawScale3D,%.3f,%.3f,%.3f,bCollideActors,%d,"
         "bBlockKarma,%d,Physics,%d,resolved,%d", what, index, uname_str(class_name_of(a)), loc[0], loc[1], loc[2],
         l[0], l[1], l[2], rot[0], rot[1], rot[2], (int)(short)(rot[1] - s.tableRot[1]), rd_float(a, N_RADIUS),
         rd_float(a, N_HEIGHT), rd_float(a, N_DRAWSCALE), ds3[0], ds3[1], ds3[2], rd_bool(a, N_COLLIDE),
         rd_bool(a, N_BLOCKKARMA), rd_byte(a, N_PHYSICS), locOk && rotOk);
  if (loc2) { loc2[0] = l[0]; loc2[1] = l[1]; }
  if (yaw) *yaw = (int)(short)(rot[1] - s.tableRot[1]);
}

static void begin(void* player, const void* ctrl) {
  memset(&s, 0, sizeof s);
  s_hdrUsed = s_hdrFlushed = 0;
  s.ctrl = ctrl;
  s.player = player;
  s.vsrc = '?';
  QueryPerformanceCounter(&s.t0);
  ++s_sessions;
  open_file();
  // The control.
  s.score = resolve(ctrl, N_SCORE, UW_INT);
  s.balls = resolve(ctrl, N_BALLS, UW_INT);
  s.mult = resolve(ctrl, N_MULT, UW_INT);
  s.plunger = resolve(ctrl, N_PLUNGER, UW_BOOL);
  s.gameOver = resolve(ctrl, N_GAMEOVER, UW_BOOL);
  s.gutter = resolve(ctrl, N_GUTTER, UW_BOOL);
  s.tilted = resolve(ctrl, N_TILTED, UW_BOOL);
  s.lBtn = resolve(ctrl, N_LBTN, UW_BOOL);
  s.rBtn = resolve(ctrl, N_RBTN, UW_BOOL);
  s.ballPtr = resolve(ctrl, N_BALL, UW_OBJECT);
  for (int i = 0; i < IN_COUNT; i++) s.in[i] = resolve(player, IN_NAME[i], UW_BOOL);
  const Ofs lv = resolve(player, N_LEVEL, UW_OBJECT);
  if (lv.off) {
    __try {
      s.level = uw_ptr(player, lv.off);
    } __except (av_only(GetExceptionCode())) {
      ++s_faults;
    }
  }
  if (!live(s.level)) s.level = 0;
  if (s.level) s.time = resolve(s.level, N_TIME, UW_FLOAT);
  const int tableOk = rd_vec(ctrl, N_LOCATION, s.tableLoc) && rd_rot(ctrl, N_ROTATION, s.tableRot);
  s.nFlip[0] = read_flippers(N_LFLIP, s.flip[0]);
  s.nFlip[1] = read_flippers(N_RFLIP, s.flip[1]);
  for (int side = 0; side < 2; side++)
    for (int i = 0; i < s.nFlip[side] && !s.flipUp.off; i++)
      if (s.flip[side][i]) s.flipUp = resolve(s.flip[side][i], N_BUP, UW_BOOL);
  resolve_pe_fn(PE_CTRL, L"ControlEvent", L"ByteProperty", 1);
  resolve_pe_fn(PE_AWARD, L"Points", L"IntProperty", 4);
  int missing = 0;
  const Ofs* const all[] = { &s.score, &s.balls, &s.mult, &s.plunger, &s.gameOver, &s.gutter, &s.tilted, &s.lBtn,
                             &s.rBtn, &s.ballPtr, &s.time, &s.flipUp };
  for (int i = 0; i < (int)(sizeof all / sizeof all[0]); i++) missing += !all[i]->off;
  for (int i = 0; i < IN_COUNT; i++) missing += !s.in[i].off;
  SYSTEMTIME st;
  GetLocalTime(&st);
  header("H,session,%u,map,%ls,control,%ls,started,%04u-%02u-%02u %02u:%02u:%02u,fps,%.1f,unresolved,%d,"
         "gamespeed,%s,%d", s_sessions, uname_str(world_map_name()), uname_str(class_name_of(ctrl)), st.wYear,
         st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, pb_fps(s_dts, 120), missing,
         g_gameSpeedOn ? "on" : "off", g_cfg.gameSpeedPct);
  header("H,table,0,location,%.3f,%.3f,%.3f,rotation,%d,%d,%d,resolved,%d", s.tableLoc[0], s.tableLoc[1],
         s.tableLoc[2], s.tableRot[0], s.tableRot[1], s.tableRot[2], tableOk);
  for (int side = 0; side < 2; side++)
    for (int i = 0; i < s.nFlip[side]; i++)
      if (s.flip[side][i])
        actor_header(side ? "flipper_right" : "flipper_left", i, s.flip[side][i], s.flipLoc[side][i],
                     &s.flipYaw[side][i]);
  header("H,processevent,ControlEvent,%d,AwardPoints,%d", s_peFn[PE_CTRL] != 0, s_peFn[PE_AWARD] != 0);
  header("H,columns,%s", PB_FRAME_COLUMNS);
  header("H,columns,%s", PB_EVENT_COLUMNS);
  np_log("pinball: tracing %ls (%p) on %ls: %d left and %d right flipper(s), %d propert(ies) unresolved, ProcessEvent "
         "ControlEvent %s, AwardPoints %s", uname_str(class_name_of(ctrl)), ctrl, uname_str(world_map_name()),
         s.nFlip[0], s.nFlip[1], missing, s_peFn[PE_CTRL] ? "watched" : "unresolved",
         s_peFn[PE_AWARD] ? "watched" : "unresolved");
  s_active = 1;
  enable_all(1);
}

static void end(const char* why) {
  if (!s_active) return;
  s_active = 0;
  enable_all(0);
  const double secs = ms_since(s.t0) / 1000.0;
  ev("session_end", s.was.score, why);
  flush();
  const int gained = s.haveWas ? s.was.score - s.score0 : 0;
  const float gameSecs = s.haveWas ? s.was.time - s.time0 : 0.f;
  np_log("pinball: game speed %.2fx over the session (Game speed %s, %d%%); game-time frame delta mean %.2f ms, max "
         "%.2f ms over %u frame(s)", pb_speed_ratio(gameSecs, (float)secs), g_gameSpeedOn ? "on" : "off",
         g_cfg.gameSpeedPct, pb_dt_mean(s.gameDt), s.gameDt.max, s.gameDt.n);
  np_log("pinball: %s after %.1f s (game time %.1f s): score %d, gained %d (%.0f a minute), balls lost %u, saves %u, "
         "game over %s; autoplay %u row(s), hits/misses left %u/%u right %u/%u; %u trace row(s)"
         " (%u dropped at the cap), velocity from KGetLinearVelocity %u / position delta %u frame(s), %u flush(es), "
         "longest write %.1f ms; through ProcessEvent: exec %u, ControlEvent %u, AwardPoints %u; %u fault(s)%s%s",
         why, secs, s.haveWas ? s.was.time - s.time0 : 0.f, s.was.score, gained, pb_per_minute(gained, secs), s.lost,
         s.saves, s.gameOverSeen ? "yes" : "no", s.botRows, s.botHits[0], s.botMisses[0], s.botHits[1],
         s.botMisses[1], s.rows, s.dropped, s.velK, s.velD, s_flushes, s_longestWriteMs, s.peExec, s.peCtrl,
         s.peAward, s_faults, s_fileWhy ? "; no file: " : "", s_fileWhy ? s_fileWhy : "");
  s_faults = 0;
  s_flushes = 0;
  s_longestWriteMs = 0.0;
}

// --- Per frame -------------------------------------------------------------------------------------------------------
// One guarded pass over the control, its ball pointer, the flippers, the pawn's flags and the level's time.
static bool read_frame(Read* r) {
  memset(r, 0, sizeof *r);
  __try {
    if (!uobj_valid(s.ctrl)) return false;
    r->score = rd_i(s.ctrl, s.score);
    r->balls = rd_i(s.ctrl, s.balls);
    r->mult = rd_i(s.ctrl, s.mult);
    r->plunger = rd_b(s.ctrl, s.plunger);
    r->gameOver = rd_b(s.ctrl, s.gameOver);
    r->gutter = rd_b(s.ctrl, s.gutter);
    r->tilted = rd_b(s.ctrl, s.tilted);
    r->lBtn = rd_b(s.ctrl, s.lBtn);
    r->rBtn = rd_b(s.ctrl, s.rBtn);
    r->ball = s.ballPtr.off ? uw_ptr(s.ctrl, s.ballPtr.off) : 0;
    if (r->ball && !uobj_valid(r->ball)) r->ball = 0;
    for (int side = 0; side < 2; side++)
      for (int i = 0; i < s.nFlip[side]; i++) {
        const void* f = s.flip[side][i];
        if (f && s.flipUp.off && uobj_valid(f) && rd_b(f, s.flipUp)) (side ? r->rUp : r->lUp) |= 1u << i;
      }
    if (uobj_valid(s.player))
      for (int i = 0; i < IN_COUNT; i++) r->in[i] = rd_b(s.player, s.in[i]);
    if (s.level && s.time.off && uobj_valid(s.level)) memcpy(&r->time, (const char*)s.level + s.time.off, 4);
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
  return true;
}

static bool read_ball(const void* ball, float loc[3]) {
  if (class_of(ball) != s.ballCls) {
    s.ballCls = class_of(ball);
    s.ballLoc = resolve(ball, N_LOCATION, UW_STRUCT);
  }
  if (!s.ballLoc.off) return false;
  __try {
    memcpy(loc, (const char*)ball + s.ballLoc.off, 12);
    return true;
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return false;
  }
}

// One line per lost ball: its cause (pbtrace.h pb_drain_cause), last place and speed, and the last flipper press.
static void drain_cause(const Read& r) {
  float ly = 1e9f, ry = -1e9f, lx = -1e9f, rx = -1e9f;   // the lower flippers' pivots
  for (int i = 0; i < s.nFlip[0] && i < FLIP_MAX; i++)
    if (s.flip[0][i] && s.flipLoc[0][i][0] > lx) { lx = s.flipLoc[0][i][0]; ly = s.flipLoc[0][i][1]; }
  for (int i = 0; i < s.nFlip[1] && i < FLIP_MAX; i++)
    if (s.flip[1][i] && s.flipLoc[1][i][0] > rx) { rx = s.flipLoc[1][i][0]; ry = s.flipLoc[1][i][1]; }
  const double now = ms_since(s.t0);
  const unsigned since = s.pressSeen ? (unsigned)(now - s.pressMs) : 0xFFFFFFFFu;
  const int cause = pb_drain_cause(s.prevPos[1], ly, ry, s.was.tilted || r.tilted, (s.was.lUp | s.was.rUp) != 0, since);
  const float speed = sqrtf(s.prevVel[0] * s.prevVel[0] + s.prevVel[1] * s.prevVel[1]);
  char d[160];
  _snprintf_s(d, sizeof d, _TRUNCATE, "%s pos %.1f %.1f vel %.0f %.0f last press %.0f ms before", pb_cause_name(cause),
              s.prevPos[0], s.prevPos[1], s.prevVel[0], s.prevVel[1], s.pressSeen ? now - s.pressMs : -1.0);
  ev("drain_cause", cause, d);
  np_log("pinball: ball lost (%d left): %s, last at (%.1f, %.1f) %.0f u/s; last flipper press %s%.0f ms before",
         r.balls, pb_cause_name(cause), s.prevPos[0], s.prevPos[1], speed, s.pressSeen ? "" : "none, ",
         s.pressSeen ? now - s.pressMs : 0.0);
}

static void frame_events(const Read& r) {
  char d[96];
  for (int i = 0; i < IN_COUNT; i++) {
    const int n = pb_flag_event(IN_BASE[i], s.was.in[i], r.in[i]);
    if (n < 0) continue;
    _snprintf_s(d, sizeof d, _TRUNCATE, "%s %s", IN_LABEL[i], r.in[i] ? "pressed" : "released");
    ev("input", n, d);
  }
  if (r.lBtn != s.was.lBtn) ev("button", r.lBtn, "bLeftButton");
  if (r.rBtn != s.was.rBtn) ev("button", r.rBtn, "bRightButton");
  if (r.score != s.was.score) {
    _snprintf_s(d, sizeof d, _TRUNCATE, "multiplier %d", r.mult);
    ev("score", r.score - s.was.score, d);
  }
  const int drain = pb_drain(s.was.gutter, r.gutter, s.was.balls, r.balls);
  if (drain == PB_DRAIN_LOST) {
    ++s.lost;
    ev("ball_lost", r.balls, "BallsLeft");
    drain_cause(r);
  } else if (drain == PB_DRAIN_SAVED) {
    ++s.saves;
    ev("ball_saved", r.balls, "GutterLight lit");
  }
  if (r.balls > s.was.balls) {
    ev("reset", r.balls, "BallsLeft rose (ResetTable)");
    s.score0 = r.score;   // "gained" counts this game, not the last one's score
  }
  if (r.gameOver && !s.was.gameOver) {
    s.gameOverSeen = 1;
    ev("game_over", r.score, "bGameOver");
  }
  if (r.tilted != s.was.tilted) ev("tilt", r.tilted, "bTilted");
  if (r.ball != s.was.ball) ev("ball", r.ball != 0, "PrimaryBall changed");
}

static void step(void) {
  Read r;
  if (!read_frame(&r)) return;
  const double ms = ms_since(s.t0);
  if (!s.haveWas) {
    s.was = r;   // no edges on the first frame
    s.time0 = r.time;
    s.score0 = r.score;
  }
  // The ball's header when it first shows (PrimaryBall is spawned by InitItems).
  if (r.ball && r.ball != s.ball) {
    s.ball = r.ball;
    actor_header("ball", 0, r.ball);
  }
  PbFrame f;
  memset(&f, 0, sizeof f);
  f.frame = s.frame;
  f.gameTime = r.time;
  f.ms = ms;
  f.dtMs = s.haveWas ? (double)(r.time - s.was.time) * 1000.0 : 0.0;
  s.frameGameMs = (float)f.dtMs;
  pb_dt_add(&s.gameDt, s.frameGameMs);
  memcpy(s.prevPos, s.pos, sizeof s.prevPos);
  memcpy(s.prevVel, s.vel, sizeof s.prevVel);
  float loc[3], w[3];
  const int havePos = r.ball && read_ball(r.ball, loc);
  if (havePos) {
    for (int i = 0; i < 3; i++) w[i] = loc[i] - s.tableLoc[i];
    pb_local(w, s.tableRot[0], s.tableRot[1], s.tableRot[2], f.pos);
    float v[3];
    char src;
    if (k_velocity(r.ball, v)) {
      pb_local(v, s.tableRot[0], s.tableRot[1], s.tableRot[2], f.vel);
      src = 'K';
      ++s.velK;
    } else {
      // The delta over game time; a frame with no tick keeps the last velocity.
      const float dt = r.time - s.was.time;
      for (int i = 0; i < 3; i++) f.vel[i] = s.havePos && dt > 0.f ? (f.pos[i] - s.pos[i]) / dt : s.vel[i];
      src = 'D';
      ++s.velD;
    }
    if (src != s.vsrc) ev("vsrc", src, src == 'K' ? "KGetLinearVelocity" : "position delta");
    f.vsrc = s.vsrc = src;
    memcpy(s.pos, f.pos, sizeof s.pos);
    memcpy(s.vel, f.vel, sizeof s.vel);
  }
  s.havePos = havePos;
  f.lUp = r.lUp;
  f.rUp = r.rUp;
  f.lBtn = r.lBtn;
  f.rBtn = r.rBtn;
  f.fire = r.in[IN_FIRE];
  f.melee = r.in[IN_MELEE];
  f.plunger = r.plunger;
  f.score = r.score;
  f.balls = r.balls;
  f.mult = r.mult;
  if (s.haveWas) frame_events(r);
  const int st = state_name(s.ctrl);
  if (st && st != s.stateName) {
    char d[96];
    _snprintf_s(d, sizeof d, _TRUNCATE, "%ls", uname_str(st));
    ev("state", st, d);
    s.stateName = st;
  }
  char row[256];
  emit(row, pb_fmt_frame(row, sizeof row, f));
  s.was = r;
  s.haveWas = 1;
  s.readAt = s_frameNo;
  ++s.frame;
}

// The control the player uses when it is a pinball table (dnControl_Pinball or dnControl_Pinball2 on its class
// chain), else null. Cached per class, cleared per level.
static const void* pinball_in_use(void* player) {
  static const void* s_cls[8];
  static int s_is[8];
  static int s_names[2];
  static unsigned s_lv;
  if (s_lv != world_level_serial()) {   // a class address may be reused by the next level's classes
    s_lv = world_level_serial();
    memset(s_cls, 0, sizeof s_cls);
    memset(s_names, 0, sizeof s_names);   // class names move across level loads (lvnames.h)
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
  if (!s_names[0]) s_names[0] = uname_find(L"dnControl_Pinball");
  if (!s_names[1]) s_names[1] = uname_find(L"dnControl_Pinball2");
  int is = 0;
  __try {
    const void* c = cls;
    for (int d = 0; c && !is && d < 32; d++, c = uw_ptr(c, US_SUPER)) {
      const int n = uobj_name(c);
      is = n && (n == s_names[0] || n == s_names[1]);
    }
  } __except (av_only(GetExceptionCode())) {
    ++s_faults;
    return 0;
  }
  s_cls[slot] = cls;
  s_is[slot] = is;
  return is ? ctrl : 0;
}

void pbtrace_frame(int liveNow) {
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
    end(s_endWhy ? s_endWhy : "level changed");   // the last level's pointers are stale: nothing read
    s_endWhy = 0;
    s_level = level;
  }
  void* player = world_player();
  if (liveNow && player) {
    const void* ctrl = pinball_in_use(player);
    if (s_active && ctrl != s.ctrl) {
      // The frame the player leaves (DetachPawn on game over): the control is still read once, for the drain; the
      // session stays open for the autoplay's release rows until pbtrace_frame_end. A new table starts next frame.
      step();
      s_endWhy = s.gameOverSeen ? "game over" : "left the table";
    } else {
      if (!s_active && ctrl) begin(player, ctrl);
      if (s_active) step();
    }
  }
  if (pb_flush_due(GetTickCount(), s_lastFlushMs, s_used, (int)sizeof s_buf, 0)) flush();
  uname_dlog_flush();
}

void pbtrace_frame_end(void) {
  if (!s_endWhy) return;
  end(s_endWhy);
  s_endWhy = 0;
}

int pbtrace_view(PbView* v) {
  memset(v, 0, sizeof *v);
  if (!s_active) return 0;
  static int s_idle;
  if (!s_idle) s_idle = uname_find(L"idle");
  v->active = 1;
  v->attached = !s_endWhy;
  v->fresh = s.haveWas && s.readAt == s_frameNo;
  v->player = s.player;
  v->ctrl = s.ctrl;
  v->haveBall = s.havePos;
  memcpy(v->pos, s.pos, sizeof v->pos);
  memcpy(v->vel, s.vel, sizeof v->vel);
  v->allowPlunger = s.was.plunger;
  v->gameOver = s.was.gameOver;
  v->tilted = s.was.tilted;
  v->idle = s_idle && s.stateName == s_idle;
  v->fire = s.was.in[IN_FIRE];
  v->melee = s.was.in[IN_MELEE];
  v->backward = s.was.in[IN_BACKWARD];
  v->score = s.was.score;
  v->gameSecs = s.was.time;
  v->frameGameMs = s.frameGameMs;
  v->balls = s.was.balls;
  for (int side = 0; side < 2; side++) {
    v->nFlip[side] = 0;
    for (int i = 0; i < s.nFlip[side] && i < FLIP_MAX; i++) {
      if (!s.flip[side][i]) continue;   // not a live flipper at session start
      const int k = v->nFlip[side]++;
      v->flip[side][k][0] = s.flipLoc[side][i][0];
      v->flip[side][k][1] = s.flipLoc[side][i][1];
      v->flipYaw[side][k] = s.flipYaw[side][i];
    }
  }
  return 1;
}

void pbtrace_bot_row(int side, const char* detail, int outcome) {
  if (!s_active) return;
  ++s.botRows;
  if (side == 0 || side == 1) {
    if (outcome == 1) ++s.botHits[side];
    else if (outcome == 2) ++s.botMisses[side];
  }
  ev("bot", side, detail);
}

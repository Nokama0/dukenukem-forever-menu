// Steam achievement backup and reset (achreset.h, docs/re/steam-stats.md). Every Steam call is on the game thread
// through ISteamUserStats009's vtable; one worker thread writes the backup file. The reset runs only from the
// confirmed Settings button and only once that file is on disk.
#include <windows.h>
#include <stdlib.h>
#include "achreset.h"
#include "hook.h"
#include "log.h"
#include "paths.h"
#include "ui/wallclock.h"

// __thiscall methods called as __fastcall: `this` in ECX, EDX unused, the rest on the stack, callee pops.
typedef void* (__cdecl* SteamUserStatsFn)(void);
typedef bool (__fastcall* NoArgFn)(void* self, void* edx);
typedef bool (__fastcall* AchTimeFn)(void* self, void* edx, const char* name, bool* achieved, uint32_t* unlock);
typedef const char* (__fastcall* AttrFn)(void* self, void* edx, const char* name, const char* key);
typedef bool (__fastcall* ResetFn)(void* self, void* edx, bool achievementsToo);
enum { SLOT_REQUEST = 0, SLOT_ACH_TIME = 9, SLOT_STORE = 10, SLOT_ATTR = 12, SLOT_RESET = 19, SLOT_LAST = 20 };
static const char kVersion[] = "STEAMUSERSTATS_INTERFACE_VERSION009";
static const unsigned PROBE_RETRY_MS = 2000;

static void* s_us;                 // the interface the probe last accepted
static void** s_vt;
static const char* s_why = "Not checked yet";
static unsigned s_probeAt;
static int s_probed;
static HMODULE s_versionChecked;   // the steam_api.dll whose image held kVersion
static ArConfirm s_confirm;
static int s_rowShown;             // the menu drew the reset row on screen since the last achreset_frame
static unsigned s_confirmAt;       // when the queued reset was confirmed (AR_RESET_TTL_MS)
static int s_resetDropped;         // the queued reset outlived AR_RESET_TTL_MS before its backup landed
static char s_note[192];
static int s_haveNote;

// The worker owns the job from CreateThread until it stores `state`; the game thread frees it after reading that.
struct ArJob {
  wchar_t dir[MAX_PATH];
  ArTime gen;
  char* text;
  int len;
  char file[64];
  DWORD err;
  unsigned ms;
  volatile LONG state;   // 0 writing, 1 written, 2 failed
};
static ArJob* s_job;
static int s_resetPending;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static void set_note(const char* fmt, const char* a) {
  _snprintf_s(s_note, sizeof s_note, _TRUNCATE, fmt, a);
  s_haveNote = 1;
}

// steam_api.dll asks steamclient for the version its own image names; another build would mean another vtable.
// This string is the real version guard; the one-module check below only catches a hooked or foreign table.
static bool image_has_version(HMODULE m) {
  const unsigned char* base = (const unsigned char*)m;
  if (!hook_readable(base, 0x400)) return false;
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x300) return false;
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
  const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
  const unsigned n = nt->FileHeader.NumberOfSections;
  if (n > 32 || !hook_readable(sec, n * (unsigned)sizeof *sec)) return false;
  const size_t vl = sizeof kVersion;   // with its terminator: the whole string, not a prefix of a longer one
  for (unsigned i = 0; i < n; ++i) {
    if (!(sec[i].Characteristics & IMAGE_SCN_MEM_READ) || (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
    const unsigned char* p = base + sec[i].VirtualAddress;
    const unsigned size = sec[i].Misc.VirtualSize;
    if (size < vl || !hook_readable(p, size)) continue;
    for (unsigned j = 0; j + vl <= size; ++j)
      if (p[j] == 'S' && memcmp(p + j, kVersion, vl) == 0) return true;
  }
  return false;
}

static const char* probe_now(void) {
  s_us = NULL;
  s_vt = NULL;
  HMODULE sa = GetModuleHandleW(L"steam_api.dll");
  if (!sa) return "steam_api.dll is not loaded";
  if (s_versionChecked != sa) {
    if (!image_has_version(sa)) return "This steam_api.dll does not use ISteamUserStats version 009";
    s_versionChecked = sa;
  }
  const SteamUserStatsFn get = (SteamUserStatsFn)GetProcAddress(sa, "SteamUserStats");
  if (!get) return "steam_api.dll has no SteamUserStats";
  void* us = get();
  if (!us) return "Steam has no achievement interface for this game";
  void** vt = hook_readable(us, sizeof(void*)) ? *(void***)us : NULL;
  if (!vt || !hook_readable(vt, (SLOT_LAST + 1) * sizeof(void*))) return "The achievement interface is unreadable";
  // Every method from RequestCurrentStats to FindOrCreateLeaderboard lives in one module (steamclient.dll).
  HMODULE first = NULL;
  for (int i = 0; i <= SLOT_LAST; ++i) {
    HMODULE m = NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)vt[i], &m) || (first && m != first))
      return "The achievement interface is not the expected one";
    first = m;
  }
  wchar_t mod[MAX_PATH] = L"?";
  GetModuleFileNameW(first, mod, MAX_PATH);
  np_log("achreset: ISteamUserStats009 at %p, table %p in %ls", us, (void*)vt, mod);
  s_us = us;
  s_vt = vt;
  return NULL;
}

const char* achreset_refused(void) {
  const SteamUserStatsFn get = s_us ? (SteamUserStatsFn)GetProcAddress(GetModuleHandleW(L"steam_api.dll"),
                                                                       "SteamUserStats") : NULL;
  if (s_us && get && get() == s_us) return NULL;   // accepted and unchanged: nothing to re-check
  const unsigned now = wallclock_ms();
  if (s_probed && now - s_probeAt < PROBE_RETRY_MS && !s_us) return s_why;
  const char* was = s_probed ? s_why : NULL;
  s_why = probe_now();
  s_probeAt = now;
  if (!s_probed || s_why != was) np_log("achreset: %s", s_why ? s_why : "available");
  s_probed = 1;
  return s_why;
}

// Local offset from UTC in minutes at a Unix time (that date's DST rule).
static int offset_at(uint32_t unix) {
  ULARGE_INTEGER u;
  u.QuadPart = ((unsigned long long)unix + 11644473600ull) * 10000000ull;
  FILETIME ft = { u.LowPart, u.HighPart }, lft;
  SYSTEMTIME st, lt;
  if (!FileTimeToSystemTime(&ft, &st) || !SystemTimeToTzSpecificLocalTime(NULL, &st, &lt) ||
      !SystemTimeToFileTime(&lt, &lft))
    return 0;
  ULARGE_INTEGER l;
  l.LowPart = lft.dwLowDateTime;
  l.HighPart = lft.dwHighDateTime;
  return (int)(((long long)l.QuadPart - (long long)u.QuadPart) / 600000000ll);
}

int achreset_offset_at(uint32_t unix) { return offset_at(unix); }

static int state_body(const char* api, bool* ach, uint32_t* t) {
  return ((AchTimeFn)s_vt[SLOT_ACH_TIME])(s_us, NULL, api, ach, t) ? 1 : 0;
}

int achreset_state(int id, int* achieved, uint32_t* unlock) {
  *achieved = 0;
  *unlock = 0;
  const char* api = ar_name(id);
  if (!api || achreset_refused()) return -1;
  bool ach = false;
  uint32_t t = 0;
  int r;
  __try {
    r = state_body(api, &ach, &t);
  } __except (av_only(GetExceptionCode())) {
    return -1;
  }
  if (r > 0) {
    *achieved = ach ? 1 : 0;
    *unlock = ach ? t : 0;
  }
  return r;
}

static const char* attr_body(const char* api, const char* key) { return ((AttrFn)s_vt[SLOT_ATTR])(s_us, NULL, api, key); }

bool achreset_attr(int id, const char* key, char* out, int cap) {
  if (cap > 0) out[0] = 0;
  const char* api = ar_name(id);
  if (!api || cap <= 0 || achreset_refused()) return false;
  __try {
    const char* v = attr_body(api, key);
    if (!v || !*v) return false;
    ar_clean(v, out, cap);
  } __except (av_only(GetExceptionCode())) {
    out[0] = 0;
    return false;
  }
  return out[0] != 0;
}

// Steam's answer for every name in the game's list. Returns the number answered, or -1 on a fault.
static int collect_body(ArEntry* e) {
  const AchTimeFn achTime = (AchTimeFn)s_vt[SLOT_ACH_TIME];
  const AttrFn attr = (AttrFn)s_vt[SLOT_ATTR];
  int answered = 0;
  for (int i = 0; i < AR_COUNT; ++i) {
    bool ach = false;
    uint32_t t = 0;
    e[i].id = i;
    e[i].api = ar_name(i);
    e[i].answered = achTime(s_us, NULL, e[i].api, &ach, &t) ? 1 : 0;
    e[i].achieved = e[i].answered && ach ? 1 : 0;
    e[i].unlock = e[i].achieved ? t : 0;
    ar_clean(e[i].answered ? attr(s_us, NULL, e[i].api, "name") : NULL, e[i].name, AR_NAME_CAP);
    answered += e[i].answered;
  }
  return answered;
}

static int collect(ArEntry* e) {
  __try {
    return collect_body(e);
  } __except (av_only(GetExceptionCode())) {
    return -1;
  }
}

static DWORD WINAPI write_worker(LPVOID p) {
  ArJob* j = (ArJob*)p;
  const DWORD t0 = GetTickCount();
  HANDLE h = INVALID_HANDLE_VALUE;
  j->err = 0;
  for (int n = 1; n <= 9 && h == INVALID_HANDLE_VALUE; ++n) {
    ar_file_name(j->gen, n, j->file, sizeof j->file);
    wchar_t path[MAX_PATH];
    _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%ls%hs", j->dir, j->file);
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
      j->err = GetLastError();
      if (j->err != ERROR_FILE_EXISTS) break;
    }
  }
  LONG result = 2;
  if (h != INVALID_HANDLE_VALUE) {
    DWORD wrote = 0;
    // One WriteFile for the BOM and the text, and a flush: the reset waits for the file to be on disk.
    BOOL ok = WriteFile(h, j->text, (DWORD)j->len, &wrote, NULL) && wrote == (DWORD)j->len && FlushFileBuffers(h);
    j->err = ok ? 0 : GetLastError();
    if (!CloseHandle(h) && ok) { ok = FALSE; j->err = GetLastError(); }
    if (ok) result = 1;
  }
  j->ms = GetTickCount() - t0;
  InterlockedExchange(&j->state, result);
  return 0;
}

static void start(int resetAfter) {
  const char* why = achreset_refused();
  if (why) {
    np_log("achreset: %s refused: %s", resetAfter ? "reset" : "backup", why);
    set_note("%s", why);
    return;
  }
  ArEntry* e = (ArEntry*)calloc(AR_COUNT, sizeof(ArEntry));
  ArJob* j = (ArJob*)calloc(1, sizeof(ArJob));
  const int cap = 64 * 1024;
  char* text = (char*)malloc((size_t)cap);
  if (!e || !j || !text) {
    free(e); free(j); free(text);
    np_log("achreset: out of memory");
    set_note("%s", "Out of memory: nothing written");
    return;
  }
  LARGE_INTEGER q0, q1, qf;
  QueryPerformanceCounter(&q0);
  const int answered = collect(e);
  QueryPerformanceCounter(&q1);
  QueryPerformanceFrequency(&qf);
  int unlocked = 0;
  for (int i = 0; i < AR_COUNT; ++i) {
    unlocked += e[i].achieved;
    if (e[i].unlock) e[i].offMin = offset_at(e[i].unlock);
  }
  np_log("achreset: %s: Steam answered %d of %d names, %d unlocked, in %lld us", resetAfter ? "reset" : "backup",
         answered, AR_COUNT, unlocked, (long long)((q1.QuadPart - q0.QuadPart) * 1000000 / qf.QuadPart));
  const char* fail = answered < 0 ? "Steam faulted while reading the achievements: nothing written"
                   : answered == 0 ? "Steam has not sent the achievements yet: try again in a moment" : NULL;
  FILETIME nowFt;
  GetSystemTimeAsFileTime(&nowFt);
  ULARGE_INTEGER nu;
  nu.LowPart = nowFt.dwLowDateTime;
  nu.HighPart = nowFt.dwHighDateTime;
  const uint32_t now = (uint32_t)(nu.QuadPart / 10000000ull - 11644473600ull);
  j->gen = ar_time_from_unix(now, offset_at(now));
  memcpy(text, "\xEF\xBB\xBF", 3);   // a UTF-8 BOM: display names may hold non-ASCII
  const int len = fail ? -1 : ar_backup_text(e, AR_COUNT, j->gen, text + 3, cap - 3);
  free(e);
  if (!fail && len < 0) fail = "The backup text did not fit its buffer: nothing written";
  if (!fail && !np_our_dir(j->dir, MAX_PATH)) fail = "No NukePanel folder: nothing written";
  if (fail) {
    np_log("achreset: %s", fail);
    set_note("%s", fail);
    free(text); free(j);
    return;
  }
  CreateDirectoryW(j->dir, NULL);   // System\NukePanel\, the deploy exception; it usually exists (the log)
  j->text = text;
  j->len = len + 3;
  HANDLE th = CreateThread(NULL, 0, write_worker, j, 0, NULL);
  if (!th) {
    np_log("achreset: CreateThread failed (%lu): nothing written", GetLastError());
    set_note("%s", "Could not start the writer: nothing written");
    free(text); free(j);
    return;
  }
  CloseHandle(th);
  s_job = j;
  s_resetPending = resetAfter;
  np_log("achreset: writing %d bytes%s", j->len, resetAfter ? "; the reset follows a written file" : "");
}

void achreset_backup_click(void) {
  if (s_job) { np_log("achreset: backup click ignored: a backup is being written"); return; }
  np_log("achreset: backup requested");
  start(0);
}

void achreset_reset_click(void) {
  if (s_job) { np_log("achreset: reset click ignored: a backup is being written"); return; }
  const unsigned now = wallclock_ms();
  const int act = ar_click(&s_confirm, now);
  if (act == AR_ACT_IGNORED) {
    np_log("achreset: reset click ignored: under %u ms after the arm", AR_CONFIRM_MIN_MS);
    return;
  }
  if (act == AR_ACT_ARM) {
    np_log("achreset: reset armed for %u ms", AR_CONFIRM_MS);
    return;
  }
  np_log("achreset: reset confirmed; backing up first");
  s_confirmAt = now;
  s_resetDropped = 0;
  start(1);
}

bool achreset_armed(void) { return ar_armed(s_confirm, wallclock_ms()); }
void achreset_row_shown(void) { s_rowShown = 1; }
bool achreset_busy(void) { return s_job != NULL; }
const char* achreset_note(void) { return s_haveNote ? s_note : NULL; }

static int reset_body(bool* r, bool* st, bool* rq) {
  *r = ((ResetFn)s_vt[SLOT_RESET])(s_us, NULL, true);
  *st = ((NoArgFn)s_vt[SLOT_STORE])(s_us, NULL);
  // The game's own UserStatsReceived handler then reloads its table (docs/re/steam-stats.md section 5).
  *rq = ((NoArgFn)s_vt[SLOT_REQUEST])(s_us, NULL);
  return 1;
}

static void do_reset(const char* file) {
  const char* why = achreset_refused();
  if (why) {
    np_log("achreset: reset not done: %s (backup %s kept)", why, file);
    set_note("Reset not done: %s", why);
    return;
  }
  bool r = false, st = false, rq = false;
  int ok = 0;
  __try {
    ok = reset_body(&r, &st, &rq);
  } __except (av_only(GetExceptionCode())) {
    ok = 0;
  }
  np_log("achreset: ResetAllStats(true)=%d StoreStats=%d RequestCurrentStats=%d%s", r ? 1 : 0, st ? 1 : 0, rq ? 1 : 0,
         ok ? "" : " (faulted)");
  if (ok && r) set_note("Achievements reset. Backup: %s", file);
  else set_note("Steam refused the reset. Backup: %s", file);
}

void achreset_frame(void) {
  const unsigned now = wallclock_ms();
  if (ar_frame_check(&s_confirm, s_rowShown, now)) np_log("achreset: reset disarmed: its row left the screen");
  s_rowShown = 0;
  ArJob* j = s_job;
  if (!j) return;
  if (s_resetPending && ar_reset_expired(s_confirmAt, now)) {
    s_resetPending = 0;
    s_resetDropped = 1;
    np_log("achreset: reset dropped: no backup written within %u ms of the confirm", AR_RESET_TTL_MS);
    set_note("%s", "Reset dropped: the backup took over 10 s");
  }
  const LONG state = InterlockedCompareExchange(&j->state, 0, 0);
  if (!state) return;
  s_job = NULL;
  const int written = state == 1;
  np_log("achreset: backup %s %ls%hs (error %lu, %u ms)", written ? "written to" : "FAILED for", j->dir, j->file,
         j->err, j->ms);
  if (ar_reset_now(s_resetPending, written, s_confirmAt, now)) do_reset(j->file);
  else if (s_resetPending) {
    np_log("achreset: reset refused: the backup was not written");
    set_note("%s", "Backup failed: achievements not reset");
  } else if (s_resetDropped) {
    if (written) set_note("Reset dropped: the backup took over 10 s. Backup: %s", j->file);
    else set_note("%s", "Reset dropped and the backup failed: see nukepanel.log");
    s_resetDropped = 0;
  } else if (written) set_note("Last backup: %s", j->file);
  else set_note("%s", "Backup failed: see nukepanel.log");
  s_resetPending = 0;
  free(j->text);
  free(j);
}

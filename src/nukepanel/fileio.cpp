// File-I/O timing (the hitch line's io= share) and the slow-open log. Each hooked slot calls whatever it held
// before (a shim or another hook may own it), so every module keeps its own original per function.
#include <windows.h>
#include "fileio.h"
#include "slowopen.h"
#include "iat.h"
#include "log.h"

typedef HANDLE (WINAPI* CreateFileA_t)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE (WINAPI* CreateFileW_t)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef BOOL (WINAPI* ReadFile_t)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL (WINAPI* WriteFile_t)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL (WINAPI* CloseHandle_t)(HANDLE);

enum { FM_ENGINE, FM_DNCOMMON, FM_MSVCR, FM_COUNT };
static const wchar_t* const kModW[FM_COUNT] = { L"Engine.dll", L"dnCommon.dll", L"MSVCR90.dll" };
static const char* const kModA[FM_COUNT] = { "Engine.dll", "dnCommon.dll", "MSVCR90.dll" };
static const char* const kWho[FM_COUNT] = { "fileio Engine.dll", "fileio dnCommon.dll", "fileio MSVCR90.dll" };

static CreateFileA_t s_CreateFileA[FM_COUNT];
static CreateFileW_t s_CreateFileW[FM_COUNT];
static ReadFile_t s_ReadFile[FM_COUNT];
static WriteFile_t s_WriteFile[FM_COUNT];
static CloseHandle_t s_CloseHandle[FM_COUNT];

static double s_ticksPerMs;          // set before the first slot is patched
static volatile LONG s_frameTid;     // written once by the first fileio_take
static volatile LONG s_opens, s_slow;

// Frame thread only: the hooks touch these only when the caller is the frame thread, and so does the take.
static unsigned s_fCalls;
static long long s_fTicks, s_fMax;
// Frame thread, read at detach from the loader's thread: a torn total there only misprints one log line.
static unsigned s_totCalls;
static long long s_totTicks, s_totMax;

// Opens on any thread; taken only for an open of 50 ms or more.
static SRWLOCK s_slowLock = SRWLOCK_INIT;
static SlowOpen s_so;

static long long qpc(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static bool on_frame(void) { return GetCurrentThreadId() == (DWORD)s_frameTid; }
static void frame_add(long long dt) {
  s_fCalls++;
  s_fTicks += dt;
  if (dt > s_fMax) s_fMax = dt;
}
static unsigned ticks_us(long long t) { return s_ticksPerMs > 0.0 ? (unsigned)(t * 1000.0 / s_ticksPerMs) : 0u; }

static void slow_open(int k, const char* nameA, const wchar_t* nameW, bool ok, DWORD err, double ms) {
  wchar_t wbuf[520];
  char abuf[1040];
  const wchar_t* key = nameW;
  const char* shown = nameA;
  if (nameA && !MultiByteToWideChar(CP_ACP, 0, nameA, -1, wbuf, 520)) wbuf[0] = 0;
  if (nameA) key = wbuf;
  if (nameW && !WideCharToMultiByte(CP_UTF8, 0, nameW, -1, abuf, (int)sizeof abuf, NULL, NULL)) abuf[0] = 0;
  if (nameW) shown = abuf;
  AcquireSRWLockExclusive(&s_slowLock);
  const int logIt = slowopen_note(&s_so, key, ms, GetTickCount());
  const unsigned total = s_so.total;
  ReleaseSRWLockExclusive(&s_slowLock);
  InterlockedExchange(&s_slow, (LONG)total);
  if (logIt)
    np_log("slow-open: %.0f ms tid=%lu via %s %s err=%lu slow=%u: %s", ms, GetCurrentThreadId(), kModA[k],
           ok ? "ok" : "failed", ok ? 0ul : err, total, shown && *shown ? shown : "?");
}

static void opened(int k, const char* nameA, const wchar_t* nameW, HANDLE h, DWORD err, long long dt) {
  InterlockedIncrement(&s_opens);
  if (on_frame()) frame_add(dt);
  const double ms = s_ticksPerMs > 0.0 ? (double)dt / s_ticksPerMs : 0.0;
  if (ms >= SLOWOPEN_MS) slow_open(k, nameA, nameW, h != INVALID_HANDLE_VALUE, err, ms);
}

// The last error is saved after the real call and restored last: the CRT reads it after a failed call, and
// ERROR_ALREADY_EXISTS after a successful open.
template <int K>
static HANDLE WINAPI hk_CreateFileA(LPCSTR n, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD d, DWORD f, HANDLE t) {
  const long long t0 = qpc();
  const HANDLE h = s_CreateFileA[K](n, a, s, sa, d, f, t);
  const DWORD err = GetLastError();
  opened(K, n, 0, h, err, qpc() - t0);
  SetLastError(err);
  return h;
}

template <int K>
static HANDLE WINAPI hk_CreateFileW(LPCWSTR n, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD d, DWORD f, HANDLE t) {
  const long long t0 = qpc();
  const HANDLE h = s_CreateFileW[K](n, a, s, sa, d, f, t);
  const DWORD err = GetLastError();
  opened(K, 0, n, h, err, qpc() - t0);
  SetLastError(err);
  return h;
}

// Reads, writes and closes are timed on the frame thread only; every other thread goes straight through.
template <int K>
static BOOL WINAPI hk_ReadFile(HANDLE h, LPVOID b, DWORD n, LPDWORD r, LPOVERLAPPED o) {
  if (!on_frame()) return s_ReadFile[K](h, b, n, r, o);
  const long long t0 = qpc();
  const BOOL ok = s_ReadFile[K](h, b, n, r, o);
  const DWORD err = GetLastError();
  frame_add(qpc() - t0);
  SetLastError(err);
  return ok;
}

template <int K>
static BOOL WINAPI hk_WriteFile(HANDLE h, LPCVOID b, DWORD n, LPDWORD w, LPOVERLAPPED o) {
  if (!on_frame()) return s_WriteFile[K](h, b, n, w, o);
  const long long t0 = qpc();
  const BOOL ok = s_WriteFile[K](h, b, n, w, o);
  const DWORD err = GetLastError();
  frame_add(qpc() - t0);
  SetLastError(err);
  return ok;
}

template <int K>
static BOOL WINAPI hk_CloseHandle(HANDLE h) {
  if (!on_frame()) return s_CloseHandle[K](h);
  const long long t0 = qpc();
  const BOOL ok = s_CloseHandle[K](h);
  const DWORD err = GetLastError();
  frame_add(qpc() - t0);
  SetLastError(err);
  return ok;
}

struct FileSlot { int mod; const char* fn; void* hook; void** orig; };
#define FILE_SLOT(K, FN) { K, #FN, (void*)&hk_##FN<K>, (void**)&s_##FN[K] }
// The spec's slot list: the functions each module imports for its file calls.
static const FileSlot kSlots[] = {
  FILE_SLOT(FM_ENGINE, CreateFileA), FILE_SLOT(FM_ENGINE, CreateFileW), FILE_SLOT(FM_ENGINE, WriteFile),
  FILE_SLOT(FM_ENGINE, CloseHandle),
  FILE_SLOT(FM_DNCOMMON, ReadFile), FILE_SLOT(FM_DNCOMMON, WriteFile), FILE_SLOT(FM_DNCOMMON, CloseHandle),
  FILE_SLOT(FM_MSVCR, CreateFileA), FILE_SLOT(FM_MSVCR, CreateFileW), FILE_SLOT(FM_MSVCR, ReadFile),
  FILE_SLOT(FM_MSVCR, WriteFile), FILE_SLOT(FM_MSVCR, CloseHandle),
};
#undef FILE_SLOT

void fileio_install(void) {
  LARGE_INTEGER f;
  QueryPerformanceFrequency(&f);
  s_ticksPerMs = (double)f.QuadPart / 1000.0;
  s_so = slowopen_init();
  HMODULE mods[FM_COUNT];
  for (int k = 0; k < FM_COUNT; ++k) {
    mods[k] = GetModuleHandleW(kModW[k]);
    if (!mods[k]) np_log("%s: not loaded; its slots are not timed", kWho[k]);
  }
  const int want = (int)(sizeof kSlots / sizeof kSlots[0]);
  int n = 0;
  for (int i = 0; i < want; ++i) {
    const FileSlot& s = kSlots[i];
    if (mods[s.mod]) n += iat_patch(mods[s.mod], "KERNEL32.dll", s.fn, s.hook, s.orig, kWho[s.mod]);
  }
  np_log("fileio: %d/%d import slots timed; slow open >= %d ms, each path logged at most once per %d s", n, want,
         (int)SLOWOPEN_MS, (int)SLOWOPEN_REPEAT_MS / 1000);
}

void fileio_take(unsigned* calls, unsigned* total_us, unsigned* max_us) {
  if (!s_frameTid) InterlockedExchange(&s_frameTid, (LONG)GetCurrentThreadId());
  *calls = s_fCalls;
  *total_us = ticks_us(s_fTicks);
  *max_us = ticks_us(s_fMax);
  s_totCalls += s_fCalls;
  s_totTicks += s_fTicks;
  if (s_fMax > s_totMax) s_totMax = s_fMax;
  s_fCalls = 0;
  s_fTicks = 0;
  s_fMax = 0;
}

void fileio_totals(FileIoTotals* out) {
  out->calls = s_totCalls;
  out->total_ms = s_ticksPerMs > 0.0 ? (double)s_totTicks / s_ticksPerMs : 0.0;
  out->max_ms = s_ticksPerMs > 0.0 ? (double)s_totMax / s_ticksPerMs : 0.0;
  out->opens = (unsigned)s_opens;
  out->slow = (unsigned)s_slow;
}

void fileio_detach(void) {
  if (s_ticksPerMs <= 0.0) return;   // never installed
  FileIoTotals t;
  fileio_totals(&t);
  np_log("fileio: totals frame-thread calls=%u %.1f ms (max %.1f) opens=%u slow-opens=%u", t.calls, t.total_ms,
         t.max_ms, t.opens, t.slow);
}

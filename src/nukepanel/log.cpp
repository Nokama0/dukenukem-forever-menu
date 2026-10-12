#include <windows.h>
#include <stdio.h>
#include "log.h"
#include "logfmt.h"
#include "paths.h"

// One run per file (a stale multi-run log misled a crash investigation). Kept as nukepanel.log.prev so a crash survives
// one more launch. Interlocked: two first calls racing must roll once, not twice.
static void log_roll_once(const wchar_t* path) {
  static volatile LONG s_rolled = 0;
  if (InterlockedCompareExchange(&s_rolled, 1, 0) != 0) return;
  wchar_t prev[MAX_PATH];
  if (wcslen(path) + 6 >= MAX_PATH) return;
  wcscpy_s(prev, MAX_PATH, path);
  wcscat_s(prev, MAX_PATH, L".prev");
  MoveFileExW(path, prev, MOVEFILE_REPLACE_EXISTING);
}

static volatile LONG s_dropped = 0;
static volatile LONG s_dropped_reported = 0;

long np_log_dropped(void) { return s_dropped; }

// The one place that opens the file; takes an already-terminated line so the drop-note write can call it directly (no
// path back into np_log, so no recursion). FILE_SHARE_WRITE avoids a sharing violation against a second appender;
// FILE_APPEND_DATA makes each call atomic.
static volatile LONG s_wCalls = 0, s_wTotalUs = 0, s_wMaxUs = 0;

static bool np_log_raw_write_impl(const wchar_t* path, const char* line, int len);
static bool np_log_raw_write(const wchar_t* path, const char* line, int len) {
  LARGE_INTEGER f, a, b;
  QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
  const bool ok = np_log_raw_write_impl(path, line, len);
  QueryPerformanceCounter(&b);
  const LONG us = (LONG)((b.QuadPart - a.QuadPart) * 1000000 / f.QuadPart);
  InterlockedIncrement(&s_wCalls);
  InterlockedExchangeAdd(&s_wTotalUs, us);
  for (;;) {
    const LONG m = s_wMaxUs;
    if (us <= m || InterlockedCompareExchange(&s_wMaxUs, us, m) == m) break;
  }
  return ok;
}

void np_log_take_write_stats(unsigned* calls, unsigned* total_us, unsigned* max_us) {
  *calls = (unsigned)InterlockedExchange(&s_wCalls, 0);
  *total_us = (unsigned)InterlockedExchange(&s_wTotalUs, 0);
  *max_us = (unsigned)InterlockedExchange(&s_wMaxUs, 0);
}

static bool np_log_raw_write_impl(const wchar_t* path, const char* line, int len) {
  const HANDLE h = CreateFileW(path, FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    InterlockedIncrement(&s_dropped);
    return false;
  }
  DWORD written = 0;
  const BOOL ok = WriteFile(h, line, (DWORD)len, &written, NULL);
  CloseHandle(h);
  if (!ok || written != (DWORD)len) {
    InterlockedIncrement(&s_dropped);
    return false;
  }
  return true;
}

static volatile LONG s_enabled = 0;
void np_log_enable(void) { InterlockedExchange(&s_enabled, 1); }

// The folder is created on the first write only (a host that is not the game never gets here).
static bool log_path(wchar_t* path) {
  if (!s_enabled) return false;
  static volatile LONG s_dir_made = 0;
  if (!np_our_dir(path, MAX_PATH)) return false;
  if (wcslen(path) + 14 >= MAX_PATH) return false;
  if (!s_dir_made) {
    // A failed create is retried on the next line; one that already exists counts as made.
    if (!CreateDirectoryW(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    InterlockedExchange(&s_dir_made, 1);
  }
  wcscat_s(path, MAX_PATH, L"nukepanel.log");
  log_roll_once(path);
  return true;
}

void np_log_block(const char* text, int len) {
  wchar_t path[MAX_PATH];
  if (len > 0 && log_path(path)) np_log_raw_write(path, text, len);
}

void np_log(const char* fmt, ...) {
  wchar_t path[MAX_PATH];
  if (!log_path(path)) return;

  char line[2048];   // the longest lines (pehook's subscriber counts, aim's) pass 1024; pehook builds 1536
  va_list ap;
  va_start(ap, fmt);
  int len = logfmt_line(line, sizeof(line), fmt, ap);
  va_end(ap);
  if (len <= 0) return;

  // Open and close per line: no handle survives across a crash mid-frame.
  const bool wrote_ok = np_log_raw_write(path, line, len);

  // Self-limiting drop note, at most once per rise (InterlockedExchange
  // reads-and-stores atomically, so only a thread observing cur > last
  // fires), and only after this write succeeded.
  if (wrote_ok) {
    const long cur = s_dropped;
    const long last = InterlockedExchange(&s_dropped_reported, cur);
    if (cur > last) {
      char note[128];
      const int nlen = _snprintf_s(note, sizeof(note), _TRUNCATE,
                                    "log: %ld line(s) lost to a failed or short write so far this session", cur);
      if (nlen > 0) {
        note[nlen] = '\n';
        np_log_raw_write(path, note, nlen + 1);
      }
    }
  }
}

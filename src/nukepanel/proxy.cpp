// winmm.dll proxy: the load vector. winmm is not a KnownDLL, so a copy beside DukeForever.exe is consulted before the
// real one (Engine.dll imports it too). STUB TABLE, NOT TYPED FORWARDS: proxy_stubs.cpp (generated) holds one naked
// jump-thunk per export rather than hand-written typed wrappers.

#include <windows.h>
#include <stdio.h>
#include "log.h"
#include "proxy_count.h"
#include "hostgate.h"
#include "fileio.h"
#include "stall.h"
#include "crashlog.h"

extern "C" void np_first_call(void);   // boot.cpp: the one hook point, run once after resolve

extern "C" void* g_np_real[NP_EXPORT_COUNT];
extern "C" void* g_np_missing[NP_EXPORT_COUNT];
extern "C" const char* const g_np_names[NP_EXPORT_COUNT];
extern "C" const unsigned short g_np_ords[NP_EXPORT_COUNT];

static CRITICAL_SECTION g_np_cs;
static volatile LONG g_np_resolved = 0;
static int s_is_game;
// DllMain's thread, for the thread log (frame.cpp).
extern "C" volatile unsigned long g_np_attach_tid = 0;

// Called only when a caller actually invokes a missing export, so it never returns: guessing the
// argument count of an unknown stdcall export would corrupt the caller's stack.
extern "C" void np_report_missing(int i) {
  const char* name = g_np_names[i] ? g_np_names[i] : "(NONAME)";
  np_log("winmm: MISSING export %s called", name);
  char msg[256];
  _snprintf_s(msg, sizeof(msg), _TRUNCATE,
              "Nuke Panel: winmm export %s is missing from the system winmm.dll; "
              "delete winmm.dll from the game folder.", name);
  FatalAppExitA(0, msg);
}

// Loads the real winmm.dll and fills every g_np_real[i], once. Runs from a stub's first-call thunk,
// never DllMain: LoadLibrary under the loader lock can deadlock.
extern "C" void np_resolve_all(int) {
  if (g_np_resolved) return;
  EnterCriticalSection(&g_np_cs);
  if (g_np_resolved) { LeaveCriticalSection(&g_np_cs); return; }

  // Absolute path: a bare LoadLibraryA("winmm.dll") would resolve back to this file. Under WOW64
  // GetSystemDirectoryA lands on the 32-bit SysWOW64 copy.
  char sys[MAX_PATH];
  const UINT sn = GetSystemDirectoryA(sys, MAX_PATH);
  int resolved = 0;
  HMODULE real = NULL;
  if (sn != 0 && sn < MAX_PATH - 16) {
    strcat_s(sys, MAX_PATH, "\\winmm.dll");
    real = LoadLibraryA(sys);
  }

  if (real) {
    for (int i = 0; i < NP_EXPORT_COUNT; i++) {
      FARPROC p = g_np_names[i] ? GetProcAddress(real, g_np_names[i])
                                 : GetProcAddress(real, MAKEINTRESOURCEA(g_np_ords[i]));
      if (p) {
        g_np_real[i] = (void*)p;
        resolved++;
      } else {
        np_log("winmm: export not found: %s (ordinal %u)",
               g_np_names[i] ? g_np_names[i] : "(NONAME)", g_np_ords[i]);
        g_np_real[i] = g_np_missing[i];
      }
    }
  } else {
    np_log("winmm: failed to load real winmm.dll from %s", sn ? sys : "(GetSystemDirectoryA failed)");
    // Every call then reaches the missing-export exit instead of a thunk that jumps to itself.
    for (int i = 0; i < NP_EXPORT_COUNT; i++) g_np_real[i] = g_np_missing[i];
  }

  np_log("winmm: real loaded, %d/%d resolved", resolved, NP_EXPORT_COUNT);
  if (s_is_game) np_first_call();
  InterlockedExchange(&g_np_resolved, 1);
  LeaveCriticalSection(&g_np_cs);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(inst);
    InitializeCriticalSection(&g_np_cs);   // forwarding needs it whatever the host
    // Any other host (launcher, editor) gets forwarding only: no log, no hooks.
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    s_is_game = n != 0 && n < MAX_PATH && host_is_game(path);
    if (!s_is_game) return TRUE;
    g_np_attach_tid = GetCurrentThreadId();
    np_log_enable();
    np_log("attach pid=%lu tid=%lu", GetCurrentProcessId(), GetCurrentThreadId());
  } else if (reason == DLL_PROCESS_DETACH) {
    if (!s_is_game) return TRUE;
    fileio_detach();
    stall_detach();
    crashlog_detach();
    np_log("detach dropped=%ld", np_log_dropped());
  }
  return TRUE;
}

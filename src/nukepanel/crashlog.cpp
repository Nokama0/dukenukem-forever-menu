// The exception and exit log. Added 2026-10-04 when a build that detours Engine.dll's SetMouseCapture made the game
// exit silently before the device existed: no Application Error event, no dump. Observes only; never handles.
#include <windows.h>
#include <tlhelp32.h>
#include <intrin.h>
#include <stdio.h>
#include <string.h>
#include "crashlog.h"
#include "crashrule.h"
#include "iat.h"
#include "log.h"
#include "stall.h"
#include "d3dcap.h"

// Filled once before the handler and the hooks go in, read-only after. A module loaded later is a bare address in
// an exception line (the handler asks no API); the exit path asks the loader for it instead.
enum { CRASH_MODS = 256 };
static StallMod s_mods[CRASH_MODS];
static int s_modCount;

static volatile LONG s_seen;   // exceptions counted (signals excluded); the first CRASH_MAX_LOGGED get a line
static volatile LONG s_seenRender;   // the same on the engine's render thread, with slots of its own
static PVOID s_veh;

static void load_modules(void) {
  const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
  if (snap == INVALID_HANDLE_VALUE) {
    np_log("crashlog: module snapshot failed (err=%lu); addresses unnamed", GetLastError());
    return;
  }
  MODULEENTRY32 me;
  me.dwSize = sizeof me;
  for (BOOL ok = Module32First(snap, &me); ok && s_modCount < CRASH_MODS; ok = Module32Next(snap, &me)) {
    StallMod* m = &s_mods[s_modCount++];
    m->base = (unsigned)(UINT_PTR)me.modBaseAddr;
    m->size = (unsigned)me.modBaseSize;
    strncpy_s(m->name, sizeof m->name, me.szModule, _TRUNCATE);
  }
  CloseHandle(snap);
}

// First in the chain, so it sees an exception before BugHandler's filter or any frame handler can end the process.
// Stack buffers and np_log only: no allocation on a thread that may be in any state.
static LONG CALLBACK on_exception(EXCEPTION_POINTERS* ep) {
  const EXCEPTION_RECORD* er = ep->ExceptionRecord;
  if (crash_skip_code(er->ExceptionCode)) return EXCEPTION_CONTINUE_SEARCH;
  if (GetCurrentThreadId() == stall_watchdog_tid()) return EXCEPTION_CONTINUE_SEARCH;
  // A vectored handler sees every thread's exceptions; the render job's get their own slots (freecam.md section 7).
  const bool render = GetCurrentThreadId() == d3dcap_render_tid();
  const LONG nth = InterlockedIncrement(render ? &s_seenRender : &s_seen);
  if (render ? !crash_take_render_slot(nth) : !crash_take_slot(nth)) return EXCEPTION_CONTINUE_SEARCH;
  char where[80];
  crash_fmt_where(where, sizeof where, s_mods, s_modCount, (unsigned)(UINT_PTR)er->ExceptionAddress);
  char av[40] = "";
  if ((er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || er->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
      er->NumberParameters >= 2)
    _snprintf_s(av, sizeof av, _TRUNCATE, " %s %08X", crash_av_kind((unsigned long)er->ExceptionInformation[0]),
                (unsigned)er->ExceptionInformation[1]);
  const CONTEXT* c = ep->ContextRecord;
  np_log("exception: code=%08lX addr=%p (%s) tid=%lu eax=%08lX ecx=%08lX edx=%08lX esp=%08lX ebp=%08lX%s n=%ld"
         " flags=%lX%s", er->ExceptionCode, er->ExceptionAddress, where, GetCurrentThreadId(), c->Eax, c->Ecx, c->Edx,
         c->Esp, c->Ebp, av, nth, er->ExceptionFlags, render ? " (render thread)" : "");
  return EXCEPTION_CONTINUE_SEARCH;
}

static int seh_read(unsigned addr, unsigned* out, void*) {
  __try {
    *out = *(const volatile unsigned*)(UINT_PTR)addr;
    return 1;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
}

// The boot table first; a module loaded since boot (BugHandler may be) is asked of the loader. Exit path only.
static int name_addr(char* out, int cap, unsigned addr) {
  if (stall_find_module(s_mods, s_modCount, addr) >= 0) return crash_fmt_where(out, cap, s_mods, s_modCount, addr);
  HMODULE h = 0;
  char path[MAX_PATH];
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCSTR)(UINT_PTR)addr, &h) && GetModuleFileNameA(h, path, MAX_PATH))
    return stall_fmt_addr(out, cap, path, addr, (unsigned)(UINT_PTR)h);
  return stall_fmt_addr(out, cap, 0, addr, 0);
}

// fp is the hook's own EBP: [fp] the caller's EBP, [fp+4] the return address. The walk stays inside this thread's
// stack (TEB limits) and reads through SEH as a backstop.
static void log_exit(const char* fn, UINT code, const char* target, unsigned ret, unsigned fp) {
  const NT_TIB* tib = (const NT_TIB*)NtCurrentTeb();
  const unsigned lo = (unsigned)(UINT_PTR)tib->StackLimit, hi = (unsigned)(UINT_PTR)tib->StackBase;
  unsigned rets[EXIT_MAX_FRAMES + 1];
  const int n = stall_walk(fp, lo, hi, seh_read, 0, rets, EXIT_MAX_FRAMES + 1);
  // rets[0] repeats `from` when the hook's frame is where expected; otherwise the chain is printed as found.
  const int first = n > 0 && rets[0] == ret ? 1 : 0;
  char from[160], stack[720];
  name_addr(from, sizeof from, ret);
  int len = 0;
  stack[0] = 0;
  for (int i = first; i < n && len < (int)sizeof(stack) - 2; ++i) {
    if (len) stack[len++] = ' ';
    len += name_addr(stack + len, (int)sizeof(stack) - len, rets[i]);
  }
  np_log("exit: %s(%u) from %s tid=%lu%s exceptions=%ld ; stack(%d)%s %s", fn, code, from, GetCurrentThreadId(),
         target, s_seen, n - first, first ? "" : " own frame not found", stack);
}

enum { XM_EXE, XM_ENGINE, XM_DNCOMMON, XM_BUGHANDLER, XM_MSVCR, XM_COUNT };
static const wchar_t* const kModW[XM_COUNT] = { 0, L"Engine.dll", L"dnCommon.dll", L"BugHandler.dll",
                                                L"MSVCR90.dll" };
static const char* const kWho[XM_COUNT] = { "exitlog(DukeForever.exe)", "exitlog(Engine.dll)",
                                            "exitlog(dnCommon.dll)", "exitlog(BugHandler.dll)",
                                            "exitlog(MSVCR90.dll)" };
// One hook per module and slot: each calls whatever its own slot held (a shim may own one).
static void* s_exitOrig[XM_COUNT];
static void* s_termOrig[XM_COUNT];

typedef VOID(WINAPI* ExitProcessFn)(UINT);
typedef BOOL(WINAPI* TerminateProcessFn)(HANDLE, UINT);

// A function containing __asm always gets an EBP frame, so reading EBP here gives this hook's own frame.
template <int M> static VOID WINAPI hk_exit(UINT code) {
  unsigned fp;
  __asm mov fp, ebp
  log_exit("ExitProcess", code, "", (unsigned)(UINT_PTR)_ReturnAddress(), fp);
  ((ExitProcessFn)s_exitOrig[M])(code);
}

template <int M> static BOOL WINAPI hk_term(HANDLE h, UINT code) {
  unsigned fp;
  __asm mov fp, ebp
  char target[40];
  const DWORD pid = GetProcessId(h);
  if (pid == GetCurrentProcessId()) strcpy_s(target, sizeof target, " target=self");
  else _snprintf_s(target, sizeof target, _TRUNCATE, " target=pid %lu", pid);
  log_exit("TerminateProcess", code, target, (unsigned)(UINT_PTR)_ReturnAddress(), fp);
  return ((TerminateProcessFn)s_termOrig[M])(h, code);
}

static void* const kExitHooks[XM_COUNT] = { (void*)&hk_exit<0>, (void*)&hk_exit<1>, (void*)&hk_exit<2>,
                                            (void*)&hk_exit<3>, (void*)&hk_exit<4> };
static void* const kTermHooks[XM_COUNT] = { (void*)&hk_term<0>, (void*)&hk_term<1>, (void*)&hk_term<2>,
                                            (void*)&hk_term<3>, (void*)&hk_term<4> };

void crashlog_install(void) {
  load_modules();
  s_veh = AddVectoredExceptionHandler(1, on_exception);
  np_log("crashlog: %d modules named; first exception handler %s; the first %d exceptions get a line", s_modCount,
         s_veh ? "installed" : "NOT installed", CRASH_MAX_LOGGED);
  int n = 0, want = 0;
  for (int k = 0; k < XM_COUNT; ++k) {
    const HMODULE m = k == XM_EXE ? GetModuleHandleW(NULL) : GetModuleHandleW(kModW[k]);
    if (!m) { np_log("%s: not loaded; its exit slots are not watched", kWho[k]); continue; }
    want += 2;
    n += iat_patch(m, "KERNEL32.dll", "ExitProcess", kExitHooks[k], &s_exitOrig[k], kWho[k]);
    n += iat_patch(m, "KERNEL32.dll", "TerminateProcess", kTermHooks[k], &s_termOrig[k], kWho[k]);
  }
  np_log("exitlog: %d/%d import slots watched (a module that does not import one has no slot)", n, want);
}

// The handler stays installed: the DLL is never unloaded, and exceptions during other modules' detach still count.
void crashlog_detach(void) {
  const LONG seen = s_seen;
  np_log("crashlog: %ld exceptions this session, %ld logged; %ld on the render thread", seen,
         seen < CRASH_MAX_LOGGED ? seen : (LONG)CRASH_MAX_LOGGED, s_seenRender);
}

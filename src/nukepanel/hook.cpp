// Writes as little as it can and refuses to write at all unless the bytes
// under the patch are the exact ones the analysis read.
#include "hook.h"
#include "hookrule.h"
#include "log.h"
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>

#define HOOK_MAX_PROLOGUE 32
#define HOOK_JMP_LEN      5
// Past this many threads we refuse rather than write a partly protected patch.
#define HOOK_MAX_THREADS  128

bool hook_readable(const void* p, unsigned n) {
  const unsigned char* a = (const unsigned char*)p;
  const unsigned char* end = a + n;
  if (!p || end < a) return false;
  while (a < end) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(a, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const DWORD rd = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                     PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!(mbi.Protect & rd)) return false;
    a = (const unsigned char*)mbi.BaseAddress + mbi.RegionSize;
  }
  return true;
}

static void hook_resume_all(HANDLE* held, int n) {
  for (int i = 0; i < n; i++) { ResumeThread(held[i]); CloseHandle(held[i]); }
}

// Suspends every other thread so no instruction pointer can sit inside the
// bytes being replaced. Returns the count, or -1 (nothing left suspended).
// No logging or allocation here: a suspended thread may hold any lock.
static int hook_suspend_others(HANDLE* held, int max) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snap == INVALID_HANDLE_VALUE) return -1;
  const DWORD pid = GetCurrentProcessId();
  const DWORD self = GetCurrentThreadId();
  THREADENTRY32 te;
  te.dwSize = sizeof(te);
  int n = 0, overflow = 0;
  if (Thread32First(snap, &te)) {
    do {
      if (te.dwSize < FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(DWORD)) continue;
      if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
      if (n >= max) { overflow = 1; break; }
      HANDLE h = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
      if (!h) continue;                       // exited between snapshot and open
      if (SuspendThread(h) == (DWORD)-1) { CloseHandle(h); continue; }
      held[n++] = h;
    } while (te.dwSize = sizeof(te), Thread32Next(snap, &te));
  }
  CloseHandle(snap);
  if (overflow) { hook_resume_all(held, n); return -1; }
  return n;
}

static void hook_log_bytes(const char* what, const unsigned char* p, unsigned n) {
  char text[HOOK_MAX_PROLOGUE * 3 + 1];
  unsigned used = 0;
  text[0] = 0;
  for (unsigned i = 0; i < n && i < HOOK_MAX_PROLOGUE; i++)
    used += (unsigned)sprintf_s(text + used, sizeof(text) - used, i ? " %02X" : "%02X", p[i]);
  np_log("hook: %s %s", what, text);
}

int hook_install_at(void* targetv, const unsigned char* fp, unsigned fpLen,
                    unsigned prologueLen, void* replacement, void** trampolineOut) {
  if (trampolineOut) *trampolineOut = 0;
  unsigned char* target = (unsigned char*)targetv;
  if (!target || !fp || !replacement || fpLen == 0 || fpLen < prologueLen ||
      prologueLen < HOOK_JMP_LEN || prologueLen > HOOK_MAX_PROLOGUE) {
    np_log("hook: refusing %p, arguments make no sense (fp=%u prologue=%u)",
           targetv, fpLen, prologueLen);
    return 0;
  }
  // Covers the fingerprint compare and the 16 bytes logged on a mismatch.
  if (!hook_readable(target, fpLen > 16 ? fpLen : 16)) {
    np_log("hook: refusing %p, not committed readable memory", targetv);
    return 0;
  }
  if (!hook_prologue_ok(target, fp, fpLen, prologueLen)) {
    np_log("hook: fingerprint MISMATCH at %p; installing nothing", targetv);
    hook_log_bytes("expected", fp, fpLen);
    hook_log_bytes("found   ", target, 16);
    return 0;
  }

  unsigned char* tramp = (unsigned char*)VirtualAlloc(0, 64, MEM_COMMIT | MEM_RESERVE,
                                                      PAGE_EXECUTE_READWRITE);
  if (!tramp) { np_log("hook: no trampoline memory for %p", targetv); return 0; }
  // A JMP already at the target (the Steam overlay's) is replayed, relocated: copied raw its rel32
  // would land somewhere else. Only its 5 bytes are replaced, so whoever owns it still finds the
  // original bytes after it.
  const bool foreign = target[0] == 0xE9;
  unsigned patchLen = prologueLen;
  if (foreign) {
    patchLen = HOOK_JMP_LEN;
    unsigned char* dest = target + HOOK_JMP_LEN + *(const int*)(target + 1);
    tramp[0] = 0xE9;
    *(int*)(tramp + 1) = (int)(dest - (tramp + HOOK_JMP_LEN));
  } else {
    memcpy(tramp, target, prologueLen);
    tramp[prologueLen] = 0xE9;
    *(int*)(tramp + prologueLen + 1) = (int)((target + prologueLen) - (tramp + prologueLen + HOOK_JMP_LEN));
  }
  DWORD trampOld = 0;
  if (!VirtualProtect(tramp, 64, PAGE_EXECUTE_READ, &trampOld)) {
    np_log("hook: trampoline %p could not be made read-only (err %lu)", (void*)tramp, GetLastError());
    VirtualFree(tramp, 0, MEM_RELEASE);
    return 0;
  }

  unsigned char patch[HOOK_MAX_PROLOGUE];
  patch[0] = 0xE9;
  *(int*)(patch + 1) = (int)((unsigned char*)replacement - (target + HOOK_JMP_LEN));
  for (unsigned i = HOOK_JMP_LEN; i < patchLen; i++) patch[i] = 0x90;

  DWORD old = 0;
  if (!VirtualProtect(target, patchLen, PAGE_EXECUTE_READWRITE, &old)) {
    np_log("hook: %p is not writable (err %lu)", targetv, GetLastError());
    VirtualFree(tramp, 0, MEM_RELEASE);
    return 0;
  }
  HANDLE held[HOOK_MAX_THREADS];
  const int suspended = hook_suspend_others(held, HOOK_MAX_THREADS);
  if (suspended < 0) {
    DWORD back = 0;
    VirtualProtect(target, patchLen, old, &back);
    VirtualFree(tramp, 0, MEM_RELEASE);
    np_log("hook: %p refused, could not hold the other threads (max %d)", targetv, HOOK_MAX_THREADS);
    hook_log_bytes("found   ", target, 16);
    return 0;
  }
  if (trampolineOut) *trampolineOut = tramp;
  unsigned char was[HOOK_MAX_PROLOGUE];
  memcpy(was, target, patchLen);
  memcpy(target, patch, patchLen);
  FlushInstructionCache(GetCurrentProcess(), target, patchLen);
  hook_resume_all(held, suspended);
  DWORD ignored = 0;
  VirtualProtect(target, patchLen, old, &ignored);

  np_log("hook: installed at %p -> %p, trampoline %p, %s, held %d other thread(s)",
         targetv, replacement, (void*)tramp, foreign ? "over a foreign JMP" : "plain prologue",
         suspended);
  hook_log_bytes("was     ", was, patchLen);
  hook_log_bytes("now     ", target, 16);
  return 1;
}

int hook_install_live(void* target, const char* what, void* replacement, void** trampolineOut) {
  if (trampolineOut) *trampolineOut = 0;
  if (!hook_readable(target, 16)) {
    np_log("hook: %s at %p is not readable code; not hooked", what, target);
    return 0;
  }
  unsigned char cur[16];
  memcpy(cur, target, sizeof(cur));
  const unsigned len = cur[0] == 0xE9 ? HOOK_JMP_LEN : hook_prologue_len(cur, sizeof(cur));
  if (len == 0) {
    np_log("hook: %s at %p starts with an instruction we do not relocate; not hooked", what, target);
    hook_log_bytes("found   ", cur, 16);
    return 0;
  }
  const unsigned fpLen = len > 8 ? len : 8;
  np_log("hook: %s at %p, prologue %u byte(s)", what, target, len);
  return hook_install_at(target, cur, fpLen, len, replacement, trampolineOut);
}

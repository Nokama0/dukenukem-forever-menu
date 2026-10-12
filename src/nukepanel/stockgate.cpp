// D8 stock gate: SHA-256 (BCrypt) of the three analysed binaries in the exe's folder.
#include <windows.h>
#include <bcrypt.h>
#include "stockgate.h"
#include "log.h"

static volatile LONG s_ok;

bool stock_ok(void) { return s_ok != 0; }

// Hashes `path` into d; false when the file cannot be read in full. *what and *err name the call
// that failed and its own error (GetLastError or NTSTATUS), taken at the failure point.
static bool sha256_file(const wchar_t* path, unsigned char* d, unsigned char* buf, DWORD bufLen,
                        const char** what, unsigned long* err) {
  bool ok = false;
  BCRYPT_ALG_HANDLE alg = 0;
  BCRYPT_HASH_HANDLE h = 0;
  NTSTATUS st;
  const HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
  if (f == INVALID_HANDLE_VALUE) { *what = "CreateFileW"; *err = GetLastError(); return false; }
  if ((st = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0)) != 0) {
    *what = "BCryptOpenAlgorithmProvider"; *err = (unsigned long)st;
  } else if ((st = BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0)) != 0) {
    *what = "BCryptCreateHash"; *err = (unsigned long)st;
  } else {
    for (;;) {
      DWORD got = 0;
      if (!ReadFile(f, buf, bufLen, &got, NULL)) { *what = "ReadFile"; *err = GetLastError(); break; }
      if (got == 0) {
        if ((st = BCryptFinishHash(h, d, 32, 0)) != 0) { *what = "BCryptFinishHash"; *err = (unsigned long)st; }
        else ok = true;
        break;
      }
      if ((st = BCryptHashData(h, buf, got, 0)) != 0) { *what = "BCryptHashData"; *err = (unsigned long)st; break; }
    }
  }
  if (h) BCryptDestroyHash(h);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  CloseHandle(f);
  return ok;
}

bool stock_run(void) {
  static volatile LONG s_done = 0;
  if (InterlockedCompareExchange(&s_done, 1, 0) != 0) return stock_ok();

  wchar_t dir[MAX_PATH];
  const DWORD n = GetModuleFileNameW(NULL, dir, MAX_PATH);
  wchar_t* slash = 0;
  for (DWORD i = 0; i < n && i < MAX_PATH; i++)
    if (dir[i] == L'\\') slash = dir + i;
  if (n == 0 || n >= MAX_PATH || !slash) { np_log("stock: MISMATCH (exe path unreadable)"); return false; }
  slash[1] = 0;

  const DWORD bufLen = 1 << 20;
  unsigned char* buf = (unsigned char*)VirtualAlloc(0, bufLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!buf) { np_log("stock: MISMATCH (no read buffer)"); return false; }
  const DWORD t0 = GetTickCount();
  bool all = true;
  for (int i = 0; i < 3; i++) {
    wchar_t path[MAX_PATH];
    if (wcscpy_s(path, MAX_PATH, dir) != 0 || wcscat_s(path, MAX_PATH, kStockFiles[i].name) != 0) {
      np_log("stock: MISMATCH %ls (path too long)", kStockFiles[i].name);
      all = false;
      continue;
    }
    unsigned char d[32];
    const char* what = "?";
    unsigned long err = 0;
    if (!sha256_file(path, d, buf, bufLen, &what, &err)) {
      np_log("stock: MISMATCH %ls (unreadable: %s failed, 0x%08lX)", kStockFiles[i].name, what, err);
      all = false;
    } else if (!stock_digest_matches(d, kStockFiles[i].sha256)) {
      char hex[65];
      for (int k = 0; k < 32; k++) wsprintfA(hex + 2 * k, "%02x", d[k]);
      np_log("stock: MISMATCH %ls sha256=%s", kStockFiles[i].name, hex);
      all = false;
    }
  }
  VirtualFree(buf, 0, MEM_RELEASE);
  if (all) {
    InterlockedExchange(&s_ok, 1);
    np_log("stock: OK (3 files, %lu ms)", GetTickCount() - t0);
  } else {
    np_log("stock: not stock; no detours this session (%lu ms)", GetTickCount() - t0);
  }
  return all;
}

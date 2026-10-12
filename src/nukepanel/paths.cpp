#include <windows.h>
#include "paths.h"

bool np_our_dir(wchar_t* out, int cap) {
  HMODULE self = NULL;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCWSTR)&np_our_dir, &self))
    return false;
  wchar_t mod[MAX_PATH];
  const DWORD n = GetModuleFileNameW(self, mod, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return false;
  return paths_join(mod, L"NukePanel\\", out, cap);
}

#pragma once
#include <wchar.h>
// True only for the game's own exe; any other host that loads winmm.dll gets plain forwarding and
// must never create or roll our log. Pure rule: final path component, case-insensitive.
inline bool host_is_game(const wchar_t* exePath) {
  if (!exePath || !*exePath) return false;
  const wchar_t* base = exePath;
  for (const wchar_t* p = exePath; *p; ++p)
    if (*p == L'\\' || *p == L'/') base = p + 1;
  return _wcsicmp(base, L"DukeForever.exe") == 0;
}

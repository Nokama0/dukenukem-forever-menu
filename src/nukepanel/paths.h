#pragma once
#include <wchar.h>
// Our folder lives beside our own module (GAME\System\NukePanel\), never derived from the exe or the
// game's ini files. paths_join is the pure rule; np_our_dir (paths.cpp) feeds it the module path.
// Copies modulePath up to and including its last '\', appends leaf; false if no '\' or it does not fit.
inline bool paths_join(const wchar_t* modulePath, const wchar_t* leaf, wchar_t* out, int cap) {
  if (!modulePath || !leaf || !out || cap <= 0) return false;
  const wchar_t* last = nullptr;
  for (const wchar_t* p = modulePath; *p; ++p)
    if (*p == L'\\') last = p;
  if (!last) return false;
  const size_t dir = (size_t)(last - modulePath) + 1, ln = wcslen(leaf);
  if (dir + ln + 1 > (size_t)cap) return false;
  wmemcpy(out, modulePath, dir);
  wmemcpy(out + dir, leaf, ln + 1);
  return true;
}

// `...\System\NukePanel\` for this module. False on any failure (out untouched or empty).
bool np_our_dir(wchar_t* out, int cap);

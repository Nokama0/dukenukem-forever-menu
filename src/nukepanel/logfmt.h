#pragma once
// Header-only and pure (no allocation, no I/O), so the tests can pin this rule without linking log.cpp.
// _CRT_SECURE_NO_WARNINGS (build.ps1) silences _vsnprintf's deprecation warning under /WX: it is used deliberately, not
// a carelessness _s fixes.
#include <stdio.h>
#include <stdarg.h>

// Formats `fmt`/`ap` into `out` (capacity `cap`, `cap >= 2`), guaranteeing out[n-1] == '\n' and no NUL before out[n].
// Returns n. _vsnprintf returns -1 on truncation and leaves no NUL, so "how much fit" is read from `limit`; the content
// limit is cap-2 so the '\n' this function appends never lands past out[cap-1].
inline int logfmt_line(char* out, int cap, const char* fmt, va_list ap) {
  if (cap < 2) return 0;
  int limit = cap - 2;
  int n = _vsnprintf(out, (size_t)limit, fmt, ap);
  int len = (n < 0 || n > limit) ? limit : n;
  // Strip a caller-supplied trailing \r\n/\n so it isn't doubled below.
  while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) len--;
  out[len] = '\n';
  return len + 1;
}

#pragma once
// Display names for the ESP label: our own table (class name -> display name), else the class name with `_` as
// spaces. The table is filled from the classes the enemy-class log shows; empty until the first
// launch has named them.
#include <string.h>

struct EspName { const char* cls; const char* shown; };
static const EspName kEspNames[] = { { 0, 0 } };

// Copies into out (cap bytes, always terminated); a table hit wins, else the class name, `_` read as a space.
inline void esp_display_name(const char* cls, char* out, int cap) {
  if (!out || cap <= 0) return;
  out[0] = 0;
  if (!cls) return;
  for (int i = 0; kEspNames[i].cls; ++i)
    if (_stricmp(kEspNames[i].cls, cls) == 0) { strncpy_s(out, cap, kEspNames[i].shown, _TRUNCATE); return; }
  int n = 0;
  for (; cls[n] && n < cap - 1; ++n) out[n] = cls[n] == '_' ? ' ' : cls[n];
  out[n] = 0;
}

// UTF-16 to ASCII for labels and logs: anything outside 0x20..0x7E becomes '?'. False when the string was empty.
inline bool esp_wide_to_ascii(const wchar_t* w, char* out, int cap) {
  if (!out || cap <= 0) return false;
  out[0] = 0;
  if (!w) return false;
  int n = 0;
  for (; w[n] && n < cap - 1; ++n) out[n] = (w[n] >= 0x20 && w[n] < 0x7F) ? (char)w[n] : '?';
  out[n] = 0;
  return n > 0;
}

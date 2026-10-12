// Glue for lvnames.h: the engine's find-only name lookup by ASCII, and the per-serial instrument line.
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include "lvnames.h"
#include "uname.h"
#include "world.h"
#include "log.h"

int ln_find_ascii(const char* s, void*) {
  wchar_t w[96];
  _snwprintf_s(w, 96, _TRUNCATE, L"%hs", s);
  return uname_find(w);
}

// One line per module per level serial, and again when a key's index changes inside it (it appeared late or moved).
void ln_report(const char* module, LnKeys* k, const char* const* names, int n) {
  const unsigned serial = world_level_serial();
  if (!ln_due(k, serial, n)) return;
  static LnCap s_cap;   // every module's lines share it
  if (!ln_cap_take(&s_cap, serial)) {
    if (s_cap.dropped == 1) np_log("lvnames: %d lines this level serial %u: the rest are not logged", LN_LINES_PER_LEVEL, serial);
    ln_logged(k, serial, n);   // not due again until another change
    return;
  }
  char keys[512];
  const int moved = ln_format(k, names, n, keys, sizeof keys);
  np_log("%s: level names, serial %u: %d key(s) moved or gone;%s", module, serial, moved, keys[0] ? keys : " none yet");
  ln_logged(k, serial, n);
}

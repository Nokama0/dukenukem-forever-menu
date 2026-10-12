#pragma once
// Level-name keys (docs/re/engine-map.md "Names move across level loads"): the index of a name a map adds (Tag,
// Event, instance name, map-package class) is not the same after a load. A module looks its keys up again on every
// scan pass; a lookup that finds nothing is 0 (absent), never the old index. Pure for the suite.
#include <stdio.h>
#include <string.h>

enum { LN_MAX = 40 };
struct LnKeys {
  int cur[LN_MAX];        // this pass's index per key, 0 absent
  int snap[LN_MAX];       // the indices at the last logged line
  unsigned loggedSerial;  // the level serial of that line
  int logged;
};

// names[i] -> cur[i]; n <= LN_MAX. Returns how many exist. find: the name's index, 0 when absent.
inline int ln_refresh(LnKeys* k, const char* const* names, int n, int (*find)(const char* s, void* ctx), void* ctx) {
  int got = 0;
  if (n > LN_MAX) n = LN_MAX;
  for (int i = 0; i < n; ++i) {
    k->cur[i] = find(names[i], ctx);
    got += k->cur[i] != 0;
  }
  return got;
}
// A line is due on a new level serial, and again when any index differs from the last line's.
inline bool ln_due(const LnKeys* k, unsigned serial, int n) {
  if (n > LN_MAX) n = LN_MAX;
  return !k->logged || k->loggedSerial != serial || memcmp(k->cur, k->snap, (size_t)n * sizeof(int)) != 0;
}
// " name=idx" per key, " (moved from old)" when it differed from the last line's and was there, " (gone)" when it was
// there and is not; only keys present now or last time. Returns how many moved or went.
inline int ln_format(const LnKeys* k, const char* const* names, int n, char* out, int cap) {
  if (cap <= 0) return 0;
  out[0] = 0;
  if (n > LN_MAX) n = LN_MAX;
  int o = 0, moved = 0;
  for (int i = 0; i < n; ++i) {
    const int was = k->logged ? k->snap[i] : 0, now = k->cur[i];
    if (!was && !now) continue;
    const bool gone = was && !now, mv = was && now && was != now;
    moved += gone || mv;
    int w;
    if (gone) w = _snprintf_s(out + o, (size_t)(cap - o), _TRUNCATE, " %s=absent (gone, was %d)", names[i], was);
    else if (mv) w = _snprintf_s(out + o, (size_t)(cap - o), _TRUNCATE, " %s=%d (moved from %d)", names[i], now, was);
    else w = _snprintf_s(out + o, (size_t)(cap - o), _TRUNCATE, " %s=%d", names[i], now);
    if (w < 0) break;
    o += w;
  }
  return moved;
}
inline void ln_logged(LnKeys* k, unsigned serial, int n) {
  if (n > LN_MAX) n = LN_MAX;
  memcpy(k->snap, k->cur, (size_t)n * sizeof(int));
  k->loggedSerial = serial;
  k->logged = 1;
}

// ln_report's lines, LN_LINES_PER_LEVEL a level serial across every module: 1 this line may be logged.
enum { LN_LINES_PER_LEVEL = 16 };
struct LnCap { unsigned serial; int have, lines; unsigned dropped; };
inline int ln_cap_take(LnCap* c, unsigned serial) {
  if (!c->have || c->serial != serial) {
    c->have = 1;
    c->serial = serial;
    c->lines = 0;
    c->dropped = 0;
  }
  if (c->lines >= LN_LINES_PER_LEVEL) {
    c->dropped++;
    return 0;
  }
  c->lines++;
  return 1;
}

// --- The glue (lvnames.cpp), game thread -------------------------------------------------------------------------
int ln_find_ascii(const char* s, void* ctx);   // an ln_refresh find: uname_find on an ASCII name
// Logs the module's line when due (a new level serial, or a key whose index differs from the last line's).
void ln_report(const char* module, LnKeys* k, const char* const* names, int n);

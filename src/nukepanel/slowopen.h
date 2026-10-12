#pragma once
// The slow-open rule (fileio.cpp): an open of 50 ms or more is slow and counted; the log names a path at most
// once per 10 s, so a file the game reopens in a loop cannot flood the log. Paths are keyed by a hash that
// ignores ASCII case and slash direction, so the table stores no paths.
#include <string.h>
#include <wchar.h>

enum { SLOWOPEN_MS = 50, SLOWOPEN_REPEAT_MS = 10000, SLOWOPEN_PATHS = 32 };
struct SlowOpenPath { unsigned hash, last_ms; int used; };
struct SlowOpen { SlowOpenPath p[SLOWOPEN_PATHS]; unsigned total; };

inline SlowOpen slowopen_init(void) {
  SlowOpen s;
  memset(&s, 0, sizeof s);
  return s;
}

// FNV-1a over the UTF-16 units, ASCII folded to lower case and '/' to '\'. A null path hashes as "".
inline unsigned slowopen_hash(const wchar_t* path) {
  unsigned h = 2166136261u;
  for (const wchar_t* p = path; p && *p; ++p) {
    unsigned c = (unsigned)*p;
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    else if (c == '/') c = '\\';
    h = (h ^ c) * 16777619u;
  }
  return h;
}

// True when this open should be logged now. now_ms is a wrapping millisecond clock. A full table takes a new
// path in the slot logged longest ago.
inline int slowopen_note(SlowOpen* s, const wchar_t* path, double ms, unsigned now_ms) {
  if (!(ms >= SLOWOPEN_MS)) return 0;
  s->total++;
  const unsigned h = slowopen_hash(path);
  int freeSlot = -1, oldSlot = 0;
  unsigned oldest = 0;
  for (int i = 0; i < SLOWOPEN_PATHS; ++i) {
    SlowOpenPath* e = &s->p[i];
    if (!e->used) {
      if (freeSlot < 0) freeSlot = i;
    } else if (e->hash == h) {
      if (now_ms - e->last_ms < (unsigned)SLOWOPEN_REPEAT_MS) return 0;
      e->last_ms = now_ms;
      return 1;
    } else if (now_ms - e->last_ms > oldest) {
      oldest = now_ms - e->last_ms;
      oldSlot = i;
    }
  }
  const int pick = freeSlot >= 0 ? freeSlot : oldSlot;
  s->p[pick].hash = h;
  s->p[pick].last_ms = now_ms;
  s->p[pick].used = 1;
  return 1;
}

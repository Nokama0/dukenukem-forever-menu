#pragma once
// The exception and exit log's pure rules: which exception codes are signals rather than faults, how many get a
// line, how an access violation's access type is named, and how an address is named from a module table.
#include "stallrule.h"

enum { CRASH_MAX_LOGGED = 16, EXIT_MAX_FRAMES = 8, CRASH_MAX_RENDER = 8 };

// Debugger signals raised as exceptions: thread naming (MSVC's SetThreadName) and OutputDebugString A and W
// (W raises 0x4001000A, then 0x40010006 as its fallback). Skipped without counting.
inline bool crash_skip_code(unsigned long code) {
  return code == 0x406D1388ul || code == 0x40010006ul || code == 0x4001000Aul;
}

// The nth counted exception (1-based) gets a line only within the first CRASH_MAX_LOGGED.
inline bool crash_take_slot(long nth) { return nth >= 1 && nth <= CRASH_MAX_LOGGED; }
// The engine's render thread has slots of its own, so a fault there is logged even after the first 16 elsewhere.
inline bool crash_take_render_slot(long nth) { return nth >= 1 && nth <= CRASH_MAX_RENDER; }

// ExceptionInformation[0] of an access violation or in-page error: 0 read, 1 write, 8 execute (DEP).
inline const char* crash_av_kind(unsigned long type) {
  return type == 0 ? "read" : type == 1 ? "write" : type == 8 ? "exec" : "?";
}

// "<module>+0x<offset>" from the table, or "0x<addr>" when no module in it holds addr. Returns the length.
inline int crash_fmt_where(char* out, int cap, const StallMod* m, int n, unsigned addr) {
  const int i = stall_find_module(m, n, addr);
  return i >= 0 ? stall_fmt_addr(out, cap, m[i].name, addr, m[i].base) : stall_fmt_addr(out, cap, 0, addr, 0);
}

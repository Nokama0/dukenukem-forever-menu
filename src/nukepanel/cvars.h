#pragma once
// Console variables through the engine's own registry (GDnExec / FindVariable, docs/re/qol.md section 4), and the
// pure rule every forced engine dword shares: the post-processing switches and the focus flag.

#include <string.h>

// A FLOAT variable is held by its bits: the rule below compares and restores dwords, so 0.6f comes back exact.
inline int cvar_bits(float f) {
  int b;
  memcpy(&b, &f, 4);
  return b;
}
inline float cvar_float(int bits) {
  float f;
  memcpy(&f, &bits, 4);
  return f;
}

// Forcing a dword the game owns. The game's value is remembered at the first force; while forced, a value the game
// wrote since (anything but ours) becomes the remembered one and is forced again; on release the remembered value
// goes back only while ours is still in place. Released, nothing is remembered, so the game's value is followed.
struct CvarHold { int held; int saved; };
enum { CVAR_NONE = 0, CVAR_FORCE = 1, CVAR_RESTORE = 2 };

inline CvarHold cvar_hold_init(void) {
  CvarHold h = { 0, 0 };
  return h;
}

// force: the option wants `forced` in place this frame. current: the variable read just now. Returns what to do,
// with the value to write in *write for CVAR_FORCE and CVAR_RESTORE.
inline int cvar_step(CvarHold* h, int force, int current, int forced, int* write) {
  if (force) {
    if (!h->held || current != forced) h->saved = current;
    h->held = 1;
    if (current == forced) return CVAR_NONE;
    *write = forced;
    return CVAR_FORCE;
  }
  if (!h->held) return CVAR_NONE;
  h->held = 0;
  if (current != forced || h->saved == current) return CVAR_NONE;   // the game's newer value, or nothing to undo
  *write = h->saved;
  return CVAR_RESTORE;
}

// A registered variable: its storage and what the registry says of it. Game thread.
struct CvarRef {
  volatile long* value;   // the variable's dword (UBOOL/INT/FLOAT), static storage of the module that registered it
  unsigned flags;         // FExecVariable +8: bit 0 = written by SaveConfigVariables at exit
  int isFloat;            // registered as L"FLOAT": the dword holds float bits
  int ok;
};

// Finds `name` (case-insensitive, as the console) once and logs name, type, flags and value. Refused (ok 0, the
// reason in *why) unless the type is UBOOL, INT or FLOAT, flags bit 0 is clear (never saved) and the storage is inside a
// loaded module's image.
CvarRef cvar_find(const wchar_t* name, const char** why);
// The whole registry, once a session: every entry's name, type, flags and value (a dword or float when readable).
void cvars_log_all(void);
// The raw dword (a FLOAT's bits); cvar_get_float reads a FLOAT as a float.
inline int cvar_get(const CvarRef& c) { return c.ok ? (int)*c.value : 0; }
inline float cvar_get_float(const CvarRef& c) { return cvar_float(cvar_get(c)); }
// An interlocked dword write: the render job reads these once per pass.
void cvar_set(const CvarRef& c, int v);
inline void cvar_set_float(const CvarRef& c, float v) { cvar_set(c, cvar_bits(v)); }

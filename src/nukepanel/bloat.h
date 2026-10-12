#pragma once
// The Expander (DLC03; docs/re/aim.md section 13). Its blast deals ExpanderDamage, whose ReduceDamage returns 0 outside
// multiplayer: no Health is lost, the pawn bloats instead (StartBloat), and the pop that kills it is the pawn's own
// TakeDamage of 9999. Pure rules; spawnbox.cpp reads the pawns, overkill.cpp counts the blasts in flight.
#include <wchar.h>

// The held gun deals a bloat type: ExpanderDamage or a subclass (its class chain, most derived first).
inline int bloat_type(const wchar_t* const* names, int n) {
  for (int i = 0; names && i < n; i++)
    if (names[i] && _wcsicmp(names[i], L"ExpanderDamage") == 0) return 1;
  return 0;
}
// Pawn.CanBloat() = bCanBloat && !bOnSteroids (EDF_Grunt_DLC03 and GeneralGraves_DLC03 clear bCanBloat). -1 unread:
// assumed it can (a shot is never held back on an unread flag).
inline int bloat_can(int canBloat, int onSteroids) { return canBloat != 0 && onSteroids != 1; }

// One more Expander hit on a pawn. state: Pawn.BloatState (0 none, 1 growing, 2 full, 3 shrinking back; -1 unread);
// second: the class's SecondBloat (-1: none); inFlight: the player's blasts on their way. A SecondBloat class pops on
// its second hit; any other is done after one (it pops when full, or only bloats); a further hit restarts the bloat.
inline int bloat_steps(int state, int second) {
  return second == 1 ? 2 : state > 0 && (second >= 0 || state != 3) ? 1 : 0;
}
enum { BL_SHOOT, BL_IMMUNE, BL_DONE };
inline int bloat_verdict(int can, int state, int second, int inFlight) {
  if (!can) return BL_IMMUNE;
  return bloat_steps(state, second) + (inFlight > 0 ? inFlight : 0) >= (second >= 0 ? 2 : 1) ? BL_DONE : BL_SHOOT;
}

// The pawns' bloat reads, by pawn; an entry not read again within BLOAT_MS no longer counts.
enum { BLOAT_SLOTS = 32 };
const unsigned BLOAT_MS = 1000;
struct BloatEntry { const void* pawn; unsigned stamp; int can, state, second; };
struct BloatTable { BloatEntry e[BLOAT_SLOTS]; int n; };
// In place for a known pawn, else a free slot, else the oldest. 1 when the pawn is new or a value changed (logged).
inline int bloat_note(BloatTable* t, const void* pawn, unsigned now, int can, int state, int second) {
  if (!pawn) return 0;
  int at = -1;
  for (int i = 0; i < t->n && at < 0; i++)
    if (t->e[i].pawn == pawn) at = i;
  int changed = 1;
  if (at >= 0) {
    const BloatEntry& e = t->e[at];
    changed = e.can != can || e.state != state || e.second != second;
  } else if (t->n < BLOAT_SLOTS) {
    at = t->n++;
  } else {
    at = 0;
    for (int i = 1; i < t->n; i++)
      if (now - t->e[i].stamp > now - t->e[at].stamp) at = i;   // unsigned ages: safe across the wrap
  }
  const BloatEntry e = { pawn, now, can, state, second };
  t->e[at] = e;
  return changed;
}
inline int bloat_get(const BloatTable* t, const void* pawn, unsigned now, int* can, int* state, int* second) {
  for (int i = 0; pawn && i < t->n; i++) {
    if (t->e[i].pawn != pawn) continue;
    if (now - t->e[i].stamp >= BLOAT_MS) return 0;
    *can = t->e[i].can;
    *state = t->e[i].state;
    *second = t->e[i].second;
    return 1;
  }
  return 0;
}
inline const char* bloat_label(int v) {
  return v == BL_IMMUNE ? "cannot bloat" : v == BL_DONE ? "fully expanded or popping" : "a hit bloats or pops it";
}

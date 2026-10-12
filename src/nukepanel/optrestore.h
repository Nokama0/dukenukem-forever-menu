#pragma once
// Pure: the restore bookkeeping the three player options share. On: the game's value is
// remembered at the first apply on this level; off: it is written back only while ours is still the value in place,
// else the game's newer value is left; a new level forgets the record, so the option re-applies fresh.

struct OptRestore { int applied; unsigned level; float saved; float ours; };

inline OptRestore optrestore_init(void) {
  OptRestore r = { 0, 0u, 0.f, 0.f };
  return r;
}

// current: the game's value read just now. Captured unless ours from this level's earlier apply is still in place
// (a slider move re-applies without forgetting what the game had).
inline OptRestore optrestore_apply(OptRestore r, unsigned level, float current, float ours) {
  if (!r.applied || r.level != level || current != r.ours) r.saved = current;
  r.applied = 1;
  r.level = level;
  r.ours = ours;
  return r;
}

// 1 with *back = the remembered value when ours is still in place; 0 when the game changed it since (leave it).
inline int optrestore_release(const OptRestore& r, float current, float* back) {
  if (!r.applied || current != r.ours || !back) return 0;
  *back = r.saved;
  return 1;
}

// A level other than the record's: nothing of the old level may be restored into the new one.
inline OptRestore optrestore_level(OptRestore r, unsigned level) {
  if (r.level != level) {
    r.applied = 0;
    r.level = level;
  }
  return r;
}

// What a frame does: apply while on and ours is not (or no longer) in place, release once when switched off.
enum { OPT_NONE = 0, OPT_APPLY = 1, OPT_RELEASE = 2 };
inline int opt_step(const OptRestore& r, int on, float current, float want) {
  if (on) return (!r.applied || current != r.ours || r.ours != want) ? OPT_APPLY : OPT_NONE;
  return r.applied ? OPT_RELEASE : OPT_NONE;
}

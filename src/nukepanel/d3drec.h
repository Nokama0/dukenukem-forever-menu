#pragma once
// The frame record (freecam.md section 7, the RendMap 6 freeze): one whole frame of the engine's render calls from both
// threads, with each call's caller, recorded by d3dcap.cpp and logged a few lines a Present. Pure: the slots, the log
// order and the arming caps. Measurement only: nothing here changes a call.

// A frame's first DREC_HEAD calls in order, then a ring of its last DREC_RING: the start (Lock) and the end (the final
// writes) are both kept however long the frame is.
enum { DREC_HEAD = 64, DREC_RING = 448, DREC_CAP = DREC_HEAD + DREC_RING, DREC_BT = 4 };
inline int drec_slot(long n) {
  if (n < 0) return -1;
  if (n < DREC_HEAD) return (int)n;
  return DREC_HEAD + (int)((n - DREC_HEAD) % DREC_RING);
}
inline int drec_kept(long total) { return total < DREC_CAP ? (int)total : DREC_CAP; }
// The call number of the i-th kept entry in call order (i < drec_kept(total)), and the slot it sits in.
inline long drec_call_no(int i, long total) {
  if (i < DREC_HEAD || total <= DREC_CAP) return i;
  return total - DREC_RING + (i - DREC_HEAD);
}
inline int drec_order(int i, long total) { return drec_slot(drec_call_no(i, total)); }

// A shader bind is kept only when the pointer changes (each kind against its own last), so the record shows passes,
// not every draw's rebind.
inline int drec_keep_shader(const void* last, const void* now) { return last != now; }

// Who armed a record. Each reason gets DREC_PER_WHY records a session; counts[why] is how many it had.
enum { DREC_WHY_FLIGHT = 0, DREC_WHY_STALL, DREC_WHY_COUNT, DREC_PER_WHY = 3 };
inline int drec_may_arm(int* counts, int why) {
  if (why < 0 || why >= DREC_WHY_COUNT || counts[why] >= DREC_PER_WHY) return 0;
  ++counts[why];
  return 1;
}

enum { DREC_RT = 1, DREC_CLEAR, DREC_STRETCH, DREC_FILL, DREC_TEX0, DREC_VS, DREC_PS };
// a: the surface (SetRenderTarget, ColorFill), the current target 0 at a Clear, StretchRect source, texture or shader.
// b: StretchRect's destination. idx: target index or a Clear's rect count. flags: a Clear's flags. ret: the caller; bt:
// the next callers, own-module frames skipped. game: called on the game thread, else the render job's.
struct DrecEntry { int kind, game; const void* a; const void* b; unsigned idx, flags, ret, btN, bt[DREC_BT]; };

#pragma once
// Pure: a ProcessEvent parms block filled from reflection's (offset, size) pairs (scall.h). A parameter that would
// run past the function's ParmsSize is refused, so a layout that differs from ours never writes outside the block.
#include <string.h>

// ProcessEvent writes up to parmsSize bytes into the block (return values, out parameters): a block of cap bytes takes
// the call only when they fit. A function without parameters needs no block.
inline bool scall_parms_fit(unsigned parmsSize, const void* parms, unsigned cap) {
  return !parmsSize || (parms && parmsSize <= cap);
}

inline int scallparms_put(unsigned char* parms, unsigned parmsSize, unsigned offset, const void* v, unsigned size) {
  if (!parms || !v || !size || offset >= parmsSize || size > parmsSize - offset) return 0;
  memcpy(parms + offset, v, size);
  return 1;
}

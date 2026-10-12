#pragma once
// The white block every flat fill samples (canvas.cpp). font_atlas (byte-identical to DNMP's) is transparent
// there: DNMP's block lived in a separately generated font.png this port does not load (measured 2026-10-04).
#include <string.h>

// Copies src (w x h, 8-bit alpha) to dst, then makes the 4x4 block at (x..x+3, y..y+3) opaque, clipped to the image.
inline void atlas_add_white_block(unsigned char* dst, const unsigned char* src, int w, int h, int x, int y) {
  if (!dst || !src || w <= 0 || h <= 0) return;
  memcpy(dst, src, (size_t)w * (size_t)h);
  for (int yy = y; yy < y + 4; yy++)
    for (int xx = x; xx < x + 4; xx++)
      if (xx >= 0 && xx < w && yy >= 0 && yy < h) dst[yy * w + xx] = 255;
}

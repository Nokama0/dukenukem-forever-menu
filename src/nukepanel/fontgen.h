// Ported from duke3d-eduke32-aa/src/ekmod/ui/fontgen.h (itself Megaton's), 2026-10-01. One change: the atlas
// also carries a 4x4 opaque white block (whiteX/whiteY), because our canvas draws every flat fill from the
// atlas's own white texel through one shader.
#pragma once
// The panel's fonts, rasterized at the applied UI scale. DLL LINE ONLY: this is GDI, so it cannot link into test_host,
// which is why the packing decision lives in fontpack.h (pure, header-only, pinned by the suite). What is left here can
// only be checked in front of a real font: ask Windows for a face at a size and read the pixels back.
#include "font.h"   // Glyph, FONT_COUNT

// One atlas, built for one scale. `pixels` is 8-bit coverage in the same layout
// font_data.cpp's baked `font_atlas` uses, so the canvas uploads it the same way.
struct FontAtlas {
  int w, h;                       // atlas size, one of FONTPACK_LADDER
  unsigned char* pixels;          // w*h alpha bytes, owned (free with fontgen_free)
  Glyph glyphs[FONT_COUNT][96];   // DEVICE pixels: bx/by/w/h/adv at this scale
  int height[FONT_COUNT];         // tmHeight per face, device pixels (for the log)
  int pct;                        // the scale it was built for
  int whiteX, whiteY;             // top-left of the 4x4 white block, packed after the last glyph
};

// Rasterizes the 3 faces at ui_scale_px(base px, pct). ALL OR NOTHING: any face unavailable or unpackable returns
// false, `*out` untouched, one warning, and the caller keeps the baked atlas (a partial one shows blank rectangles).
// False for pct == UI_SCALE_DEFAULT. `out` must be ZEROED OR PREVIOUSLY BUILT: success frees its old content.
bool fontgen_build(int pct, FontAtlas* out);

// Frees `pixels` and zeroes the struct. Safe on a zeroed struct, so a caller
// can free unconditionally before every rebuild.
void fontgen_free(FontAtlas* a);

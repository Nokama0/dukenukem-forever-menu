#pragma once
// Free camera > Photo: an N x N grid of screen-sized tiles stitched into one PNG (docs/re/freecam.md section 5).
// DNMP's photo.h with the engine's own tile lever: the MegaShot cvars, read by FSceneNode::UpdateTransforms as an
// off-axis sub-frustum at the unchanged FOV, so no FOV narrowing or view shear. Pure; the glue is photo.cpp.
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { PHOTO_MULT_MIN = 2, PHOTO_MULT_MAX = 16, PHOTO_MULT_DEF = 4 };
// Frames: settled frozen before the first tile; arming given up; one tile never read.
enum { PHOTO_SETTLE_FRAMES = 2, PHOTO_ARM_TIMEOUT = 120, PHOTO_TILE_TIMEOUT = 30 };
// Frames a tile's cvars are in place before its back buffer is read: the render may run a frame behind the
// Present our frame path sits in (the engine has a render thread manager), so one frame of margin and one more.
enum { PHOTO_HOLD_FRAMES = 3 };

inline int photo_mult_clamped(int m) { return m < PHOTO_MULT_MIN ? PHOTO_MULT_MIN : m > PHOTO_MULT_MAX ? PHOTO_MULT_MAX : m; }

// The engine's own loop (dnRenderThreadMgr::TakeMegaShot 0x10115910, read 2026-10-07): rows outer from YShift 1-N
// by +2, written top first into a top-down BMP; columns inner from XShift N-1 by -2, written left to right.
inline float photo_shift_x(int n, int c) { return (float)(n - 1 - 2 * c); }
inline float photo_shift_y(int n, int r) { return (float)(1 - n + 2 * r); }

// Each tile is rendered PHOTO_MARGIN px wider than it keeps on every side and cropped: the bottom row and right column
// of a frame come out dark in lit areas (measured 2026-10-08 on a 4x photo: 22 against 240) and edge-clamped screen
// filters differ there. The photo is the screen's view less the margin: n (W - 2M) x n (H - 2M).
enum { PHOTO_MARGIN = 16 };
inline int photo_margin(int s) { return s >= 8 * PHOTO_MARGIN ? PHOTO_MARGIN : 0; }
inline int photo_kept(int s) { return s - 2 * photo_margin(s); }
inline float photo_keep_ratio(int s) { return s > 0 ? (float)photo_kept(s) / (float)s : 1.f; }
// The crop's first byte in a locked tile: the margin's rows and columns, then row0 rows into the kept part.
inline size_t photo_tile_src_offset(unsigned pitch, int mx, int my, int row0) {
  return (size_t)(my + row0) * pitch + (size_t)mx * 4u;
}

// A 24-bit row padded to 4 bytes (the strips WIC encodes).
inline unsigned photo_stride(int w) { return ((unsigned)w * 3u + 3u) & ~3u; }

// One tile (BGRA rows, top first: a locked X8R8G8B8 surface) into tile column c of a strip of 24bppBGR rows. True
// when any pixel is not black.
inline bool photo_copy_bgra(const unsigned char* src, unsigned pitch, int w, int h, unsigned char* strip,
                            unsigned stripStride, int c) {
  unsigned any = 0;
  for (int y = 0; y < h; ++y) {
    const unsigned char* s = src + (size_t)y * pitch;
    unsigned char* d = strip + (size_t)y * stripStride + (size_t)c * (size_t)w * 3u;
    for (int x = 0; x < w; ++x, s += 4, d += 3) {
      d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
      any |= (unsigned)s[0] | s[1] | s[2];
    }
  }
  return any != 0;
}

// --- A strip in chunks (DNMP, 2026-10-03: a fragmented 32-bit address space refused one big block) --------------
const unsigned PHOTO_CHUNK_BYTES = 16u << 20;
enum { PHOTO_CHUNKS_MAX = 64 };

inline int photo_chunk_rows(unsigned stride, int h) {
  const int byBytes = stride ? (int)(PHOTO_CHUNK_BYTES / stride) : h;
  const int byCount = (h + PHOTO_CHUNKS_MAX - 1) / PHOTO_CHUNKS_MAX;
  int r = byBytes < byCount ? byCount : byBytes;
  if (r > h) r = h;
  return r < 1 ? 1 : r;
}
inline int photo_chunk_count(int h, int rowsPer) { return h > 0 && rowsPer > 0 ? (h + rowsPer - 1) / rowsPer : 0; }
inline int photo_chunk_rows_in(int k, int h, int rowsPer) {
  const int left = h - k * rowsPer;
  return left < 0 ? 0 : left < rowsPer ? left : rowsPer;
}
// photo_copy_bgra into a chunked strip: rows 0..h-1 of the tile go to rows 0..h-1 of the strip's chunks.
inline bool photo_copy_bgra_chunks(const unsigned char* src, unsigned pitch, int w, int h, unsigned char* const* chunks,
                                   int rowsPer, unsigned stripStride, int c) {
  bool any = false;
  for (int k = 0; k < photo_chunk_count(h, rowsPer); ++k)
    any |= photo_copy_bgra(src + (size_t)k * (size_t)rowsPer * pitch, pitch, w, photo_chunk_rows_in(k, h, rowsPer),
                           chunks[k], stripStride, c);
  return any;
}

// The strips in flight (two: one filling, one encoding) stay within this in a 32-bit process that is already large
// One whole tile row as a strip allows 9x at 3440x1440 (photo_budget_ok).
const unsigned long long PHOTO_BUDGET_BYTES = 256ull << 20;
inline bool photo_budget_ok(int W, int H, int n) {
  return W > 0 && H > 0 && 2ull * photo_stride(W * n) * (unsigned long long)H <= PHOTO_BUDGET_BYTES;
}
// Past that a tile row is captured in bands (16x): its tiles are rendered once per band and each
// render keeps that band's rows, so a strip is n kw wide and kh / bands tall. Bands cost renders, not memory.
enum { PHOTO_BANDS_MAX = 16 };
inline int photo_band_rows(int kh, int bands) { return bands > 0 ? (kh + bands - 1) / bands : kh; }
// The fewest bands whose two strips fit the budget, 0 when not even PHOTO_BANDS_MAX do. kw, kh: a tile's kept size.
inline int photo_bands(int kw, int kh, int n) {
  if (kw <= 0 || kh <= 0 || n <= 0) return 0;
  for (int b = 1; b <= PHOTO_BANDS_MAX; ++b)
    if (2ull * photo_stride(kw * n) * (unsigned long long)photo_band_rows(kh, b) <= PHOTO_BUDGET_BYTES) return b;
  return 0;
}
// The largest multiplier the screen allows, 0 when not even the smallest fits; no cap before the size is known.
inline int photo_mult_max(int W, int H) {
  if (W <= 0 || H <= 0) return PHOTO_MULT_MAX;
  for (int n = PHOTO_MULT_MAX; n >= PHOTO_MULT_MIN; --n)
    if (photo_bands(photo_kept(W), photo_kept(H), n)) return n;
  return 0;
}
// The Photo size row's reason when the chosen size is past the screen's maximum.
inline void photo_too_large_text(int maxN, char* out, int cap) {
  if (!out || cap <= 0) return;
  if (maxN >= PHOTO_MULT_MIN) _snprintf_s(out, (size_t)cap, _TRUNCATE, "Too large for this screen: max %dx.", maxN);
  else _snprintf_s(out, (size_t)cap, _TRUNCATE, "Too large for this screen: even %dx does not fit.", PHOTO_MULT_MIN);
}

// "<level> <yyyy-mm-dd hh-mm-ss> <W>x<H>.png" (DNMP). Characters Windows refuses become '_'; no level gives "level".
// -1 and an empty string when it does not fit: never a cut name.
inline int photo_file_name(char* out, int cap, const char* level, int y, int mo, int d, int h, int mi, int s, int W,
                           int H) {
  if (!out || cap <= 0) return -1;
  char lv[64];
  int n = 0;
  for (const char* p = level; p && *p && n < (int)sizeof lv - 1; ++p) {
    const unsigned char ch = (unsigned char)*p;
    lv[n++] = (ch < 0x20 || strchr("<>:\"/\\|?*", ch)) ? '_' : (char)ch;
  }
  lv[n] = 0;
  const int len = _snprintf_s(out, (size_t)cap, _TRUNCATE, "%s %04d-%02d-%02d %02d-%02d-%02d %dx%d.png",
                              n ? lv : "level", y, mo, d, h, mi, s, W, H);
  if (len < 0) { out[0] = 0; return -1; }
  return len;
}

// A photo never overwrites a file: name as it is (n <= 1), else "-n" before its ".png" ("... 7680x4320-2.png"), tried
// up to PHOTO_NAME_TRIES. -1 and "" when it does not fit or name is not a .png: never a cut name.
enum { PHOTO_NAME_TRIES = 9 };
inline int photo_file_name_n(const char* name, int n, char* out, int cap) {
  if (!out || cap <= 0) return -1;
  out[0] = 0;
  const int len = name ? (int)strlen(name) : 0;
  if (len < 4 || _stricmp(name + len - 4, ".png") != 0) return -1;
  const int w = n <= 1 ? _snprintf_s(out, (size_t)cap, _TRUNCATE, "%s", name)
                       : _snprintf_s(out, (size_t)cap, _TRUNCATE, "%.*s-%d.png", len - 4, name, n);
  if (w < 0) { out[0] = 0; return -1; }
  return w;
}

// "4x 7680x4320": the multiplier and the photo's pixel size.
inline void photo_size_text(int mult, int W, int H, char* out, int cap) {
  if (!out || cap <= 0) return;
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "%dx %dx%d", mult, mult * W, mult * H);
}

// The capture, one step a frame: ARMING waits until the world has stayed frozen PHOTO_SETTLE_FRAMES frames. TILE (r, c)
// of band b is up until its back buffer was read. After a band's last tile its strip goes to the encoder (FLUSH) and
// WAIT holds until a strip buffer is free. DRAIN waits for the encoder.
enum { PH_IDLE = 0, PH_ARMING, PH_TILE, PH_WAIT, PH_DRAIN };
enum { PA_NONE = 0, PA_START, PA_FLUSH, PA_FINISH, PA_ABORT };
enum { PHR_NONE = 0, PHR_CANCELLED, PHR_CAMERA, PHR_PANEL, PHR_LEVEL, PHR_FOCUS, PHR_SIZE, PHR_NOT_FROZEN,
       PHR_THAWED, PHR_NO_TILE, PHR_ENCODER, PHR_START, PHR_DEVICE, PHR_TOO_LARGE };
struct PhotoSm { int phase, n, r, c, arm, settled, miss, flushRow, reason, bands, b; };
// press: Photo key presses this frame. cam: flying and seeded. world: the level the capture began in. sameSize: the
// back buffer it began at. frozen: our pause holds. tileRead: tile (r, c) was read this frame. bufferFree: a strip
// buffer for the next row.
struct PhotoIn { int press, cam, panel, world, focus, sameSize, frozen, tileRead, bufferFree, encFailed, encDone; };

// bands: photo_bands for the capture, set by the glue at the start; 1 until then.
inline PhotoSm photo_begin(int n) {
  PhotoSm s;
  memset(&s, 0, sizeof s);
  s.n = n;
  s.flushRow = -1;
  s.bands = 1;
  return s;
}

inline int photo_abort(PhotoSm* s, int why) {
  s->phase = PH_IDLE;
  s->reason = why;
  return PA_ABORT;
}

inline int photo_step(PhotoSm* s, const PhotoIn& in) {
  s->flushRow = -1;
  if (s->phase == PH_IDLE) {
    if (!in.press || !in.cam) return PA_NONE;
    s->phase = PH_ARMING;
    s->r = s->c = s->b = s->arm = s->settled = s->miss = 0;
    s->reason = PHR_NONE;
    return PA_START;
  }
  if (in.encFailed) return photo_abort(s, PHR_ENCODER);
  if (s->phase == PH_DRAIN) {
    if (!in.encDone) return PA_NONE;
    s->phase = PH_IDLE;
    return PA_FINISH;
  }
  if (in.press) return photo_abort(s, PHR_CANCELLED);
  if (!in.cam) return photo_abort(s, PHR_CAMERA);
  if (in.panel) return photo_abort(s, PHR_PANEL);
  if (!in.world) return photo_abort(s, PHR_LEVEL);
  if (!in.focus) return photo_abort(s, PHR_FOCUS);
  if (!in.sameSize) return photo_abort(s, PHR_SIZE);
  if (s->phase == PH_ARMING) {
    s->arm++;
    s->settled = in.frozen ? s->settled + 1 : 0;
    if (s->settled >= PHOTO_SETTLE_FRAMES) {
      s->phase = PH_TILE;
      s->r = s->c = s->b = s->miss = 0;
      return PA_NONE;
    }
    return s->arm >= PHOTO_ARM_TIMEOUT ? photo_abort(s, PHR_NOT_FROZEN) : PA_NONE;
  }
  if (!in.frozen) return photo_abort(s, PHR_THAWED);
  if (s->phase == PH_WAIT) {
    if (in.bufferFree) s->phase = PH_TILE;
    return PA_NONE;
  }
  // PH_TILE
  if (!in.tileRead) return ++s->miss >= PHOTO_TILE_TIMEOUT ? photo_abort(s, PHR_NO_TILE) : PA_NONE;
  s->miss = 0;
  if (++s->c < s->n) return PA_NONE;
  const int bands = s->bands < 1 ? 1 : s->bands;
  s->flushRow = s->r * bands + s->b;
  s->c = 0;
  if (++s->b >= bands) {
    s->b = 0;
    s->r++;
  }
  s->phase = s->r >= s->n ? PH_DRAIN : PH_WAIT;
  return PA_FLUSH;
}

// Renders done and in all: every band renders the row's tiles again.
inline int photo_tiles_done(const PhotoSm& s) { return (s.r * (s.bands < 1 ? 1 : s.bands) + s.b) * s.n + s.c; }
inline int photo_tiles_total(const PhotoSm& s) { return s.n * s.n * (s.bands < 1 ? 1 : s.bands); }

// The glue could not go on (memory, folder, thread, the device): an abort with its reason. Nothing when idle.
inline int photo_fail(PhotoSm* s, int why) { return s->phase == PH_IDLE ? PA_NONE : photo_abort(s, why); }

// The MegaShot cvars this frame: a tile's scale and shifts while one is up, else the engine's own 1, 0, 0.
// MegaShotScale != 1 makes Post_HDRFinalTonemap skip the adaptation, so exposure stays frozen across tiles (freecam.md
// 5); scale 1 while arming keeps it adapting, and between rows (WAIT) the scale holds with the next tile's shifts.
inline void photo_megashot(const PhotoSm& s, int* scale, float* x, float* y) {
  const int up = s.phase == PH_TILE || s.phase == PH_WAIT;
  *scale = up ? s.n : 1;
  *x = up ? photo_shift_x(s.n, s.c) : 0.f;
  *y = up ? photo_shift_y(s.n, s.r) : 0.f;
}

// With the margin: the engine's shifts times the kept share of the screen (photo.h's test proves the kept pixels abut).
// kx, ky: photo_keep_ratio of the width and height.
inline void photo_megashot_k(const PhotoSm& s, float kx, float ky, int* scale, float* x, float* y) {
  photo_megashot(s, scale, x, y);
  *x *= kx;
  *y *= ky;
}

// Variables TakeMegaShot zeroes, forced 0 from arming to the last tile, and both mirror switches (Engine UBOOLs, flags
// 0, default 1): UpdateTransforms applies MegaShot only if !IsMirror, so a mirror renders the whole view into a tile
// and its reflection jumps at every seam. Off, the surface draws as plain BSP (Vis_ProcessSurfaceOrPortal 0x10322676).
enum { PHOTO_CVAR_COUNT = 6 };
inline const wchar_t* photo_cvar_name(int i) {
  static const wchar_t* const k[PHOTO_CVAR_COUNT] = { L"r_DrawConsole", L"r_AmbientOcclusion", L"r_SkinMeshLOD",
                                                      L"r_Occlude", L"r_DrawBSPMirrors", L"r_DrawActorMirrors" };
  return k[i < 0 ? 0 : i >= PHOTO_CVAR_COUNT ? PHOTO_CVAR_COUNT - 1 : i];
}
inline int photo_cvar_forced(int phase) { return phase == PH_ARMING || phase == PH_TILE || phase == PH_WAIT; }

inline const char* photo_why_text(int why) {
  switch (why) {
    case PHR_CANCELLED: return "the Photo key again (cancelled)";
    case PHR_CAMERA: return "the camera ended";
    case PHR_PANEL: return "the panel opened";
    case PHR_LEVEL: return "the level changed";
    case PHR_FOCUS: return "the game window lost focus";
    case PHR_SIZE: return "the screen size changed";
    case PHR_NOT_FROZEN: return "the world did not freeze (a level that refuses the pause)";
    case PHR_THAWED: return "the world woke mid-capture";
    case PHR_NO_TILE: return "the back buffer could not be read";
    case PHR_ENCODER: return "the PNG encoder failed (see its line above)";
    case PHR_START: return "it could not start (see the line above)";
    case PHR_DEVICE: return "the device was reset or replaced";
    case PHR_TOO_LARGE: return "the size is past the 256 MB strip budget for this screen (nothing allocated)";
    default: return "none";
  }
}

// --- The glue (photo.cpp), game thread unless said ---------------------------------------------------------------
struct IDirect3DDevice9;
// Present, before anything of ours draws: reads the back buffer into the strip when a tile has been held long enough.
void photo_readback(IDirect3DDevice9* dev, int W, int H);
// Frame path, after freecam_frame: the Photo key, the capture's step, the cvars and the encoder.
void photo_frame(int W, int H);
// The progress line, after our other overlays (never in a read back buffer: the read comes first).
void photo_draw(int W, int H);
// A Reset or a new device: our D3D objects go, and a capture under way is aborted.
void photo_device_lost(void);
// A capture is under way (arming to drain): the world stays frozen.
bool photo_busy(void);
// Arming to the last tile: the camera holds still and its other keys do nothing.
bool photo_capturing(void);
// The back buffer size the frame path saw last (the Photo size readout), 0 x 0 before it is known.
void photo_screen_size(int* W, int* H);
// Null while usable; else why the photo is unavailable.
const char* photo_refused(void);

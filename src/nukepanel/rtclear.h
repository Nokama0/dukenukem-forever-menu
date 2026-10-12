#pragma once
// Free camera smear fix: the engine never colour-clears its G-buffer or HDR scene target, so last frame's values stay
// where nothing draws. In flight each screen-sized target but the back buffer is filled with 0 at its first bind of a
// frame, except the motion-blur feedback target (rtc_set_kept), one sampled before its bind, one the engine cleared.

enum { RTC_MAX = 16 };
enum { RTC_FILL = 1, RTC_LEARN, RTC_SKIP_SIZE, RTC_SKIP_BOUND, RTC_SKIP_HISTORY, RTC_SKIP_ENGINE, RTC_SKIP_FULL,
       RTC_SKIP_KEPT };

// verdict: the last FILL / HISTORY / ENGINE decision for the target; reported: the one already logged (d3dcap.cpp).
struct RtcSurf { const void* surf; const void* tex; int lastBind, sampled, engineCleared, history, verdict, reported;
                 unsigned w, h, fmt; };
// kept: the motion-blur feedback target's surface, never filled (0 while unknown).
struct RtcState { RtcSurf s[RTC_MAX]; int n, frame, w, h; const void* bb; const void* kept; };

inline void rtc_init(RtcState* st, const void* bb, int w, int h) {
  st->n = 0;
  st->frame = 0;
  st->bb = bb;
  st->w = w;
  st->h = h;
  st->kept = 0;
}

inline void rtc_set_kept(RtcState* st, const void* surf) { st->kept = surf; }

// The colour formats ColorFill takes and this engine was seen to use or may: A8R8G8B8, X8R8G8B8, A2B10G10R10,
// A2R10G10B10, A16B16G16R16, G16R16F, A16B16G16R16F, R32F, G32R32F. Never a depth or FOURCC format.
inline bool rtc_format_ok(unsigned fmt) {
  switch (fmt) {
    case 21: case 22: case 31: case 35: case 36: case 112: case 113: case 114: case 115: return true;
    default: return false;
  }
}

// D3DDrv9's resource hash bucket (FUN_10020D80, read 2026-10-07): 4096 chains at FD3DDevice +0x1A5D8, keyed by a
// texture's 64-bit cache id (low dword here) and its resource flags.
inline unsigned d3dres_bucket(unsigned lo) { return (((lo >> 4) & 0xFF0u) + ((lo >> 16) & 0xFu)) ^ (lo & 0xFFFu); }

// A Present: a new frame.
inline void rtc_frame(RtcState* st) { st->frame++; }

inline RtcSurf* rtc_find(RtcState* st, const void* surf) {
  for (int i = 0; i < st->n; ++i)
    if (st->s[i].surf == surf) return &st->s[i];
  return 0;
}

// SetRenderTarget(any index, surf): what to do with surf now. tex: its container texture, or null.
inline int rtc_bind(RtcState* st, const void* surf, int w, int h, const void* tex) {
  if (!surf || surf == st->bb || w != st->w || h != st->h) return RTC_SKIP_SIZE;
  if (surf == st->kept) return RTC_SKIP_KEPT;
  RtcSurf* e = rtc_find(st, surf);
  if (!e) {
    if (st->n >= RTC_MAX) return RTC_SKIP_FULL;
    e = &st->s[st->n++];
    e->surf = surf;
    e->tex = tex;
    e->lastBind = st->frame;
    e->sampled = e->engineCleared = -10;
    e->history = 0;
    e->verdict = e->reported = RTC_LEARN;
    e->w = (unsigned)w;
    e->h = (unsigned)h;
    e->fmt = 0;
    return RTC_LEARN;
  }
  if (e->lastBind == st->frame) return RTC_SKIP_BOUND;
  e->lastBind = st->frame;
  if (!e->tex) e->tex = tex;
  if (e->sampled == st->frame) e->history = 1;
  const int v = e->history ? RTC_SKIP_HISTORY : e->engineCleared == st->frame - 1 ? RTC_SKIP_ENGINE : RTC_FILL;
  e->verdict = v;
  return v;
}

// SetTexture(tex): a known target read before its first bind of this frame holds last frame's data.
inline void rtc_sampled(RtcState* st, const void* tex) {
  if (!tex) return;
  for (int i = 0; i < st->n; ++i)
    if (st->s[i].tex == tex && st->s[i].lastBind != st->frame) st->s[i].sampled = st->frame;
}

// A Clear with D3DCLEAR_TARGET while surf is render target 0.
inline void rtc_engine_clear(RtcState* st, const void* surf) {
  RtcSurf* e = surf ? rtc_find(st, surf) : 0;
  if (e) e->engineCleared = st->frame;
}

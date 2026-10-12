#pragma once
// One frame's render-target and clear sequence (the free camera's smear, freecam.md section 1d), recorded by
// d3dcap.cpp's d3d9 detours between two Presents and logged by freecam.cpp. Pure: the arming steps, the bound and
// the format names. Measurement only: nothing here changes a call.

// IDLE -> ARMED (the game thread asks) -> RECORDING (from the end of the next Present) -> READY (at the next
// Present's entry, before anything of ours draws) -> IDLE (logged).
enum { DTR_IDLE = 0, DTR_ARMED, DTR_RECORDING, DTR_READY };
enum { DTR_EV_ARM = 0, DTR_EV_PRESENT_ENTRY, DTR_EV_PRESENT_DONE, DTR_EV_TAKEN };
inline int dtr_step(int s, int ev) {
  switch (ev) {
    case DTR_EV_ARM: return s == DTR_IDLE ? DTR_ARMED : s;
    case DTR_EV_PRESENT_ENTRY: return s == DTR_RECORDING ? DTR_READY : s;
    case DTR_EV_PRESENT_DONE: return s == DTR_ARMED ? DTR_RECORDING : s;
    case DTR_EV_TAKEN: return s == DTR_READY ? DTR_IDLE : s;
    default: return s;
  }
}

// At most this many calls a frame; the rest are counted, not kept.
enum { DTR_CAP = 64 };
inline int dtr_slot(long n) { return n >= 0 && n < DTR_CAP ? (int)n : -1; }

enum { DTR_RT = 1, DTR_CLEAR, DTR_STRETCH, DTR_FILL };
// a: the surface (SetRenderTarget, ColorFill), the render target current at a Clear, StretchRect's source.
// b: StretchRect's destination. w, h, fmt: a's description (a Clear's: its current target's). idx: the render-target
// index, or a Clear's rect count. flags, colour: a Clear's and a ColorFill's.
struct DtrEntry { int kind; const void* a; const void* b; unsigned w, h, fmt, flags, colour, idx; };

// The D3DFORMAT values the engine is likely to use, by name; "" for anything else (logged as a number).
inline const char* dtr_fmt_name(unsigned f) {
  switch (f) {
    case 20: return "R8G8B8";
    case 21: return "A8R8G8B8";
    case 22: return "X8R8G8B8";
    case 23: return "R5G6B5";
    case 31: return "A2B10G10R10";
    case 35: return "A2R10G10B10";
    case 36: return "A16B16G16R16";
    case 75: return "D24S8";
    case 77: return "D24X8";
    case 111: return "R16F";
    case 112: return "G16R16F";
    case 113: return "A16B16G16R16F";
    case 114: return "R32F";
    case 115: return "G32R32F";
    case 116: return "A32B32G32R32F";
    case 0x5A544E49u: return "INTZ";
    default: return "";
  }
}

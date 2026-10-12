#pragma once
// The render watch's pure rules (d3dcap.cpp): whether a frame was drawn by the engine, when a run of undrawn frames
// gets its render thread sampled, and how GetExitCodeThread reads. Measurement only (freecam.md section 7).

// A SetRenderTarget that says the engine drew its frame: target 0 set to the back buffer outside our own Present-time
// drawing. prim_d3d9 binds the back buffer and restores the game's target every frame, which kept the first stuck net
// from ever tripping.
inline int rst_engine_bb_bind(unsigned index, int isBackBuffer, int ours) { return index == 0 && isBackBuffer && !ours; }

// Frames without an engine back-buffer bind while Present goes on. At RST_FIRST the run is a stall and the render
// thread is sampled; at RST_SECOND once more (a hang moves or it does not). The samples are capped for the session;
// the lines are not.
enum { RST_FIRST = 30, RST_SECOND = 300, RST_MAX_SAMPLES = 8 };
enum { RST_NONE, RST_STALLED, RST_STILL, RST_BACK };
struct RstWatch { int frames, samples, episodes, lastRun; };

// One Present. rendered: 1 the engine bound the back buffer since the last one, 0 not, -1 unknown (no detour), which
// counts nothing. *sample: take a stack sample now. lastRun: the run's length when RST_BACK ends it.
inline int rst_step(RstWatch* w, int rendered, int* sample) {
  *sample = 0;
  if (rendered < 0) return RST_NONE;
  if (rendered) {
    const int run = w->frames;
    w->frames = 0;
    if (run < RST_FIRST) return RST_NONE;
    w->lastRun = run;
    return RST_BACK;
  }
  ++w->frames;
  if (w->frames != RST_FIRST && w->frames != RST_SECOND) return RST_NONE;
  if (w->frames == RST_FIRST) ++w->episodes;
  if (w->samples < RST_MAX_SAMPLES) { ++w->samples; *sample = 1; }
  return w->frames == RST_FIRST ? RST_STALLED : RST_STILL;
}

// The RendMap 6 latch (freecam.md 7). D3DDrv9's Lock takes render target 0 as the next frame's main target, and only
// the world's draw or the tonemap switches back, so an unlit frame with no BSP surface ends on a G-buffer target and
// every later frame renders into it. At Present, target 0 goes back to the back buffer if the engine left it elsewhere.
inline int rst_heal_rt(int haveRt, int rtIsBackBuffer, int haveBackBuffer) {
  return haveRt && haveBackBuffer && !rtIsBackBuffer;
}
// The net's input: frames whose first Clear after Present (Lock's) ran, and those of them not on the back buffer.
// 1 every Lock cleared the back buffer; 0 a Lock cleared another target, or no Lock ran.
inline int rst_lock_verdict(long frames, long misses) { return frames > 0 && !misses ? 1 : 0; }

// GetExitCodeThread's answer: ok its return, code its exit code (STILL_ACTIVE 259 while the thread runs).
inline const char* rst_thread_state(int ok, unsigned long code) {
  if (!ok) return "unknown";
  return code == 259ul ? "running" : "exited";
}

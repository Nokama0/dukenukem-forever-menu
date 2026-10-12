#pragma once
// D4: device capture through the COM path. Direct3DCreate9 -> IDirect3D9::CreateDevice body ->
// the device's Present and Reset bodies. Code addresses are read from the vtables; none is written.
struct IDirect3DDevice9;
typedef struct HWND__* HWND;

// From np_first_call (outside the loader lock): loads System32 d3d9.dll and detours Direct3DCreate9.
void d3dcap_install(void);

// The last device created successfully (NULL before), and its focus window.
IDirect3DDevice9* d3dcap_device(void);
HWND d3dcap_window(void);
// Thread that ran the successful CreateDevice (0 before).
unsigned long d3dcap_create_tid(void);
// True unless the last present parameters asked for D3DPRESENT_INTERVAL_IMMEDIATE (true before any device).
bool d3dcap_vsync_on(void);
// The free camera's frame clear (freecam.h freecam_clear_flags): on while flying. Game thread sets it; the Clear
// detour reads it on whichever thread renders.
void d3dcap_set_flight_clear(int on);
// Since the last call: frames whose first Clear came while flying, those we gave the target, those the engine already
// cleared (the camera outside the world), and the last first Clear's flags. hooked: the Clear detour is in place.
void d3dcap_clear_take(unsigned* firsts, unsigned* added, unsigned* engineTarget, unsigned* lastFlags, int* hooked);
// The frame trace (d3dtrace.h): the game thread arms it; it records one whole frame, Present to Present, and is
// then ready. take: true with the entries, the count kept, the calls dropped past the cap, the back buffer and the
// render target 0 current when it began; done() rearms nothing, it only frees the slot for the next arm.
void d3dcap_trace_arm(void);
struct DtrEntry;
bool d3dcap_trace_take(const DtrEntry** e, int* n, int* dropped, const void** bb, const void** rt0, unsigned* bbW,
                       unsigned* bbH, unsigned* bbFmt);
void d3dcap_trace_done(void);
// The detours the trace needs (SetRenderTarget, StretchRect, ColorFill) are installed.
int d3dcap_trace_hooked(void);
// The smear fix (rtclear.h), on with d3dcap_set_flight_clear. Since last call: frames, targets filled, fills refused,
// targets kept as history, binds left to the engine's clear, microseconds filling (CPU time), whether its detours are
// all in. full: binds past the table's 16; kept: binds of the feedback target; format: binds of a format not filled.
struct RtcStats { unsigned frames, fills, fillFails, history, engine, learned, fillUs, full, kept, format; int hooked; };
void d3dcap_rtclear_take(RtcStats* out);
// The motion-blur feedback target's D3D surface (freecam.cpp, read through D3DDrv9's resource hash), never filled;
// null while unknown.
void d3dcap_rtclear_keep(const void* surf);
// Game thread at Present: logs each target whose verdict changed (capped).
void d3dcap_rtclear_report(void);
// Since the last call: 1 every frame's Lock cleared the back buffer, 0 a Lock cleared another target or none ran, -1
// unknown (Clear not hooked, no Present yet). Game thread; the stuck-frame net's input (rstall.h rst_lock_verdict).
int d3dcap_lock_on_bb_take(void);
// Since the last call: frames whose render target 0 was set back to the back buffer at Present, and refusals.
void d3dcap_heal_take(unsigned* heals, unsigned* fails);
// The thread other than the game thread that the engine's render calls come from (the render job), 0 before one is
// seen. Interlocked; crashlog tags that thread's exceptions.
unsigned long d3dcap_render_tid(void);
// Game thread: records the next whole frame (d3drec.h) as a flight's first frame; 0 when busy or capped.
int d3dcap_record_flight(void);
// Game thread: one line of the render watch's state (the run of undrawn frames), e.g. at a flight's end.
void d3dcap_watch_log(const char* when);
// True while the device runs windowed (Borderless window included), as its last successful CreateDevice or Reset
// asked; false in exclusive fullscreen and before any device.
bool d3dcap_windowed(void);

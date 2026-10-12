#pragma once
// Batched 2D triangles on the captured D3D9 device (prim_d3d9.cpp). Present thread only, inside our
// own BeginScene/EndScene, between prim_begin and prim_end; the game's state is restored at prim_end.
struct IDirect3DDevice9;

// Pixels (origin top-left, y down), texture UV, colour 0xAARRGGBB.
struct PrimVert { float x, y, u, v; unsigned argb; };

// Saves the device state and sets ours for a W x H back buffer on `dev` (the device being presented);
// false (and nothing may be drawn) when a D3D call failed earlier this session.
bool prim_begin(IDirect3DDevice9* dev, int W, int H);
// The same, but a D3D failure from here to prim_end sets *failed (the caller's own flag, logged once) instead of
// disabling every caller's drawing; false when *failed is already set.
bool prim_begin_isolated(IDirect3DDevice9* dev, int W, int H, const char** failed);
// Draws what is queued and restores the game's state.
void prim_end(void);

// The texture the following triangles use (NULL: vertex colour only). A change draws the queued
// run first.
void prim_texture(void* tex);
void prim_tri(const PrimVert* a, const PrimVert* b, const PrimVert* c);

// A8R8G8B8 managed-pool texture, white RGB with alpha = coverage; NULL on failure (logged).
void* prim_make_alpha_texture(IDirect3DDevice9* dev, const unsigned char* alpha, int w, int h);
// Returns the texture's remaining reference count.
unsigned long prim_release_texture(void* tex);

// The state block is created by prim_begin and released by prim_end (it holds references to the game's
// objects); released here too before Reset and when the device is replaced, in case a frame left one.
// Returns its remaining reference count (0 when there was none).
bool prim_has_state_block(void);
unsigned long prim_release_state_block(void);
// A new device gets a fresh chance after a failure on the old one.
void prim_reset_failure(void);
// The D3D call whose failure disabled drawing for the session, or NULL while drawing works.
const char* prim_failed(void);

// Cumulative since load, for the log.
struct PrimStats { unsigned tris, draws, frames; };
PrimStats prim_stats(void);

// Fullbright's brightness lift (fblift.h; docs/re/freecam.md section 7g). At Present, before the photo's read and our
// overlays: the back buffer is copied into a texture (StretchRect) and drawn back over itself through a gamma pixel
// shader, between prim_begin and prim_end, so the state block puts back every state the lift sets. Presenting thread.
#include <windows.h>
#include <d3d9.h>
#include "fblift.h"
#include "prim.h"
#include "config.h"
#include "log.h"

// fxc /T ps_2_0 /E main of: sampler2D s0 : register(s0); float4 c0 : register(c0);
//   float4 main(float2 uv : TEXCOORD0) : COLOR0 { return float4(pow(tex2D(s0, uv).rgb, c0.x), 1); }
// c0.x is fblift_exponent. pow is log, mul, exp per channel; log 0 is -inf, so black stays 0 (fblift_curve).
static const DWORD kPs[] = {
  0xffff0200, 0x0028fffe, 0x42415443, 0x0000001c, 0x00000073, 0xffff0200, 0x00000002, 0x0000001c,
  0x00000100, 0x0000006c, 0x00000044, 0x00000002, 0x00020001, 0x00000048, 0x00000000, 0x00000058,
  0x00000003, 0x00020001, 0x0000005c, 0x00000000, 0xab003063, 0x00030001, 0x00040001, 0x00000001,
  0x00000000, 0xab003073, 0x000c0004, 0x00010001, 0x00000001, 0x00000000, 0x325f7370, 0x4d00305f,
  0x6f726369, 0x74666f73, 0x29522820, 0x534c4820, 0x6853204c, 0x72656461, 0x6d6f4320, 0x656c6970,
  0x30312072, 0xab00312e, 0x05000051, 0xa00f0001, 0x3f800000, 0x00000000, 0x00000000, 0x00000000,
  0x0200001f, 0x80000000, 0xb0030000, 0x0200001f, 0x90000000, 0xa00f0800, 0x03000042, 0x800f0000,
  0xb0e40000, 0xa0e40800, 0x0200000f, 0x80010001, 0x80000000, 0x0200000f, 0x80020001, 0x80550000,
  0x0200000f, 0x80040001, 0x80aa0000, 0x03000005, 0x80070000, 0x80e40001, 0xa0000000, 0x0200000e,
  0x80010001, 0x80000000, 0x0200000e, 0x80020001, 0x80550000, 0x0200000e, 0x80040001, 0x80aa0000,
  0x02000001, 0x80080001, 0xa0000001, 0x02000001, 0x800f0800, 0x80e40001, 0x0000ffff,
};

static IDirect3DDevice9* s_dev;        // the device the objects below belong to
static IDirect3DPixelShader9* s_ps;    // survives a Reset
static IDirect3DTexture9* s_tex;       // default pool: released before every Reset
static UINT s_texW, s_texH;
static D3DFORMAT s_texFmt;
static const char* s_failed;
static unsigned s_frames;
static double s_ms;
static LARGE_INTEGER s_qpf;

static void fail(const char* what, HRESULT hr) {
  if (s_failed) return;
  s_failed = what;
  np_log("fblift: %s failed hr=0x%08lX; the brightness lift is off for the session", what, (unsigned long)hr);
}

static void release_tex(void) {
  if (s_tex) s_tex->Release();
  s_tex = 0;
  s_texW = s_texH = 0;
}

void fblift_device_lost(int replaced) {
  release_tex();
  if (!replaced) return;
  if (s_ps) s_ps->Release();
  s_ps = 0;
  s_dev = 0;
}

// The shader once a device, the copy texture at the back buffer's size and format. False: nothing this frame.
static bool objects(IDirect3DDevice9* dev, const D3DSURFACE_DESC& d) {
  if (dev != s_dev) {
    fblift_device_lost(1);
    s_dev = dev;
  }
  HRESULT hr;
  if (!s_ps) {
    D3DCAPS9 caps;
    if (FAILED(hr = dev->GetDeviceCaps(&caps))) { fail("GetDeviceCaps", hr); return false; }
    if (caps.PixelShaderVersion < D3DPS_VERSION(2, 0)) { fail("pixel shader 2.0 (not offered)", E_FAIL); return false; }
    if (FAILED(hr = dev->CreatePixelShader(kPs, &s_ps)) || !s_ps) {
      s_ps = 0;
      fail("CreatePixelShader", hr);
      return false;
    }
  }
  if (s_tex && s_texW == d.Width && s_texH == d.Height && s_texFmt == d.Format) return true;
  release_tex();
  hr = dev->CreateTexture(d.Width, d.Height, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &s_tex, NULL);
  if (FAILED(hr) || !s_tex) {
    s_tex = 0;
    if (hr != D3DERR_DEVICELOST) fail("CreateTexture (the copy)", hr);   // lost: the Reset comes first
    return false;
  }
  s_texW = d.Width;
  s_texH = d.Height;
  s_texFmt = d.Format;
  np_log("fblift: copy texture %ux%u format %d (back buffer multisample %d)", d.Width, d.Height, (int)d.Format,
         (int)d.MultiSampleType);
  return true;
}

// One full-screen quad of the copy through the shader, in its own scene; prim_end restores the game's state.
static void draw(IDirect3DDevice9* dev, int W, int H) {
  if (FAILED(dev->BeginScene())) return;   // a lost device: the next frame tries again
  if (prim_begin_isolated(dev, W, H, &s_failed)) {   // a failure here turns off the lift, never the panel
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    const float c0[4] = { fblift_exponent(g_cfg.freecamLift), 0.f, 0.f, 0.f };
    HRESULT hr = dev->SetPixelShader(s_ps);
    if (SUCCEEDED(hr)) hr = dev->SetPixelShaderConstantF(0, c0, 1);
    if (SUCCEEDED(hr)) {
      const float w = (float)W, h = (float)H;
      const PrimVert a = { 0.f, 0.f, 0.f, 0.f, 0xFFFFFFFFu }, b = { w, 0.f, 1.f, 0.f, 0xFFFFFFFFu };
      const PrimVert c = { w, h, 1.f, 1.f, 0xFFFFFFFFu }, e = { 0.f, h, 0.f, 1.f, 0xFFFFFFFFu };
      prim_texture(s_tex);
      prim_tri(&a, &b, &c);
      prim_tri(&a, &c, &e);
      ++s_frames;
    } else {
      fail("SetPixelShader", hr);
    }
    prim_end();
  }
  dev->EndScene();
}

void fblift_present(IDirect3DDevice9* dev, int W, int H, int holding) {
  if (!dev || !fblift_wanted(holding, g_cfg.freecamLift, s_failed != 0) || prim_failed()) return;
  LARGE_INTEGER t0, t1;
  if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
  QueryPerformanceCounter(&t0);
  IDirect3DSurface9* bb = 0;
  D3DSURFACE_DESC d;
  HRESULT hr = dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
  if (FAILED(hr) || !bb) {
    fail("GetBackBuffer", hr);
  } else if (FAILED(hr = bb->GetDesc(&d))) {
    fail("GetDesc (the back buffer)", hr);
  } else if ((int)d.Width == W && (int)d.Height == H && objects(dev, d)) {
    IDirect3DSurface9* dst = 0;
    hr = s_tex->GetSurfaceLevel(0, &dst);
    if (SUCCEEDED(hr)) hr = dev->StretchRect(bb, NULL, dst, NULL, D3DTEXF_NONE);
    if (dst) dst->Release();
    if (SUCCEEDED(hr)) draw(dev, W, H);
    else if (hr != D3DERR_DEVICELOST) fail("StretchRect (the copy)", hr);
  }
  if (bb) bb->Release();
  QueryPerformanceCounter(&t1);
  s_ms += (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)s_qpf.QuadPart;
}

void fblift_take(unsigned* frames, double* ms) {
  *frames = s_frames;
  *ms = s_ms;
  s_frames = 0;
  s_ms = 0.0;
}

const char* fblift_refused(void) {
  return s_failed ? "Off for this session: the graphics device refused it (see nukepanel.log)." : 0;
}

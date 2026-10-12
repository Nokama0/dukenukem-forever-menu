// D3D9 backend for prim.h: pre-transformed vertices through DrawPrimitiveUP, the game's state saved
// in a D3DSBT_ALL state block created at prim_begin, applied back and released at prim_end.
#include <windows.h>
#include <d3d9.h>
#include "prim.h"
#include "primbatch.h"
#include "log.h"

struct PrimD3DVert { float x, y, z, rhw; DWORD color; float u, v; };
static const DWORD kFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

enum { PRIM_CAP = 3 * 512 };
static PrimD3DVert s_verts[PRIM_CAP];
static PrimBatch s_batch;
static void* s_tex;                     // current texture for the next triangle
static IDirect3DDevice9* s_dev;         // device of this frame (prim_begin..prim_end)
// Lives only from prim_begin to prim_end: a captured block holds references to the game's bound textures and
// buffers, which a block kept between frames would keep alive while the panel is closed (2026-10-04).
static IDirect3DStateBlock9* s_sb;
static IDirect3DSurface9* s_gameRt;     // the game's render target 0, put back at prim_end
static const char* s_failed;            // the D3D call that failed (draw nothing for the session), or NULL
// prim_begin_isolated's caller's flag, from its prim_begin to its prim_end: a failure there stops only that caller.
static const char** s_sink;
static PrimStats s_stats;

static const char* cur_failed(void) { return s_sink ? *s_sink : s_failed; }

static void prim_fail(const char* what, HRESULT hr) {
  const char** f = s_sink ? s_sink : &s_failed;
  if (*f) return;
  np_log("prim: %s failed hr=0x%08lX; %s", what, (unsigned long)hr,
         s_sink ? "that caller's drawing is off for the session; the panel draws on" : "drawing disabled for the session");
  *f = what;
}

static void prim_set_stage(void* tex) {
  s_dev->SetTexture(0, (IDirect3DBaseTexture9*)tex);
  // No texture samples as opaque black, so an untextured run takes the vertex colour alone.
  const DWORD op = tex ? D3DTOP_MODULATE : D3DTOP_SELECTARG2;
  s_dev->SetTextureStageState(0, D3DTSS_COLOROP, op);
  s_dev->SetTextureStageState(0, D3DTSS_ALPHAOP, op);
}

static void prim_draw(int count, void* tex) {
  if (count < 3) return;
  prim_set_stage(tex);
  const HRESULT hr = s_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(count / 3), s_verts, sizeof(PrimD3DVert));
  if (FAILED(hr)) { prim_fail("DrawPrimitiveUP", hr); return; }
  s_stats.draws++;
}

static bool begin(IDirect3DDevice9* dev, int W, int H) {
  s_dev = 0;
  if (!dev || s_failed || cur_failed() || W <= 0 || H <= 0) return false;
  prim_release_state_block();   // none unless a prim_begin went without its prim_end
  {
    const HRESULT hr = dev->CreateStateBlock(D3DSBT_ALL, &s_sb);   // captures on creation
    if (FAILED(hr) || !s_sb) { s_sb = 0; prim_fail("CreateStateBlock(D3DSBT_ALL)", hr); return false; }
  }
  // A state block does not hold render targets: the game's RT0 is kept and put back by hand, and
  // ours is the back buffer so an offscreen target left bound at Present cannot hide the drawing.
  IDirect3DSurface9* bb = 0;
  HRESULT hr = dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
  if (FAILED(hr) || !bb) { prim_release_state_block(); prim_fail("GetBackBuffer", hr); return false; }
  if (FAILED(dev->GetRenderTarget(0, &s_gameRt))) s_gameRt = 0;
  hr = dev->SetRenderTarget(0, bb);
  bb->Release();
  if (FAILED(hr)) {
    if (s_gameRt) { s_gameRt->Release(); s_gameRt = 0; }
    prim_release_state_block();
    prim_fail("SetRenderTarget", hr);
    return false;
  }
  s_dev = dev;

  D3DVIEWPORT9 vp = {0, 0, (DWORD)W, (DWORD)H, 0.0f, 1.0f};
  s_dev->SetViewport(&vp);
  s_dev->SetVertexShader(NULL);
  s_dev->SetPixelShader(NULL);
  s_dev->SetFVF(kFvf);
  s_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
  s_dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
  s_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  s_dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
  s_dev->SetRenderState(D3DRS_LIGHTING, FALSE);
  s_dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
  s_dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
  s_dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
  s_dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
  s_dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
  s_dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
  s_dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
  s_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  s_dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
  s_dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
  s_dev->SetRenderState(D3DRS_WRAP0, 0);
  s_dev->SetStreamSourceFreq(0, 1);
  s_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
  s_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
  s_dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
  s_dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
  s_dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
  s_dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
  s_dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
  s_dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
  s_dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
  s_dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
  s_dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
  s_dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
  s_dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
  s_dev->SetTextureStageState(0, D3DTSS_RESULTARG, D3DTA_CURRENT);
  s_dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
  s_dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
  s_dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

  primbatch_init(&s_batch, PRIM_CAP);
  s_tex = 0;
  return true;
}

bool prim_begin(IDirect3DDevice9* dev, int W, int H) {
  s_sink = 0;
  return begin(dev, W, H);
}

bool prim_begin_isolated(IDirect3DDevice9* dev, int W, int H, const char** failed) {
  s_sink = failed;
  if (begin(dev, W, H)) return true;
  s_sink = 0;
  return false;
}

void prim_texture(void* tex) { s_tex = tex; }

void prim_tri(const PrimVert* a, const PrimVert* b, const PrimVert* c) {
  if (!s_dev || cur_failed()) return;
  const int oldCount = s_batch.count;
  void* const oldTex = s_batch.tex;
  if (primbatch_add_tri(&s_batch, s_tex) == PB_FLUSH_THEN_QUEUE) prim_draw(oldCount, oldTex);
  const PrimVert* in[3] = {a, b, c};
  PrimD3DVert* out = s_verts + s_batch.count - 3;
  for (int i = 0; i < 3; i++) {
    // Pixel centres sit at .5 in D3D9: the -0.5 puts texel and pixel grids on top of each other.
    out[i].x = in[i]->x - 0.5f;
    out[i].y = in[i]->y - 0.5f;
    out[i].z = 0.0f;
    out[i].rhw = 1.0f;
    out[i].color = in[i]->argb;
    out[i].u = in[i]->u;
    out[i].v = in[i]->v;
  }
  s_stats.tris++;
}

void prim_end(void) {
  if (!s_dev) return;
  if (!cur_failed()) prim_draw(s_batch.count, s_batch.tex);
  primbatch_reset(&s_batch);
  // RT first: setting a target resets the viewport, which the state block then restores.
  if (s_gameRt) { s_dev->SetRenderTarget(0, s_gameRt); s_gameRt->Release(); s_gameRt = 0; }
  if (s_sb) s_sb->Apply();
  prim_release_state_block();   // drops its references to the game's objects until the next frame we draw
  s_stats.frames++;
  s_dev = 0;
  s_sink = 0;
}

void* prim_make_alpha_texture(IDirect3DDevice9* dev, const unsigned char* alpha, int w, int h) {
  if (!dev || !alpha || w <= 0 || h <= 0) return 0;
  IDirect3DTexture9* t = 0;
  HRESULT hr = dev->CreateTexture((UINT)w, (UINT)h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, NULL);
  if (FAILED(hr) || !t) { np_log("prim: CreateTexture %dx%d failed hr=0x%08lX", w, h, (unsigned long)hr); return 0; }
  D3DLOCKED_RECT lr;
  hr = t->LockRect(0, &lr, NULL, 0);
  if (FAILED(hr)) {
    np_log("prim: LockRect failed hr=0x%08lX", (unsigned long)hr);
    t->Release();
    return 0;
  }
  for (int y = 0; y < h; y++) {
    DWORD* row = (DWORD*)((unsigned char*)lr.pBits + (size_t)y * (size_t)lr.Pitch);
    for (int x = 0; x < w; x++) row[x] = ((DWORD)alpha[y * w + x] << 24) | 0x00FFFFFF;
  }
  t->UnlockRect(0);
  return t;
}

unsigned long prim_release_texture(void* tex) {
  return tex ? ((IDirect3DTexture9*)tex)->Release() : 0;
}

bool prim_has_state_block(void) { return s_sb != 0; }

unsigned long prim_release_state_block(void) {
  if (!s_sb) return 0;
  const unsigned long n = s_sb->Release();
  s_sb = 0;
  return n;
}

void prim_reset_failure(void) { s_failed = 0; }

const char* prim_failed(void) { return s_failed; }

PrimStats prim_stats(void) { return s_stats; }

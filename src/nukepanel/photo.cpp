// Free camera > Photo glue (photo.h; freecam.md 5). DNMP's strip-to-PNG pipeline, engine tile lever: the MegaShot cvars
// give UpdateTransforms an off-axis sub-frustum per tile, read from the back buffer at Present into a 24-bit strip.
// Strips go to the WIC PNG encoder on a worker thread (two max); photo_worker shares only interlocked scalars.
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <d3d9.h>
#include <string.h>
#include "photo.h"
#include "photofolder.h"
#include "freecam.h"
#include "cvars.h"
#include "postfx.h"
#include "world.h"
#include "uname.h"
#include "input.h"
#include "dimouse.h"
#include "aimbot.h"       // aimbot_vk_di_button
#include "canvas.h"
#include "esp_layout.h"   // esp_draw_label
#include "font.h"
#include "config.h"
#include "log.h"

// The GUIDs by value, so nothing links windowscodecs.lib or uuid.lib (wincodec.h).
static const GUID kClsidWicFactory = { 0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
static const GUID kPng = { 0x1b7cfaf4, 0x713f, 0x473c, { 0xbb, 0xcd, 0x61, 0x37, 0x42, 0x5f, 0xae, 0xaf } };
static const GUID kBgr24 = { 0x6fddc324, 0x4e03, 0x4bfe, { 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0c } };

static PhotoSm s_sm = photo_begin(PHOTO_MULT_DEF);
static int s_W, s_H;                 // the last frame's back buffer
static int s_capW, s_capH, s_n;      // the capture's back buffer and multiplier
static int s_keepW, s_keepH, s_mx, s_my;   // a tile's kept size and the margin cropped off each side (photo.h)
static float s_kx = 1.f, s_ky = 1.f;       // the kept share: the shifts' scale
static int s_bands = 1;                    // bands per tile row (photo_bands): a strip is one band
static unsigned s_level;             // the level it began in
static int s_held;                   // photo_frame calls since the current tile went up
static int s_tileRead;               // photo_readback read the current tile this frame
static int s_mouseDown;
static const char* s_refused;
static unsigned s_tiles, s_blackTiles, s_readFails;
static double s_readMs, s_readMaxMs, s_frameMaxMs;
static ULONGLONG s_t0;
static unsigned long long s_ours, s_oursPeak;

// The readback: a system-memory copy of the back buffer, and a plain render target when the back buffer is
// multisampled (GetRenderTargetData takes no multisampled source). Made at the first tile, released at the end.
static IDirect3DSurface9* s_sys;
static IDirect3DSurface9* s_rt;
static IDirect3DDevice9* s_surfDev;

// The tile cvars, the four TakeMegaShot zeroes besides the camera's two, and the mirrors (photo.h).
static int s_cvFound;
static CvarRef s_scale, s_shiftX, s_shiftY;
static int s_megaOn;   // MegaShot values of ours are in place: the engine's 1, 0, 0 go back at the end
static CvarRef s_cv[PHOTO_CVAR_COUNT];
static CvarHold s_cvHold[PHOTO_CVAR_COUNT];

// A strip buffer (DNMP): chunks of s_chunkRows rows, never one block. state: SLOT_FREE (main may claim),
// SLOT_FILLING (main writes tiles), SLOT_QUEUED (the worker encodes it and sets it FREE).
enum { SLOT_FREE = 0, SLOT_FILLING = 1, SLOT_QUEUED = 2 };
struct Slot { unsigned char* chunk[PHOTO_CHUNKS_MAX]; volatile LONG state; volatile LONG row; };
static Slot s_slots[2];
static int s_fill = -1;
static unsigned s_stripStride, s_stripBytes;
static int s_stripRows, s_chunkRows, s_chunks;   // set at the start, before the worker starts
static int s_secondRefused;

static HANDLE s_thread, s_wake;
static volatile LONG s_wStop, s_wFailed, s_wDone, s_wStrips;
static volatile LONG64 s_wBytes;     // written once by the worker before it sets s_wDone
static wchar_t s_path[MAX_PATH];     // written before CreateThread, read by the worker
static char s_pathUtf8[MAX_PATH * 3];
static int s_closing;                // finished or aborted; buffers freed once the worker has exited

static double qpc_ms_since(LARGE_INTEGER t0) {
  LARGE_INTEGER t1, f;
  QueryPerformanceCounter(&t1);
  QueryPerformanceFrequency(&f);
  return (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
}

static unsigned char* big_alloc(unsigned bytes) {
  unsigned char* p = (unsigned char*)VirtualAlloc(NULL, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (p) { s_ours += bytes; if (s_ours > s_oursPeak) s_oursPeak = s_ours; }
  return p;
}

static void big_free(unsigned char** p, unsigned bytes) {
  if (!*p) return;
  VirtualFree(*p, 0, MEM_RELEASE);
  *p = 0;
  s_ours -= bytes;
}

static unsigned chunk_bytes(int k) { return (unsigned)photo_chunk_rows_in(k, s_stripRows, s_chunkRows) * s_stripStride; }

static void slot_free(Slot* sl) {
  for (int k = 0; k < PHOTO_CHUNKS_MAX; ++k) big_free(&sl->chunk[k], chunk_bytes(k));
}

// Every chunk of a strip, or none: what was got is freed and *refused names the chunk that was not.
static bool slot_alloc(Slot* sl, int* refused) {
  for (int k = 0; k < s_chunks; ++k) {
    sl->chunk[k] = big_alloc(chunk_bytes(k));
    if (!sl->chunk[k]) { *refused = k; slot_free(sl); return false; }
  }
  return true;
}

// ---------------------------------------------------------------------------------------------- the encoder
typedef HRESULT (WINAPI* CoInitExFn)(LPVOID, DWORD);
typedef void (WINAPI* CoUninitFn)(void);
typedef HRESULT (WINAPI* CoCreateFn)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
typedef HRESULT (WINAPI* StreamOnFileFn)(LPCWSTR, DWORD, DWORD, BOOL, IStream*, IStream**);

static void worker_fail(const char* what, HRESULT hr) {
  np_log("photo: encoder: %s failed (hr 0x%08lX)", what, (unsigned long)hr);
  InterlockedExchange(&s_wFailed, 1);
}

// The queued strip `row` (a band's index, r * bands + b), or -1. Main queues them in order, so at most one matches.
static int queued_slot(int row) {
  for (int i = 0; i < 2; ++i)
    if (s_slots[i].state == SLOT_QUEUED && s_slots[i].row == row) return i;
  return -1;
}

static DWORD WINAPI photo_worker(LPVOID) {
  const int n = s_n, tw = s_keepW, th = s_keepH, bands = s_bands, bandRows = s_stripRows, chunkRows = s_chunkRows;
  const int strips = n * bands;
  const unsigned stride = s_stripStride;
  HMODULE ole = LoadLibraryW(L"ole32.dll");
  CoInitExFn coInit = ole ? (CoInitExFn)GetProcAddress(ole, "CoInitializeEx") : 0;
  CoUninitFn coUninit = ole ? (CoUninitFn)GetProcAddress(ole, "CoUninitialize") : 0;
  CoCreateFn coCreate = ole ? (CoCreateFn)GetProcAddress(ole, "CoCreateInstance") : 0;
  HMODULE shl = LoadLibraryExW(L"shlwapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
  StreamOnFileFn onFile = shl ? (StreamOnFileFn)GetProcAddress(shl, "SHCreateStreamOnFileEx") : 0;
  IWICImagingFactory* fac = 0;
  IStream* file = 0;
  IWICStream* st = 0;
  IWICBitmapEncoder* enc = 0;
  IWICBitmapFrameEncode* fr = 0;
  IPropertyBag2* bag = 0;
  bool ok = false, coOn = false, opened = false;
  HRESULT hr = E_FAIL;
  do {
    if (!coInit || !coUninit || !coCreate) { worker_fail("ole32 (CoInitializeEx/CoCreateInstance)", E_FAIL); break; }
    if (!onFile) { worker_fail("shlwapi (SHCreateStreamOnFileEx)", E_FAIL); break; }
    hr = coInit(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) { worker_fail("CoInitializeEx", hr); break; }
    coOn = true;
    if (FAILED(hr = coCreate(kClsidWicFactory, NULL, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory),
                             (void**)&fac))) { worker_fail("the WIC factory", hr); break; }
    if (FAILED(hr = fac->CreateStream(&st))) { worker_fail("CreateStream", hr); break; }
    // STGM_FAILIFTHERE with fCreate is CREATE_NEW: a file already at the name is never truncated, and so never deleted.
    if (FAILED(hr = onFile(s_path, STGM_WRITE | STGM_SHARE_DENY_WRITE | STGM_FAILIFTHERE, FILE_ATTRIBUTE_NORMAL, TRUE,
                           NULL, &file))) { worker_fail("creating the file (it must not exist yet)", hr); break; }
    opened = true;
    if (FAILED(hr = st->InitializeFromIStream(file))) { worker_fail("opening the file", hr); break; }
    if (FAILED(hr = fac->CreateEncoder(kPng, NULL, &enc))) { worker_fail("the PNG encoder", hr); break; }
    if (FAILED(hr = enc->Initialize(st, WICBitmapEncoderNoCache))) { worker_fail("encoder Initialize", hr); break; }
    if (FAILED(hr = enc->CreateNewFrame(&fr, &bag))) { worker_fail("CreateNewFrame", hr); break; }
    if (FAILED(hr = fr->Initialize(bag))) { worker_fail("frame Initialize", hr); break; }
    if (FAILED(hr = fr->SetSize((UINT)(tw * n), (UINT)(th * n)))) { worker_fail("SetSize", hr); break; }
    WICPixelFormatGUID pf = kBgr24;
    if (FAILED(hr = fr->SetPixelFormat(&pf))) { worker_fail("SetPixelFormat", hr); break; }
    if (memcmp(&pf, &kBgr24, sizeof pf) != 0) { worker_fail("24bppBGR (the encoder offered another format)", E_FAIL); break; }
    bool stopped = false;
    for (int row = 0; row < strips && !stopped; ++row) {
      int i = -1;
      // The stop is checked before every strip, not only while waiting.
      while (!(stopped = s_wStop != 0) && (i = queued_slot(row)) < 0) WaitForSingleObject(s_wake, 100);
      if (stopped) break;
      const ULONGLONG t = GetTickCount64();
      hr = S_OK;
      const int rowsIn = photo_chunk_rows_in(row % bands, th, bandRows);   // the last band may be shorter
      for (int k = 0; k < photo_chunk_count(rowsIn, chunkRows) && SUCCEEDED(hr) && !(stopped = s_wStop != 0); ++k) {
        const UINT rows = (UINT)photo_chunk_rows_in(k, rowsIn, chunkRows);
        hr = fr->WritePixels(rows, stride, rows * stride, s_slots[i].chunk[k]);
      }
      InterlockedExchange(&s_slots[i].state, SLOT_FREE);
      if (FAILED(hr)) { worker_fail("WritePixels", hr); stopped = true; break; }
      if (stopped) break;
      InterlockedIncrement(&s_wStrips);
      np_log("photo: strip %d/%d encoded in %llu ms", row + 1, strips, (unsigned long long)(GetTickCount64() - t));
    }
    if (stopped) break;
    if (FAILED(hr = fr->Commit())) { worker_fail("frame Commit", hr); break; }
    if (FAILED(hr = enc->Commit())) { worker_fail("encoder Commit", hr); break; }
    ok = true;
  } while (0);
  if (bag) bag->Release();
  if (fr) fr->Release();
  if (enc) enc->Release();
  if (st) st->Release();
  if (file) file->Release();   // closes the file
  if (fac) fac->Release();
  if (coOn) coUninit();
  if (ok) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    LONG64 size = 0;
    if (GetFileAttributesExW(s_path, GetFileExInfoStandard, &fa))
      size = ((LONG64)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
    InterlockedExchange64(&s_wBytes, size);
    InterlockedExchange(&s_wDone, 1);
  } else if (opened) {
    // Our own partial output, never a file this capture did not create.
    const BOOL del = DeleteFileW(s_path);
    np_log("photo: encoder stopped; partial file %s (%lu)", del ? "deleted" : "NOT deleted", del ? 0ul : GetLastError());
  }
  if (shl) FreeLibrary(shl);
  if (ole) FreeLibrary(ole);
  return 0;
}

// ---------------------------------------------------------------------------------------------- the cvars
static void find_cvars(void) {
  if (s_cvFound) return;
  s_cvFound = 1;
  const char* why = 0;
  s_scale = cvar_find(L"MegaShotScale", &why);
  if (s_scale.ok && s_scale.isFloat) { s_scale.ok = 0; why = "MegaShotScale is a FLOAT, not an INT"; }
  if (!s_scale.ok && !s_refused) s_refused = "MegaShotScale is not usable; see nukepanel.log";
  s_shiftX = cvar_find(L"MegaShotXShift", &why);
  if (s_shiftX.ok && !s_shiftX.isFloat) s_shiftX.ok = 0;
  s_shiftY = cvar_find(L"MegaShotYShift", &why);
  if (s_shiftY.ok && !s_shiftY.isFloat) s_shiftY.ok = 0;
  if ((!s_shiftX.ok || !s_shiftY.ok) && !s_refused) s_refused = "MegaShotXShift/YShift are not usable; see nukepanel.log";
  for (int i = 0; i < PHOTO_CVAR_COUNT; i++) {
    const char* w = 0;
    s_cv[i] = cvar_find(photo_cvar_name(i), &w);
    if (s_cv[i].ok && s_cv[i].isFloat) s_cv[i].ok = 0;
    s_cvHold[i] = cvar_hold_init();
    if (!s_cv[i].ok) np_log("photo: %ls not used (%s); its effect may show a seam", photo_cvar_name(i), w ? w : "a FLOAT");
  }
  np_log("photo: MegaShot cvars %s", s_refused ? s_refused : "found: the tile lever is the engine's own");
}

// The tile's MegaShot values while one is up; the engine's own 1, 0, 0 once ours are not needed (TakeMegaShot's
// own reset). The six photo cvars (TakeMegaShot's four, the mirrors) go to 0 from arming to the last tile and come
// back after.
static void cvars_frame(void) {
  if (s_sm.phase != PH_IDLE) find_cvars();
  int scale;
  float x, y;
  photo_megashot_k(s_sm, s_kx, s_ky, &scale, &x, &y);
  if ((s_sm.phase == PH_TILE || s_sm.phase == PH_WAIT) && !s_refused) {
    cvar_set(s_scale, scale);
    cvar_set_float(s_shiftX, x);
    cvar_set_float(s_shiftY, y);
    s_megaOn = 1;
  } else if (s_megaOn) {
    cvar_set(s_scale, 1);
    cvar_set_float(s_shiftX, 0.f);
    cvar_set_float(s_shiftY, 0.f);
    s_megaOn = 0;
  }
  const int forced = photo_cvar_forced(s_sm.phase);
  for (int i = 0; s_cvFound && i < PHOTO_CVAR_COUNT; i++) {
    if (!s_cv[i].ok) continue;
    const int cur = cvar_get(s_cv[i]);
    int w = 0;
    const int act = cvar_step(&s_cvHold[i], forced, cur, 0, &w);
    if (act != CVAR_NONE) {
      cvar_set(s_cv[i], w);
      np_log("photo: %ls %d -> %d", photo_cvar_name(i), cur, w);
    }
  }
  postfx_set_photo(forced);
}

// ---------------------------------------------------------------------------------------------- start and end
static void release_surfaces(void) {
  if (s_sys) { s_sys->Release(); s_sys = 0; }
  if (s_rt) { s_rt->Release(); s_rt = 0; }
  s_surfDev = 0;
}

static void free_all(void) {
  for (int i = 0; i < 2; ++i) { slot_free(&s_slots[i]); s_slots[i].state = SLOT_FREE; }
  s_fill = -1;
  if (s_thread) { CloseHandle(s_thread); s_thread = 0; }
  if (s_wake) { CloseHandle(s_wake); s_wake = 0; }
  s_closing = 0;
}

// The buffers go only once the worker has exited: it may be inside WritePixels on one of them.
static void poll_cleanup(void) {
  if (!s_closing) return;
  if (s_thread && WaitForSingleObject(s_thread, 0) != WAIT_OBJECT_0) return;
  free_all();
}

static void release(bool stopWorker) {
  release_surfaces();
  if (stopWorker) InterlockedExchange(&s_wStop, 1);
  if (s_wake) SetEvent(s_wake);
  s_closing = 1;
  poll_cleanup();
}

// The map's name, ASCII, "" before a level.
static void level_name(char* out, int cap) {
  out[0] = 0;
  const int idx = world_map_name();
  const wchar_t* w = idx ? uname_str(idx) : L"";
  int n = 0;
  for (; w && w[n] && n < cap - 1; n++) out[n] = w[n] < 0x80 ? (char)w[n] : '_';
  out[n] = 0;
}

// <Photo folder>\<name>, the folder created (photofolder.h), with "-n" while a file already has the name. Built once
// at the start: a folder chosen later does not move a photo in progress. False (logged) without one.
static bool build_path(const char* name) {
  wchar_t dir[MAX_PATH], wname[160];
  if (!photo_folder_dir(dir, MAX_PATH)) return false;   // logged there
  const size_t n = wcslen(dir);
  const bool sep = n && (dir[n - 1] == L'\\' || dir[n - 1] == L'/');   // a drive root ends in one
  char numbered[160];
  for (int i = 1; i <= PHOTO_NAME_TRIES; ++i) {
    const bool ok = photo_file_name_n(name, i, numbered, sizeof numbered) > 0 &&
                    MultiByteToWideChar(CP_ACP, 0, numbered, -1, wname, 160) > 0 &&
                    _snwprintf_s(s_path, MAX_PATH, _TRUNCATE, sep ? L"%s%s" : L"%s\\%s", dir, wname) > 0;
    if (!ok) { np_log("photo: the photo's path is over %d characters", MAX_PATH - 1); return false; }
    if (GetFileAttributesW(s_path) != INVALID_FILE_ATTRIBUTES) continue;   // the worker would refuse it anyway
    WideCharToMultiByte(CP_UTF8, 0, s_path, -1, s_pathUtf8, sizeof s_pathUtf8, NULL, NULL);
    return true;
  }
  np_log("photo: %s and its -2..-%d names are all taken", name, (int)PHOTO_NAME_TRIES);
  return false;
}

// At the key: the first strip, the file and the encoder thread. PHR_NONE when started, else the abort's reason;
// the budget is checked before anything is allocated.
static int start(int W, int H) {
  s_capW = W; s_capH = H; s_n = s_sm.n;
  s_mx = photo_margin(W);
  s_my = photo_margin(H);
  s_keepW = photo_kept(W);
  s_keepH = photo_kept(H);
  s_kx = photo_keep_ratio(W);
  s_ky = photo_keep_ratio(H);
  s_level = world_level_serial();
  s_held = 0;
  s_tiles = s_blackTiles = s_readFails = 0;
  s_readMs = s_readMaxMs = s_frameMaxMs = 0.0;
  s_secondRefused = 0;
  s_oursPeak = s_ours;
  s_t0 = GetTickCount64();
  s_bands = photo_bands(s_keepW, s_keepH, s_n);
  if (!s_bands) {
    np_log("photo: %dx at %dx%d does not fit the %llu MB budget even in %d bands; max %dx", s_n, W, H,
           PHOTO_BUDGET_BYTES >> 20, (int)PHOTO_BANDS_MAX, photo_mult_max(W, H));
    s_bands = 1;
    return PHR_TOO_LARGE;
  }
  s_sm.bands = s_bands;
  s_stripRows = photo_band_rows(s_keepH, s_bands);
  const unsigned long long strip = (unsigned long long)photo_stride(s_keepW * s_n) * (unsigned long long)s_stripRows;
  s_stripStride = photo_stride(s_keepW * s_n);
  s_stripBytes = (unsigned)strip;
  s_chunkRows = photo_chunk_rows(s_stripStride, s_stripRows);
  s_chunks = photo_chunk_count(s_stripRows, s_chunkRows);
  int refused = -1;
  if (!slot_alloc(&s_slots[0], &refused)) {
    np_log("photo: no memory for a %u MB strip: chunk %d/%d of %u MB refused", s_stripBytes >> 20, refused + 1, s_chunks,
           chunk_bytes(refused) >> 20);
    return PHR_START;
  }
  s_slots[0].state = SLOT_FILLING;
  s_fill = 0;
  char level[64], name[128];
  level_name(level, sizeof level);
  SYSTEMTIME t;
  GetLocalTime(&t);
  if (photo_file_name(name, sizeof name, level, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                      s_keepW * s_n, s_keepH * s_n) < 0 || !build_path(name))
    return PHR_START;
  s_wStop = s_wFailed = s_wDone = s_wStrips = 0;
  s_wBytes = 0;
  s_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
  s_thread = s_wake ? CreateThread(NULL, 0, photo_worker, NULL, 0, NULL) : 0;
  if (!s_thread) { np_log("photo: the encoder thread did not start (%lu)", GetLastError()); return PHR_START; }
  char size[48];
  photo_size_text(s_n, s_keepW, s_keepH, size, sizeof size);
  np_log("photo: start %s from the %dx%d back buffer (margin %d x %d cropped, shifts x %.5f y %.5f), %d tiles in %d "
         "band(s) a row: %d renders, %d strips of %d rows, %u MB (%d chunks, at most 2 held), level \"%s\", to %s",
         size, W, H, s_mx, s_my, (double)s_kx, (double)s_ky, s_n * s_n, s_bands, photo_tiles_total(s_sm),
         s_n * s_bands, s_stripRows, s_stripBytes >> 20, s_chunks, level, s_pathUtf8);
  return PHR_NONE;
}

// A free strip buffer for the next row (the second allocated on first need), or -1.
static int free_slot(void) {
  for (int i = 0; i < 2; ++i)
    if (s_slots[i].chunk[0] && s_slots[i].state == SLOT_FREE) return i;
  for (int i = 0; i < 2; ++i) {
    if (s_slots[i].chunk[0] || s_secondRefused) continue;
    int refused = -1;
    if (slot_alloc(&s_slots[i], &refused)) { s_slots[i].state = SLOT_FREE; return i; }
    s_secondRefused = 1;
    np_log("photo: a second %u MB strip was refused at chunk %d/%d; one strip in flight, each row waits for the "
           "encoder", s_stripBytes >> 20, refused + 1, s_chunks);
  }
  return -1;
}

// ---------------------------------------------------------------------------------------------- the readback
// The surfaces once per capture, on the device the back buffer belongs to.
static bool surfaces(IDirect3DDevice9* dev, const D3DSURFACE_DESC& d) {
  if (s_sys && s_surfDev == dev) return true;
  release_surfaces();
  HRESULT hr = dev->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &s_sys, NULL);
  if (FAILED(hr)) { np_log("photo: CreateOffscreenPlainSurface hr=0x%08lX", (unsigned long)hr); return false; }
  if (d.MultiSampleType != D3DMULTISAMPLE_NONE) {
    hr = dev->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &s_rt, NULL);
    if (FAILED(hr)) {
      np_log("photo: CreateRenderTarget (the multisampled back buffer's resolve) hr=0x%08lX", (unsigned long)hr);
      release_surfaces();
      return false;
    }
  }
  s_surfDev = dev;
  np_log("photo: readback %ux%u format %d multisample %d%s", d.Width, d.Height, (int)d.Format, (int)d.MultiSampleType,
         s_rt ? " (resolved through a plain render target)" : "");
  return true;
}

void photo_readback(IDirect3DDevice9* dev, int W, int H) {
  s_tileRead = 0;
  if (s_sm.phase != PH_TILE || s_fill < 0 || s_held < PHOTO_HOLD_FRAMES || W != s_capW || H != s_capH) return;
  LARGE_INTEGER t0;
  QueryPerformanceCounter(&t0);
  IDirect3DSurface9* bb = 0;
  D3DSURFACE_DESC d;
  bool ok = SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb && SUCCEEDED(bb->GetDesc(&d));
  if (ok && (d.Format != D3DFMT_X8R8G8B8 && d.Format != D3DFMT_A8R8G8B8)) {
    if (!s_readFails) np_log("photo: the back buffer's format %d is not a 32-bit BGRA one", (int)d.Format);
    ok = false;
  }
  ok = ok && (int)d.Width == W && (int)d.Height == H && surfaces(dev, d);
  IDirect3DSurface9* src = bb;
  if (ok && s_rt) {
    ok = SUCCEEDED(dev->StretchRect(bb, NULL, s_rt, NULL, D3DTEXF_NONE));
    src = s_rt;
  }
  HRESULT hr = ok ? dev->GetRenderTargetData(src, s_sys) : E_FAIL;
  D3DLOCKED_RECT lr;
  if (ok && SUCCEEDED(hr) && SUCCEEDED(hr = s_sys->LockRect(&lr, NULL, D3DLOCK_READONLY))) {
    // This band's rows of the kept part only.
    const int row0 = s_sm.b * s_stripRows, rows = photo_chunk_rows_in(s_sm.b, s_keepH, s_stripRows);
    const unsigned char* kept =
      (const unsigned char*)lr.pBits + photo_tile_src_offset((unsigned)lr.Pitch, s_mx, s_my, row0);
    const bool any = photo_copy_bgra_chunks(kept, (unsigned)lr.Pitch, s_keepW, rows, s_slots[s_fill].chunk,
                                            s_chunkRows, s_stripStride, s_sm.c);
    s_sys->UnlockRect();
    s_tileRead = 1;
    ++s_tiles;
    if (!any) ++s_blackTiles;
  } else if (++s_readFails <= 8) {
    np_log("photo: tile (%d, %d) not read (hr 0x%08lX)", s_sm.r, s_sm.c, (unsigned long)hr);
  }
  if (bb) bb->Release();
  const double ms = qpc_ms_since(t0);
  s_readMs += ms;
  if (ms > s_readMaxMs) s_readMaxMs = ms;
}

// ---------------------------------------------------------------------------------------------- the frame
// A Photo key bound to a mouse button: its edge is polled, as the camera's own keys are (freecam.cpp).
static int mouse_edge(void) {
  const int vk = config_bind_get(BIND_ROW_PHOTO);
  int down = 0;
  if (bind_is_mouse(vk) && input_game_front() && !input_panel_open()) {
    down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const int b = aimbot_vk_di_button(vk);
    if (!down && b >= 0) down = dimouse_button(b);
  }
  const int edge = down && !s_mouseDown;
  s_mouseDown = down;
  return edge;
}

void photo_frame(int W, int H) {
  LARGE_INTEGER t0;
  QueryPerformanceCounter(&t0);
  s_W = W;
  s_H = H;
  poll_cleanup();
  const int presses = input_photo_take() + mouse_edge();
  FreecamPose pose;
  const int cam = freecam_pose_now(&pose);
  if (s_sm.phase == PH_IDLE) {
    if (presses && cam) {
      find_cvars();
      if (s_closing) { np_log("photo: the last photo's encoder is still closing; key ignored"); return; }
      if (s_refused) { np_log("photo: not taken: %s", s_refused); return; }
    }
    s_sm.n = photo_mult_clamped(g_cfg.photoMult);
    s_sm.bands = 1;   // start() sets the capture's own
  }
  PhotoIn in;
  memset(&in, 0, sizeof in);
  in.press = presses > 0;
  in.cam = cam;
  in.panel = input_panel_open();
  in.world = world_player() != 0 && (s_sm.phase == PH_IDLE || world_level_serial() == s_level);
  in.focus = input_game_front();
  in.sameSize = s_sm.phase == PH_IDLE || (W == s_capW && H == s_capH);
  in.frozen = freecam_pause_holding() && world_paused_now();
  in.tileRead = s_tileRead;
  const int waiting = s_sm.phase == PH_WAIT;
  const int slot = waiting ? free_slot() : -1;
  in.bufferFree = slot >= 0;
  in.encFailed = s_wFailed != 0;
  in.encDone = s_wDone != 0;
  const int prevPhase = s_sm.phase, prevR = s_sm.r, prevC = s_sm.c, prevB = s_sm.b;
  const int tilesDone = photo_tiles_done(s_sm);
  const int act = photo_step(&s_sm, in);
  s_tileRead = 0;
  if (s_sm.phase != prevPhase || s_sm.r != prevR || s_sm.c != prevC || s_sm.b != prevB) s_held = 0;
  else if (s_sm.phase == PH_TILE) ++s_held;
  if (waiting && s_sm.phase == PH_TILE) {
    s_fill = slot;
    InterlockedExchange(&s_slots[slot].state, SLOT_FILLING);
  }
  if (act == PA_START) {
    const int why = start(W, H);
    if (why != PHR_NONE) {
      photo_fail(&s_sm, why);
      np_log("photo: ABORTED: %s", photo_why_text(s_sm.reason));
      release(true);
    }
  } else if (act == PA_FLUSH) {
    Slot& sl = s_slots[s_fill];
    InterlockedExchange(&sl.row, s_sm.flushRow);
    InterlockedExchange(&sl.state, SLOT_QUEUED);
    SetEvent(s_wake);
    s_fill = -1;
  } else if (act == PA_FINISH) {
    np_log("photo: done in %.1f s, %lld bytes, %u tile renders in %d band(s) a row (black %u, read failures %u), "
           "readback %.1f ms a tile (max %.1f), frame max %.1f ms, peak held %llu MB: %s",
           (double)(GetTickCount64() - s_t0) / 1000.0, (long long)s_wBytes, s_tiles, s_bands, s_blackTiles, s_readFails,
           s_tiles ? s_readMs / s_tiles : 0.0, s_readMaxMs, s_frameMaxMs, s_oursPeak >> 20, s_pathUtf8);
    release(false);
  } else if (act == PA_ABORT) {
    np_log("photo: ABORTED after %.1f s at tile %d/%d: %s; %u tiles read, readback max %.1f ms, frame max %.1f ms",
           (double)(GetTickCount64() - s_t0) / 1000.0, tilesDone, photo_tiles_total(s_sm), photo_why_text(s_sm.reason),
           s_tiles, s_readMaxMs, s_frameMaxMs);
    release(true);
  }
  // After the step: every way a capture ends gives the cvars back this same frame.
  cvars_frame();
  if (s_sm.phase != PH_IDLE) {
    const double ms = qpc_ms_since(t0);
    if (ms > s_frameMaxMs) s_frameMaxMs = ms;
  }
}

void photo_draw(int W, int H) {
  if (s_sm.phase == PH_IDLE) return;
  char line[96];
  if (s_sm.phase == PH_DRAIN)
    _snprintf_s(line, sizeof line, _TRUNCATE, "Photo: writing the PNG, strip %ld/%d", s_wStrips, s_n * s_bands);
  else _snprintf_s(line, sizeof line, _TRUNCATE, "Photo %d/%d", photo_tiles_done(s_sm), photo_tiles_total(s_sm));
  canvas_open(W, H);
  const int y = FREECAM_READOUT_MARGIN + (g_cfg.levelStats ? 22 : 0) + (g_cfg.freecamDebug ? 16 : 0);
  esp_draw_label(FREECAM_READOUT_MARGIN, y, line, FREECAM_READOUT_RGBA, FONT_SMALL);
}

void photo_device_lost(void) {
  release_surfaces();
  if (photo_fail(&s_sm, PHR_DEVICE) == PA_ABORT) {
    np_log("photo: ABORTED: %s", photo_why_text(s_sm.reason));
    release(true);
    cvars_frame();
  }
}

bool photo_busy(void) { return s_sm.phase != PH_IDLE; }
bool photo_capturing(void) { return s_sm.phase != PH_IDLE && s_sm.phase != PH_DRAIN; }
void photo_screen_size(int* W, int* H) { *W = s_W; *H = s_H; }
const char* photo_refused(void) { return s_refused; }

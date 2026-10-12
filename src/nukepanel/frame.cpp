// The frame path: the thread log, the hitch line and frame line, input and the panel
// inside our own scene, then the stall watchdog's frame end. Runs on whichever thread presents the captured device.
#include <windows.h>
#include <d3d9.h>
#include "frame.h"
#include "framestats.h"
#include "fileio.h"
#include "stall.h"
#include "framecap.h"
#include "d3dcap.h"
#include "d3dres.h"
#include "prim.h"
#include "canvas.h"
#include "config.h"
#include "input.h"
#include "panel.h"
#include "bigpackage.h"
#include "drawgate.h"
#include "world.h"
#include "esp.h"
#include "dmgnum.h"
#include "scall.h"
#include "fov.h"
#include "gamespeed.h"
#include "accuracy.h"
#include "handling.h"
#include "turretwep.h"
#include "focuskeep.h"
#include "postfx.h"
#include "pehook.h"
#include "rawmouse.h"
#include "noshake.h"
#include "dukevision.h"
#include "progress.h"
#include "achhelp.h"
#include "levelstats.h"
#include "autowin.h"
#include "pbtrace.h"
#include "arctrace.h"
#include "pinbot.h"
#include "arcbot.h"
#include "autoexec.h"
#include "aim.h"
#include "tbot.h"
#include "aimbot.h"
#include "spawnbox.h"
#include "overkill.h"
#include "aircraft.h"
#include "freecam.h"
#include "teleport.h"
#include "photo.h"
#include "photofolder.h"
#include "fblift.h"
#include "worldscan.h"
#include "items.h"
#include "ui/menu.h"     // menu_active_tab: the Progress tab reads while it is open
#include "log.h"
#include "mpgate.h"

extern "C" volatile unsigned long g_np_attach_tid;   // proxy.cpp, DllMain's thread

static volatile LONG s_firstCallTid;
void frame_note_first_call_tid(unsigned long tid) { InterlockedExchange(&s_firstCallTid, (LONG)tid); }

// Presenting-thread state only.
static IDirect3DDevice9* s_resDev;   // device our objects belong to
static D3dRes s_res;
static int s_bbW, s_bbH;
static volatile LONG s_presentTid;   // read by hooks on any thread
static bool s_threadsLogged, s_movedLogged, s_sceneFailLogged;
static unsigned s_frames;
static LARGE_INTEGER s_qpf, s_lastEntry;
static FrameStats s_fs;
static HitchStats s_hitch;
static double s_panelMs;   // our time on the frame path since the last Present entry

static double qpc_ms(LONGLONG ticks) { return (double)ticks * 1000.0 / (double)s_qpf.QuadPart; }

// At Present entry: dt spans entry to entry, so a game-side stall anywhere in the frame lands in one dt. The
// log and file-I/O stats are taken every frame, so a hitch line's log= and io= cover the window its dt spans.
// The game exits without DLL_PROCESS_DETACH, so the frame line carries the session totals.
static void hitch_frame(LARGE_INTEGER now) {
  unsigned lc, lt, lm, ic, it, im;
  np_log_take_write_stats(&lc, &lt, &lm);
  fileio_take(&ic, &it, &im);
  const double panelMs = s_panelMs;
  s_panelMs = 0.0;
  if (!s_lastEntry.QuadPart) {
    s_lastEntry = now;
    fs_reset(&s_fs);
    return;
  }
  const double dtMs = qpc_ms(now.QuadPart - s_lastEntry.QuadPart);
  s_lastEntry = now;
  fs_add(&s_fs, dtMs / 1000.0);
  if (fs_hitch_note(&s_hitch, dtMs)) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    np_log("hitch: %02u:%02u:%02u.%03u dt=%.0f ms panel=%.1f ms log=%u writes %.1f ms (max %.1f) io=%u calls %.1f ms"
           " (max %.1f) stall=%d", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, dtMs, panelMs, lc,
           lt / 1000.0, lm / 1000.0, ic, it / 1000.0, im / 1000.0, stall_frame_samples());
  }
  if (++s_frames % 600 == 0) {
    const PrimStats ps = prim_stats();
    FileIoTotals io;
    fileio_totals(&io);
    StallTotals sl;
    stall_totals(&sl);
    np_log("frame avg=%.2fms min=%.2f max=%.2f n=%d hitches>100=%u >500=%u frames=%u draws=%u tris=%u"
           " | totals io=%u calls %.1f ms (max %.1f) opens=%u slow-opens=%u stalls=%ld samples=%ld long=%ld",
           fs_avg_ms(&s_fs), s_fs.min_ms, s_fs.max_ms, s_fs.n, s_hitch.over100, s_hitch.over500, s_frames,
           ps.draws, ps.tris, io.calls, io.total_ms, io.max_ms, io.opens, io.slow, sl.stalls, sl.samples,
           sl.longFrames);
    fs_reset(&s_fs);
  }
}

static void log_threads(DWORD tid) {
  HWND w = d3dcap_window();
  const DWORD wndTid = w ? GetWindowThreadProcessId(w, NULL) : 0;
  np_log("threads: dllmain=%lu firstcall=%lu createdevice=%lu present=%lu window=%lu",
         (unsigned long)g_np_attach_tid, (unsigned long)s_firstCallTid, d3dcap_create_tid(),
         (unsigned long)tid, (unsigned long)wndTid);
}

static void query_back_buffer(IDirect3DDevice9* dev) {
  IDirect3DSurface9* bb = 0;
  if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
  D3DSURFACE_DESC d;
  if (SUCCEEDED(bb->GetDesc(&d))) {
    s_bbW = (int)d.Width;
    s_bbH = (int)d.Height;
    np_log("frame: back buffer %dx%d format=%d", s_bbW, s_bbH, (int)d.Format);
  }
  bb->Release();
}

static void present_body(IDirect3DDevice9* dev) {
  const DWORD tid = GetCurrentThreadId();
  if (!s_threadsLogged) {
    s_threadsLogged = true;
    InterlockedExchange(&s_presentTid, (LONG)tid);
    log_threads(tid);
    config_load();   // first frame, never in DllMain: outside the loader lock
  } else if (tid != (DWORD)s_presentTid && !s_movedLogged) {
    s_movedLogged = true;
    np_log("threads: present moved from tid=%lu to tid=%lu", (unsigned long)s_presentTid, (unsigned long)tid);
  }

  if (dev != s_resDev) {
    if (s_resDev) frame_on_device_replaced();   // normally already done in the CreateDevice detour
    s_resDev = dev;
    s_res = d3dres_on_created(s_res);
    s_bbW = s_bbH = 0;
  }
  // Every frame, before the early returns: Insert opens nothing while nothing can be drawn.
  input_set_draw_disabled(drawgate_reason(prim_failed(), !s_res.stateBlock));
  if (!s_res.stateBlock) return;   // a Reset failed: nothing until one succeeds
  if (!s_bbW) query_back_buffer(dev);
  if (!s_bbW) return;
  // Fullbright's lift first, over the engine's frame (holding as of last frame, when this frame's RendMap was set),
  // so a photo tile carries it and our overlays draw over it.
  fblift_present(dev, s_bbW, s_bbH, freecam_bright_holding());
  // Then, before anything else of ours draws: a photo tile is the engine's frame alone.
  photo_readback(dev, s_bbW, s_bbH);

  // Before the panel: the ESP reads this frame's pawns and view. Pawns only for a feature that draws them.
  // Auto-execute and Silent aim pick their targets from the same walk.
  const int inLevel = world_frame(s_bbW, s_bbH, !mp_refused() &&   // no pawn reads in multiplayer (last frame's mode)
                                                (world_wants_pawns(g_cfg.espOn, g_cfg.dmgNumbers) ||
                                                 g_cfg.autoExecute || g_cfg.silentOn || g_cfg.tbotOn ||
                                                 g_cfg.aimbotOn));
  // Outside standalone play (mpgate.h) the marker passes below do not run; each feature also refuses itself.
  const int mpOff = mp_refused() != 0;
  const int espLevel = inLevel && !mpOff;
  // Finds and logs the script functions the player options call; calls none of them.
  if (inLevel) scall_selfcheck(world_player());
  // The player options apply and release here, on the game thread, whenever a player and its level exist.
  fov_frame();
  gamespeed_frame();
  accuracy_frame();
  // The free camera's own pause leaves inLevel 1 (the ESP draws through it); what acts on the game sees a pause.
  // The queries (worldscan, items, spawnbox, progress) run on through it by intent: they change nothing.
  const int live = inLevel && !freecam_pause_holding();
  // Fire rate and reload speed: the held weapon's controller and lockout, by ProcessEvent; nothing while paused.
  handling_frame(live);
  // The same two rows on the mounted gun (spread, its fire anims); nothing while paused.
  turretwep_frame(live);
  // QTE presses and minigame wins through ProcessEvent; nothing while paused (inLevel is 0 then).
  autowin_frame(live);
  // The pinball trace (reads only) while the player uses a table; before pehook_frame applies its enables.
  pbtrace_frame(live);
  // Pinball autoplay on the trace's frame read: the table's own flipper and plunger inputs; it stands down while the
  // panel is open or the camera flies.
  pinbot_frame(live, input_panel_open() || freecam_flying());
  pbtrace_frame_end();
  // The Nuke Tank trace (reads only) while the player uses the arcade machine; before pehook_frame applies its enables.
  arctrace_frame(live);
  // Arcade autoplay on the trace's frame read: the machine's own Fire, Left and Right; it stands down while the panel
  // is open or the camera flies.
  arcbot_frame(live, input_panel_open() || freecam_flying());
  arctrace_frame_end();
  // Executions through the game's own Use (CanBeUsedBy, Used); nothing while paused, flying or with the panel open.
  autoexec_frame(live, input_panel_open() || freecam_flying());
  // The item and breakable rows' object-table pass (per level and 3 Hz), then CanPickupClass at 4 Hz, before the ESP draws.
  worldscan_frame(espLevel);
  items_frame(espLevel);
  // The hostile aircraft of worldscan's pass, before the targeting (its last resort) and the ESP read them.
  aircraft_frame(espLevel);
  // Progress.ini and the chapter table, at most once a second, while the tab, the Level stats line or the Ego boosts,
  // Helmets or Phone messages markers show them.
  progress_frame(input_panel_open() && menu_active_tab() == TAB_PROGRESS,
                 inLevel && (g_cfg.levelStats || (!mpOff && (g_cfg.espEgo || g_cfg.espHelmets || g_cfg.espPhones)) ||
                             achhelp_wants_progress()), GetTickCount());
  // After progress_frame (the map and chapter): the Achievements rows, their progress and Steam state at 1 Hz.
  achhelp_frame(inLevel, input_panel_open() && menu_active_tab() == TAB_ACHIEVEMENTS, GetTickCount());
  input_frame(s_bbW, s_bbH);
  // After input_frame (its key presses and focus losses) and before raw mouse (which yields the mouse counts in
  // flight) and pehook_frame (which applies its enable). Ends a flight on every exit path, the same frame.
  freecam_frame(inLevel);
  teleport_frame();   // the camera's pose of this frame: the marker's cast and the Teleport key
  photo_frame(s_bbW, s_bbH);   // after the camera: its key, its step, the tile's cvars for the next frame
  // After input_frame: the focus flag reads the subclassed window's activation state. Neither needs a level.
  focuskeep_frame();
  postfx_frame();
  // The slot's subscribers first, then the slot: it installs once and applies this frame's enables.
  rawmouse_frame(inLevel);   // inLevel is 0 while paused: no script call then
  noshake_frame();
  dukevision_frame(inLevel);
  // Drop-in pawns whose body is away from Location, and cut-scene pawns (spawns.md section 5): before the targeting
  // leaves them out and before the ESP and damage numbers box them. The ESP and the aim features stand down with the
  // panel open.
  spawnbox_frame(espLevel && (g_cfg.dmgNumbers || (!input_panel_open() && (g_cfg.espOn || g_cfg.silentOn ||
                                                                          g_cfg.aimbotOn || g_cfg.tbotOn))));
  // The player's projectiles in flight and the targets they will kill (overkill.h): before the pick leaves those out.
  overkill_frame(inLevel);
  // Flying stands the aim features down as the open panel does (spec: they never act from the camera).
  const int gated = input_panel_open() || freecam_flying();
  // Silent aim's target for the next tick's shots; its bracket runs in the slot's handlers.
  aim_frame(s_bbW, s_bbH, inLevel, gated);
  // After aim_frame (its gate and target): the aimbot's lock; its turn runs in the slot, after raw mouse's.
  aimbot_frame(inLevel, gated);
  // After aim_frame (its gate, weapon and target): FireDown/FireUp, and FireUp at once when the gate closes.
  tbot_frame(s_bbW, s_bbH, inLevel, gated);
  pehook_frame();
  bool scene = false;
  // The ESP draws with the panel closed, so a level with it on needs a scene too.
  if (panel_wants_draw() || freecam_wants_draw() || photo_busy() || (inLevel && (g_cfg.levelStats ||
                                  (!mpOff && (g_cfg.espOn || g_cfg.espEgo || g_cfg.dmgNumbers || g_cfg.espItems ||
                                              g_cfg.espAmmoCrates || g_cfg.espQuest || g_cfg.espBreakables ||
                                              g_cfg.espHelmets || g_cfg.espPhones || achhelp_any_on() ||
                                              aim_marker_shown(g_cfg.silentOn, g_cfg.aimbotOn, g_cfg.aimMarker)))))) {
    const HRESULT hr = dev->BeginScene();
    scene = SUCCEEDED(hr);
    if (!scene && !s_sceneFailLogged) {
      s_sceneFailLogged = true;
      np_log("frame: BeginScene failed hr=0x%08lX", (unsigned long)hr);
    }
  }
  // The canvas keeps its atlases on this device while d3dres allows textures; no scene, nothing drawn.
  canvas_device(scene && s_res.texture ? dev : NULL);
  // Screenshot mode: none of our overlays while flying (the panel itself still opens).
  if (!freecam_screenshot()) {
    esp_frame(s_bbW, s_bbH);      // the panel covers the ESP, so it draws after
    dmgnum_frame(s_bbW, s_bbH);   // also steps its tracker, and resets it when no level is up
    levelstats_draw(s_bbW, s_bbH, inLevel);
  }
  freecam_draw(s_bbW, s_bbH);
  photo_draw(s_bbW, s_bbH);
  panel_frame(s_bbW, s_bbH);
  canvas_close();   // the one begin/end of the frame, opened by whichever drew first
  canvas_device(NULL);
  if (scene) dev->EndScene();
  // Outside the scene: a requested Photo folder picker blocks this thread until the player closes it.
  photo_folder_frame();
}

unsigned long frame_thread_id(void) { return (unsigned long)s_presentTid; }

void frame_on_present(IDirect3DDevice9* dev) {
  LARGE_INTEGER t0, t1;
  QueryPerformanceCounter(&t0);
  if (!s_qpf.QuadPart) QueryPerformanceFrequency(&s_qpf);
  hitch_frame(t0);
  present_body(dev);
  bigpackage_frame();   // after the panel, on this (the game) thread, whatever present_body skipped
  QueryPerformanceCounter(&t1);
  s_panelMs += qpc_ms(t1.QuadPart - t0.QuadPart);
}

// After the real Present: the cap waits, then the stall watchdog's frame end. The wait sits inside the
// watchdog's frame window, but it is at most one period, far below the 300 ms threshold. The hitch dt (Present entry to entry) includes the wait, so hitches are judged against the
// cap period: a 100 ms line still means the game lost 100 ms, at any cap.
void frame_after_present(void) {
  framecap_wait(d3dcap_window());
  stall_frame_end();
}

// Runs on the CreateDevice thread: the game has torn its old device down by then, so no Present on
// it is in flight. Our objects keep that device alive, so releasing them here is safe.
void frame_on_device_replaced(void) {
  IDirect3DDevice9* old = s_resDev;
  if (!old) return;
  photo_device_lost();   // its readback surfaces belong to the old device
  fblift_device_lost(1);
  const bool held = prim_has_state_block() || canvas_holds_device();
  if (held) old->AddRef();   // only while our objects pin it: measure what the game left on it
  const unsigned long sbRef = prim_release_state_block();
  const int textures = canvas_release_device();
  const unsigned long devRef = held ? old->Release() : 0;
  np_log("frame: device %p replaced: released state block (ref %lu) and %d atlas texture(s); "
         "device refs left %lu%s", (void*)old, sbRef, textures, devRef, held ? "" : " (we held nothing)");
  s_res = d3dres_on_device_replaced(s_res);
  s_resDev = 0;
  s_bbW = s_bbH = 0;
  prim_reset_failure();
}

void frame_on_reset_before(void) {
  if (s_resDev != d3dcap_device()) return;   // our objects live on another device
  s_res = d3dres_on_reset_before(s_res);
  prim_release_state_block();
  photo_device_lost();   // a capture does not survive a Reset
  fblift_device_lost(0);  // its copy texture is in the default pool
  s_bbW = s_bbH = 0;
}

void frame_on_reset_after(bool ok) {
  if (s_resDev != d3dcap_device()) return;
  s_res = d3dres_on_reset_after(s_res, ok);
  if (ok) framecap_forget_monitor();
  if (!ok) np_log("frame: Reset failed; drawing paused until one succeeds");
  // Now, not at the next Present: a game looping on Reset without presenting must not let the panel open.
  input_set_draw_disabled(drawgate_reason(prim_failed(), !s_res.stateBlock));
}

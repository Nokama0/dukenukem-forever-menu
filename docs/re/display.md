# Display: how DNF goes fullscreen, and how the borderless window takes over

Fullscreen is a D3DDrv9 present-parameters flag (`Windowed = !fullscreen`) plus an Engine.dll window restyle
(caption cleared). Borderless keeps the engine in its fullscreen state and changes only what d3d9 sees: a windowed
device at the game's own back-buffer size, in a `WS_POPUP|WS_VISIBLE` window over its monitor, pinned there through
Engine.dll's USER32 import slots. On focus loss the engine's own mouse-move handler pins the cursor on the visible
inactive window; borderless holds it back while another window is in front (below, "Focus loss"; verified in game).

Method: Ghidra MCP on Engine.dll and D3DDrv9.dll (names saved in project dnf); the panel's log and Steam's own logs
for the launch cases.

## Read (Engine.dll, D3DDrv9.dll; names saved in project dnf)

- `UWindowsViewport::OpenWindow` Engine.dll!0x102BC090: creates the window with style `0xCB0000` (caption, sysmenu,
  minimise box, no sizing frame; `0xCF0000` in the editor), ex style `0x40000` (WS_EX_APPWINDOW), sized to the windowed
  resolution (client `+0x58/+0x5C`). Its `SetWindowLong(0x90000000)` is the **embedded child-window** path
  (`HWND=` on the command line), not fullscreen. Fullscreen is `UClient+0xE0` (StartupFullscreen), passed to
  `TryRenderDevice` (vtable `+0xB8`) for `D3DDrv9.D3DRenderDevice9`.
- `BeginFullscreen` 0x102BB370 / `EndFullscreen` 0x102B9ED0: write `StartupFullscreen 1/0` to the game's ini
  (`set ini:Engine.Engine.ViewportManager ...`) and call the render device's `SetRes` (vtable `+0x64`) with the
  fullscreen (`+0x64/+0x68`) or windowed (`+0x58/+0x5C`) size.
- `UD3DRenderDevice9::SetRes` D3DDrv9!0x1002F6F0 -> `FD3DDeviceHandle_SetRes` 0x10006850: picks the nearest
  fullscreen mode, calls the viewport's `ResizeViewport(fullscreen | BLIT_Direct3D, w, h)` (vtable `+0x88`) FIRST,
  then `FD3D_FillPresentParams` 0x100057E0 and the device. The params (0x38 bytes at handle `+0x3C`): back buffer
  w/h, `A8R8G8B8`, count 1 or 2 (`+0xD8`), DISCARD, `Windowed = (fullscreen == 0)`, refresh 0, interval
  IMMEDIATE (`0x80000000`) or ONE by the client's VSync.
- `FD3DDevice_InitD3DDevice` D3DDrv9!0x10002180: copies the params and calls `CreateDevice(adapter, HAL,
  hDeviceWindow, 0x44, ...)`, retrying on DEVICELOST. `ChangeDisplaySettingsW` (D3DDrv9's only one,
  `FD3DDevice_CreateDeviceStereoFallback` 0x10002040) runs only on the AMD HD3D stereo path: no desktop mode change
  otherwise.
- `UWindowsViewport::ResizeViewport` Engine.dll!0x102BB410: entering fullscreen saves the window rect (`+0x2E8`),
  `SetWindowLong(GWL_STYLE, style & 0xFF3FFFFF)` (clears WS_CAPTION only) and registers Alt/Ctrl+Esc/Tab/
  Space hotkeys; it does not move the window (exclusive d3d9 does). Leaving: `SetWindowLong(style | 0xC00000)` and
  `MoveWindow` back to the saved rect. `SetTopness` 0x102B87A0 is a NOMOVE|NOSIZE `SetWindowPos`.
- `UWindowsViewport::ViewportWndProc` Engine.dll!0x102BC410, fullscreen only: `WM_SIZE` restored ->
  `MoveWindow(saved rect)`; **`WM_ACTIVATE` inactive -> unless `SteamUtils()->IsOverlayEnabled()` (ISteamUtils
  `+0x44`) or a load holds input: `EndFullscreen`, `+0x150 = 1`, `ShowWindow(SW_MINIMIZE)`**; `WM_ACTIVATEAPP(0)` ->
  the same `EndFullscreen` without the minimise. A later `WM_SIZE` restored clears `+0x150` and toggles fullscreen.
- Engine.dll's USER32 import slots (RVA): SetWindowLongW `0x41E65C`, SetWindowLongA `0x41E660`, SetWindowPos
  `0x41E6A4`, MoveWindow `0x41E6D8`, ShowWindow `0x41E70C`; `WWindow::MoveWindow` 0x100982B0 calls the slot.

## What the panel does (src/nukepanel/display.h/.cpp, d3dcap.cpp)

- Launch: `display_boot` (first winmm call, before the device) peeks `[display] borderless` from nukepanel.ini
  with kernel32 only (`config_peek_borderless`); the value holds for the session. When on, it patches the five
  slots above.
- CreateDevice and every Reset: a fullscreen request goes to d3d9 as a copy with `Windowed = TRUE`, refresh 0, the
  size kept (`bl_rewrite_pp`); d3d9's write-backs (size, format, count) are copied to the engine's struct, never our
  Windowed. Only INVALIDCALL/NOTAVAILABLE fall back to the engine's exclusive params (`bl_fallback`); DEVICELOST
  and other failures go back to the engine unchanged (it retries DEVICELOST itself). Before the call the device window becomes `0x90000000` / ex 0 over the monitor holding most of it
  (`bl_pick_monitor`); no off-by-one row (that was GLDirect's trap on the Manhattan Project port; d3d9 windowed has no such test). A
  refused windowed device is retried with the engine's params (exclusive, as without the panel).
- The import-slot hooks act only on that window and only while active: an engine style write without a caption
  becomes ours (`bl_style_rule`/`bl_popup_style`), ex style 0, SetWindowPos/MoveWindow pinned to the monitor
  (`bl_fix_pos`); a style write that adds the caption back is the engine leaving fullscreen (Alt+Enter, or focus
  loss with the overlay off): the window gets back the style and ex style saved before our first restyle, plus
  the caption (`bl_leave_style`), and borderless idles until the engine next asks for a fullscreen device.
  ShowWindow only logs minimises.
- Not in front (`GetForegroundWindow() != game window`, borderless on, panel closed): the engine's WM_MOUSEMOVE is
  answered by DefWindowProc in our subclass, and its SetCursorPos and ClipCursor(rect) through the cursor gate's
  slots return TRUE unapplied (`bl_bg_hold`, `bl_bg_hold_clip`, test 0164). ClipCursor(NULL), the release, passes.
- Log lines: `display: borderless=N at launch`, `display: <call> params: ... -> windowed 1 refresh 0`,
  `display: before|after: window ...`, `display: monitor i of n ...`, `display: <call>: windowed device ok`,
  `display: Steam overlay enabled=N`, and each undo with a running count (`engine style|ex style|SetWindowPos|
  MoveWindow ... (#n)`, the first 16 then every 256th).

## Focus loss: the cursor pinned at the screen centre

Symptom: with borderless on, alt-tab to a program on the second monitor leaves the cursor locked at the
centre of the game's monitor.

- **The engine sees the overlay as on at every alt-tab.** The log's `Steam overlay enabled=0` is asked at the first
  CreateDevice, while Steam's overlay is still loading (the same log:
  `CreateDevice body ... is in gameoverlayrenderer.dll`); Steam's `IsOverlayEnabled` answers false until the overlay has hooked the game. At the
  alt-tabs the engine's line reads `focus: WM_ACTIVATE 0 ... overlay=1`, and no `the engine leaves fullscreen` line
  and no `engine ShowWindow(6)` line follow, so `EndFullscreen` and the minimise do not run: the engine's branch reads
  the overlay as on (the startup `overlay enabled=0` line is read while the overlay is still loading). It keeps its fullscreen flag. Our hold took `mousemoves_held=113` in the measured session, and a
  free cursor was seen on the second monitor.
- **WM_ACTIVATE inactive** (`ViewportWndProc` Engine.dll!0x102BC410): `+0x7c(0)`; `Viewport_FocusPause(actor, 1)`
  (below); then, only when the overlay is off, `EndFullscreen` and `ShowWindow(SW_MINIMIZE)`; always
  `SetMouseCapture(0,0,0)` (vtable `+0xB0`, vtable at Engine.dll!0x104A6B44) and `FocusChanged(0)` 0x102B9A60, which
  calls `SetMouseCapture(0,0,0)` again. With the DirectInput mouse that is `AcquireDIMouse(0)` (`+0x2DC = 0`),
  `ShowCursor(1)` until visible, `ClipCursor(NULL)`: the engine releases the cursor itself.
- **WM_MOUSEMOVE** (same function): while the DI mouse is acquired (`+0x158 && +0x2DC`) it goes to DefWindowProc.
  Released, with the fullscreen flag (`+0x148` bit 0) or a saved cursor (`+0x2E0 != -1`), and the client's input
  mode (`[client+0x28]` vtable `+0xB8`) not 1, it queues the offset from the client centre as MouseX/MouseY
  (`QueueInputEvent 0xE4/0xE5`) and calls **`SetCursorPos(centre)`** (Engine.dll!0x102BD7FC, one of the three
  SetCursorPos calls in Engine.dll; the other two are in `SetMouseCapture`).
- **Why only borderless:** exclusive d3d9 minimises its window on focus loss, so no WM_MOUSEMOVE reaches the engine.
  Our windowed device keeps the window visible over the whole monitor, the fullscreen flag stays set, and each
  move over that monitor is recentred before the cursor reaches the edge.
- **Regaining focus:** WM_ACTIVATE active -> `Viewport_FocusPause(actor, 0)` -> `FocusChanged(1)`: `SETPAUSE 0`, and
  with the fullscreen flag `SetMouseCapture(1,1,0)`: `SetForegroundWindow`, Acquire, `ClipCursor(client rect)`.
  The window is in front by then, so the hold above lets it through.
- **Fix:** the hold under "What the panel does". Instrument (always on): `focus: game window in front|not in front
  (#n); foreground <hwnd> class '<c>' process '<exe>'; engine while not in front: setcursorpos= (held) clipcursor=
  (held) setcapture= mousemoves_held=; totals ...; borderless= overlay= panel=` at each change and every 600 frames
  when the engine made such calls; `focus: WM_ACTIVATE|WM_ACTIVATEAPP n (#n) ... overlay=` before the engine sees the
  message (the overlay state its branch reads). With the Steam overlay on, the alt-tab lines read `overlay=1` and show held counts.

## Pausing on focus loss (map for "Keep the game running when alt-tabbed"; no code)

- `Viewport_FocusPause(actor, pause)` Engine.dll!0x102BB250: called from WM_ACTIVATE (both ways)
  and WM_ACTIVATEAPP(0) when `!GIsEditor` and the byte `[[viewport+0x30]+0x7C]+0x918` is 0 (that byte is `LevelInfo.NetMode`: standalone only, qol.md). Pause:
  `APlayerPawn::eventSetPause(L"__MAINMENU", 1)` and bit 0 of `[actor+0x7C]+0x85C`; resume undoes it. Skipped while
  `g_LoadingOnThread` or `g_IgnoreFocusChanges` (an imported global, Engine.dll slot 0x1041EFA0) is set. Beside it
  the audio subsystem (`[client+0x28]+0x4C`) gets vtable `+0x64` and `+0xD8(0.0)` (volume), `+0xD8(1.0)` on return.
- `FocusChanged(0)` 0x102B9A60 also executes the console command `SETPAUSE 1` (when `GIsRunning`), `SETPAUSE 0` on
  return. Completed map and levers: docs/re/qol.md section 1 (`SETPAUSE` has no handler; `+0x918` is NetMode, not a
  lever; the lever is `g_IgnoreFocusChanges` plus the master volume).
- **Incomplete as a single switch:** a minimise also pauses (`WM_SIZE`); `WM_KILLFOCUS` reaches
  `FocusChanged(0)` on its own route; `g_IgnoreFocusChanges` does not stop `FocusChanged(0)`'s `SETPAUSE 1`; and
  `FocusChanged(0)` stops `UpdateInput`. "Keep running" therefore needs all four paths handled, not one data write.
- The focus-loss skip conditions also include `[viewport+0x150]` and `g_LoadingInputLock`; on return the mouse is
  re-taken twice, from `WM_ACTIVATE` and again from `WM_SETFOCUS` (`FocusChanged(1)`), the second with the game in
  front.

## Windowed at launch: StartupFullscreen=False

When `StartupFullscreen=False` is in system.ini the game starts in a 1280x720 captioned window.

- **Evidence (Steam's own logs, `logs/gameprocess_log.txt` and `cloud_log.txt`):** a session that exited normally
  (exit code 0) uploaded system.ini at its exit at 8440 bytes (UTF-16 `False`; `True` is 8438). A session that started
  windowed wrote `True` 6 s in: its window opened windowed (`before: ... at 104,104 1286x749, client 1280x720`) and
  then asked a fullscreen device (`CreateDevice params ... windowed 0 -> windowed 1`). Only BeginFullscreen writes 1,
  and nothing but a key or a command reaches it at startup: most likely a manual Alt+Enter.
- **Every writer (Engine.dll):** EndFullscreen 0x102B9ED0 writes 0 only while the viewport's
  fullscreen bit (`+0x148` bit 0) is set; callers: BeginFullscreen (its SetRes failed), `Exec` 0x102BBC50 (console
  `ENDFULLSCREEN`/`TOGGLEFULLSCREEN`), ToggleFullscreen 0x102BB3E0, and ViewportWndProc 0x102BC410 from:
  `WM_ACTIVATE` inactive and `WM_ACTIVATEAPP(0)` (skipped while `+0x150`, `g_LoadingInputLock`, or `SteamUtils()` non-null
  with `IsOverlayEnabled`), and **`WM_DESTROY` while fullscreen, unconditionally**. `WM_CLOSE` and `SC_CLOSE` are not
  handled: DefWindowProc destroys the window. Alt+F4 (`WM_SYSKEYDOWN` VK_F4) is `PostQuitMessage(0)`, or
  `DestroyWindow` while `g_LoadingOnThread`. `UViewport::Destroy` 0x102150C0 calls `CloseWindow` first.
- **Excluded by measurement:** focus loss in play. Every `focus: WM_ACTIVATE|WM_ACTIVATEAPP 0` line (in the alt-tab launch and
  the windowed-start session) reads `overlay=1`, and no `the engine leaves fullscreen` line follows. A normal quit: the windowed-start session quit from a
  level (its exit `WM_ACTIVATE 0` reads `overlay=-1`, Steam already shut down) and system.ini kept the mtime of its `True` write.
- **Not panel code:** the subclass forwards every activation, size, close and destroy message unchanged; Keep running
  (`g_IgnoreFocusChanges`) only gates `Viewport_FocusPause`, not the EndFullscreen branch; freecam sends no messages.
- **Remaining paths (each stock):** the window closed by `WM_CLOSE` (taskbar "Close window") while a level runs;
  Alt+F4 during a level load; a console/script `ENDFULLSCREEN`; Alt+Enter with a failed SetRes.
- **Instrument (always on, display.cpp + input.cpp, rule `fs_watch_msg`, test 0464):** the live client's
  `StartupFullscreen` (`GEngine.Client`, both by reflection) is read before and after the engine handles each message
  that can reach Begin/EndFullscreen; a change, and every `WM_CLOSE`/`SC_CLOSE`/`WM_DESTROY`, logs
  `display: StartupFullscreen ended (the next launch starts windowed) (1 -> 0) in the engine's WM_xxx 0x.. (#n); in
  front= iconic= overlay= borderless active= loading=`. Every 60 frames a change made between messages logs `between
  window messages (a console command or script)`. At the window's destroy: `display: at window destroy:
  StartupFullscreen N (what the next launch starts with); the engine's last device ...`. Start-up:
  `display: StartupFullscreen watch on: Engine.Client +0x.., StartupFullscreen +0x.. mask 0x.., now N`.
- **Restoring fullscreen:** Alt+Enter. `WM_SYSKEYDOWN` VK_RETURN with Alt down -> ToggleFullscreen -> BeginFullscreen,
  which writes `StartupFullscreen 1` (unless `[[viewport+0x30]+0x1730] & 0x80`, not read). The subclass passes it
  while the panel is shut, no free camera flight is on and no bind takes Enter; borderless then takes the fullscreen device (as in the windowed-start session above).

## Open questions

- The Steam-overlay-off focus path (engine ends fullscreen and minimises) has never been launched: the
  `focus: WM_ACTIVATE 0 ... overlay=` line settles which path a session takes.
- Which of the remaining stock paths wrote `StartupFullscreen=False` in the session that exited normally is unknown, so no
  behaviour fix was made.
- Whether `[[viewport+0x30]+0x1730] & 0x80` blocks Alt+Enter's `StartupFullscreen 1` write (not read).
- The engine still registers its Alt+Tab/Alt+Esc hotkeys in "fullscreen"; alt-tab was seen to work with them.
- Brightness: no gamma strings in D3DDrv9; if the game used `SetGammaRamp` it would be ignored in a windowed device.

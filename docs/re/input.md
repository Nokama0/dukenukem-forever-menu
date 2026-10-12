# Input: how the game reads the mouse and keyboard

The mouse is an **exclusive DirectInput device**, so the game window receives no mouse messages; the keyboard comes
through window messages and `GetKeyState`. The engine releases and re-takes the mouse itself through
`UWindowsViewport::SetMouseCapture`, but that code is CEG-checked (docs/re/ceg.md): the panel releases the
DirectInput device itself, through dinput8.dll, and asks dinput8 for a shared mouse instead of the engine's exclusive
one (section "Click lock after alt-tab"). Method: the panel's log from a launch, and Engine.dll in Ghidra.

## Measured (System\NukePanel\nukepanel.log, a panel launch)

- With the panel open over the main menu for ~5,000 frames: `wm_mousemove=0 wm_input=0`, `getcursorpos_passed=0`,
  `getkeystate=249 per frame`. Insert (WM_KEYDOWN) toggled the panel through our WndProc: keyboard messages arrive,
  mouse messages do not, so the panel could not be clicked.

## Read from Engine.dll (Ghidra project dnf, names saved)

- `UWindowsViewport::diSetupKeyboardMouse` `0x102B8B80`: `CoCreateInstance` of the DirectInput device (no dinput
  import: COM through ole32), `SetDataFormat` (mouse), **`SetCooperativeLevel(viewport hwnd, 5)` = EXCLUSIVE |
  FOREGROUND**. Called from `OpenWindow` `0x102BC090` when the client flag `+0x11C` is set. Device at viewport
  `+0x158`. An exclusive DI mouse stops legacy WM_MOUSE* messages to the app (the Manhattan Project port's click-lock mechanism).
- `UWindowsViewport::AcquireDIMouse(int)` `0x102B8AD0`: sets the acquired flag `+0x2DC`; 0 = Unacquire.
- `UWindowsViewport::SetMouseCapture(int capture, int clip, int focusOnly)` `0x102B9460` (virtual, exported):
  capture 0 = `AcquireDIMouse(0)`, `ShowCursor(1)` until visible, `SetCursor`, `ClipCursor(NULL)`; capture 1 =
  `SetForegroundWindow`, `ShowCursor(0)`, `SetCursor(0)`, Acquire, `ClipCursor(client rect)`.
- `UWindowsViewport::UpdateInput(int)` `0x102BA070`: reads the DI mouse only while `+0x2DC != 0` (re-acquires on
  DIERR_INPUTLOST/NOTACQUIRED); then polls all 256 keys through the vtable's `GetInputKeyState` (`GetKeyState`).
  So after `SetMouseCapture(0,..)` the engine stops reading the mouse until something calls `SetMouseCapture(1,..)`.

## Consequence for the panel

A `SetMouseCapture` detour makes Engine.dll's Steam CEG kill the game at launch (docs/re/ceg.md), so the panel works
through dinput8 (`src/nukepanel/dimouse.cpp`, rule `dimouse.h`, tests 0071 and 0076): Engine.dll's ole32
`CoCreateInstance` import slot records each `CLSID_DirectInputDevice8` it creates; the first device's vtable gives
dinput8.dll's `Acquire`/`Unacquire`/`GetDeviceState`/`Release` bodies (slots 7/8/9/2), detoured in dinput8.dll;
the tracked device's last Release forgets it. The device is
classified once (GetDeviceInfo dwDevType 0x12, else GetDeviceState's size 0x10/0x14). Open asks the device (a real GetDeviceState:
DI_OK = acquired, NOTACQUIRED/INPUTLOST = not; this beats a stale recorded intent) and calls the real Unacquire
when it is acquired; while open the engine's Acquire/Unacquire are recorded and return DI_OK and
its GetDeviceState reads zeros with DI_OK (so UpdateInput neither re-acquires nor re-creates); close calls the real
Acquire when the engine still wants it. Log lines `dimouse: ...`; the input line carries `di_acquire_held=` and
`di_getstate_zeroed=`. Observed in game: clicks work, mouse look returns, the cursor hides after close.

## Click lock after alt-tab (tests 0186-0188)

Cause (confirmed on the sibling Manhattan Project port): dinput8's exclusive mouse registers the raw-input row
`NOLEGACY|CAPTUREMOUSE` (0x230) asynchronously, and focus leaving mid-change leaves clicks dead in every other window
until Ctrl+Alt+Del. Here the engine asks the same exclusive level (`SetCooperativeLevel(hwnd, 5)`, above). The
fix is a shared mouse. The `dimouse: raw rows change N` line shows the rows in this game.

- **Instrument:** `dimouse: raw rows change N: mouse <row>; keyboard <row>` from the first tracked device on,
  polled once a frame, logged on change (`flags 0x230 NOLEGACY|CAPTUREMOUSE`, `flags 0x100 INPUTSINK`, `none
  registered`). The level line carries the rows before and after.
- **Shared mouse:** dinput8's SetCooperativeLevel body (slot 13, vtable `+0x34`) is detoured beside the other four
  (dimouse.cpp, own install: a refusal leaves the panel's release working). For the classified engine mouse
  `EXCLUSIVE` becomes `NONEXCLUSIVE`, FOREGROUND kept (`cl_coop_level`): `dimouse: mouse <dev> window <hwnd>: level
  0x05 -> 0x06 set`, once per device and window. The engine calls it in `diSetupKeyboardMouse` 0x102B8B80 right
  after Initialize and before any Acquire, so no Unacquire/Acquire sequence is needed. A refused level 6 is retried
  with the engine's 5.
- **What the engine does with a shared mouse (read in Engine.dll):**
  - `UpdateInput` 0x102BA070: while its mouse is acquired it skips VK 1, 2 and 4 in its `GetKeyState` poll, so
    the buttons come from DirectInput only.
  - `ViewportWndProc` 0x102BC410: `WM_MOUSEMOVE` goes to DefWindowProc while acquired (`+0x2DC`), but
    `WM_[LRM]BUTTONDOWN/UP` and `WM_MOUSEWHEEL` queue `QueueInputEvent` presses whatever the mouse mode. Duplicate
    button presses are dropped by `UInput::PreProcess` 0x10232190 (press only when up, release only when down);
    a wheel notch is a full press/release pair from each source, so it would switch weapons twice.
  - `SetMouseCapture(1)` 0x102B9460: `ShowCursor(0)` once, `SetCursor(0)`, Acquire, `ClipCursor(client)`.
    `SetMouseCapture(0)`: Unacquire (only when `GetDeviceState` succeeds), `ShowCursor(1)` until visible, its
    shape, `ClipCursor(NULL)`. `SetDrag` (vtable `+0x7C`, `UViewport::SetDrag` 0x10212400) only notifies the
    client: no capture.
- **Cursor hold** (input.cpp, rule `cl_hold_block`): while the window is in front and activated, not minimised,
  the panel closed, the engine's last DirectInput call was Acquire (`dimouse_engine_wants`) and the OS cursor is
  not showing: `WM_SETCURSOR` over the client answers `SetCursor(NULL)`, the cursor goes back to the client centre
  each frame unless already there (our own SetCursorPos import, not Engine's gated slot), and every `WM_MOUSE*`
  (0x200-0x20E) is answered by DefWindowProc before the engine (`cl_swallows`), as under the exclusive mouse.
  Released at once on WM_ACTIVATE inactive, WM_ACTIVATEAPP 0, WM_KILLFOCUS, the panel opening and a window
  destroyed or replaced. No ClipCursor and no ShowCursor of ours. Log: `input: cursor hold on|off (<reason>); on N
  time(s), M warp(s), K mouse message(s) kept from the engine so far`.
- **Yields:** the engine's release (`SetMouseCapture(0)`: its windowed and menu cursor, window-message input)
  shows the cursor and unacquires, either of which stops the hold; the panel; any focus loss. Borderless and Keep
  running act only while not in front (`bl_bg_hold`, the engine's own focus path), where the hold is off.
- **Expected in play:** `level 0x05 -> 0x06 set`, the mouse row `0x100 INPUTSINK` (never 0x230), `cursor hold off
  (focus lost)` at each alt-tab; clicks work in other programs after alt-tab, Start and quit, with no Ctrl+Alt+Del;
  mouse look, firing and the wheel (one weapon per notch) and the menus' pointer as under the exclusive mouse.

## Cursor left showing after alt-tab (tests 0465-0466)

Cause read from Engine.dll and matched to the log; the fix keeps the cursor from staying visible in play.
Symptom: the Windows cursor appeared over the game and stayed. The log read `cursor hold on` at the start
(flags 0x0, the count below 0), `cursor hold off (focus lost)` at an alt-tab to SearchHost.exe, focus back
(`WM_ACTIVATEAPP 1`, `WM_ACTIVATE 2`), no `cursor hold on` again, and `cursor count 2` at the next panel open with
the engine still holding its mouse (`dimouse: open: GetDeviceState hr=0x00000000 (recorded wants 1)`).

- **The engine's ShowCursor counting is unbalanced.** `SetMouseCapture(0)` Engine.dll!0x102B9460 runs
  `do ShowCursor(1) while (count < 0)` (DirectInput path 0x102B9650, other 0x102B96F0): at least one call, so every
  release with the cursor already visible adds one. `SetMouseCapture(1)` calls `ShowCursor(0)` once (0x102B9518).
  Engine.dll's other ShowCursor is one `ShowCursor(1)` in `ShutdownAfterError` 0x102B7A23 (xrefs to the slot
  0x1041E544).
- **Focus loss releases five times, the return captures twice** (`ViewportWndProc` 0x102BC410, `FocusChanged`
  0x102B9A60): `WM_ACTIVATE` 0 = SetMouseCapture(0) + FocusChanged(0); `WM_ACTIVATEAPP` 0 = FocusChanged(0) always,
  plus SetMouseCapture(0) first with the fullscreen flag (`+0x148` bit 0, set under borderless); `WM_KILLFOCUS` =
  FocusChanged(0).
  `WM_ACTIVATE` 2 and `WM_SETFOCUS` each run FocusChanged(1) = SetMouseCapture(1,1,0). So -1 -> 4 -> 2: the logged
  2. Nothing else in the log calls ShowCursor (the panel's `cursor_count` is a balanced pair; the close replayed 0).
  The pause (`world: paused=1`) and Windows Search play no part.
- **Why it stayed:** the hold yields while the OS cursor shows (`CL_HOLD_CURSOR_SHOWN`), and the engine's own
  `SetCursor(0)` in SetMouseCapture(1) ran while the cursor was still over Search, so the arrow kept its shape. Under
  the stock exclusive mouse the window got no WM_SETCURSOR, so its NULL shape hid the cursor whatever the count.
- **Fix (cursorgate.h `cg_show_redundant`, input.cpp `gate_ShowCursor`):** on Engine.dll's slot a ShowCursor(TRUE)
  that finds the count already >= 0 is undone in the same call (the real TRUE, then one real FALSE) and answered
  with the count before, which ends the engine's loop: "until visible" as the engine means it. Likewise a
  ShowCursor(FALSE) that finds the count already < 0 (SetMouseCapture(1)'s one-shot hide): the
  count floors at -1, so `ShutdownAfterError`'s single ShowCursor(1) still shows the cursor over a crash dialog.
  Open, neither is recorded for close. The return then leaves -1, the hold engages, and a later menu release loops
  to 0.
  dnCommon.dll's slot is only logged (its calls not read). The hold keeps yielding to a showing cursor (the Steam
  overlay shows one in front of the game with the engine still holding its mouse).
- **Instruments (stay):** `cursor: ShowCursor(n) from <module>+0x<off>: count a -> b[ (already visible|hidden:
  not counted)| (panel open: held for close)]` for every call through the slots (first 128, then every 64th);
  `dimouse: engine Acquire|Unacquire from <module>+0x<off>: wants its mouse a -> b` on each change of the engine's
  intent (first 64, then every 64th); `input: cursor hold kept off 5 s in play (<reason>): ...` once per stretch
  blocked by the engine's release or a showing cursor while in front with the panel shut (first 32, then every 32nd).
- **Expected at an alt-tab:** five releases from Engine.dll+0x2b9654 of which four `not counted`, two
  `ShowCursor(0)` from Engine.dll+0x2b951e on the return (the second `not counted`), `cursor hold on` right after,
  and no `kept off 5 s` line in play.

## Open questions

- Whether the `dimouse: raw rows change N` line ever shows the 0x230 row with the shared mouse (expected: never).
- The in-game result of the cursor fix: the log lines expected at an alt-tab are listed above.

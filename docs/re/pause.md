# Pausing without the pause menu (for the free camera)

The free camera pauses the game by calling `PlayerPawn.ForcePause_NoFade()` on the player by ProcessEvent (no
parameters) and `PlayerPawn.ForceUnPause()` to undo it: the game's own pause without the HUD dim. It stops every actor
tick, game time and animation time, pauses sound and movies and snapshots the game's input; rendering goes on and
players still get input. No SaveConfig is on the path. The pause menu's own unpause ends it, so the free camera must
own that case.

Method: as docs/re/player-options.md (`tools/ubc.py` on Engine.u; Ghidra MCP on Engine.dll, plate comments saved).
The facts below are read from the bytecode and Engine.dll.

## The script path

- `PlayerPawn.ForcePause_NoFade()` = `SetPause(true, "", true)`; `ForceUnPause()` = `SetPause(false)`;
  `ForcePause()` = `SetPause(true)` (dims). `PlayerPawn.Pause` (exec) toggles with `SetPause(!Level.bPaused)`.
- `PlayerPawn.SetPause(bool bPause, optional string Pauser, optional bool bSkipFade)`: `Level.Game.SetPause(bPause,
  self, Pauser, bSkipFade)`; if that succeeded: when pausing with bSkipFade while `bIFire`, `FireUp()`; when locally
  controlled, `PauseGameInput(bPause)`.
- `GameInfo.SetPause`: returns false when `Level.bPaused` already equals `bPause`; needs `bPauseable` or
  standalone. Pause: `Pauser` (or the player's name) into `Level.Pauser`, `P.Paused(bSkipFade)` (HUD
  `DimBackground` unless bSkipFade), `Level.bPaused = true`, `Level.NoteGamePaused()`. Resume: `P.Unpaused()`
  (`UnDimBackground`), `bPaused = false`, `Pauser = ""`, `NoteGameResumed()`.
- Overrides: `DukeChangeroomPlayer.SetPause` returns false (no pause in that level) and so does `DukeIntro.SetPause`;
  `DukeMyDigsPlayer.SetPause` refuses an unpause while quitting the Digs and clears two console flags, neither config.
- The pause menu (`WindowConsole.LaunchUWindow`) pauses with `SetPause(true, "__MAINMENU")` and
  `CloseUWindow` unpauses with `SetPause(false)`: opening the menu during our pause is a no-op pause (already
  paused), closing it unpauses.
- No SaveConfig/GConfig anywhere on these paths; `bPaused` (flags 0x800020), `Pauser` (0x400020) and
  `bPlayerPausedGame` (0) are not config.

## What freezes (natives)

- `ULevel::Tick` `Engine.dll!0x10241F00`: paused = `LevelInfo+0x85C` bit 1 (`bPaused`) or, outside the editor,
  `+0x794` or `+0x7A0` set (a level-enter movie, the Bink texture `NoteGamePaused` pauses). While paused: no
  `TickActor`, game time (`TimeSeconds`) and the animation clock stop; each player with a viewport gets its
  input read (`Player` vt+0x5C), `APlayerPawn::PlayerInput` and rumble only (no PlayerTick, so the view does not
  turn); actors with byte `+0x250` equal to 1 or 4 get vt+0x128 (a paused tick); deferred mounts and queued
  actor events still run.
- Rendering is not in the tick (`UGameEngine::Draw`, engine-map.md section 5): the world keeps drawing, as behind
  the pause menu.
- Sound and movies: `ALevelInfo::execNoteGamePaused` `0x102018B0` calls the audio subsystem
  (`XLevel+0x40 -> +0x4C`, vt+0x64), every entry of `LevelInfo+0x8CC` (vt+0x5C) and pauses the Bink texture;
  `execNoteGameResumed` `0x10201950` mirrors it.
- Input: `UInput::PauseGameInput` `0x102325D0` copies the key-state table on pause; on resume it replays every key
  whose state changed during the pause as a press or release.

## For the free camera

- Engage: `ForcePause_NoFade()`; release: `ForceUnPause()`, only if we paused (check `Level.bPaused` first:
  pausing an already paused game returns false and we must not unpause what we did not pause).
- The pause menu closing ends our pause; re-pause or leave the camera then.
- `world_gate` (world.h) hides the ESP while paused; the free camera's pause needs its own exemption.
- Our camera is ours to move: PlayerPawn view rotation is frozen while paused.

# Player options: field of view, game speed, perfect accuracy (build 244)

Each option has a path that writes no config property and calls no SaveConfig. FOV, game speed and perfect accuracy
are verified in game, and the ini files stay unchanged. **FOV:** the game's own `'ConsoleFOV'` scale modifier,
`Actor.SetScaleModifier('FOV', 'ConsoleFOV', want / DefaultFOV)` by ProcessEvent on the player, cleared with
`ClearScaleModifier('FOV', 'ConsoleFOV')`; never `DefaultFOV` (a config property the quit path saves).
**Game speed:** write `PlayerPawn.DebugSlomo` (transient; what the gated `SloMo` cheat writes); the engine's
per-tick `UpdateGameSpeed` turns it into `TimeDilation`. A direct `TimeDilation` write lasts one tick.
**Perfect accuracy:** `GlobalSetProperty("0", cls, prop, offset, bAllInstances=0, bNoSave=1)` on
`HorizShotError`, `VertShotError`, `ShrunkHorizShotError`, `ShrunkVertShotError` of the player's weapons'
`WeaponConfig` classes only (CDO only; the AI variants are separate classes).

Method: `extracted-packages/*.u` decoded with `tools/ubc.py` (package reader plus a bytecode decoder; 15,451 of
15,472 script functions decode end to end, 13,719 of 13,910 before the switch-token fix). Token formats checked in Engine.dll: `EX_Context` is expr, u16 skip,
u16 size, expr (`execContext` 0x100197B0); natives start at token 0x90, 0x80-0x8F are the high-native prefixes
(`execHighNative0` registered at 0x80, 0x103E3A25); DNF adds 0x60 LetDelegate, 0x61-0x6F compact constants
(0x64 IntConstWord), 0x66 DynArrayEmpty. Natives named from each UFunction's iNative (FMax = 0xE1, SaveConfig =
0x170). Property flags from each property export (`CPF_Config` 0x4000). Ghidra MCP for the natives (plate
comments saved).

## Field of view

- `PlayerPawn.FOV(int View, float Time)` (exec, flags 0x00020202): `if (!CanCheat()) return; if (View <= 0)
  View = int(DefaultFOV); SetScaleModifier('FOV', 'ConsoleFOV', float(View) / DefaultFOV, 0, Time)`. `CanCheat`
  is `bCheatsEnabled && (bAdmin || standalone)`, so calling the exec does nothing in a normal game: call
  `SetScaleModifier` directly. (An "open exec" for FOV would be a mistake: it is CanCheat-gated.)
- `Actor.SetScaleModifier` is native 577 (flags 0x00220401). Parameters (all from the export): Name
  ScaleGroupIdentifier, Name ScaleIdentifier, float ScaleTarget, optional float ScaleStartOverride, float
  ScaleRampTime, byte ScaleStyle, Name ScaleFunctionComplete, Name ScaleEventComplete, Actor
  ScaleFunctionCompleteActor, float Priority. Zero for every optional one is what the exec passes.
- `AActor::SetScaleModifier` `Engine.dll!0x10205310`: finds or adds the entry in the actor's group array; with
  ramp time <= 0 sets current = target at once. Writes only that array: no GConfig, no SaveConfig.
  `AActor::ClearScaleModifier` `0x10202E60` marks the entry inactive and resets it to the group default.
- `APlayerPawn::CalculateFOV` `0x10268410`: with no camera actor (`+0x12A0`), FOV = lerp(+0x1728, +0x172C,
  sprint alpha) (steroids-scaled) x `CalcScaleModifier('FOV')`, clamped to [1, 179], stored at +0x1274 and
  +0x1724. A camera actor (cutscenes, turrets, camera viewers) supplies its own FOV and ignores modifiers. The game
  uses the same group for drunk, size change, turrets, zoom upgrade, vehicle boost: our modifier multiplies with
  them.
- `PlayerPawn.DefaultFOV` is **CPF_Config (0x4000)**. The game's own FOV option
  (`UDukeSceneVideoAdvancedOptions.defaultFOVSetting`) writes `User.ini` itself (`SetConfigInt('Engine.PlayerPawn',
  'DefaultFOV', ..)`, `SaveConfigFile`), and `PlayerPawn.PostBeginPlay` / `DukePlayer.PreGameInit` read it into
  `DefaultFOV` at level start. The quit path saves the player's config (`UWindowRootWindow.DoQuitGame`:
  `Console.Viewport.Actor.SaveConfig()`), so a written `DefaultFOV` would reach `User.ini`. Never write it.
- `CurrentFOV` (flags 0) is what the ESP reads; it already includes the modifier.
- **Path:** apply = `SetScaleModifier('FOV', 'ConsoleFOV', want / DefaultFOV, 0, 0)` with the current
  `DefaultFOV` (the ratio is relative to the game's own value, so a changed game option is followed on the next
  apply); off = `ClearScaleModifier('FOV', 'ConsoleFOV')`. The modifier lives on the pawn and **is saved with it** (`Actor.ScaleModifierGroupList` is not transient): a
  checkpoint reload brings it back, which is why off always clears it and why, with the option off, a carried
  `ConsoleFOV` is cleared once per level. A save made with Custom FOV on keeps that FOV if loaded without the panel. `'ConsoleFOV'` exists in the name table (the bytecode names it), so no name is
  created. **Reaches SaveConfig: no.**

## Game speed

- `GameInfo.SetGameSpeed(float t)`: `GameSpeed = FMax(t, 0.01); Level.TimeDilation = GameSpeed`. No SaveConfig.
  `GameSpeed` flags 0x1 and `TimeDilation` flags 0x21: neither is config. (dnDeathmatchGame overrides it, MP.)
- `PlayerPawn.UpdateGameSpeed` (event, flags 0x00020903), standalone only: `Level.Game.SetGameSpeed(
  [GameSpeedModifier x] DebugSlomo x CalcScaleModifier('TimeDilation', 3))` (GameSpeedModifier is dropped while
  dead). **`APlayerPawn::UpdatePawnTimers` `0x102109A0` raises it every tick** (dnName `0x107E4458`), so a
  direct `TimeDilation` write is overwritten on the next tick.
- `PlayerPawn.DebugSlomo` flags 0x2000 (transient, not config). Written only by `PreBeginPlay` and
  `TravelPostAccept` (= 1) and `ServerSetSloMo` (the gated `SloMo` cheat: `if (!CanCheat()) return; DebugSlomo =
  t`). `LevelInfo.GameSpeedModifier` is the Extras menu's Game Speed (`UDukeSceneExtraSettings.NotifyFromControl`:
  `ConsoleCommand("set LevelInfo GameSpeedModifier " $ value)` then `UpdateGameSpeed()`); leave it to the game.
- `ULevel::Tick` `0x10241F00` scales the frame delta by `TimeDilation` (LevelInfo +0x6CC) after clamping the
  delta to [0.001, 0.059]; the frame limiter is outside the tick, so the frame rate is unaffected.
- **Path:** remember `DebugSlomo`, write `DebugSlomo = pct / 100` (0.10..4.00) on the player; the next tick
  applies it through the game's own SetGameSpeed and it multiplies with the game's Extras speed. Off: write back the
  remembered value (1 after any level start). `PreBeginPlay`/`TravelPostAccept` reset it to 1 on a new level:
  re-apply. Optionally call `UpdateGameSpeed()` (no parameters) by ProcessEvent to apply in the same frame.
  **Reaches SaveConfig: no** (the quit path's `Level.Game.SaveConfig()` saves only config properties; none is
  touched).

### Game speed pitch-shifts the audio

Read in Ghidra, with MilesAudio.dll loaded into the project. The audio multiplies every sample's playback rate by
`LevelInfo.TimeDilation`. That is the field that speeds the game
(`ULevel::Tick` multiplies the clamped delta by +0x6CC at 0x1024214A), so no other speed property avoids it. The
lever is **MilesAudio.dll's import slot for mss32 `AIL_set_sample_playback_rate` (0x100104C8)**. Divide the rate by
our own factor (the `DebugSlomo` we set). The game's own dilation (Extras speed, scripted slow motion) keeps its
designed pitch.

- `Sample_UpdateParams` MilesAudio!0x1000CAF0 (renamed) runs per frame per sample, from FUN_1000D6A0, from
  `UMilesAudioSubsystem::Update` 0x100060E0. It computes the dilation: `LevelInfo.TimeDilation`, reached through
  Level, XLevel (+0x6C8) and `Actors[0]` (+0x2C), read at 0x1000D0F1. When the sound's flags (+0x20) have bit 0x80000
  (`SSoundFlags.bIgnoreTimeDilation`) the dilation is 1.0 instead. Then `rate = SampleRate(+0x140) x (pitch +0x48 +
  bend +0x2C) x dilation`, and `AIL_set_sample_playback_rate(HSAMPLE +0x14C, rate)` is called through slot 0x100104C8
  (call at 0x1000D45C; ESI is the sound there).
- Music streams: `UpdateStream` 0x1000B066 does the same unless the stream's +0x2C is set
  (`AmbientMusicIgnoreTimeDilation`, `MusicTrigger.IgnoreTimeDilation`). It uses the same slot (call 0x1000B17D).
  The other slot callers are sample start FUN_1000BFC0 (0x1000C1B0) and `InitializeVoIPSamples` (0x10005B8F, MP).
- `Update` also scales its fade clock by TimeDilation. Fades follow game time; leave that.
- MilesAudio.dll carries neither CEG fingerprint (`81 E1 00 F0 FF FF`, `83 C6 FF 33 D2 F7 F6`: no hits;
  modules-ceg.md), and a slot write is not code anyway.
- Rejected:
  - Setting bit 0x80000 in each live sound: Miles-internal structs, and every new sound would need it.
  - The `SetSamplePitch` export: per actor slot, and it misses streams.
  - Warping the engine's clock instead of TimeDilation: the frame limiter, the 0.059 s delta clamp and the fades all
    follow it.
  - `PhysicsVolume.ActorTimeScale`: per volume.

**The lever (risk L-M):** replace slot 0x100104C8. While game speed is on, `rate' = rate / f`, with f = the
`DebugSlomo` we wrote (pct/100), for calls returning into the update and stream sites. At the 0x1000D45C site,
sounds whose ESI+0x20 has bit 0x80000 are skipped. Off and other callers pass through unchanged.

**As built (`gamespeed.cpp`):**
- The import is `_AIL_set_sample_playback_rate@8` (stdcall, HSAMPLE and an int rate pushed by both callers).
- Sites:
  - Sample start 0x1000C1B0 passes the raw `SampleRate` (+0x140), not dilated: it passes through.
  - `UpdateStream` holds the stream entry in ESI (`FIMUL [ESI+0x20]`, ignore flag `+0x2C`).
  - A naked stub hands the caller's ESI to the rule.
  - Only the update and stream sites are divided, and only when the sound or stream does not ignore dilation.
  - Music streams are covered by the same slot.
- The factor is our DebugSlomo while it is the value in place, else 1.0. That covers off, 100%, menus, and the frame
  after a level reset.
- Miles' own rate limits were not read (mss32.dll is not in the project). The result is clamped to 1..192 kHz, never
  past the input. Clamps are counted.
- Installed once MilesAudio.dll is loaded; the slot and the original are logged. One line per 600 frames while
  dividing: calls per site, divided, ignore-dilation skips, clamps, and the rate range in and out.

## Perfect accuracy

- `Weapon.GetShotError(out float HorizShotError, out float VertShotError)` (event): reads
  `WeaponConfig.static.default`: `HorizShotError`, `VertShotError`; when `bUseShrunkShotError` and the instigator
  is shrunk, `ShrunkHorizShotError` / `ShrunkVertShotError`; zoomed, both x `ZoomErrorPct`. Only
  `MP_MachineGun` overrides it (multiplayer).
- `AWeapon::PerformFire` `0x100D0260` calls it once per shot, then randomises each trace direction by
  `(rand - 0.5) x H` and `(rand - 0.5) x V` only when H > 0 or V > 0; the first `[config+0x8C]` traces are never
  randomised. Zero both and every trace leaves along the aim (a shotgun fires all pellets to one point).
- All six properties are flags 0x1 (not config). No script SaveConfigs a WeaponConfig class.
- `UObject::GlobalSetProperty` `0x10043400` (cdecl `(const wchar_t* value, UClass*, UProperty*, int offset,
  int bAllInstances, int bNoSave)`): with bAllInstances = 0 it ImportTexts into the class CDO
  (`class+0x508 + offset`) only; `SaveConfig(CDO, 0x4000)` runs only when bNoSave == 0.
- AI weapons use their own config classes (`*_Pigcop`, `*_EDF`, `*_DukeClone`, `*_HoloDuke`, `RPG_AIWeaponConfig`,
  `MiniGun_BLWeaponConfig`; 83 WeaponConfig classes in all), so a CDO write on the player's class leaves them
  alone. `BattleLord_Boss` writes its own minigun config's CDO at run time (AI only).
- **Path:** for the player's weapons (`Pawn.Weapon` / inventory -> `Weapon.WeaponConfig` class), remember the four
  floats, `GlobalSetProperty("0", cls, prop, prop.Offset, 0, 1)`; off writes each remembered value back with a text
  that round-trips the float (`%.9g`). CDOs persist across levels: restore before a level change or keep the record.
  **Reaches SaveConfig: no** (last argument 1, and the properties are not config).
- In game the zeroing works, but the pistol's visible spread is its view kick, not these values
  (docs/re/accuracy.md).

## SaveConfig audit (who saves what)

Script `SaveConfig`/`StaticSaveConfig`/`SaveConfigFile` callers that run in single player: the quit paths
(`UWindowRootWindow.DoQuitGame`: root, console, player; `UDukeRootWindow.DoQuitGame`: LookAndFeel, `Level.Game`,
its GameReplicationInfo), the options scenes (player, `User.ini`), `DukePlayer.ResetHints` (`User.ini`), and the
PlayerPawn setters (`SetSensitivity`, `InvertMouse`, `SetName`, ...). All save only CPF_Config properties: none of
`DebugSlomo`, `GameSpeed`, `TimeDilation`, the scale-modifier arrays or the WeaponConfig spreads is config.
`DefaultFOV` is, and no recommended path writes it.

## Open questions

- Whether Miles clamps extreme playback rates (mss32.dll is not in the project; check at 400 % game speed).
- Whether the pitch correction sounds right in game; the 600-frame line at 50 % and 200 % is the measurement (see "Game speed pitch-shifts the audio").

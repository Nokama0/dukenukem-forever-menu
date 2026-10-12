# Quality-of-life options: background running, raw mouse, no screen shake, post effects (build 244)

All four options have a lever that patches no Engine.dll code and writes no game ini. **Keep running:** set the
engine's own flag `g_IgnoreFocusChanges` (dnCommon.dll export, what `-IGNOREFOCUSCHANGES` sets) and give the sound
back with `SetMasterVolume(1.0)` after each focus-loss message. Ticking and rendering already continue unfocused. The
input stop and the mouse release must stay. **Raw mouse:** there is no time smoothing. The non-raw parts are a
velocity cap, a curve, the 1/30 s turn clamp and int truncation, and `UpdateRotation` multiplies by 7.68. The panel
replaces the per-frame turn in the dnGame ProcessEvent slot on `TriggerViewRotationAdjust`, using counts from our
dinput8 detour. **No screen shake:** every shake is a named `ActiveShakes` entry. Neutralise every entry not named
`*_ViewKick_*` just before `UpdateViewShake`. The sprint shake left over is the eye-bone camera position, not a
shake entry; the head bob stays (section 3). **Post effects:** each effect has a non-config cvar (`FExecVariable`
flags 0, never saved), set through `GDnExec`. Bloom, DOF and grading run in one uber pass that `r_DrawBlooms`,
`r_DrawDOF` and `r_UseToneMapping` never reach; its own Test/brightness cvars are the levers (section 4).

Method: Ghidra MCP on project `dnf` (Engine.dll, D3DDrv9.dll, dnGame.dll, dnCommon.dll; renames and plate comments
saved). `tools/ubc.py` on `extracted-packages/*.u`. Property order comes from each property's `Next` chain. PE export
tables were read with Python (MilesAudio.dll vtable names from its exports). Snapshots of the game's ini
files were read only.

## 1. Keep the game running when alt-tabbed

**What the engine does.** All of these are in `UWindowsViewport::ViewportWndProc` Engine.dll!0x102BC410.

| message | what runs |
|---|---|
| `WM_ACTIVATE` inactive | `SetDrag(0)` (vt+0x7C). If `!GIsEditor` and `[[vp+0x30]+0x7C]+0x918 == 0`: `Viewport_FocusPause(actor,1)`, audio `CheckPause` (vt+0x64), audio `SetMasterVolume(0.0)` (vt+0xD8). Then the overlay-off `EndFullscreen`+minimise (display.md). Always `SetMouseCapture(0,0,0)` and `FocusChanged(0)`. |
| `WM_ACTIVATE` active | `Viewport_FocusPause(actor,0)`, `SetMasterVolume(1.0)`, then `FocusChanged(1)` |
| `WM_SETFOCUS` / `WM_KILLFOCUS` | `FocusChanged(1)` / `FocusChanged(0)` |
| `WM_ACTIVATEAPP(0)` | fullscreen only: `FocusPause(1)`, `CheckPause`, `SetMasterVolume(0)`, `EndFullscreen` unless the overlay is on, `SetMouseCapture(0,0,0)`. Always `FocusChanged(0)`. |
| `WM_SIZE` minimised | `DAT_107FEFF8 = 1`, `FocusPause(1)`, `CheckPause`, and then `FocusChanged(0)` when the window is not fullscreen. Restored: `FocusPause(0)`, `DAT_107FEFF8 = 0`. |

- **`+0x918` is `LevelInfo.NetMode`, not a pause byte** (read by `AActor::IsMP` 0x102F9D20 and about 50 net
  paths). The focus pause runs in standalone games only. display.md's "+0x918 byte" candidate is therefore wrong.
- `Viewport_FocusPause` 0x102BB250 sets `LevelInfo.bPausedFromTaskSwitch` (bit 0 of `+0x85C`; bit 1 is `bPaused`,
  in `Next`-chain order) and calls `APlayerPawn::eventSetPause("__MAINMENU", 1)`, which runs the same script pause as
  the menu (pause.md). It returns early when `g_LoadingOnThread` or `*g_IgnoreFocusChanges` is set. No script reads
  `bPausedFromTaskSwitch`.
- `g_IgnoreFocusChanges` = **dnCommon.dll!0x1009ECC0** (exported int, imported through Engine.dll slot 0x1041EFA0).
  The only writer is `appInit` (0x1003BBDD), which sets it to 1 when the command line has `-IGNOREFOCUSCHANGES`
  (`ParseParam` 0x100343B0). Its readers are `Viewport_FocusPause` and `UGameEngine::RenderLevel`.
- `FocusChanged` 0x102B9A60 writes the focus flag `DAT_106478F0`. Losing focus runs `SetMouseCapture(0,0,0)`,
  `Exec("SETPAUSE 1")`, `SetDrag(0)`, `Input->ResetInput`, `Client->MakeCurrent(0)` (`UWindowsClient` vt+0x7C
  0x102B8050). **`SETPAUSE` has no handler:** the string appears in no module or package except this call
  (byte search). The only script `SetPause` functions are not `exec` (flags 0x20802), so the console rejects it.
- With the focus flag at 0, `UpdateInput` 0x102BA070 returns at once (no keys, no mouse), and `ULevel::Tick`
  0x10241F00 stops re-asserting `SetMasterVolume(1.0)`/`SetGlobalVolume(1.0)` (0x102424C3). The audio slots are
  MilesAudio.dll's `UMilesAudioSubsystem` UObject vtable: +0x64 `CheckPause`, +0xD8 `SetMasterVolume`, +0xE0
  `SetGlobalVolume`.
- **Ticking and drawing do not depend on focus.** `MainLoop` 0x102444B0 checks the foreground window only in the
  editor. `UWindowsClient::Tick` 0x102B7C80 draws every realtime viewport. `UGameEngine::RenderLevel` 0x10225420
  writes `+0xD0 = 1` just before its focus test, so the test always passes. Only a minimised window (`DAT_107FEFF8`)
  skips `DrawWorld`. The menu Bink mutes itself when `!HasFocus` (`UDukeDesktopWindow.Tick`), which is cosmetic.

**Levers.**
- (a) Write `*g_IgnoreFocusChanges = 1` while the option is on. The engine writes this flag itself. It removes the
  script pause from all three paths. Flip it only while the game is in front: if it flips between a pause and its
  resume, the resume is skipped and the game stays paused. The cost is one dnCommon data write.
- (b) Sound: the volume write is not gated by the flag. After the engine handles `WM_ACTIVATE 0`/`WM_ACTIVATEAPP 0`
  in our subclass, call `audio->SetMasterVolume(1.0)`. The object is `Client(vp+0x18)+0x28` → `Engine+0x4C`. 1.0 is
  the value the engine itself writes every focused tick.
- (c) Must stay: `SetMouseCapture(0,..)` and the `UpdateInput` stop. Without them, keys and the mouse would reach a
  background game. Borderless's cursor hold (display.md) already covers the recentring.
- (d) Rendering while unfocused needs no lever on the borderless/overlay-on path. With the overlay off, the engine
  minimises, so ticking continues but nothing is drawn. Exclusive fullscreen loses the device and cannot draw.
- (e) Alternative with no code: `-IGNOREFOCUSCHANGES` in the Steam launch options (a manual setting, the same flag).

**Built:** (a) + (b), with (c) kept. The lever is observable in the log: `Level.bPaused`, `bPausedFromTaskSwitch`, the master
volume and `TimeSeconds` after an alt-tab; `TimeSeconds` still rising with the flag on shows the game keeps running.

## 2. Raw mouse input

**Pipeline (no time-domain smoothing anywhere).**
1. `UpdateInput` reads the exclusive DI mouse once per frame. `lX/lY` go to `CauseInputEvent(0xE4/0xE5)` with no
   filter.
2. The `user.ini` binding `MouseX=Axis aMouseX Speed=1.0` accumulates into `PlayerPawn.aMouseX` (+0x1AC4).
3. `APlayerPawn::PlayerInput` 0x10262470 (from 0x10262E79): `v = aMouseX/dt`. If `MouseAccelDampening` (+0x1CA4,
   config, user 1.0) > 0: `x = sign(v)·min(|v|/26214, 1)^(1+(D−1)/9)·(0.5·GameSpeedModifier+0.5)` (LevelInfo+0x7C0,
   confirmed by `MirrorModeEnabled` reading +0x7BC bit 2). Then `aTurn += MouseSensitivity(+0x1CA0)·CurrentFOV/90·
   13107·x` (constants 26214.0/13107.0 at 0x104CBAA8/0x104CBAA0, 9.0 at 0x1048AFD8).
4. `APlayerPawn::UpdateRotation` 0x1026ACA0: `dt = min(dt, 1/30)`; `yaw = int(aTurn·dt·7.68 + adjust)`, the same
   for pitch (the 7.68 is detailed below). Then `eventTriggerViewRotationAdjust(&Pitch,&Yaw,&Roll,dt)` 0x10141F70, raised through `this->vt[1]`.
   Then the deltas are added to `ViewRotation` and the pitch limit (+0x1FD4/+0x1FD8) is applied.

**What is not raw:** a velocity cap of 26214 counts/s (Speed 1: about 16 in/s at 1600 DPI; faster flicks lose
turn), the curve when D > 1 (the default D is 1, which is linear), the 1/30 s clamp (below 30 fps a turn loses
counts), and per-frame int truncation. Linear gain below the cap: 3.84·Sens·FOV/90 rotator units per count (0.5 from `PlayerInput`, 7.68 from
`UpdateRotation`).
`MouseSmoothThreshold`/`bMaxMouseSmoothing` are read by no native code (only the copy constructor). Their execs
call `SaveConfig`.

**Levers.**
- `MouseAccelDampening` ≤ 0 skips the cap and the curve, but it also skips the /26214, so turning becomes 26214×
  faster. It is also config, saved by the quit path's `SaveConfig` (player-options.md). Rejected.
- Scaling `MouseSensitivity` with pre-scaled DI counts: config, and clamped to 1..10 at level start. Rejected.
- Our dinput8 `GetDeviceState` detour alone cannot help, because the cap acts after it.
- **dnGame ProcessEvent slot** (engine-map.md): on `TriggerViewRotationAdjust` for the player, overwrite
  Pitch/Yaw in Parms before calling through. Use the counts our DI detour accumulated since the last event ×
  0.5·7.68·Sens·CurrentFOV/90 (× Speed), with the fractional remainder carried, the invert flag (+0x1CC0 bit 1) and mirror mode.
  Calling through first lets turret/vehicle `InputHook_ViewRotationAdjust` see our values, and the pitch limit
  still applies. The event fires only when the engine would rotate (unlocked, not paused). That gives no cap, no
  clamp and no truncation, with the same feel below the cap. Risks (M): the Parms layout must be read from the
  UFunction (3 ints + float expected); gamepad input in the same event would be overwritten (mouse-only switch).
- Side lever, not used: D3DDrv9 cvar `r_StopMouseLag` (default 2, D3DDrv9!0x100A0020; mode 1 locks the back
  buffer each frame) affects render-ahead latency, not the turn math.

**Built (`rawmouse.cpp`, a Controls switch; read in Ghidra and Engine.u; verified in game):** the `TriggerViewRotationAdjust` rewrite.
- The slot is dnGame.dll's import of `?ProcessEvent@AActor@@UAEXPAVUFunction@@PAX1@Z` (0x10053054). Thunk
  0x10047CBA (`jmp [0x10053054]`) is vt[1] of `ADukePlayer`'s vtable 0x10059554.
- `eventTriggerViewRotationAdjust` 0x10141F70 fills the block {+0 pitch, +4 yaw, +8 roll, +0xC dt} and copies the
  three ints back after the call. Engine.u names the parameters `PitchAdjust`, `YawAdjust`, `RollAdjust`
  (IntProperty) and `DeltaTime` (FloatProperty). They are found by name in the UFunction at runtime. If they are
  missing, the option is refused.
- `UpdateRotation` 0x1026ACA0 locks an axis with `ViewRotationMode` (+0x1FB0, ByteProperty): 2 leaves pitch at 0,
  1 leaves yaw at 0. Locked axes stay 0 under the rewrite too.
- `PlayerInput` 0x10262470 skips the mouse while the console is typing: `[[Player(+0x12B8)]+0x34]+0x660` bit 0.
  The rewrite then turns nothing. Invert is `bInvertMouse` (+0x1CC0 bit 1, which flips aLookUp). Mirror mode
  flips aTurn when `ALevelInfo::MirrorModeEnabled` 0x1027B9E0 is set (not MP, not My Digs, `+0x7BC` bit 2; not
  exported). The script function `LevelInfo.MirrorModeEnabled` is called once per level.
- Each reflected field (MouseSensitivity +0x1CA0, CurrentFOV +0x1724, ViewRotationMode, bInvertMouse, Player) is
  compared with the offset the native code reads. Any mismatch refuses the option.
- Not modelled: the `(0.5 x GameSpeedModifier + 0.5)` factor (1 at normal speed) and the binding's `Speed=` (1.0
  by default).
- Gain: `k = 0.5 * 7.68 * sens * fov * 0.01111` in `rawmouse.h` (`RAW_ROTATION_GAIN` 7.68 in `raw_turn`, FOV/90 for
  0.01111).

### The 7.68 gain in `UpdateRotation`

`UpdateRotation` multiplies by **7.68** (`FLD double [0x104CC010]` at 0x1026ACF9): `yaw = trunc(aTurn x dt x 7.68 +
adjust)`. The engine's gain below the cap is **3.84 x Sens x CurrentFOV x 0.01111 rotator units per count**; a rule
without the 7.68 turns 7.68 times too slowly.

The whole chain at one set of settings (user.ini: `MouseSensitivity=10.0`; `MouseAccelDampening=1.0`; `MouseX=Axis aMouseX Speed=1.0`):
1. `UpdateInput` 0x102BA070: one `GetDeviceState(0x14)` per call. `lX` goes to `InputEvent(0xE4, IST_Axis=4,
   (float)lX)`, `-lY` to 0xE5.
2. `UEngine::InputEvent` 0x1021C7A0, then `UInput::PreProcess` 0x10232190 (key table only), then `UInput::Process`
   0x10234010 (stores action and delta, runs the binding). Neither scales.
3. `UInput::Exec` AXIS (from 0x102331A0): `Speed=` defaults to 1.0 (0x1051049C). For IST_Axis it runs
   `*aMouseX += Delta x Speed`, with no 0.01 and no 1/dt. So aMouseX holds the frame's counts.
4. `PlayerInput` 0x10262470: `v = aMouseX/dt`, `x = clamp(v/26214, -1, 1)`. With D > 0 it applies the curve and
   `(0.5 x GSM + 0.5)` (D = 1 is linear). Then `aTurn += Sens x (CurrentFOV x 0.01111) x 13107 x x`. Below the cap
   that is `0.5 x Sens x FOV x 0.01111 x counts/dt`, a rate per second.
5. `UpdateRotation` 0x1026ACA0: `dt' = min(dt, 1/30)`, `yaw = trunc(7.68 x dt' x aTurn + [+0x1C98])`
   (`_ftol2_sse` 0x103C4790 truncates; `+0x1C98` is the aim-assist adjust, 0 with no target). Pitch is the same
   from aLookUp and `+0x1C94`. Then TVRA, and `ViewRotation` (65536 per turn).

Above 30 fps, dt' equals dt and cancels: **3.84 x Sens x FOV x 0.01111 units per count**. At Sens 10 and the logged
`CurrentFOV` 89.4 that is 38.1 units per count, 1719 counts per 360 degrees (1.07 in at 1600 DPI). Without the 7.68
the turn would be 4.97 units per count (13190 counts per turn). The cap stays 26214 counts/s. MouseSensitivity's
range is 1..10 (section 2 above).

**Check:** in the TVRA handler the panel reads the game's `PitchAdjust`/`YawAdjust` from Parms before overwriting
and logs, every 600 frames, `rawmouse: game yaw G pitch Gp, ours O Op, counts x y, G/O yaw r pitch rp`. The ratio
G/O is 1.00 for movements below the cap with no aim assist (7.68 would mean the gain is missing). Any other steady
ratio at slow movement points to something else, for example a scale in the binding.

## 3. Screen shake removal

The removal is verified in game.

- Every shake goes through `PlayerPawn.ShakeView` (script; scaled only by `GetShakeViewScale` while on a usable).
  It calls `AddViewShake` (native 1017, 0x10263A40), which copies the 0x48-byte `SActiveViewShake` into
  `ActiveShakes` (+0x1574) and runs `SetupShake` 0x10261BF0. C++ raises `ShakeView` too (dnGame imports
  `APawn::eventShakeView`).
- Sources (script refs): weapon kicks (`Weapon.StartWeaponViewKick`, names `<Gun>_ViewKick_<n>`), damage flinch
  (`DamageType.CreateDamageViewKick`: `DamageShake_Pitch`/`_Roll`, size `DamageViewKickSoft/Hard`), melee/execution
  contact, projectiles and explosions (`*_Shake`), the nuke, bosses and AI stomps/footsteps, turrets, vehicles
  (`Vehicle_LandingShake`, boost, kills), `TriggerPlayer`, `MeleeHit`, falling damage.
- `UpdateViewShake` 0x10267CB0 (PlayerTick, after PlayerMove) zeroes `CurrentShake` (+0x1580..+0x1588, one float per axis). It sets it, per axis,
  to the largest of each entry's `ProcessShake` 0x10265AB0 output (`Magnitude` +0x18 lerped to `FullMagnitude`
  +0x1C, × `Scalar` +0x30 → `Limit` +0x20 → `Result` +0x24..+0x2C) and of the stop residual `CompOldShakes`
  (+0x1568, decaying at `ShakeFadeRate` +0x1564). **`StopShake` 0x10263B80 copies the stopped angle into that
  residual**, so stopping a shake fades it out instead of cutting it. (accuracy.md's "damage view kicks +0x1568" is
  that residual: the damage kicks themselves are named entries.)
- **Lever:** walk `ActiveShakes` by reflection. For each entry whose `ShakeName` lacks `_ViewKick_`, write
  Magnitude/FullMagnitude/Limit/Result to 0. The engine writes all of these fields itself. Do it in the dnGame slot
  on `TriggerViewRotationAdjust` (in PlayerMove, before UpdateViewShake in the same tick, so no frame leaks), and
  again at Present for ticks without PlayerMove (vehicles, cutscenes).
- Recoil keeps its own path: No recoil stays the switch for `_ViewKick_` (camera bob is a separate system).
- Rejected: `StopAllShakes`/`StopShake` (the residual still shakes, and they stop kicks too), zeroing
  `CurrentShake` (it removes the kick from the aim), per-source defaults (dozens of classes).
- **As built (`noshake.cpp`):** the layout is found by reflection: `PlayerPawn.ActiveShakes`
  (ArrayProperty) → Inner (+0x78, `UArrayProperty::CopyCompleteValue` 0x1004C756) → StructProperty → Struct (+0x78,
  `UStructProperty::DestroyValue` 0x1004ABD0) → `ShakeName`, `Magnitude`, `FullMagnitude`, `Limit`, `Result`. The
  result must match the native reads (array +0x1574, entry 0x48, name +8, +0x18/+0x1C/+0x20/+0x24); otherwise the
  option is refused. Each name is logged once as kept or zeroed.
- **Only the name decides.** A recoil-like shake whose name lacks `_ViewKick_` is zeroed, although the row's hint
  says weapon recoil stays. The log line for the name shows any such case.

### Sprint camera: the eye bone

The head bob is kept (a decision). An eye-bone smoothing was built and tried in game: it put the camera inside
Duke's head (his glasses and teeth clipped into view). Its module (`eyecam.cpp`, the dnGame `GetCameraLocation` slot
hook) is removed; No screen shake zeroes the shake entries only. What the engine does (read in Ghidra):

The sprint's rotational bob is a pair of ActiveShakes entries (`PlayerPawn_Sprinting_0`/`_2`). Our zeroing removes
it: the 476 zeroings per 600 frames are `UpdateCameraBob` rewriting `Magnitude` every tick. What remains is the
camera's **position**. In first person the view location is the player mesh's **eye bone**
(`APawn::GetCameraLocation` 0x1024DC40), so the sprint animation's head motion moves the camera. No shake field
holds that motion. The only lever is dnGame's import slot for `GetCameraLocation`.

- `UpdateCameraBob` 0x10265ED0 is called from `APlayerPawn::TickSpecial` 0x1020FA30. The state is `CameraBobState`
  +0x1598: 1 walking (`WalkingCameraBob` +0x159C), 2 crouching (+0x15A8), 4 sprinting (`SprintingCameraBob` +0x15B4).
  `BobPCT` +0x15C0 must be > 0. It is a default property (flags 0x800001) with no runtime writer besides construction
  and copy. `CameraBob_ApplyShakes` 0x10263D50 (renamed) does, for each array entry: `FindShake`; if absent,
  `ShakeView` by ProcessEvent with scale BobPCT; if present, `Magnitude` (+0x18) = BobPCT-scaled. A state change runs
  `CameraBob_StopShakes` 0x10265E60 (`StopShake`, which leaves the residual).
- Our zero runs on TVRA inside `PlayerMove`. `UpdateViewShake` runs later in the same `PlayerTick` 0x10270D80.
  ProcessShake's output is `Result` (+0x24..+0x2C) x a triangle of the timer +0x38 x a distance factor
  (0x10265CD6-0x10265D8E). `Result` is rebuilt from `Limit` only when the timer expires. With all four fields at 0,
  the bob adds 0 to `CurrentShake`.
- View rotation is `ViewRotation` + `CurrentShake`, scaled by CurrentFOV/DefaultFOV when the two differ
  (`PlayerCalcView` 0x10268730 tail). `UpdateViewRoll` 0x10267E90 only decays the roll. Nothing else rotates the view.
- View location is `vt+0x330` = `APawn::GetCameraLocation`. It returns `Location`, replaced by
  `BoneGetTranslate(Mesh, EyeBone)` (`Pawn.EyeBone` +0x1278) when the bone exists. For `ADukePlayer` (dnGame vtable
  0x10059554, slot +0x330) the entry is thunk 0x1004843A, `jmp [0x100537B8]`, dnGame's import of
  `?GetCameraLocation@APawn@@UAE?AVFVector@@XZ`. Other callers: `PlayerInput`'s aim assist (three calls in
  0x10262470).
- Sprinting also ramps the `'Sprinting'` FOV modifier (`StartedSprinting`: `SprintFOVScale` over 0.25 s). That is a
  zoom, not a shake.

**The slot lever (built, then removed):** replace dnGame's import slot 0x100537B8. Our function calls the original.
For the player only, and only when the return address is inside `PlayerCalcView` (Engine.dll
0x10268730..0x10268FEF, so aim and AI keep the bone), it returned `Location + lowpass(bone - Location)`, with a time
constant of about 0.25 s so crouch and landing transitions keep their shape and the stride-rate bob goes. It wrote no
engine memory. dnGame carries no CEG (modules-ceg.md), and a slot write is not code anyway. Recoil is untouched:
ViewKicks stay `CurrentShake` entries and No recoil's.

**The removed implementation (`eyecam.cpp`):**
- PlayerCalcView (body 0x10268730-0x10268E38) calls vt+0x330 three times. 0x10268919 is the animated camera (with
  vt+0x334's bone rotation: death, mounts), 0x10268AA6 the behind view, and **0x10268C4F the first-person view**
  (return 0x10268C51). Only that return address was smoothed. The 14 bytes before it were checked at install.
- `GetCameraLocation` is `__thiscall` with a hidden return slot (`mov edi,[ebp+8]` ... `ret 4`); the hook was
  `__fastcall(pawn, edx, FVector*)`, installed the first time No screen shake was on.
- The bone's offset from Location was turned into the pawn's yaw frame (Rotation +0x1F8), low-passed (0.25 s while
  `CameraBobState` != 0, 0.05 s at rest), and turned back. A jump over 32 units or a 0.5 s gap took the bone exactly.
  Chosen over Location + base eye height, which would lose crouch, ledge and execution head motion.
- One line per 600 frames: sprint frames, max |CurrentShake| per axis while sprinting, the raw eye offset's z and xy
  ranges, the smoothed z range, engagements and snaps.

## 4. Post-processing toggles

The cvar toggles remove their effects in game (verified).

Cvars are `FExecVariable` {name, desc, flags +8, ptr +0xC, type +0x10, handler +0x14, next +0x18} in one registry,
`GDnExec` (Engine.dll export 0x10661FCC; D3DDrv9 and dnGame register through Engine's constructor).
`FDnExec::Exec` 0x10009C60 parses `name value`. **Only `SaveConfigVariables` 0x10009BA0 (called from `appExit`)
writes them, and only entries with flags bit 0.** Every variable below was registered with `PUSH 0` (static
initialisers at 0x10416300-0x10417A00, D3DDrv9 0x10058BB1, dnGame 0x10050121), so none is ever saved.
`FindVariable` (export 0x100099B0) gives the pointer.

The chain is `Post_DrawAll` 0x1031F1D0 (renamed), from `DrawFrame`, gated by `r_DrawPostEffects` (0x10649AAC). A typical
system.ini already has `AllowPostEffects=False`, `AllowFilmGrain=False` and `PerPixelMotionBlur=False`
(UClient config +0xB8/+0xB4/+0xA4, saved through the client's config: never write them). So bloom, DOF, tone
mapping, stars and film grain are already off in that case. The blur family is not.

| effect | pass (Engine.dll) | gate | off lever |
|---|---|---|---|
| Screen blur (drunk, pain, water, nuke, death, `TriggerPlayer`/`TriggerViewSaturation`: scale group `Blur`) | `Post_ScreenBlur` 0x1031C970 | `r_DrawBlur` 0x10649AD0, PlayerPawn `BlurAmount` > 0 | `r_DrawBlur 0` |
| Ghosting motion blur (drunk, pain, size change, nuke: group `MotionBlur`) | `Post_MotionBlurFeedback` 0x1031CA80 | `r_DrawMotionBlur` 0x10649ADC, `MotionBlurAmount` > 0 | `r_DrawMotionBlur 0` (MP `OnEndGame` re-sets it to 1) |
| Per-pixel motion blur | `Post_PixelMotionBlur` 0x1031E4F0 | `PerPixelMotionBlur` && `r_DrawPixelMotionBlur` 0x10649AC0 | `r_DrawPixelMotionBlur 0` |
| Depth of field (zone, auto, HeatVision) | `Post_DepthOfField` 0x1031D3B0 | `AllowPostEffects` && `r_DrawDOF` 0x10649AF0, render type 5/8 | `r_DrawDOF 0`; `r_DOFPow` (D3DDrv9!0x100A0274) only shapes the DOF shader |
| Bloom / star glints | `Post_Bloom` 0x1031EF70, `Post_Stars` 0x1031F120 | `Post_BloomAllowed` 0x1031A6D0: `AllowPostEffects`, HDRMode, `r_DrawBlooms` 0x10649DC4 | `r_DrawBlooms 0` (`r_DrawStars` 0x10808A38 for glints only) |
| Colour grading / desaturation | `Post_ToneMapping` 0x1031E820 | `AllowPostEffects` && `r_UseToneMapping` 0x10649AF8, zone flag | `r_UseToneMapping 0` |
| Underwater/volume overlay | `Post_VolumeOverlay` 0x1031B5C0 | `r_DrawUnderWater` 0x10649B20, the volume's overlay material | `r_DrawUnderWater 0` |
| Film grain + vignette | `ADukeHUD::DrawHUDEffects` dnGame!0x100328C0 | `AllowFilmGrain` && `bFilmGrainEnabled` (level triggers) && dnGame `r_AllowFilmgrain` dnGame!0x1008C068 | `r_AllowFilmgrain 0` |
| HUD overlays (steroids, invincibility, ego depleted, vision types) | the same `DrawHUDEffects` list | per-effect materials | no cvar: `RemoveSteroidHudEffect`/`RemoveInvincibilityHudEffect` by ProcessEvent |
| Pink-o-vision | renderer | `r_DrawPinkOVision` 0x1080E9D4 | `r_DrawPinkOVision 0` |
| FXAA/FSAA (not an effect) | `Post_FXAA_FSAA` 0x10316FC0 | `AllowFXAA`/`AllowFSAA`, `r_DrawFSAA` | leave |

**Built:** one row per effect. The panel remembers the cvar's value, writes the new value through `FindVariable` (an
interlocked dword: the render job reads it once per pass) or `FDnExec::Exec`, and restores it when the row turns off.
It re-applies after a level load (`r_DrawMotionBlur` is reset by MP end-game only). Persists to an ini: no (flags 0).
Measurement: log each variable's value per level and the player's `BlurAmount`/`MotionBlurAmount` while drunk.

### The uber pass

Three of the nine variables are dead in the shipped path (read in Ghidra and a test log).
`r_TestUberPost` (0x10649AA8, default **1**, log registry) makes `Post_DrawAll` skip
`Post_Bloom`, `Post_DepthOfField` and `Post_ToneMapping` and run one combined pass, **`Post_UberPost` 0x1031D8B0**
(renamed). That pass reads none of `r_DrawBlooms`, `r_DrawDOF` or `r_UseToneMapping`. Two more variables remove an
effect the game's own options already turn off, and two act only during transient effects. Each uber effect has its
own non-saved cvar lever, listed below.

- `Post_DrawAll` 0x1031F1D0 has two callers: `dnRenderer::DrawFrame` 0x1032F3B0 and `DrawFrameDeferred_Normal`
  0x10330A20 (`r_UseDeferred`=1). `if (!r_TestUberPost) Post_Bloom` ... `if (!r_TestUberPost) Post_DepthOfField
  else Post_UberPost` ... `if (!r_TestUberPost) Post_ToneMapping`. Names come from the registration code at
  0x10416370-0x10416A50 (`PUSH ptr; PUSH 0; PUSH name`).
- `Post_UberPost` gates on `Client.AllowPostEffects` (+0xB8), render type 5 or 8, and `r_DrawUberPost` 0x10649AF4
  (=1). In one pass it draws three effects. **Bloom:** a bright-pass blur at `r_BloomIndex`, whose brightness argument
  (the slot where `Post_ScreenBlur` passes `BlurBrightnessScale`) is `PlayerPawn.BloomBrightnessScale` (+0x150C) x
  `r_BloomBrightnessScale` (0x10649AA4) x an animated value. **DOF:** two separable blurs mixed by `DOF_BlurAmount`
  (+0x1518; focal +0x151C..+0x1524), all taken from the cvars instead when `r_TestDOF`. **Grading:**
  shadows/midtones/highlights and desaturation from the scene's animated values, taken from the cvars instead when
  `r_TestToneMapping`. PlayerPawn offsets come from Engine.u declaration order (`BlurAmount` +0x1504 ... `DOF_FocalRangeMax`
  +0x1524).
- **Options menu** (`UDukeSceneVideoAdvancedOptions`, dnWindow.u): "Motion blur" sends `set ini:Engine.Engine.ViewportManager
  PerPixelMotionBlur`, "Film grain" sends `AllowFilmGrain`, "Post effects" sends `AllowPostEffects`. These are Client
  +0xA4, +0xB4 and +0xB8, saved to system.ini `[Engine.WindowsClient]`. Motion blur and film grain are greyed in game
  (`bCanToggleMotionBlurFlimGrain = !IsInGame`). Nothing writes a D3DDrv9 field. One system.ini reads
  `AllowPostEffects=True`, `PerPixelMotionBlur=False` and `AllowFilmGrain=False`. So the
  visible post effects are the uber pass's bloom, DOF and grading.

| variable | read by | status in the shipped path |
|---|---|---|
| `r_DrawBlur` | `Post_ScreenBlur` (runs) | live; draws only while `BlurAmount` > 0 (drunk, pain, water, nuke) |
| `r_DrawMotionBlur` | `Post_MotionBlurFeedback` (runs) | live; only while `MotionBlurAmount` > 0. `DukePlayer.SetMotionBlur(true)` (writer FUN_1035E9F0) at every `dnSinglePlayer.Login` sets it to 1; the per-frame re-force covers that |
| `r_DrawPixelMotionBlur` | `Post_PixelMotionBlur` (runs) | live, but `PerPixelMotionBlur` (+0xA4) is off by default |
| `r_DrawDOF` | `Post_DepthOfField` only | **dead** while `r_TestUberPost` = 1 |
| `r_DrawBlooms` | `Post_BloomAllowed` 0x1031A6D0: `Post_Bloom`, `Post_Stars`, the bloom target switch FUN_1031B3D0 | `Post_Bloom` is dead and the uber bloom does not read it (whether the uber bright pass reads the target it switches is not read; it is also exported, D3DDrv9 use not checked) |
| `r_UseToneMapping` | `Post_ToneMapping` only | **dead** |
| `r_DrawUnderWater` | `Post_VolumeOverlay` | live, underwater only |
| `r_AllowFilmgrain` | `ADukeHUD::DrawHUDEffects` dnGame!0x100328C0 | live, but it also needs `AllowFilmGrain` (+0xB4) and `bAllowFilmGrain` (+0x12D8 bit 0x20); grain is never drawn then |
| `r_DrawPinkOVision` | renderer | live; 0 by default |

**Levers in the uber pass** (all `FExecVariable` with flags 0, never saved):

| effect | lever | note |
|---|---|---|
| Bloom | `r_BloomBrightnessScale` (FLOAT, 1.0) to 0.0; keep `r_DrawBlooms` 0 for the star glints | zero brightness into the bloom blur, so nothing is added |
| Depth of field | `r_TestDOF` (UBOOL 0x10808A40, 0) to 1, **and** `r_DOFBlurAmount` (FLOAT 0x10649AE0, 0.6) to 0.0 | the pass then takes all four DOF values from the cvars, and blur 0 leaves the image sharp. `DOF_*` on the pawn are rewritten each frame (auto DOF, `PlayerCalcView`), so a property write would not hold |
| Colour grading | `r_TestToneMapping` (UBOOL 0x10808A48, 0) to 1 | grade from `r_SceneShadows` (0x10808E14, bss 0), `r_SceneMidTones`/`r_SceneHighlights` (0x10649B24/B28 = 0x007F7F7F, x2 = 254/255) and `r_SceneDesaturation` (0x10808A4C, 0): the neutral grade. Leave the colours alone |
| All three | `r_DrawUberPost` 0 | not read: whether this pass also resolves HDR (HDRMode=3, `r_FinalHDR`=1). Could change brightness. Not the default |
| (alternative) | `r_TestUberPost` 0 | routes to the legacy passes so the three dead rows go live. The look may differ from the shipped path even with every effect on. Rejected |

**Built:** Bloom row = `r_BloomBrightnessScale` 0.0 plus `r_DrawBlooms` 0. DOF row = `r_TestDOF` 1 plus
`r_DOFBlurAmount` 0.0. Colour grading row = `r_TestToneMapping` 1. The screen blur, motion blur and underwater rows
stay. `cvars.cpp` admits FLOAT, held by its bits (write 0.0f, restore the remembered bits), besides UBOOL/INT. Each
variable of a row has its own hold; a row with two variables holds both and restores both. Per-pixel motion blur and
Film grain grey out with "Off in the game's options." while Client +0xA4/+0xB4 reads 0, or while their variable
reads 0 and is not ours. The measurement line `postfx: client PerPixelMotionBlur a AllowFilmGrain b AllowPostEffects
c; r_TestUberPost x r_DrawUberPost y` is logged once per level and on any change (Client +0xA4/+0xB4/+0xB8,
`r_TestUberPost`, `r_DrawUberPost`).

## Open questions

- Keep running: that the game keeps ticking in the background (the proof above is read from the binary); whether Steam's
  overlay state changes the minimise path with the flag on.
- Post effects: whether `RemoveSteroidHudEffect`/`RemoveInvincibilityHudEffect` remove the HUD overlays when called by ProcessEvent.

# Free camera, pause, teleport, photo and screenshot mode (build 244)

Every row has a lever that patches no Engine.dll code. **Camera:** rewrite `PlayerCalcView`'s two out-params in the
dnGame ProcessEvent slot, *after* the native has run (pehook's `after` subscribers). The engine copies them into
`CalcedView*` and the renderer, and builds the frame, its zone and the sound listener from them, so culling follows
the camera. **Pause:** `ForcePause_NoFade` plus the cvar `r_UsePausedFrameCache` 0 while flying. Without that cvar, a
paused game redraws its last frame and the camera never seems to move. **Input:** the panel's own gate (GetKeyState
0, dinput8 mouse zeroed, keyboard messages swallowed); the camera reads keys and mouse counts itself. **Teleport:**
`Pawn.PawnSetLocation`, the Teleporter's own call. The red marker is `Actor.FindSpot`, the same fit test
SetLocation makes. **Photo:** the engine has a tiled high-res shot of its own. Its off-axis tile projection is three
non-saved cvars (`MegaShotScale`, `MegaShotXShift`, `MegaShotYShift`) read by `FSceneNode::UpdateTransforms`. Set
them per tile and capture at Present: the Manhattan Project port's strip pipeline then needs no FOV narrowing or shear. **Screenshot mode:**
the cvar `r_DrawHUD` 0, which is what the engine's own screenshot tool and mega shot use. **Fullbright:** holds
`PlayerPawn.RendMap` 6 for the flight, which reaches only the deferred surfaces, plus a brightness lift drawn at
Present (section 7). **No culling:** forces three cvars to 0 and writes frame zone 0 at `PlayerPawn.PreRender`
(section 8).

The engine also has a complete **paused fly camera** (the MP screenshot tool, `bScreenshotMode`). It is the
reference for the pause path and a fallback lever (section 1b).

Method: Ghidra MCP on project `dnf` (Engine.dll, dnGame.dll, D3DDrv9.dll, MilesAudio.dll); plate comments saved on
0x1026B5F0, 0x10115910, 0x1032C400, 0x10226AD0, 0x103313B0 (renamed `Render_OwnerSeeVisible`). `tools/ubc.py` on
`extracted-packages/*.u`. Bool bit numbers come from the property `Next` chain. They are checked against the natives:
`bInMapView` 0x80, `bPlayerViewLock` 0x100 and `LastFog` 0x1000 of PlayerPawn+0x12D0, and `bPaused` 0x2 of
LevelInfo+0x85C (pause.md). Cvar names, flags and defaults were read from their static initialisers in
Engine.dll's file (Python), import and export names from the PE tables. Field offsets below are what the natives
read. Code resolves each by name (reflection) and refuses on a mismatch, as rawmouse/noshake do. The frame
instruments (the trace, the render watch, the frame record) write the panel's log.

## 1. Camera override

### 1a. The view path (the lever: PlayerCalcView's out-params)

- `UGameEngine::StartRendering` 0x10226BC0 calls `SetupViewInfo` 0x10226AD0. That function seeds the camera with
  Viewport->Actor's Location/Rotation and raises `APlayerPawn::eventPlayerCalcView` 0x10157490 **through
  ProcessEvent vt[1]**. For ADukePlayer that is the dnGame slot (pehook.h) we already own. Parms
  `{ViewActor +0, CameraLocation +4, CameraRotation +0x10}` are copied back after the call. SetupViewInfo then
  writes them to `CalcedViewLocation` +0x14C4, `CalcedViewRotation` +0x14D0 and RenDev +0xB8/+0xC4.
- `URender::CreateMasterFrame` 0x1027A760 builds the frame from those two values. FOV = PlayerPawn +0x1274
  (FovAngle). **Zone = `UModel::PointRegion` of the camera location**, so zone/portal culling is done from the
  camera, not from Duke. StartRendering also feeds the sound listener from the frame's coords. A point check on the
  camera location (`this+0x80 -> +0x90` vt+0x5C) sets the "clear screen" flag when the camera is outside the world.
- The native `APlayerPawn::PlayerCalcView` 0x10268730 (via `execPlayerCalcView` 0x10268FF0) picks in this order:
  menu camera (+0x14DC), dead/MP corpse camera, `bBehindView` (+0x9AC bit 0, `PlayerCalcView_Behind` from +0x1554
  at distance 180), ViewMapper (+0x1740, `eventCalcView`), `bInMapView` (top-down), **`bScreenshotMode`**,
  and otherwise first person. First person is `GetCameraLocation` (the eye bone) and ViewRotation, plus
  `CurrentShake`, scaled when CurrentFOV != DefaultFOV.
- **Lever:** an after-call handler on `PlayerCalcView` (Viewport->Actor only) overwrites CameraLocation and
  CameraRotation in Parms with the free camera's. This writes only the event's own out-params, which the engine
  writes itself. It works paused or not, is per frame, and our ESP (which reads `CalcedView*`) follows for free.
  pehook's `PeHandler` is called before the native by default, and a before-call write is overwritten by the native,
  so this handler runs after it. The native is not skipped: it also does menu-camera and view-lock writes.
- Rejected:
  - **A spawned camera actor as `ViewTarget`.** The native never reads ViewTarget for first person. It is used
    only behind `bBehindView` (the cheat `CheatView` sets both), as a chase cam with a trace-clamped orbit at 180
    units. A spawn is also a new actor.
  - **`ViewMapper`.** It is a ControlRemapper (turrets, security cameras) with gameplay side effects.
  - **Writing `CalcedView*` after SetupViewInfo.** The frame is already built from the Parms copy by then.
  - **The cheats `Fly`/`Ghost`.** They move Duke himself (collision off, PHYS_Flying), not a camera.
  - **The exec `FreeCamera`** (flag 0x20000000). It only toggles `bCameraLook` and `BehindView`.

### 1b. The engine's own fly camera (reference and fallback)

- `dnMultiplayer.ToggleScreenshotMode` (dnGame.u; console `sst`, MP only):
  1. Flips `LevelInfo.bScreenshotMode` (+0x85C bit 0x4, flags 0x02800020, not config).
  2. Calls `SetPause(bScreenshotMode, LocalPlayer, ' ', true)`, forcing `bPauseable` for the call.
  3. Calls `Level.NU_ScreenshotMode(on)`. That seeds `ScreenshotModeLocation` (+0x860) and
     `ScreenshotModeRotation` (+0x86C) from `GetCameraLocation`/`GetCameraViewRotation`, and runs
     `ConsoleCommand("r_drawhud off|on")`.
- Each frame, PlayerCalcView (bScreenshotMode and `IsLocallyControlled` = vt+0x344, dnGame import
  `?IsLocallyControlled@APlayerPawn@@UAEHXZ`) calls `eventHandlePlayerMovePrePhysics`. When that returns 1,
  `PlayerMove_ScreenshotMode` 0x1026B5F0 moves the Level fields, and the view is the Level fields. The function:
  - Turns by `round(dt * 7.68 * aLookUp/aTurn + adjust)` (+0x1ADC/+0x1AD0, +0x1C94/+0x1C98) through
    `eventTriggerViewRotationAdjust`, with the pitch limits.
  - Moves by `(aForward +0x1ACC, aStrafe +0x1AD4, aUp +0x1AD8)` rotated by the view, times
    `r_ScreenShotMode_Speed` (0x106476D4, float 6.0, flags 0), times +0x2A4 over its class default (probably
    DrawScale).
  - There is no dt on the move: whether the axes carry dt is unmeasured.
- It runs in render, so it moves while paused (ticks stopped). PlayerInput still fills the axes when paused
  (pause.md). bScreenshotMode also switches off the paused-frame cache (section 3). Natively, bit 0x4 is read only by
  PlayerCalcView 0x10268BBD and DoPausedRender 0x1032C438.
- Why it is not the main lever:
  - It is pause-only.
  - Its keys are the game bindings, and Space/Ctrl are probably buttons, not `aUp`.
  - Its speed model has no Shift/Alt.
  - TVRA fires inside it, so Raw mouse would rewrite its turn.
  - `bScreenshotMode` sits after `bBehindView` in the pick order.

  Its parts (the paused render and r_DrawHUD) are reused below.

### 1c. What still renders at the new viewpoint

- **Duke and his weapon:** `Render_OwnerSeeVisible` 0x103313B0 keys on `OwnerSeeStyle` (+0x2FC: 1
  FirstPersonOnly, 2 ThirdPersonOnly). It shows FirstPersonOnly actors owned by Viewport->Actor and hides
  ThirdPersonOnly ones **while that actor is not `bBehindView`**. The decision comes from bBehindView, not from where
  the camera is. In flight Duke is therefore drawn in his first-person set, seen from outside (expect floating arms,
  a weapon, perhaps no head). `ShouldCastShadows` 0x102FBA97 reads the same byte. Setting `Pawn.bBehindView`
  (the engine writes it in `BehindView`/`CheatView`) would show his third-person set. The out-param override makes
  the view ignore the behind branch. The panel's Show player marker stands in for the third-person set.
- **HUD, crosshair, messages:** section 6. **Our overlays:** our own Present draw (gated).
- **Distance fog follows Duke, not the camera:** `APlayerPawn::HasDistanceFog` 0x102668E0 and `GetDistanceFog*`
  read the player's state (`LastFog` +0x12D0 bit 0x1000, end +0x1718, ticked by `TickDistanceFog`). Flying out of a
  fogged area keeps Duke's fog. **The far plane follows Duke's fog too:** `r_UseFarPlane` puts it at his fog end,
  measured from the camera (section 8). No distance fog and No culling force it off while flying.

### 1d. Distance fog and the frame clear in flight

Read in Engine.dll and D3DDrv9.dll (loaded into the same Ghidra server), then measured with the frame trace.

- **The fog cvar.** `r_DrawDistanceFog` is registered by the static initialiser at 0x1041780A (name 0x104D7FCC,
  storage 0x10649F04, flags 0, never saved). It has exactly two readers:
  - `dnRenderer::HandleFogDeferred` 0x1032D4A0 (the read is at 0x1032D540). It draws the distance-fog fullscreen tile
    only when `r_DrawFog`, the frame's fog flag (FSceneNode +0x338, from `APlayerPawn::HasDistanceFog`: Duke's fog)
    and `r_DrawDistanceFog` are all set.
  - The threaded path FUN_101120B0 (the read is at 0x10112743), under the same frame fog flag.
  - Inside HandleFogDeferred's branch, `r_FillFogHole` (storage 0x10649F08) fills the background past the far plane
    with the fog colour, but only when frame +0x348 > 0.99. `r_DrawFogDecal` (0x10649F0C) draws the fog decal tile.
    Turning `r_DrawDistanceFog` off therefore also stops the fog-hole fill.
  - **Not covered:** Engine.dll exports `ABreakableGlass::DrawDistanceFog`, `AGeoWater::DrawDistanceFog` and
    `ASoftParticleSystem::DrawDistanceFog`, and none of them reads 0x10649F04. Glass, water and soft particles keep
    their own fog.
  - **The lever:** the FREE CAMERA row No distance fog forces it to 0 through cvars.h while flying
    (`freecam_cvar_forced`, FC_CVAR_FOG) and gives it back on every exit.
- **What clears the frame.** `UGameEngine::StartRendering` 0x10226BC0 calls the level's point check at the camera
  (`this+0x80 -> +0x90`, vt+0x5C). Only when that check fails, the camera being outside the world, does it pass
  clear = 1 to the viewport's Lock (vt+0x84). The clear colour is Duke's distance-fog colour, or black.
  - `UD3DRenderDevice9::Lock` 0x1002E540 reaches 0x10004CA0 through FUN_100053D0 (its only caller). That function
    calls BeginScene (FUN_100018A0), then FUN_10003A10 (the targets), then the device's Clear (vt+0xAC, site
    0x10004F5E-0x10004F7E). The call is Count 0, pRects NULL, flags `(clear & 1) | 6`, the colour from Lock's FPlane
    through `FColor::A8R8G8B8`. `r_ShowWireframe == 3`, `r_UseDeferred` or `r_FreezeVis` force the flags to 7.
  - **Measured:** with `r_UseDeferred` on, every first Clear after Present already has flags 0x7, in every zone,
    iLeaf valid or -1. So the back buffer, or whatever Lock binds there, is colour-cleared every frame by the engine
    itself, and the smear lives in another surface.
  - **No cvar lever:** the three cvars that force 7 each change the renderer.
  - **A Clear detour in `d3d9.dll`** (d3dcap.cpp, d3dbody D3DM_CLEAR; `freecam_clear_flags`): while flying, the first Clear
    after Present with the Lock shape (Count 0, no rects, flags exactly 6) also gets D3DCLEAR_TARGET. That is the
    call the engine itself makes at this site with the camera outside the world. It changes nothing while the engine
    already clears (flags 7), and stays on during a photo capture on purpose: a tile's empty areas get the clear
    colour, not the previous tile.
  - D3DDrv9 has other Clear paths: `UD3DRenderDevice9::Clear` 0x1002E890 (vt+0xB8) and `ClearRenderTarget`
    0x1002EDD0 (vt+0x184), both reaching FUN_10029720 with the caller's flags (ClearRenderTarget binds another
    target first), and `EnableLighting`, through FUN_1002A170, which clears TARGET only (flags 1). The
    `freecam: camera zone ...` line shows which Clear comes first: in flight, `last flags` is 0x6 or 0x7, and
    `given the colour by us` plus `cleared by the engine` is close to `first clears`. Every `camera zone` line shows
    `first clears` equal to the frames, with flags 0x7 (Lock's Clear, D3DDrv9 0x10004CA0). The Fullbright net relies
    on it.
- **Where the scene is drawn.**
  - `dnRenderer::DrawFrame` 0x1032F3B0 colour-clears the main scene through the RenDev's Clear (vt+0xB8, D3DDrv9
    `UD3DRenderDevice9::Clear` 0x1002E890 -> FUN_10029720 with the caller's flags) only in these cases:
    - Clear(2), Z only, when frame +0x10 is set.
    - The frame's own render target (frame +0x2B0, a URenderTarget: render-to-texture), bound through
      `SetMainRenderTarget` (vt+0x178, D3DDrv9 0x1002ED50 -> FUN_10003BB0, no clear of its own). It gets Clear(3),
      colour and Z, only once: when its flag +0x80 bit 0 is set, which is then cleared.
    - Clear(4) only in the light-complexity view.
  - `UD3DRenderDevice9::Lock` (0x10004CA0) also sets the frame's scratch frame buffer: device +0x1A5D0 +0x2C8 from
    FUN_10003B00(w, h), the viewport-sized target `GetScratchFrameBuffer` 0x1002F390 returns. `FUN_10003A10` binds the
    viewport's own surfaces only for a viewport that is not the main one.
  - Neither the main scene's HDR target nor the scratch target has a colour clear of its own anywhere read. Nothing
    found depends on the camera's zone, the sky zone or r_FillFogHole, besides the fog-hole fill above.
  - So the scene is drawn into an offscreen HDR target that the final HDR/uber pass (`FEMU_OP_FinalHDRPass`,
    `Post_UberPost`) copies to the back buffer. Lock's 0x7 clear lands on another surface, the back buffer, before
    that target is bound. Wherever no geometry, sky or fog fill writes the HDR target, last frame's pixels stay, and
    the composite shows them on a freshly cleared back buffer.
- **The frame trace (d3dtrace.h, logged by freecam.cpp).** It logs every SetRenderTarget, Clear, StretchRect and
  ColorFill of one engine frame, Present to Present, through d3d9 detours that pass every call unchanged. It runs at
  a flight's first seeded frame, then every 600 flown frames, and keeps at most 64 calls. Each entry has the surface
  pointer, its size and format, a Clear's flags and colour and current render target, and whether a surface is the
  back buffer.
- **Measured (frame trace 1):**
  - The engine's only colour clear in the frame is the back buffer's 0x7 (call 03).
  - The deferred G-buffer is bound at calls 13-16: RT0 3440x1440 A8R8G8B8, RT1 G16R16F, RT2 and RT3 A8R8G8B8. It is
    bound again at calls 24-26.
  - The HDR scene target (3440x1440 A16B16G16R16F) is bound at call 55.
  - None of these gets a colour clear: the stale pixels are theirs.
  - The 860x360 bloom pair and the 2048x2048 shadow target clear themselves (call 32 flags 0x1, call 61 flags 0x3).
- **The fix (rtclear.h, d3dcap.cpp):** in flight, photo included, each screen-sized render target that is not the
  back buffer is filled with 0 (ColorFill, through our detour so the trace shows it) at its first bind of a frame,
  once a frame. Skipped:
  - The first frame a target is seen (its texture is learned).
  - The motion-blur feedback target, by identity. `Post_MotionBlurFeedback` 0x1031CA80 (gated by r_DrawMotionBlur,
    0x10649ADC) binds the FAuxRenderTarget at 0x10649B2C first and only then samples it, blending the frame into last
    frame's content: the drunk and pain trail. It clears it itself only when its +0xFC flag is set. Because the bind
    comes first, the sampled-before-bind rule does not see it. Its surface is read on the game
    thread at Present, read-only and under a guard:
    - the object's cache id at +8 (`FAuxRenderTarget::GetCacheId` 0x1028A3F0), with its vtable checked (RVA 0x4947EC);
    - Duke's viewport +0x60, the RenDev, whose vtable must be UD3DRenderDevice9's (D3DDrv9 RVA 0x66FAC);
    - RenDev +0xD4, the FD3DDevice;
    - D3DDrv9's resource hash (FUN_10020D80: +0x1A5D8 + bucket x 4, nodes at +0x20, keyed lo +8, hi +0xC and
      flags 0 at +0x10);
    - the node's surface at +0x28 (FUN_1002D6F0).

    Logged when it changes.
  - A target sampled before its first bind in the frame, from then on (the second guard).
  - Any format outside the colour ones ColorFill takes: A8R8G8B8, X8R8G8B8, the 10-bit pair, A16B16G16R16,
    G16R16F, A16B16G16R16F, R32F, G32R32F.
  - A target the engine colour-cleared itself last frame. The AO pass (FUN_103177D0) clears its target with the
    RenDev's Clear(1) before drawing, and ping-pongs between two targets within the frame.
  - The eye-adaptation targets (g_LastLum) are not screen-sized.
  - SetTexture is detoured to see the reads; without it nothing fills. Logged every 600 frames: targets filled a
    frame, refused, history, left to the engine, and the fill cost.
- `r_UseFarPlane` (0x10649EDC) adds a far plane to the portal clip set (FUN_103271A0, read at 0x1032745D). Its
  distance is Duke's fog end (section 8). No distance fog and No culling force it to 0 while flying.

## 2. Input

- **The panel's gate, in both pause modes.** input.cpp already does it while the panel is open:
  - `GetKeyState` through Engine.dll's USER32 import slot reads 0 (cursorgate.h).
  - The dinput8 `GetDeviceState` detour hands the engine zeros (input.md).
  - Keyboard messages are swallowed, and held keys are released into the engine first.

  With all keys up, `UpdateInput` sees no presses, so no binding fires (no exec Fire/Jump/Use either), and Duke
  stands. The camera reads W/A/S/D, Space, Ctrl, Shift and Alt with `GetAsyncKeyState`, and the mouse from the
  counts our DI detour already accumulates (rawmouse).
- **Raw mouse stands down while flying.** Its TVRA rewrite injects our DI counts into Duke's turn, and
  PlayerMove_ScreenshotMode also raises TVRA. In flight the counts belong to the camera.
- The game's own stop, rejected: `bIsTyping` (PlayerPawn +0x12D0 bit 0x800000). `APlayerPawn::PlayerInput`
  0x102624D4 then zeroes aForward/aStrafe/aTurn/aUp/aLookUp, but buttons and exec bindings still fire.
  `bPlayersOnly`/`ForceDisablePlayersOnly` are cheat-gated level-wide switches.
- The pause path snapshots keys: `UInput::PauseGameInput` replays every key whose state changed during the pause
  (pause.md). With the gate, none changed for the engine.

### 2a. The wheel's field of view

The Manhattan Project menu's freecam wheel FOV: 2 degrees a notch, away from you narrows, within the Custom FOV slider's 40-150, separate
from the View tab's FOV and given back when the flight ends. **Lever:** `Pawn.FovAngle` (FloatProperty, flags 0x1,
resolved by name and required at +0x1274, the dword `CreateMasterFrame` reads). The engine writes it itself
(`CalculateFOV` each tick, `PlayerPawn.SetFOVAngle` in script), and the `PlayerCalcView` after-call writes it every
flown frame once the wheel has turned, after the tick and before the frame is built, so it holds paused or not. A
`ConsoleFOV` modifier was rejected: paused, nothing recomputes FovAngle (section 3). At the end the value before the
first notch goes back while ours is still in it. The notches are DIMOUSESTATE `lZ` from our GetDeviceState detour;
in flight the engine's copy is zeroed and WM_MOUSEWHEEL is swallowed, so no weapon switch. The ESP projects with it
(`world.cpp`), the Debug readout shows it, and photo tiles are drawn with it. Saved games: FovAngle is not config; a
checkpoint during an unpaused flight saves our value, and the next tick's CalculateFOV replaces it.

## 3. Pause with the camera

- `ForcePause_NoFade` / `ForceUnPause` as in pause.md. Ticks, time and animation stop; `UGameEngine::Draw` and
  StartRendering (so SetupViewInfo and our override) keep running.
- **The paused-frame cache:** `dnRenderer::DoPausedRender` 0x1032C400
  redraws the last frame (`g_PausedRT`) as a fullscreen tile instead of the scene. It does so when (bPaused or the
  level-movie flag +0x7A0) **and not bScreenshotMode**, and cvar `r_UsePausedFrameCache` (0x10649F10, bool, default
  1, flags 0) is set. A paused camera would be invisible. **Lever:** set `r_UsePausedFrameCache` 0 through GDnExec
  while flying paused, and restore it at the end. The engine's own screenshot mode gets the same effect from
  bScreenshotMode, and TakeMegaShot zeroes the same cvar.
- FOV while paused: the frame takes FovAngle (+0x1274), which only the tick changes, so it holds at its paused
  value. The FOV option (a ConsoleFOV modifier, player-options.md) does not apply until unpause.
- `world_gate` hides the ESP while paused (pause.md): the free camera's own pause needs its exemption.

## 4. Teleport

- **Move:** `Pawn.PawnSetLocation(NewLocation, bNoFail=false)` (native 864, `APawn::PawnSetLocation`
  0x101E1B40) by ProcessEvent on Duke. This is `Teleporter.Accept`'s own call; it also re-syncs a rigid body. It
  runs `AActor::SetLocation` -> `ULevel::FarMoveActor` 0x10238570, which:
  - fits the spot with the level's FindSpot (vt+0xA8, the actor's CollisionRadius +0xD8 / Height +0xDC);
  - runs `EncroachTo`;
  - sets Location and OldLocation;
  - `UpdateTouchingActors` at the destination, then `SetActorZone`.

  It returns false on no room, and Duke stays put. After it: Velocity 0, `ViewRotation` = the camera's yaw and
  pitch, `MoveTimer` -1, as the Teleporter does.
- **Red marker:** `Actor.FindSpot(out AdjustedLocation, bCheckActors, bAssumeFit=false)` (native 636,
  `execFindSpot` 0x10257E30) on Duke. It is the same level vt+0xA8 test with his collision size, and it moves
  nothing. False = red. The point is `Actor.Trace` from the camera along its view (HitLocation + HitNormal *
  (radius + 1)), with the out-params read after the call.
- **Refuse while mounted** (`MountParent` != none: turrets, vehicles, ladders), as `Ghost`/`dnWarp` do.
- **What the move skips:** nothing is touched on the way; only the destination's triggers fire. A level-transition
  trigger at the destination loads the next map. Scripted gates on the path (doors, spawns, checkpoints) do not run,
  so a teleport can strand progress: the hint says so. While paused the move applies at once and physics
  resumes on unpause.

## 5. Photo

- **The engine's mega shot** `dnRenderThreadMgr::TakeMegaShot` 0x10115910:
  - Console `MEGASHOT|TILEDSHOT N [border]` in `UViewport::Exec` 0x102156B0 (N clamped 2..128, border 0..320,
    default 64). It calls `SetDeferredMegaShot(N, border)` 0x1003BC50 (exported, thiscall on the exported
    `g_RenderThreadMgr` 0x10769030), run from `ProcessDeferredRequests` 0x10115F90.
  - For the shot it zeroes `r_DrawHUD`, `r_DrawConsole`, `r_AmbientOcclusion`, `r_UsePausedFrameCache`,
    `r_SkinMeshLOD` and `r_Occlude` (all bool, default 1, flags 0).
  - It sets `MegaShotScale` = N and steps `MegaShotXShift`/`MegaShotYShift` from 1-N by 2, then renders N x N tiles
    (StartRendering/RenderLevel/FinishRendering), reading each back with RenDev vt+0x90.
  - It writes one 24-bit BMP, `<profile>\Screenshots\<date>_<time>_MegaShot%04i.bmp`, from a single GMem buffer of
    (wN+2b) x (hN+2b) x 3 bytes. At 1080p that is ~100 MB at N=4 and ~400 MB at N=8: in a 32-bit process, large N
    will fail.
- **The projection:** `FSceneNode::UpdateTransforms` 0x10214390 scales the projection matrix (+0xD8) by
  `MegaShotScale` (0x106472B0, int, default 1, flags 0) when it is above 1. When `MegaShotXShift`/`YShift`
  (0x107E1000/0x107E1004, float, default 0) are non-zero it adds them (0x102149FA-0x10214A8E). This is an off-axis
  tile frustum at the unchanged FOV. While MegaShotScale != 1, `Post_HDRFinalTonemap` skips
  `Post_HDRLuminanceAdapt` 0x10316AC0 (0x10316E9E), so exposure is frozen across tiles.
- **The panel's photo:** the Manhattan Project port's pipeline (one tile per frame, strips streamed to a PNG on a worker) with the
  engine's projection in place of the Manhattan Project port's FOV narrowing and shear. For a photo:
  - Set the six cvars TakeMegaShot zeroes. Our free camera holds the view; pause is on or the frame cache is off.
  - Per tile, set MegaShotScale = n and the shifts, let one frame render, and copy the back buffer at Present
    (d3dcap).
  - Restore all of them at the end or on cancel.
- **Post effects per tile:** bloom, DOF, film grain and vignette run per tile (screen-space). They are switched off
  for the shot with the post-effect cvars (qol.md section 4) and restored. Only AO and adaptation are handled by the
  engine's own path.
- **Fallback:** calling `SetDeferredMegaShot` (no patch) gives the engine's BMP in its own folder. Its memory limit
  and format make it a small-N fallback only.

### 5a. Seams

Measured on 4x photos with tools/photo_seams.py: seam segments ran about 2x their neighbours. Two causes:
- **A dark last row and column per tile.** In a Map00 4x photo row 1439 reads 22 against 240 above and
  below it, and column 6879 reads 11 against 69: each frame's bottom row and right column come out dark in lit
  areas. **Fix:** every tile is rendered `PHOTO_MARGIN` (16) px wider than it keeps on each side and cropped. The
  shifts become `k (N-1-2c)` and `k (1-N+2r)` with k = kept/screen per axis (`UpdateTransforms` 0x10214390: columns
  0 and 1 of the projection times `MegaShotScale`, then column 3 times the shift added, so x' = N x + XShift). The
  photo is the screen's view less the margin, `n (W-32) x n (H-32)`. A reconstruction with this math and the dark
  edge (scratch `seamsim.py`) gives seam/neighbour 1.02 with the crop and 1.96 without, and matches the scene exactly.
- **Mirrors.** `UpdateTransforms` applies the MegaShot scale and shifts only when `!IsMirror(frame)`, so a mirror
  renders the whole view while its surface is drawn in a tile: a window's reflection jumps at every seam. **Fix:**
  `r_DrawBSPMirrors` (0x10649E08) and `r_DrawActorMirrors` (0x10649E04), UBOOL, flags 0, default 1, go to 0 for the
  capture; off, `Vis_ProcessSurfaceOrPortal` (0x10322676) draws the surface as plain BSP. Light shafts stay on: their
  slices are deprojected through the tile's own transforms (FUN_100DA8B0), so they agree across tiles.

### 5b. 16x in bands

Two whole tile-row strips within 256 MB allowed 9x at 3440x1440; a 16x strip is 54528 px wide. Each tile row is
captured in B bands (`photo_bands`, the fewest that fit, at most 16): the row's tiles are rendered once per band and
each render keeps that band's rows, so a strip is `n kw x kh/B`. B is 1 to 9x and 2 at 16x on 3440x1440, 3 at 16x on
3840x2160; the Photo size slider reaches 16x on any screen up to 4K. Cost: n x n x B renders (512 at 16x on a
3440x1440 screen), each held `PHOTO_HOLD_FRAMES` frames. The PNG is still streamed strip by strip.

## 6. Screenshot mode

- **HUD and crosshair:** cvar `r_DrawHUD` (0x10647468, bool, default 1, flags 0, never saved). In
  `UGameEngine::FinishRendering` 0x10226020 it gates the player's `PostRender` event (`PlayerPawn.PostRender` ->
  `MyHUD.PostRender`: HUD, crosshair, messages, HUD effects) and `URender::PostRender`. `r_DrawConsole`
  (0x1064746C) gates the console. The engine's screenshot tool and mega shot both use r_DrawHUD. Set it through
  GDnExec and restore on exit. `DukeHUD.bHideHUD` (ShrinkHUD/GrowHUD) is the HUD's own partial hide and is not
  needed.
- **Weapon:** in flight it is part of the world in Duke's hands (section 1c), not an overlay. r_DrawHUD does not
  remove it. Hiding it would mean bBehindView or SetHidden on the weapon actor.
- **Our overlays:** gated at our Present draw.

## 7. Fullbright

Fullbright is built on RendMap 6 (7e), not on the tonemap cvars: it holds `PlayerPawn.RendMap` for the whole flight,
paused or not, and the render-target heal makes it safe outside the world (the latch, 7e). Read in Engine.dll,
D3DDrv9.dll and Engine.u. RendMap 6 reaches only the deferred (G-buffer) surfaces; most DLC03 rocks and buildings are
forward draws it cannot reach, and no engine lever unlights them (7f). The brightness lift (7g) lifts what RendMap
leaves shaded. The probe that names each surface's path is built (7f).

What was tried and measured on the way:
- `r_NewAmbient` is dead for this. Forced with a scale of 1.0 (the portal test log), the dark areas stayed dark (7c).
- Duke Vision brightens through the **exposure clamp, not the light.** While `CameraStyle` == 1 the tonemap uses
  `r_ThermalMaxScale` 1000, `r_ThermalMinScale` 0, `r_ThermalMiddleGray` 0.04 and `r_ThermalClip` 0.001. Otherwise it
  uses the level's own `HDRMaxScale`, `HDRMinScale`, `HDRMiddleGray` and `HDRClip` (7d). With a maximum exposure of
  1000, the eye adaptation can lift a dark frame up to middle grey. **No cvar reaches the exposure without
  CameraStyle**: the thermal values are read only under CameraStyle 1, and the normal values come from map data.
- Built and replaced: Fullbright as the Duke Vision exposure, the game's own `PlayerPawn.SetCameraStyle(1)` call
  (CameraStyle is a player field). It applied, but the dark areas stayed dark: an exposure scale multiplies light
  that is not there (7e).
- The only cvar lever left is the post-tonemap grade, `r_TestToneMapping` 1 with `r_SceneHighlights` (the white
  point) lowered, and `r_SceneMidTones` as a second knob (7d). It brightens what the tonemap kept; it cannot recover
  detail the exposure crushed to 0. It was not built. The direction of each knob is read from the names, not from the
  shader.

### 7a. How a frame is lit (deferred path, `r_UseDeferred` 1)

- `dnRenderer::HandleDeferredSolidGeometry` 0x103307A0 fills the G-buffers. Only when `UViewport::IsLit` 0x10212220
  is true does it then run the lighting block:
  - FUN_10316430, the light pre-pass (`r_DrawLights`);
  - `BeginUnlitHDRDrawing`;
  - the ambient pass FUN_10319760;
  - the light passes FUN_10317D00.

  Then, again only when IsLit, it runs `Post_HDRFinalTonemap` (`r_DrawHDR`) and `HandleFogDeferred`.
- **The ambient pass** FUN_10319760 is gated by `r_DrawAmbient` (0x106499C0) and render type 5/8. It draws one
  fullscreen tile, UMiscMaterial type 0xE. D3DDrv9's `D3D_BuildMiscMaterialShader` 0x100140E0, case 0xE, builds its
  constants:
  - A light direction (device +0x22C04, rewritten every frame by the Lock path FUN_10004CA0, section 1d), turned
    into view space.
  - Two actor colours from `GetActorColor` (hardcoded names 0x377 and 0x378, not resolved) and an animated value
    (index 6). This is the level's ambient.
  - (`r_NewAmbientScale`, Scale - `r_NewAmbientBump` x Scale, `r_NewAmbientPower`) and (`r_NewAmbientR`, `G`, `B`).
  - FUN_100074E0(`r_NewAmbient`) picks the shader variant (shader 0x2D).

  With `r_NewAmbient` set, it also replaces the direction with (1,1,1)/sqrt(3) (0x1001508E). The engine's ambient
  tile is then multiplied by the AO map (`g_pAmbientOcclusionMap`, reset after the tile).

### 7b. The candidates

| Lever | What it does to the picture | Persists | Verdict |
|---|---|---|---|
| `r_NewAmbient` 1 (D3DDrv9 0x100A8698, default 0) with `r_NewAmbientScale` (0x100A0254, 0.4); R/G/B 0x100A025C/60/64 (1, 1, 0.9); Bump 0x100A0258 (0.65); Power 0x100A0268 (4) | One uniform hemispherical ambient on every surface, in the ambient pass. Textures and bump stay; lights add on top; the tonemap and eye adaptation (`Post_HDRLuminanceAdapt`) pull a brighter scene back, so it should not clip to white | no: flags 0, not saved; the direction it overwrites is rewritten by Lock every frame | tried: no visible change (7c) |
| `RMODE n` (`UViewport::Exec` 0x102156B0; writes `PlayerPawn.RendMap` +0x1734, 1..23) | In game every renderer branch reads `g_RenderType` (dnCommon export, value 5; only the editor writes it, `dnRenderer::Render` 0x1032C8A0), but `IsLit` reads RendMap. Any value but 5/8 therefore drops the ambient, the lights, the tonemap and the fog, while the rest of the frame stays in lit mode | RendMap is an actor property, so a save game may store it | **built (7e)**, with the carried-from-a-save undo |
| `r_DrawLights` 0 (0x10649DC8) / `g_LightMode` 1 (0x10808A24, FUN_1031BD50 exits at 0x1031BD6B) | Removes the dynamic lights; only the ambient is left, so darker | no | rejected (the wrong direction) |
| `r_PostLightMode`, `r_LightTest` (D3DDrv9 0x100A8C68, 0x100A8C60) | Registered, never read: the only reference to each is its registration | - | dead |
| `r_ForceBoxLight` (D3DDrv9 0x100A9818) | Only changes how the light attenuation volume texture is built (FUN_1002D940) | - | not a light mode |
| Duke Vision (CameraStyle 1, dukevision.md section 3) | Swaps the tonemap exposure and adds a blue light on Duke's camera bone. Exposure scales the light that is there, and in flight the light stays on Duke | no | built, then replaced (7e) |
| `LevelInfo.Brightness`, `ZoneInfo.AmbientLightScale` | Map data | - | rejected: the engine does not write them at run time |

### 7c. Why r_NewAmbient did nothing

- **The forcing worked and the pass runs.** The portal test log shows `r_NewAmbient set: 0 -> 1` and
  `r_NewAmbientScale set: 0.4 -> 1`. The cvar registry dump in the same log has `r_DrawAmbient` 1, `r_UseDeferred` 1 and
  `r_CompileShaders` 1. The ambient pass FUN_10319760 runs at render type 5. The first toggle may compile a shader
  variant (`r_CompileShaders` 1); no hitch line followed any of the toggles, so none was compiled: it was already in
  the map's cache (`System\Shaders\<map>_shaders.dat`).
- **The constants are rebuilt on every draw.** The material set FUN_10018E90 calls `D3D_BuildMiscMaterialShader`
  with no result cache on that path. `r_NewAmbient` goes in as a type-5 literal through FUN_100074E0 (called at
  0x100153E0), so it is part of the generated shader's key, not a uniform.
- **What the term multiplies.** Case 0xE also loads two LevelInfo actor colours (`AActor::GetActorColor` on
  `GetLevel()`'s LevelInfo, hardcoded names 0x377 and 0x378, default white) and animated value 6 (the level's
  `HDRClip`). The ambient tile is then multiplied by the AO map (7a). Best reading, not confirmed because the HLSL is
  generated and was not read: the new ambient scales the level's ambient colours, so it stays dark where the level's
  ambient is dark. The tonemap clamp (7d) would cap it anyway. AO also darkens corners under the new ambient.

### 7d. Where the exposure and the grade come from

- **Exposure.** The tonemap material (D3DDrv9 0x10015467-0x10015638) reads
  `dnAnimatedValueMgr::GetCurrentValue` (Engine 0x10314E30, D3DDrv9 import slot 0x1005A334) for indices 5, 6, 7 and 8.
  `UpdateTargetValues` 0x10314E70 fills those from an `SSceneInfo` struct (Engine.u declaration order, checked against
  the four `r_Scene*` swaps below): 5 = `HDRMiddleGray` +0x2C, 6 = `HDRClip` +0x38, 7 = `HDRMinScale` +0x30,
  8 = `HDRMaxScale` +0x34. `dnAnimatedValueMgr::Update` 0x10315430 runs at the start of `dnRenderer::Render`. It takes
  the targets from LevelInfo +0x5E4, then from **Duke's** zone (Actor +0x70; ZoneInfo +0x5E4 when its +0x4A4 bit 0x100
  is set), then from Duke's PhysicsVolume (+0xEF4; +0x7BC when +0x6F4 bit 0x2000 is set). It then lerps the current
  values towards them. So the exposure follows Duke's zone even in flight.
- **Duke Vision's branch.** At 0x100154E5 the tonemap tests the viewport actor's `CameraStyle` (+0x1500) == 1. If it
  is 1, it replaces those four values with `r_ThermalMiddleGray`/`Clip`/`MaxScale`/`MinScale` and sets a vision
  flag (local -0x28). The flag swaps the fourth constant from (`r_HDRLowOfs`, `r_HDRLowScale`) to a pair built from
  the interference amount. `Post_HDRLuminanceAdapt` 0x10316AC0 also uses another adaptation rate under CameraStyle 1.
  The blue light (dukevision.md section 3) adds light around Duke only; the full-screen lift is the 1000 maximum scale.
  `r_HDRLowOfs`/`r_HDRLowScale` (D3DDrv9 0x100A022C/0x100A0230, 0.2/0.5) are the tonemap's fourth constant outside
  Duke Vision; their meaning was not read.
- **The grade.** `Post_UberPost` (live while `r_TestUberPost` 1, qol.md section 4) reads `r_TestToneMapping`
  (0x10808A48, bool 0) at 0x1031DFB6, 0x1031DFFC, 0x1031E169 and 0x1031E2DA. When it is set, the animated values
  2, 1, 0 and 3 (`Shadows`, `MidTones`, `Highlights`, `Desaturation`) are replaced by these cvars:
  - `r_SceneShadows` (FColor 0x10808E14, 0);
  - `r_SceneMidTones` (FColor 0x10649B24, 127,127,127);
  - `r_SceneHighlights` (FColor 0x10649B28, 127,127,127);
  - `r_SceneDesaturation` (float 0x10808A4C, 0).

  Each colour byte is doubled, clamped to 255 and converted to a plane, so 127 is 1.0 and any byte from 128 up is
  also 1.0. The cvars can only lower a value from its neutral 1.0. The names match UE3's scene tonemapper, where a
  `Highlights` below 1 is a white point that brightens. This is read from the names, not the shader. Using them would
  replace the level's own grade while the row is on; the qol.md "Colour grading" row holds `r_TestToneMapping` 1 too
  (cvars.h's holder rule covers both). A design for it: force `r_TestToneMapping` 1 and `r_SceneHighlights` to
  (h, h, h) with h = round(127 / strength), leaving `r_SceneShadows` 0, `r_SceneMidTones` 127 and
  `r_SceneDesaturation` 0. If Highlights 64 darkened the picture, the knob would be a gain and the lever dead.
- Rejected:
  - `r_CalcLum` 0 freezes the adapted luminance at its last value, which is darker in a dark area.
  - Writing the level's `SSceneInfo` is map data the engine does not write at run time.
  - The `AnimatedValues` globals (0x10808268, 0x24 per entry) are rewritten every frame on the render thread.
- **A true unlit view through a non-saved cvar: none** (Engine.dll, D3DDrv9.dll).
  - `r_DebugRT` (Engine 0x1080E9C0, UBOOL) is read only in `dnRenderer::DrawFrameDeferred_Post` 0x1032E690. It calls
    `DebugMaterial` for a frame whose +0x10 render target has a material (+0x2B0): a render-to-texture debug, not a
    view mode.
  - `r_DrawGBuffer` (0x10649EFC) gates the whole G-buffer fill in `FillDeferredGBuffers` 0x1032F070.
    `r_DrawGBufferBase` (0x10649F00) gates its base draw (RenDev vt+0x1E8, FUN_10114BD0). Off, they remove
    surfaces; neither shows albedo.
  - D3DDrv9 has no debug-display cvar beyond `r_DebugComplexMaterials`. Its `DumpGBuffers` export writes files.
  - The only unlit path is `UViewport::IsLit` false. `HandleDeferredSolidGeometry` 0x103307A0 then skips the
    lighting, the tonemap and the fog, so the scene target holds no lit colour. It is reached only through RendMap,
    a PlayerPawn property a save may store (7b).
- **The Duke Vision light (dnGame `Upgrade_HeatVision.UpdateDukeVisionLight`).** It spawns a `DukeVisionLight`
  (radius `DukeVisionLightRadius`, colour `DukeVisionLightColor`) and mounts it on the Instigator, Duke, through
  `DukeVisionLightMountPrefab`. It lights around Duke, not where the camera looks. It needs the Upgrade_HeatVision
  item and spawns an actor, so it is no flight lever.

### 7e. Fullbright as built: RendMap 6

- **The Duke Vision exposure (built, then replaced).** The panel called `SetCameraStyle(1)` through scall while
  flying and gave the previous style back through the same call. The script (Engine.u, no override anywhere) only
  sets CameraStyle, and RendMap 5 for style 0. The only other script users are ClientRestart_Common, BecomeDead and
  Upgrade_ViewMode. Native readers of +0x1500 are only the D3DDrv9 material builds (tonemap, light shafts) and
  `Post_HDRLuminanceAdapt`. It brings the thermal exposure, Duke Vision's adaptation rate and
  `r_LightShaftDukeVisionBrightnessScale`, not the HUD material, the blue light, the DOF modifiers, the energy
  drain or the sound: those belong to `Upgrade_HeatVision.StartActivate`. A strength scaling
  `r_ThermalMiddleGray` ran backwards (the picture darkened as it rose), so the strength divided it (0.04 / strength).
  Measured: CameraStyle 1 was applied, but the dark areas stayed dark. DNF saves only at checkpoints and
  `TriggerCheckpoint.Trigger` calls `SaveGame` at once, so a checkpoint touched during a flight would have stored
  CameraStyle 1. A save may also store CameraStyle, because it is a PlayerPawn property.
- **RendMap.** The value is 6, UE1's REN_PlainTex next to the game's lit 5.
  - RendMap is held by cvars.h's rule (`fc_hold_step`). It is read and written by reflection: an IntProperty, which
    RMODE's dword store at `UViewport::Exec` 0x102157EF writes for 1..23; `SetCameraStyle(0)` writes 5 by script.
    No script or exec sets other values without the console, so the write is raw, as the engine's own.
  - It is given back on every exit and just before a teleport's move, and the values set and restored are logged.
  - RendMap 6 is held for the whole flight, paused or not, so a checkpoint in an unpaused flight can save it with
    the pawn. The panel undoes that. On each new level, and when the player is first read, a RendMap of 6 that we do
    not hold for this level is set back to 5 through the same write and logged `carried from a save`
    (freecam.h `freecam_rendmap_carried`). Any other value is the game's own and stays.
  - RendMap has no brightness lever, so there is no strength slider; the brightness lift (7g) is the lever.

#### The render-target latch outside the world

No freeze has been seen. The heal has not been seen firing: the camera left the BSP six
times with RendMap 6 held, and the log has no heal line and no net trip, so the latch never formed. No colour
difference was seen inside or outside. The net stays as the last resort with 3 frames; `fullbright_net_delay` stays
as an undocumented ini key. The render watch logs, samples and records only while flying, and the recorder stays
capped.

- **Symptom (two freeze test logs, RendMap 6, camera above the map, zone 0).** Frames keep presenting (5.7 ms a
  frame). From the stuck frame on, every trace starts with render target 0 on a 3440x1440 A8R8G8B8 G-buffer target
  (12582880 in the second freeze log), not the back buffer. The engine never binds the back buffer again. A healthy
  mode-6 frame (the second freeze log's trace 1) starts on the back buffer and binds it twice (call 08, call 17). A
  stuck frame makes 17-37 calls and ends after the G-buffer binds. The vectored handler sees every thread and
  logged one exception all session (the boot breakpoint): no fault. RendMap 5 did not recover it (the view stayed
  frozen after the Camera key gave RendMap 5 back, the second freeze log 803); the next flight set 6 again at 822,
  so how long 5 was held is not measured.
- **Lock and Unlock run on the game thread.** `StartRendering` 0x10226BC0 calls the viewport's Lock (vt+0x84).
  `FinishRendering` 0x10226020, called by `WaitTillNotRendering` on the game thread (engine-map.md section 5),
  calls Unlock (vt+0x8C). The job only runs `RenderLevel`. RenDev vt+0x19C and vt+0x1A0 are one empty D3DDrv9 stub
  (0x1002E4F0).
- D3DDrv9's Lock FUN_10004CA0 binds no render target for the main viewport. FUN_10003A10 sets targets 1..n to null,
  and sets target 0 only when the device's current viewport (`*this` +0x18) differs. Lock's Clear (flags 7 under
  `r_UseDeferred`) therefore lands on whatever target 0 the last frame left. So the Clear 0x7 on the G-buffer target
  in every stuck trace shows Lock succeeding: in the second freeze log Lock was not refused. The engine relies on
  each frame ending on the back buffer. A frame that does not end there leaves the back buffer holding the last
  picture, and the next Lock clears the G-buffer target instead. The +0x1E630 refusal stays possible only if Unlock
  is skipped; the freeze test log's PreRender 36/600 is the one sign of it.
- **Cause (the record test log record 3 and D3DDrv9): a render-target latch in Lock.**
  - **Trigger.** The camera leaves the level's BSP entirely: iZone 0 and iLeaf -1, the engine's outside-the-world
    state. Out of bounds but inside the BSP is fine.
  - **The normal mode-6 frame** (record 3, inside the world) never runs a tonemap. Its only bind of the back buffer
    (call 24) is D3DDrv9's target-mode switch `FUN_1002AB20` case 0. That case binds the frame's main target (render
    interface +0x2C0) and comes from the first BSP surface batch (Engine `FUN_10325350` -> `FUN_103242F0` 0x10324461).
    In lit mode 5 the tonemap switches back too.
  - **Outside the BSP no surface is drawn.** The mode-6 frame then never switches back to case 0: it ends on a
    G-buffer target (cases 1/2/6 bind `FUN_1002CCD0(0)`, the 3440x1440 A8R8G8B8 target 12582880 in the second freeze
    log). Case 10 asks for render target 0 = null, which D3D refuses, so render target 0 stays.
  - **The latch.** Lock (`FUN_10004CA0`) does `GetRenderTarget(0, &state+0x2C0)` and `GetDepthStencilSurface(&+0x2C4)`
    (device slots 38 and 39 through `FUN_1001E3B0`/`FUN_1001E3F0`). It takes whatever render target 0 the last frame
    left as **this frame's main target**, then PushState (`FUN_1002A570`).
    - Unlock's PopState (`FUN_1002A5E0`) rebinds only when the two levels' +0x2C0 differ, and here they do not.
    - So once a frame ends on the G-buffer target, every later frame's main target is that target: case 0, the
      tonemap and the post chain all write into it, and the back buffer keeps the last picture.
    - That is why RendMap 5, the flight's end and the Camera key do not bring it back.
  - The target-mode cache (FD3DDevice +0x1E690) is reset to 0xFF only by `ResetState` 0x1002E7F0, render-to-texture
    `FUN_1002D6F0` and PopState from mode 0/0xC.
- **Other paths read and ruled out.** `HandleDeferredSolidGeometry` 0x103307A0 with `IsLit` false skips the light
  passes (FUN_10316430, FUN_10319760, FUN_10317D00) and `Post_HDRFinalTonemap`; after the G-buffer fill
  (`FillDeferredGBuffers` 0x1032F070) only FUN_103203C0 and FUN_103140D0 run. `Post_DrawAll` 0x1031F1D0, called from
  `DrawFrameDeferred_Normal` 0x10330A20, runs bloom, blur, `Post_MotionBlurFeedback` (only while PlayerPawn +0x1514 >
  0), DOF or UberPost, tone mapping, the volume overlay and FXAA; several follow the player's zone and volumes (7d).
  `DrawFrames_r` 0x10330E00 draws a frame only when FSceneNode +0x3A0 (its BSP render info) is set. The paused-frame
  cache cannot latch: `DoPausedRender` 0x1032C400 rebuilds its validity (+0x240) every frame from the pause state and
  `r_UsePausedFrameCache`.
- **Measured with the render watch (rwatch log): the job is alive and the frame is drawn, but never to the back
  buffer.**
  - The job thread (56208, = dnCommon `g_RenderThreadID`) is running (exit code 259) and idle at each sample, in
    dnCommon's wait loop (+0x2047C <- +0x3A09B <- +0x3A1A1 <- +0x20610). It does not hang.
  - In episode 3 (flying, after the net gave RendMap 5 back) the job made about 115 render calls a
    frame (147036 -> 178592 between the 30- and 300-frame lines), and none bound the back buffer. The state outlives
    RendMap 5, the flight's end (1300) and a third flight (net trip 2 at 1452); no `draws again` line follows.
  - Episodes 1 and 2 (flying 0, the Entry map and a load) ended by themselves. The menu draws to the back buffer
    without binding it (Lock leaves it as target 0), so a run there is not a freeze.
  - The last-call stack had one frame: the detour itself, since that file was built without frame pointers.
  - D3DDrv9: Lock stores a screen-sized FAuxRenderTarget (FUN_10003B00, format 5, cached in FD3DDevice
    +0x1E6A0) in the render interface (+0x1A5D0) +0x2C8: the frame's scene target. The back buffer is written by a
    later copy or pass, which the record names.
- **Instruments.**
  - **The render watch** (d3dcap.cpp, rstall.h) counts Presents without an engine back-buffer bind.
    - At 30 frames it logs `d3dcap: render watch: N frame(s) presented without the engine binding the back buffer`,
      with the job thread's state from GetExitCodeThread (running or exited), and the stack of the last engine
      SetRenderTarget or Clear before that Present: where the frame's work ended.
    - It then samples the job thread as stall.cpp does (suspend, GetThreadContext, 16 EBP frames, resume): `stall:
      render thread <tid>: ...`. It samples once more at 300 frames, at most 8 times a session.
    - `the engine draws the back buffer again after N frame(s)` closes the run.
    - The job thread is learned from the detours (any engine call off the game thread), checked against dnCommon's
      `g_RenderThreadID` export.
    - Outside the world in mode 6 the engine back-buffer bind count legitimately reads 0, so watch runs there are
      expected.
    - Our own Present-time drawing (prim_d3d9.cpp) binds the back buffer and restores the game's target every
      frame, so only binds outside it count (rstall.h `rst_engine_bb_bind`).
  - crashlog gives that thread's exceptions 8 slots of their own, tagged `(render thread)`.
  - **The frame record** (d3drec.h). One whole frame of the engine's calls on both threads: SetRenderTarget, Clear,
    ColorFill, StretchRect, SetTexture on sampler 0, and vertex and pixel shader binds when the pointer changes. It
    keeps the first 64 and the last 448 calls. Each call gets its caller (the detour's return address) and four more
    frames (RtlCaptureStackBackTrace; d3dcap.cpp keeps frame pointers).
    - It is armed at each flight's first frame (a normal flown frame, 3 a session) and at a watch stall (3 a session).
    - It is logged as `d3dcap: record N #call game|job kind ... from <caller> ; <callers>`, 48 lines a Present.
    - At each flight's end, `d3dcap: render watch at the flight's end` gives the run of undrawn frames.
- **Fix (rstall.h `rst_heal_rt`).** At Present entry, before our own drawing and with the job idle, render target 0
  is set back to the back buffer whenever the engine's frame ended elsewhere.
  - It goes through the real `SetRenderTarget`, so neither the watch nor the trace counts it as the engine's bind.
  - Logged: the first 16 heals, then every 600th, plus a 600-frame count.
  - **Why it is safe.** It is the device state every healthy frame ends in (record 3; the menu never leaves it). The
    engine never means to present with render target 0 elsewhere, because Lock would adopt that target and the back
    buffer would stop updating. No engine memory is written; it is one D3D call, as our overlay already makes.
  - The size is the same (both targets are screen-sized), so the viewport D3D resets does not change.
  - **Expected outside the world in mode 6:** Lock's clear colour and whatever the frame draws to its main target, no
    freeze. Back inside, the BSP draw switches to case 0 at once.
  - One frame after a heal may skip its first target-mode switch, if the mode cache already holds that mode. It is
    at most a one-frame flash, and the next mode change rebinds.
- **The net reads Lock's target:** it trips on frames whose first Clear after Present (Lock's) is off the back
  buffer, or on frames with no Lock (the +0x1E630 refusal). With the heal it should never trip.
  `[freecam] fullbright_net_delay=N` (hidden, 1-600, default 3) sets its patience, so a research test can see the
  frozen frames first.

### 7f. Why most DLC rocks and buildings stay shaded

Read in Engine.dll, D3DDrv9.dll, nukepanel.log and the map packages, with one frame record. RendMap 6 is one switch,
`UViewport::IsLit`, and IsLit gates only the deferred passes. A surface drawn to the G-buffer loses its lighting. A
surface drawn forward keeps it, because its shading is in its own draw. In a DLC03_Map04 frame every BSP
and static-mesh draw was forward. No cvar, per-frame setting or engine call switches the forward path to unlit.

- **What reads RendMap in game.** `PlayerPawn.RendMap` (+0x1734) has 129 instruction readers in Engine.dll. Every
  renderer branch reads `g_RenderType` unless `GIsEditor` is set, and `dnRenderer::Render` 0x1032C8A0 copies RendMap
  into `g_RenderType` only in the editor. So in game the renderer always runs as mode 5.
  - RendMap reaches the frame through `UViewport::IsLit` 0x10212220 (true for 5 and 8). Its callers:
    `HandleDeferredSolidGeometry` 0x103307A0 (twice), `BeginUnlitHDRDrawing` 0x1032DA00,
    `HandleDeferredTransparentGeometry` and `Post_ToneMapping`.
  - It also reaches `AActor::STY2PolyFlags` 0x101FDA40, which adds poly flag 0x400000 (unlit) when RendMap is not 5.
    Its callers are sprites, particles and projectors.
- **What IsLit gates (HandleDeferredSolidGeometry).** These run only in lit mode:
  - the light pre-pass FUN_10316430;
  - `EnableUnlitHDR` (D3DDrv9 0x1002EF90, stores the unlit-HDR scale at render interface +0x32C);
  - the ambient tile FUN_10319760 and the light passes FUN_10317D00;
  - `Post_HDRFinalTonemap` and `HandleFogDeferred`.

  The G-buffer fill (`FillDeferredGBuffers` 0x1032F070) runs in both modes, with the lighting flags of mode 5.
- **Which surfaces are deferred: decided in D3DDrv9 by the material's class.** The material set FUN_10018E90 checks
  the class with the StaticClass imports at 0x1005A2A8 and on:
  - Only **UBumpShader** (FUN_1001D1B0), and **ULayerMaterial** over a BumpShader (FUN_1001D360), take the deferred
    build FUN_1001C570. Under the G-buffer lighting flags (`EnableLighting` 0x1002E8E0, flags 4|8) it sets render
    target mode 1, 2 or 3. These are the G-buffer MRT cases of FUN_1002AB20.
  - Every other class goes through FUN_10013340 or its neighbours: UBitmap/Texture, UCombiner, UFakeBumpMap,
    UConstantMaterial, UBumpCubeEnvMap and the fallbacks FUN_10018AB0, FUN_10013D70, FUN_100135D0. They keep the
    default target mode 0 (FUN_1000F610), which binds the frame's own target (render interface +0x2C0, the back buffer).
    They switch to mode 5 (G-buffer 4) only while EnableUnlitHDR holds a scale, and to mode 10 (depth only) under
    flag 0x100000.
  - Their colour comes from the material's own draw. The generated vertex shader passes the mesh's vertex colour
    through (`CreateVS_HLSLBody` 0x100258E0: `Out.vColor = In.vColor` when bHasVertexColor is set). That colour is
    where a static mesh's baked lighting would be. Not read: the generated pixel shader's use of it.
- **Which surfaces are listed where.** In Engine, FUN_10111E80 sorts each opaque mesh by its material's flags
  (FUN_10110F40, material vt+0x60 bit 4):
  - **+0x2008**, the base list: drawn by FUN_10114BD0 with flags 0xC (lit, G-buffer).
  - **+0x2024**, the unlit list: drawn by FUN_10114C80 with flags 8. Actors with +0x5C bit 0x400 also go here; that is
    the bit STY2PolyFlags treats as unlit.

  In lit mode the unlit list draws inside EnableUnlitHDR. In mode 6 it never does, so its FUN_10013340 materials draw
  to the back buffer at full value.
- **Measured: the DLC frame is forward.** The log has `d3dcap: record 3`, the
  first frame of flight 3 in DLC03_Map04 with RendMap 6: 237 calls, all kept.
  - After the depth pre-pass (case 10, FUN_10113550), the BSP batches, the base list and the unlit list all draw
    into the back buffer, target mode 0 (call 0032). The BSP batches are Engine 0x10324461 <- 0x1032543F.
  - **No G-buffer MRT bind happens in that frame.** The only G-buffer targets (case 3, calls 0184-0192) come from
    FUN_10114D80's list.
  - The DLC packages fit this: DLC03's texture packages export 978 Texture, 165 Combiner and 75 FinalBlend against
    306 BumpShader.
  - DLC03_Map04 holds 473 StaticMeshActors, 196 PatchActors and 269 brushes. Their materials are mostly in the
    DLC03_sm_* and DLC03_dt_* packages.
  - Read with tools/ubc.py's package reader. The per-actor material is native (StaticMesh data), so it is not
    tallied here.
- **So, in the screenshots.** The shaded cave walls, far rocks and most structures are forward draws: RendMap 6 leaves
  them as they are. The deferred fog is the visible IsLit-gated pass after them; whether the tonemap also covered them
  in lit mode was not read. That fits a Fullbright that is only slightly brighter there.
  - The few flat ones went through a path that RendMap 6 changes. Two candidates: a BumpShader surface whose lighting
    passes are gone, or an unlit-list surface now drawn at full value. Which draw writes a deferred surface's flat
    colour into the back buffer in mode 6 was not identified statically; both candidates are named by the probe's
    class chain and list.
- **Levers searched, none found:**
  - Cvars, by name in both modules: nothing for lightmaps, vertex colour, static lighting or an unlit draw.
    - `r_DrawLights`, `r_DrawAmbient` and `r_NewAmbient*` act on the deferred passes (7a-7d).
    - `r_LightShift` (D3DDrv9 0x100A00B4, read only by FUN_1000D560) is a pixel offset over the screen size.
    - `r_StaticMeshPosOnly` (Engine 0x108056C8, FUN_102EEDB0) drops every stream but position.
    - `r_DrawEmissive`, `r_DrawAmbientBumps`, `r_AllowBumpLayers`, `r_LightOptimization` and `r_unlitpasszbias`
      were matched by name only, not traced.
  - Per-frame engine settings: the forward flags come from `g_RenderType`, which the engine writes only in the editor.
    Setting it to 6 in game would reach every branch, but it is engine memory the engine does not write in game:
    excluded by the rules.
  - Material and actor switches: Actor `bUnlit` (likely +0x5C bit 0x400, the bit both readers above treat as unlit) and `Texture.bUnlit` exist.
    No engine call sets them at run time, so using them would write map data: excluded.
- **The brightness lift (7g)** is the answer for these surfaces: a Fullbright-only lift drawn by the panel at
  Present, over the game's frame and under our overlay.
  - It writes no engine memory, as the render-target heal and the overlay already draw there.
  - It cannot flatten baked shading. It can only brighten it.
  - Raising the engine's ambient or exposure cannot reach these surfaces: in mode 6 nothing tonemaps them, and in
    mode 5 the ambient tile and the lights light only the G-buffer.
- **The probe (fbprobe.cpp, freecam.h `fbp_*`, tb_0553).**
  - While Fullbright holds RendMap, every 0.5 s, it casts `Actor.TraceEx` from the camera's centre: actors, mesh
    accurate, texture, Duke ignored, 65536 uu.
  - It logs a line when the hit (actor, BSP surface, material) changes, at most 64 a flight:
    `freecam: Fullbright probe N (flight F, sample S): <class> <name> at D uu, BSP surface I; StaticMesh .., Mesh ..,
    Skins[0] .. (class); material .. (class < super < super): deferred|forward; actor bUnlit .. bNoAmbientLight ..
    bUseLightTag .. Style .. DrawType ..; material bUnlit ..`.
  - The material is STraceHitResult.Texture at +36, by Engine.u's declaration order (Actor, Location, Normal,
    BSPSurfIndex, MeshBone, Texture, UV, Time), checked against the parameter's size. Without it, the material is
    Skins[0].
  - `deferred` means the material's class is BumpShader or LayerMaterial, found by name once a level. When either
    class does not resolve, a material that is neither reads `unknown`, not `forward`.
  - **Invisible collision:** BSP surfaces with material `NoRenderGFX` (Texture, bUnlit 1) are collision the frame
    never draws. Such a hit is passed: the ray is traced again from 4 uu past it, at most 4 times, and the line ends
    `past N invisible collision hit(s)`.
  - The first sample logs the resolution once: `freecam: Fullbright probe: TraceEx ParmsSize ..`.
- **Gate (DLC03_Map04, Fullbright on):** point the crosshair at a rock that stays shaded and at one that goes flat.
  Expected: the shaded ones read `forward`, the flat ones `deferred` or an actor with bUnlit 1. A shaded surface that
  reads `deferred` would kill this reading.

### 7g. The brightness lift

Kept: it was built so it could be reverted to the plain Fullbright; 0% turns it off without a rebuild. A failure in
its draw turns off only the lift (prim_begin_isolated), never the panel and the ESP. Verified in game.

- **What:** while Fullbright holds RendMap, the back buffer is copied into a texture (StretchRect, default pool, the
  back buffer's size and format) and drawn back over itself through a ps_2_0 pixel shader: one copy and one quad a
  frame, only while active. It writes no engine memory; the forward-lit surfaces of 7f keep their shading, only lifted.
- **The curve:** per channel `out = x^(1/(1+k))`, k = 1.5 x the slider (fblift.h). Chosen over `1 - (1 - x)^(1+k)`
  because it lifts the darks harder (0.1 -> 0.30 against 0.18 at the default 60%) and keeps a slope at white (the
  exponent) where the other's is 0. It is monotonic, keeps 0 and 1, and never clips: 0.9 -> 0.95 at 60%.
- **When:** at Present, first (fblift.cpp, frame.cpp), from last frame's hold, which is the RendMap this frame was
  rendered with. Before the photo's read, so a photo tile carries it, and before the ESP, the camera's marks and the
  panel, so they are drawn over it unchanged.
- **State:** between prim_begin and prim_end, in its own BeginScene/EndScene: the D3DSBT_ALL block restores the
  shader, its constant, the texture, the sampler and the blend state, and render target 0 is put back by hand.
- **Control:** View > FREE CAMERA > Brightness lift, under Fullbright, 0-100%, default 60%, `[freecam] fblift`.
- **Fallbacks:** no ps_2_0, a failed shader, texture, copy or back-buffer query: off for the session, logged once
  (`fblift: ... failed`), and the row's hint says so. D3DERR_DEVICELOST skips the frame. The texture goes before every
  Reset and with a replaced device; the shader goes with a replaced device.
- **Cost:** the freecam 600-frame line ends `brightness lift N%: F frame(s), X ms a frame (CPU)`.
- **Check:** DLC03_Map04, Fullbright on: the shaded rocks are lighter at 60%, unchanged at 0%; the crosshair and the
  panel look as before; a photo shows the same lift as the screen.

## 8. Culling from a free camera

The visibility walk follows the camera. The frame's zone, the portal frusta, the anti-portal volumes, the occlusion
boxes and the node tests all use the frame's camera location (+0x1D8). Three mechanisms can still hide geometry
wrongly in flight, and each has a cvar with flags 0, never saved:

1. **The far plane**, `r_UseFarPlane` (0x10649EDC, 1). It sits at Duke's fog end and cuts portals beyond it. With
   No distance fog the hole it leaves shows.
2. **Hardware occlusion of actors**, `r_Occlude` (0x1064999C, 1). It uses last frame's query, with a 10/50-pixel
   hysteresis.
3. **Anti-portals**, `r_UseAntiPortals` (0x10649F34, 1). They are extruded from the camera, but placed for the
   player's paths.

The FREE CAMERA sub-row "No culling" forces all three to 0 while flying. Separately, `r_UseFarPlane` is forced to 0
whenever No distance fog is forced: the far plane exists because the fog hides what lies past it. No cost has been measured.

**Measured (portal log):** with all three forced off, holes remained in both levels, and they follow the camera's
zone. 8e has the cause (the zone and portal walk itself) and the lever.

### 8a. How the frame culls

- **The walk.** `dnRenderer::Render` 0x1032C8A0 drives a walker object (FUN_1031FE70, vtable 0x104D74CC). Its setup,
  FUN_103315C0, seeds the visible-zone mask (BSP info +0xC38) with the frame's zone (frame +0x18, CreateMasterFrame's
  `PointRegion` of the camera) and the frame's clip set (+0x2AC).
  - FUN_10322F30 walks the BSP front to back from the camera's side.
  - FUN_10321B70 culls a node unless one of its zone bits (node +0x10) is in the mask and its box passes FUN_10321A50
    against the zone clip sets.
- **Portals.** A portal surface (flag 0x4000000) seen from a visible zone goes to FUN_10322120. FUN_103274A0 and
  FUN_10326AA0 clip it against the parent clip set, and FUN_103271A0 turns it into a new clip set: the portal's plane
  plus one plane per edge through the eye. Its far zone joins the mask.
  - `r_PortalBias` 1.5 (0x10649EE0, read at 0x10327604): within 1.5 units of the portal plane the parent set is
    reused un-narrowed, which is conservative.
- **Leaves.** FUN_10321860 hands each leaf's actors to FUN_103322D0. It tests the actor's box against the zone's clip
  sets and anti-portal volumes (FUN_1031F420), then FUN_10331BB0 adds it to the draw lists unless it is occluded.
- **Temporal reuse is conservative.** A node or actor that was visible is re-tested only on its 16th frame
  ((frame ^ index) & 0xF; FUN_10321A50, FUN_103322D0). One that was culled is re-tested every frame, so a stale
  result can only over-draw.
- **No precomputed PVS.** `UModel::PotentiallyVisible` 0x10246530 returns 1 and has no callers.

### 8b. Each mechanism

| Mechanism | Camera-relevant cause | Cvar (flags 0) | Cost | Use |
|---|---|---|---|---|
| **Far plane** | `FSceneNode::UpdateTransforms` 0x10214390 writes the frame's far plane (+0x48..+0x54). Its normal is minus the view direction, and it passes through camera + direction x D. D is Duke's `GetDistanceFogEnd` when `HasDistanceFog(Duke)` and `GetDistanceFogOpacity` > 0.99; otherwise the frame's far clip (+0x2A8, 256000). `r_wFar` (0x107E0FFC) replaces D when it is above 0. FUN_10322120 passes the plane into the portal clip (FUN_103274A0 -> FUN_103271A0, read at 0x1032745D) only while `HasDistanceFog(Viewport->Actor)`. **Zones whose portal lies past Duke's fog end, measured from the camera along the view, are not drawn.** The fog-hole fill (`r_FillFogHole`, same > 0.99 condition, section 1d) normally paints exactly that hole; with No distance fog the hole shows the clear colour or the last frame. Approaching moves the plane past the next portal and the corridor appears. The plane turns with the view, which fits "from certain angles" in a fogged level. The fog is Duke's: flying out of a fogged area keeps the plane | `r_UseFarPlane` 0 | the zones past the fog end get drawn; unmeasured | **off while No distance fog is forced**, and under No culling. Not `r_wFar`: it also sets the projection's far clip (+0x2A8) |
| **Occlusion queries** (actors) | FUN_10314540 runs after the G-buffer fill. For each actor that passed visibility, FUN_103142A0 reads the previous query of its box (vt+0x154; `r_WaitForOcclusion` 0x106499A0 is the wait flag). It sets the render data's occluded byte (+0xF0), then draws a new box query against this frame's depth; no query while the camera is within 7 units of the box. **Hysteresis:** a visible actor turns occluded under `r_OccludeThresholdHide` (0x106499A4, 10) pixels, and an occluded one needs `r_OccludeThresholdShow` (0x106499A8, 50) to return. Two readers skip occluded actors: FUN_10331BB0 (with `r_SkipOccludedActors` 0x10649F30, read at 0x10331CA4) and the mesh batcher FUN_10113550 (`r_Occlude` alone, 0x1011363C and 0x10113903). In flight, the draw decision comes from a query made at an earlier viewpoint, so a fast camera sees actors pop in a frame or two late. A small, distant actor (a ground piece seen from the air) that once fell under 10 pixels stays hidden until its box covers 50. The portal log shows `r_Occlude` alone does not explain the holes | `r_Occlude` 0. The mega shot zeroes the same cvar (section 5). `r_SkipOccludedActors` 0 alone leaves the batcher's skip | every visible actor is drawn and the queries stop; unmeasured | off under No culling |
| **Anti-portals** | In FUN_103322D0, an actor of DrawType 13 (+0x285) with a mesh (+0x398) becomes an occluder. That needs `r_UseAntiPortals` on and threaded visibility off (DAT_1080E9A4 = `r_ThreadVisibility`, 0). `dnRenderer::ExtrudeAntiPortal` extrudes it from the camera (frame +0x1D8) and appends it to the zone's anti-portal list. An actor box that lies wholly inside one is culled (FUN_1031F420). This is right wherever the anti-portal sits inside opaque geometry. Designers place them for the player's paths, so from the air, or from inside a wall, a volume can hide what is visible. Not read: whether BSP nodes test them too (FUN_100DD090) | `r_UseAntiPortals` 0 | actors behind hills and buildings get drawn | off under No culling |
| `r_UseZoneSections` (0x1080E9D8, 0) | An alternate surface path (FUN_10323DD0, FUN_10324E50), off by default | - | - | leave |
| `r_StaggerActorLeaves` (0x10649DDC), `r_CullShadows` (0x10649DD4) | Registered, never read. The staggering that does exist is hardcoded and conservative (8a) | - | - | nothing to switch |
| `r_ProcessActorVisGroups` (0x10649E14, read at 0x103219CB) | Gates one more actor list per leaf (walker vt+0x60). Off can only remove actors | - | - | leave on |
| `r_FreezeVis` (0x1080E9B8, read in Render at 0x1032C92F) | Freezes the vis viewpoint at the last unfrozen camera (RenDev +0xB8..+0xCC and the frame's zone): the opposite of what flight needs. D3DDrv9 also forces the colour clear with it (1d) | - | - | leave 0 |
| Distance culling | `dnRenderer::ShouldRenderActor` 0x1032BDB0 culls actors past a radius measured from the camera (frame +0x1D8). The radius is Duke's zone's (Actor +0x70 -> +0x4C4, else LevelInfo +0x4C4; per actor +0x5C8), so it follows the player's zone, not the camera's. It is gated by the cvar `BList` (0x10649EE8, "Enable/Disable BList"), which the registry log shows at 0: inactive. `r_TexStreamDistance` (D3DDrv9 0x100A0BB8) is never read | - | - | nothing to switch |

### 8c. Outside the world (LevelInfo0, iZone 0, iLeaf -1)

In the log, the camera zone line shows LevelInfo0 (iZone 0, iLeaf -1) when the camera leaves the level's volume.

- The frame's zone is then 0. FUN_10321B70 returns "not culled" for every node while the walk's zone (BSP info
  +0xC80, from frame +0x18) is 0, and every leaf uses zone 0's clip set, which is the frame's own frustum.
- The whole BSP is walked. Only the view frustum, anti-portals and occlusion still cull, and the engine clears the
  frame (section 1d).
- Expect the whole level's cost while outside, and no portal holes.

### 8d. Per-mechanism checks

- **Underground**, at a spot that showed a hole: No distance fog on, then `r_UseFarPlane` 0 alone. The corridor
  ahead must be drawn at once. If it is not, the far plane is not the cause.
- **Above ground:** `r_Occlude` 0 alone. The ground pieces must stop blinking. If they still blink, try
  `r_UseAntiPortals` 0 alone.
- **Cost:** with No culling on, watch the hitch line and the frame rate in the largest outdoor level.

### 8e. Zone and portal culling from inside a zone

The holes come from the zone mask of the visibility walk. The walk draws a BSP node only when one of its zones is in
the visible-zone mask. The mask starts with the camera's zone and grows only through portals that zone can see. No
cvar turns the mask off. The engine has a "whole level" state of its own: **frame zone 0**, which it produces
whenever the camera is outside the world (8c).

**Lever:** while flying with the row on, write 0 to the frame's zone (FSceneNode +0x18). It is done in a before-call
handler for `PlayerPawn.PreRender(Canvas)` in the dnGame ProcessEvent slot, taking the frame from `Canvas.Frame`.
The engine writes that field itself: `CreateMasterFrame` sets it from `PointRegion`, `dnRenderer::Render` sets it
under `r_FreezeVis`, and 0 is a value the engine produces itself.

**Cost:** the whole BSP is walked with no node culling, as outside the world. Actors that tick only while their
zone is drawn may stop, the car sound among them. The lever is folded into "No culling": that row already means
"draw what is hidden". Its hint names both costs.

**Measured (the portal test log):**
- Above ground the zone lines switch at z ~ 1280. At (735, 4425, 1276-1279) the camera is in `ZoneInfo___9 (iZone 1,
  iLeaf 155)`. At z 1280-1281 it is in `LevelInfo0 (iZone 0, iLeaf -1)`. Duke's own spot (-46, 5377, 138) is also zone 1, so zone 1 is the main above-ground zone and leaf
  155 is its top.
- Underground (map02) the camera-zone lines come only from the frame-trace header. Traces 54 and 55 at (-192, 477, -471) are in iZone 20, iLeaf 121, and the camera is handed back at x = -192.0.
  Trace 57 after a paste to the same printed spot is in iZone 15, iLeaf 82, handed back at x = -192.1. The camera
  sits on the boundary between zones 20 and 15.
- `r_UseFarPlane`, `r_Occlude` and `r_UseAntiPortals` were forced to 0 in every flight (`set: 1 -> 0` lines), so
  none of them is the cause.

**How the walk builds the mask** (functions renamed in the dnf project):
1. `Vis_SetupZoneMask` 0x103315C0 (walker vt+0x40) seeds the mask (walker +0xC38, 8 dwords, so up to 256 zones) with
   the frame's zone. It also adds the frame's extra-zone list (+0x2B4, count +0x2F4, at most 16, filled by
   FUN_10330FB0 from the actor pass). Each of these zones gets the frame's clip set (+0x2AC).
2. FUN_10323040 (walker vt+0x30) stamps the frame zone's render time: ULevel +0x120 + zone x 100 = LevelInfo
   +0x6D4. It then walks the BSP front to back (FUN_10322F30).
3. `Vis_NodeCulledByZoneMask` 0x10321B70 returns "not culled" at once when the walk's zone is 0. Otherwise a node is
   culled unless its zone bits (node +0x10) meet the mask and its box passes FUN_10321A50.
4. `Vis_ProcessSurfaceOrPortal` 0x10322120 handles a portal surface (flag 0x4000000) in this order:
   - It skips the portal if the camera is behind it and it is not two-sided (0x100), or if its bounds fail the clip
     sets (FUN_100DCC40).
   - It traverses only a surface with bit 0x40000 (FUN_100DCDA0). Any other portal is drawn as a plain surface.
   - The near zone is the zone on the camera's side of the node plane, by a strict `dot - W > 0`. Its clip sets clip
     the portal polygon (FUN_103274A0, FUN_103271A0; within `r_PortalBias` 1.5 of the plane the parent sets are reused).
   - If the polygon survives, the far zone joins the mask and gets its render time.

**Why leaf 155 misses the ground** (two readings; neither is measured):
- *Above ground:* the camera is within a unit of zone 1's top, with the outside of the world above it. Any line of
  sight from there to a ground zone beyond a portal crosses zone 1's outer boundary above the portal's top edge. That
  boundary is not a portal, so nothing adds those zones. At z 1280.2 the camera is outside, the frame zone is 0, and
  everything is walked: the ground appears.
- *Underground:* on the 20/15 boundary, `PointRegion`'s leaf descent and the walk's strict plane-side test can
  disagree. The walk then takes the zone that is not in the mask as the near zone, and the far zone is the camera's
  own, which is already in the mask. The other zone is never added, so one side of the line loses the next corridor.

**Levers considered:**

| Lever | Effect | Verdict |
|---|---|---|
| **Frame zone 0 at `PreRender`** | The walk takes the outside-the-world path: no zone culling, and zone 0's clip set is the frame's own frustum (8c). Surfaces still get the back-face and clip-set test | **used**, under No culling |
| `r_FreezeVis` (0x1080E9B8) | Render sets the frame zone and the vis transforms to the last unfrozen camera (`dnRenderer::Render` 0x1032C92F) | rejected: the opposite of flight |
| `r_PortalBias` (0x10649EE0, 1.5) | Reuses the parent clip sets only near a portal plane | reaches neither case |
| RenDev +0xB0 (StartRendering writes 0 each frame) | Skips all portal processing and adds a global zone filter (DAT_107EC1B4) in 0x10321B70 | rejected: hides more |
| Frame extra-zone list (+0x2B4) | Holds at most 16 zones; a level can have up to 256 | rejected |
| `r_UseZoneSections`, `r_UseZoneInfoArrays` (0x10649E00, read only in FUN_10321860, the leaf actor lists) | Not the BSP mask | no effect on this |
| Nudging the camera off a zone plane | Might fix the underground case; does nothing for the above-ground one | not enough |

No other Engine cvar touches the mask. The `r_*` strings were scanned: `r_ShowPortals`, `r_PortalBias`,
`r_UseAntiPortals`, `r_UseZoneSections`, `r_UseZoneInfoArrays`, `r_FreezeVis`, `r_ThreadVisibility`,
`r_ProcessActorVisGroups` and `r_AllowAlwaysVisible` (`GetAllowAlwaysVisible` only).

**The hook is safe by ordering.** `UGameEngine::StartRendering` 0x10226BC0 runs on the game thread in this order:
1. `SetupViewInfo`.
2. The outside-the-world point check.
3. `CreateMasterFrame`, which sets the frame zone.
4. The audio `Update`.
5. Lock.
6. `URender::PreRender`, which does not read +0x18.
7. `Canvas.Update(frame)`, which stores the frame at Canvas +0x98.
8. `PlayerPawn.PreRender(Canvas)`: `FindFunctionChecked` on name 0x107E3FD8, then ProcessEvent vt[1] at 0x10226F85.

`StartRenderingThread` 0x101152F0 triggers the render job only after StartRendering returns, and `RenderViewport`
waits for the previous frame first. The mega shot also goes through StartRendering, so a photo gets the same frame.
`Canvas.Frame` is an IntProperty in Engine.u. It is resolved by name, and +0x98 is expected (`UCanvas::Update`
0x1021A890). Before writing, frame +0 must be the viewport and frame +4 the level (CreateMasterFrame writes both).
The panel logs `freecam: whole level: n frame(s) forced from zone z` every 600 frames.

**Audio and the car sound.** The audio does not follow the render zone:
- `UMilesAudioSubsystem::Update` (MilesAudio 0x100060E0) ignores the FPointRegion that StartRendering passes (Duke's
  Region +0x70, with the frame's coords).
- Its listeners are PlayerPawn +0x1558, an array of 0x34-byte {actor, FCoords} entries. Each is refreshed from the
  listener actor's vt+0x118 point.
- `ULevel::OccludeSound` 0x1023BC50 traces from that same point and only sets the low-pass. `PointPhysicsVolume` only
  sets the volume filter.

What does follow the camera is ticking. `AActor::ShouldTick` 0x1020FB90 skips the tick of an actor in two cases:
- its +0x54 bit 0x800000 is set and its zone's render time (ULevel +0x120 + ZoneNumber x 100) is older than +0x15C
  seconds;
- its bit 0x400000 is set and its own LastRenderTime (+0x2EC) is older than +0x158 seconds.

Render times are stamped by the camera's walk. Best reading: the car, or its sound emitter, stops ticking when its
zone stops being stamped, and the camera outside the world does exactly that. The lever should reproduce the
outside state, car sound included. The same gating already follows the camera in any flight: actors near Duke can
freeze while the camera is away.

**Checks:**
- Above ground at (734.7, 4425.1, 1279.8), yaw -61.68, pitch -27.67, with No culling on: the missing ground must be
  drawn. Note whether the car sound stops.
- Underground at (-192.0, 476.7, -471.0), yaw -175.1, on both sides of x = -192: the corridor must be drawn.
- If a hole stays with the frame zone at 0, the zones are not the cause.
- Cost: the frame time and the hitch line, with the row on against off, in the largest outdoor level.

## Hold and restore rules

- No culling (section 8): r_UseFarPlane, r_Occlude and r_UseAntiPortals come back on every exit path with the
  others. r_Occlude has a second holder, the photo; both holds follow cvars.h's rule, so whichever releases last gives
  the game's value back.
- Fullbright (section 7) holds no cvar: it holds PlayerPawn.RendMap 6 for the whole flight, paused or not. It is
  given back on every exit, before a teleport, at the Camera key and by the net. A 6 saved by a checkpoint during an
  unpaused flight goes back to 5 on the next level (`carried from a save`).
- Frame zone 0 (8e) is a per-frame write in the PreRender handler and holds nothing, so it simply stops when the row
  or the flight ends.
- `r_UsePausedFrameCache`, `r_DrawHUD`, `r_DrawDistanceFog` and the MegaShot cvars come back on every exit path
  (camera off, level change, pause menu, quit). So does the frame clear's flight flag (section 1d).
- The pause menu: closing it unpauses (pause.md), and the free camera owns that case.

## Open questions

- `bBehindView` side effects, if used to show Duke's third-person set; its other readers (aim, weapon) are unchecked.
- Whether a mega-shot tile frustum also reaches the frame's culling (`UpdateTransforms`). A tile frustum culled with
  the full frustum only over-draws.
- Whether `PlayerMove_ScreenshotMode`'s axes carry dt (the engine's own fly camera is a reference only).
- The cost of the far plane in flight (`r_UseFarPlane`), of No culling, and of frame zone 0 (the walk's cost and the
  actors that stop ticking, the car sound) is unmeasured.
- Which of the two readings for the zone-mask holes (above-ground zone-1 boundary, underground 20/15 boundary) is
  right; the frame zone 0 checks (8e) settle it.
- Fullbright: whether the generated pixel shader uses a static mesh's vertex colour; which draw writes a deferred
  surface's flat colour into the back buffer in mode 6 (7f); the gate on DLC03_Map04 (shaded rocks read `forward`)
  and the result of the invisible-collision pass in the probe are not recorded.
- Whether the render-target heal ever fires in normal play (no freeze was seen, so it was not observed firing).

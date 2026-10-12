# Duke Vision: what draws the scanlines and the distortion, and how to remove them (build 244)

The scanlines and the distortion both live in **one HUD effect**, not in the
renderer, so they can be removed without patching code or writing an ini. While Duke Vision is on,
`Upgrade_HeatVision` keeps the HUD effect `DukeVisionHUDEffect` in `DukeHUD.HUDEffects`. Its material
`dt_hud.DukeVision.DukeVision_fb` copies the frame buffer and redraws the whole
screen over it (FinalBlend `FB_Overwrite`): distorted through a FakeBumpMap (panning wobble, lens bulge, chromatic
aberration), mixed with two scanline layers, then tinted by a colour vignette. The brightening is elsewhere and stays:
the HDR tonemap swaps in the `r_Thermal*` exposure values and a blue light rides on the camera. **Lever:** on the game
thread, call the game's own `DukeHUD.RemoveHudEffect` for the three Duke Vision effect names whenever they appear.
Cost: the tint and vignette go with the scanlines (same material). Persists: nothing.

**Status:** lever A is built and works in game. View > POST PROCESSING "Clean Duke Vision" (off by
default; `src/nukepanel/dukevision.cpp`) removes the three effects at `TriggerViewRotationAdjust` (before the HUD
draws), with a Present pass for ticks that raise no event. Off restores nothing.

Method: `tools/ubc.py` on `extracted-packages/dnGame.u`/`Engine.u` (bytecode, class defaults, enums, property order by
the `Next` chain). `dt_hud.Dtx` was extracted from `System\MegaPackage.dat` with mpkgx into a scratch folder,
read only and not kept, and parsed with the same tagged-property reader. Ghidra MCP on project `dnf` (Engine.dll,
D3DDrv9.dll, dnGame.dll). Renamed and saved: `Engine.dll!0x10316E20 Post_HDRFinalTonemap`, `0x10316AC0
Post_HDRLuminanceAdapt`, `0x10808590 g_flDukeVisionInterference`, `D3DDrv9.dll!0x100140E0
D3D_BuildMiscMaterialShader` (each with a plate comment).

## 1. The toggle and the script class

- The key runs `DukePlayer.DoHeatVision` = `UseInventory('dnGame.Upgrade_HeatVision')` (dnGame.u). The item
  `Upgrade_HeatVision` extends `Upgrade_ViewMode` -> `Upgrade_SOS`. Its defaults: `CommandAlias "DoHeatVision"`,
  `InventoryDrainClass InventoryDrain_HeatVision` (the energy drain), `bAffectCameraStyle True`,
  `SpecialCameraStyle 1`.
- `Upgrade_HeatVision.StartActivate` (decoded): refuses while a HUD effect named `DukeVisionBlackHack` exists (a
  transition is running). Then `Upgrade_ViewMode.StartActivate` -> `PlayerPawn.SetCameraStyle(1)` (`PCS_HeatVision`
  of `Actor.EPlayerCameraStyle` {Normal, HeatVision, NightVision}). Then four DOF scale modifiers
  `'DukeVisionOverride'` (blur 0.5, focal range 8..512, distance 16), `MyHUD.AddHudEffect(BlackHUDEffect)`,
  `MyHUD.AddHudEffect(HUDEffect)`, `UpdateDukeVisionLight(true)` and the ambient sound `DukeVision_Amb`.
- `StartDeactivate`: the same refusal, then `RemoveHudEffect(HUDEffect.Name)`, adds `BlackHUDEffectRemove` and
  `HUDEffectRemove`, and arms the timer `DelayedDeactivation` after `HUDEffectRemove.MaxLife` (0.2 s). That clears
  the DOF modifiers, the light and the sound, and `SetCameraStyle(0)`. The timer does not depend on the HUD array.
- `TravelPostAccept` (level travel) and `PreRemove` call `RemoveAllHUDEffects`, then `DelayedDeactivation`. While
  active, travel calls `StartActivate` again, which **re-adds the effects after every level change**.

The four HUD effects (`SHUDEffect` class defaults of `Upgrade_HeatVision`):

| field | Name | Texture | life | what it is |
|---|---|---|---|---|
| `HUDEffect` | `DukeVisionHUDEffect` | `dt_hud.DukeVision.DukeVision_fb` | MaxLife -0.2 (stays), scale ramps X 0->1, Y 0.01->1, centred 480x360, viewport size | **the steady Duke Vision image: scanlines + distortion** |
| `BlackHUDEffect` | `DukeVisionBlackHack` | `Engine.BlackTexture`, full screen | 0.2 s | black behind the "CRT turn-on" ramp |
| `HUDEffectRemove` | `DukeVisionHUDEffectRemove` | `DukeVision_fb` | 0.2 s, scale ramps to 0 | the turn-off collapse (scanlines for 0.2 s) |
| `BlackHUDEffectRemove` | `DukeVisionBlackHack` | `DukeVision_BlackAlphaBlendMaterial` (FB_AlphaBlend of black) | 0.5 s, alpha 1->0 | black fade-out |

## 2. The material: where the scanlines and the distortion are (dt_hud.Dtx, group DukeVision)

The material graph, from each object's tagged properties. Enum names come from Engine.u: `EColorOperation` 3 =
`CO_Modulate2X`, 4 = `CO_Modulate4X`, 6 = `CO_AlphaBlend`, 18 = `CO_Modulate2X_With_Alpha`; `EFrameBufferBlending`
0 = `FB_Overwrite`.

```
DukeVision_fb        FinalBlend  FB_Overwrite (default), ZTest/ZWrite off  -> Dvision_new_c4
Dvision_new_c4       Combiner    CO_Modulate4X( Dvision_new_c3 , dukevision_colorvignette )   <- tint + vignette
Dvision_new_c3       Combiner    CO_AlphaBlend( dvision_fbump , Dvision_new_c2 , mask dv_final_alpha_pan )
Dvision_new_c2       Combiner    CO_AlphaBlend( dvision_fbump , dvision_c1 , mask dvision_acolor )
dvision_c1           Combiner    CO_AlphaBlend( dv_panning_scan_pan , dv_scanlines_vert_pan )  <- SCANLINES
dvision_fbump        FakeBumpMap BumpTarget dukevision_frmbuf (FrameBuffer)                  <- DISTORTION
                       HeightMap dv_fbuf_scan_pan (512x256, TexPanner rate 200), BumpScale 0.05
                       NormalMap2.HeightMap dukevision_vignette (64x64, clamped), BumpScale 0.25
                       CA_Filter True, CA_ShiftR/G 5.0, CA_ShiftR/GScale 1.5 (chromatic aberration)
```

- **Distortion** = `dvision_fbump`. It samples the frame buffer copy (`dukevision_frmbuf`) at UVs offset by a
  panning height map (the rolling wobble), plus a vignette height map (the bulge towards the edges), plus a red/green
  channel shift (chromatic aberration). Every path in the graph starts from it, so the distortion is in the whole image.
- **Scanlines** = `dvision_c1`: the rolling bar `dv_panning_scan` (512x256, panned at 200) and the fine vertical
  lines `dv_scanlines_vert` (32x32, tiled x40, panned). They are blended in twice: by the ActorColor `dvision_acolor`
  (id `DukeVisionAC`, default 70,70,70,255) and by the panning mask `dv_final_alpha` (32x32, x40). The pulse that
  `DukeVisionLight.Tick` writes to `LinesActorColor.DefaultColor` (a CLerp from `LinesMinColor` to `LinesMaxColor`
  255,255,255) is most likely that ActorColor. The link is by name only: the `LinesActorColor` default was not decoded.
- **Tint and vignette** = `CO_Modulate4X` with `dukevision_colorvignette` (128x128; its smallest mip averages
  29,20,15, so x4 is about 0.45/0.31/0.24 on average: it warms the image and darkens the edges). The pixels were not
  decoded (they live in `Textures\Compiled\*.dat`).
- Unused objects in the same group (`dvision_c2..c6`, `dukevision_c1..c4`, `dukevision_fbump`, `dukevision_scanline*`,
  `DukeVisionScanlines_*`, `dukevision_static_scale` on `dt_effects.tv_static`) are not referenced from
  `DukeVision_fb`'s chain.

Where it is drawn: `ADukeHUD::DrawHUDEffects` dnGame!0x100328C0 (qol.md section 4) draws the `HUDEffects` array.
`ADukeHUD::DrawVisionModes` dnGame!0x1002BAF0 (the only native with "Vision" in its name) is the zoom overlay
(`Upgrade_ZoomMode`) and has no Duke Vision code.

## 3. The brightening (stays)

`PlayerPawn.CameraStyle` is the byte at **+0x1500**. It is declared right before `BlurAmount` (+0x1504, qol.md) in
`PlayerPawn`'s property chain, and APlayerPawn's copy code moves it as a byte at +0x1500 (Engine.dll!0x1018953E).
Its only readers in the three native modules (`search_instructions` `+ 0x1500]`; dnGame.dll none):

- **HDR tonemap exposure:** `D3D_BuildMiscMaterialShader` D3DDrv9!0x100140E0, cases 0x19, 0x2D and 0x2E (the
  three tonemap shader variants: shader ids 0x3C, 0x61, 0x60; reads at 0x100143AB, 0x10015C7C, 0x100158B0). While
  CameraStyle == 1, they use the D3DDrv9 cvars `r_ThermalMiddleGray` (0x100A0238), `r_ThermalClip` (0x100A023C),
  `r_ThermalMaxScale` (0x100A0240) and `r_ThermalMinScale` (0x100A8690) instead of the scene's animated values. They
  also set a "vision" flag constant and replace `r_HDRLowOfs`/`r_HDRLowScale` (0x100A022C/0x100A0230) with
  (2i+1, 0.9i) from the interference amount i (below). Engine's side is `Post_HDRFinalTonemap` 0x10316E20 (MiscMaterial
  type 0x19, gated by `r_DrawHDR`, render type 5/8). **This is the night-vision brightening.**
- **Eye adaptation:** `Post_HDRLuminanceAdapt` Engine!0x10316AC0 (types 0x17/0x18, compare at 0x10316CF6) uses a
  different adaptation curve while CameraStyle == 1.
- **Light shafts:** the same D3DDrv9 function at 0x100154E5 scales them by `r_LightShaftDukeVisionBrightnessScale`
  (0x100A026C).
- **The light:** `Upgrade_HeatVision.UpdateDukeVisionLight` spawns `DukeVisionLight` (LightEx; colour 64,128,255,
  radius 1024) on the camera mount (`mount_camera`). Its `Tick` fades the intensity from 4 x
  `Level.DukeVisionIntensity` to 1 x.
- **Interference:** `StartRendering` (Engine!0x10226E11..0x10226F1E) zeroes `g_flDukeVisionInterference`
  (0x10808590). It then keeps the maximum of `1 - distance / radius` over an actor array: base+0x9B8, radius at
  actor +0x5D0. By its shape this is most likely `LevelInfo.DukeVisionInterferenceActors` with
  `RenderActor.DukeVisionInterference`; not proven by offset. No class default sets that radius in the script
  packages, so it comes from placed actors in maps. `Post_HDRFinalTonemap` passes the value as material +0xF0. Near
  such an actor the tonemap's low end shifts: a proximity disturbance in the renderer, separate from the HUD
  material. It is 0 away from them.

All the cvars above were registered with flags 0 (D3DDrv9 static initialisers 0x100587BA..0x10058B4A: `PUSH var;
PUSH 0; PUSH name`), so they are never saved (qol.md section 4).

## 4. The levers

| lever | removes | keeps | persists | code patch | risk |
|---|---|---|---|---|---|
| **A. `DukeHUD.RemoveHudEffect` by ProcessEvent for `DukeVisionHUDEffect`, `DukeVisionHUDEffectRemove`, `DukeVisionBlackHack`** whenever `HUDEffects` holds one | scanlines, rolling bar, wobble, lens bulge, chromatic aberration, the turn-on/off ramps and black flashes; also the warm tint and vignette | tonemap brightening, adaptation, the camera light, the DOF override, energy drain, sound | nothing (runtime array) | none | low: it is the game's own script function. The script's only use of the names is the transition gate in StartActivate/Deactivate (removing it allows an instant re-toggle). `DelayedDeactivation` is timer-driven. |
| B. Edit the material objects (e.g. `Dvision_new_c4.Material1 = dukevision_frmbuf` to keep only the tint; `dvision_fbump` BumpScale/`NormalMap2.BumpScale` 0 and `CA_Filter` False for distortion only; `Dvision_new_c2/_c3.Material2 = dvision_fbump` for scanlines only) | per effect | per effect | nothing, but the package is shared for the whole session | none | **risk:** D3DDrv9 compiles and caches material shaders (`r_CacheMaterialResults`), so a property write may never reach the GPU. These are writes to engine objects the engine never makes itself. |
| C. `SetCameraStyle(0)` / D3DDrv9 patch | n/a | n/a | n/a | C would need a detour | rejected: CameraStyle drives the brightening, not the scanlines |
| D. DOF row of the post-processing switches (`r_TestDOF` 1 + `r_DOFBlurAmount` 0, qol.md section 4) | Duke Vision's DOF blur (`DukeVisionOverride` modifiers) | everything else | no | none | already built; only if the blur is counted as "distortion" |
| E. Interference: none needed | n/a | n/a | n/a | n/a | it is 0 away from interference actors; zeroing it would need a write to `g_flDukeVisionInterference` the engine makes each frame anyway (StartRendering). Leave it. |

## 5. The switch "Clean Duke Vision" (View group, next to the post-processing switches)

- **On:** on the game thread, in the same per-frame hook that No screen shake uses (`TriggerViewRotationAdjust` in
  the dnGame ProcessEvent slot, qol.md section 3), read `PlayerPawn.MyHUD`. If it is a `DukeHUD`, read its
  `HUDEffects` array by reflection (`SHUDEffect.Name` at its reflected offset) once per frame. For each of
  `DukeVisionHUDEffect`, `DukeVisionHUDEffectRemove` and `DukeVisionBlackHack` that is present, call
  `DukeHUD.RemoveHudEffect(EffectName)` by ProcessEvent (`FindFunction` by name; Parms = one `name`). Removal on the
  frame it appears means it is never drawn. The array read costs one validated read of the array per frame (the
  ~12 us guarded-read rule).
- **Off:** nothing more; the next activation adds the effects again (the row's tooltip says so). Restoring a Duke
  Vision that is active when the switch turns off would be `DelayedDeactivation` and then `StartActivate` on the
  active `Upgrade_HeatVision`, as `TravelPostAccept` does; the switch does not.
- Per effect: scanlines and distortion go together. They are one material, and lever A cannot split them. The warm
  tint and vignette go too. The brightening stays: exposure, adaptation and the blue camera light.
- **Instrument:** `dukevision: removed <name> at frame n (CameraStyle c)` on each removal and
  `dukevision: active, HUDEffects count k` once per activation. In a dark area with Duke Vision on there are no
  lines, no wobble, no colour fringes, and the image is still bright; a level change re-adds the effects
  (TravelPostAccept) and they are removed again.

## Open questions

- Whether removing `DukeVisionHUDEffect` loses brightness the material's x4 colour vignette added at the centre; its
  pixels were not decoded.
- Whether the scanline pulse is driven by `LinesActorColor` = `dvision_acolor` (by name only).
- Which actors carry `DukeVisionInterference` in the shipped maps, and what the shader does with the (2i+1, 0.9i)
  pair (the HLSL is generated, not read).
- Bringing the amber tint back: lever B's `Dvision_new_c4.Material1` swap is the experiment, and it needs its own
  measurement that the shader cache picks it up.

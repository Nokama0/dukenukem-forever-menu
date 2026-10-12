# Mounted guns (turrets): how the player aims and fires them, and the levers for aimbot / triggerbot

A turret is aimed by the **same `TriggerViewRotationAdjust` event** our aimbot already edits,
so adding the turn to PitchAdjust/YawAdjust moves the gun with no new hook. The shot is the turret's own code
(`Actor.TraceFire` for the MG42, a spawned grenade or rocket for the others): it never passes `Weapon.PerformFire`,
`UpdateWeaponTraceVars` or `RecordShot`. **Silent aim does apply (section 8; verified in game),** bracketed
in `FireEffects` (MG42, launcher) and in `BeginState` / the player's `AnimCallback_AnimEnd` (SuperTurret). Fire is
**not** `PlayerPawn.FireDown`: it is `bIDnControlFire`, set by `PlayerPawn.DnControlFireEnable` / `DnControlFireDisable`
(exec functions). Tell a turret from an execution, QTE or cutscene by the class of `PlayerPawn.InputHookActor`
(`dnControlRemapperEx_Turret`), then its `MountParent` for the gun class. `Pawn.UsedSomething` does not exist in
this build.

Method: read from the script packages, the level files and `APlayerPawn::UpdateRotation`. `tools/ubc.py` over `extracted-packages/*.u`; class defaults and level instances read with throwaway Python
(a tagged-property parser over the class export and over `Maps\*.dnf`, which open with `ubc.Pkg` unchanged);
Ghidra MCP `Engine.dll!0x1026ACA0` (decompiled). Level files were read only.

## 1. The classes

| Class (chain) | Package | Shot | Defaults |
|---|---|---|---|
| `dnControl_Turret` < `dnControl` < `dnUsableSomething` < `dnDecoration` (abstract base) | dnGame | none | `bUseInstantAimRate` false, `InputScale` 7.68, `bAimingEnabled`/`bFiringEnabled` true |
| `dnControl_Turret_MG42` < `dnControl_Turret` | dnDecorations | **hitscan**, `Actor.TraceFire`, range `ShotDistance` 50000, `LargeCaliberBulletDamage`, `ShotDamage` 5 x `PlayerShotDamageScale` 4.2 for the player | pitch -8192..8192, yaw -8192..8192 (`bClampYaw` true), `bUseInstantAimRate` true, spread `MaxSpread` 256 (x 1.0 H and V) |
| `dnControl_Turret_MountedGrenadeLauncher` < MG42 | dnDecorations | **projectile** `dnGrenadeLauncherProjectile`, `GrenadeVelocity` 2500, **PHYS_Falling (2)**, not Karma: the class overrides dnGrenade's Physics 18 and its InitializeMotion, so `GravityScale` is never read (aim.md section 8), damage 80, radius 300 | MG42's limits and instant rate |
| `dnControl_Turret_SuperTurret` < `dnControl_Turret` | dnDecorations | **projectile** `dnRocket_SuperTurret`, `Speed` 16000 rising by `AccelerationScaler` 100000 per second (aim.md section 8), damage 50, radius 100, via `DecoActivity 'SuperTurret_FireProjectile'` | pitch -4096..8192, yaw -16384..16384 only if the instance sets `bClampYaw`, rate-limited `PitchAimRate` = `YawAimRate` = 8192 per second (45 deg/s), `ShotRate` 0.7 s (no reader, section 7) |

Related, not player guns: `AIInputDispatcher_Turret` (dnAI; AI pawns man the same MG42 in the same levels),
`MiniGun_BL` (the player's own weapon, not a mount), `Weapon_LightEx_*` (lights). **No vehicle guns and no separate
minigun nests exist** (dnVehicles has none; no `*Turret*` class beyond the three above).

Per-instance overrides in the level files (the live values are what matter, section 5): Map02 and Map03 SuperTurret
(Map02: pitch -128..10240, `bClampYaw`, `OverheatMaxTime` 7.5); **Map05 four MG42** (yaw -11000..10240 and -9000..9000
on two, defaults on the others); Map08 two MG42 (yaw +-12000, locked/detached by the tower sequence); Map10 two (one
with pitch -5120..4096, yaw +-6144); Map12 one grenade launcher (`GrenadeVelocity` 2600, pitch max 2048, yaw min
-7936, attached by the dropship intro); Map14 and Map14B two each (pitch min -2730, yaw +-24576, `YawAimRate`
32768); Map17 one (yaw -7186..9216); Map22 three MG42 on the wasps (`ShotDamage` 300, yaw +-12000). A log
(chapter 5, "Use pressed at dnControl_Turret_MG42") is Map05.

## 2. How the player's input aims it

- `APlayerPawn::UpdateRotation` (`Engine.dll!0x1026ACA0`, Ghidra): builds Pitch/Yaw from `aLookUp`/`aTurn`, raises
  `eventTriggerViewRotationAdjust` **unless** `this[0x9A0] & 1` or `*(this+0x12D0) & 0x100` (`bPlayerViewLock`) is set,
  then adds the (edited) deltas to `ViewRotation`. `bPlayerViewLock` is only set by `PlayerPawn.LockView` (no script
  caller); the turret code never touches either bit. `UpdateRotation` raises the event every frame while mounted: the aimbot's `s_events` count shows it.
- `PlayerPawn.TriggerViewRotationAdjust` (Engine.u) calls `PlayerPawn.InputHookActor.InputHook_ViewRotationAdjust(
  Pitch, Yaw, Roll, dt)`. While mounted the hook actor is the turret's `dnControlRemapperEx_Turret`
  (`dnUsableSomething.InternalControlRemapper`; `dnControl_Turret_SuperTurret.TriggerFunc_Kaboom` casts it so).
- `ControlRemapperEx.InputHook_ViewRotationAdjust`: when the state is not `CRS_InterpolatingIn` it does
  `ViewOffset = Normalize(ViewOffset + Rot(Pitch, Yaw, Roll))`, clamps by Min/MaxViewOffset, forwards the event to
  `InputForwardActors` (the turret), then zeroes the adjusts unless `bCameraLook`. **The control keeps its own
  rotation**, not `ViewRotation`.
- `dnControl_Turret.InputHook_ViewRotationAdjust` (first `final dnUsableSomething.InputHook_ViewRotationAdjust`, which
  only feeds MouseInputX/Y for the animation): if `AimingIsEnabled()` (MG42: false in `bInCrashMode`), it clamps
  `RotateYaw + ViewOffset.Yaw` to `[MinYaw, MaxYaw]` (only with `bClampYaw`) and the pitch to `[MinPitch, MaxPitch]`,
  takes the move as the whole `ViewOffset` (`bUseInstantAimRate`) or `min(ViewOffset, dt * AimRate)`, adds it to
  `RotateYaw`/`RotatePitch`, `ConstrainRotation`, subtracts it from `ViewOffset`, and `UpdateRotations` turns the bones
  `YawPivotBoneName` (`mount_yaw`) and `PitchPivotBoneName` (`mount_pitch`). It returns early on a frame where
  `RotateYaw != LastRotateYaw` (a one-frame sync).
- The view is the **output**: `dnControlRemapperEx_Turret.CalcView` sets `Pawn.ViewRotation` to the camera rotation
  (`TurretViewRotationOffset` rotated in, `TurretViewLocationOffset` added); the player's own `ViewRotation` is
  overwritten each frame. Defaults of both offsets were not found in the class data (probably zero or per
  instance): read them.
- Pitch is also clipped by the view limit in `ViewRotation` (`+0x1FD4/+0x1FD8`, and a 15000..49152 test in the
  hook). Rotator units are the usual 65536 per turn, as the adjusts.

## 3. The shot

- **MG42** `PerformFire` (anim callback from `FireEffects`, `Role` 4 only): `FireShot()` (shake, rumble); then with a
  `PlayerUser`: `startLocation = PlayerUser.CalcedViewLocation` (**the camera, not the muzzle**), `MuzzleRot` = muzzle
  bone rotation (`mount_muzzle_1..3`, cycled) with **Pitch replaced by `CalcedViewRotation.Pitch`**; yaw therefore
  comes from the bone (the gun), pitch from the camera. Both get `IVar(x, HorizSpreadScalar*MaxSpread)` = +-256 units.
  Then `TraceFire(start, start + dir * ShotDistance, TraceDamageType, MaxTracePenetrationCount)`, the native
  `AActor::TraceFire` (`0x10282A30`). With no player the start is the muzzle bone (AI use). Not `Weapon.PerformFire`,
  so no `UpdateWeaponTraceVars` and no `RecordShot`: the on-foot Silent aim bracket never sees it (section 8 brackets the gun's own events).
- **Grenade launcher** `PerformFire`: `Spawn(dnGrenadeLauncherProjectile)` at the **muzzle bone**, `Rot` = full
  `PlayerUser.CalcedViewRotation`; `Velocity = GrenadeVelocity * dir`, `Acceleration` 0, Damage/Radius from the
  control. Then it falls under `physFalling` (zone gravity, halved by Projectile's `bBounce`; aim.md section 8) and
  explodes on any impact.
- **SuperTurret** `FireShot`: bone `mount_beam`; pitch replaced by the camera's; trace start is the camera moved onto
  the muzzle's line; a `GetTraceTarget` (the player's `TraceLook`) result pulls `ProjectileRotation` toward the
  crosshair hit, **clamped to +-1000 units** per axis; then the rocket spawns. Heat per shot `HeatPerShot` (MG42 1.5,
  launcher 8.5, super 1.5); `OverheatLevel` 100 locks `CanFire` until the cooldown (`bInfiniteAmmo` skips heat).

## 4. How fire is pressed

- The bind (`LeftMouse=Fire | VehicleFire | ... | DnControlFireAction`, aim.md section 4) reaches a turret through
  `PlayerPawn.bIDnControlFire`, set by `DnControlFireEnable` and cleared by `DnControlFireDisable` (both exec,
  flags 0x20202, body is the one assignment). `PlayerPawn.FireDown`/`FireUp` set `bIFire` for the weapon, **and the
  weapon is lowered while mounted** (`ShouldLowerWeapon`), so the triggerbot's FireDown does nothing here.
- `ControlRemapperEx.InputHook` runs `HandleInputEventsWrapper(8, bIDnControlFire, bIWasDnControlFire)` ->
  `HandleEvents` -> `dnControl.CREvent_KeyChange` -> `ControlEventInternal(14 + type)`: `CONTROLEVENT_FirePressed` 14,
  `FireDown` 15, `FireReleased` 16 -> the `FireDown` state. The turret hook `dnControl_Turret.CREvent_KeyChange`
  handles the Zoom key only. `bIWasDnControlFire` is the engine's own previous-frame copy (no script writes it).
- Cadence: the MG42 fires on the fire animation's callback (`FireEffects` -> `PerformFire`), so it is animation-paced
  while held (rate **not measured**; no re-press needed, the key stays down). The SuperTurret fires in
  `FireDown.BeginState` and again each `AnimCallback_UserAnimEnd` (`ShotRate` has no reader, section 7). `bFiringEnabled` false
  (`TriggerFunc_DisableFiring`, level scripts) raises `CantFire`; `bOverheated` blocks until cooled.

## 5. Aim and fire levers

- **Event to adjust: `TriggerViewRotationAdjust`, unchanged** (aimbot.cpp `on_event`). Add the turn to
  PitchAdjust/YawAdjust as now; it lands in `ViewOffset` and the turret applies it (instantly for MG42 and launcher,
  at most 8192 per second for the SuperTurret, so smoothing is forced there). A pitch-invert setting may flip the
  pitch sign inside the hook (`bCorrectInverse`, `bInvertMouse`): check the first turn's sign live.
- **Origin: `PlayerPawn.CalcedViewLocation`** (the camera), already `v.eye` in `read_view`. It is the shot start for
  the MG42 and the SuperTurret. For the launcher the start is the muzzle bone (the camera is a close approximation
  only for the arc's first metres).
- **Current aim:** read the control's `RotateYaw` / `RotatePitch` (script ints, by name) with the turret's
  `Actor.Rotation`, or take the camera `CalcedViewRotation` as now. Caveat: the shot's yaw is the **bone**, its pitch
  the **camera**; the two agree only if `TurretViewRotationOffset.Yaw` is 0, so log both once per attach before
  trusting the camera yaw.
- **Weapon kind:** MG42: hitscan (range 50000, +-256 spread, `MaxTracePenetrationCount`); launcher: projectile
  2500 u/s (2600 in Map12), falling; SuperTurret: projectile 16000 u/s accelerating, a hitscan kind led by its model
  (aim.md section 8).
- **Limits:** read `MinPitch`, `MaxPitch`, `MinYaw`, `MaxYaw`, `bClampYaw` from the live control (instances override,
  section 1). A target outside the yaw range is unreachable: skip it, do not wind up `ViewOffset` (the hook clamps it
  back). Skip while `bOverheated`, `!bFiringEnabled`, `!bAimingEnabled` or MG42 `bInCrashMode`.
- **Triggerbot:** call `PlayerPawn.DnControlFireEnable` / `DnControlFireDisable` through ProcessEvent (as FireDown/
  FireUp today), only while the player is mounted and a point trace along the camera ray (the MG42 fires from the
  camera) hits a hostile; never release a press the player holds (the mouse also drives `bIDnControlFire`).
  Hold, do not pulse.

## 6. The gate (aim.h `aim_gate`, `AIM_G_ATTACHED`)

`bAttachedToUsableSomething` stays the first test; split what follows by **what is attached**. Read
`PlayerPawn.InputHookActor` (reflection by name) and test `IsA(dnControlRemapperEx_Turret)`; if so read that actor's
`MountParent`, test `IsA(dnControl_Turret)`, and classify the gun by that class: `...MountedGrenadeLauncher` (test
first, it derives from MG42), `...MG42`, `...SuperTurret`. Also require `ControlRemapperEx.CurrentRemapperState` ==
`CRS_FullyAttached` (2): interpolating in or out ignores the hook. Executions, QTEs, cutscenes and ladders use other
remapper classes (`dnControlRemapperEx_Execution`, `_ScriptedAnim`, `_Ladder`, ...) and stay closed. `ViewMapper`
(`AIM_G_VIEWMAPPER`) is set while mounted too (the turret remapper owns the camera, see "Implemented" below); the
gate opens it only for this case. The weapon gate (`AIM_G_WEAPON`) needs the mounted gun as its own weapon kind (MG42, grenade,
rocket), since the on-foot weapon is lowered. Refuse on `bDetachOnDamage` ejects (Map02 `EjectDukeFromGun`) by
re-reading the attach each frame.

## Implemented (aim.cpp / aimbot.cpp / tbot.cpp)

- **Range** (`aim_range_for`): mounted, the Aim range slider is ignored; the range is the gun's `ShotDistance`
  (hitscan) or 65536 UU (projectile gun / unread reach). The mount log line prints it.
- **Gate** (`aim_turret_mounted`, `aim_gate`'s `mountedTurret`): `InputHookActor` IsA `dnControlRemapperEx_Turret`,
  its `CurrentRemapperState` == 2 (`ControlRemapperEx.EControlRemapperState` in Engine.u: NoUserAttached 0,
  InterpolatingIn 1, FullyAttached 2, InterpolatingOut 3), its `MountParent` IsA `dnControl_Turret`. Then ATTACHED
  opens, and `ViewMapper` / `InteractiveDecoration` close the gate only when they are not the turret's own remapper /
  the turret (`aim_mount_closes`; the remapper sets `ViewMapper = self` in `ControlRemapperEx.FinalizeState`,
  `Pawn.AttachToUsableSomething` sets `InteractiveDecoration`). Everything else attached stays closed.
- **Weapon** (`aim_turret_class`, `aim_turret_kind`): by the gun's class chain, the launcher looked for first. MG42:
  hitscan, range the live `ShotDistance` (default 50000). SuperTurret: hitscan, no range cap. Launcher: projectile at
  the live `GrenadeVelocity` (default 2500); its arc is solved from the muzzle bone and the limits
  below are asked of the solved lob (aim.md section 8).
  `aim_weapon()` reports the gun (its class as `cfgName`); the on-foot
  out-of-ammo rule is skipped while mounted.
- **Limits** (`aim_turret_reachable`): a candidate needs `RotatePitch + (wantPitch - cameraPitch)` inside
  `[MinPitch, MaxPitch]` and, with `bClampYaw`, the same for yaw, all read live by name from the gun each frame. This
  is the hook's own clamp (section 2), so `TurretViewRotationOffset` and the actor's `Rotation` drop out. With an
  unread limit or rotation nothing is filtered (logged as "unread, not applied").
- **Aimbot:** unchanged event (`TriggerViewRotationAdjust`) and eye (`CalcedViewLocation`). Mounted, the camera is
  the player's free look and `ViewOffset` the gun's queue (aim.md section 9): the turn is a damped step
  from the camera (half the error an event, held while the camera has not shown the last step), and the limit tests
  ask from the gun's base `Rotation` plus `RotatePitch/RotateYaw`. Its 600-frame line counts
  frames mounted and events while mounted (the proof the event still fires).
- **Silent aim** (section 8): bracketed in the gun's own shot events. `AIM_G_MOUNTED` ("this
  mounted gun cannot be bracketed") closes the gate only for a gun that cannot be bracketed (an unknown class, a
  subscription or its event name, CalcedViewRotation, RotateYaw, the limits or UpdateRotations unresolved).
- **Triggerbot:** mounted, `tbot_press_fn` picks `DnControlFireEnable` / `DnControlFireDisable` (exec, flags
  0x20202 like `FireDown`, called the same way through scall); the game-firing test reads `bIDnControlFire`; the
  press holds (no tap, no pulse). A press is released by its own mode's function: a mount or dismount under it closes
  the gate for that frame (`tbot_mode_gate`), as do the panel and the aim gate; a level change drops it as before.
  The Aimbot-mode ray traces from `CalcedViewLocation` along `CalcedViewRotation`. Its 600-frame line counts frames
  mounted, enables and disables.
- **Logs:** `aim: mounted <gun> (remapper, CurrentRemapperState): kind, range, speed; limits ...; RotatePitch/Yaw,
  camera pitch/yaw; TurretViewRotationOffset, TurretViewLocationOffset; ViewMapper, InteractiveDecoration` once per
  attach, `aim: dismounted from <gun>` after 30 read frames off (a pause or a faulted read logs nothing; the
  triggerbot still releases at once), `tbot: DnControlFireEnable/Disable and bIDnControlFire resolved`.

## 7. Fire rate and Perfect accuracy on the mounted gun

Read from the bytecode, the class defaults and Engine.dll: the two Weapons rows reach
the mounted gun through three levers, chosen per gun by `tw_levers` (turretwep.h). The guess that the turret has its
own `AnimationControllerEx` does not hold: no turret class sets `AnimationControllerClass`, so the gun has no
controller and plays its anims through `DecoPlayAnim` -> `PlayAnimEx`. No script reads `ShotRate` (a dead default).

Facts (`tools/ubc.py`; class defaults by a positional tag scan of the class export):
- **Who shoots.** Each `dnControl` state in `States[]` names a `UserAnimName` (the player's, through
  `Pawn.SetUsableSomethingAnimation` -> the pawn's controller) and a `MyAnim` (the gun's own mesh: `PlayMyAnim` uses
  `AnimationController.SetAnimState` if there is a controller, else `DecoPlayAnimEx` -> `DecoPlayAnim`, rate 0 read as
  1). Anim notifies go to the mesh's own actor: the engine's event callback (`Engine.dll!0x10035EE0`, registered in
  `appInit` at `0x1003B5A5`) calls ProcessEvent on the actor it is given, so `FireEffects` (flag `0x04000000`, only on
  `dnControl_Turret` and MG42) fires from the **gun's** mesh.
- **MG42** `FireDown`: user `MG42_Fire`, gun `Fire` **looped** (rate 0 -> 1); transitions only on release (16),
  `CantFire`, `Overheated`, `Pain`, `StartCrash`. Cadence = the gun's `Fire` loop: each notify runs `FireEffects` (heat,
  `MuzzleIndex` 0..2 cycled) -> `PerformFire`.
- **Launcher** (`< MG42`) `FireDown`: user `MountedGrenadeLauncher_Fire`, gun `Fire` not looped at 1.2; the user anim's
  end (ControlEvent 9) goes to `idle`, a held key (15) back to `FireDown`. Cadence = the player's anim; the shot = the
  gun anim's notify (inherited `FireEffects` -> its `PerformFire`), so both are scaled by one factor (handling.md
  section 5: keep the stock order of notify and end).
- **SuperTurret** `FireDown`: user `SuperTurret_Fire`, gun `a_superturret_fire` looped at 2. Shots from
  `FireDown.BeginState` and `FireDown.AnimCallback_UserAnimEnd` (the pawn's anim end, `Pawn` -> `InteractiveDecoration`)
  -> `MaybeFireShot`. Cadence = the player's anim. `ShotRate` (0.7) has no reader in any package.
- **Spread.** `MaxSpread`, `HorizSpreadScalar`, `VertSpreadScalar` are read only in `MG42.PerformFire`, on **self**
  (the instance), and no script writes them. Launcher and SuperTurret have no random term. `GlobalSetProperty`
  (`0x10043400`) with `bAllInstances` 0 writes only the class default, which a spawned turret never reads again; with 1
  it walks every object whose class chain names the class (`FUN_1003f3f0`: ImportText at object + offset, then
  `PostEditChange`; `AActor::PostEditChange` `0x102041b0` returns after `UObject::Modify` when `!GIsEditor`, a no-op
  without GUndo), then the default. The match is by name along the class chain, so an MG42 apply or restore also
  writes the launcher's instances and default (a subclass; it inherits 256 and has no random term).
  The MG42 also kicks the view (`FireViewShake` pitch 300, `bToggleSign`) and takes its shot pitch from the camera: if
  `CalcedViewRotation` carries `CurrentShake` while mounted, that kick is a second spread (not measured).
- **Heat** (`dnControl_Turret` defaults `OverheatLevel` 100, `CoolDownLevel` 75, `CoolDownTime` 1.5; MG42 `HeatPerShot`
  1.5; launcher 8.5 with `CoolDownLevel` 50; SuperTurret `CoolDownTime` 3, `CoolDownLevel` 50): `FireEffects` adds
  `HeatPerShot` per shot unless `PlayerPawn.bInfiniteAmmo`; `Tick` always cools `(OverheatLevel - CoolDownLevel) /
  CoolDownTime` per second (MG42 16.7/s); at 100 `bOverheated` locks fire until below `CoolDownLevel`. `OverheatMaxTime`
  has no turret reader. So the MG42 overheats only above about 11 shots/s (`1.5 x rate > 16.7`); at k x a stock rate R
  it overheats after `100 / (1.5kR - 16.7)` s and then waits 1.5 s (25 / 16.7).

Levers (turretwep.cpp, once a frame on the game thread; the mount read as section 6, but every frame):
- **Accuracy (MG42):** `GlobalSetProperty("0", gun class, MaxSpread, offset, bAllInstances 1, bNoSave 1)` while
  Perfect accuracy is on and the player is mounted; the default's value from before is written back the same way on
  dismount, row off, or the first frame after a level change (the default outlives the level), only while our 0 is
  still on the default (accuracy.md rule). At most `TW_SPREAD_APPLY_MAX` (3) applies per mount, each an object-table
  walk; the apply and restore lines carry its time in microseconds. **AI turrets:** every MG42 (and launcher) instance gets 0 while the player
  is mounted, so an AI manning another MG42 in that window (`AIInputDispatcher_Turret`) also shoots without spread:
  the accuracy.md trade-off, bounded to the mount. A per-instance write of ours would break the project's rule on engine writes; this is
  the engine's own `set` path.
- **Fire rate, gun anim (MG42, launcher):** `Actor.SetAnimRate(0, stock x f, None)` on the gun by ProcessEvent while
  its script state is `FireDown` (StateFrame read as world.cpp), the stock read by `UMeshInstance::GetAnimRate`; put
  back when it leaves `FireDown` only if ours is still the channel's rate (the next state's anim usually replaces it).
  Per instance: an AI's gun is never touched.
- **Fire rate, player anim (launcher, SuperTurret):** `UpdateGlobalAnimRateScale(f)` on the player pawn's own
  controller while the gun is in `FireDown`, 1.0 back after (handling.md lever (a), on the pawn, not a weapon). It
  scales all of the player's channels, including the MG42 family's aim-grid channel, which `PostUserTick` re-zeroes
  each tick (`AnimUpdateRate(chan, 0)`).
- No panel unload exists (the DLL stays loaded and the game exits without detach); bNoSave keeps the default out of
  the ini.
- **Overheat recommendation:** do not fold "no overheat" into Fire rate; at 400 % a held MG42 will overheat within
  seconds. The game's own skip is `PlayerPawn.bInfiniteAmmo` (no heat added, `Tick` clears `bOverheated`), which also
  covers weapon ammo: offer it as its own row (a design decision) rather than a write to `TotalHeat`.

Logs: `turret: mounted <gun>: levers ...; MaxSpread default D, instance I; gun AnimationController set|none, channel 0
rate R; player AnimRateScale S; fire P%, accuracy on|off`, `turret: <cls>.MaxSpread 256 -> 0 (...)` / `restored 256`,
`turret: <gun> gun channel 0 rate 1.000 -> 4.000 (Actor.SetAnimRate; FireDown)`, `turret: player AnimRateScale ...`,
`turret: <gun> fire held: N shots over S s = R/s (counted by MuzzleIndex | TotalHeat rises; fire P%); TotalHeat H,
bOverheated b` per release, `turret: dismounted from <gun>: ...` with the counts.

## 8. Silent aim on mounted guns: a bracket exists for all three

Read from the bytecode, Engine.dll and dnAnimation.dll; verified in game. Silent aim applies on all three guns, through two hooks. The shot
never reaches `UpdateWeaponTraceVars` / `RecordShot`, but it runs inside an event that passes our dnGame slot:
- **MG42 and launcher: `FireEffects`, raised on the gun.** It is an anim notify on the gun's mesh, and
  `Engine.dll!0x10035EE0` (renamed `AnimNotify_ProcessEvent`, registered in `appInit` at `0x1003B5A5`) calls it as
  `(*actor)[1] (UFunction, parms, 0)`. That is a virtual ProcessEvent. The gun classes are not native
  (`dnControl_Turret*` export flags `0x000f0004`), so the nearest native class is `dnControl` (`0x040f0004`). Its
  vtable `AdnControl` (`dnGame.dll!0x1005B23C`) holds thunk `0x10047CBA` in slot 1, so ProcessEvent on the gun goes
  through the import slot that pehook already owns. A before-handler and an after-handler on `FireEffects` therefore
  enclose `PerformFire` in the same call stack.
- **SuperTurret: `BeginState` on the gun (first shot) and `AnimCallback_AnimEnd` on the player (later shots).**
  `UObject::GotoState` (`Engine.dll!0x10025610`) raises `BeginState` (name 0x139) through `vt[1]`.
  `FireDown.BeginState` calls `MaybeFireShot` -> `FireShot`. The later shots come from `Pawn.AnimCallback_AnimEnd`
  (no script caller; the name is in no engine binary, so it comes from the anim data like the notifies above). On
  ADukePlayer that call goes through the slot. Its script chain to the shot is `InteractiveDecoration.AnimCallback_UserAnimEnd`
  -> `FireDown.AnimCallback_UserAnimEnd` -> `MaybeFireShot` -> `FireShot`. 
- Every other link in the chain bypasses the slot: script-to-script calls (`PerformFire`, `FireShot`,
  `GetTraceTarget`), natives (`TraceFire`, `Spawn`, `TraceLook`/`TraceEx`, `DecoActivity`), and events on the
  projectiles. `dnRocket_SuperTurret.PostVerifySelf` reads `ProjectileRotation`, but rockets and grenades derive
  from `Projectile`, whose vtable is in Engine.dll (`AProjectile`).

Chains from the press. **[slot]** marks a call that passes ProcessEvent through our slot; the shared prefix is section 4:
`DnControlFireEnable` [slot] -> `bIDnControlFire` -> `ControlRemapperEx.InputHook` -> `CREvent_KeyChange` ->
`ControlEventInternal(14)` -> `GotoState('FireDown')` -> `BeginState` [slot].
- **MG42:** `StartFiring` plays the gun's looped `Fire` anim. Each notify runs `AnimNotify_ProcessEvent` ->
  `FireEffects` [slot] -> (script) `dnControl_Turret.FireEffects` (heat) -> muzzle flash -> `PerformFire` ->
  `FireShot` (shake, rumble) -> `BoneGetRotate` -> native `TraceFire`.
- **Launcher:** the same up to `FireEffects` [slot] -> its `PerformFire` -> `FireShot` -> native
  `Spawn(dnGrenadeLauncherProjectile)`; then the script sets `Velocity`.
- **SuperTurret:** `FireDown.BeginState` [slot] -> `MaybeFireShot` -> `CanFire` -> `FireShot` -> `GetTraceTarget` ->
  `TraceLook` -> native `TraceEx`. Then native `DecoActivity` spawns `dnRocket_SuperTurret`, whose own
  `PostVerifySelf` (Engine vtable, not our slot) calls `SetRotation(GetProjectileRotation())`. Later shots:
  `Pawn.AnimCallback_AnimEnd` [slot] -> ... -> `MaybeFireShot`.

### The values that set the direction (section 3, re-read with `tools/ubc.py`)

| Value | Read by | Who writes it, and when |
|---|---|---|
| `PlayerPawn.CalcedViewRotation` (+0x14D0) | MG42 and SuperTurret: **pitch only** (`MuzzleRot.Pitch = ...Pitch`). Launcher: **pitch and yaw** (`Rot = CalcedViewRotation`) | the engine, every rendered frame (`SetupViewInfo` after `PlayerCalcView`, freecam.md 1a). The renderer takes its own copy (RenDev +0xB8/+0xC4), so a bracket inside the tick never reaches the screen |
| `PlayerPawn.CalcedViewLocation` (+0x14C4) | MG42: trace start. SuperTurret: start of the `GetTraceTarget` trace (projected onto the muzzle line) | the same, every rendered frame |
| muzzle bone world rotation (`mount_muzzle_1..3`, `mount_beam`) | MG42 and SuperTurret: **yaw** | rebuilt from `RotateYaw`/`RotatePitch` by `dnControl_Turret.UpdateRotations` on every `PostUserTick` and every `OnEvalBonesHelper` (both events) and on each input move |
| `dnControl_Turret.RotateYaw` / `RotatePitch` | `UpdateRotations` -> `RotateBoneAroundAxis` on `mount_yaw` / `mount_pitch` | the turret script, on each input event that moves the gun (`InputHook_ViewRotationAdjust`). Read every tick. **Not rebuilt every tick: a value we leave behind stays** |
| `GetTraceTarget` (= `User.TraceLook` = `TraceEx` from TraceStart along `MuzzleRot`) | SuperTurret: pulls `ProjectileRotation` toward the hit, clamped to +-1000 per axis relative to `MuzzleRot` | computed once per shot, not stored |
| `ProjectileRotation` | `dnRocket_SuperTurret.PostVerifySelf` -> `SetRotation` | written by `FireShot` just before `DecoActivity` spawns the rocket. Nothing we can see runs between that write and the read |

The bone moves within the call: `RotateBoneAroundAxis` -> `s_RotateBoneAroundAxis` (`Engine.dll!0x10303FE0`) ->
`dnBone::SetAbsoluteTransform` (`dnAnimation.dll!0x100389B0`). That calls `DirtyRelative`, which marks every child
bone `DirtyAbsolute` recursively (`0x1002AE70`). `GetAbsoluteTransform` (`0x1002A540`) recomputes a dirty bone when
it is read, and `BoneGetRotate(name, true)` (`Engine.dll!0x103061D0`) reads through it. So once `UpdateRotations`
has run, `BoneGetRotate(muzzle, true)` already returns the turned muzzle. `UpdateRotations` relies on the same thing:
it reads `BoneGetRotate(yaw bone, true)` right after rotating it.

### The brackets, by gun

- **Launcher: feasible, one value.** In the before-handler on `FireEffects` (object = the player's mounted gun,
  `PlayerUser` set): save `CalcedViewRotation` and set it to the solved lob direction from the muzzle bone
  (aim.md section 8). The after-handler restores it. `PerformFire` spawns the grenade at the muzzle with
  `Velocity = GrenadeVelocity x vector(Rot)`. This is the CurrentShake bracket in the same form.
- **MG42: feasible, two values.** Before `FireEffects`:
  - **Pitch:** set `CalcedViewRotation.Pitch` to the pitch from `CalcedViewLocation` to the target.
  - **Yaw:** set `RotateYaw` to the value whose muzzle yaw points from `CalcedViewLocation` at the target. Clamp it
    to `[MinYaw, MaxYaw]` when `bClampYaw`; the script never holds a value outside those limits. Then call the gun's
    `UpdateRotations` through ProcessEvent (scall).
  - **After:** put `RotateYaw` back and call `UpdateRotations` again. That restores the bones, `LastRotateYaw` (the
    input hook's one-frame sync) and `BoneRotateAbs`/`BoneGunAbs`. Restore `CalcedViewRotation` last.
  - The split limits only what we must write: yaw goes through the gun's state, pitch through the camera. A target
    outside the yaw limits stays out of reach, as with the aimbot.
  - Mapping: bone yaw is about `Rotation.Yaw + RotateYaw` (`aim_gun_view`). Read the muzzle once through
    `MeshInstance.BoneGetRotate` after the first `UpdateRotations`, correct, and log the residual.
  - The +-256 spread is still added afterwards (Perfect accuracy zeroes it, section 7).
- **SuperTurret: feasible, the same two values,** in the handlers of `BeginState` (only the SuperTurret's
  `FireDown.BeginState`, matched by UFunction, with `CanFire` and `bFiringEnabled`) and of the player's
  `AnimCallback_AnimEnd` (only while the gun's state is `FireDown`):
  - Set `CalcedViewRotation.Pitch` and `RotateYaw` (plus `UpdateRotations`) so that the `mount_beam` line runs
    through the intercept point.
  - The trace then runs along that line, and its hit (if any) lies on the line, so the +-1000 pull is about 0 and
    `ProjectileRotation` = `MuzzleRot`.
  - Without the `RotateYaw` half, the pull alone corrects at most 1000 units (5.5 degrees). It does so only when the
    trace hits something on the target's line; a lead point in open sky gives no hit and no pull.
- Rejected:
  - Writing `ProjectileRotation`: no visible call between its write and the rocket's read.
  - `TurretViewRotationOffset`: it moves the camera, not the shot.
  - A lateral `CalcedViewLocation` shift for the MG42 yaw: a parallel ray through the target. It works only for
    hitscan and moves the trace origin by d x sin(angle), possibly through cover. Keep it only as the fallback if
    the bone route fails to measure.

### Side effects

- **Camera:** none expected. Both CalcedView values are rewritten every rendered frame before use, and the after-handler
  restores them anyway. Inside the bracket, `Actor.EffectIsRelevant` reads them, so the muzzle flash (MG42:
  triggered inside `FireEffects`), a casing or the launcher's `FireShake` may be culled or spawned for that one shot
  (cosmetic).
- **Gun model:** the bones are turned only between the two `UpdateRotations` calls and are restored before the frame
  renders. The muzzle flash and casing of that shot start at the turned muzzle, a few units off (cosmetic).
  `IncrementRevision` makes the render thread resync the skeleton, which it does anyway on every tick.
- **A missed restore:** CalcedView heals at the next `SetupViewInfo`. **`RotateYaw` does not heal:** the gun would
  stay turned by the delta. So restore in the after-handler, and only while the value is still ours (accuracy.md
  rule). pehook runs after-handlers only on the game thread, and runs a before-handler off it only when the handler
  asked for any thread, which ours do not. A notify raised off the game thread is therefore never bracketed, so no
  half-bracket can happen.
- **Network:** single player only. `PerformFire` and `UpdateRotations` run only at `Role` 4. AI-manned guns are
  filtered out by the object test (the player's `MountParent`).
- **Heat, rate and spread are unchanged.** `FireEffects` adds the heat before `PerformFire`, as now.

### Implementation (aim.cpp's mounted block, mountaim.h, tests 0575-0581)

- **Hooks:** pehook before and after `FireEffects`, `BeginState` and `AnimCallback_AnimEnd` (six subscribers;
  `PE_MAX_SUBS` raised from 24 to 32).
  - `FireEffects` counts only on the player's mounted gun.
  - `BeginState` counts only on that gun, and only for its FireDown state (the UFunction's outer).
  - `AnimCallback_AnimEnd` counts only on the player, and only while the gun's state is `FireDown`.
  - `ms_hook_brackets` pairs each hook with its gun.
- **Aim:**
  - It comes from the shared pick and its turret limits. The direction is computed from the shot's real origin: the
    camera for the MG42, the next muzzle for the launcher, `mount_beam` for the SuperTurret. The projectile guns use
    the aim.md section 8 intercept.
  - Launcher: the whole `CalcedViewRotation` is written. MG42 and SuperTurret: only its pitch, plus `RotateYaw`:
    `ms_yaw_plan` takes the muzzle bone's world yaw from `MeshInstance.BoneGetRotate`, then `UpdateRotations` runs
    through ProcessEvent.
  - The muzzle is read again after `UpdateRotations`. One secant step (`ms_yaw_secant`) follows when the yaw is
    still more than 32 units off.
  - A `RotateYaw` past `[MinYaw, MaxYaw]` by more than 64 units is not redirected; a value within that slack is
    clamped to the limit.
- **Restore (`MsBracket`):**
  - Only the after-call of the call that opened a bracket closes it (same hook, UFunction and gun). A hook raised
    inside an open bracket writes nothing (counted as nested). The same UFunction raised again inside its own bracket
    is counted in a depth, so its after-call does not close the outer bracket.
  - A second yaw step that the limit clamps back onto the first value is skipped.
  - Values are put back only while they are still ours. `RotateYaw` goes back first, followed by `UpdateRotations`;
    `CalcedViewRotation` goes back last.
  - A bracket whose after-handler never ran is closed at the next frame start (counted as left open). A level change
    drops it without writing.
- **Triggerbot:** `aim_redirect_ready` is the mount's bracketability while mounted. Locked mode therefore counts a
  mounted shot as covered, as on foot.
- **Logs:**
  - Per bracketed shot (32 per level): `aim: mounted shot N, <gun> via <hook>: target ..., want pitch/yaw;
    CalcedViewRotation a -> b; RotateYaw r -> r', muzzle yaw y0 -> y1 (off, steps)`, then one gun-specific part:
    - MG42: the target's `Health` before and after the shot, and whether the shot's line (camera, camera pitch,
      muzzle yaw) hits the target by the bullet trace.
    - SuperTurret: the rocket's `ProjectileRotation` and its offset from the wanted direction.
    - Launcher: the grenade's `Velocity` direction, read the next frame from the object snapshot (the grenade
      nearest the muzzle).
    - Each line also says whether each value was put back.
  - The 600-frame line `aim: mounted silent aim` gives, per hook: events seen, brackets and shots inside them (a
    shot = `MuzzleIndex` stepped, or `bFireLeft` flipped). It also gives the no-shot count, the bracket counters,
    put-backs refused, the reasons for not redirecting, the muzzle-yaw residual, and how often the MG42 target's
    Health fell.

### Checks the logs make

`FireEffects` on the mounted MG42 or launcher passes the slot, on the game thread (counted per mount beside the `fire
held` shot count). The player's `AnimCallback_AnimEnd` passes the slot once per SuperTurret shot after the first. After
the bracketed `UpdateRotations`, `BoneGetRotate(muzzle, true).Yaw` equals the wanted yaw (the residual is logged). The hit
actor of a bracketed MG42 shot is the target; the grenade's and rocket's launch rotation equals the solved direction
(the projectile's `Rotation`/`Velocity` read on the next frame).

## Open questions

- `TurretViewRotationOffset` / `TurretViewLocationOffset` values and the bone vs camera yaw difference.
- The MG42 and SuperTurret real fire rates (the `fire held` line, section 7), `MaxTracePenetrationCount`, and whether the
  pitch-invert sign flips inside the turret hook for the player.
- Whether the live gun lacks an `AnimationController` (the mount line's `AnimationController none` says so), and the MG42's held rate
  at 100 % vs 400 % (the fire-held line should show about 4x; equal rates would kill the gun-anim lever).
- The launcher and SuperTurret fire rates (heat rises undercount above one shot a frame and read 0 with infinite ammo).
- That the spread line's instance value reads 0 after apply, and a checkpoint save while it is applied (instance and
  default both 0).
- Whether `CalcedViewRotation` carrying `CurrentShake` while mounted makes the MG42 kick a second spread.

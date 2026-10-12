# Perfect accuracy: what moves a shot (build 244)

Zeroing the spreads works (read from Engine.dll and the bytecode; confirmed in game with the No recoil switch). The
four `WeaponConfig` spreads on the class default are the only random term in `AWeapon::PerformFire`, and the shot reads the default we write. The spread still seen on the pistol is
its **view kick**: `Weapon.FireSuccess` starts the config's `ViewKicks` as random view shakes, and single-player
shots aim along `ViewRotation + CurrentShake` (`PlayerPawn.GetWeaponAim`). The pistol's kick reaches about +-2.2
deg pitch and +-1.1 deg yaw. Its spread was only +-0.29 deg. The kick is removed by its own switch, No recoil
(section below).

## The path of one shot (single player)

1. `Weapon.FireSuccess` (Engine.u): `WpnFire()` (plays the fire animation, `SetWeaponState(4)`), then for the
   locally controlled player `StartWeaponViewKick(PlayerPawn(Owner))`.
2. The fire animation's event runs `Fire_Effects` (`Pistol.Fire_Effects` calls `Weapon.Fire_Effects`), then
   `DoFire_Effects`, then native `PerformFire()`.
3. `AWeapon::PerformFire` `Engine.dll!0x100D0260`:
   - calls `Owner.UpdateWeaponTraceVars()`. Its `GetWeaponTraceVars` (`0x1026CFA0`) is `ViewRotation` only, no
     shake, and `Weapon.GetTraceFireAim` reads it only when `IsMP()`.
   - `cfg = *(weapon+0x9D0) -> +0x508`: the config class's default object, the same one `GlobalSetProperty`
     writes (`*(class+0x508) + offset`, `0x10043400`). No instance copy exists.
   - `eventGetShotError(&H, &V)`: `Weapon.GetShotError` reads `WeaponConfig.static.default`, so `HorizShotError`
     and `VertShotError`, the `Shrunk*` pair when `bUseShrunkShotError` and the instigator's `DrawScale` is below
     its default, then x `ZoomErrorPct` when zoomed. There is no movement, crouch or bloom term. Only
     `MP_MachineGun` overrides it, with a time-based bloom (multiplayer).
   - `eventGetTraceFireAim(&start, &dir)`: in single player this is `Instigator.GetWeaponAim`.
     `PlayerPawn.GetWeaponAim` returns `vDir = vector(ViewRotation + CurrentShake)`, with the shake scaled by
     `CurrentFOV / DefaultFOV` when zoomed.
   - It fires `[cfg+0x88]` traces. Trace k >= `[cfg+0x8C]` gets `dir += (rand/32767 - 0.5) x H x right +
     (rand/32767 - 0.5) x V x up`, but only if H > 0 or V > 0. With H = V = 0 no trace is randomised. The only
     other `rand` is `srand(seed)` in MP.
   - `Owner.RecordShot(this)`: `PlayerPawn.RecordShot` adds one to `AccuracyShots`. The probe counts shots by this.
4. Projectile weapons spawn along the same `GetWeaponAim`, with no H/V term.

## The view kick (recoil): which guns kick, and through what

- `Weapon.StartWeaponViewKick` calls `PlayerOwner.ShakeView(WeaponConfig.default.ViewKicks[i], true)` for each
  entry (and `AddRumble` for `RumbleKicks`). `StopWeaponViewKick` stops the looping ones (duration < 0) when
  firing ends.
- `PlayerPawn.ShakeView` turns `Style` into axis flags (`BuildShakeFlags`: 0 pitch, 1 yaw, 2 roll, 3
  pitch+yaw ...), then `AddViewShake` (`0x10263A40`, then `SetupShake` `0x10261BF0`). Each tick
  `APlayerPawn::UpdateViewShake` (`0x10267CB0`, from `PlayerTick` after `PlayerMove`) rebuilds `CurrentShake`
  (+0x1580) through `ProcessShake` (`0x10265AB0`). Per axis, `SolveShakeAngle` (`0x10263AF0`) picks
  `(rand/32767 - 0.5) x magnitude`, so each shot gets a **random** angle up to +-magnitude/2. The shake decays
  over `ShakeDuration`.
- `APlayerPawn::PlayerCalcView` (`0x10268730`) adds the same `CurrentShake` to the camera. Bullets go where the
  kicked camera points, and the kick is gone 0.06-0.2 s later, so the holes scatter around the crosshair.
- Defaults (dnGame.u, tagged class defaults; magnitude in rotator units, 65536 = 360 deg):

| Config | H / V (rad) | ViewKicks |
|---|---|---|
| PistolWeaponConfig (DualPistol inherits it, H/V 0.04) | 0.01 / 0.01 | pitch 800 + yaw 400, 0.06 s |
| ShotgunWeaponConfig | 0.11 / 0.11 | pitch 2048 + yaw 800, 0.08 s |
| MachineGunWeaponConfig | 0.07 / 0.04 | pitch+yaw 50, looping while firing |
| RailgunWeaponConfig | 0.01 / 0.01 | pitch 1024, 0.08 s |
| RPGWeaponConfig | - | pitch 900, 0.1 s |
| DevastatorWeaponConfig | - | pitch 650 + yaw 50, 0.08 s |
| NukeWeaponConfig | - | pitch 1500, 0.2 s |
| FreezeRay / EnforcerGun / ATLaser | - | pitch+yaw 32, looping |

- The No recoil switch zeroes each entry's `ShakeMagnitude` on the same class defaults (below). Damage view
  kicks (`DamageType.CreateDamageViewKick`, entries `DamageShake_Pitch/Roll`; +0x1568 is the stop residual, qol.md) also go through `CurrentShake`; the switch leaves them.

## No recoil (the switch)

- A second row in Weapons > PERFECT ACCURACY, default off, key `[weapons] norecoil`. Perfect accuracy still
  zeroes only the spreads.
- The write: `GlobalSetProperty(L"0", cls, ShakeMagnitudeProperty, offset, bAllInstances 0, bNoSave 1)`, where
  offset = `&ViewKicks[i].ShakeMagnitude - default`. `GlobalSetProperty` calls `prop->ImportText(value, *(cls+0x508)
  + offset, 1)` (vt+0x90), and `UIntProperty::ImportText` (`0x1004BD00`) writes `_wtoi(text)` at exactly that
  address. It does not use its own Offset, so 32-bit wrap reaches the array's own block. bNoSave 1 skips
  `SaveConfig`. Each kick is written in place. This avoids two struct-text paths. The array-level text would
  rebuild the array. The struct-level text, `UStructProperty::ImportText` (`0x1004E090`: `Struct` = +0x78,
  members from `Struct+0x58`, element = data + ElementSize x index + member Offset), skips a member that fails its
  vt+0xA4 check.
- Layout, all by reflection: `ViewKicks` is an ArrayProperty with Inner at +0x78. The Inner StructProperty has its
  Struct at +0x78 and its ElementSize at +0x3E. `ShakeMagnitude` is found by name among the struct's members. The
  array is {data, count} on the default.
- Restore: each kick's value from before ours is written back as `%d` text, only while ours (0) is still in place.
  A class that stays loaded keeps the record across levels. One that is reloaded starts a new record and gets
  re-applied. Values are logged once per class per level: `recoil: <Config>: ViewKicks n, ShakeMagnitude 800,400 ->
  0,0 (...)`. Only the class the held weapon names gets a record.

## Instrument

`accuracy.cpp` probe: once per weapon class and switch state, on the frame after a shot (`AccuracyShots` rose),
it logs the four spreads, `ZoomErrorPct` and the kicks' `ShakeMagnitude` from the class default, plus
`CurrentShake` at the shot and its peak over 12 frames. The switch state is part of the key (accuracy on/off x
no recoil on/off):
`accuracy: fire probe PistolWeaponConfig (accuracy on, no recoil off): the shot read HorizShotError 0 ...
ViewKicks 2 ShakeMagnitude 800,400; view kick (CurrentShake, in the aim) pitch .. yaw .. at the shot, peak over 12
frames pitch .. yaw .. (.. / .. deg)`. Spreads of 0 with a non-zero peak kick confirm this note. With no recoil
on, the line should show magnitudes 0,0 and a peak of 0.

Note: the fire probe runs only while Perfect accuracy is available; with only No recoil available it is skipped (diagnostics only).

Mounted guns: the MG42 reads its spread (`MaxSpread`) from its own instance, so the row writes it with
`bAllInstances` 1 while the player is mounted and restores it on dismount; AI MG42s share it for that window
(turrets.md section 7).

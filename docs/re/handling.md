# Handling: fire rate and reload speed (build 244)

Every single-player gun's fire cadence and every reload are driven by the held weapon's own animation controller,
`AnimationControllerEx` (one instance per weapon actor), plus one game-time timer, `LockoutFireTime`. The controller
has a rate multiplier, **`AnimRateScale`** (default 1.0), that scales every animation it starts, and the engine's
own native **`UpdateGlobalAnimRateScale(f)`** rescales the running channels and stores it. No script calls it in
build 244. The levers: call `UpdateGlobalAnimRateScale` through ProcessEvent on the held weapon's controller, with
the fire factor while the weapon is in a fire state and the reload factor in a reload state; and for the lockout,
call the weapon's own `ClearLockout()` through ProcessEvent once `LockoutFireTime / factor` of game time has passed.
Do not write the `LockoutFireTime` class default: AI pawns hold the same weapon classes. Notifies (shots, ammo) and
anim-end callbacks ride the animation clock, so ammo and gameplay stay with the animation. Measured in game: the
running channel follows `AnimRateScale` at 4x and 20x (Pistol_Gold, Railgun), so the levers work as read.

Method: `tools/ubc.py` over `extracted-packages/*.u` (every function decoded once into a scratch dump and grepped);
a scratch tagged-property reader for class defaults (DNF bools are tagged `0xD3 00` when true; every
`SAnimationInfo` member is serialized, zeros included); Ghidra MCP: Engine.dll, and dnAnimation.dll loaded into the
`dnf` project. Plate comments were set on `Engine.dll!0x1009D630` and `0x100A0340` and on
`dnAnimation.dll!0x10016460`, and both programs were saved.

## 1. What sets the fire cadence

### The fire path (Engine.u `Weapon`)

- `AttemptFire` -> `CanFire` (= loaded ammo or `bInfiniteAmmo`, **and `!bWeaponFireLockout`**) -> `Fire` ->
  `TryState('Firing')` -> `FireSuccess`: `WpnFireStart` (`SetWeaponState(5)`) when `bHasFireStart` and this is not
  a continued shot, else `WpnFire`.
- `WpnFire`: `SetWeaponState(4)`; `bWeaponFireLockout = true`;
  `SetCallbackTimer_Always(WeaponConfig.default.LockoutFireTime, false, 'ClearLockout')`. This is **the only timer
  in the fire path**: no weapon script sets another fire or reload timer. The freeze ray's beam ammo timers are
  outside this path (section 1, per gun).
- `SetWeaponState(n)` -> `GetWeaponAnimReq` -> `SetWeaponAnimState(name)` ->
  `AnimationController.SetAnimState(name)`. The controller plays the state's entry from its `Animations` array.
  The shot itself comes from a mesh notify in the fire animation (`Fire_Effects` -> `PerformFire`; accuracy.md).
- The anim-end callback is data: each `SAnimationInfo` names `AnimEnd_FunctionCallName`. In the weapon
  controllers' class defaults these are `WeaponCallback_MaybeDoneFiring` / `_DefinitelyDoneFiring` /
  `_MaybeDoneReloading` / `_DefinitelyDoneReloading` (e.g. `dnAnimationControllerEx_Pistol`: Fire ->
  `WeaponCallback_MaybeDoneFiring`, Reload -> `WeaponCallback_DefinitelyDoneReloading`, idle -> `WpnIdle`). Chains
  go through `AnimEnd_NextAnimStateName`: the minigun's `ChainGunFireStart` -> `ChainGunFireLoop` spin-up is one.
- `Firing.WeaponCallback_MaybeDoneFiring` (at the fire anim's end): `global.AttemptFire(true)` refires while the
  trigger is held. If that fails it stops the view kick, then `WpnFireStop` (when `bHasFireStop`) or `TryState('idle')`.
- `ClearLockout` (the timer): `bWeaponFireLockout = false`, then `AttemptFire(true)` **only in `idle` and only
  without `bFireInterruptFiring`**. `Firing.AttemptFire` passes a press during the animation only with
  `bFireInterruptFiring` (aim.md section 4).

So the cadence is: **held = the fire animation's length** (`MaybeDoneFiring`). If the lockout has not cleared by
then, the gun goes idle. A non-interrupting gun refires from `ClearLockout`. An interrupting gun waits for a new
press. **Clicked = `LockoutFireTime`** for interrupting guns.

### Per gun (dnGame.u config class defaults; flags interrupt / fire start / fire stop / looping)

| Gun | Config | LockoutFireTime | flags | Notes |
|---|---|---|---|---|
| Pistol | PistolWeaponConfig | 0.15 s | 1/0/0/0 | click cadence = lockout, held = anim |
| Dual pistols | DualPistolWeaponConfig (< Pistol) | 0.15 (inherited) | ?/1/1/1 | interrupt untagged: inherited from Pistol or stored false, not settled (aim.md reads 0); second pistol has its own controller, set by `DualPistol.SetWeaponAnimState` -> `SecondPistol.SetAnimControllerState` |
| Shotgun | ShotgunWeaponConfig | 1.0 s | 0/0/0/0 | `Shotgun.Firing.WeaponCallback_MaybeDoneFiring` calls `ClearLockout()` first: cadence = anim |
| Ripper | MachineGunWeaponConfig | 0.06 s | 1/1/1/0 | start and stop anims; our lockout clear is eager (section 8) |
| Minigun | MiniGun_BLWeaponConfig | 0.06 s | 1/0/0/0 | spin-up is an anim chain (above); never reloads (`GetLoadedAmmo` 1, `IsOutOfLoadedAmmo` false) |
| Railgun | RailgunWeaponConfig | 1.5 s | 0/0/0/0 | cadence = max(anim, 1.5 s); `PerformFire_Custom` plays `Charge` (sound) |
| Devastator | DevastatorWeaponConfig | none | 0/1/1/1 | looping fire; shots from the loop anim's notifies |
| Freeze ray | FreezeRayWeaponConfig | none | 1/1/1/1 | beam (non-classic) is `Tick` + `ExpendFreezeAmmo` every 0.03 s: **not** anim-driven, unchanged by any lever here |
| RPG, shrink ray, Enforcer gun, AT laser | - | none | AT laser 0/0/1/1 | anim only; projectile speed unchanged |
| Pipe bomb, trip mine | - | none | - | throw states are anims (`Throw`, `ThrowStart`) |

The "none" rows have `LockoutFireTime` 0 (absent in every class default, so the base value 0).

## 2. What sets reload duration

- `Weapon.Reload` -> `TryState('Reloading')`. `Reloading.BeginState`: when `ReloadCount > 0`,
  `AmmoLoaded < ReloadCount` and reserve > 0: `WpnReloadStart` (state 8) if `bHasReloadStart`, else `WpnReload`
  (state 7). Otherwise `WeaponCallback_DefinitelyDoneReloading`.
- **Ammo is added by a mesh notify mid-animation**, not at the end: `Ammo_Inserted` (function flag `0x04000000`, the
  anim-callable mark shared by `Mount_Clip_*`, `Eject_Clip`, `Shell_Inserted` and the `WeaponCallback_*` set; none
  is named by any controller default, so they come from the mesh sequences' events) -> `ReloadAmmo`:
  `AmmoLoaded += min(reserve - loaded, ReloadClipAmmo, ReloadCount - loaded)`.
- The end: the controller's anim-end callback. `Reloading.WeaponCallback_DefinitelyDoneReloading` ->
  `TryState('idle')`. `Reloading.WeaponCallback_MaybeDoneReloading` -> `WpnReload` again while `CanReload()`, else
  `WpnReloadStop` (state 9).
- **`ReloadCount` is the clip size, not a reload count.** `ReloadClipAmmo` is the rounds one insert adds.
- **Shotgun = looping per-shell reload:** `ReloadCount` 7, `ReloadClipAmmo` 1, `bHasReloadStart`, `bHasLoopingReload`.
  ReloadStart, then Reload once per shell (`Shell_Inserted` notify, then the `MaybeDoneReloading` end), then ReloadStop.
  `Shotgun.Reloading.WeaponCallback_Maybe/DefinitelyDoneReloading` try `AttemptFire()` first, so fire interrupts it.
  **Fire interrupts only the shotgun (read with `tools/ubc.py`; used by tbot.cpp):** `Weapon.Reloading.CanFire` has an empty
  body (false) and no `Fire`, while `Shotgun.Reloading` overrides `CanFire` and `Fire` to the global ones and
  `Shotgun.Reloading.WeaponCallback_Maybe/DefinitelyDoneReloading` run `if (!AttemptFire()) super()`.
  `Weapon.AttemptFire` fires when `WantsToFire` and `CanFire()` (loaded > 0, no lockout), so a held fire after the
  first shell is in starts a shot and `Firing` replaces `Reloading`. The Triggerbot therefore holds fire while the
  held weapon is in state 7-9 with `ReloadClipAmmo < ReloadCount`, `AmmoLoaded < ReloadCount` and `Ammo.Charge >
  AmmoLoaded` (`tbot_reload_wait`); log `tbot: holding fire while ... reloads shell by shell` and "fire held during a
  reload N frame(s)" in the 600-frame line. The aimbot keeps tracking.
- Who reloads (ReloadCount / ReloadClipAmmo): Pistol 8/8, Dual pistols 16/16, Ripper 50/50, Railgun 3/3, Shotgun 7/1.
  Every other config has ReloadCount 0. `GottaReload` and `CanReload` are then false, so they never enter Reloading.
  The RPG's controller has a `Reload` entry, but the gun never enters `Reloading`. If its reload is chained after
  `Fire`, it is part of the fire cadence (state 4).
- No timer anywhere in reload. The only one nearby is `Waiting.DoIdleReload` (a 3 s repeating timer in the `Waiting` state).

## 3. The animation clock (Engine.dll, dnAnimation.dll)

- `RenderActor.InitializeAnimation`: `AnimationController = new(Level.XLevel) AnimationControllerClass;
  InitAnimationControllerEx(self)`. So **each weapon actor has its own controller**, whose outer is the level.
- `UAnimationControllerEx::HandleAnimInternal` `Engine.dll!0x100A0340` (reached from `SetAnimStateInternal`
  `0x100A0F80` through `HandlePrimaryAnim` `0x100A0CF0` / `HandleStandaloneAnim` `0x100A0AF0`) plays an entry
  (100 bytes): `PlayAnimEx(..., startFrame +0x14, rate = AnimRateScale(+0xD0) x (AnimRate +0x18 + (rand/32767 -
  0.5) x AnimRateVariance +0x1C), tween +0x20, earlyEnd +0x28)`. (`AActor::PlayAnimEx` `0x102F9C50` ->
  `UMeshInstance::PlaySequence` `0x10310210` -> `dnSkeletonInstance::PlayAnim` `dnAnimation.dll!0x1002EC10`: the rate
  becomes the channel node's `+0x44`, through `Init`'s settings copy or `SetRate` vt+0x48.)
- `dnAnimationInstance::Update` `dnAnimation.dll!0x10016460`: **`time += rate x dt`**, and `dt` is the level's
  dilated delta. It queues `AnimEarlyEnd` when `end - time <= earlyEnd` and `AnimEnd` at the end, then
  `UpdateEvents` (`0x100160A0`). `UpdateEvents` walks each event track with a cursor and fires **every** event whose
  time has been passed, so a notify skipped over within one frame still fires. `dnSkeletonInstance::Update`
  (`0x1002F300`) dispatches general events (notifies) first, then early-end, then end. So at any rate the ammo
  notify reaches script before the reload's end callback in the same frame.
- **`AnimRateScale`** (`AnimationControllerEx`, float, flags 0: not config, not transient; class default 1.0) has
  exactly one native writer, `UAnimationControllerEx::UpdateGlobalAnimRateScale` `Engine.dll!0x1009D630`. It was
  found by an instruction search for every store to `+0xD0` in the controller code. For each channel of the owner's
  mesh it sets `rate = chan.rate x New/old` (`rate = New` when the old scale or the channel rate is 0), then
  `AnimRateScale = New`. It is reached only by `execUpdateGlobalAnimRateScale` `0x100A1E80` (native 1318, script
  `AnimationControllerEx.UpdateGlobalAnimRateScale(float)`), **with no script caller in any package**.
- `Actor.SetGlobalAnimRate` (native 714, `0x102FA150`) is a stub: it reads its parameter and does nothing.
  `Actor.SetAnimRate(Channel, Rate, GroupName)` (native 713 -> `UMeshInstance::SetAnimRate` `0x1030DE60`) sets
  one channel's rate. A later `SetAnimState` replaces it. `UMeshInstance::GetAnimRate` `0x1030DF40` reads it.
- Per-entry rates in the class defaults (the `AnimRate` tag of each `Animations` element): Activate and Deactivate
  1.2 on most guns; **Fire, idle, Reload, Melee and the rest 0.0** (Pistol, Shotgun, Ripper, Railgun, RPG, Devastator,
  freeze ray, minigun, dual pistols). See section 6.

## 4. Levers

| Lever | Animation and gameplay | Ammo and notifies | Extras game speed | Persists |
|---|---|---|---|---|
| **(a) `UpdateGlobalAnimRateScale(f)` on the held weapon's controller, by ProcessEvent** | together: the anim, its notifies (shots, `Ammo_Inserted`, `Shell_Inserted`) and its end callbacks all run on the scaled clock | correct: the same notifies fire, only sooner | multiplies with it (both scale the channel clock; the game speed also scales `dt`) | no config; the controller dies with the weapon or level (outer XLevel). It is a plain property, so **a save game may carry it** (open questions) |
| (a') `Actor.SetAnimRate(chan, r)` per anim start | the same, one channel | correct | multiplies | no; the next `SetAnimState` resets it |
| (a'') `PlayerPawn.DebugSlomo` / `LevelInfo.GameSpeedModifier` | everything (world, AI, timers) | correct | it *is* the game speed | rejected: not a weapon lever |
| (b) `WeaponConfig.LockoutFireTime` CDO via `GlobalSetProperty(bNoSave=1)` | lockout only, no animation | n/a | separate (lockout runs on game time) | CDO, across levels; bNoSave |
| (b') `SAnimationInfo.AnimRate` in the controller's `Animations` | per anim | correct | multiplies | rejected: an instance array element the engine never writes |
| (c) `Weapon.ClearLockout()` by ProcessEvent after `LockoutFireTime / f` | lockout only | n/a | consistent if timed in game time | none |

- (b) is the accuracy.md precedent, but **AI pawns hold the same weapon classes**: `AIActor.ReceiveWeapon` ->
  `WeaponClass.static.GiveToAI(self)`, and `AIActor.AIActor_Weapon_Fire` / `ForceShootNoAnim` /
  `DukeBot.CalculatePawnPhysics` call `Pawn.Weapon.AttemptFire()`, which goes through `CanFire`'s
  `bWeaponFireLockout`. A shortened class-default lockout would speed every AI holding a Pistol, Shotgun, Ripper or
  Railgun. (c) touches only the player's weapon.
- (a) is per instance, so AI weapons are untouched. It writes only through the engine's own native: the field and
  the channel rates are what that native writes.
- Neither (a) nor (c) touches `DebugSlomo`, `TimeDilation` or `GameSpeedModifier`.

## 5. Interactions

- **Triggerbot (aim.md section 4):** PULSE re-presses when `bWeaponFireLockout` clears, so with (c) it presses
  sooner with no change. HOLD guns refire at the fire anim's end, which (a) brings forward. tbot's per-class
  `LockoutFireTime` log stays the stock value, because nothing writes the class default. A held interrupting gun
  whose scaled animation ends before the lockout clears goes idle (section 1). So (a) and (c) must use **the same
  factor**. That keeps the stock order of anim end vs lockout.
- **Silent aim:** one CurrentShake bracket per `PerformFire`, between `UpdateWeaponTraceVars` and `RecordShot`.
  More shots only means more brackets. No change.
- **No recoil:** each shot starts its `ViewKicks` (`FireSuccess` -> `StartWeaponViewKick`). Shakes last 0.06-0.2 s,
  so at higher rates they overlap and stack in `CurrentShake` while No recoil is off. Looping kicks (Ripper, freeze
  ray) are unchanged. With No recoil on there is nothing to stack.
- **Perfect accuracy:** per trace; unaffected.
- **Game speed (DebugSlomo):** scales `dt` for the animation clock and for the lockout timer alike, so the product
  holds. (c) must time against `Level.TimeSeconds` (game time), not wall time. Paused (`ForcePause_NoFade`): no
  ticks; the levers must do nothing while `Level.bPaused`.
- **Infinite ammo:** `CanFire` passes with `bInfiniteAmmo`; reloads still play when the clip empties.

## 6. AnimRate 0.0 in the class defaults

The `Animations` arrays of `dnAnimationControllerEx_Pistol` / `_Shotgun` / `_MachineGun` / `_Railgun` / ... store
`AnimRate = 0.0` for Fire, idle, Reload and Melee. Each entry is serialized in full; the raw Pistol Fire element
reads `1b 24 00000000`. The code read gives such an entry a channel rate of `AnimRateScale x 0 = 0`, and
`dnAnimationInstance::Update` would then never advance it. The game plainly animates these, so something resolves
the 0 at play time. `UPlayAnimInfo`'s constructor default (1.0) is overwritten. `PlayAnim`,
`dnAnimationNode::Init`, `PreHashInit`, `CacheAnimations` and `HandleSpecial` do not resolve it.

**Measured:** `UpdateGlobalAnimRateScale 1.000 -> 4.000: channel rates 1.000 ... one frame later 4.000`. The running
channels move with the scale, so lever (a) works as described, on the pistol, the Railgun and the Pistol_Gold at 4x
and 20x. Where the 0 is resolved was not found.

The panel logs one line per state change of the held weapon: the weapon class, `WeaponState`, the controller's
`AnimRateScale`, and `UMeshInstance::GetAnimRate(chan, 0)` for channels 0-3 (export `0x1030DF40`, thiscall, `this` =
the actor's mesh instance at +0x364), before and one frame after an
`UpdateGlobalAnimRateScale(2.0)` call. It also logs
the interval between `AccuracyShots` increments while held.

## 7. How the levers are used

**Fire rate** (Weapons > HANDLING, the Manhattan Project port behaviour: 100-400 %, 50 % stops, starting at `WEAP_FIRE_MIN` 100):
- Each frame, for the local player's `Pawn.Weapon`: if `WeaponState` is 4, 5 or 6 (`WS_Fire`, `WS_Fire_Start`,
  `WS_Fire_Stop`; enum `EWeaponState`: 0 Unknown, 1 Activating, 2 Deactivating, 3 Idle, 4-6 fire, 7-9 reload, 10
  Melee), want = fire factor. Fire anim chains (minigun spin-up, an RPG reload chained after Fire) stay in state 4.
- If want != the controller's `AnimRateScale`, call `UpdateGlobalAnimRateScale(want)` by ProcessEvent on that
  controller, and on `DualPistol.SecondPistol`'s controller too. Resolve both `AnimationController` and
  `SecondPistol` by name. A state change starts its animation one frame before the poll sees it. The native then
  rescales that running channel, so at most one frame plays at the old rate.
- Lockout (c): on the frame `bWeaponFireLockout` rises, record `Level.TimeSeconds`. Once `LockoutFireTime / f` has
  passed and it is still set, call `ClearLockout()` on the weapon by ProcessEvent. The engine's own timer fires
  later and finds it clear. The lockout length per shot is logged.
- Covers: pistol, dual pistols, shotgun, Ripper, railgun, minigun (spin-up included), RPG, Devastator, shrink ray,
  Enforcer gun, AT laser, freeze ray (classic fire only), pipe bomb and trip mine throws. Hint: "The freeze ray's
  beam is unchanged." Melee (state 10, `MeleeAttack`) and weapon switching (1, 2) stay at 1.0.

**Reload speed** (100-1000 %, 50 % stops, plus one stop past it, "Max", stored as 1100 as on the Manhattan Project port):
- The same poll: states 7, 8 and 9 use the reload factor. Max = a factor of 20. It cannot be instant, because
  the ammo comes from a mid-animation notify. At 20x `UpdateEvents` still fires every passed notify, and notifies
  dispatch before the end callback. The shotgun inserts at most one shell per frame, because each shell needs its
  own anim end.
- Covers: pistol, dual pistols, shotgun (per shell), Ripper, railgun. Hint: "Only the pistols, shotgun, Ripper and
  railgun reload."
- In any other state (idle, activating, deactivating, melee), want = 1.0.

**Restore:**
- Slider back to 100 %: `UpdateGlobalAnimRateScale(1.0)` on every controller we changed, only while our value is
  still in place (the accuracy.md rule). Stop calling `ClearLockout`.
- Weapon switch: the outgoing weapon goes to Deactivating (state 2), so the poll returns it to 1.0 on its last
  held frame. A record of the controllers we changed (object index + pointer) sets any that are no longer held back
  to 1.0.
- Level change: the controllers are new objects (default 1.0). The records are dropped; nothing to restore.
- Save games: the controller is a level object with a non-transient `AnimRateScale`. If a save keeps it, the poll's
  "want = 1.0 outside fire/reload" heals it, because the poll compares with the live value and not with a record.
- Nothing here reaches `SaveConfig`, a game ini or Engine.dll code: one engine native, one script function, both by
  ProcessEvent.

**Known gap:** nothing restores 1.0 before a checkpoint save. If a checkpoint fires while the held weapon fires or
reloads at a raised rate and the save keeps `AnimRateScale`, a later session without the panel runs that one
weapon's animations at the raised rate until the level ends. With the panel, the carried scale is adopted and put
back on the first held frame. AI is never affected (the scale lives on the player's weapon objects only).

## 8. The Ripper's one-shot bursts

Above 100 % the held Ripper loses a race between its Fire anim's end and our `ClearLockout`. The anim end and its
callback run on the scaled channel clock inside the engine's tick. Our clear runs on a once-a-frame poll and is timed
from the poll that first sees the lockout, so it lands one or two frames after `0.06 / f`. Once the Fire anim ends
first, every burst is one Fire. Symptom: 150 % staggers, 200-400 % fire in one-shot bursts that get faster with the
factor. **Fix: for a fire-start gun without a looping fire (`hd_lock_eager`: the Ripper is the only such gun with a
lockout), our `ClearLockout` comes on the frame the lockout is seen.**

The chain (Engine.u `Weapon`, dnGame.u `dnAnimationControllerEx_MachineGun` defaults; `MachineGun <
dnContinuousFireWeapon < Weapon`, and dnContinuousFireWeapon only adds the looping sound):
- Controller entries: `FireStart` (`machinegun_fire_start`) and `Fire` (`machinegun_fire`) both end in
  `WeaponCallback_MaybeDoneFiring`; `FireStop` (`machinegun_fire_stop`) ends in `WeaponCallback_DefinitelyDoneFiring`.
- `WpnFireStart`: `SetWeaponState(5)` only, with **no lockout**. `WpnFire`: `SetWeaponState(4)`, `bWeaponFireLockout =
  true`, timer `LockoutFireTime` (0.06 s) -> `ClearLockout`.
- `Firing.WeaponCallback_MaybeDoneFiring`: `if (!global.AttemptFire(true))` -> `StopWeaponViewKick`, then
  `WpnFireStop()` (`bHasFireStop`). `AttemptFire` -> `CanFire` = ammo **and `!bWeaponFireLockout`**. So the loop
  continues only if the lockout is clear at the Fire anim's end.
- `ClearLockout` refires only in `idle` without `bFireInterruptFiring`; the Ripper interrupts, so it never refires.
- `Firing.WeaponCallback_DefinitelyDoneFiring` -> `TryState('idle')`; `idle.BeginState` -> `AttemptFire()`
  (`bContinueFire` false) -> `FireSuccess` -> `WpnFireStart` again.

So one lost race gives the cycle FireStart (5) -> one Fire (4) -> FireStop (6) -> idle (3) -> FireStart. That is a
one-shot burst, and every anim in it runs at `f`, so the bursts get faster with the factor. At stock the engine's
timer and the anim end run in the same tick, and the anim (>= 0.06 s, or the stock gun would not loop) wins. Scaling
both by `f` keeps the order only on paper. A log (the 8-interval bucket lines) has
`lockouts 4` for every 8 shot intervals at 150-400 %, all `cleared by us`. That is one state-4 pass per burst. Our
lockout lengths were 0.044 / 0.034 / 0.029 / 0.023 / 0.017 s against 0.040 / 0.030 / 0.024 / 0.020 / 0.015 asked,
and these are counted from our first sight, which itself lags the rise.

The fix (`handling.h` `hd_lock_eager`, `hd_lock_step(..., eager)`): eager when the config's `bHasFireStart` is 1
and `bHasLoopingFire` is 0. Among guns with a lockout that is only the Ripper (dual pistols loop, the rest have no
fire start). Every other gun keeps the timed clear exactly. Side effect: a fresh press during the Ripper's Fire anim
(`Firing.AttemptFire` passes it with `bFireInterruptFiring`) can restart FireStart about one frame after a shot
instead of `0.06 / f`; at stock the window is the whole anim past 0.06 s anyway.

The tick and the hooked Present share the game thread (engine-map.md section 5), so the poll always runs between the
tick that raised the lockout and the next one; the one assumption is one Present per tick, which the trace's frame
column shows.

**The instrument (stays): the fire trace,** one line per WeaponState change while `PlayerPawn.bIFireServer` (what
`WantsToFire` reads) is set or on the frame it drops, 120 lines a level, logged before this poll's own calls:
`handling: fire trace N: frame F t T ms (+D): <class> state A -> B, anim <channel-0 sequence> rate R, lockout L,
shots S, fire held|let go; ours since the last line: AnimRateScale X (n call(s)), ClearLockout k (the last M ms ago)`.
The sequence comes from `Actor.GetSequence(0)` through ProcessEvent. Plus one line per config class:
`handling: <Config>: LockoutFireTime ..., bHasFireStart a, bHasLoopingFire b: above 100 % our ClearLockout comes
on the frame the lockout is seen | after LockoutFireTime / factor`.

What would show the fix wrong: at 200-400 % a held Ripper's fire trace still shows `4 -> 6` (or `5 -> 6`) with
`lockout 1`, or the 8-interval line still has half as many lockouts as shot intervals. Other guns whose Fire anim
sits within a frame or two of their lockout at a high factor (the minigun, 0.06 s, no fire start) would show the
same race: for those, a `4 -> 3` in the trace with `lockout 1`.

## 9. Mounted guns

Fire rate also reaches the gun the player is mounted on: `Actor.SetAnimRate` on the gun's channel 0 (MG42, launcher)
and `UpdateGlobalAnimRateScale` on the player pawn's controller, for the launcher and SuperTurret, while the gun is in `FireDown`
(turrets.md section 7). The on-foot weapon is lowered while mounted, so this file's poll leaves it at 1.0.

## Open questions

- Where the 0.0 `AnimRate` in the class defaults is resolved to a real channel rate at play time.
- The Ripper's Fire anim length (`machinegun_fire`), and whether the eager clear always lands before the next
  tick's anim end.
- Whether `SetCallbackTimer_Always` with the same name replaces a pending timer (a stale one could clear a newer
  lockout early).
- Whether a save made at a raised rate (checkpoint at 400 %) loads with the raised `AnimRateScale` without the
  panel. Test: save at 400 %, load without the panel, read the first-held line.

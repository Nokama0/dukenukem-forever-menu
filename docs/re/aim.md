# Aim: silent aim, head zones, visibility, triggerbot (build 244)

Every
single-player gun shot, hitscan or projectile, is fired by `AWeapon::PerformFire` (`Engine.dll!0x100D0260`), and
its direction comes from `PlayerPawn.GetWeaponAim` = `ViewRotation + CurrentShake`, a script-to-script call that no
hook sees. The obvious lead (write `WeaponStartTraceDirection`) is **wrong for single player**: that
property is read only when `IsMP()`. The lever is a **CurrentShake bracket** in our dnGame ProcessEvent slot:
PerformFire raises `UpdateWeaponTraceVars` on the player first and `RecordShot` on the player last, both through
the slot; write `CurrentShake = (aim rotation - ViewRotation) / (CurrentFOV / DefaultFOV)` in the first and put
the saved value back in the second. The camera never sees it, and the engine zeroes and rebuilds CurrentShake every
tick, so a missed restore heals itself one tick later. Head zones are real: the hit bone maps to a body part
through the mesh's `BonePartMapper`, and the damage type scales it: **Pistol x4, Ripper x2** on Head/Eye, every
other gun x1. The triggerbot presses `PlayerPawn.FireDown` and `FireUp` through ProcessEvent; holding it refires
every gun on its own, though slower than re-pressing for the pistol and minigun (section 4, Cadence).

Other sections:

- Overkill (section 7): a target the projectiles in flight will kill is not picked or fired at.
- Projectiles (section 8): an exact intercept replaces the lead; the launcher's grenade is PHYS_Falling.
- Mounted collision (spawns.md section 8): a pawn with collision 0 or tiny is aimed at, led by and boxed by the
  collision actors mounted on it.
- Aircraft: hostile dropships, gunships and fighters are not pawns; they are a last-resort candidate list, taken only
  when no pawn is picked and the held or mounted gun hurts them (aircraft.md section 7).
- Mounted ship flips (section 9): the SuperTurret's camera is the player's free look, not the gun; a damped step from
  the camera and limit tests from the gun's own rotation.
- Achievement targets (section 10): Pescaphobe's catfish are a last-resort list after the ships, while its row is on
  in Map21.
- Splash (section 11): the triggerbot holds fire while an explosive shot's predicted detonation point is within its
  blast's reach of Duke; the player's own blast hurts him in single player.
- Mounted guns (turrets.md section 8): silent aim brackets the turret's own shot events, `FireEffects` (MG42,
  launcher), `FireDown.BeginState` and the player's `AnimCallback_AnimEnd` (SuperTurret). In them it sets
  `CalcedViewRotation` (the launcher's whole aim, the others' pitch) and `RotateYaw` plus `UpdateRotations` (the
  muzzle's yaw), and puts both back while still ours.

Method: `tools/ubc.py` over `extracted-packages/*.u` (every function decoded once into a scratch dump and grepped);
a scratch tagged-property reader for class defaults (DNF arrays carry one extra inner-type byte before their size;
struct and array values are nested tagged lists); Ghidra MCP on project `dnf`, Engine.dll (plate comments set on
`0x100D0260`, `0x102706A0`, `0x1026CD80`, `0x1027C740`, `0x1030CC70`, `0x10305C10`; saved). Head-hit zones were
confirmed in game.

## 1. The shot path

### PerformFire, in order (`Engine.dll!0x100D0260`, decompiled)

| step | call | through our dnGame slot? |
|---|---|---|
| 1 | `Owner.UpdateWeaponTraceVars()` (vtable slot 1 of the owner; for `ADukePlayer` the thunk to the IAT slot, pehook.h) | **yes** |
| 2 | `ExpendAmmo(cfg+0x30)` on the weapon; returns 0 -> PerformFire returns, **no RecordShot** | no (weapon, Engine vtable) |
| 3 | only if `MaxTraceDistance` (cfg+0x44) > 0 and `TraceDamageType` (cfg+0x54) set: `GetShotError`, `GetTraceFireAim(&start, &dir)`, then `TraceFire(start, start + range x (dir + spread))` x `NumProjectiles` (cfg+0x88) | no |
| 4 | only if `WeaponState` (+0x420) == 4: `GetProjectileClass`, then `ProjectileFire(class)` | no |
| 5 | `PerformFire_Custom` (Railgun: plays `Charge` when ammo is left) | no |
| 6 | `Owner.RecordShot(this)` (same owner slot) | **yes** |

`cfg` = `(weapon+0x9D0)->+0x508`, the WeaponConfig class default (accuracy.md).

### Where the direction comes from (script)

- `Weapon.GetTraceFireAim` (event): `if (IsMP() && PlayerPawn(Instigator) != None)` read
  `WeaponStartTraceLocation`/`WeaponStartTraceDirection`; **else `Instigator.GetWeaponAim(Start, Forward)`**.
- `PlayerPawn.UpdateWeaponTraceVars`: fills those two properties with native `GetWeaponTraceVars` (`ViewRotation`
  only; accuracy.md). In SP nothing reads them (the only readers are these two functions).
- `PlayerPawn.GetWeaponAim` (flags 0x20102: not an event, called by the script VM, never through ProcessEvent):
  `vStart = GetCameraLocation()`; `vDir = vector(ViewRotation + ScaledShake)`, `ScaledShake = CurrentShake x
  (CurrentFOV / DefaultFOV)` when zoomed, else `CurrentShake`.
- `Weapon.ProjectileFire`: `Instigator.GetWeaponAim(SpawnLocation, Forward)`; `GetCurrentBarrelLocation` only when
  `bFireProjFromMuzzle` (false on every SP config); `AdjustProjectileSpawn`; `Spawn(class, ..., rotator(Forward))`.
  So projectiles start at the camera and fly along the same aim as bullets.

### Per weapon (config class defaults, dnGame.u)

| weapon | config | kind | range (units) | damage type |
|---|---|---|---|---|
| Pistol / Dual pistol | Pistol(/DualPistol)WeaponConfig | hitscan | 3000 (WeaponConfig default) | PistolDamage |
| Shotgun | ShotgunWeaponConfig | hitscan, 10 pellets, H/V 0.11 | 2048 | ShotgunDamage |
| Ripper | MachineGunWeaponConfig | hitscan, auto | 3000 | MachineGunDamage |
| Railgun | RailgunWeaponConfig | hitscan (no held charge; the `Charge` sound is the reload) | 8192 | RailgunDamage |
| Minigun (`MiniGun_BL`) | MiniGun_BLWeaponConfig | hitscan | 4800 | MachineGunDamage |
| RPG | RPGWeaponConfig | projectile `dnRocket_RPG_Homing` 780 -> 1040 u/s; homes on `LookActor`/`LockOnBone` when locked | - | explosion |
| Devastator | DevastatorWeaponConfig | projectile `dnRocket_Devastator` 900 -> 1600 (dnRocket) | - | explosion |
| Shrink ray | ShrinkRayWeaponConfig | projectile `dnRocket_ShrinkBlast` 1500 | - | - |
| Enforcer gun | EnforcerGunWeaponConfig | projectile `dnRocket_RPG_HeatSeek` 400 (seeks) | - | - |
| AT laser | ATLaserWeaponConfig | projectile `dnRocket_ATLaser` | - | - |
| Freeze ray | FreezeRayWeaponConfig | **beam** in `FreezeRay.Tick` (its own `GetWeaponAim` + `TraceEx` each tick); classic mode only fires `dnRocket_IceBlast` through PerformFire | - | ColdDamage |
| Pipe bomb, trip mine | PipeBomb/TripMineWeaponConfig | thrown: `PipeBombBase.AdjustProjectileSpawn` lofts the throw (`UpwardsViewBoost`, gravity) | - | - |
| Mighty foot, melee | - | `Weapon.PerformMelee` (its own `GetWeaponAim`, no PerformFire) | 72 | MeleeDamage |

**Freeze-ray reach (`tools/ubc.py`):** `FreezeRay.Tick` traces `TraceEx(Start, Start +
FreezeRay.BeamMaxLength x FreezeBeamDir x Instigator.ShrinkScale)`; `BeamMaxLength` is a weapon-class property (class default
read from the dnGame.u class export: `FreezeRay` 256 u, `MP_FreezeRay` 900 u), not a WeaponConfig field. The panel reads it by
name from the weapon class default and caps the aim range with it (`aim_range_uu`, `tbot_crosshair_enemy`) for `AIMW_BEAM`, so the
aimbot and triggerbot skip targets farther than the beam; unread, the range stays uncapped and the log says so. ShrinkScale is not
applied (a shrunk player's reach is shorter: the cap is the upper bound). The weapon line prints `BeamMaxLength N`. Classic mode's
`dnRocket_IceBlast_Classic` is a projectile through PerformFire; its own reach was not measured (IceBlast: Speed 1000, LifeSpan 0.35 s).

All reach PerformFire through `Weapon.Fire_Effects -> DoFire_Effects -> PerformFire()` (the only script caller);
the overrides (`Pistol`, `DualPistol`, `ShrinkRay`, `PipeBombBase`, `FreezeRay`, `EnforcerGun`, `ATLaser`) call the
parent. Not bracketed, so not silently aimed: the freeze-ray beam, melee and vehicles. Turrets
(`dnControl_Turret*`) fire their own traces and projectiles outside PerformFire. They have a bracket
of their own (turrets.md section 8).

### The lever: a CurrentShake bracket

- In the pehook before-handler for `UpdateWeaponTraceVars` on the local player: if a target is chosen, save
  `CurrentShake` (+0x1580, rotator ints; resolve by name), compute the rotator `R` from `GetCameraLocation()` (the
  shot's start; the eye is close enough) to the aim point, and write `CurrentShake = (R - ViewRotation) /
  (CurrentFOV / DefaultFOV)` per axis (pitch and yaw normalised to +-32768 first; roll 0). Every
  `GetWeaponAim` inside this PerformFire (bullets, the projectile spawn, the pipe bomb's second call) then aims at
  the target.
- In the before-handler for `RecordShot`: put the saved value back. Nothing between the two reads CurrentShake
  except GetWeaponAim: the camera (`APlayerPawn::PlayerCalcView` `0x10268730`) runs later in the frame, and
  `APlayerPawn::UpdateViewShake` (`0x10267CB0`) **zeroes CurrentShake and rebuilds it from ActiveShakes every
  tick**. So the view never moves, and if step 2 refuses (no RecordShot) the leak lasts at most one tick: gate the
  write on loaded ammo > 0 or `bInfiniteAmmo` to keep that rare.
- Writes only a value the engine itself writes every tick (the evidence rule holds); no Engine.dll byte changes.
- Side effect, by design: the redirected shot ignores the current view kick (the bracket replaces the whole shake),
  so the recoil no longer scatters it; the spread (H/V) still applies around the new direction unless Perfect
  accuracy zeroes it.
- Rejected: `WeaponStartTraceDirection` (MP only); a ViewRotation bracket (same effect, but a missed restore would
  turn the view for good); hooking `GetTraceFireAim`/`ProjectileFire` (raised on the weapon, whose vtable is
  Engine.dll's: not our slot, and a vtable or code patch there is CEG ground).
- The pehook already offers before-handlers only (pehook.h); both halves of the bracket are before-handlers, so no
  new hook shape is needed. Subscribe to `UpdateWeaponTraceVars` and `RecordShot`; match the player object.

### Lead and range

- Hitscan: no lead. Beyond the config's `MaxTraceDistance` the trace ends short: cap the aim range by it per weapon.
- Projectiles: the exact intercept of section 8 (not a mean of Speed and MaxSpeed with a
  Lead amount). Target velocity: the pawn's `Velocity`; for a pawn hit through mounted collision (the EnergyLeech
  boss), the chosen helper's own per-frame motion (spawns.md section 8).
- Pipe bomb and trip mine: lofted or placed; exclude them from silent aim (as the Manhattan Project port excluded its pipebomb).

### The fists (melee) and an empty gun (from the scripts)

- **Fists = `dnGame.MightyFoot`** (super `Engine.Weapon`), config class `MightyFootWeaponConfig` (the panel's
  `AIMW_MELEE`; `MP_MightyFootWeaponConfig` is multiplayer). `MightyFoot.Fire` is `Melee()`; `Weapon.Melee` is
  `TryState('MeleeAttack')` + `WpnMelee()` (`SetWeaponState(10)`). So FireDown with the fists up throws a punch through
  the game's own path. The aim log's `aim: weapon ...` and `aim: fists ...` lines confirm the name at runtime.
- **Reach:** `Weapon.PerformMelee` traces from the camera (`Location + MeleeStartTrace`, which `Melee` sets to the
  camera offset) along `GetWeaponAim` for `MeleeTraceDistance` (`MeleeTraceDistanceShrunk` while
  `Pawn.ShrinkingActive()`), and on a miss retries with the extent `ShrinkScale x MeleeHitExtent`. The panel's reach
  is the distance plus the extent's largest component (`aim_melee_reach`); distance-to-centre picks add the target's
  collision radius, the look trace's `LookHitDistance` (a surface distance) is compared with the reach alone.
- **Cadence: hold.** `MeleeAttack.Fire` is empty (a press inside a punch does nothing); at the punch's end
  `MeleeAttack.WeaponCallback_MeleeComplete` goes to `idle`, whose `BeginState` runs `AttemptFire`, which throws the
  next punch while `bIFireServer` is held. Punches are counted by `Weapon.WeaponStateImpulse` moving with
  `WeaponState` 10 (`SetWeaponState` bumps the impulse on the authority). A punch never reaches PerformFire or
  RecordShot: silent aim neither covers nor redirects it.
- **Out of ammo:** `Weapon.IsCompletelyOutOfAmmo()` is false under `bInfiniteAmmo`, else `Ammo.Charge <
  AmmoExpenditure` (Charge is the total, reserve included: an empty clip with reserve is not out). `MiniGun_BL` and
  `MachineGun_Pigcop` override it. The panel calls the game's own function on the held gun once a frame while the
  aimbot is engaged, never on the fists (their `Ammo` may be None); while it is true the aimbot releases ("out of
  ammo").

## 2. Head zones and the head position

### How a headshot is decided

1. The gun's `TraceFire` hits a bone (mesh-accurate trace) and calls `TakeDamage(..., HitBoneName, ...)`.
2. `Pawn.TakeDamage` (Engine.u): `BodyPart = GetBodyPartForBone(Mesh, HitBoneName)`, then
   `NotifyBodyPartHit(Instigator, ActualDamage, BodyPart, ...)` (Damage is an out parameter, flags 0x180).
3. `GetBodyPartForBone` (native 570) -> `UMesh::GetBodyPartForBone` (`Engine.dll!0x1030CC70`): the mesh's
   BonePartMapper class at `UMesh+0x7C`, its default object, maps the bone name to `EPawnBodyPart` (0 Default,
   **1 Head**, 2 Neck, 3 Chest ... **19 Eye**); no mapper or an unmapped bone gives 0.
4. `Pawn.NotifyBodyPartHit`: `Damage *= Lerp(DamageType.BodyPartDamageScaleLerp, 1, GetDamageScaleForPart(...))`.
   `Pawn.GetDamageScaleForPart` returns 1; **`AIActor.GetDamageScaleForPart`** (dnAI.u) returns
   `DamageType.default.DamageScaleMap[Part]`. So only AI pawns have zones.
5. `DamageScaleMap` (all 1.0 in `DamageType`); overrides on Head and Eye: **PistolDamage 4.0, MachineGunDamage 2.0**
   (BabeGunDamage 4.0, MP). `MeleeDamage` and `CrushingDamage` set the lerp to 0 (no zones). Shotgun, railgun and
   explosions have no head bonus. The kill stats also count headshots (`S_HEADSHOT*`, `ACH_HeadShotKills`).

### Head bones per enemy (BonePartMapper `PartMapping`, dnGame.u; which mapper a mesh uses is mesh data)

| enemy (class, dnAI.u) | mapper (by name) | bone mapped to Head | EyeBone / LockOnBone |
|---|---|---|---|
| Pig Cop (`PigCop_Grunt` and subclasses; meshes PigCopA/B/C) | `dnBonePartMapper_PigCop` (A/B/C inherit) | `Head` (+ face bones, jaw, eyes); `neck` = Neck | `forehead` / `spine_mid` |
| Assault Trooper (`AssaultTrooper_Captain`, `_Grunt`) | `dnBonePartMapper_AssaultTrooper` (Captain inherits) | `Head`, `head_end`, face; `neck` = Neck | Pawn default `Root` / `Root` |
| Enforcer (`Enforcer`, `Enforcer_Shield`) | `dnBonePartMapper_Enforcer` | `Head`, `jaw`, face; `neck` = Neck | `Head` / `Spine` |
| Assault Commander | `dnBonePartMapper_AssaultCommander` | `Head`, `jaw`, hoses, face | `Head` / `Head` |
| Octababy | `dnBonePartMapper_Octababy` | **none**: `Head`, eyes, brain map to Default | `Head` / `Head` |
| Octabrain | `dnBonePartMapper_Octabrain` | **none** (arms, tentacles, tail only) | inherited |
| Pregnator | `dnBonePartMapper_Pregnator` | Head/Neck/Eye entries exist; bone names did not decode | inherited |
| EDF soldiers (allies), civilians | `dnBonePartMapper_Male` family | `Head`, face; `neck` | - |
| Bosses (Cycloid, Battlelord, Queen, Energy Leech, Octaking) | - | not read: their own TakeDamage gates | - |

`Pawn.EyeBone` and `Pawn.LockOnBone` are NameProperties on the instance (the RPG's lock-on aims at `LockOnBone`:
`RPG.FindLookActorMeshBone`).

### Reading the head position

- `Actor.MeshInstance` (or `GetMeshInstance()`), then `MeshInstance.BoneGetTranslate(BoneName, bAbsolute=true)`
  (native 533). Its body `UMeshInstance::BoneGetTranslate` (`Engine.dll!0x10305C10`, exported, `(FVector* ret,
  dnName&, int bAbsolute, int bDefault, float Scale)`): absolute bone transform x `GetToWorld` = **world position**;
  a missing bone returns (0,0,0). It takes the engine's dnThread semaphore: **game thread only**. Script uses it the
  same way for feet, hands and the corpse camera.
- Aim point rule, per pawn, resolved once per class and cached: **Head** = bone `Head` when
  `GetBodyPartForBone(Mesh, 'Head') == 1` (static native, callable on any actor, or the export on the UMesh);
  else the first of `EyeBone`, `LockOnBone` whose part is 1; else no head (aim at the body). **Body** = the pawn's
  `Location` (cylinder centre, inside the torso for humanoids; `LockOnBone` defaults to `Root`, which is not).
- Read the bone in the `UpdateWeaponTraceVars` handler, at the moment of the shot (one native call per shot); pick
  the target per frame beforehand.

## 3. Visibility

- **Status:** the bullet flags let 20 points through that the old flags hid (the skipped lines name
  `SM_InvisibleCollisionHull`). From one spot in a probe level a `PatchActor` still hid the point trace (43 lines
  against 45 with the old flags). `PatchActor_Bevel` has response 2 and blocks the real bullet too, so it is real cover
  (Map19B, below).
- **The point trace uses the bullet's own flags.** With the player-blocking flags,
  nearly every hidden result hit a `PatchActor` (45 of 54 lines), about 230 of 1190 units out. It blocks the player
  but not always bullets, so the triggerbot hit enemies that aim called hidden. `AActor::TraceFire` (`0x10282A30`), the
  hitscan shot that `PerformFire` runs, calls `MultiLineCheck` with flags `0x1080675` and takes the first hit that
  `ShouldTrace` admits. Through `Actor.TraceEx`, STraceFlags `0x17` (bTraceActors, bMeshAccurate, bShotTrace,
  bTraceTexture) maps to exactly that (`0x1027B730`: bit 0 gives `0x80075`, bit 2 gives `0x1080075`, bit 1 adds
  `|0x400`, bit 4 adds `|0x200`).
  - Not modelled: TraceFire also passes BSP surfaces that carry one flag, and lets `eventTraceFireHit` pass an
    actor (penetration).
  - Clear traces are counted by what they hit: "clear by hitting the target" (the pawn or an attachment) and "clear
    with no hit" (nothing before the point, so bullets may pass through the pawn; spawns.md section 6). The
    no-hit case logs the target's collision flags; it does not change targeting.
  - A clear point is traced again with the old flags. A hidden answer there counts as "bullet-transparent blocker
    skipped", and the first lines name the class.
- **Egg pods (Map19, Map19B, nukepanel.log).** `EggPod` (mesh `pod`,
  no BonePartMapper, EyeBone and LockOnBone `spine_2`, neither a head part) aims at the body points.
  - The pod's body is not away from its cylinder. The spawnbox round-robin boxes every hostile pawn each
    frame. In Map19, with one pod the only hostile pawn (600 boxes per 600 frames), the render-box centre was at most
    **4 units** from `Location`, and at most 12 with its `Pregnator_EggPod`. Script agrees: `EggPod.PostInit` places
    the pod's base at `Location - LocalUp * CollisionHeight`, so `Location` is the pod's centre and the chest point
    lies on the body. A render-box point would aim where the chest already aims.
  - The logged hidden lines are real cover. All 279 `point hidden` lines on a pod in this log hit within the
    first quarter of the trace (29-409 units from a camera 1500-3000 units away): elevator walls, crates and
    `PatchActor`s near the player. Only 9 lines, in the previous log, hit 40-520 units short of the pod.
  - The log alone cannot settle it: the 40 `point hidden` lines per level are spent in the first seconds on far pods
    behind cover. The near-pod probes that followed (hundreds per 600 frames, every one `none`, hits "other" and
    "world") were never logged.
  - Pods were targeted and killed at 427-1210 units several times (`aim: target EggPod`, Health to 0), once through
    `chest hidden -> knees` where the knees trace hit nothing.
  - **Instrument:** `aim: points of <class> at D units` and `aim: points of <class> traced`. The first line gives
    `Location`, the collision size, the render box (`spawnbox_render_box`) and the LockOnBone's world position. The
    second gives each point's first hit by the bullet-flag trace (chest, hips, knees, LockOnBone, box centre):
    `hit <class> (world|actor) at A of B units (C short)`, or clear. It is written once per pawn and distance band
    (under 1000, under 2000, beyond), 8 pawns a band a level (`aim_ptlog_take`), for a probe that found no point
    clear. A small "short" means the point sits in the geometry; a short near B means cover near the player.
  - **Measured (Map19B):** every probe of a floor pod in water (z -1606, camera about -1467, pods
    980-1460 units out) stops on `PatchActor_Bevel` (actor) 65-250 units from the camera, for every point, the
    LockOnBone and the box centre alike (`chest hit PatchActor_Bevel (actor) at 65 of 996 units (931 short)`). The
    pods are targeted only from 103-168 units. So the point choice is not the cause; the bevel is cover.
  - **What the real bullet does there (Engine.dll, Ghidra).** `AActor::TraceFire` (`0x10282A30`) walks
    `MultiLineCheck`'s whole hit list. A level BSP hit whose surface flags `& DAT_10647720` is skipped. Every actor hit
    gets `eventTraceFireHit`, which returns its `ETraceFireHitResponse`: `AActor::TraceFireHit` (`0x10203A50`) returns
    the byte at Actor+0x2FE, the `TraceFireHitResponse` property (enum: 0 Abort, 1 Immediate, 2 Backtrace).
    - 1 passes to the next hit for free. 0 stops.
    - Any other value stops when `MaxTracePenetrationCount` (cfg+0x4C) is 0, or when `MaxPenetrationDistance`
      (cfg+0x50) is above 0 and below the hit distance. Otherwise it spends one penetration and goes on.
    - `PerformFire` (`0x100D0260`) passes those two config fields. Later hits are filtered by `ShouldTrace(0x1080675)`.
  - **Defaults (package defaults read with a tagged-property scan over extracted-packages):**
    - `TraceFireHitResponse`: Actor 2. Triggers, PhysicsVolume, GeoWater and BreakableGlass 1. dnUnBreakableGlass 0.
      `PatchActor` and its subclasses set nothing, so they inherit 2.
    - `MaxTracePenetrationCount`: 0, except Railgun 5 and Shotgun 1.
    - `MaxPenetrationDistance`: 256 (Shotgun 128, Railgun 0).
  - So with the MachineGun, the bullet stops at a `PatchActor_Bevel` that keeps the class default. It passes only if
    the map instance sets `TraceFireHitResponse` to 1, and the map files are not extracted. In Map19B the pods'
    `PatchActor_Bevel` answers 2 and still blocks, as the real bullet does. The pods are
    reached past response-1 actors (volumes, water): `passed as the bullet does 118` against 0 before, and 12
    `aim: target EggPod` at 520-1330 units.
  - **Model (`bullet_trace`, aim.cpp; rule `aim_bullet_passes`, `aim_step_past`):**
    - The shot-flag trace, then past each actor the bullet would pass, re-traced one unit beyond it with that actor
      as `IgnoreActor`. At most `AIM_PASS_MAX` (4) re-traces.
    - The penetration budget is the held gun's two config fields (none while mounted).
    - Used by `trace_point` (so `aim_box_visible`), the ship pass and `aim_view_ray_hits`.
    - Not modelled: the flagged BSP skip, and script `TraceFireHit` overrides (BreakableGlass, Pillars_Destructible,
      ...), which may answer otherwise.
    - The 600-frame line counts `passed as the bullet does N`.
- **Per body point: target whatever is visible, like a priority list of body parts.**
  LineOfSightTo passes when the target's view point or its `Location` is clear, so a flying grunt showing only its
  lower half was aimed at its hidden head. Each candidate traces its body points in order, at most 4 traces:
  - Order: Aim at's choice, head, chest (`Location`), hips (`Location - 0.5 CollisionHeight`), knees
    (`Location - 0.8 CollisionHeight`). The head appears only when the class has one, and the order list skips
    the point already tried.
  - The first clear point is picked and cached with the 100 ms visibility entry. Silent aim's shot, the aimbot,
    the marker and the cone all use it. When no point is clear, the pawn is hidden.
  - The player-blocking trace (the second pass over a clear point) is `Actor.TraceEx` (native 736) on the player, with STraceFlags `bTraceActors` (bit 0) and
    `bOnlyBlocking` (bit 11). `0x1027B730` maps them to `SingleLineCheck` flags `0x80075 | 0x2000000`. That covers
    the world and the collision actors, but only those that block the player (`ShouldTrace`'s `IsBlockedBy`): a
    closed door stops it, a trigger does not.
  - Clear means no hit, the target, or an actor hanging from the target (Owner/MountParent, 4 links).
  - Unlike LineOfSightTo, there is no see-through exclusion here: glass that blocks the player hides the point.
  - The log counts probes, the point each one picked, and the point traces with their average cost. It writes
    "head hidden -> hips" lines.
  - LineOfSightTo stays only for the triggerbot's projected-box fallback.

- **`Pawn.LineOfSightTo(Other, bCheckFOV)` (native 904, called on the player with false) is not the aim's visibility test.** It is
  used only for the triggerbot's projected-box fallback (`aim_line_of_sight`). FastTrace's world-only flags `0xA4` do
  not stop on a door actor, so Silent aim saw through a closed sliding door: the look trace hit
  `DoorMoverEx_Sliding` while the aimbot was locked behind it.
  - `APawn::LineOfSightTo` (`0x1024FDA0`) runs a zone check on both actors' zones (+0x74, `FUN_10321660`), then
    `AActor::LineCheckTranslucency` (`0x10200A90`) from the player's view point to the target's, then to its
    `Location`. That is `SingleLineCheck` with flags `0x32000035`; it passes when the line is clear or its first hit
    is the target. It answers "can see the pawn", not "can see this bone".
  - `ULevel::MultiLineCheck` (`0x10238770`) queries actors for flags `& 0x80031`. `AActor::ShouldTrace`
    (`0x1014CB00`) then admits an actor for bit `0x10` when it blocks the player (bit `0x2000000`, `IsBlockedBy`), as
    a closed door does.
  - `UpdateTraceMasks` (`0x102367D0`) puts see-through actors in the exclude bit `0x20000000`, so glass the game
    treats as clear passes. An open door's collision is out of the line.
  - Side effect: another pawn in front of the target also blocks the check.
  - autoexec.cpp keeps its own world-only FastTrace.
- **`Actor.FastTrace(TraceEnd, TraceStart, bCheckActors, out Time)`** (native 734, `execFastTrace`
  `Engine.dll!0x1027C740`): one `ULevel::SingleLineCheck` with flags 0xA4 (world) or 0x800F4 (with actors); true
  when clear. Called through scall by autoexec.cpp (world only, on the player). With `bCheckActors` true it would
  stop on the target itself.
- `AActor::Trace` (`0x1027C600`) and `TraceVisible` (`0x1023D100`) need argument structs; `Actor.TraceEx` and FastTrace
  through ProcessEvent need none. Budget: at most 4 traces per frame, round-robin over the candidates sorted by angle
  to the crosshair, each result cached for 100 ms; the QueryPerformanceCounter time of the first 64 calls is logged.

## 4. The triggerbot

### Pressing fire

- `PlayerPawn.FireDown` (exec, Engine.u): returns when `Level.bPaused`; `bIFire = true`; `HandleClientFireDown`
  (the HUD may take it); `bIFireServer = true`; native `HandleServerFireDown` (`Engine.dll!0x1026CD80`): refused in
  round states 1/3 or when a vt+0x348 check fails; a carried actor may take it; else, unless +0x974 bit 0 or
  +0x12A0 is set, **`Weapon.AttemptFire()`**.
- `PlayerPawn.FireUp`: `bIFire = bIFireServer = false`.
- Holding: at the end of every fire animation `Weapon.Firing.WeaponCallback_MaybeDoneFiring` calls
  `AttemptFire(true)`, which fires again while `PlayerPawn.WantsToFire` (= `bIFireServer`). So FireDown, hold,
  FireUp covers semi-auto and auto guns alike; the Ripper and Devastator loop (`bHasLoopingFire`). No SP gun needs a
  charge (Railgun's script has no charge state); the railgun fires on the press (LockoutFireTime 1.5 s between shots).
- **Cadence: holding is not the fastest for every gun (Engine.u and the dnGame.u config defaults).**
  `Weapon.Fire` -> `FireSuccess` -> `WpnFire` sets `bWeaponFireLockout` for `LockoutFireTime`; `ClearLockout`
  refires a held trigger only in `idle` and only without `bFireInterruptFiring`. Inside the Firing state a press
  passes (`Firing.AttemptFire`) only with `bFireInterruptFiring`, and a held trigger refires at
  `MaybeDoneFiring`, the fire animation's end. So a held pistol waits for its animation while clicks fire every
  0.15 s: the fire looks staggered, not as fast as it can go. Config flags (interrupt / fire start / looping,
  LockoutFireTime): Pistol 1/0/0 0.15 s; MiniGun_BL 1/0/0 0.06; Ripper 1/1/0 0.06 (fire start and stop);
  DualPistol 0/1/1; FreezeRay and ATCaptainLaser 1/1/1; Devastator 0/1/1; ATLaser 0/0/1; Shotgun 1.0 s,
  Railgun 1.5 s and the rest none. The triggerbot re-presses (`tbot_fire_mode` PULSE) an interrupting gun without
  a start or loop animation as soon as `bWeaponFireLockout` clears, and holds every other gun.
- **The triggerbot presses FireDown/FireUp through ProcessEvent** (as autowin presses JumpUp). The bindings
  (`user.ini`: `Aliases[0]=(Command="FireDown | OnRelease FireUp",Alias=Fire)`, `LeftMouse=Fire | VehicleFire |
  ... | DnControlFireAction`) show why not the dinput button byte: it would also fire vehicles and turrets, and it
  depends on the binding (keyboard-bound fire would not work). Never send FireUp while the player's own fire input is
  down (track our press; read the button from our dinput hook).

### Is the crosshair on an enemy?

- The game's own crosshair trace: `execNativeUpdateLookHitActor` (`Engine.dll!0x102706A0`) runs
  `APawn::TraceFromCrosshairMesh` out to **10000 units** and stores the raw hit at **+0x1D64** and its distance at
  **+0x1D74** whatever the Use reach (most likely `PlayerPawn.DirectLookHitActor` and `LookHitDistance`: resolve
  both by name and log the offsets once). It runs on every 5th call of `UpdateLookHitActor` (a global counter shared
  with Use); who calls that event per tick was not traced (section 6 lists the pehook count).
- "Fire when: Crosshair": `DirectLookHitActor` is a hostile pawn (world.cpp classification) and `LookHitDistance` <=
  the held weapon's `MaxTraceDistance`; fall back to our projection (view.h) of the pawn's model box (modelbox.md)
  containing the screen centre plus a LineOfSightTo (the fallback as built).
- "Fire when: Locked": silent aim has a visible target (section 1), so the shot will be redirected anyway. A shot
  Silent aim does not cover (Silent aim off, or the beam) follows the aimbot's lock when it holds one, as the Aimbot
  mode does, else the Crosshair checks (Locked names Silent aim only).
- **Flying Assault Troopers:** while the aimbot was locked on `AssaultTrooper_Grunt`
  the look trace hit "another actor" on 327 and 593 of 600 frames. tbot logs the hit's class with its `Owner` and
  `MountParent` while a target is near the crosshair. From the scripts: the trooper spawns its
  jetpack effects with itself as Owner and mounts them on itself (`AssaultTrooper_Captain.JetStart`,
  `AttachToActorMountPrefab`; `StartBackFire` uses `AttachToActor`), and a held weapon is owned by its pawn. So a
  look hit whose `Owner`/`MountParent` chain (at most 4 links) reaches a walked pawn counts as that pawn.
- **Pick hysteresis:** the aimbot's lock flipped 62 times in 600 frames between two troopers. A
  challenger replaces the still-visible previous target only when its key is lower by 10 %, or by 1 degree
  (Crosshair) / 32 units (Nearest) when that is more. It holds the shared pick: Silent aim's target and the
  aimbot's lock. That log was in Crosshair mode with keys about 2.3 and 7 degrees, not a tie: the nearer one lost
  the pick without being out-ranked. Its counters (hidden 0, ally in the way 0, frozen 0, out of the cone 1290 in 600
  frames) point at the cone test at the aim point, which hysteresis does not change (the cause is open). aim.cpp logs
  why the previous target lost each pick.
- **Aimbot mode ray gate:** without it the first shot at each enemy missed, rats included. The first FireDown came on
  the same frame as `aimbot: lock on Rat` (11.0 degrees off, the look trace on a LevelInfo), because the mode held on
  `locked` alone and the aimbot turns the view only on the next TriggerViewRotationAdjust. With perfect accuracy the
  shot left along the old view. The mode holds when Silent aim covers the shot, or
  when a trace from the eye along ViewRotation (where the next shot starts, after the turn; Calced lags to the next
  render) to MaxTraceDistance (65536 when unread) with the bullet trace flags lands on the lock or its attachment
  (hit_clear). An unresolved ray fires on the lock; the fists keep their rule. The 600-frame tbot line counts
  "waiting for the turn" and unresolved frames; FireDown logs the degrees off the crosshair. Mirrors the AA port's
  tbot_shot_ray_hit.
- Delay: the condition must hold for N ms before FireDown; release when it stops holding (or after one shot for
  the RPG, pipe bomb and railgun).

## 5. Risks and gates

- **Animated pawns:** a hostile pawn that spawnbox flags as animated away from its
  `Location` (render-box centre more than 64 units off, the drop-in AnimX case) is aimed at, and traced to, its
  render-box centre, and is left out only when no box was read this frame (spawns.md section 7). A cut-scene
  (`bAnimCutScene`) does not exclude. This holds for Silent aim's pick, the aimbot's keep (`aim_track`) and the
  triggerbot's Crosshair, including a look hit mapped to its pawn. The flag comes from spawnbox's round-robin
  (`spawnbox_excluded`); the cut-scene bit is read only with a box. aim's 600-frame line counts the exclusions.
- **Allies:** `dnSinglePlayer.ModifyDamage` zeroes AI-on-AI friendly damage, but the player's shots on an ally
  still hurt it. Target only pawns world.cpp classifies hostile (factions.md); never an EDF soldier or civilian.
- **Kneeling enemies** (`ControlX`, executions.md): a shot kills them as a plain kill (`DieOnTakeDamage`), losing
  the execution; skip pawns in `ControlX` while Auto-execute is on. Frozen enemies: skip them too while Auto-execute is on (a shot may end the frozen execution; not traced).
- **Cutscenes, turrets, QTEs, executions:** the player is attached (`bAttachedToUsableSomething`, a `ViewMapper`,
  `bGrabbing`) or not `CanPressUse()`; skip both features (autoexec.cpp's player gate). Turret fire is its own
  `PerformFire`/trace and is not redirected.
- **Mounted guns (turrets.md "Implemented"):** the one attachment that opens the gate is a fully mounted
  turret (`InputHookActor` IsA `dnControlRemapperEx_Turret`, `CurrentRemapperState` 2, `MountParent` IsA
  `dnControl_Turret`); its own remapper as `ViewMapper` and the turret as `InteractiveDecoration` no longer close it.
  The aimbot and the triggerbot then treat the gun as the weapon (MG42 and SuperTurret hitscan, the grenade launcher a
  projectile with no gravity modelled), pick only targets inside its live limits, and press fire with
  `DnControlFireEnable`/`Disable`. Silent aim's gate reads "unavailable on mounted guns".
- **Vehicles:** FireDown goes to the on-foot weapon path, not `VehicleFireDown`; skip while in a vehicle (attached).
- **Menus and pause:** `Level.bPaused` refuses FireDown; skip while the panel is open, and FireUp anything we
  pressed when the panel opens.
- **Bosses:** their `TakeDamage` overrides gate damage; target them, but the head rule does not apply.
- **Leak:** a refused ExpendAmmo skips RecordShot; the next tick's UpdateViewShake clears our CurrentShake. Count
  open brackets at the end of each frame (should stay 0).
- **Freeze-ray beam, melee, thrown weapons:** not redirected; say so in the row hints.
- **Achievements:** `DukePlayer.UpdateKillAchievements` returns early when `bHasCheatedThisLevel`; neither feature
  sets it (the game sets it from its Extras God mode / Infinite ammo / InstaGib, progress.md section 1).

## 6. What the logs show

- pehook counters per 600 frames: `UpdateWeaponTraceVars`, `RecordShot` and `UpdateLookHitActor` calls; brackets
  opened, closed by RecordShot, left open at frame end.
- Per shot (capped per level): weapon config, target class, aim point (bone name and part), distance, CurrentShake
  saved/written, FOV scale, and `AccuracyShots`/`AccuracyHits` deltas.
- Once per class: the head bone found, `GetBodyPartForBone(Mesh, 'Head')`, EyeBone and LockOnBone parts.
- `DirectLookHitActor` and `LookHitDistance` are resolved by name and their offsets logged (section 4 reads them at
  0x1D64 and 0x1D74).
- Damage numbers show the result: a pistol headshot on a Pig Cop reads 4x a body hit.

## 7. Overkill: no fire at a target the projectiles in flight will kill

**Summary (from the bytecode):** a projectile's damage is its instance `Projectile.Damage`,
dealt by `Projectile.DoDamage` with `Instigator` as InstigatedBy; the player's scale is `GameInfo.ModifyDamage`'s
`InstigatedBy.DamageScaling` times `dnSinglePlayer.IssuedDamageScaler` (Difficulty 0: 1.25, 1: 1, 2: 0.75, else 0.5)
when the instigator is the player pawn and the damage type lacks `bIgnoreDifficultyDamageScaling`. A target whose live
projectiles younger than 2 s carry at least its Health is doomed: the aim does not pick it (spawnbox exclusion
`SB_EXCL_DOOMED`, last in precedence; ships skipped in `pick_ship`), `aim_track` drops a lock on it, and the
triggerbot does not fire at it. Only `AIMW_PROJECTILE` guns (RPG, Devastator, shrink ray, Enforcer gun, AT laser, the
mounted grenade launcher); hitscan, beam and melee never doom (`ok_applies`). The SuperTurret stays hitscan in the
panel (16000 u/s, `aim_turret_kind`), so it is out of scope. No ESP marking.

Facts (`tools/ubc.py`):
- `dnControl_Turret_MountedGrenadeLauncher.PerformFire`: `Spawn(dnGrenadeLauncherProjectile, self, ...)` (Owner = the
  turret), then writes `Projectile.Damage = GrenadeDamage` and `DamageRadius` on the instance. `dnGrenade.PostVerifySelf`
  sets `Instigator = Pawn(Owner)` only when the Owner is a pawn, so the grenade's Instigator is the turret's (not
  read; the projectile log line prints it). With no player instigator neither scale applies.
- `Weapon.ProjectileFire`: `Spawn(GetProjectileClass(), none, ...)`: no Owner, Instigator = the weapon's (the player).
  `Weapon.GetProjectileClass` is the config default's `ProjectileClass` (FreezeRay overrides; a beam, out of scope).
- `Projectile.Explode` = `DoDamage` (direct `TakeDamage`, or `HurtRadius` with falloff) then `Destroy()`. Splash is
  counted as the full Damage. A destroyed actor sets `bDeleteMe` and is freed only at the next garbage collection, so
  a tracked projectile is gone when its snapshot slot changes or `bDeleteMe` is set.

How (overkill.cpp, once a frame before `aim_frame`):
- **Shots:** `PlayerPawn.AccuracyShots` growing on foot, the mounted gun's `MuzzleIndex` moving (`tw_muzzle_shots`) on
  a turret; each queues the lock of that moment (`ok_lock`: the triggerbot's target, else the aimbot's lock, else the
  aim's pick) for 1 s.
- **Projectiles:** an object-table pass (about 1 ms over 67555 objects, as worldscan's) at once after a shot and every
  100 ms while armed (a targeting feature on, a projectile gun, the gate open). Chosen over worldscan's 333 ms rescan
  (a new rocket would go unseen for a third of a second) and over the level's actor list (a native offset with no
  name). Matches the exact class the gun fires (by name, then by pointer), this level's `Level`, `bDeleteMe` clear, and
  `ok_is_mine`: Instigator the player, or Owner the player, its weapon or the mounted gun. Between passes the tracked
  ones are only checked (slot, `bDeleteMe`) under one guard. The periodic pass runs only within 2 s of activity (a
  shot seen, our fire press held, a projectile tracked); a shot's pass runs at once regardless. Projectiles
  past the 2 s cap are retired (slot freed, remembered so a pass does not find them again).
- **Target of a projectile:** the oldest queued shot's lock; else the lock now; else the nearest hostile pawn or ship
  on 3 s of its path (straight, or the grenade's stepped arc, section 8).
- **Marks** (`ok_mark_step`): set when expected >= Health (pawns `w.health`, ships their Health); cleared when the
  target dies, or with it alive (a miss): every projectile gone, the rest past the 2 s cap, or the rest too few.

Logs: `overkill: tracking <class> ...` once a level with the pass time; `overkill: projectile <class>: Damage D (class
default) x S = E (Instigator, Owner; DamageScaling, single player, Difficulty, DamageClass bIgnore...) -> <target> by
the shot's lock | the lock now | its path | none` (8 a level); `overkill: doomed <target>: Health H, N projectile(s) in
flight, expected damage E` (8 a level); `overkill: <target> mark cleared after T ms: dead | alive, ...`; a 600-frame
`overkill:` line (passes and their cost, projectiles by attribution, marks, clears); the tbot 600-frame line's
`overkill: fire held back on a doomed target N time(s) over F frame(s), marks cleared with the target alive M`; the aim
line's `doomed` counts (pawns excluded, ships skipped).

## 8. Projectile models and the intercept

**Summary (from the script packages, the class defaults and Engine.dll):** rockets fly
straight with a speed that rises linearly (`AccelerationScaler` units/s per second) to `MaxSpeed`; the mounted
launcher's grenade is **not Karma**: it is `PHYS_Falling` under full zone gravity, and Projectile's `bBounce` halves
the gravity it feels and skips the TerminalVelocity clamp (UE1's own physFalling quirk). The lead is an exact intercept, not a mean of
Speed and MaxSpeed times a Lead amount %; there is no Lead amount slider, and an old `aim.leadamount` key in
nukepanel.ini is skipped like any unknown key.

Method: `tools/ubc.py` (functions), a scratch tagged-property reader over the class exports (the parse that ends on
`None` at the export's end with the most tags; DNF arrays carry an inner-type byte), the `EPhysics` enum read from
Engine.u, Ghidra MCP on Engine.dll (decompiled): `AActor::PerformPhysicsMode` `0x1025DBC0`,
`physProjectile` `0x1025A110`, `physFalling` `0x10259300`, `NewFallVelocity` `0x10256E20`, `GetFluidFriction`
`0x10256C60`.

Facts:
- **EPhysics** (Engine.u): 2 `PHYS_Falling`, 6 `PHYS_Projectile`, 18 `PHYS_Meqon` (the physics-engine mode;
  15 `PHYS_Karma_Deprecated`). PerformPhysicsMode dispatches 2 to vtable +0x164 (physFalling), 6 to +0x160.
- **Launch (hand weapons):** `Weapon.ProjectileFire` spawns at `GetWeaponAim`'s start (the camera; `bFireProjFromMuzzle`
  false in SP) with `rotator(Forward)`, after `AdjustProjectileSpawn`: `EnforcerGun` adds `Rot(200, -200, 0)`,
  `ATLaser` adds the same plus `RVar` +-200. `Projectile.InitializeMotion`: `Velocity = Speed x dir`, `Acceleration =
  AccelerationScaler x dir`. `Projectile.PostVerifySelf` with `bMatchInstigatorScale` (Projectile default true):
  `Speed` and `MaxSpeed` x the instigator's `DrawScale / default.DrawScale` (a shrunk player's rockets are slower).
- **physProjectile:** `Velocity += Acceleration x dt` (fluid friction only in a water volume), then for a class whose
  chain is the global name at `0x107D0EB8` (Projectile) the speed is clamped to `[+0x980, +0x984]`, taken as
  [Speed, MaxSpeed] (the weapon log line prints both offsets); no gravity. So `speed(t) = min(Speed + A t, MaxSpeed)`.
- **Class defaults** (Speed / MaxSpeed / AccelerationScaler, LifeSpan): `dnRocket` 900 / 1600 / 50, 6 s (Devastator
  inherits; at 50 u/s^2 it is 1200 u/s after 6 s); `dnRocket_RPG` 600 / 800;
  `dnRocket_RPG_Homing` 780 / 1040; `dnRocket_RPG_HeatSeek` 400 (seeks only within `SeekDistance` 300 after
  `EngageDelay` 0.3 s, else keeps flying at the point 1000 ahead); `dnRocket_ShrinkBlast` 1500 / 1500;
  `dnRocket_AssaultTrooper_Laser` (AT laser's parent) MaxSpeed 9e14; `dnRocket_SuperTurret` 16000 / 9e14 / 100000;
  `dnRocket_ATCaptainLaser` 900 / 9e14 / 50, 6 s; `dnRocket_Nuke` 500 / 700 / 50, 6 s (about 3800 units in 6 s);
  `dnRocket_ExpandBlast` 2500 / 2500 / 50, 6 s (section 13).
  Projectile's own defaults: MaxSpeed 2000, Physics 6, `bBounce` true. The panel reads Speed, MaxSpeed,
  AccelerationScaler, LifeSpan and bMatchInstigatorScale live from the ProjectileClass default.
- **The launcher's grenade** (`dnGrenadeLauncherProjectile` < `dnPipeBombProjectile` < `dnGrenade` < `dnProjectile` <
  `Projectile`): class default **Physics 2**, Speed 2500, `bRotationFollowsVelocity`. Its own `InitializeMotion`
  replaces dnGrenade's (the only `SetPhysics(18)` / `KSetLinearVelocity` path), so `GravityScale` 0.5 and the Karma
  damping are never used. The turret's `PerformFire` spawns it at `MuzzleNames[MuzzleIndex]` (after `FireEffects`
  stepped the index) with the full `CalcedViewRotation`, then writes `Velocity = GrenadeVelocity x dir`,
  `Acceleration` 0. No LifeSpan (0), no ExplosionDelay; it explodes on any impact.
- **physFalling** for a non-pawn: gravity = `PhysicsVolume.Gravity` (+0x724) x 1 (the scale is a pawn's or MP's only);
  each step (the frame, halved while over 0.05 s) moves at `NewFallVelocity = v (1 - FluidFriction dt) + a dt / 2`.
  Unless `+0x58 & 0x10` (bBounce) or `& 0x2000` (bJustTeleported), the velocity is then refined to `v + g dt` and
  clamped to `TerminalVelocity` (+0x730). With bBounce the stored velocity is the midpoint, so it gains only `g dt / 2`
  a step: half gravity, no clamp. PhysicsVolume defaults: Gravity (0, 0, -950), TerminalVelocity 2500.

The solver (aim.h, pure, tests 0459-0461):
- `AimProjModel` {kind STRAIGHT | BALLISTIC, speed, maxSpeed, accel, gravity, terminal, maxT, bounce}; `aim_intercept`
  takes the origin, the target point and its velocity and returns the launch direction, the time of flight, the
  target's point then, and ok.
- **Straight:** the first t where `aim_straight_dist(t)` (the speed profile integrated) equals `|p + v t - o|`: 64 steps
  over maxT (LifeSpan, else 10 s), then bisection. No crossing: no solution.
- **Ballistic:** for the target's point at t, the low arc: the closed form's low root (gravity halved with bounce)
  starts a bracket below the peak angle (`tan = s^2 / (g D)`), bisected on a flight stepped exactly as physFalling
  (60 Hz, chords between steps); the time iterated to a fixed point (16 at most, settled within 4 units). Out of
  reach or unsettled: no solution.
- **Glue (aim.cpp `solve`):** origin the camera, or the launcher's next muzzle bone (within 512 units of the gun,
  else the camera); the target's `Velocity` only with Lead moving targets on (the arc is solved either way); the
  spawn rotator offset is taken back; the result is a point along the launch direction at the target's distance, so
  every consumer turns the camera to it. Used by the aimbot's turn (`aim_track`, which drops a lock out of reach), silent
  aim's redirect (an unreachable shot is not redirected), the triggerbot's Aimbot-mode lead gate (through `aim_track`),
  the calibration marker and the pick (a walked candidate out of reach counts as hidden, so the next one is taken).
  Mounted, the gun's pitch and yaw limits are asked of the solved launch direction, not the direct line
  (`aim_turret_dir_ok`): a lob above MaxPitch is out of reach, so no lock. The mounted guns' projectile classes are
  found by name once a level: the grenade's `bBounce` and the SuperTurret rocket's Speed, MaxSpeed,
  AccelerationScaler and LifeSpan come from their class defaults (the built-in values only when unread).
  Overkill's path fallback steps the grenade's arc in 12 chords. The SuperTurret stays a hitscan kind for the gates
  and overkill and is led by its straight model.

Logs: `aim: projectile model <class>: straight, Speed S (+0x..) rising A/s to MaxSpeed M (+0x..), LifeSpan L,
bMatchInstigatorScale b; spawn rotator offset p,y` once per weapon config; `aim: mounted <gun> projectile model:
ballistic, speed, gravity (PhysicsVolume.Gravity x,y,z), TerminalVelocity, bBounce (Actor.bBounce +0x.. mask 0x..),
LifeSpan (its source); origin the next muzzle bone, D units from the camera` once per attach; the aim 600-frame line's
`intercepts solved N (ballistic B), no solution X (frames x targets), shots out of reach S, candidates out of reach U
(projectile scale R; solves, avg and max ms)`.

## 9. Mounted guns: the camera is the player's free look

On the SuperTurret (limits pitch -128..10240, yaw -16384..16384, 4 dropships hostile and damageable, 2 allies, no enemy pawn,
Game speed 400 %, Pick target Nearest, cone 90) a turn that assumed the camera follows the gun made the aimbot switch
targets very quickly. Measured:

- `aim: target dnAlienDropship (aircraft, no pawn picked) ... (3 candidate(s))`, `aimbot: lock on`,
  `aimbot: released (no target)`, again, every other frame. The aimbot's lock lines stop at their cap of 32 (line
  7059); the target lines go on. "no target" is aimbot_frame's release: `aim_target_shared()` was null with the gate
  open, so the ship pass did not pick the ship it picked the frame before. The lock does not read a different pick.
- The aim 600-frame lines of the flip windows: target frames 463, 400, 555, 84; ship candidates out of the cone 233,
  111, 0, 260 (0 in every steady window), outside the turret's limits 1014-1515 (kept by the hull's slack 86-453),
  hidden 10-34; no reach failures. The target was dropped by the cone or the limits, not by visibility.
- The turn does not converge. In the steady windows before (6941, 6983: locked 600 frames, one lock) the aimbot added
  about 1190 pitch units every event (`|pitch| 712198`) from a pick 19.6 degrees off, and the next pick found a
  dropship 59 degrees off; in the flip windows it added about 4850 pitch and 9400 yaw units an event (52 degrees),
  about the angle the target lines show. The crosshair never came onto the ship.
- **The counters are right.** The flip windows show `mounted 600 frame(s)` and 59, 31, 23 locks; the
  `locked 0 frame(s), 0 lock(s), mounted 0` lines come later, with `world: ... paused=1` (the pause menu).

**Wrong models:** the camera is not the gun plus or minus the pending `ViewOffset`, and a per-frame detector that
compares the camera within one frame is wrong too: the frame's camera lags the handler's by one event, so it judged
"not shown" on the first event and the pending turn was subtracted again.

**The model, from the script and Engine.dll:**
- `ControlRemapperEx.InputHook_ViewRotationAdjust` (Engine.u): `ViewOffset = Normalize(ViewOffset + adjust)`, clamped
  by `Min/MaxViewOffset` (pitch stops near -4100 in the log), forwarded to the turret. The adjusts are zeroed only
  with `bClearInput` (the non-home-pose path, or while interpolating) and no `bCameraLook`; otherwise
  `APawn::UpdateRotation` adds them to `ViewRotation`: the camera is the player's **free look**.
- `dnControl_Turret.InputHook_ViewRotationAdjust` (dnGame.u): returns on a frame where `RotateYaw/Pitch` changed
  since `UpdateRotations`; clamps `ViewOffset` so `Rotate + ViewOffset` stays inside the limits; takes the whole
  `ViewOffset` (`bUseInstantAimRate`: MG42, launcher) or `min(ViewOffset, dt * AimRate)` (SuperTurret 8192/s);
  `ConstrainRotation`; subtracts the move from `ViewOffset`; keeps `ViewOffset.Pitch + RotatePitch` out of
  15000..49152. `UpdateRotations` turns the bones to `Rot(RotatePitch, RotateYaw) >> Rotation`.
- `ControlRemapperEx.CalcView`: camera = `CurrentPlayer.GetCameraViewRotation()` = `APawn::GetCameraViewRotation`
  (`Engine.dll!0x1024DCE0`): `ViewRotation` (+0x1258), or the bone named at +0x1278 when it exists; blended with
  `InterpViewRotation` by the FBBlock channel in the home-pose path; `dnControlRemapperEx_Turret.CalcView` composes
  `TurretViewRotationOffset` (0 here) and writes the camera back to `ViewRotation`.
- The MG42 worked: its gun takes the whole `ViewOffset` in the same event, so the free look and the gun move together
  and the pending turn is about 0 (`MG42.PostUserTick` also drives the player's `MG42_AimGrid` from the gun).
- The shot: SuperTurret yaw from the `mount_beam` bone, pitch from `CalcedViewRotation`, pulled toward the crosshair
  hit by at most 1000 units (`FireShot`); MG42 the same split (turrets.md section 3).

**Replay (the log's six one-event pairs):** "the camera moves by g times the add
within one event" holds in direction, not in size. Per pair g is 1.09-1.47 in pitch and 0.82-1.05 in yaw; the
least-squares g (1.168, 1.019) predicts the next rendered view with errors of 471-1720 units in pitch (5-20 %) and
497-2031 in yaw (2-24 %), the largest on the first lock. **The model is not exact** (the blend or the camera bone may
explain the gain; not measured), so the rule is built to hold for any gain below 2.

**Fix:** the mounted turn is a damped step from the camera: half the remaining error an event (k g < 1 for every
measured g: the error never changes sign), the pending `ViewOffset` never subtracted (it is the gun's queue), and no
step while the camera has not shown half of the last one on an axis where it was at least 512 units, 8 events at most
(`aimbot_mount_step`, test 0519: every measured gain within 64 units by event 16 with no sign change; the old rule
swings). The limit tests ask from the gun's own direction, its base `Rotation` plus `RotatePitch/RotateYaw`, when that
Rotation has no pitch or roll (16 units at most), else the camera (`aim_gun_view`, test 0518: the logged ship is inside from the gun,
outside from the camera). Logs: the mount line's `Rotation p,y,r`; the aim 600-frame
line's `limits asked from the gun's rotation N frame(s), from the camera M`; the damping applies
only to a rate-limited gun (`bUseInstantAimRate` false, read by name; unread counts as rate-limited), the slower of it
and Smoothing, never both; the MG42 and the launcher snap as before (test 0424); the turn line's `the gun p,y (its rotation
| the camera)` and `added | held`; the aimbot 600-frame line's `N step(s) held`.

Instrument: `aim: ship flip: <class> lost to <class|none>: <reason>; pawn pass N candidate(s), ship pass
full|cache only|none; distance (range), degrees off (cone), vis; gun to pitch P of min..max (slack), yaw Y of
min..max (slack): inside|inside by the slack|outside; camera p,y, RotatePitch/Yaw, ViewOffset p,y`, 64 a level, the
reason the first ship-pass test the lost target failed (`ac_lost_reason`, test 0517); the aim 600-frame line's
`ship target lost N (<reason> n, ...)`; `aimbot: mounted turn K on <class>: want p,y; rendered p,y, ViewRotation
pitch; adjust in p,y, ViewOffset pending p,y; added p,y (dt)`, the first 3 events after each lock, 48 a level.

## 10. Achievement targets: the Pescaphobe catfish

Goal: for the Pescaphobe achievement, the aimbot and triggerbot target the catfish required.

**What a catfish is (from the scripts and the map package):** `Biology_Generic_Catfish` (dnDecorations.u) < `Biology_Generic` <
`Nature_Generic` < `dnGame.aFinalDecoration` < `dnDecoration`: a decoration, never in `Level.PawnList`. Class defaults
(scratch tagged-property reader over the class export, tools/ubc.py's `Pkg`): `HealthPrefab` 1, `Physics` 9
(`PHYS_MovingBrush`, Engine.u's EPhysics), collision radius 19 height 10, a skeletal `Mesh`; from `dnDecoration`
Health 100, `bCollideActors`, `bBlockActors`, `bBlockPlayers`. `dnDecoration.ResetHealthProperties` case 1 sets
`HealthCap` 1, so one hit of 1 damage kills. No class on the chain overrides `TakeDamage`: `dnDecoration.TakeDamage`
(decoded) returns before Health moves for `Damage < 1` from a non-player, `DamageIgnoreRules`, a non-empty
`DamageTypesAcceptedOnly` the type is no child of (`ClassIsChildOf`), a type equal to a `DamageTypesIgnored` entry,
`bNoDamage`, or under `DamageThreshold`; `Health <= 0` calls `CriticalDamage`. The 15 Map21 instances (map package read
in place) set only Tag, Event `catfishCounter`, Group, Location, Rotation, DrawScale (one at 2): no lists, no
`bNoDamage`, so every weapon's damage kills them. `TraceFireHitResponse` is not set on the chain: Actor's 2, so the
bullet stops on the fish (a hit).
**Through water:** Map21 holds 13 `WaterVolume` (a PhysicsVolume) and 2 `dnGeoWater`; both answer
`TraceFireHitResponse` 1 (section 3), which the bullet passes and `bullet_trace` models. Not modelled: a water BSP
surface TraceFire may skip or stop on. The `achievement target hidden` line below records what the trace hits.

**Design:**
- **When:** `ah_aim_kinds` (achhelp.h, tests 0524, 0527): the `AH_K_AIM` kinds of the rows listed in this map by name
  whose switch resolves on (`ah_esp_on`: on, or auto while unearned), the markers' own rule. Never in Duke's digs or
  while a checkpoint's map is pending. The catfish is the only `AH_K_AIM` kind.
- **Which:** achhelp builds the list at the end of `achhelp_frame` (before `aim_frame`) from its worldscan cache, under
  one guard, while Silent aim, the aimbot or the Triggerbot is on: `ah_aim_target_ok` (test 0526: Health read and
  above 0, not `bDeleteMe`, not hidden, not `bNoDamage`); the model box (`modelbox_read`'s corners, `ac_box_ok`), else
  the cylinder; `ah_aim_type_ok` (test 0528) against `aim_damage_type()` read last frame.
- **Priority:** `pick_walk` runs `pick_ach` after the ship pass, by `ah_aim_pass` (test 0525):
  only when the frame has no hostile pawn candidate at all (in range, on screen, inside the cone's pre-filter at its
  centre; hidden or not) and no ship was picked, so the bot never shoots fish during a fight; a fish lock is kept only
  while that holds. Among targets the pick mode and `aim_pick`'s hysteresis apply; range (the fists' reach), the screen, the
  cone, a mounted gun's strict limits, overkill and the damage type are tested; visibility is one bullet-flag trace
  to the box centre (ally or neutral pawns in the line refuse). The aim point is the box centre moved with a fresh
  `Location`; projectile guns intercept with the fish's `Velocity` (Lead on), which `PHYS_MovingBrush` may leave 0.
- **Triggerbot:** Locked and Aimbot follow the pick; Crosshair accepts a look hit that is an achievement target the gun
  hurts (`aim_ach_shootable`), within range, not doomed, and only by the same rule (`aim_pawn_cands`: the pick's count,
  or a trace-free count when the Triggerbot runs alone; unknown refuses). Overkill reads the fish's Health (`target_health`).
  `aim_track` drops it once achhelp no longer offers it or the gun's type cannot hurt it.

**Instrument:** `aim: achievement target <class> (<row>, no pawn or ship picked) at D units, A degrees off (mode; N
candidate(s) of M, Health H, model|cylinder box)` per pick (32 target lines a level, shared); `aim: achievement target
hidden: <class>; hit <class> (response R) at A of B units, P passed` (8 a level); the aim 600-frame line's `achievement
target N frame(s), skipped for the damage type, doomed, out of range or off screen, outside the turret's limits, out of
the cone, ally in the way, hidden`; the tbot line's `an achievement target N`; `achhelp: aim targets on|off (kinds
0x...)`; `achhelp: aim targets, 600 frames: offered avg, refused by the gun's damage type, faults`.

## 11. Splash: no explosive shot whose blast would reach Duke

Goal: the triggerbot is aware of explosive projectiles that can harm Duke if shot at an enemy that is too close.

**Summary (from the bytecode and the class defaults, `tools/ubc.py` and the tagged-property reader):** `Projectile.Explode` calls `DoDamage`, which (unless `bDamageDirectly`) runs `Actor.HurtRadius(Damage,
HitLocation, DamageRadius, DamageFalloffStart, DamageClass)`, or `HurtRadiusNoVisTest` with `bSkipVisibilityTests`.
HurtRadius takes every `VisibleCollidingActors(HurtRadiusAffectClass, DamageRadius, origin)` except the projectile itself
and what `CanHurtRadiusOther` refuses (`Projectile.WillNotHarmClasses`, empty in every class below). **The Instigator is
not excluded: Duke's own blast hurts him in single player.** No `bHurtInstigator` exists in build 244.

- **Falloff** (`Actor.CalcRadiusDamagePercent`): full damage within `DamageFalloffStart` (negative: that fraction of
  DamageRadius; Projectile's default -0.25) of the point on the victim's cylinder facing the origin
  (`HurtRadiusActivity`'s `CylinderLineSegmentIntersection`), then linear to 0 at DamageRadius.
- **Self damage:** HurtRadius's damager is the projectile's Instigator (the player for the hand guns, Weapon.ProjectileFire's
  spawn; `dnRocket_SuperTurret.PostVerifySelf` sets it to the turret's `User`). `PlayerPawn.TakeDamage` applies
  `PlayerDamageScale` only when the Instigator is someone else; `dnSinglePlayer.ModifyDamage` multiplies by
  `InstigatedBy.DamageScaling` and, for an injured player and a type without `bIgnoreDifficultyDamageScaling`,
  `TakenDamageScaler` (Difficulty 0: 0.75, 1: 1, 2: 1.5, else 2); its friendly zeroing skips the player's own shots;
  `DamageType.ReduceDamage` then scales by `SelfDamageScale` when the victim is the Instigator outside MP (DamageType 1,
  `RocketDamage` 0.5).

| Gun (primary; the alt fire is zoom) | Projectile | Damage / DamageRadius (full within) | DamageClass, SelfDamageScale | Duke's worst at Normal | Detonation |
|---|---|---|---|---|---|
| RPG (`RPG.ProjectileFire`; its lock-on is `RPG.Tick`'s look actor) | `dnRocket_RPG_Homing` < `dnRocket_RPG` | 125 / 260 (65) | RocketDamage, 0.5 | 62 (94 on Hard) | impact |
| Devastator | `dnRocket_Devastator` | 20 / 96 (24); 192 (125) while the Instigator `IsMostlyShrunk` (`PostVerifySelf`) | DevastatorDamage, 1 | 20 a rocket | impact |
| Enforcer gun | `dnRocket_RPG_HeatSeek` | 55 / 130 (32) | EnforcerGunDamage < RocketDamage, 0.5 | 27 | impact |
| Shrink ray | `dnRocket_ShrinkBlast` | - / 64 | ShrinkerDamage | none: `CanHurtRadiusOther` refuses its Instigator | impact |
| AT laser | `dnRocket_ATLaser` | 20, `bDamageDirectly` | ATLaserDamage | none (no HurtRadius) | impact |
| Freeze ray ice blast | `dnRocket_IceBlast` | 40, `bDamageDirectly` | ColdDamage | none | impact |
| Mounted grenade launcher | `dnGrenadeLauncherProjectile` | the turret's `GrenadeDamage` 80 / `GrenadeDamageRadius` 300 written on each grenade (150; falloff -0.5); `bSkipVisibilityTests`: walls do not shield | GrenadeDamage, 1 | 80 | any impact (`PerformFire` sets `bExplodeOnAnyImpact`), along its arc |
| SuperTurret | `dnRocket_SuperTurret` | 50 / 100 (25) | SuperTurretDamage, 1 | 50 | impact; spawned at `mount_beam`, turned toward `FireShot`'s trace hit |
| Pipe bomb | `dnPipeBombProjectile` | 125 / 260 | PipeBombDamage, 1 | 125 | the detonator; never fired by the triggerbot (thrown: gate "weapon not redirected") |
| Trip mine | `LaserMine_Thrown` < `LaserMine` | 125 / 100 | LaserMineDamage | - | its beam (`LaserMine.Detonate`); never fired by the triggerbot |
| AT Captain's laser (pinned, section 13) | `dnRocket_ATCaptainLaser` < `dnRocket_AssaultTrooper_Laser` | 20, `bDamageDirectly` (DamageRadius 5) | ATCaptainLaserDamage | none (no HurtRadius) | impact |
| Nuke (pinned, section 13) | `dnRocket_Nuke`: its own `DoDamage` is empty; `SpawnOnDestroyedSimple` spawns `dnNukeExplosion`, whose `StartEffect` runs `HurtRadius(500, Location, 1024, -0.25, NukeDamage)` | 500 / 1024 (256); the panel reads the rocket's own DamageRadius, also 1024 | NukeDamage < ExplosionDamage; the explosion's Instigator not read | 500 (lethal) | impact |
| Expander (section 13) | `dnRocket_ExpandBlast` | - / 48 | ExpanderDamage | none: `CanHurtRadiusOther` refuses its Instigator | impact |
| Impregnader (section 13) | `dnGrenade_PregnatorGunBlast` < `dnGrenade` | 65 / 60 (15) | PregnatorGunDamage, 1 (no override; Duke is not spared) | 65 | any impact (`bExplodeOnAnyImpact`), else its fuse 4 +- 2 s |

Impact: `Projectile.HitWall`/`KImpact` -> `ProjectileHitSomething` -> `ExecuteExplode` at `Location + ExploWallOut (16) x
HitNormal`; no deferred timer in SP (`dnRocket.DeferredExplosionTimer` 0). Hitscan guns and the MG42 have no blast.

**Rule (splash.h, tests 0530-0533, 0538, 0540):** hold fire while the predicted detonation point is less than 1.1 x
DamageRadius from Duke's collision cylinder (`splash_cyl_dist`: horizontally beyond his `CollisionRadius`, vertically
beyond his `CollisionHeight`; read by name, unread 21 and 45). This is the margin: DamageRadius plus his collision
radius plus 10 %, with his height counted below his feet. The point is the one nearest Duke of (a) the target's point at
the intercept (`aim_intercept` from the real origin, the target's Velocity with Lead moving targets on; the launcher's
grenade unsolved for a moving target is solved for its current point; unsolved, its point now); (b) for a straight
projectile the first blocker on the line from the origin to (a), and (b') on the gun's current line (`view_eye_fwd`) as
far as (a) plus the blast's reach; (c) for the launcher's falling grenade the first blocker along the solved arc, and
(c') along the arc launched in the gun's current direction for as long (unsolved: the horizontal distance at the
launch's horizontal speed, 3 s at most), 5 chords each. The origin is the camera on foot (`Weapon.ProjectileFire`), the
launcher's next muzzle bone, or the SuperTurret's `mount_beam` bone (within 2048 units of the gun; the name is looked up
again each level). The blocker trace is the shot-flag trace with no penetration budget: the
world or any hit with `TraceFireHitResponse` other than 1 is the detonation point. Response-1 actors are passed, as a
rocket passes them: a trigger only gets `Touch` (`Projectile.ProjectileTouchedSomething` is empty; no rocket class
overrides it); a volume or water explodes a projectile only with `bDestroyOnVolumeTouch` or `bExplodeOnWaterTouch`
(`Projectile.PhysicsVolumeChange`), which no class sets. **Glass is the exception** (`splash_passes`):
`Projectile.ProjectileHitSomething` breaks a `BreakableGlass` and flies on only with the projectile's `bBreakGlass` (set
on `dnRocket_SuperTurret` alone) and the pane not `bUnBreakable`; every other rocket falls through to `ExecuteExplode`, and
the launcher's grenade (`bExplodeOnAnyImpact`) explodes before the glass test. Hits on the player, the gun and what hangs
from them are stepped past too (6 passes at most). 12 traces a call at most; a frame can make two calls (the boss judged, then an add fired at), so 24 at worst. **Fails closed:** Duke's Location, the origin or the aim point unread, a trace refused, or the budget spent
before a verdict holds an explosive shot. Only explosive shots (`splash_kind`): a hitscan gun, `bDamageDirectly` or the
shrink blast is never held; DamageRadius unread holds inside the default 400 (logged). The Devastator's radius doubles
while `Pawn.IsMostlyShrunk()`. The aimbot keeps tracking; only the press stops (the hold ends the condition, so a press
in flight is released).

Instrument: `aim: splash <class>: DamageRadius R, bDamageDirectly d -> ...` once per weapon config; `aim: mounted <gun>
splash: ...` per attach; `tbot: held fire: <gun>'s blast would reach Duke: radius R (DamageRadius | unread: the
default), reach X beyond his collision (radius, half-height); detonation point D units from it (C from his Location) by
(a) | (b) | (b') | (c) | (c'), hit <class>; target <class>`, or `tbot: held fire: <gun>'s blast not judged: <why> (N
trace(s))`, the first frame of each hold, 8 a level; the tbot 600-frame line's `held fire: blast would reach Duke N
frame(s) (H hold(s); not judged F, trace budget spent C; T splash trace(s), M ms, max X ms a frame)`.

## 12. Boss priority: the boss first, no ammo on the adds

Goal: smart targeting in boss battles. Other enemies spawn during some boss fights; the triggerbot focuses the boss whenever
possible and does not waste ammo on other enemies.

**Design:** the boss is the HUD's boss-meter actor while `bDrawBossMeter` is set (world.cpp
`read_boss_meter`, `WPawn.bossMeter`; factions.md "The attack script"); no class list and no setting. A fight without a boss meter
behaves as before.
- **Possible** (`boss_eval`, aim.cpp; verdict `aim_boss_verdict`, test 0535): the pick's own tests in its order, the first
  failure the reason: alive (a knockdown is not), in range and on screen (`pawn_cand`), not left out by spawnbox
  (invulnerable, teleporting, the held gun's damage type, doomed by overkill, animated away without a box), a mounted
  gun's reach and limits, the cone (the pre-filter and the aim point), the projectile's reach, then visibility by the
  bullet-flag probe (through its mounted collision when it has some, spawns.md section 8), an ally in the line or frozen
  under Auto-execute counting as hidden. The boss is probed first, so the frame's 4 traces serve it before the adds.
- **Splash:** while the Triggerbot is on, a visible boss its splash guard would hold fire on (section
  11, including a fail-closed hold) is not possible, reason `the blast would reach Duke`: the adds may then be fired at,
  each judged by the same guard. The boss's `aim_splash` call is kept for the frame, so the Triggerbot's on the same
  target spends no second trace cap (test 0541).
- **The pick** (`aim_boss_pick`, test 0536): while possible, the boss and nothing else, whatever the pick mode; the adds
  are not walked or traced. Otherwise the normal pick runs over the adds with the boss's entry taken out and no
  hysteresis toward it; the boss is judged again every frame, so the pick goes back to it the frame it is possible.
- **Where it is judged:** by the pick (its cone) while Silent aim or the aimbot picks; for the Triggerbot alone, by the
  screen without the cone (Crosshair mode has none), and then on screen whatever Off-screen targets says, or a boss
  behind Duke would hold fire on the adds in front (`aim_boss_offscreen`, test 0541).
- **Triggerbot** (`tbot_boss_skips`, test 0537), every mode: while the boss is possible a target that is not the boss is
  not fired at. In Crosshair mode an add under the crosshair is skipped; the look hit of a mounted helper maps to the
  boss (`tbot_attached_pawn`), so the boss's collision fires.

Instrument: `aim: boss priority: on <class>` and `aim: boss priority off: <reason> (<class>)` on each change of none/on/off
or of the boss (32 a level); the target line's `; the boss, by priority`; `aim: <class> lost the pick: the boss is possible`, and for the boss `the boss is not possible: <reason>`;
`aim: boss priority, 600 frames: on the boss N frame(s), not possible M (<reason> n, ...), no boss meter K; the Triggerbot
held off an add F frame(s) (H time(s))`, written only in a window with a boss meter.

## 13. The DLC's weapons: the Expander and the Impregnator

`aim_weapon_kind` is a table of the base game's config names; `ExpanderWeaponConfig` (< `WeaponConfig`,
DLC03_Game.u) is not in it, so its kind would be `AIMW_NONE` and the gate would read "weapon not redirected"
(`AIM_G_WEAPON`) for Silent aim, the aimbot and the triggerbot alike (`aim: weapon ExpanderWeaponConfig: not
redirected, AmmoExpenditure 1, MaxTraceDistance 0`, `damage type unknown (not read for this kind)`). The fallback
below classifies such configs.


**Summary (DLC03_Game/_AI.u decoded with `tools/ubc.py`, class defaults by the tagged-property reader; from the scripts):**
- **Fire:** `Expander` (< `Weapon`) overrides only effects: `Fire` calls `Weapon.Fire` and sets `bFirePreProjectile` (a
  pre-fire sound), `Fire_Effects` clears it and calls `Weapon.Fire_Effects` -> PerformFire. No charge state, no
  `ProjectileFire` or `AdjustProjectileSpawn` override: the blast is spawned by `Weapon.ProjectileFire` exactly as the
  RPG's rocket. Its config sets no `bFireInterruptFiring`, `bHasFireStart`, `bHasLoopingFire` or `LockoutFireTime`: the
  triggerbot holds (section 4). **Not a charge weapon, not a beam, no second hitscan path.**
- **Projectile:** `ExpanderWeaponConfig.ProjectileClass = dnRocket_ExpandBlast` (< `dnRocket`): Speed 2500, MaxSpeed
  2500 (AccelerationScaler 50 from dnRocket, so a constant 2500 u/s), LifeSpan 6 s (reach 15000 units), Physics 6
  (`PHYS_Projectile`: straight, no gravity), Damage 100 (dnRocket's), DamageRadius 48, DamageClass `ExpanderDamage`.
- **Alt fire:** none of its own: the generic `PlayerPawn.ZoomDown`.
- **Damage:** `ExpanderDamage.ReduceDamage` returns 0 outside multiplayer (0.1 in MP, 9001 on a bloated MP player): **no
  Health is lost in single player**. `ExpanderDamage.ApplyServerDamageEffects` calls `StartBloat(BloatTime 12)` when
  `CanBloat()` (`bCanBloat && !bOnSteroids`; `EDF_Grunt_DLC03` and `GeneralGraves_DLC03` clear bCanBloat).
  `dnRocket_ExpandBlast.DoDamage`: a `Biology_Generic` hit takes the full Damage (the catfish die); an AI in `ControlX`
  that can bloat is bloated directly; else `Projectile.DoDamage` (HurtRadius 48). Its `CanHurtRadiusOther` refuses its
  Instigator and every non-pawn, and a pawn whose `AcceptsExpandDamage` refuses (`Enforcer_Shield_DLC03` with its shield
  toward the blast; not modelled).
- **Bloat and pop:** `Pawn.StartBloat`: `BloatState` 1, `TimeWarp = ExpandTimeWarp` (0.3: slowed), an `EndBloat` timer
  of `BloatTime x TimeWarp`; `BloatFinished` (full) 2, `EndBloat` 3, `EndBloatFinished` 0. `Pawn.TakeDamage` multiplies
  any damage by 4 while `BloatState != 0`. The kill is the pawn's own `TakeDamage(9999, ExpanderPopDamage)`:
  - classes with their own `SecondBloat` (`PigCop_*_DLC03`, `Pigcop_Captain/Berzerker_DLC03`, `AssaultTrooper_*_DLC03`,
    `Enforcer(_Shield)_DLC03`, `Octabrain_DLC03`): a hit while bloated sets SecondBloat and pops 0.5 s x TimeWarp later
    (`ExpandDeath`); a third hit pops at once (`BlowUpNow`);
  - `Rat_DLC03`, `Pregnator_DLC03`, `Pregnator_EggPod_DLC03`, `Octababy_DLC03`, `EggPod_DLC03`: pop in `BloatFinished`,
    one hit;
  - the base enemies, `AssaultCommander_DLC03`, `DukeRobot_Metallic` (BloatState 2 at once): bloat only; a further hit
    restarts the bloat. **So the Expander kills the DLC's own enemies, within the blast's 15000 units.**
- Bubble Buster (achievement 50, dlc.md section 5) counts melee kills on a pawn with `BloatState != 0`.
- **The Impregnator** (`PregnatorGun`, config `PregnatorGunWeaponConfig`): `dnGrenade_PregnatorGunBlast` (< `dnGrenade`),
  Physics 18 (`PHYS_Meqon`): `InitializeMotion` -> `dnGrenade.DoThrow` -> `SetPhysics(18)` and
  `KSetLinearVelocity(Speed 800 x dir + UpwardsBoost (0,0,140))`, GravityScale 0.5, physics-engine damping,
  `KDisableCollisionBetween(Instigator)`; explodes on any impact or after 4 +- 2 s; Damage 65, DamageRadius 60,
  `PregnatorGunDamage`. `CanFire` refuses in water; ammo regenerates (`AmmoRegenRate` 1.25 s). Aimed as a
  ballistic lob: see "The lob" below.
- **The others:** `HotSauce` (`bNonWeapon`, AmmoExpenditure 0, no ProjectileClass, no TraceDamageType: a drink, ego award
  8) and `MoonBuggy_PowerCellWeaponConfig` (a quest item) fire nothing. DLC03_AI's `*_DukeRobot` configs are enemy
  weapons. The DLC03 packages hold no other `Weapon` subclass but the MP ones.

**Fix (aim.h `aim_weapon_classify`, test 0557):** the table first (every base gun keeps its verified kind); a config it
does not name by what PerformFire does with it: a ProjectileClass whose default `Physics` is 6 is a straight projectile
(the intercept, overkill, the splash guard); any other physics is thrown or physics-driven, not redirected (the
Impregnator 18, the pipe bomb 18, the trip mine 9); no projectile, a MaxTraceDistance and a TraceDamageType: hitscan;
else nothing. The weapon line prints `(<how>; ProjectileClass <class>, Physics <n>)`.
- **Pinned (tests 0557):** the fallback also turned two base configs into projectiles. Kept, and
  pinned in `kKinds` so the kind is a decision, not an accident: `ATCaptainLaserWeaponConfig` (`dnRocket_ATCaptainLaser`:
  Speed 900 rising 50/s, MaxSpeed 9e14, LifeSpan 6 s; `ATCaptainLaser.AdjustProjectileSpawn` adds Rot(200, -200, 0) plus
  RVar +-200, taken back by `aim_spawn_offset`; `bDamageDirectly`: no splash) and `NukeWeaponConfig` (`dnRocket_Nuke`:
  Speed 500 rising 50/s to MaxSpeed 700, LifeSpan 6 s; `Nuke.CanFire` waits for its countdown; the blast is
  `dnNukeExplosion`'s HurtRadius 500 over 1024 units, so the triggerbot holds while the detonation point is within 1126
  units of Duke). Both rows are in section 11's table.
- **MP configs:** the fallback changes their kind too (`MP_*WeaponConfig` < `MP_WeaponConfig` name nothing in the
  table): their rockets become projectiles, their traced guns (MaxTraceDistance and a TraceDamageType) hitscan, the
  thrown ones stay out. **The aim has no multiplayer gate:** `aim_gate` (aim.h) takes on, the panel, inLevel, alive,
  grabbing, attached, ViewMapper, control, the weapon kind and the mount; no `NetMode` or `IsMP` read exists in
  world.cpp, aim.cpp, aimbot.cpp or tbot.cpp. So the aimbot and the triggerbot would run in a multiplayer match; Silent
  aim's bracket would not redirect there (`GetTraceFireAim` reads `WeaponStartTraceDirection` when `IsMP`, section 1).
  Not measured: the panel has never been run in multiplayer.
- **Splash:** `splash_spares_instigator` (test 0559): `dnRocket_ExpandBlast` spares its Instigator as the shrink blast
  does, so it is never held for Duke's sake.
- **Expanded or popping (bloat.h, tests 0558, 0559):** while the held gun deals `ExpanderDamage` (its class chain by
  name), spawnbox reads each hostile pawn's `bCanBloat`, `bOnSteroids`, `BloatState` and its class's `SecondBloat` by
  name every frame under one guard. `bloat_verdict`: cannot bloat -> left out as the wrong damage type; a SecondBloat
  class needs 2 steps, any other 1; a step is SecondBloat set (2), or `BloatState` set (1; a non-popping class shrinking
  back counts 0, so it is bloated again). Done -> `SB_EXCL_EXPANDED` ("fully expanded or popping"): not picked, a lock
  dropped, the triggerbot holds, the boss verdict says so (`AIM_BOSS_EXPANDED`). A bloated pop-capable pawn is still a
  target: that hit kills it.
- **Overkill (test 0560):** the blast's Damage 100 would have doomed every pawn of Health 100 or less. On a walked pawn an
  Expander blast in single player counts no damage (`ok_proj_counts_dmg`; any other target keeps it: `DoDamage` deals it
  in full to a `Biology_Generic`, the catfish). The pawn is doomed when the other projectiles' damage reaches its Health,
  or when its blasts in flight complete the pop (`bloat_verdict` with them; `ok_bloat_expected` stands its Health in).
  An unread bloat state (the Expander put away with blasts in flight, the table full) never dooms by the blasts. A mark
  set by the blasts records the pawn's bloat steps (`bloat_steps`); cleared with the pawn alive and a step gained, it is
  counted as a landed blast (`ok_bloat_landed`, `OK_EV_LANDED`), not a miss.

Logs: `aim: weapon ...: projectile (by its ProjectileClass (PHYS_Projectile); ProjectileClass dnRocket_ExpandBlast,
Physics 6)`; `spawnbox: Expander on <class>: bCanBloat, bOnSteroids, BloatState, SecondBloat -> <verdict>` on each
change (16 a level); `spawnbox: Expander, 600 frames: N hostile pawn-frame(s) read, cannot bloat I, fully expanded or
popping D`; the aim 600-frame line's `expanded or popping N`, the tbot line's share; `overkill: projectile ...; a bloat
type: judged by bloat steps` and `overkill: doomed <class> by the Expander: BloatState, SecondBloat, N blast(s) in flight`; the overkill 600-frame line's
`Expander blast landed N` and the clear line's `the Expander's blast landed: bloated or popping`.

### The lob: the Impregnader aimed

**Read (DLC03_Game.u, dnGame.u, Engine.dll):**
- **Fire path:** `PregnatorGun.Fire` = `Weapon.Fire` (+ `bFirePreProjectile`); the projectile comes from
  `Weapon.ProjectileFire` (camera aim, `GetWeaponAim`), then `PregnatorGun.ProjectileFire` calls
  `KDisableCollisionBetween(Instigator)`; `AdjustProjectileSpawn` moves the spawn 5 units forward. The config sets no
  cadence flags: the triggerbot holds. **Alt fire:** none (zoom); `PregnatorGun.Melee` is the tongue slap (the melee key).
- **Projectile:** `dnGrenade_PregnatorGunBlast`, Physics 18 (`PHYS_Meqon`). `InitializeMotion`: `UpwardsBoost` (0,0,140)
  turned by the gravity's direction (none in normal gravity) and x `Instigator.DrawScale`; `ThrowImpulse = UpwardsBoost +
  Speed (800) x dir`, x `DrawScale^0.25`; `dnGrenade.DoThrow`: `SetPhysics(18)`, `KSetLinearVelocity(ThrowImpulse)`.
  `dnGrenade` defaults: `GravityScale` 0.5, `KLinearDamping` 0, `KAngularDamping` 0.
- **Meqon gravity** (`meqRBUtils::HandlePerFrameUpdate` `0x101EC0A0`, `ApplyGravity` `0x101EA500`, `AActor::GetGravity`
  `0x10257F40`): force = mass x the PhysicsVolume's Gravity x a scale that is `*(+0x550) x *(+0x53C)` only when `Actor+0x50
  & 0x200`; the speed is clamped to the volume's `TerminalVelocity` (+0x730); damping = volume +0x734 x actor +0x524
  (KLinearDamping 0: none). Which property +0x53C is (GravityScale expected) and which bool is the flag were not read: the
  model takes Gravity x GravityScale (0.5: 475 u/s^2, matching the measured flights), and the weapon line prints
  GravityScale's offset.
- **Detonation:** `bExplodeOnAnyImpact`: the first `HitWall` or `KImpact` (`KImpactThreshold` 50) runs `ImpactExplode`
  at its Location; no bounce, no stick. Else the fuse primed in `PostVerifySelf`: `ExplosionDelay` 4 +- 2 s
  (`NonImpactExplode`); `bExplodeOnWaterTouch`, `bExplodeOnDamage`.
- **Damage:** 65 over 60 units, `PregnatorGunDamage` (no `ReduceDamage` or effect override: "impregnate" is a name; the
  kill counts toward Threesome: `DukePlayerDLC03.UpdatePregnatorGunAchievement`, three kills within 3 s, achievement 51).

**Model (aim.h `aim_lob_model`, `aim_launch_vel`; tests 0562, 0563):** `AIM_PM_BALLISTIC`, no bBounce half step (Meqon,
not physFalling), launch velocity `Speed x dir + (0,0,UpwardsBoost.Z)` (both x DrawScale ratio^1.25), gravity the player's
PhysicsVolume Gravity.Z x GravityScale, the volume's TerminalVelocity, flight bounded by the shortest fuse (ExplosionDelay -
Variance: 2 s, so about 1600 units at most). The arc solver takes the boost (`aim_ballistic_reach`; the peak angle is the
root of `cos + (b/s) sin cos - g D sin / s^2`; a short fuse lowers the bracket's top). The aim is the solved launch
direction (the game adds the boost). The first impact is the detonation point: the splash guard (65 / 60, Duke not spared)
and overkill (Damage 65) apply as to any projectile. Classification: a `PHYS_Meqon` ProjectileClass whose default sets
`bExplodeOnAnyImpact` is a lob (`AIMW_BY_LOB`); the pipe bomb (no impact detonation) stays thrown.

**Instrument (named "ballistic": it covers the mounted launcher too):** `aim: lob model
<class>: ... GravityScale g (+0x..; 0x53c expected), fuse ...` once per config; `overkill: ballistic shot at <class>: launch
pitch P degrees (solved), flight T s, predicted impact x,y,z; eye x,y,z, first seen x,y,z, offset from the eye dx,dy,dz
(horizontal h, vertical v)`, logged when the pass finds the projectile (16 a level); `overkill: ballistic <class> ended
after T s (game time) last seen at x,y,z; first seen at ..., speed S, velocity pitch Q (from the next Location); predicted
impact ... (D units off; solved pitch, flight); the model from its first state ... (E units off; gravity, boost,
terminal)` per projectile (16 a level). D is the whole aim (origin, launch, flight); E is the flight alone from the first
state the pass saw. The last position is one frame before the detonation (about 13 units at 800 u/s). `Actor.Velocity`
reads 0 for a Meqon body: the first velocity is the difference of the first two Locations
(`aim_fd_velocity`, test 0564, its vertical put back by half a step of gravity).

**Measured (5 flights):** the model fits within a few units: Speed 800, boost 140, gravity
475 (so GravityScale 0.5 applies). The spawn back-solves to about 12 units below the eye (8-13): the lob's origin is the
eye lowered by `AIM_LOB_SPAWN_DZ` (-12, aim.h; only `AIMW_BY_LOB`), and the shot line keeps measuring it.

**Cost:** a target the arc has no solution for is not solved again for 100 ms (`aim_nosolve_recent`, test 0565; the
600-frame line's `skipped while recent N`): the short-fuse scan steps the whole flight per angle.

## Open questions

- Freeze ray (section 1): the in-game weapon line `BeamMaxLength N` has not been seen; classic mode's
  `dnRocket_IceBlast_Classic` reach is not measured.
- Fists (section 1): the punch cadence and the out-of-ammo release are read from the scripts, not observed at runtime.
- Look trace (section 4): the offsets of `DirectLookHitActor`/`LookHitDistance` expected at 0x1D64/0x1D74 are not
  confirmed by the logged names; the `LineOfSightTo` time of the first 64 calls (the triggerbot fallback) is not measured; who calls the `UpdateLookHitActor` event per tick was not traced (count it in pehook).
- Flying troopers (section 4): which actor the look trace hit is not measured (the jetpack effects or the held weapon
  are the reading); why the nearer trooper lost the pick (the cone test at the aim point is the suspect) is open.
- Mounted camera (section 9): the camera model is not exact (the blend or the camera bone may explain the gain); the
  rule holds for any gain below 2.
- Overkill (section 7): target-side damage scaling is not modelled (a pawn's `DamageScaleInfo` below 1, armour
  `DamageAbsorbers`, a ship's own damage rules), like the full splash: either over-estimates, so a high `cleared ... alive` count
  against `marks` may be these and not misses (each costs at most one flight, 2 s capped). Unchecked:
  that the launcher's MuzzleIndex moves per grenade (else the projectile takes the lock of the frame it is
  found, up to 100 ms late); the grenade's Instigator (the log line); the pass cost while armed (the 600-frame line;
  above 2 ms a pass, move to a cheaper list); that misses stay rare at Map12's ranges (the gunship's `cleared ... alive`
  count against `marks`); the RPG's homing rockets reaching another target than the lock.
- Projectile models (section 8): that `Actor.bBounce` is `+0x58` mask `0x10` (the mount line; a different place means
  full gravity with the clamp, and grenades landing short of the marker); that +0x980/+0x984 are Speed/MaxSpeed (the
  weapon line); Map12's PhysicsVolume gravity (the mount line); the 60 Hz step (bBounce's drift is `g T dt / 4`, about
  7 units over 1.7 s); one frame of spawn latency (a few units on a running target); the AT laser's random +-200; the
  RPG's homing after a lock and the Enforcer gun's seeking change the path after launch; the solve cost while the
  launcher is mounted (the 600-frame line; above 0.5 ms a solve, cache the angle between frames).
- Catfish (section 10): whether the game's own look trace (`DirectLookHitActor`, the Triggerbot's Crosshair) reaches a
  fish through the water; `TraceFromCrosshairMesh` is not the bullet's walk. The tbot 600-frame line's `an achievement
  target N` settles it: N above 0 with the crosshair on a fish says yes; 0 while Locked or Aimbot fire at fish says no.
- Splash (section 11): that Duke is hurt as read (a hold line with no damage taken when firing anyway); a map volume
  with `bKillMarkedProjectiles` and a projectile with `bDestroyOnVolumeTouch` (none read); that `TraceEx` with
  `IgnoreActor` set still skips the player (a stepped-past self hit covers either answer); the SuperTurret's spawn bone
  (`FireShot` reads `mount_beam`; the spawn activity's own mount was not decoded); the RPG's homing and the Enforcer
  rocket's seeking after launch; the arc's 5 chords against a thin ledge; the trace cost while held (the 600-frame
  line).
- Multiplayer: the aim has no multiplayer gate and the panel has never been run in multiplayer.

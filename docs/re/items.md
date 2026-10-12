# Items and explodables: pickups, caches, explosive props (build 244)

Every single-player pickup is an `Engine.Pickup` actor (`WeaponPickup`, `AmmoPickup`,
`AutoUseWeaponPickup` or a direct subclass) whose `InventoryType` names the inventory it gives. In single player a
taken pickup is **destroyed** (`Pickup.HandleRespawn`: `NetMode == NM_Standalone` -> `Destroy()`); no respawn, no
hide. **The game's own "can I take it now" gate is one static, side-effect-free native,
`Inventory.CanPickupClass(InvClass, Pawn)`** (`Engine.dll!0x100CFC60`): the game mutators, then
`Pawn.PawnAllowPickup`, which asks every held item `InventoryAllowPickup(InvClass)`. It refuses exactly: a weapon
already held whose ammo is full, an ammo/charge item whose held copy is at `MaxCharge`. **The two-weapon limit
never refuses**: a third primary weapon swaps out and drops the current one (`APlayerPawn::AddInventory`
`Engine.dll!0x1026A5E0`). Caps (class defaults): pipe bombs 4, trip mines 4, beer 1, steroids 1, HoloDuke 1.
**Containers:** `Containers_Crates_ItemCrate` (1 hp, breaks and spawns its instance `SpawnedInventory`; 57 in SP,
almost all pipe bombs/trip mines) and the infinite-ammo crate/pile (never empty). **Explodables** are decorations
whose `DestroyedActivities` deal radius damage (barrels, red canisters, propane, fire extinguishers, boulders),
plus cars, lanterns, hive gas pods and `dnDestructibleBuilding`; alive = `dnDecoration.DecoState == 0`.
**Cost:** a level holds 0-34 placed pickups and 0-78 explodables; the egoesp full object-table scan took 0.63 ms
on Map00's 71,704 objects; worldscan's rescans measure about 1.7 ms each in play (nukepanel.log `worldscan: scans=` lines). **Pickups spawn mid-level** (enemy weapon drops, Pig Cop pipe bombs, crates, corpse
pinatas, Duke's own swap drop), so the cache needs a periodic full rescan, not one scan per level.

Method: read from the script packages, the map packages and Engine.dll. `tools/ubc.py` (bytecode) plus a scratch tagged-property reader over `extracted-packages/*.u` (class
defaults, inherited along the class chain) and `Maps\Map*.dnf` (instance properties; read in place, read only);
`System\dnGame.int` / `dnDecorations.int` / `Engine.int` for names; Ghidra MCP on Engine.dll (plate comments on
`0x100CFC60` and `0x1026A5E0`, saved).

## 1. Pickups

### Classes (SP; every `MP_*` class skipped)

Chain abbreviations: WP = `WeaponPickup < Pickup < InteractiveActor < RenderActor < KarmaActor < Actor` (Engine),
AP = `AmmoPickup < Pickup`. Pickup flags: `bAllowUsePickup` (taken with Use), `bAllowTouchPickup` (taken by walking
over it). Weapon pickups are taken with Use (the player's `TouchingWeaponPickup` + `ServerUseDown` ->
`CheckHoldForWeaponPickup` -> `Used`, Engine `PlayerPawn`); `WeaponPickup.CanBeUsedBy` returns false only to keep
the generic use prompt off. "Cap" = the ammo class's `MaxCharge` default (the weapon's `WeaponConfig.AmmoClass`).

| pickup class (pkg) | chain | InventoryType (chain) | ItemName (.int) | use / touch | cap | SP placed |
|---|---|---|---|---|---|---|
| PistolPickup (dnGame) / `_Gold` | WP | Pistol / Pistol_Gold (PistolBase < Weapon) | M1911 | yes / yes | ammo 64 | 54 / 1 |
| ShotgunPickup | WP | Shotgun (Weapon) | Shotgun | yes / yes | 28 | 54 |
| MachineGunPickup | WP | MachineGun (dnContinuousFireWeapon) | Ripper | yes / yes | 200 | 59 |
| RPGPickup | WP | RPG | RPG | yes / yes | 5 | 42 |
| RailgunPickup | WP | Railgun | Railgun | yes / yes | 12 | 28 |
| DevastatorPickup | WP | Devastator | Devastator | yes / yes | 69 | 6 |
| ShrinkRayPickup | WP | ShrinkRay | Shrink Ray | yes / yes | 10 | 6 |
| FreezeRayPickup | WP | FreezeRay (dnContinuousFireWeapon) | Freeze Ray | yes / yes | 200 | 8 |
| EnforcerGunPickup | WP | EnforcerGun | Enforcer Gun | yes / yes | 15 | 2 |
| ATLaserPickup / ATCaptainLaserPickup | WP | ATLaser / ATCaptainLaser | AT Laser / AT Captain Laser | yes / yes | 60 / 80 | 2 / 5 |
| PipeBombPickup | WP | PipeBomb (PipeBombBase < Weapon) | Pipebomb | **no** / yes | **4** | 83 (+49 in crates) |
| TripMinePickup | WP | TripMine (Weapon) | Trip Mine | yes / yes | **4** | 41, 2 hidden (+29) |
| FortyOunceBeerPickup | WP | FortyOunceBeer (Weapon) | 40oz Beer | yes / yes | **1** | 18 (+6) |
| SteroidsPickup | WP | Steroids (Weapon) | Steroids | yes / yes | **1** | 7 (+8) |
| HolodukePickup | WP | HoloDuke (Weapon) | Holoduke | yes / yes | **1** | 7, 1 hidden (+6) |
| WallBombPickup | WP | WallBomb (dnControlQuestItem < dnQuestItem < Weapon) | Wall Bomb | yes / yes | 5 | 2 |
| CigarPickup | `AutoUseWeaponPickup < WP` | Cigar (dnActivateUsedWeapon) | Cigar | yes / no | 1 | 4 |
| Pistol/Shotgun/MachineGun/RPG/Railgun/ShrinkRay/FreezeRay/Devastator`AmmoPickup` | AP | `<Weapon>Ammo` (Ammo < Inventory) | none | no / yes | as above | 6 (pistol only) |
| JetpackPickup (dnGame) | Pickup | Jetpack (Upgrade_SOS < ActivatableInventory) | none | yes / yes | charge 100 | 0 |
| BatteryPickup, GasCanPickup (dnDecorations), EnergyCellPickup (dnGame) | Pickup | Battery / gascan / EnergyCell (dnControlQuestItem) | none | no / no, `bGrabbable` | 1 / 10 / 3 | 1 / 8 / 5 |

- **No keys or keycards exist** in DNF (no such class in any package). Quest items (battery, gas can, energy cell,
  wall bomb, and the control-given condom/popcorn/vibrator `dnQuestItem_*`) are the only "carry it there" items;
  the first three are grabbed (`bGrabbable`), not used or touched (their conversion path was not read).
- No health pickups: `HealthInventory`/`EnergyInventory` exist (handlers refuse health at the cap,
  `HealthInventoryHandler.InventoryAllowPickup`) but no SP map places a pickup for them.
- **Display name:** `WeaponPickup.ItemName` (localised `<?int?dnGame.<Class>.ItemName?>`, `System\dngame.int`
  `[<Class>] ItemName=`); read the property from the class default object at runtime. Ammo pickups, jetpack, quest
  pickups and `NukePickup` have none: the panel names them from the InventoryType class. Generic fallback:
  `Engine.int [Pickup] UsePhrase=Pick Up`.
- **A weapon pickup vs a weapon in the world:** the world never holds a loose `Weapon` actor for the player to
  take; a weapon is always shown by a pickup. An AI's weapon is an inventory item owned by the pawn (not an ESP
  target). A **dropped** weapon is a fresh `WeaponPickup` with `bWeaponDropped = true` and `AmmoCharge` = the
  weapon's ammo (`Weapon.SpawnPickupFor`, Engine); a placed one has `bWeaponDropped` false and `AmmoCharge` 0
  (it then gives `WeaponConfig.AdditionalPickupAmmoMin + Rand(AdditionalPickupAmmoRnd+1)` ammo,
  `Weapon.PickedUpAdditionalCopyCustom`).

### How "taken" shows

- `Pickup.FinishPickup` -> `HandleRespawn`: `if (RespawnTime < 0 || Level.NetMode == 0) Destroy()` (Engine
  `Pickup.HandleRespawn`, decoded). SP: always destroyed. The MP `Sleeping` state (`EnterVoid`: `SetCollision(false)`,
  `SetHidden(true)`) never runs in SP.
- `AutoUseWeaponPickup` (cigar box): `--PickupsRemaining`; destroyed only at 0 (`PickupsRemaining = NumPickups` in
  `PostLoadMap`; CigarPickup `NumPickups = 1`).
- So the ESP's per-frame validation (object slot still holds this object, not `bDeleteMe`) is the "taken" test;
  also skip `bHidden` (3 SP pickups are placed hidden: `HolodukePickup` x1, `TripMinePickup` x2).

### What makes one "not takeable now" (the fields the game checks)

Path: `Pickup.Touch` / `Used` -> native `APickup::AttemptPickup` `0x100CF7C0` -> `DoPickup` ->
`AInventory::AttemptPickup(InvClass, Pickup, Pawn, Source)` `0x100D14F0` -> **`AInventory::CanPickupClass`
`0x100CFC60`** -> on false `Pawn.FailedPickupAttempt` (HUD "can't pick up" icon, `DukePlayer.CantPickupWeapon`
/`CantPickupAmmo`), on true `PickupSuccess` `0x100D0B00` (finds the held copy by `InventoryReferenceClass` and
calls its `PickedUpAdditionalCopy`, else spawns a new inventory item and `GiveTo`).

`CanPickupClass(InvClass, Pawn)`:
1. `Level.Game` vt+0x240 `GameAllowPickup` -> mutator chain (`AGameInfo::GameAllowPickup` `0x1027B810`,
   `AMutator::AllowPickup` `0x1027B880`); true when no mutator says otherwise.
2. Event `Pawn.PawnAllowPickup(InvClass)` (Engine, decoded): false if `!CanDoThings()`; else false if any item on
   `Pawn.InventoryListHead -> NextInventory` returns false from `InventoryAllowPickup(InvClass)`.

`InventoryAllowPickup` (all SP overrides decoded; all pure reads):
- `Inventory`: only for a held item with the same `InventoryReferenceClass` as `InvClass`'s default: refuse when
  `MultiplePickupBehavior == PICKMULT_CompositeChargeClamped (2)` and `Charge >= MaxCharge`, or when it is
  `PICKMULT_Disallow (1)`. (`EInventoryMultiplePickupBehavior`: None, Disallow, CompositeChargeClamped, Custom.)
- `Weapon`: for a weapon `InvClass` with the same `InventoryReferenceClass`: refuse when
  `CanPickupLikeWeapon` is false, i.e. **`Weapon.Ammo.Charge == Weapon.Ammo.MaxCharge`**; then the Inventory rule.
  Weapons are `PICKMULT_Custom (3)`: taking a held weapon adds ammo (`PickedUpAdditionalCopyCustom`).
- `HealthInventoryHandler`: refuse health items at `Health >= GetHealthCap()` (no SP health pickups).

So, per question:
- **Ammo pool full:** ammo pickup `InvClass` = `<X>Ammo` (behaviour 2): refused while the held `<X>Ammo.Charge >=
  MaxCharge`. Without the weapon, ammo is still taken (nothing held refuses it).
- **Two-weapon limit:** not a refusal. `APlayerPawn::AddInventory` `0x1026A5E0` (plate comment) fills
  `PrimaryWeapon1`/`PrimaryWeapon2` (+0xB24/+0xB28; 3 and 4 only with `Pawn.CanUse4Weapons()` = config
  `bUse4Weapons`, not console, not MP); when both are full it replaces one (the current weapon by preference),
  drops it as a pickup (`eventSpawnPickupForWeapon`, only if its ammo is non-zero) and destroys it. A weapon the
  player **already has** is refused only when its ammo is full; otherwise taking it tops up the ammo.
- **Item caps:** pipe bombs, trip mines, beer, steroids and HoloDuke are `Weapon` classes with their own ammo class;
  the cap is that ammo's `MaxCharge` (defaults: `PipeBombAmmo` 4, `TripMineAmmo` 4, `FortyOunceBeerAmmo` 1,
  `SteroidsAmmo` 1, `HolodukeAmmo` 1) and the refusal is the Weapon rule above. HoloDuke additionally has
  `MultiplePickupBehavior = 2` on the item itself (`Charge 1 / MaxCharge 1`). No script writes `MaxCharge`; read
  the live values, not these defaults.

**The ESP test:** call `Inventory.CanPickupClass(InvClass, PlayerPawn)` through ProcessEvent (static, no
`this` use in `execCanPickupClass` `0x100D00E0`; parms: class, pawn, return bool) once per distinct `InventoryType`
per refresh (4 Hz is plenty), and cache the answer by class. It is the game's exact rule and writes nothing. The
pure-read alternative is to walk `InventoryListHead` and apply the three rules above by reflection.

## 2. Weapon caches and ammo crates

| class (dnDecorations) | behaviour | "empty" | SP placed |
|---|---|---|---|
| `Containers_Crates_ItemCrate` (`< Containers_Crates < Containers_Generic < aFinalDecoration < dnDecoration`) | `Health 1`, `HealthPrefab = HEALTH_UseHealthVar`; on destruction `dnDecoration.InsertSpawnInventoryItems` adds the instance's `SpawnedInventory` (array of pickup classes) to `SpawnOnDestroyed`; Duke says "GotAmmo" (`TakeDamage`) | destroyed / `DecoState != 0`; contents are then real pickups | 57: pipe bombs 49, trip mines 29, steroids 8, beer 6, HoloDuke 6, pistol 1, shotgun 1 (Map05 x4, 07B x6, 14B x5, 18 x5, ...) |
| `Containers_Crates_InfiniteAmmo` | `Used` opens it (`CrateState`: 0 Closed, 1 Opening, 2 Open, 3 Closing); `FullyOpened` -> `Pawn.InfiniteAmmoCrateUsed` = `GiveFullAmmo` for each `PrimaryWeapon1..4` | never; returns false only when every held weapon is full | 23 (e.g. Map05 x4, 04C x3, 15 x3) |
| `Containers_Pile_InfiniteAmmo` ("Get Ammo") | `Used` -> `InfiniteAmmoCrateUsed` | never | counted with the crates above |

- "Useful now" for an infinite crate = some held primary weapon has `Ammo.Charge < Ammo.MaxCharge` (what
  `GiveFullAmmo` -> `AttemptPickup(AmmoClass, ..., PICKUP_Use)` tests).
- Plain breakable crates (`Containers_Crates_Destructible*`, 357 placed) and boards/vases/pumpkins are cosmetic
  unless an instance sets `SpawnedInventory` (none does in SP: every crate with contents is an `ItemCrate`).

## 3. Explodables

Rule: a `dnDecoration` explodes when its `DestroyedActivities` (class defaults or instance) holds a
`DecoActivities_RadiusDamage` element (`DA_RadiusDamage_Standard`, `_FireExt`, `_Propanetank`,
`_Boulder_Destroyed`); a few classes call `HurtRadius` in script instead.

| class (dnDecorations unless noted) | trigger | SP placed |
|---|---|---|
| `Barrels_Generic_Explosive01`, `_NoGibs`, `_C9`, `_FireExt`, `Barrels_Generic_WoodenBarrel_Explosive` (`< Barrels_Generic_Unstable`) | first hit starts a fuse (`DelayedExplosionTime` 3-3.5 s +- variance, `bExplodeOnNextHit = true`, fire); next hit, a throw or the timer -> `CriticalDamage` | 33 + 35 + 9 + 5 + 8 |
| `Cannister_Generic_Red` | DA_RadiusDamage_Standard | 18 |
| `Industrial_Generic_Propanetank` | DA_RadiusDamage_Propanetank | 6 |
| `Nature_Generic_Boulder_Destructible` | DA_RadiusDamage_Boulder_Destroyed | 9 |
| `Lights_Generic_Lantern` | `Destroyed` -> `HurtRadius(DamageOnExplode)` | 24 |
| `Vehicles_Deco_Car_*` (`MazbaA`, `MazbaB`, `Police`, `Taxi`; `< Vehicles_Deco_Car_Standard_Base`) | `Health <= IgnitionHealthThreshhold` -> burning (`bIgnited`), explodes after `ExplosionDelay`; one hit `>= SingleHitDamageThreshhold` -> at once; `Explode` -> `HurtRadius` | 50 |
| `Biology_Generic_GasPod` (hive) | pops when a pawn is near (`DoDamageRadius`); `bBroken` once popped | 188 |
| `dnDestructibleBuilding` (dnGame, `< RenderActor`, not a dnDecoration) | `TakeDamage`: explosive damage `>= ExplosiveDamageRequired` with `bEnableExplosiveDestruction` (or vehicles) -> `PerformExplosiveDestruction` | 137 (Map13 x68) |

Not placed in SP: `Cannister_Generic_Green`, `Barrels_Generic_Explosive_Rolling`, `Biology_Generic_ExplosivePod`.
Breakable walls: `Pillars_Destructible_*` (concrete/marble, 314) and `Settings_LadyKiller_RetainingWall` (19) break
into chunks under damage/explosions (`HurtRadiusActivity`) but do not explode; destructible boards, vents, vases,
pumpkins, crates and glass are cosmetic and excluded.

**Fields** (dnGame `dnDecoration` / Engine `RenderActor`, by reflection):
- **Alive or exploded:** `DecoState` (`EDecoState`: 0 Alive, 1 MostlyDead, 2 ReallyDead;
  `DecorationIsDead()` = 1 or 2); also gone when destroyed. `bStartupDestroyed` instances start dead (2 in SP, both breakable walls/vents).
  Buildings: `Parts.Num() > 0` (emptied on destruction); gas pods: `bBroken`.
- **Can be damaged now:** `!bNoDamage` and `HealthPrefab != HEALTH_NeverBreak (0)` (`EHealthPrefab`: NeverBreak,
  Easy, Medium, SortaHard, Hard, UseHealthVar); barrels also `!bDamageDisabled`; `DamageThreshold`,
  `DamageTypesAcceptedOnly`/`DamageTypesIgnored` filter by type (`dnDecoration.TakeDamage`). Health: `Health` /
  `HealthCap` (barrels 40). 11 SP explodables are `bNoDamage` and 4 are `bHidden`: hide both.
- **About to blow (worth a colour):** barrels `bExplodeOnNextHit`, cars `bIgnited`.

### Instances per map (placed; instance properties read from the map files)

| map | objects (exports) | pickups | item crates | infinite ammo | explodables |
|---|---|---|---|---|---|
| Map00 | 10,019 | 1 (Devastator) | 0 | 0 | 0 |
| Map01 | 12,228 | 0 | 0 | 0 | 0 |
| Map05 | 5,711 | 7 (MachineGun, 2 pipe bombs, 3 RPG, Shotgun) | 4 (steroids; 2+2 pipe bombs; beer + 2 trip mines) | 4 | 17 (6 barrels, 11 cars, 3 of them `bNoDamage`) |
| Map09 | 6,229 | 0 | 0 | 0 | 0 |
| largest | | Map20 34, Map13 33, Map11B 30 | Map07B 6 | 4 | Map13 78 (68 buildings), Map14 55, Map07B 51 (50 gas pods) |

All 40 SP maps: placed pickups 0-34 per map (449 total), explodables 0-78 (522 total). A 128-entry cache covers
any level with room for drops.

## 4. Cost and mid-level spawns

- **Scan cost (measured):** `egoesp: level serial 2: 13 giver(s) in 71704 objects, 0.63 ms` (Map00,
  `System\NukePanel\nukepanel.log`); the main menu table is 54,063 objects (0.46 ms). One full scan for pickups and
  explodables (a class-rule lookup per object, as egoesp does) costs the same order.
- **Spawned mid-level (not in the map file):**
  - Enemy weapon drops: `Pawn.DiedActivity` -> `SpawnPickupForWeapon` -> `GearboxEngineGlobals.
    RequestDeferredWeaponPickup` (native `0x10364F00` queues a 0x34-byte request; the pickup appears on a later
    tick) with `AmmoCharge` = the dead pawn's ammo; AI skip it when `bDisableWeaponDrops` or when the player and
    the AI differ in shrunk state (`AIActor.ShouldSpawnPickupForWeapon`). `AIActor.DropWeapon`/`DropCarriedWeapon`
    take the same path.
  - Pig Cop grunts: `PigCop_Grunt.DiedActivity` spawns `PipeBombDrops` x `PipeBombPickup` at `PipeBombDropChance`
    each (when `NumPipeBombs > 0`).
  - Item crates: `SpawnedInventory` on destruction (above). Corpse pinatas: `dnCorpsePinata.DestroyBoneWebbing`
    spawns up to 3 `PickupTypes`.
  - Duke's own swap drop (`APlayerPawn::AddInventory`, section 1).
  - No script spawns an `AmmoPickup`: dropped ammo travels inside a weapon pickup's `AmmoCharge`.
- **Consequence:** the egoesp pattern (cache + per-frame slot validation) fits, but a scan once per level misses every
  drop. Rescan the whole table on a timer (2-4 Hz: measured about 1.7 ms each, about 1 % of frame time at 60 fps) and on
  level change; keep the per-frame validation for removals. Freed object slots are reused, so scanning only new
  indices is not safe. Explodables are never spawned mid-level (only destroyed); they could stay per-level, but
  one shared rescan is simpler.

## 5. Breakable paths

DNF single player has **no Duke3D-style
blow-open secret walls**. No breakable wall, pillar, vent, board or building opens a secret area: there is no
`SecretArea` in any SP map (progress.md). What exists is smaller:
- **3 sure optional stashes** behind a breakable, plus 2 probable ones.
- **About 12 scripted breakables on the main route**: shot padlocks on fence gates, barrels that blow doors open,
  boards over a tunnel.
- **Hundreds of physical-only breakables** that fire nothing and open nothing scripted.

The ESP row is **"Breakables"**: it marks padlocks and every damageable actor whose `Event` is set, and gives
the known stashes their own colour. Physical-only breakables never show (breakables.h).

Method: every damageable SP map actor (a `dnDecoration` with `HealthPrefab != 0` and `!bNoDamage`, plus glass and
`dnDestructibleBuilding`) whose instance `Event` is set: 338 actors, 242 distinct events. The chain was followed
through the `Tag`s of Dispatcher, Counter, TriggerRelay, TriggerAssign, TriggerDestroy, KeyframeDispatch and
TriggerKarmaState, up to four levels. Mover/brush targets did not parse; for those the event name is the evidence.
Destruction fires `Event` from `InteractiveActor.Destroyed` -> `GlobalTrigger(Event)` (Engine, decoded). Destroyed
decorations also run `DestroyedActivities` and `DisableNavDoor` (`dnDecoration.BringOutYerDead`).

"Stash" is optional loot only; a breakable whose Event opens the route is "Path: <what it opens>" (same breakable colour).
Both labels are verified in game. Two findings (The Hive and Map14B; map packages read, nukepanel.log):
- **Identity is the Event name, not the map name, which a checkpoint load changes.** The log shows `level package
  'cp03', MapFileName 'Map07B'`: a checkpoint loads from `cpNN`, so a (map, instance) match failed after every reload
  and the pod fell back to its class label "Biology GasPod". Rows key on the **Event name** (`BK_STASHES.event`),
  authored and unique across the SP maps (each Event below occurs on one map only; the two Map11C crates and the two
  Map11 boards share theirs). Instance names are not used.
- **Map07B `Biology_Generic_GasPod___1`** sits in a cluster of 5 hub pods (`___0..4`, `Hive_Hub_Generic`, near
  (-448, 452, -6592)); only it carries `Floor5_Jumppad1_Open_Relay` -> `Floor5_Jumppad1_Open` (group `Hive_Hub_Extra`),
  the pad `Floor5_Jumppad1` -> target (-503, 654, -6676), the hub's way up to floor 5 (pods `Hive_Floor5_Generic` y 600
  to 1070). Route: **Path**.
- **Map14B `dnDestructibleBuilding___51`** (-64872, -31554, 1554), `TriggerKarmaState_RailgunPickup_Mining_02`: wakes a
  Railgun and a Shotgun pickup and destroys `SM_InvisibleCollisionHull_Mining_Shack` (group `84_Mining_Trussel`), a hull
  that blocks the space; the Monster Truck (`Veh_MonsterTruck___0`) is on this map. It is the only building the old
  table named, so it is the house meant. The data shows loot AND a removed blocker; it is played as the way on, so it
  is a **Path ("railgun shack")**. The data alone does not settle which it is.
- Map11 boards (`Counter_BathroomCloset` -> `DoorMoverEx_Rotating_Bathroom_Closet_Unlock`, intro hallway): what sits
  inside is not known. Map21 chest and Map11C steroids: sure stashes.

### Optional (stash, easter egg)

| map | breakable | what it opens |
|---|---|---|
| Map21 | `Details_Padlock_Base___0` (group `02e_DamExterior_ChestSecret`) | **Secret treasure chest**. `TreasureChest_Dispatcher`: lid keyframe, bubbles, `TriggerKarmaState` wakes `TreasureChest_RewardItem` = 2 `PipeBombPickup` + `FortyOunceBeerPickup` |
| Map11C | a `Containers_Crates_Destructible` + an `ItemCrate` | `SecretSteroids_RoundRobin`/`_Momentum`: physics on a stashed `SteroidsPickup` and its crate (knocks the steroids down) |
| Map11 (probable) | 2 `Details_Generic_DestructibleWoodenBoard_A` (`NEW_Intro_Hallway`) | `Counter_BathroomCloset` -> `DoorMoverEx_Rotating_Bathroom_Closet_Unlock`; nearest goods: `Details_Generic_PornMag_A` (ego 14) 141 u, a pipe bomb 324 u. What sits inside is not known |

### Mandatory or scripted (route forward, puzzles)

| map | breakable | what it opens |
|---|---|---|
| Map07B | `Biology_Generic_GasPod___1` (one of 5 hub pods) | `Floor5_Jumppad1_Open_Relay`: the jump pad up to floor 5. Labelled "Path: jump pad" |
| Map14B | `dnDestructibleBuilding___51` (mining shack, `81_Mining_Details`) | `TriggerKarmaState_RailgunPickup_Mining_02`: Railgun + Shotgun pickups inside, and the shack's invisible collision hull removed. Labelled "Path: railgun shack" (loot or route not settled from data) |
| Map06 | `Details_Padlock_Base___1` | post-Wild-Ride fence doors (`TriggerAssignDoorMoverEx` x2, collision destroyed, checkpoint) |
| Map12B, Map16, Map18 x2 | `Details_Padlock_Base` | fence and bridge doors (`Dis_BridgeDoor01`, `ViewRoomFenceDoor1`, `InfoRoomFenceDoor1`). Main route or side room: not settled from data |
| Map08 | `Barrels_Generic_Explosive01___1` (gym) | `Blow_Up_Doors_Dispatcher`: gym exit doors blown off (fake doors destroyed, physics launch) |
| Map06B | 3 explosive barrels | `CraneAreaCargoContainerDoorBlowDispatcher`: cargo container doors unmount and swing open |
| Map06B | 2 explosive barrels + a crate | `ConstructionPitCargoContainer1Counter` (3 of 3, `ConstructionPitPuzzle3`): a container drops (physics puzzle) |
| Map06 | `Barrels_Generic_Explosive_FireExt___0` | trench-fire puzzle: the fire goes out, its blocking collision is destroyed |
| Map21B | 2 `Cannister_Generic_Red` | `blowUpBotStairFloor`: the floor grate at the stairs is destroyed and an open grate is shown |
| Map14, Map14B | `Details_Generic_DestructibleWoodenBoard_C` x3 per set | mine-tunnel entrance boards (`Trigger_Duke_Crowbar_Disable_*`: the crowbar hint stops once they are broken) |
| Map16B | `Vents_Generic_BreakableGrate_B___8` | `BigMachineRoom_FloorVent` (group `07b_Vent_Exit`) |
| Map13 | 3 `Barrels_Generic_WoodenBarrel` (not explosive) | `Trailer_Door1_Unlock`, `Trailer_Door2_Counter` -> `Trailer_Door2_Unlock` (the doors are brushes, not resolved) |
| Map16 | 2 `Cannister_Generic_Red` | `EntranceHubCannisterDispatcher`: destroys 2 patch actors (`EntranceHubCannisterStuff`) |
| Map13, Map14 | `dnDestructibleBuilding` | vehicle/explosive set pieces in Ghost Town and the mine: destroy bridge collision, `NavVolume_Door`s, water tower. Monster-truck route |

### Physical-only breakables (no event; nothing scripted behind them)

Placed in SP:

| class | count |
|---|---|
| `Pillars_Destructible_*` | 312 |
| `dnDestructibleBuilding` | 137 (mostly Map13/14 set pieces) |
| `Details_Generic_Barricade_Metal_A` | 73 (2 Map17B sets only start AI scripts) |
| `Details_Generic_DestructibleWoodenBoard_*` | 53 |
| `Industrial_Generic_WoodPallet_A` | 45 |
| `Vents_Generic_BreakableGrate_*` | 38. Events only re-assign the grate itself (Map02, Map03, Map04C), except Map03's `TriggerDestroy_Porn_Vent` |
| `Settings_LadyKiller_RetainingWall` | 19 (events are AI action points) |
| `Biology_Generic_HiveWebbing_*` | 15 |
| `Details_Padlock_*` | 7, all listed above |

A vent grate or board can still open a crawl space by collision alone. Nothing marks such a space as a secret,
and proximity to pickups (384 u) found only ordinary room ammo.

### Barrels and canisters beyond combat

Yes, in a few places:
- **Opening a path:** Map08 gym doors, Map06B cargo container doors and the pit puzzle, Map21B stair floor,
  Map06 fire extinguisher.
- **A set piece:** Map16 canisters, Map20 propane tank (`12CraneArea_TankExplosion` -> `TriggerExplosion`).
- **Map13 trailer doors**, opened by plain wooden barrels.

No barrel guards a stash. The ego and secret stashes above are opened by a padlock, a crate or a shack, not a barrel.

### Crates in Breakables

A `Containers_Crates_*` actor is marked in Breakables only when it holds items (`SpawnedInventory` or
`SpawnedSnacks` non-empty) and is not an `ItemCrate` (the Items row already names those by contents). An Event alone does
not mark a crate. A known stash crate stays marked ("Stash: ..."): the stash leads to a pickup. In SP the only
ones are Map11C's two `SecretSteroids_*` `Destructible` crates; no other non-ItemCrate crate holds items.
Rule: `bk_crate_class`, `bk_crate_shown` (breakables.h); the log's breakables line ends `crates hidden: empty N,
item crates N`.

Survey (map packages read with a scratch tagged-property reader, 38 SP maps; `ubc.py` for script):
- Pickups reach the world on destruction only through `dnDecoration.InsertSpawnInventoryItems` (`SpawnedInventory`)
  and `InsertSpawnSnackItems` (`SpawnedSnacks`), both copied into `RenderActor.SpawnOnDestroyed`. Of 551 placed crates,
  only the 52 readable `ItemCrate` instances set `SpawnedInventory` (14 instances did not parse, 12 of them
  ItemCrate or Area51; two Map20 `Destructible` stop at `SheerRate`, with only `DestroyableActionPoints` before it).
  No instance sets `SpawnOnDestroyed`, `SpawnedSnacks` or `DestructionLevels`.
- Class defaults: `Destructible`, `Destructible_B` and `ItemCrate` default `SpawnOnDestroyed` to gib classes
  (`Containers_Crates_*_Gib_*`, a motion prefab); no `*Pickup` or inventory class is referenced in any crate class.
- Event crates: 19 `Containers_Crates_Destructible` (Map17B 7 `Garage02_FloorAP_Set*_KILL` / `Garage02_KillFootAP02`
  (AI action points), Map08 8 `VIPLizzy3AP1DestroyA`, Map06B 1 `ConstructionPitCargoContainer1Counter`, Map11C 2
  `SecretSteroids_*`, Map20 1 `12AssCommCrates`) and 8 `InfiniteAmmo`. These are what showed before the rule.
- A marked crate in a pile of identical `Destructible` crates is the `eventSet` input of `bk_purposeful`, not a stash
  (stashes are Map11C only). The log was on Map17B, where seven crates carry a
  `Garage02_*_KILL` Event that only re-assigns AI action points; the instance shown carries one of them.

## Open questions

- How the grabbable quest pickups (battery, gas can, energy cell) turn into inventory was not read.
- `APickup::AttemptPickup` `0x100CF7C0` requires two flag bits at `Pickup+0x97C` (bits 0 and 1) before anything
  else; which bools they are was not mapped (resolve `bAllowTouchPickup`/`bInitialized` by reflection instead).
- Whether difficulty changes ammo `MaxCharge` natively was not checked; read live values.
- Whether the Map11 intro-hallway boards open anything worth marking, and which of loot or route the Map14B shack is.

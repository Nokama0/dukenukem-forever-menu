# Aircraft: troop dropships, gunships, wasps, fighters (ESP and aim research, build 244)

None of the aircraft is a pawn. All are `dnDecoration` actors (`dnDecoration < Decoration <
InteractiveActor < RenderActor < KarmaActor < Actor`), so `Level.PawnList` never holds them. They have Health (1200
for the alien dropship and gunship, 150 for the drone fighter), die at Health <= 0 and can be killed, **but the
alien dropship and gunship accept only `ExplosionDamage` (rockets, Devastator, pipe bombs, mines) and
`LargeCaliberBulletDamage` (the MG42 turret): every ordinary bullet, laser or railgun hit is returned before it
touches Health** (`DamageTypesAcceptedOnly`). That is why the aim log shows `dnAlienDropship` as a blocker the bullet
"passes". The panel can find them with a class scan (worldscan.cpp pattern, periodic rescan), box them with
`GetRenderBoundingBox`, and aim only while the held or mounted weapon can hurt them.

Method: read from the script packages, the map packages and StaticMeshes.dat (section 7 notes what the panel
measured at runtime). `tools/ubc.py` over `extracted-packages/*.u` (functions decoded and grepped, class defaults by a scratch
tagged-property reader), `Maps\*.dnf` read in place (export class counts, instance properties), `StaticMeshes.dat`
bounds. No native code was needed (everything is script, or already in modelbox.md / spawns.md), so no Ghidra.

## 1. Classes and where they are

| class (pkg) | role | `bEnemy` | placed instances per map (export class counts) |
|---|---|---|---|
| `dnAlienDropship` (dnAI) < `dnAlienDropship_Base` < dnDecoration | carries and drops troops (`SpawnInfo[]` riders, spawns.md 2a); **no weapon** | True | DM-VegasRuins 2, Map01 1, Map03 4, Map04B 4, Map05 6, Map06 3, Map10 4, Map11C 6 (3 are hidden dummies), Map12 3, Map12B 3, Map13 3, Map14 1, Map14B 1, Map22 3 |
| `dnAlienGunship` (dnAI) < `dnGunship_Base` < dnDecoration | the attack ship: guns aimed by `AimInfo[]`, `BulletDamage` traces and `dnRocket_Gunship` | True | Map05 2, Map06B 2, Map10 1, Map12B 3, Map14 4, Map14B 1, Map15 2 |
| `dnEDFWasp`, `dnEDFWasp_Low` (dnAI) < `dnGunship_Base` | **friendly** EDF wasp (rocket pods, truck drop-off and pick-up) | **False** | Map00 1, Map05 2, Map06 2, Map06B 2, Map08 1, Map10 1, Map11C 1, Map12 2, Map15 1, Map22 6 |
| `dnFlybyPlane_Fighter` (dnGame) < `dnFlybyPlane` < `dnDecoration_Characters` < dnDecoration | alien drone fighter on a spline; shoots Duke (`Enemy` = the player) | not set | Map05 1 placed; more from `TriggerSpawnFlybyPlane_Fighter` (Map03 and Map10 import the class with no placed instance) |
| `dnMotherShip`, `dnDumbMotherShip`, `dnMothershipGun` (dnGame) | scenery; the gun is `HealthPrefab` 0 (NeverBreak), collision 16384 | gun True | Map03 1 ship |

Not targets: `Flyer` (a `BaseAI` pawn, already in the pawn list), `dnFlybyPlane_Low`, jetpack troopers (pawns),
`dnBomb_EDFWasp`, the gib classes.

- **Pawns?** No. The riders (Pig Cops, Assault Troopers) are pawns and already listed; the ships are not.
- **Placed or spawned?** Dropships, gunships and wasps are placed in the map (counts above are export-table
  counts) and sit `bHidden` / powered off until a script event runs (`PowerOn`/`PowerOff` tags, `SpawnAI`,
  `ReleaseAI0..3`, interpolation). Fighters are placed or spawned by `TriggerSpawnFlybyPlane_Fighter`.
- Map05 (the MG42 encounter) holds 6 dropships, 2 gunships, 2 wasps and 1 fighter. The MG42
  (`dnControl_Turret_MG42`, turrets.md) fires `LargeCaliberBulletDamage`, a type the ships accept.

## 2. Damage

Chain: `dnAlienDropship/dnAlienGunship.TakeDamage` -> `dnDecoration.TakeDamage` -> `InteractiveActor.TakeDamage` ->
`RenderActor.TakeDamage`.

- `dnDecoration.TakeDamage` returns before Health moves if: `Damage < 1` and the instigator is not the player;
  `DamageIgnoreRules` match; the type is not a child of any `DamageTypesAcceptedOnly` entry (when the list is
  non-empty); the type is in `DamageTypesIgnored`; `bNoDamage`; or the frame's damage is under `DamageThreshold`.
  `DamageThrottle` caps one hit. Then Health drops; `Health <= 0` or a `DamageTypesInstaKill` type calls
  `CriticalDamage()` -> `Destroyed()`.
- Class defaults (dnAI.u): `dnAlienDropship` and `dnGunship_Base`: `HealthPrefab` 5 (`HEALTH_UseHealthVar`; 0 is
  `HEALTH_NeverBreak`), Health = HealthCap = **1200**, `DamageTypesAcceptedOnly` = [`Engine.ExplosionDamage`,
  `dnGame.LargeCaliberBulletDamage`], `bEnemy` True, `DamageThreshold1/2` 800 / 400 (smoke and fire effects only).
  `dnEDFWasp_Low` Health 500. `dnFlybyPlane_Fighter`: Health = HealthCap **150**, no accepted-only list (any type
  works), but its `TakeDamage` returns while `bNoDamage` or `!bIsVulnerable`.
- **Weapons that hurt the 1200-HP ships:** `ExplosionDamage` children `RocketDamage` (RPG), `DevastatorDamage`,
  `PipeBombDamage`, `LaserMineDamage`, `NukeDamage`, `GrenadeDamage`, `CannonDamage`, `SuperTurretDamage`; and
  `LargeCaliberBulletDamage` (referenced by the MG42 turret, bosses and the gunship's own guns). **Pistol, Ripper,
  Shotgun, Railgun, AT laser (the `BulletDamage` subtree) do nothing.** Whether player splash reaches a ship through
  `HurtRadius` (`HurtRadiusAffectClass`) was not read.
- **Destroyed:** `DecoState` 0 (`DECOSTATE_Alive`) -> 2 (`DECOSTATE_ReallyDead`); `DecorationIsDead` is state 1 or
  2. The dropship's `Destroyed` calls `TriggerFunc_ReleaseAllAI` (riders fall) and `HurtRadius(350, radius 800)`; a
  player kill with `DamageTaken >= HealthCap` bumps achievement counter 22. The actor is then destroyed or hidden
  shortly after.
- **Invulnerable cases (instance data in the map packages):** Map08 `dnEDFWasp___0` and Map12's two wasps carry
  `bNoDamage=True` (Map12 also `HealthPrefab` 0); Map11C's three `PC_Dropship*_Dummy` ships are `HealthPrefab` 0,
  Health 9999, `bHidden`. Map06B `dnAlienGunship___1` and Map10 `dnAlienGunship___3` override `DamageThreshold1/2`
  (8800/8400 and 3800/2800), which implies a Health far above 1200 (the Health tag itself was not read: unmeasured).
  The class code has no scripted invulnerable phase; only `bNoDamage` (events can set it) and `HealthPrefab` 0.
  `dnDecoration.ResetHealthProperties` has no difficulty term: a switch on `HealthPrefab` only sets `HealthCap`
  (1: 1, 2: 12, 3: 45, 4: 100, 5: current Health) and `HealthMin` (0: 1, else 0). Read live Health anyway. Also
  `dnDecoration.NotDeadYet`, run from `Destroyed`, sets `DecoState` 1 and `bNoDamage` for the dying ship.

## 3. Hostility

- Class flag: `bEnemy` True on the alien dropship and gunship, False on the EDF wasp. Key on the class chain
  (`dnAlienDropship`, `dnAlienGunship`, `dnFlybyPlane_Fighter` = enemy; anything under `dnEDFWasp` = friendly, never
  mark as an enemy) or read `bEnemy` by name.
- The gunship fires on the player when `bFiringEnabled` (`bStartFireEnabled`; `HoldFire`/`ResumeFire` tags toggle
  it) and an `AimInfo[i].TargetActor` exists: `InitialTargetTag` defaults to `DukePlayer` (`SearchTarget` finds that
  tagged actor); an active HoloDuke replaces the target (`GetTargetActor`). "Shooting at the player now" = a gun's
  `TargetActor` is the player pawn. The dropship has no gun: it is hostile only through its riders.
- The fighter shoots `dnDecoration_Characters.Enemy` (the tagged target, else the local player).
- Wasps default to `InitialTargetTag` DukePlayer too, but Map10 and Map12 instances override it (`JunkTag001`,
  `None`) and they fire rockets only on events; keep them friendly by class.

## 4. Geometry

- Collision cylinder (class defaults): dropship and gunship radius 500, height 150; wasp 350 / 150; fighter 260 / 95.
  `bCollisionAssumeValid` and `bAcceptMeshAccurateMoveActorTrace` are True on the dropship and wasp (traces test the
  mesh).
- The three ships have a skeletal `Mesh` (DrawType 2): `c_vehicles.Alien_TroopCarrier`, `c_vehicles.Alien_Gunship`,
  `c_vehicles.edf_wasp`. Their bounds come from `GetRenderBoundingBox` through the mesh instance (modelbox.md;
  spawnbox.cpp already calls that native for pawns). The same-named static meshes in StaticMeshes.dat (local box,
  not the skin mesh): Alien_TroopCarrier and Alien_Gunship each 778.6 x 991.8 x 320 UU (x from -330 to +448);
  EDF_WaspCarrier 325 x 169 x 138. The fighter uses `StaticMesh` `Aircraft.AlienDroneShip` (DrawType 8),
  425 x 456 x 186. The cylinder under-covers a dropship (about 1000 UU long), so box the render bounds and keep the
  cylinder as the fallback.
- No hit zones: `BonePartMapper` zones exist for pawns only (aim.md), and `HitBoneName` carries no damage scale on
  these decorations. Aim at the render-box centre, not a bone.

## 5. Finding them cheaply

The scan is one `WsClient` to worldscan.cpp (`class_rule` on the class-name chain as breakables.h does,
`row_of` per object of this level), so the object table is still walked once per pass (0.63 ms on Map00's 71,704
objects). Rows: hostile ships (alien dropship, gunship, fighter) and friendly (wasps, only if the allies row wants
them). `oncePerLevel = 0` because fighters spawn mid-level, so it rescans every `WS_RESCAN_MS` (333 ms), and the
cache is pruned each frame against the snapshot. Per ship per frame read only `bHidden`, `DecoState`, `Health`,
`bNoDamage` and `Location` (validate the array once per frame; a guarded read costs ~12 us). Map05 has the most
(11), `WS_MAX` 128 is ample. Alive = `!bHidden && DecoState == 0 && Health > 0 && !bNoDamage`.

## 6. Aim and ESP rules

- ESP: draw alive hostile ships as enemies with their own label (Dropship, Gunship, Fighter), box from the render
  bounds, health from Health/HealthCap. Wasps only in the allies row, never as enemies. Hidden or dead never show.
- Aim and Triggerbot, only when no normal enemy is a candidate:
  1. `pick_target` (aim.cpp) fills `cand[]` with `AimCand{idx}` indexing the `WPawn` array, and `ally_between`,
     `cand_vis`, `probe`, `cone_ok`, `point_of`, `log_lost` all take that index. Ships are a second list (a small
     ship array from the scan, same `aim_cand_key` ordering) run only when the pawn pass picked nothing
     (`at < 0`); a ship target drops the frame a pawn is picked.
  2. Weapon gate: a ship is a candidate only if the held weapon's damage is an `ExplosionDamage` child (RPG,
     Devastator, pipe bomb) or the mounted gun is the MG42 / grenade / Super turret (`s_mt`); never for pistol,
     Ripper, shotgun, railgun, lasers. The existing `w_kind()` projectile lead serves rockets.
  3. Aim point: render-box centre (no head bone); visibility with the ship as the trace target (`hit_clear`);
     skip ships with `bNoDamage`, hidden, or Health far above the weapon's reach (read live).
  4. `aim_target()` and `s_target` are pawns today. tbot.cpp re-finds the locked
     target in `world_pawns()` by index, so Locked and Aimbot modes need the ship path or must ignore a ship
     target. `spawnbox_excluded` is pawn-only; ships need their own exclusion.
  5. `aim_cone_prefilter` takes radius and height: pass the render-box extents, not the cylinder.

## 7. What the panel does

- **Finding:** `aircraft.cpp` is a fourth worldscan client (`g_aircraftScan`, rescanned every
  `WS_RESCAN_MS`) on the class chain `dnAlienDropship`, `dnAlienGunship`, `dnFlybyPlane_Fighter`; `dnEDFWasp` and
  `dnEDFWasp_Low` match only to be dropped. It runs while the Enemies row or any targeting feature is on. Each frame
  every cached ship's `bEnemy`, `bHidden`, `bDeleteMe`, `DecoState`, `Health`, `HealthCap`, `bNoDamage`,
  `HealthPrefab`, `bIsVulnerable` and `Location` are read under one guard.
- **Rules (`aircraft.h`, tests 0427-0432):** hostile and live = enemy, not hidden, not `bDeleteMe`, `DecoState` < 1,
  Health > 0. Enemy = `bEnemy` for the dropship and gunship; **the fighter by class**, because its defaults do not
  set `bEnemy` (section 1), **and only while it can be damaged** (`bNoDamage` 0, `bIsVulnerable` not 0; -1 unread
  counts as vulnerable). A checkpoint-load log showed a "Fighter" with `bNoDamage 1`, `bIsVulnerable 1`: scenery,
  logged "not shown", never drawn or targeted. Damageable =
  not `bNoDamage`, `HealthPrefab` not 0, and for the fighter `bIsVulnerable`. Hurts aircraft = RPG, Devastator, pipe
  bomb configs, or any mounted turret (MG42, grenade launcher, Super turret).
- **Box:** `RenderActor.GetRenderBoundingBox` through scall for each hostile live ship (at most 8 calls a frame), kept
  as an offset from Location; the collision cylinder when the box is unusable.
- **ESP:** no row of its own (ships are enemies): drawn whenever the Enemies row is on, enemy red,
  the class label, a health bar from Health over the cap (the live `HealthCap` while it holds the most seen Health,
  else the most seen), the distance. Non-damageable dropships and gunships draw too. The old `esp.aircraft` ini key is
  ignored.
- **Aim:** `pick_target` runs a ship pass only when the pawn pass picked nothing and no pawn's visibility is still
  unknown, with a weapon that hurts them. Aim point and cone at the render-box centre (Location read afresh), one
  bullet-flag `TraceEx` to it for visibility (the ship, an attachment or nothing is clear), range, screen and turret
  limits as pawns; lead from `Velocity`. `aim_track` drops a ship the frame the scan stops holding it hostile and live.
  The Triggerbot's Crosshair accepts a look hit that is a shootable ship (`aim_ship_shootable`); the spawnbox exclusions and Auto-execute stay pawn-only (`aim_is_ship`).
- **Log lines:** `aircraft: <class> (<label>): Health, HealthCap, DecoState, bEnemy, bHidden, bNoDamage,
  HealthPrefab, bIsVulnerable -> hostile and live | not shown` once per ship a level; `aircraft: <class> render box
  X x Y x Z, centre ... off Location; cylinder r h` once per ship; `aircraft: frames=...` every 600 frames; the
  worldscan line's `aircraft: Aircraft N, hostile H (damageable D), drawn K`; the aim 600-frame line's `aircraft
  target N frame(s), skipped for the weapon M`; `aim: target <class> (aircraft, no pawn picked)`; the tbot line's
  `a ship N`; `aim: weapon ...; hurts aircraft yes|no`.
- **Target keeping:** a previous ship target is kept from the visibility cache alone while the pawn walk
  stops on an unknown pawn (`ac_ship_pass_mode`, test 0433); the ship pass counts its own rejections in the aim line.
- **Ship target ping-pong (Map10 cp02, MG42):** two fighters near the crosshair would swap every frame
  (`aim: target ... 3 candidate(s)` then `1 candidate(s)`), because the nearer fighter sat on the gun's pitch limit and
  passed the strict limit test only every other frame (the camera and RotatePitch disagree by the turn in flight),
  and the cache-only pass could take any cached-visible ship. So the current target ship keeps through the limits
  with its hull's angular half extent as slack (`ac_limit_slack`, at most 2 degrees), other ships still need the strict
  test and aim_pick's margin, and the cache-only pass admits the current target alone (`ac_ship_admit`, test 0446).
- **Known gaps:** `s_seen` (64 entries) is never pruned within a level, so mid-level fighters
  (`TriggerSpawnFlybyPlane_Fighter`, Map03 and Map10) past 64 get the cylinder, no cap tracking and no log line, and a
  reused pointer inherits an old entry; dropping entries that left `s_row` at each rescan fixes both (m2). The
  Triggerbot's `crosshair_fallback` (look trace unresolved) never takes a ship, and a look hit on a ship's attachment
  counts as "another actor" (m4). `EnforcerGunWeaponConfig` fires `dnRocket_RPG_HeatSeek` and is left out of the
  weapon table until that rocket's damage type is read (m5).
## Open questions

- Whether player splash reaches a ship through `HurtRadius` (`HurtRadiusAffectClass`).
- The Health of the Map06B and Map10 gunships that override `DamageThreshold1/2` (the Health tag was not read).
- The fighter's live `bEnemy` and `bIsVulnerable`; whether Health is a float or an int on these
  classes (both are read); the render box of a skeletal ship; whether a trace to the box centre hits the hull;
  whether `Velocity` moves on an interpolating ship.

# Enemies that drop in from above, and the ESP box (build 244)

Every scripted drop-in path in the shipped script moves the pawn's own `Location` with its Meqon physics controller:
the dropship's riders are mounted on the ship and then fall or rappel for real, egg-pod Pregnators are launched as
`PHYS_Falling`, and jetpack troopers fly and land. A box drawn at `Location` rides down with them, except when the
visible body is **away from `Location`**: an animation whose root displacement the actor did not take (AnimX,
cut-scene or sync playback). A test log shows that case (section 4). The panel follows the body with the engine's own
render bounds (`ARenderActor::GetRenderBoundingBox`): the box follows the animated root bone, costs one engine call per
pawn per frame and needs no state detection (section 5). The level scripts that drive these spawns live in the map
files, so which path a level uses is not in the packages.

Method: read from the script packages, dnAI.dll, Engine.dll and dnAnimation.dll. `tools/ubc.py` over
`extracted-packages/*.u` (every function decoded once into a scratch dump and grepped; enums read from the package);
Ghidra MCP on project `dnf`: dnAI.dll, Engine.dll and dnAnimation.dll; plate comments saved on dnAI `0x100102A0`,
`0x10028850`, `0x1006A4F0`, Engine `0x1007E410`, dnAnimation `0x10035C90`. Export names read from the PE export tables
with Python. Sections 6 to 8 also rest on nukepanel.log from play tests.

## 1. Who drops in, and how

| path | classes | script / native | what moves |
|---|---|---|---|
| Alien dropship | `dnAlienDropship` (dnAI.u, base `dnAlienDropship_Base`), riders from `SpawnInfo[]` (`SDropShipSpawnInfo`: `Factory`, `MountBone`, `RappelAttachToBone`, `NextScriptObj`, `ReleaseTag`) | `SpawnAI`, `ReleaseAI`, `TriggerFunc_ReleaseAI0..3` / `_ReleaseAllAI` | see 2a |
| Exit dropship | riders with `Anim_ExitDropship`: `PigCop_Grunt` (+ subclasses), `Enforcer`, `AssaultTrooper_Captain` (their `RegisterPrecacheComponents`) | `UScriptMethod_ExitDropship::ExecuteNative` `dnAI!0x1006A4F0` | PlayAnim `Anim_ExitDropship`, then `ScriptMethod_Fall` |
| Rappel (the green beam) | `PigCop_Grunt` only registers `Anim_RappelStart/Idle/End` | `UScriptMethod_Rappel::ExecuteNative` `dnAI!0x100676F0` -> op `AIOP_Rappel` (102) | see 2b |
| Jetpack fly-in | Assault Troopers (`ChanceFly`, `ChanceLand`, `ForceStartJetpack` copied from the factory in `AIActorFactory.InternalSpawn`); `AssaultTrooper_Captain.COND_NeedToLand` / `COND_ShouldLand`, op `AIOP_Land` | flying, then `NotifyLand` clears `bCanFly`/`bIsFlying` | the pawn |
| Teleport-in | `AssaultTrooper_Captain` (`TeleportParms` lights `AssaultTrooper_Captain_TeleportLight`, overlay fade), `ScriptMethod_TeleportToPos` / `_TeleportToActor`, `Action_Teleport` | `ExecuteOp_Teleport` `dnAI!0x100283A0` | `Location` jumps at once (not a fall) |
| Egg pod drop | `Pregnator_EggPod` (spawned by `EggPod.StaticSpawnPregnatorFromEggpod`; pods `acEggPod_Ceiling`/`_Wall`) | `ApplyVelocity`: `SetPhysics(2)` (PHYS_Falling), op `AIOP_Fall` (28), `Velocity += PodVel`; `Landed` -> `SetPhysics(18)` | the pawn |
| Factory drop | any `AIActorFactory` with `ForceDropToFloor` | `InternalSpawn`: `SpawnedActor.DropToFloor()` right after spawning | `Location` placed on the floor at spawn |

`SpawnScript` (on the factory, copied to the pawn) and the dropship's `NextScriptObj` are `ScriptObj` objects in
the maps; which map uses which path is not in the packages.

## 2. Where `Location` is during the drop

### 2a. Dropship riders (dnAI.u)

- `dnAlienDropship.SpawnAI`: the factory is moved to the mount bone (`BoneGetTranslate(MountBone)` + the class's
  `CollisionHeight` along the bone's up), spawns, then the rider gets `SetPhysicsControllerActive(false)`,
  `SetPhysics(0)` (PHYS_None), `AttachToActor(ship, bMatchLocation=false, bMatchRotation=false, bGenerateOffsets=true)`, `bNoFailMountedMovement = true`, `bHackDropshipNoShrink`, `bHackNoDeathAnim`, a death
  impulse, and `RappelParms.AttachToActor/AttachToBone` set to the ship. **Not hidden, collision untouched** (only the
  factory's `DisableAllCollision` turns it off). While mounted, `Location` rides with the ship.
- `dnAlienDropship.ReleaseAI`: `DetachFromMountParent`, `SetPhysics(18)` (`PHYS_Meqon`, `Object.EPhysics`),
  `SetPhysicsControllerActive(true)`, flags restored, `PlayScript(NextScriptObj)`.

### 2b. Rappel (op 102)

- `AAIActor::ExecuteOp_Rappel` `dnAI!0x100102A0`: event `ExecuteRappelEffects` (spawns
  `RappelParms.BeamSystemClass`, an `AIRappelBeamSystem`, between the ship bone and the pawn) and
  `ExecuteState_Anim` (state `AnimX`).
- The fall is physical: `AIActor.CalculatePhysics_Falling` sets `KarmaActor.GravityScale = GetFallingGravityScale()`, which returns `RappelParms.GravityScale` while `GetCurrentOp() == 102`.
- `AAIActor::ExitStateOp_Rappel` `dnAI!0x10028850`: `RappelParms.Mode` (+0x20A0) 0 loops the idle anim
  (`CanUseState_AnimExitOnLand`), and on Meqon ground contact (`FMeqonPawnController::GetGroundContact`) switches to
  mode 2 and the end anim; `EndRappel` destroys the beam.

### 2c. Exit dropship, falls, jump-downs

- `UScriptMethod_ExitDropship::ExecuteNative` builds a two-step script: `PlayAnim('Anim_ExitDropship')`, then
  `ScriptMethod_Fall`. The fall is state `Fall` (`AIActor.Fall.BeginState` records `FallParms.StartZ = Location.Z`; `ExecuteModeLand` measures `StartZ - Location.Z`): **the script itself relies on `Location` falling**.
- The `PlayAnim` step runs in state `AnimX`. `AIActor.SetAnimXAnimSyncParms`: a synced anim calls
  `SetAnimSync(2,2,2,2,2)` (`EBitModifier` 2 = BM_Clear: clears `bUseAnimDispSpeed`, `bUseAnimDispDir`,
  `bUseAnimRotation`, `bUseAcceleration`, `bAnimCutScene`), `FullAnim` sets direction/rotation/acceleration, other
  anims set direction and rotation and leave `bUseAnimDispSpeed` as it was. **Where the actor does not take an
  anim's displacement, the mesh's root bone moves away from `Location`.** The test log of section 4 shows exactly this case (a box waiting below while the body falls).
- Cut-scene mode (`bAnimCutScene`, set by every `DumbAI_*` class's `SetAnimCutScene(true)`):
  `CalculatePhysics_CutScene` sets gravity 0 and drives the controller from `CurrentMoveDelta`;
  `ShouldEnableGroundFinder` returns false.
- Jump-downs: separate `a_combat_*_stand_jumpdown_takeoff/idle/landing` sequences, so the airtime is physical.

### 2d. Hidden, collision, physics mode

No drop-in path sets `bHidden`. Collision changes only through the factory's `DisableAllCollision` and the egg
pod's `LeftEggPod` (`SetCollision(true,...)`). Every AI pawn uses `PHYS_Meqon` (18), so the physics mode does not
identify the drop. The exceptions are `PHYS_None` while mounted on a dropship and `PHYS_Falling` for a launched
egg-pod Pregnator.

## 3. Reading where the body is

- **Render bounds (used).** `ARenderActor::GetRenderBoundingBox(FBox&)` `Engine!0x1007E410` (exported):
  DrawType 2 (`DT_Mesh`) with a mesh -> `GetMeshInstance` (vt+0x198) -> `UMeshInstance::GetRenderBoundingBox`
  `Engine!0x1030EA80` -> `dnSkinMeshInstance::GetAABB(0)` (`dnAnimation!0x10035C90`, imported by Engine) through
  `GetToWorld`, as a world AABB. In `GetAABB(0)`, when the mesh is not `ForceAccurateAABB` (+0x84), the cached box
  (stamped by skeleton+0x218) is placed by the **root bone's** transform (skeleton+0x104, `UpdateDirtyTransforms`).
  Otherwise it is `dnSkeletonInstance::GetAABB` (`0x1002EB70`): per-bone AABBs of the posed skeleton
  (`dnBone::UpdateAABB`). Either way it is where the renderer draws the body, and it follows a root-bone offset.
  Limit: a drop animated only below the root (pelvis moving, root still) on a non-accurate mesh would not show. The
  log in section 4 shows that case as a zero offset.
  - Cost: one call per pawn per frame. The non-accurate path is cached per pose. It takes the `dnThread`
    semaphore, which is free at our Present: the render job has finished by then (engine-map.md section 5). It
    writes only the AABB cache the renderer writes itself. Returns 0 for a pawn without a mesh: keep the
    cylinder.
- **A bone.** `UMeshInstance::BoneGetTranslate` `Engine!0x10305C10` (aim.md section 2) on the root (`Root`, the
  `LockOnBone` default) or a pelvis bone. One named lookup per pawn per frame, game thread only, and per-mesh bone
  names. Better for the aim point than for the box.
- **No cinematic actor.** No path found draws the falling body as a separate actor. The dropship's riders are the
  real pawns, mounted.

## 4. Detecting the state, and what the log shows

- Detection without a fix list: compare the render-bounds centre with `Location`. More than about
  `CollisionHeight` apart means "body away from `Location`". This needs no state names. A gate by state is possible:
  state `AnimX` or `Fall` (the state name the world walk already reads on knockdowns), `GetCurrentOp() == 102`,
  `MountParent != None` with `Physics == 0` (riding a dropship), `bAnimCutScene`.
- Instrument (stays, per the evidence rule): for each hostile pawn, each frame, log once per pawn per state
  `spawnbox: <class> state <name> phys <n> mount <0/1> cutscene <0/1> dz <body centre Z - Location.Z>` when
  |offset| > 64 UU. Also log `spawnbox: skipped <class> state <name>` for pawns the hostility filter drops while in
  `AnimX`/`Fall`/mounted. That separates a root offset from a faction/attitude timing issue (a box that only appears
  once the enemies land), which would not be a position problem.
- Measured: a test log shows the root-offset case: `spawnbox: AssaultTrooper_Grunt state AnimX phys 18 mount 0 cutscene 1 offset 343 dz 264`, 240 far frames in one 600-frame window, the render-bounds call 0.001-0.002 ms.
- A drop-in played with no `spawnbox:` offset line and no skip line would kill the root-offset explanation.

## 5. What the panel does

spawnbox.cpp flags a hostile pawn whose render-box centre is more than 64 units from `Location`. It finds them
round-robin (8 calls a frame) and reads each flagged pawn every frame until it is back within 64 units, or for 5 s at
most (8 flagged at most). Meanwhile its ESP box and damage-number anchor come from the render bounds; otherwise the
cylinder at `Location` stays, so the usual box and the aim/health-bar layout do not change. The box is not hidden: no
drop path hides the pawn or turns off its collision (section 2d), so it stays a live target while it drops. A flagged
pawn is aimed at its render box (section 7); only one with no box this frame is left out (aim.md section 5).

## 6. Teleports and damage nullifiers

What keeps a hostile pawn from being a valid target, and how spawnbox.cpp reads it (each rule excludes through
`spawnbox_excluded`: no pick, the lock drops, no triggerbot fire; the ESP is unchanged).

- **Teleport.** An AI teleport runs as AI op 138, `AIOP_Teleport`, the index in `BaseAI.EAIOp` (Engine.u, 151 names; read
  with tools/ubc.py). Op 137 is `AIOP_TakeOff`, a flight, not a teleport. The reflection layer reads no enum names, so
  138 is hardcoded in spawnbox.h (`SB_AIOP_TELEPORT`). Only AssaultTrooper_Captain sets `bNoDamage`, `bTeleporting` and
  `bAcceptMines=false` on op 138; NotifyTeleportInProgress hides the pawn (`bHidden`). Grunts may show op 138 alone.
  spawnbox.cpp reads `CurrentOp`, `bTeleporting` and `bHidden` once per hostile pawn per frame; any of them excludes
  the pawn (SB_EXCL_TELEPORT). Log: `spawnbox: teleporting <class> excluded: op N (teleport 0/1), bTeleporting, bNoDamage, bHidden` and `... back as a target after N ms`, 8 each a level.
- **bNoDamage.** A Captain was shot by the triggerbot while invincible after spawning. `RenderActor.bNoDamage` is read
  with the teleport signals and kept in the same table (bit `SB_TELE_NODAMAGE`); a hostile pawn with it set is
  excluded as `SB_EXCL_NODAMAGE`, after far and teleporting in precedence. The aim and tbot 600-frame lines count it
  as "invulnerable N".
- **Watch log:** from the first frame the walk sees a hostile pawn until 6 s after its last teleport/no-damage
  exclusion (6 s after first sight if none), one line per change of the tuple, 12 a pawn and 60 a level:
  `spawnbox: watch <class> +<ms since first sight> ms: state <S> op N bNoDamage N bHidden N bTeleporting N TKShield N health N` (TKShield 1 = set, -1 = the property did not resolve). The line also carries `DamageScaleInfo <-1|0|1|2> neutral zone <-1|0|1> absorbers N`, `bExpanding N bCollideActors N bProjTarget N` and ends with `Role N`.
- **The damage path** (script dump): `Pawn.TakeDamage` returns on `ImmuneToDamage()` and at once when
  `Role < ROLE_Authority (4)`, then `GameInfo.ModifyDamage` (returns 0 when `Injured.Region.Zone.bNeutralZone`, else
  `Damage * InstigatedBy.DamageScaling`), then `DamageType.ReduceDamage` and `DamageAbsorbers`, then `TookDamage`;
  `AIActor.TookDamage` calls `AIScaleDamage` first, which walks `DamageScaleInfo` last to first and the first entry that
  matches (QualifierClass None or a parent of the instigator class; type lists empty or passing) multiplies the damage.
  This is level-designer data, so a scripted pawn can be immune to the player. Natively, `ARenderActor::SetHealth`
  (Engine.dll 0x1007F7B0) stores `clamp(NewHealth, HealthMin +0x5F4, HealthCap +0x5F0)` into Health (+0x5EC); every
  damage path ends there (Pawn.TookDamage -> ModifyHealthActivity -> SetHealth). `TriggerAssign` can raise a pawn's floor
  (NewHealthMin), so a pawn whose Health is at or under its HealthMin takes hits and loses nothing. The native bullet
  path is TraceFireHit 0x10203A50 -> eventGetBaseDamage -> eventTakeHitDamage.
- **Rules** (spawnbox.cpp reads them by name once per hostile pawn when first seen, then every 30 frames
  (`SPAWNBOX_DMG_REREAD`), and logs a change; the Role, floor and lock signals every frame):
  - `Actor.Role` below 4 (`spawnbox_role_blocks`, 0..3) counts as no-damage (SB_TELE_NODAMAGE); first sight logs
    `spawnbox: <class> Role N RemoteRole N: TakeDamage returns before any damage; left out`.
  - HealthMin > 0 and Health <= HealthMin counts as no-damage (`spawnbox_health_floored`); first sight logs
    `spawnbox: <class> health held at its floor: Health X, HealthMin Y`.
  - Any of Invulnerable, bGodMode, bGhostMode, bProtectedEgoRecharge excludes the pawn as no-damage from the first
    frame (`spawnbox_health_locked`); first sight logs `spawnbox: <class> health locked: Invulnerable N bGodMode N bGhostMode N bProtectedEgoRecharge N`. Basis: `Pawn.ModifyHealthActivity` sets `DeltaHealth = max(0, DeltaHealth)`
    while `CurrentlyInvincible()` (AIActor: `Invulnerable`, else `bGodMode`, `bGhostMode`, `bNoDamage`) and zeroes a
    loss while `bProtectedEgoRecharge`.
  - `DamageScaleInfo` is evaluated for the player's class chain (`spawnbox_dscale_verdict`): the last entry whose class
    matches decides; DamageScale <= 0 excludes (`SB_EXCL_PLAYERDMG0`); an entry with type lists is decided by the held
    weapon's damage type (below), unknown when that type is unknown. `Region.Zone.bNeutralZone` excludes
    (`SB_EXCL_NEUTRAL`). Both rank after bNoDamage. Members are found by name (StructProperty's struct at +0x78, then
    `uprop_get` on it; arrays are {data, count}, count capped at 64). The aim and tbot 600-frame lines count `player damage 0` and `neutral zone`; each level logs up to 16 `spawnbox: damage scale <class> [i/n]: QualifierClass ..., N type(s), N not-type(s), DamageScale X` lines for non-empty arrays.
- **The Map04 cp03 Captain pair** (two Captains held 75 health through 3 s of shotgun fire with bNoDamage 0, bHidden 0,
  bTeleporting 0, TKShield 0). A probe showed: Role 4, HealthMin 0, HealthCap 75; shots on armour pieces
  (`AssaultTrooper_Captain_Armor_Helmet`, `_LeftShoulderTop`) do nothing to Health as expected, and body shots register
  in full: `ThisFrameDamage` ~110 (ShotgunDamage), `AIActor.DamageInfo` valid, damage ~11 a pellet, time = now. So the
  damage reaches `AIActor.TookDamage` -> `Pawn.TookDamage` -> `Pawn.ModifyHealthActivity`. The bullet trace itself hits
  the pair (aim: "clear by hitting the target" 21, "with no hit" 0; bCollideActors 1, bExpanding 0), so the shots
  reach them and the damage is lost later, past every script gate read above. The lock rule above
  (`ModifyHealthActivity` zeroes the loss) covers this path.
- **Bullet-trace instrument** (log only): aim's 600-frame line counts clear bullet traces "by hitting the target" and
  "with no hit"; the first 8 no-hit traces a level log `spawnbox: bullet trace hit nothing before <point> of <class>:`
  with bCollideActors, bBlockActors, bBlockPlayers, bProjTarget, bHidden, bExpanding, bTraceShootable, bShootable,
  bIgnoreAimAssist (-1 = unresolved). Names found in the package name tables (extracted-packages, tools/ubc.py):
  bCollideActors, bBlockActors, bBlockPlayers, bExpanding, bTraceShootable, bShootable, bIgnoreAimAssist, bBlockAI;
  bShotTrace exists only as an STraceFlags member. **No `bProjTarget`, `bBlockShots` or `bIgnoreShot` exists anywhere**;
  bProjTarget is read anyway and logs -1. `AActor::TraceFire` (0x10282A30, flags 0x1080675) uses other flags than the
  look trace (DirectLookHitActor); aim.cpp `trace_point` calls "nothing hit before the point" clear, so a pawn the
  bullet passes through still reads as visible.
- **Per-shot probe** (`probe_line`, capped per level by `spawnbox_probe_feed`): while the triggerbot holds on a hitscan
  target, each growth of AccuracyShots logs (24 a level, 6 a pawn): `spawnbox: probe <class> shot N: Health H HealthMin M HealthCap C Role R RemoteRole RR ThisFrameDamage D (<DamageType class>) DamageInfo valid V damage D time T (<DamageType class>) now G; trace hit <class|the target|nothing>`. Members by name (-1 / ? when unresolved):
  RenderActor.ThisFrameDamage (SDamageInfo: Damage, DamageType), AIActor.DamageInfo (SAIDamageInfo: Valid, Damage, Time,
  DamageType), Level.GameTimeSeconds; the trace hit is aim.cpp's last bullet-flag hit for that target
  (`spawnbox_note_hit`). Reading it: ThisFrameDamage and DamageInfo.Time never change -> the damage dies before
  InteractiveActor.TakeDamage (Role, immune, ModifyDamage, ReduceDamage, or no call); DamageInfo.Time moves with
  Damage > 0 but Health holds -> it dies in Pawn.TookDamage, SetHealth or the clamp.
- **No guard on "shots had no effect".** A guard that fed `PlayerPawn.AccuracyShots` and marked a pawn when the count
  grew by >= 4 with Health unchanged excluded damageable enemies: `BattleLord_Boss` (x6, health held at 2800),
  `Enforcer_Shield` (x4, the shield absorbs frontal hits) and `Pigcop_Berzerker` were marked from misses, shields and
  armour, and a mark cleared only when Health moved, so the Triggerbot stopped until the player landed a hit. The exact
  rules above (`AIActor.Invulnerable` is in the health-locked rule) replace it. Gone: `SbNoEffs`, `spawnbox_noeff_*`, `SB_EXCL_NOEFFECT` (SB_EXCL_DOOMED is 7), `spawnbox_noeff_feed`, the tbot feed
  and the "no effect" counters and log lines.
- **Wrong damage type.** A Ripper targeted the Battlelord boss in the Map15 final battle although only explosives hurt
  it. The log: `damage scale BattleLord_Boss [0/1]: QualifierClass None, 0 type(s), 2 not-type(s), DamageScale 0.00`,
  so DamageScaleInfo verdict 2 and no exclusion. AIScaleDamage (dnAI.u, tools/ubc.py) substitutes `Engine.DamageType`
  for a None type, then an entry matches when (QualifierClass None or ClassIsChildOf(instigator class, it)) and (no
  QualifierDamageTypes or `PassDamageType(type, i, false)`) and (no QualifierNotDamageTypes or
  `!PassDamageType(type, i, true)`); the first match from the end multiplies and returns. `AAIActor::PassDamageType`
  (dnAI.dll!0x10024680, exec 0x1002CDC0, decompiled and disassembled): casts the type to UClass (0x1000AE30; not a
  class: false), takes the entry's list (`DamageScaleInfo` data at +0x13C4, entries 0x20 bytes, the type list's data at
  +0x08, the not-type list's at +0x14), and walks it last to first, skipping None elements; true when the type or any
  of its supers (`GetSuperStruct`) is an element: ClassIsChildOf(type, element). The lists hold class references.
  Rule (`spawnbox_pass_type`, `spawnbox_dscale_verdict`): each entry's lists (count capped at 16, data readable, else
  the type counts as unknown) are evaluated against the weapon type's class chain (16 supers at most); a typed entry
  with the type unknown stops the walk as unknown (no exclusion); once a typed entry was walked, a zero is
  `SB_DS_TYPE_ZERO` (verdict 3), excluded as `SB_EXCL_WRONGTYPE` (8; after the neutral zone, before doomed) only while
  the weapon it was read with is still held (`spawnbox_dmg_type_excludes`), so a switch to an explosive frees the target
  the same frame; the verdicts are re-read the frame after a switch (`spawnbox_dmg_slot`'s wtype). The weapon's damage
  type (`aim_damage_type`, read once per config class or gun, by name): hitscan: `WeaponConfig.TraceDamageType` (the
  native PerformFire's TraceFire, Engine.dll!0x100D0260, cfg+0x54; aim.md section 1); projectile: the ProjectileClass
  default's `Projectile.DamageClass` (Projectile.DoDamage passes it to TakeDamage and HurtRadius); melee:
  `WeaponConfig.MeleeDamageType` (Weapon.MeleeHit); beam (freeze ray) and other kinds: unknown. Mounted: the MG42 gun's
  `TraceDamageType` (its PerformFire's TraceFire); the launcher spawns `dnGrenadeLauncherProjectile` and sets only
  Damage and DamageRadius, so its default DamageClass; the SuperTurret fires through a
  `DecoActivity('SuperTurret_FireProjectile')`, taken as `dnRocket_SuperTurret`'s default DamageClass (as aim.cpp's
  projectile model). The aim and tbot 600-frame lines count "wrong damage type". Logs: `aim: weapon <cfg> damage type <class> (<source>)` once per weapon change (32 a level); the startup entry lines carry the lists' class names (`N not-type(s) [A, B]`); a weapon switch that changes a pawn's verdict logs `spawnbox: <class> with damage type <type>: DamageScaleInfo verdict a -> b` (8 a level).

## 7. Targets in a scripted animation

- **Evidence (Map16B/Map17, nukepanel.log):** the aimbot never locked on a damageable enemy working a mounted MG42
  (holding the Ripper). The exact rules removed nobody (wrong damage type, doomed, invulnerable, teleporting all 0);
  aim's 600-frame lines showed `excluded: animated away 0, cut-scene 511/558/514...`: a blanket `bAnimCutScene`
  exclusion was the only rule removing candidates, so it is gone.
- **Rule: target if damageable.** An enemy in a scripted animation is targeted like any enemy when the exact damage
  rules allow it (Invulnerable, bNoDamage, HealthMin, Role, DamageScaleInfo and the damage type, the neutral zone,
  teleporting) and the bullet-flag visibility trace reaches it.
- **Implementation (`spawnbox_exclusion`):** the cut-scene bit is still an input and is still noted per pawn, but never
  excludes. A flagged pawn (body more than 64 units from `Location`) is excluded as `SB_EXCL_FAR` only when its render
  box was not read this frame (`spawnbox_body` fails); otherwise aim.cpp aims at the render-box centre
  (`aim_pawn_point`), traces visibility to it with the bullet flags (one trace, as the aircraft pass does), and uses it
  for the pick's distance, screen, turret-limit, reach and ally-in-the-line tests. SB_EXCL_CUTSCENE is gone (value 2
  unused).
- A grunt in an intro animation behind a closed door (a test log) stays covered by the bullet-flag visibility trace
  (`trace_point`, aim.md section 3): that trace stops on movers (the `door/mover` hidden-hit counts in test logs and
  nukepanel.log).
- The Triggerbot's Crosshair: a look-trace hit on the pawn is taken as is. The projected fallback (when the look
  trace's properties do not resolve) places a far pawn at its render box (`tbot_pawn_cyl`, as esp.cpp boxes it) and
  checks it with the same bullet-flag trace to the box centre (`aim_box_visible`), not LineOfSightTo at `Location`.
- A pawn's cached visibility (`aim_vis`, 100 ms) is dropped when spawnbox flags it far or it comes back near
  (`aim_vis_forget`): the cached verdict was for the other aim point. Box probes count in the probe line's
  `render box N`.
- **Instrument:** aim's 600-frame line counts `admitted in a cut-scene N, at the render box N (box traces N)` and
  `excluded: animated away without a render box N`; the first picks a level in a cut-scene or animated away log
  `spawnbox: target <class> picked: cut-scene 0/1, state <S>, offset D dz Z (-1: near), aim at the render box 0/1`
  (8 a level, once per pawn, state and box use).

## 8. Pawns hit through mounted collision: the EnergyLeech boss

`EnergyLeech_Boss` has no hit shape of its own. Its rendered
body is animated far from `Location`, so the render box is the wrong body for it. It is hit through collision actors
mounted on it. A pawn whose own cylinder is 0 or tiny is aimed at, led by and boxed by those actors. In game the box fits the boss
and side-on Devastator rockets land.

- **Measured (nukepanel.log of the boss fight; before the helpers were used the ESP box was bigger than the screen
  and shots missed):**
  - `spawnbox: EnergyLeech_Boss state AnimX phys 9 mount 0 cutscene 0 offset 2511 dz 70 (collision height 0)`.
  - The 600-frame lines show the render box 1917 to 2511 units from `Location`, flagged far every frame (600 of 600).
  - The aim used the render-box centre (`aim at the render box 1`). Its bullet trace hit nothing before that point
    (`bullet trace hit nothing before default`, 5 lines): the centre is in open air, not on the body.
  - The look trace hit `EnergyLeech_HelperCollision` and `EnergyLeech_HelperCollision_Body`, each with Owner and
    MountParent the boss.
- **Script (tools/ubc.py, dnAI.u):**
  - `PostBeginPlay` finds the mounted `EnergyLeech_HelperCollision` and `_HelperCollision_Body`. Neither class
    declares a function: no `TakeDamage`, so they are hit shapes and not damage takers.
  - Damage reaches the boss through `EnergyLeech_Boss_HurtRadiusSensor.TakeDamage`, which takes `ExplosionDamage`
    children only and calls `Leech.NoteSensorDamaged`. That keeps the frame's best damage, and `ApplyBestDamage` then
    calls `TakeDamage(LocalPlayer, BestDamage, ..., ExplosionDamage)` on the boss. So the sensors are the weak point
    for splash. The boss's own `TakeDamage` only tracks the suck-attack and stage thresholds before `Pawn.TakeDamage`.
  - `unhide` restores the boss's collision and calls `EnableCollisionHelpers` (sensors on). `StrengthTapBegin`
    sets bNoDamage and disables them.
- **Rule (`mountcol.h`, glue in spawnbox.cpp and aim.cpp):**
  - A hostile pawn with `CollisionRadius` or `CollisionHeight` under 4 (`mc_tiny`) has its `Actor.MountedActorList`
    read every frame (the array validated once, 64 entries at most).
  - Its helpers are the entries whose `MountParent` is the pawn, with `bCollideActors` set and a real cylinder, and
    which are not walked pawns (`mc_is_helper`). At most 16 are kept, for 4 pawns.
  - **Aim order** (`mc_order`): damage takers first, then the largest cylinder. A taker is a class that declares its
    own `TakeDamage`: the UFunction's outer is the actor's class. The probe traces the helpers' Locations in that
    order with the bullet flags (4 at most). The first clear one is the aim point (`spawnbox_helper_choose`).
  - **Intercept:** the projectile lead uses the chosen helper's per-frame `Location` delta over the frame time,
    averaged over 4 frames (`mc_vel_step`), not the pawn's `Velocity`. It restarts when the helper changes or after a
    0.25 s gap.
  - **Box:** `spawnbox_body` returns the box round every helper cylinder (`mc_union`), ahead of the render box
    (`mc_body`). So ESP, damage numbers, the pick's centre, the triggerbot's fallback and the far exclusion all follow
    it. `aim_box_visible` traces the helpers.
  - A pawn without helpers is unchanged.
- **Instrument:** `spawnbox: mounted collision of <class>: ...` once a second while the pawn has helpers and is not
  hidden or invulnerable, 10 a level. It prints `Location`, the cylinder, `Velocity`, the render box (centre, half, offset),
  the list size, colliding entries, helper count and their box. It also prints the aim's helper, its velocity, its
  frame count and the last frame's delta in units and ms.
  One `spawnbox:   helper i <class> ...` line per helper gives its place, size, collision flags, `MountMeshItem`,
  `TakeDamage`'s class and `GetCollisionBounds`. The pick line now says `aim at its mounted collision`. The aim line
  counts `mounted collision N (T trace(s))`, and the spawnbox line `mounted collision N pawn-frame(s)`.
- **First snapshots** (nukepanel.log, 10; the box was better, rockets landed when faced head on and mostly missed
  from the side):
  - 16 helpers: 14 `EnergyLeech_Boss_HurtRadiusSensor` (radius 64, takers, bones Root and body_02..body_18), plus
    `EnergyLeech_HelperCollision_Body` (body_16) and `EnergyLeech_HelperCollision` (head_03), both radius 22.
  - The render box's half extents were 1564,1459,1354 against the helpers' 164,498,232. That was the over-size box.
  - The 14 sensors tie on rank, so list order picked helper 0: the Root sensor at the static tail base, 519 units
    from Location. It never moved (velocity 0, "last frame 0.0 units"). The body sensors moved: body_10 by about 100
    units, and head_03 swung in z from -1066 to 169.
- **Helper choice:**
  - Among helpers of equal rank, the aim order prefers the one nearest the helpers' box centre, the middle of the
    body (`mc_order`).
  - The probe keeps the current helper while it stays clear. It switches only to a higher rank, or to an equal one
    more than 30 % nearer that centre (`mc_better`).
  - The velocity takes each move of the helper over the time since its last move, so a frame that sees the same
    position twice adds no zero sample. No move for 0.25 s reads as still (`mc_vel_step`).
- **Instrument:**
  - `spawnbox: <aim pick | aimbot lock | FireDown> on <class> at its mounted collision: helper i of n ... MountMeshItem ... lead velocity ...`, 20 a level.
  - Each pick or lock grants 10 more once-a-second snapshots (60 a level at most).

## Open questions

- Which signal fires for a grunt's teleport (op 138 alone, or `bTeleporting`).
- Which of Invulnerable, bGodMode, bGhostMode, bProtectedEgoRecharge holds on the Map04 cp03 Captain pair.
- The DamageScaleInfo member names (taken from the script dump) and the probe's member types (DamageType read as
  class or object); that AccuracyShots grows in the same frame the hit lands.
- The Battlelord's two NOT types (the log names them), the SuperTurret's rocket class, and the Ripper's
  TraceDamageType name.
- That the MG42 gunner of Map16B/Map17 now locks; whether a far-box grunt behind a closed door is ever picked.
- EnergyLeech boss:
  - That the helpers' `Location` follows the animation. A delta of 0 while the boss moves means they do not, and the
    lead stays zero. The root sensor at the static tail base never moved; the body sensors did.
  - That the HurtRadiusSensors collide. If they do, they are takers and are aimed at first, which is right for splash.
  - That no large non-hit actor (`AimAssist_Enemy_Decoration`, mounted and its collision restored in `unhide`) joins
    the helpers and swells the box or wins as the largest. The helper lines name every member.
  - That `GetCollisionBounds` and the cylinder agree.
  - Only `MountParent` children are walked. Actors owned by the pawn but not mounted on it are not.
  - The render box's half extents beyond the snapshots above, and the boss's `Velocity` against the helpers' motion
    (the instrument prints both).

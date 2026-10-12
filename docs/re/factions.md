# Factions, relationships and attitude (who is an enemy)

Relationships are not stored on the faction classes. Each AI pawn (dnAI `AIActor`) carries two arrays
of `SRelationship {class m_aClass; EAIAttitude m_eAttitude; float m_fDamageThreshold}`: `Relationships` (its class
defaults) and `Relations` (+0x188C, the runtime list the AI decides by; the ESP reads it, falling back to the
defaults when it is empty). The AI's attitude toward a pawn is the entry for that pawn's `Faction` class
(exact, else the most derived ancestor); below `AIATT_Neutral` is hostile, above is friendly, no entry is neutral.
The ESP uses exactly that: the pawn's attitude toward the player's faction (`AIFactionDuke`).
**Exception (section Cycloid):** a level can give one pawn an instance `Relationships` that loves Duke and make it
fight him through a scripted forced target; attitude alone then reads it as an ally (the Map00 Cycloid).
The ESP therefore counts a pawn as an enemy by attitude, while its `AIActor.PrimaryTargetActor` is the player, or
while it is the HUD's boss-meter actor (`DukeHUD.BossMeterActor` with `bDrawBossMeter`); damage numbers follow,
without a kill marker when Health reaches 0 in `ControlX` or as the boss-meter actor. A `world: watch` line every
600 frames names any non-hostile pawn with a target or the meter.

## The types (Engine.u, parsed with Python from extracted-packages/Engine.u)

- `Actor.SRelationship` (struct): `m_aClass` (ClassProperty), `m_eAttitude` (ByteProperty, enum EAIAttitude),
  `m_fDamageThreshold` (FloatProperty; "how much damage ... to move us to the next lower attitude level").
- `Actor.EAIAttitude`: 0 AIATT_InfiniteHate, 1 Despise, 2 Hate, 3 Dislike, 4 Neutral, 5 Like, 6 Love, 7 Idolize,
  8 InfiniteLove.
- Faction classes: `AIFaction` (: Actor), `AIFactionHuman`, `AIFactionAlien` (: AIFaction), `AIFactionDuke`
  (: AIFactionHuman), `AIFactionHoloduke` (: AIFactionDuke), `AIFactionRat` (: AIFactionAlien). None declares a
  relationship property; their only content is `GetFaction()` (an `EFaction` byte: Human 0, Alien 1).
- `Pawn.Faction` is a `class<AIFaction>` (ClassProperty).

## Where the relationships live (dnAI.u, dnAI.dll)

- Two arrays of SRelationship on `AIActor` (dnAI.u, declaration order `RelationshipMgr`, `Relations`,
  `Relationships`, `m_RelationOverrides`; each Inner is a StructProperty of SRelationship):
  - `Relations` at **+0x188C**: the runtime list. FindRelationship walks it (`lea ebx,[esi+0x188c]` at
    `dnAI.dll!0x1003aa29`), AddRelationship appends to it (`0x1003acfd`), `eventSetRelations(dnArray<SRelationship>)`
    sets it. It starts as a copy of the defaults and changes when the game changes an attitude (a level
    `AIRelationships` actor, script `AddRelationship`, the `m_fDamageThreshold` demotion of a shot ally).
  - `Relationships` at +0x1898 (reflection, test log): the class defaults the runtime list is built from; native
    code touches it only in the constructor, destructor and copy routines.
- `Relationships` class defaults, decoded from each class's default properties (attitude toward AIFactionHoloduke /
  Duke / Human / Alien):
  - Enforcer, PigCop_Grunt, AssaultTrooper_Captain, Octabrain_Super, Pregnator, EggPod, cycloid, Octababy,
    BattleLord_Boss, AlienQueen, EnergyLeech_Boss: Despise / **Hate** / Hate / Like.
  - AssaultCommander: Despise / **Hate** / Dislike / Like. Rat: Despise to all four, Love to AIFactionRat.
  - Male, Female (civilians; Workman : Male declares none and inherits Male's): Love / **Love** / Like / Hate.
  - EDF_Grunt: Love / **Love** / Like / Hate, Neutral to AIFactionRat. PodGirl_HolsomTwin: Like to all.
  - DukeClone: Duke **Hate**; HoloActor: Duke **Despise**.
- `UAIRelationshipMgr::FindRelationship(AAIActor*, UClass*)` `dnAI.dll!0x1003a9c0`: walks the actor's `Relations`
  at `+0x188c` ({data, count}), stride `0xC`, class at `+0`. Returns the first entry whose class equals the subject
  class; otherwise, among the first 16 entries whose class is an ancestor of it, the one with the most ancestors
  among the others (the most derived; the first on a tie, `0x1003ab2c`). A null class matches nothing.
- `UAIRelationshipMgr::GetAttitudeTowards(AAIActor* self, APawn* subject, const wchar_t*)` `0x1003ad50`:
  `FindRelationship(self, subject->Faction)` (Faction read at pawn `+0x9c0`); none -> `0.0`; else
  `WeightForAttitude(byte [entry+4])` (`0x1003ae60`).
- `WeightForAttitude` `0x1003a790` (jump table `0x1003a7f4`): 0 -> -1000, 1 -> -3, 2 -> -2, 3 -> -1 (floats at
  `0x1008ddd4`, `0x1008ddd0`, `0x1008ddcc`, `0x100847e0`), 4 -> 0, 5 -> 1, 6..8 positive. The sign is the
  hostility: the panel's `world_classify` (world.h) takes attitude < 4 as enemy, > 4 as ally, else neutral.

So the direction is **the AI's attitude toward the player's faction**, not the player faction's toward the AI.

## The other rule, not used: Pawn.IsEnemyTo (Engine.u bytecode)

`Pawn.IsEnemyTo(Other)` returns false for None, handles multiplayer teams, returns false when `Other.IsFriendly`
(`Pawn.IsFriendly` returns false; HoloActor overrides it), and otherwise compares `GetFaction()` with
`Other.GetFaction()` (the coarse EFaction: Duke and Workman are both Human, so not enemies; Alien vs Human are).
It agrees for aliens and civilians but cannot tell an EDF marine from a civilian, and is not what the AI fights by.

## Runtime (src/nukepanel/world.cpp)

Per pawn per frame (only while Enemies or Damage numbers is on): `Relations` by name, its element size checked to be
12 by reflection (`UArrayProperty::Inner` +0x78, read by `CopyCompleteValue` `Engine.dll!0x1004c756`, then the
inner's ElementSize +0x3E); when it is unresolved, the wrong size or empty, `Relationships` the same way. The
`{data, count}` header, the span checked readable once per array per level, each entry's class checked against the
object snapshot, then `world_attitude`; `AIActor.PrimaryTargetActor` by reflection, compared with the player.
`world_classify`: enemy = alive and (attitude < Neutral, targeting the player, or the boss-meter actor). Logged once per level per source:
`world: relationships from <Relations (runtime list) | Relationships (class defaults)> first served <Class> ...`;
once per (class, attitude, targets) per level (64 at most): `world: attitude <Class> (<its Faction>)-><player
Faction> = <name> (<value>)[, targets player]`. The 600-frame line counts `enemies`, `byTarget` (enemies whose
attitude is not hostile), `allies`, `neutrals`, `relBad` (arrays refused), `relMiss` (span-cache misses),
`downProbes` and `downs` (next section).

## Cycloid (Map00 boss): an Idolize override and a scripted target

The Cycloid fought is
`cycloid___0` (Tag `cycloid`), and its map instance overrides `Relationships` with **AIFactionDuke = Idolize (7)**
(read from Map00.dnf, dnAI.u and a test log). The runtime list it copies says it loves Duke, so attitude alone draws
it as an ally and the damage tracker would never see it. The game makes it fight Duke with a **scripted forced
target** (`ScriptMethod_SetTarget` `TargetActorTag=DukePlayer, ForceUpdate, AllowClear=False`), not by attitude. The
ESP therefore also counts a pawn as an enemy while its `AIActor.PrimaryTargetActor` is the player, or while it is the
boss-meter actor. Code: world.cpp `read_target`, `read_boss_meter`, world.h `world_classify`, `world_watch`,
`world_knock_rule`.

### The attack script (a test log, Map00)

`PrimaryTargetActor` is the right attack signal but starts only with the attack script; the ally marker in the log
is the fight Cycloid before that script, and after its Health reaches 0. The boss meter starts with the same
dispatcher, so it adds no earlier signal; it does make the knockdown a knockdown.
- **What the Cycloid attacks through:** every attack setup reads `AIActor.PrimaryTargetActor` (`cycloid.SetupShockwave`,
  `SetupBeamAttack`, `SetupDoubleFistSmash`, `SetupMortarSearch`, `COND_ShouldStandFire`, `COND_ShouldCharge`,
  `FireGrenadeWeapon`). Only `AIActor.NotifySetTargetActor` (`= GetTargetActor(0)`) and `NotifyTargetLost` (`= none`)
  write it. No Enemy field or target list is used instead.
- **When:** `Dispatcher_Cycloid_Attack` (Map00 `Dispatcher___61`) fires, with sequential delays 0/3/0/0/3/10 s, Duke's
  line, then together `ScriptTrigger_Cycloid_Attack_Duke` (the forced SetTarget),
  `dnControl_Elevator_DetachComplete` and `TriggerPlayer_Boss_Meter_Enable` (`TriggerPlayer___10`,
  `BossMeterActorTag=cycloid`, `BossMeterAction=1`). `TriggerPlayer.Trigger` finds the actor by tag and calls
  `DukeHUD.AddBossMeter` (sets `bDrawBossMeter` and `BossMeterActor`); `RemoveBossMeter` clears both.
- **The log:** the 16 `byTarget=1` lines (7386-7664) are the whole fight: continuous, `targets player` logged at
  7384. Before them (7313-7364) the fight Cycloid stands on the field with Idolize and no target: the ally marker.
  At 7684 `cycloid Health reached 0 in state AnimX: kill`: the fall plays in `AnimX` before `ControlX`, so the
  knockdown read as a kill and the Cycloid, not alive, got no marker through the QTE (7757-7804).
- **Built:** the boss-meter actor is an enemy while alive and a knockdown when its Health reaches 0; the 600-frame
  `world: watch <Class> attitude <a> alive=<0|1> target <Class>[ (the player)][, boss meter]` line (4 per line, 400
  in all) and `world: boss meter <Class|off>` on change (16 per level) show the next case.
- **Every boss, not only the Cycloid:** a boss's final Health 0 counts as a knockdown whenever `RemoveBossMeter` has not
  run yet, so its kill marker is lost while the meter is up (`(boss meter)` on the down line and
  `world: boss meter off` measure it).
- **Not covered:** the pre-attack stretch, where no game signal marks the Cycloid hostile yet. Its class-default
  attitude is Hate; only this map instance idolizes Duke. Ruling it in early would be by class or by the `cycloid`
  Tag.

### Which pawn, and the two Cycloids of Map00

- Two `cycloid` instances (class `dnAI.cycloid : AIActor`; no boss subclass; `bForMap00` switches its Map00 setup
  in `PostBeginPlay`: FinishMe type `dnControl_Cycloid_BattleC`, humiliation `dnControl_CycloidHumiliation_FieldGoal`):
  - `cycloid___1`, Tag `Actor_Cycloid_Intro`, group `14_Field_Cycloid`: the intro Cycloid the EDF shoot at from the
    hall. No instance Relationships (class defaults: Duke = Hate). Its spawn script `ScriptObj___72` sets
    `SetDamageReactions Invulnerable=BM_Set`. Destroyed by the `TriggerPlayer_Cycloid_Intro_Explosion` sequence.
  - `cycloid___0`, Tag `cycloid`, the fight on the field. Instance `Relationships`: Holoduke Despise (1), **Duke
    Idolize (7, threshold 0.1)**, Human InfiniteHate (0), Alien Like (5), Rat InfiniteLove (8). `Health_Stage1`
    3000, `bForMap00`. Spawn script `ScriptObj___35` (GuardPos, Idle).
- `AIActor.InitRelationships` copies the instance array into `Relations` with `AddRelationship`, so the runtime
  list the ESP reads (+0x188C) holds Idolize: `FindRelationship` matches `AIFactionDuke` exactly before the
  `AIFactionHuman` ancestor (InfiniteHate).
- How it attacks Duke: `ScriptTrigger_Cycloid_Attack_Duke` (Event `cycloid`) runs `ScriptObj___2`: SetAbortable,
  SetDamageReactions Invulnerable=BM_Set, `a_scream`, **SetTarget(DukePlayer, ForceUpdate, AllowClear=False)**,
  SetDamageReactions Invulnerable=BM_Clear, voice pack. `AIActor.NotifySetTargetActor` then sets
  `PrimaryTargetActor = GetTargetActor(0)`. `ScriptTrigger_Cycloid_Attack_EDF` (`ScriptObj___1`) targets `EDF_Start`
  the same way. The other three bosses (AlienQueen Map08, BattleLord_Boss Map15, EnergyLeech_Boss Map21) and the
  Cycloids of Map19B/Map22 carry no instance Relationships: class defaults, Duke = Hate.
- The game's own boss marker: `TriggerPlayer_Boss_Meter_Enable` (`BossMeterActorTag=cycloid`) feeds
  `DukeHUD.AddBossMeter`; `DukeHUD.BossMeterActor` / `bDrawBossMeter` name the boss while its bar is up, and
  `cycloid.GetHealthMeterPercent` = Health / Health_Stage1 (Map00) is the bar.

### What the log shows (a test log, Map00)

- `world: attitude cycloid (AIFactionAlien)->AIFactionDuke = Hate (2)` is the **intro** Cycloid: the
  attitude line is logged once per class per level, from the first instance, so the fight instance's Idolize was
  never logged.
- During the fight (CycloidStompShake 2510, CycloidStepShake 2534) every 600-frame line reads `pawns=4 enemies=0
  allies=3 neutrals=0`: the Cycloid is one of the allies. dmgnum stops at `hits=13` for the whole fight.
- `relMiss` is not an attitude miss: it counts span-cache misses in `rel_span_ok` (world.cpp; one `engine_readable`
  each). 1200 per 600 frames is two pawns whose `Relations` data pointers share a slot of the 64-slot cache and
  evict each other every frame: a cost, not a classification failure.
- `m_fDamageThreshold` 0.1 on the Duke entry did not move the Cycloid below Neutral in this run (it stayed in the
  allies count through the fight); where the demotion is applied (native) was not read.

### Classification rule (built)

- `world_classify` has a third input: **enemy = alive && (attitude < Neutral || PrimaryTargetActor == player)**,
  ally/neutral otherwise. `AIActor.PrimaryTargetActor` (ObjectProperty, by reflection) is the pawn's current
  target as the AI itself fights by; a forced script target shows there. It covers every scripted attacker, not
  only bosses. A second signal: the pawn equals `DukeHUD.BossMeterActor` while `bDrawBossMeter`.
- The attitude is logged once per (class, attitude) pair, and a line when a pawn turns enemy by target, so the next
  override is visible in the log.
- Damage numbers follow from this: dmgnum feeds only `p.enemy` pawns (next section).

### Damage on the Cycloid (damage numbers)

- Damage lands on the Cycloid's own `RenderActor.Health` through `cycloid.TakeDamage` -> `Pawn.TakeDamage`; no proxy
  actor, no separate boss health. `SetNewHealth(h)` sets `HealthCap` and `Health` (stage starts).
- Map00: `PostBeginPlay` sets Health to `Health_Stage1` (3000); at half of it `TakeDamage` enables charge and
  shockwave. When Health runs out, `ShouldUseDieOp` keeps it alive: it falls into state `ControlX` (the knockdown,
  FinishMe type `BattleC`), whose `TakeDamage` is empty (no damage while down). `TimeOutControlX` and
  `EndControlX_UserFailed` restore `Health_RecoveryHealth`; `EndControlX_UserSuccess` for BattleC goes on to the
  field-goal humiliation and the death.
- Invulnerable windows: `SetDamageReactions Invulnerable=BM_Set` during the scream of the attack script (cleared
  right after); the intro Cycloid stays invulnerable.
- **The tracker never saw it while it was not an enemy** (dmgnum.cpp skips `!p.enemy`). With the classification
  rule the Health-delta tracker works: drops are events; a knockdown reads as a kill (Health <= 0 retires
  the entry) and the recovery or next stage is a first sighting, so no kill is marked while the pawn is in state
  `ControlX`.
- **Knockdown handling (built):** on the frame a pawn's Health reaches 0
  (alive in the previous walk, dead in this one) world.cpp reads its state once: `UObject.StateFrame` (+0x0C) ->
  `StateNode` (+0x14) (FindObjectField `Engine.dll!0x1003D3D0`, engine-map.md section 3), one guarded read, the
  state checked against the snapshot and its name against `ControlX`. A knockdown feeds the tracker as damage
  without a kill; a Health rise (stage reset, recovery) is no event; a tracked pawn alive but no longer hostile
  (its target moved off the player) leaves the tracker quietly instead of as a kill. Log: `world: <Class> Health
  reached 0 in state <State>: knockdown, no kill | kill` (16 per level), and `downProbes` / `downs` cumulative.

## Open questions

- Where the native `m_fDamageThreshold` demotion is applied (the Cycloid's 0.1 on the Duke entry did not move it
  below Neutral).
- Whether the pre-attack Cycloid should read as hostile (by class or by the `cycloid` Tag).

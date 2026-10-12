# Executions: when an enemy can be executed, how, and how to start one (build 244)

An
execution is a **Use** on an enemy kneeling in the AI state `ControlX` (a "finish me" stun that a lethal bullet or
melee hit causes with a 20% chance on Pig Cops, Enforcers and Assault Troopers; 8 s to act) or on a **frozen**
enemy whose class allows it. Both paths end in `dnControl_Execution.idle.PerformExecution`: kill credited to Duke
(`ExecutionDamage`), **ego refilled to the cap**, the `'execution'` stat event. Octababies, Octabrains and the
other aliens have no execution. The lever Auto-execute (autoexec.cpp) uses: **call `Used(player, player)` on the enemy through
ProcessEvent** once the game's own `CanBeUsedBy(player)` says yes and the enemy is within the Use reach: the same
function the Use key reaches, without needing the crosshair on it.

Method: read from the script packages and dnAI.dll/Engine.dll. `tools/ubc.py` over `extracted-packages/*.u`, a scratch tagged-property reader for class defaults (struct
and dynamic-array values parsed as nested tagged lists), `System\dnGame.int`; Ghidra MCP on project `dnf`
(dnAI.dll analysed for this note; plate comments on `dnAI.dll!0x1001B7C0`, `0x100301A0`, `Engine.dll!0x102706A0`;
saved).

## 1. When an enemy can be executed

### Two kinds

| kind | what makes it ready | classes (class defaults) |
|---|---|---|
| Finish-me stun | AI in state `ControlX` with `ControlXParms.MyControl` a `dnControl_FinishingMove`, `IsControlled` false, `bUsable` true | `DeathAnimChance` 0.2: `PigCop_Grunt`, `_DualPistol`, `_MachineGun`, `_Shotgun`, `_Grunt_RPG`, `Pigcop_Berzerker`; `Enforcer`, `Enforcer_Shield`; `AssaultTrooper_Grunt`, `AssaultTrooper_Captain` (+ `_WildRide`); their `DumbAI_*` copies. `PigCop_Captain`/`PigCop_RPG` have 0.0 |
| Frozen | `Pawn.FrozenState != 0` (`IsFrozen`) and `AIActor.AllowFrozenExecution` | the same families, all with `AllowFrozenExecution=True` (Captain and RPG Pig Cops included) |

- Every other AI has `DeathAnimChance` unset (0) and no `AllowFrozenExecution`: Octababy, Octabrain, octaking,
  Pregnator, EggPod, Rat (-1), EDF, civilians. `acOctababy`/`acOctabrain` carry no `DeathAnims` at all. Level
  designers can override both per spawner (`AIActorFactory.m_bOverrideDeathAnimChance`/`m_fDeathAnimChance`,
  `bOverrideAllowFrozenExecution`/`bAllowFrozenExecution`, read in `AIActorFactory.InternalSpawn`) or per script
  (`ScriptMethod_OverrideExecution`). So read the instance, not the class.
- Bosses use the same `ControlX` state with their own controls (`FinishMeControlType` = `dnControl_Cycloid_Battle*`,
  `dnControl_BattleLord_*`, `dnControl_AlienQueen_FinishingMove`, `dnControl_EnergyLeech_Boss_BattleFinish`,
  `DeathAnimChance` 1.0): Use starts a mash QTE (autowin.md section 1), not an execution. Excluded here.
- "Squish" (stat 79, `S_SQUISHEXECUTION`) is stepping on a shrunk enemy, not a Use; out of scope.

### How an enemy reaches the stun (dnAI.dll)

- A lethal hit runs `AIActor.DiedActivity` -> `ShouldUseDieOp`: true for `BulletDamage`/`MeleeDamage` (thrown
  objects: `ThrownObjectDeathAnimChance` 0.4), false for the railgun and steroid special kills.
- `AAIActor::CanUseOp_Die` (`dnAI.dll!0x1001B7C0`): with `DeathAnimChance` (+0x20E0) in (0,1) it rolls
  `rand()/32767` and plays a plain death when the roll is higher. `GetDeathAnimName` fills the finish-me anim from
  the controller's `DeathAnims[part]` (`acPigCop_Combat`, `acEnforcer_Combat`, `acAssaultTrooper_Combat`: front and
  back `Anim_DieFinishMe` -> `Anim_FinishMeIdle`, revive `Anim_FinishRevival`, **timeout 8.0 s**). For a non-boss it
  then sets `DeathAnimChance = 0` (**one stun per enemy**), unless the LevelInfo bool at +0x7BC bit 0 is set, which
  refuses the die op (code 0x68; the name of that bool was not resolved). It calls
  `CanUseState_ControlX(FinishMeControlType, FinishMeAnimName, FinishMeTimeout)` (script: spawns the
  `dnControl_FinishingMove` owned by the AI, attaches it, refuses when Duke cannot fit at its exit) and
  `LevelInfo.NotifyFinishingMove` (gate = now + `FinishingMoveInterval` 60 s). `LevelInfo.FinishingMoveAllowed`
  reads that gate but has no caller in any script package, nor an import in dnAI.dll or dnGame.dll: the 60 s gate
  is dead in this build.
- `ExitStateOp_Die` (`0x100301A0`) enters `ControlX` (`ExecuteState_ControlX`). `ControlX.BeginState` sets
  `bUsable = true`; `ExecuteControlX` plays the kneel loop and arms `TimeOutControlX` after `ControlXParms.Time`.
- **Not executed in time:** `TimeOutControlX` -> `ExitAIState(3)` -> `ExitStateOp_Die` status 3 ->
  `RaiseHealthToCap` and the revive animation: **the enemy gets up at full health.**
- **Shot while kneeling:** `ControlXParms.DieOnTakeDamage` defaults true, so `ControlX.TakeDamage` kills it as a
  normal kill (no execution, no ego refill).

### The ESP while an enemy is downed

The kneeling enemy reads Health 0 for the whole stun, so world.cpp would call it dead. A pawn that was hostile on the
previous walk and reaches Health 0 becomes a candidate (the state is still AnimX on that frame, so the state is no
test). It is downed while Health is 0 and
`ControlXParms.MyControl` is a `dnControl_FinishingMove`; `IsControlled` is logged but not a gate, so the enemy stays
drawn through the execution until MyControl clears. A candidate leaves on revive, on MyControl clearing after it was
seen (30 frames of grace before it is first seen) or when it leaves the walk. The ESP draws it as an enemy with an
"- EXECUTE" name suffix and no health bar; `w.enemy` stays 0, so aim and autoexec are unchanged. The log shows
`world: downtrace ...` lines for the first 8 candidates per level.

### The prompt, range and angle

- HUD: `ControlX.ShouldDrawHUDInfoUsePhrase` -> the control's; `UsePhrase` "Execute" (`dnGame.int`
  `[dnControl_Execution]`). Frozen: `AIActor.GetUsePhrase` returns the same phrase when `IsFrozen() &&
  AllowFrozenExecution`.
- Range is the Use target: `PlayerPawn.LookHitActor`, refreshed natively by `execNativeUpdateLookHitActor`
  (`Engine.dll!0x102706A0`) **every 5th call**: a crosshair mesh trace, kept when its hit distance is <= the float
  at `PlayerPawn+0xB18` (by its use and default most likely `Pawn.UseDistance` = 72; resolve it by name), else a
  second trace of that length with a 6.5 x (+0xA78) extent box. So the game's reach is ~72 units from the eye to
  the enemy's mesh, with the crosshair on it.
- No facing limit: `dnControl_Execution.ViewingAngleThreshold` -2 and `UseAngleThreshold` -1 never refuse;
  `AttachPawnSuccess` turns Duke toward the victim and the LERP moves him to it.

## 2. How the player triggers it (Use key)

`PlayerPawn.UseDown` (Exec): refused when `!CanPressUse()` (frozen, dead, `!IsActiveInGame()`, or the weapon has
`bCantUseActorWhileEquiped`) or `Level.bPaused` -> `ServerUseDown` (`bIUse = true`; a weapon pickup underfoot
defers 0.2 s) -> `ServerUse`: returns when `bGrabbing` or `bAttachedToUsableSomething`; `UpdateLookHitActor`;
`CanGrabActor(LookHitActor)` would grab instead; then **`LookHitActor.CanBeUsedBy(self)` -> `LookHitActor.Used(self,
self)`**.

- Stun: `AIActor.ControlX.CanBeUsedBy` (false if no control, already controlled, or expanding) ->
  `dnUsableSomething.CanBeUsedBy`: `UsableSomethingCanBeUsedBy` (pawn class, `TimeGate`, no current `User`, the
  weapon `CanDeactivate()`, the player has no `ViewMapper` remapper, view angle) and
  `InteractiveActor.CanBeUsedBy` (posture, physics, use angle, shrunk match, not switching weapons, user not
  frozen). `ControlX.Used` -> `MyControl.Used` -> `dnUsableSomething.AttachPawn`; when the player became `User` it
  sets `IsControlled`, `bUsable = false`, cancels `TimeOutControlX`.
- Frozen: `AIActor.CanBeUsedBy`/`Used` (global state) -> `CanPerformFrozenExecution` (player, `IsFrozen`, same
  shrink state, the level's `dnControl_FinishingMove` tagged `AIFinishMove`, found or spawned) ->
  `ExecutionVictim = self`, `AttachPawnSuccess(player)`.
- Then: `dnControl_FinishingMove.AttachPawnSuccess` sets `AIActor.WasExecuted` (the AI's death line becomes
  `Sound_Executed`); `AttachUserLERP` makes Duke `bNoDamage` until `DetachUserLERP` ends; the `idle` state plays
  Duke's `ExecutionAttack`, whose animation calls `PerformExecution` (no script caller in single player; the notify was not traced. The only script call
is `DukePlayer.Execute`, reached from `DukePlayer.Used` in MP with Role 4).

## 3. Levers

| rank | lever | keeps | risks |
|---|---|---|---|
| 1 | **ProcessEvent `victim.CanBeUsedBy(player)`, then `victim.Used(player, player)`** (state version resolves first) | everything after the key press: `ControlX.Used` bookkeeping, attach, animation, kill credit, ego, stat event, `WasExecuted` | skips `CanPressUse`, the pause and grab checks and the crosshair reach: the gate must replicate them (section 5) |
| 2 | Press Use: ProcessEvent `PlayerPawn.UseDown` then `UseUp` | the whole key path | acts on whatever the crosshair holds: a door, a pickup or a grabbable prop instead of the enemy; fires only when the player already aims at it within ~72 (no gain over pressing E); leaves `bIUse` held for a frame (pickup hold timer, the pool probe) |
| 3 | `MyControl.Used` or `AttachPawnSuccess` directly | the animation | skips `ControlX.Used`: `TimeOutControlX` stays armed and can revive the enemy mid-execution; not recommended |
| - | `DukePlayer.PerformExecution(victim)` | - | the multiplayer path (its own `dnControl_Execution`, no `WasExecuted`); not for SP |
| - | Making every kill kneel (`DeathAnimChance` 1) | - | writes engine memory the game writes only through spawners; a separate feature at most |

Situations, for lever 1:
- **Another execution running:** the player is attached (`bAttachedToUsableSomething`, `ViewMapper` set) and the
  victim's control has a `User`; both refuse. One victim per frame.
- **Cutscene, vehicle, turret, QTE:** a control or vehicle attaches the player or sets a `ViewMapper`;
  `CanPressUse()` covers `IsActiveInGame`; `Level.bPaused` covers menus and `ForcePause`.
- **Panel open:** the call needs no input; skip while the panel is open anyway so an execution never starts
  behind the menu.
- **Several enemies:** Duke is `bNoDamage` during the attach, but the others keep acting; the animation locks him
  for its length. A setting may limit auto-execution to one victim at a time (already guaranteed) and nothing more.
- **Frozen allies:** `CanPerformFrozenExecution` does not check the faction: gate on hostility (factions.md).

## 4. The reward

`dnControl_Execution.idle.PerformExecution`: `SentinelAddEvent(victim, 'execution', '' or 'Frozen')`,
`TraceFireHit(ExecutionDamage)` with `Instigator` = Duke (`GetBaseDamage` 1000), head bone destroyed, view shake and
rumble, `ProtectedEgoRecharge`, **`Ego = EgoCap`** (`SteroidEgoCap` on steroids), `RaiseHealthToCap`, the HUD
shown, `ExecutionSound`, `CleanupExecution`. Lever 1 runs all of it: the ego refill and kill credit are kept. PerformExecution
grants no ego-cap award. Steam: `DukePlayer.EAchievement` 4 `ACH_Executions` is awarded by script:
`idle.PerformExecution` kills with `TraceFireHit(..., ExecutionDamage, ...)` -> `DukePlayer.Killed` ->
`UpdateKillAchievements` -> `UpdateAchievementCounter(4, 20)` (20 kills; frozen kills are counter 6, 15). Both return early in
MP and while `bHasCheatedThisLevel` (progress.md section 1). Stats screen: `S_FROZENEXECUTION` (77), `S_SQUISHEXECUTION` (79). The auto-execution therefore
counts toward a real achievement: say so in the UI, as the QTE toggle does.

## 5. The Auto-execute rule

Per frame, on the game thread, while the toggle is on and the panel is closed:
1. Player gate: local DukePlayer alive; ProcessEvent `CanPressUse()` true; `Level.bPaused` false;
   `bGrabbing`, `bAttachedToUsableSomething` false; `ViewMapper` none.
2. Candidates from the existing AI walk (world.cpp), hostile by factions.md, within reach: horizontal distance
   <= `UseDistance` (read by name) + both `CollisionRadius` values, plus `FastTrace(victim, eye)` once the cheap
   checks pass. Then either `GetStateName() == 'ControlX'` with `ControlXParms.MyControl` IsA
   `dnControl_Execution` and `IsControlled` false, or `IsFrozen()` with `AllowFrozenExecution`.
3. `victim.CanBeUsedBy(player)` (ProcessEvent, bool) true -> `victim.Used(player, player)`; at most one per frame;
   latch the victim until it leaves `ControlX`.
4. Every name resolved by reflection (`AIActor.ControlXParms` struct `SControlX {ControlType, MyControl, AnimName,
   Time, IsControlled, DieOnTakeDamage, AssumeCanFit}`, `AllowFrozenExecution`, `WasExecuted`, `Pawn.FrozenState`,
   `Pawn.UseDistance`, `dnUsableSomething.User`, `dnControl_Execution.ExecutionVictim`); refuse the feature with a
   reason when one is missing, as autowin does.

Log lines (one each, capped per level): each enemy entering `ControlX` (class, distance, `ControlXParms.Time`,
`MyControl` class); each call (`CanBeUsedBy` result, distance, frozen or stun) and whether `MyControl.User` became the
player the next frame; the result per victim (`WasExecuted`, player `Ego` before and after, time to the victim's death,
or a revive); every refusal by its gate; the value read for `UseDistance` and the LevelInfo bool at +0x7BC once per
level.

## Open questions

- The name of the LevelInfo bool at +0x7BC (bit 0 refuses the die op).
- Whether `PlayerPawn+0xB18` is `Pawn.UseDistance` (72): its use and default say so; the panel resolves it by name.

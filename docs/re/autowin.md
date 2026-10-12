# Auto-win: QTEs and minigames (build 244)

Every DNF button-mash QTE is one class, `dnGame.dnControl_TapPowered`, won when its `UserLiftVal` climbs to 1 from the
player's Jump taps; the recommended lever is to **press Jump for the player every frame** (`PlayerPawn.JumpUp`
through ProcessEvent, the function the Jump key runs) while the player is the user of a TapPowered control in
state `idle`: the game's own success path, animations and timers. Boss finishers after the mash (field goal,
BattleLord finishing move) are plain animations with no fail state. Of the minigames, **pool, air hockey, pinball
and slots** each grant their ego award from one script function a ProcessEvent call can reach; video poker grants
award 1 on any paying hand but has no deterministic win, and Alien Abortion is a map-scripted game won by hitting
every alien (below); the whiteboard has no win, the valve puzzles grant no award. Recommended set:
QTEs; pool, pinball, slots, air hockey, poker, Alien Abortion.

Method: `tools/ubc.py` (bytecode) and a tagged-property reader over `extracted-packages/*.u` and `Maps\*.dnf`
(read in place); everything below is from the scripts and maps unless it says "test log". Names below are
`Class.State.Function` as the packages hold them.

## 1. QTEs (button mash)

### The class and its state (dnGame.u `dnControl_TapPowered : dnControl`)

- Subclasses (all the mash QTEs): `dnControl_Cycloid_BattleA/B/C`, `dnControl_BattleLord_BattleA/B/C`,
  `dnControl_AlienQueen_FinishingMove`, `dnControl_EnergyLeech_Boss_BattleFinish`, `dnControl_ATCaptain_WildRide`,
  `dnControl_PigCop_Grapple`, `dnControl_PregnatorFaceHump` (dnAI); `dnControl_WorkoutBench`,
  `dnControl_VentRip_CircularGymVent`, `dnControl_TapPowered_DoorBase` -> `dnControl_LiftDoor`, `dnControl_SplitDoor`
  (dnDecorations). The boss ones are spawned by the AI (`AIActor.FinishMeControlType`, entered from the knockdown
  state `ControlX`); doors, vents and bench are placed (doors in Map01/02/04/04B/06/11B/17, vents Map02, bench
  Map02/08/mydigs).
- State: `UserLiftVal` (float -1..1, default -1; bench -0.7), `LastTapTime`, `bSuccessfulTap`, `bLift`,
  `bForceDrop`; tuning `StartTapInterval`/`EndTapInterval`/`RateOfGain`/`RateOfLoss` (class defaults read: End 0.2,
  gain 0.08, loss 0.1; Cycloid gain 0.1; PigCop 0.5/0.3/0.05/0.2). `dnUsableSomething.PlayerUser` is the player
  using it; the player has `Pawn.bAttachedToUsableSomething`.
- The tap is the **Jump key**: `PlayerPawn.JumpUp` (Exec, no parms) sets `bIJump = false, bIJumpPressed = true`.
  In state `idle`, `Tick` calls `AddUserPower`, which consumes `PlayerUser.bIJumpPressed` (sets it false), marks the
  tap successful when it came within `MinTapInterval` = max(2*dt, lerp(alpha, Start, End) * TimeDilation) of the
  last one, and stamps `LastTapTime`. A successful tap adds `(MinTapInterval - TimeSinceLastTap) * RateOfGain * dt
  * 60` (doubled on steroids); otherwise `TimeSinceLastTap * RateOfLoss * dt * 60` is subtracted.
- **What decides success:** `UserLiftVal == 1 && LastValue < 1` (LastValue = the value at the start of that tick)
  calls `FullyLifted()`; the subclass's `idle.FullyLifted` sends `ControlEvent('Finish')` (Cycloid, BattleLord,
  AlienQueen, Leech, PigCop, Pregnator, vents, doors). Reaching -1 calls `FullyRelaxed`.
- **Failure:** the boss and grapple subclasses arm a timer on entering `idle` (Cycloid: `StrengthTapTimeout`, 6 s on
  Map00) whose `FailTimer` sends `ControlEvent('Fail')` -> `cycloid.ControlX.EndControlX_UserFailed` (recovery
  health, the fight goes on). `PregnatorFaceHump` damages the player on a timer until finished.
- Not mash QTEs: the "press Use" prompt that starts a finisher is the AI's `ControlX` state (timeout
  `TimeOutControlX` -> recovery health; Map00 counts ignored knockdowns in `Map00NoUseKnockdowns` for a hint).
  `dnControl_CycloidHumiliation_FieldGoal/EyeUrinal`, `dnControl_BattleLord_FinishingMove` and
  `dnControl_FinishingMove` (executions) play an animation with no fail branch.

### Levers

1. **Press Jump every frame (recommended).** While the control `IsA dnControl_TapPowered`, its `PlayerUser` is the
   player and `GetStateName() == 'idle'`, call `PlayerPawn.JumpUp` on the player through ProcessEvent once per
   frame. Each tap lands at TimeSinceLastTap 0 (AddUserPower stamps it in the same tick): the fastest possible
   masher, about `MinTapInterval * RateOfGain * 60` per second, so ~1.2-2 s from -1 to 1 with the defaults
   (estimated from the decode; the base class's StartTapInterval was not read), inside the 6 s Cycloid timeout.
   Each QTE logs its class, time to `Finish` and presses sent.
   Everything after is the game's: grunts, rumble, the lift animation grid, `Finish`.
   - Risks: a press outside the gate makes Duke jump, so check all three conditions every frame. Steroids double
     the gain (harmless).
   - **A press left pending cannot make Duke jump.** A test log: `QTE
     dnControl_Cycloid_BattleC ended: ... Jump still pending on the player: 1`; no stray jump was seen. The jump
     itself is `bIJump` (set by `JumpDown`, the key press; read by physics, `CalculatePhysics_Base3DMotion_ShouldFlyUp`,
     vehicles, `DukeMultiPlayer.Tick`). `JumpUp`, the release we call, *clears* `bIJump` and sets `bIJumpPressed`.
     The only reader of `bIJumpPressed` in every package is `dnControl_TapPowered.AddUserPower`, which consumes it.
     Native: `PlayerPawn.bIJumpPressed` is bit 0x1 of +0x1EC8 (the first bool after `GridYCached2`; bits 0x2/0x4 are
     `bReceivedServerPositionUpdate`/`bReplayingSavedMoves`, as `ClientUpdatePosition` 0x1026D849 uses them). No
     Engine.dll or dnGame.dll instruction tests bit 0x1 there; the dword is only copied whole (`APlayerPawn` copy,
     `operator=`) or diffed for replication (`GetOptimizedRepList`); dnAI and dnVehicles hold no `+0x1EC8`
     operand. So a pending press is at most one tap credited to the next TapPowered control the player uses (an
     early unsuccessful tap, its loss clamped at -1). No change.
2. **Call `FullyLifted` through ProcessEvent** on the control in state `idle` (the state version resolves first):
   instant `Finish`, but the lift animation jumps from wherever it was. Fallback if lever 1 shows a stray jump.
3. **Write `UserLiftVal`:** not recommended. Success is edge-triggered on the tick's starting value, so a 1 written
   between ticks never fires `FullyLifted`, and a missed tap pulls it back down.

### Cutscene state and achievements

- Lever 1 changes nothing the game does not change itself, so the scripted sequences after the QTE (Cycloid field
  goal, `Stage*_CompleteEvent`s, BattleLord stages) run as normal.
- **The cheat gate is real.** `DukePlayer.AmICheating` is
  `LevelInfo.InstaGib || bGodMode || bInfiniteAmmo`, which the Extras menu sets (`UDukeSceneExtraSettings.NavigateBack`
  also sets `PlayerPawn.bHasCheatedThisLevel`). While that flag is set, `NoteDiscoveredEgoAward`,
  `UpdateKillAchievements` and `UpdateAchievementCounter` return early: a minigame win grants no award and counts no
  achievement. Every autowin win line logs `cheated this level <0|1|-1>`. Otherwise winning a boss QTE leads to the normal
  `ACH_CycloidDefeated0`, `ACH_BattlelordDefeated0/1`, `ACH_AlienQueenDefeated`, `ACH_LeechDefeated` and the boss ego
  TriggerPlayers. Steam achievements are real: the feature should say so.

## 2. Minigames and puzzles

`GivePermanentEgoCapAward(id)` grants only while the player's `EgoCapAwardsEnabled` bit is set (a level switches
it: Map00 has `TriggerPlayer_Ego_Enable`/`_Disable`; progress.md section 1), so an auto-win in a disabled stretch
grants nothing, as in normal play.

| game (maps) | how a win is decided and granted | script lever | support |
|---|---|---|---|
| Pool, `Furniture_Generic_PoolTable_A` (Map00 penthouse, Map09 x2, Map_mydigs) | Map logic, not the table: pocket `KBoxSensor`s (`OnlySenseTagged PoolTable_Ball`) count balls into a `Variable`; at 0 `Dispatcher_Penthouse_PoolTable_Win` plays the win dialogue, whose end fires `Penthouse_PoolTable_Ego` = the table's `SE_WinEgoCapIncrease` -> `TriggerFunc_WinEgoCapIncrease` -> award 23 (PoolWin). The cue ball in a pocket is a scratch (sensor off, reset). Balls are hit with Use, not a cue: see "Pool: how a ball is hit" below. | ProcessEvent `TriggerFunc_WinEgoCapIncrease` (no parms) on the table, on the first ball hit in a level: the award without the twins' cheer. | yes |
| Air hockey, `AirHockeyTable` + `dnControl_AirHockeyPlayer` (Map09, Map_mydigs) | The native table counts goals (`PlayerInfo[i].Score`, `WinningScore`) and calls event `GameWon(PlayerIndex)` -> `ControlEvent('WonGame')` -> `GameOverWon`: award 5, achievement 23 (`ACH_AirHockeyWon`) only when the other score is 0, `PlayerWinEvent`. | ProcessEvent `AirHockeyTable.GameWon(index of the player's PlayerInfo)` while playing. The native table state (puck, scores) is not reset by it: runtime check first. | yes, test first |
| Pinball, `dnControl_Pinball` (Map02, Map09, Map_mydigs) | `AwardPoints(Points)` adds `Points * multiplier`; above `highscore` -> award 9 (PinballHighScore) and `HighScoreEvent` once; >= 1,000,000 -> `Achievement` (`ACH_PinballScore`). | ProcessEvent `AwardPoints(N)` while the player uses it (`User` must be the DukePlayer). | yes |
| Slots, `dnControl_CoinOp_SlotMachine` (Map04B x11, Map09 x4, Map_mydigs x2) | Rotors stop on random symbols in `Tick`; a payout calls `Win(basePayout)` (1, 5, 10, 25, 40, 80, 800) -> sound, award 2 (SlotsWin) to `LastUser` for any amount; the amount only picks the payout texture (settled with the fixed decoder). | ProcessEvent `Win(amount)` once the player has played it (`LastUser` set). | yes |
| Video poker, `dnControl_CoinOp_Poker` / `VideoPoker` (Map09 x3, Map_mydigs) | `VideoPoker.UncoverCallback` after the draw: a paying hand (`Payout() * CurrentBet > 0`) grants award 1 (PokerWin) to `CurrentInstigator` (progress.md section 7). The cards are random: no deterministic win (below). | ProcessEvent `DukePlayer.GivePermanentEgoCapAward(1)` on the player while it uses a machine: the grant a paying hand makes. | yes |
| Alien Abortion, `Switches_Generic_PushSwitch_B` Tag `SwitchAlienAbortReset01` (Map09, Map_mydigs) | Map triggers: seven waves of 12 pop-ups, each hit +1 (the mouth alien +3); at the end `CheckGameFinal` copies the score into `PerfectGameVar`, and `>= 85` fires `DisExpertScore` (`Dispatcher___164`) -> `AAWinEgoTriggerPlayer` (award 29) and `ACH_AliensAborted`; `< 85` (or `< 51` after wave 4) fires `DisEndGame`. A perfect game scores 99. Map_mydigs has the game but no `AAWinEgoTriggerPlayer` or `Abortion_ACH_Dispatcher`: winning there grants nothing. | Play it by the game's own hit: `GlobalTrigger('DisPopDown0N')` on each pop-up once it waits on its last event (below). | yes |
| Whiteboard, `dnControl_Whiteboard` (Map00, 09, 16B, 19, 20) | No win: drawing with a pen (not the eraser) in `Drawing.Tick` awards achievement 28 (`ACH_WhiteboardDrawing`) once. No ego award. | none needed | no |
| Workout bench (Map02, 08, mydigs) | A TapPowered QTE: `FullyLifted` with `LiftWeight >= 600` -> awards 3 and 4. | QTE lever 1 | via QTEs |
| Basketball hoop, `Settings_Workout_Basketball_Hoop` (Map02, 09, 11B x2, mydigs x2) | `Score(Other)`: a `Details_Balls_BasketBall` whose `Instigator` is Duke (or a shrunk Duke) -> award 24. | needs a ball actor as the parm | later |
| Valves, `dnControl_Valves_*` (Map16B, 19B, 20B, 21); `dnValvePuzzle` (no instance in any map) | Per-map trigger chains (`RightEvent`/`LeftEvent`, gauges); `dnValvePuzzle.UpdateValvesAndGauges` fires `CompletionEvent` when the master gauge reaches `DesiredMasterGaugeValue`. No ego award. | no generic call | no |
| Keypad (Map09), nut-and-bolt (Map16B), cranes (Map06/06B/18) | Map-scripted progress, no award. | none | no |

### Pool: how a ball is hit

DNF has no cue play. A ball is struck by pressing Use on it; the cue is a physics prop. `Pawn.CarriedActor` never
holds a `Details_Generic_PoolCue` in normal play, so an edge keyed on a carried cue never fires.
- `Details_Generic_PoolCue` (: `Details_Generic` : `aFinalDecoration` : `dnDecoration`) declares no function; its
  defaults are a karma prop (Physics 18, Mass 15, `sm_class_decorations.PoolTable.PoolCue`). No carry pose or
  weapon form exists for it (no `CarryPose*Cue*` in any package).
- `Details_Balls_PoolBall`: `UsePhrase` "Hit Ball" (`System\dndecorations.int`), `bForceUsePhrase`, and
  `DecoActivities_Default` with activity id 3 (Used) running `DA_KImpulse_Details_Balls_PoolBall_Used` (a
  `DecoActivities_KarmaImpulse`) and a HUD info. `dnDecoration.Used` runs `DecoActivity(3)`.
- The press: `PlayerPawn.UseDown` -> `ServerUseDown` sets `bIUse = true` and calls `ServerUse`, which updates
  `LookHitActor` and, if `CanGrabActor` refuses it, calls `LookHitActor.Used(self, self)`.
- Table: `PoolBalls` (found as mounted actors), `RackTable_EightBall`/`NineBall`, `TriggerFunc_WinEgoCapIncrease`
  (grants award 23 to `Level.GetLocalPlayerPawn()`). No ball or pocket script on the table itself.

**As built (autowin.cpp `pool_frame`, autowin.h `aw_pool_hit`, `aw_pool_latch`):** the edge is Use held
(`PlayerPawn.bIUse`) while `LookHitActor` is a `Details_Balls_PoolBall`, or a ball or the cue carried. The first hit
finds the level's table (one object-table pass), latches it and calls `TriggerFunc_WinEgoCapIncrease` once per level
(the award is permanent). Inputs logged, 32 lines per level: `autowin: pool probe: Use pressed at <Class> (<kind>),
carrying <Class>` on each Use press and `autowin: pool probe: carrying <Class>` when the carried object changes; then
`autowin: pool: hit on <Class>; ...` and `autowin: pool won on a hit: ...`.

### Video poker

No game-own action makes a hand pay, so the lever is the grant a paying hand makes. The game
(`VideoPoker`, dnGame.u, one shared actor that every `dnControl_CoinOp_Poker` finds or spawns): `AddToBet` (state 0)
sets `CurrentInstigator`; `DrawPressed` 0 -> 1 uncovers the deal (`DealCards`: five `RandomCard`s); the callback moves
1 -> 2 (hold); `DrawPressed` 2 -> 3, `UncoverCallback` case 3 -> 4 runs `DealDrawnCards` (a `RandomCard` per card not
held); case 4 -> 5 settles: `i = Payout() * CurrentBet`, and if `i` is not 0, `DukePlayer(CurrentInstigator)
.GivePermanentEgoCapAward(1)`. In order: (1) the settling function exists (`UncoverCallback` case 4)
but pays only on the random hand; (2) no hold or draw is deterministic: both deals are `RandomCard`; `Activate(Key)`
restores a machine's saved `Triggers[Key]` hand, not a win. (3) is left: `GivePermanentEgoCapAward(1)` (native,
`AwardID` a ByteProperty) on the player through scall. `NoteDiscoveredEgoAward` raises the cap only for an award
not yet found, so a repeat is a no-op. Cards and the payout table are never written.

**As built:** kind `dnControl_CoinOp_Poker` on `Pawn.InteractiveDecoration`; ready once the player uses it; one call
per use (`aw_use_step`). Line: `autowin: video poker won on use (the award a win grants):
GivePermanentEgoCapAward(1) on <player> -> called; EgoCapAwardsEnabled 1, cheated this level 0` (the native
grants nothing while `EgoCapAwardsEnabled` is 0 or the cheat flag is 1).

### Alien Abortion

Read from Map09.dnf and Map_mydigs.dnf in place, with Engine.u decoded. The win is the game's end check, not an event the player triggers, so the lever is to play it: hit
every alien with the game's own hit, and the map's own logic fires `DisExpertScore` at the end (about 2.5 minutes).
- Start: the switch's `SwitchUserStates[1].Event` `DisRound1` (`Dispatcher___158`) disables the switch
  (`TogAlienAbortSwitch01`, "Game in progress..."), sets the score to 1 and starts wave 1. Waves: `PeriodicTrigger`s
  `PerTrigRound1..6` (2.0, 1.625, 1.375, 1.25, 1.25, 1.125, 0.75 s) fire `RandomPopUp` (a `RandomDispatcher`, pick
  once over 12 slots: 01-05 twice, 03 a third time, 06 once) -> `DisPopUp0N`. Each pop-up dispatcher: `KeyDisUp0N`,
  `TrigSmkFaceReset`, `WackModifyRound` (+1 to `WackRoundVar`; at `WackStopVar` 13 the `RoundRobin` `RobinDoneNext`
  starts the next wave), `ScreenReset`, then after ~1.1 s `KeyDisDown0N` (gone, unhit).
- The hit: `Trigger___35` (LookUse, tags `PopUp01..06`) on Use fires `DisPopDown0N` through `Trigger.TriggerTarget`
  = `GlobalTrigger(Event, Other.Instigator, Other)`. `DisPopDown0N` fires `ResetDisPopUp0N` (the pop-up's `ResetTag`,
  registered as `EndDispatching`: `KeyDisDown` never fires), `VarModScore` (+1; three times for 06), and
  `VarModPerfectScoreTracking` (13 in a wave -> `DisIsPerfect`).
- End: after wave 4 `DisBonusCheck` copies the score into `BonusVar`: `< 51` -> `DisEndGame`; `>= 51` -> three bonus
  waves, then `DisBonusPart3` -> `CheckGameFinal` -> `PerfectGameVar`: `>= 85` (`Variable___52`, type 3 =
  `>=` in `Variable.TestValue`) -> `DisExpertScore`; `< 85` (`Variable___53`) -> `DisEndGame`. Every alien hit:
  1 + 7 x 14 = 99, the score `Variable___63` cheers. `DisExpertScore` fires `AAWinEgoTriggerPlayer` (`TriggerPlayer___9`,
  award 29 to every `DukePlayer`) and `Abortion_ACH_Dispatcher` (once), then re-enables and resets the switch.
- Rejected: triggering `DisExpertScore` at the start. It wins at once, but nothing it fires stops the waves: they run
  on, end in `DisEndGame` ("aliens win") at wave 4, and the switch it re-enables about 12 s later can start a second game
  over the first (its `PerTrigRound1` toggle then turns a wave on twice).

**As built:** the Start press is Use held while `LookHitActor` is a `Switches_Generic_PushSwitch_B` whose Tag is
`SwitchAlienAbortReset01` (`aw_aa_kind`). It finds the level's `Dispatcher`s by Tag (`DisPopUp01..06`,
`DisExpertScore`, `DisEndGame`; one object-table pass per level) and begins one game (`aw_aa_step`). Each frame of the
game the eight dispatchers' `bDispatching`, `Index` and `OutDelays` count are read in one guarded pass, at
offsets resolved once per level from the `Dispatcher` class (a miss refuses the game once). When a pop-up waits on
its last event (`Index == OutDelays.Num() - 1`, where `Dispatch` ends; so `WackModifyRound` has fired) the player calls `GlobalTrigger('DisPopDown0N', player,
player)` once (`aw_whack_step`). The game ends when `DisExpertScore` or `DisEndGame` starts dispatching, or after 30 s
of in-level time with no alien up. Lines: `autowin: Alien Abortion started at Switches_Generic_PushSwitch_B (..):
the game's own hit on every alien (...); pop-ups found 6/6, DisExpertScore found, DisEndGame found, award 29 TriggerPlayer here` and
`autowin: Alien Abortion game over (the game's own hit on every alien): won: DisExpertScore started (award 29's TriggerPlayer, ACH_AliensAborted); 84 hit(s),
0 refused, 150.0 s; cheated this level 0`; on a map without the TriggerPlayer the result says "no award on
this map".

## What the panel does

- **QTEs:** lever 1 (per-frame `JumpUp` on the player while it uses a TapPowered control in `idle`), with one log
  line per QTE (class, time to `Finish`, presses sent) to measure the estimate and catch a stray jump.
- **Minigames:** one "win" action per supported game on the instance the player is using: pool
  `TriggerFunc_WinEgoCapIncrease`, pinball `AwardPoints`, slots `Win`, air hockey `GameWon` (after a runtime test),
  poker the award grant, Alien Abortion the game's own hit on every alien.
  All are ProcessEvent calls on the game thread; none writes engine memory.
- The UI states the achievement consequence (section 1).

## Open questions

- The pool win on a ball hit and the Alien Abortion run (`autowin: pool won on a hit`, `Alien Abortion game over`)
  have not been seen in game.
- Air hockey: `AirHockeyTable.GameWon` does not reset the native table state (puck, scores); a runtime check comes
  first.
- The QTE mash time (~1.2-2 s from -1 to 1) is an estimate from the decode; the base class's `StartTapInterval` was
  not read.

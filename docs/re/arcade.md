# Arcade machines: Nuke Tank (DLC) is the only playable cabinet; it is map-scripted and Arcade autoplay plays it legitimately

The only playable arcade game in the base game and the DLC is **Nuke Tank**, the cabinet
`dnControl_ArcadeMachine___1` (Tag `NukeTankArcadeMachine`, DLC03_Map03 (-3355, -2447, -160)). It is not native and not
the script helper: the game is about 300 map actors (Dispatchers, Variables, KeyframeDispatches, TriggerSpawns) on a hidden
set near (-7800, -1200, 100); the machine only turns the player's Left/Right/Fire into map events. A win awards DLC ego 1
(`ArcadeGame`, +3 cap) and nothing else. Every input is a ControlEvent the game's own binds produce, and everything a bot must see (the tank, five enemy
"souls", the boss, the score Variables) is a tagged actor, so Arcade autoplay plays it legitimately (section 7). The
award chain is recorded only to say what a completed game does. The BS3000 cabinet (Hedonist 62) has no game; the base
game has no playable cabinet. The tank, the invaders, the shots and the rule that ends a game (any sixth landing) were
measured in a traced game (section 7).

Method: the script packages, class defaults and the instances of DLC03_Map03.dnf (read in place), plus the trace of
section 6.

## 1. The machines

| machine | where | playable? |
|---|---|---|
| Nuke Tank, `dnControl_ArcadeMachine___1`, Tag `NukeTankArcadeMachine` | DLC03_Map03 (-3355, -2447, -160); screen `StaticMeshActor___52` Tag `NukeTankScreen` shows a camera the map switches between title, stage-change, game and game-over views | yes |
| BS3000 cabinet, `dnDecoration___0` (exact `dnDecoration`, `bUsable`), Tag `BS3000_ArcadeMachine` | DLC03_Map03 (-3263, -2262, -160), 190 u from Nuke Tank | no game: `Trigger___31` (`BS3000AchTrigger`, TriggerType 6 look+use, once) fires `Dispatcher___97`: `ACH_Chauvinist` (62) after 3.5 s. Nothing to autoplay |
| `dnControlHelper_ArcadeMachineGameShooter` (+ `...ShooterDuke`, `_EnemySuper`, `_PigCop`, `_BombBall`, `...Proj1`, `...LifeLine`, `dnControlHelper_ArcadeMachineScreen`) | DLC03_Decorations.u only | dormant: no instance in DLC03_Map03 (`grep -a` of that map for `ArcadeGameShooter` is 0, no other map has the class). An unused earlier Nuke Tank (shmup, 3 lives, 30 s, `GameClear` -> `GivePermanentEgoCapAward_DLC03(1)`) |
| Basketball hoops, `Settings_Workout_Basketball_Hoop` (Map02, 09, 11B x2, mydigs x2; ChallengeTag `u-arcadehoops` in mydigs) | base | a physics toy (award 24, autowin.md); not a cabinet. A bot would be a ballistic throw with a carried `Details_Balls_BasketBall`: a separate study |
| Rest of the base | Map11B `ArcadeMachines` is a hidden static mesh and `Arcade*` tags are an AI combat scene; Map00 `Videogame` tags are TV camera effects; DM-Hollywood (MP) has `Hollywood_Arcade0x` props. Pool, air hockey, pinball, slots, poker, Alien Abortion are in autowin.md and minigame-autoplay.md | no other playable cabinet |

## 2. How Nuke Tank plays

- **Control class:** `dnControl_ArcadeMachine` (Use enters `AttachUserLERP`; `idle` is the play state). `idle.ControlEventInternal`
  reads the `dnControl.EControlEvent` numbers pinball uses: 14/15 Fire down, 16 up; 17/18 Melee down, 19 up (Fire and Melee
  exclude each other); 20/21 Forward down, 22 up; 23/24 Backward down, 25 up; 26/27 Left down, 28 up; 29/30 Right down, 31 up.
  `DetermineOutEvent` turns the booleans into `GlobalTrigger` of the instance's event names, once per edge. Instance wiring:
  Left -> `MoveTankYPos`, Right -> `MoveTankYNeg`, any stick release -> `StopTankY`, Fire -> `FireRocketRelay`. Forward/Backward
  have no event. `Tick` also drives the player's and the cabinet's stick animation grid (cosmetic). `ReuseTime` 2.0.
- **Start flow:** the end of `AttachUserLERP` fires `StartArcGameDispatcher` (reset lives and score, title screen, enable the start
  button). Menu flow is Fire presses: `FireRocketRelay` also feeds relays to `PlayGameDispatcher` (while the start button is
  enabled), then `AdvanceScreenRR` (two instruction screens -> `InfScreenEndDispatcher`), then `FireRocket` in play; each relay
  is gated by the `Enable/Disable...Input` events. `DetachUserLERP` fires `EndArcGameDispatcher`.
- **Playfield:** hidden set, X about -7900..-7700. Five lanes at Y = -1088, -1152, -1216, -1280, -1344 (proximity triggers
  `NukePlayerTriggers`, radius 24). The tank is `TankAvatar` (StaticMeshActor ___17, Z 120) with a Barrels actor
  `TankAvatarSoul` mounted on it. Left/Right are `KeyframeDispatch` `MoveTankYPos/Neg` (Rate Y +-96 u/s, Style 5, stopped by
  `StopTankY`): Y moves at 96 u/s while held, lanes are 64 u apart, no snap. Fire: `FireRocket` = two TriggerSpawns mounted on
  the tank (a `dnGame.dnRocket` tagged `BitBullet`, and a `dnRocket_AssaultTrooper_Laser`), LifeSpan 0.2 s, pitch up;
  `RocketHit` (`TriggerDestroy`) removes the bullet on a hit, `RocketKillerTrigger` / `KillRocketMisses` clear strays.
- **Enemies:** five `dnDecoration`s `Enemy01Soul`..`Enemy05Soul` (___2,3,4,5,7; one per lane, home Z about 432), `Bomb01Soul`,
  `Bomb02Soul` and the boss `BigBadSoul` (___6, Health 99, reset to 300). `MoveEn0NStart` -> relay (only if `Enemy0NIsHome` and not
  attacking) -> a `KeyframeDispatch` lowering the enemy in Z (rates -8 and -32 appear in the set; descent measured at 32 u/s, section 7) to touch-down near Z 92 (measured 98). Looping dispatchers `StageOne/Two/ThreeLoopDispatcher` (1 s) pick a lane via
  `ChooseEnemyS1/2/3` (stages 2-3 add bombs). A soul's destruction level (`HealthReq` 99, `bSurviveDeath`) fires
  `Enemy0NDestroyedDispatcher`: hide, `AddToScore`, explosion, send home, `ResetEnemy0NHealth` (100), show.
- **Hit and lose:** touch-down fires `En0NTouchDown`, a relay enabled only while the tank is in that lane (`YesNukePlayer0N`,
  cleared by `NoNukePlayer0N`): `En0NNukeDispatcher` -> `NukePlayer`: flicker, death sound, `UpdateLifeCount` (RoundRobin hides
  `Life02Tank`, `Life03Tank`, then `GameOverDispatcher`), input off for 1.5 s. So an enemy hurts only if the tank is in its lane
  at touch-down; the boss reaching the bottom (`BigBadTouchDown`) is an immediate game over. Game over restarts at the title
  after 6 s.
- **Score and stages (Variables):** each kill adds 100 to `Stage01Score`, `Stage02Score` and `BigBadSpawnScore` at once
  (`TotalScore` never moves). `Stage01Score` 1000 -> phase 2 (10 kills); `Stage02Score` 2000 -> phase 3 (20 kills); `BigBadSpawnScore`
  2500 -> boss (25 kills; it descends and moves in Y at 192 u/s, `StartBigBadAttackDispatcher`); boss destroyed ->
  `AddToScoreBigBad` +1000 to `Stage03Score` (comparison 1000) -> `WinGameDispatcher` (once). 25 kills plus the boss.

## 3. What a completed game awards

- DLC ego 1 `ArcadeGame` (cap +3): `WinGameDispatcher` (`Dispatcher___34`, once) -> `HackAwardEgo` (`TriggerDelay___46`, 1.5 s) ->
  event `ArcadeEgoTriggerPlayer` = `TriggerPlayerDLC03___0` (`EgoCapAwardsDLC03` [1]) -> `GivePermanentEgoCapAward_DLC03(1)` on
  every `DukePlayerDLC03` (no check of who uses the machine; returns on `bHasCheatedThisLevel` and MP). The chain starts at the win; using
  the machine alone grants nothing.
- Hedonist 62 belongs to the other cabinet (look and use), no play. No achievement, counter or stat for Nuke Tank.
- The existing auto-win has no row for it (`src` references the cabinet only in `egoesp.h` as the ego-1 marker). The bot is
  therefore the only way to a win without cheating the chain.

## 4. What a bot reads (Nuke Tank)

**Readable by name** (guarded reads, one Tag lookup per session, `Actor.Location`): `TankAvatar` / `TankAvatarSoul` Y (lane),
`Enemy01Soul`..`05Soul`, `Bomb01Soul`, `Bomb02Soul`, `BigBadSoul` (Z = height, Y = lane, `bHidden`, `Health`), `Life0NTank`
`bHidden` (lives left), the `Variable` actors `Stage0NScore` / `BigBadSpawnScore` (property `Variable.Value`), and the
machine's `User` plus `Left/Right/Fire` booleans (state: `idle`). Slow targets, five lanes, no physics, a deterministic
win after 25 kills and the boss.

## 5. What a bot presses

Fire: `PlayerPawn.DnControlFireEnable/Disable` (`bIDnControlFire` -> `HandleEvents` -> `ControlEventInternal(14+type)`).
Left/Right have no exec: the remapper's `HandleEvents` path the pinball plunger uses (minigame-autoplay.md section 7),
event base 26 (Left) / 29 (Right); details in section 7. Never release a press the player holds. Nothing is written: no
Variable, `GlobalTrigger`, ego or rocket spawn by us.

The game runs in the normal tick, so a Present-rate hook is enough: the tank moves 1.6 u per frame at 60 fps, a lane
change is 0.67 s, enemies fall for seconds. The stop is closed loop on `TankAvatarSoul.Y` (release does the stop; no snap).

## 6. The Nuke Tank trace (instrument; reads only, presses nothing)

`arctrace.cpp` records whenever the player's `Pawn.InteractiveDecoration` has `dnControl_ArcadeMachine` on its class
chain (not in multiplayer). A **session** is one use of the machine (attach to detach; game over keeps the player
attached, so one session can hold several games); it also ends on a level change. Output:
`System\NukePanel\arcade-trace.csv`, with pinball's writer discipline (minigame-autoplay.md section 6): the last launch's
file kept as `arcade-trace.prev.csv`, a 64 KB buffer written at most once a second and at session end, 200,000 rows a
session (then one `cap_reached` row), past 50 MB a new file (one `.old` kept, the header rows written again), a file
that cannot be renamed never truncated (`arcade-trace-<tick>.csv` instead).

**Finding the actors:** the Tags go through lvnames (`arcade` and `arcade dispatchers` lines in nukepanel.log); one
object-table pass per session takes this level's Actors whose `Tag` is a key (the first of each) and every
`Dispatcher` with a dispatcher Tag (`WinGameDispatcher` has two, `___34` and `___41`). Offsets are resolved by name
per actor: `Location`, `bHidden`, `Health` (RenderActor, float) and, on the score Variables, **`Variable.Value`**
(IntProperty, Engine.u; also `ComparisonValue`). Lives: `Life01Tank`..`Life03Tank` not
hidden (from the map: all three start shown).

Each row's first field is its kind:
- **`H` header rows** at session start: `H,session` (number, map, control class, the machine's Tag, `nukeTank` 1 when
  it is `NukeTankArcadeMachine`, start time, frame rate, tagged actors found of 19, dispatcher tags found of 36 and
  their objects, properties unresolved, game speed); `H,actor,<tag>,resolved,0|1,...` for every tagged actor
  (object, class, Location, bHidden, Health); `H,variable,<tag>,...,property,Value,offset,..,value,..,ComparisonValue,..`
  for `TotalScore`, `Stage01Score`..`Stage03Score`, `BigBadSpawnScore`; `H,dispatcher,<tag>,kind,<kind>,<value>,found,<n>`
  for each watched Dispatcher Tag; `H,lanes` (the five enemy souls' Y at session start); `H,processevent`;
  `H,columns` twice.
- **`F` frame rows**, one per presented frame while not paused: `frame,gameTime,ms,dtMs`, the tank
  (`TankAvatarSoul` x, y, z, bHidden), `lane` / `laneOff` (the transform: the nearest enemy lane to the tank's Y, 0 =
  `Enemy01Soul`'s, and the tank's Y minus that lane's), then x, y, z, bHidden, Health for `Enemy01Soul`..`Enemy05Soul`,
  `Bomb01Soul`, `Bomb02Soul` and `BigBadSoul` (`e1..e5`, `b1`, `b2`, `boss`), `lives`, the five Variables' `Value`,
  the machine's `Left`, `Right`, `Fire` (what the control received) and the pawn's `bIStrafeLeft`, `bIStrafeRight`,
  `bIDnControlFire` (what the keys hold). An actor or Variable that did not resolve leaves its fields empty.
- **`E` event rows** (`frame,gameTime,ms,event,value,detail`). Polled each frame: `input` (a pawn flag edge; value the
  control event it raises: 14/16 fire, 26/28 left, 29/31 right), `machine` (an edge of the machine's Fire/Left/Right),
  `hidden` (a soul's or the tank's bHidden edge), `hit` (a soul's Health fell; value the new Health, detail
  `<tag> <was> -> <now>`), `health_reset` (it rose), `lives` (value the Life tanks shown), `score` (a Variable's
  Value changed; value the delta, detail `<tag> <new>`), `state` (the machine's `GetStateName`), `session_end`. From
  the Dispatchers (`bDispatching` rising, or `Index` going back while up): `menu` (0 StartArcGame, 1 PlayGame,
  2 InfScreenEnd, 3 EndArcGame), `attack` (MoveEn0NStart 1-5, MoveBomb0NStart 6-7), `touch_down` (En0NTouchDown
  1-5, BigBadTouchDown 8), `kill` (Enemy0NDestroyed 1-5, Bomb0NDestroyed 6-7, BigBadDestroyed 8), `life_lost`
  (En0NNuke 1-5, NukePlayer 0), `stage` (2, 3), `boss` (StartBigBadAttack), `win`, `game_over`; detail the Tag. A
  dispatch that starts and ends between two frames is not seen: the score, lives, hit and hidden rows are the record
  that does not depend on it (the summary shows both counts). Through the ProcessEvent slot: `exec`
  (`DnControlFireEnable/Disable` on the player, 1/0) and `control_event` (`ControlEvent(N)` on the machine; -1 from a
  state's override). The key path is script to script, so those may never pass the slot.

One `arcade:` log line per session: why it ended (left the machine, level changed), real and game
duration, game speed and frame deltas, kills by dispatch (enemies, bombs, boss) and hits, TotalScore and its gain,
the stage scores, lives lost by dispatch and by the Life tanks, the stage reached, boss seen, wins, game overs, the
other dispatch counts, input/machine/hidden edges and score rows, the ProcessEvent counts, rows written and dropped,
flushes, the longest write and faults.

## 7. A traced game and Arcade autoplay

**The traced game** (`arcade-trace.csv`, cp02 = DLC03_Map03, Game speed on at 1.60x: 32.1 s, 51.5 s of
game time; 8 kills, game over in phase 1). All 19 tagged actors and 36 dispatcher tags resolved. Times are game time.
It plays like a dumbed-down Space Invaders: invaders descend straight down five lanes, nothing shoots back, and the
real threat is any invader landing.

| quantity | measured | from |
|---|---|---|
| lanes | Y -1088, -1152, -1216, -1280, -1344 (the souls' Y); the lane triggers `NukePlayerTriggers` (Trigger___2..6) sit at the same Y, radius 24, proximity on `TankAvatarSoul` | trace, map |
| tank | 96.5 u/s median while Left or Right is held (frames 80-100); Left is +Y; it moves from the frame of the press edge and stops dead in the frame of the release (no coast, no snap); it stops at -1076 (lane 0 + 12); starts at -1212 | trace |
| enemies | appear at Z 320 (from home 432), fall at 32 u/s (31.6-32.0), touch down at Z 98 after 6.95 s. One hit kills (Health 100 -> 97, `HealthReq` 99); a hit soul keeps falling ~1 s at 97, then goes home at 100 | trace |
| spawn cadence (phase 1) | attacks start on the 1 s `StageOneLoopDispatcher` beat; 16 starts 1.0-3.0 s apart (5.9 once: the lane drawn was not home); at most 4 live at once | trace, map |
| shots | 12 presses in play, 8 hits. Hits had the tank 0.2-7.1 u off the lane at the press, misses 7.5-13.2: tolerance ~7. Press to hit 0.10-0.14 s at Z 227-259 (0.03 s at Z 164): ~900-1000 u/s from Z ~138; bullet LifeSpan 0.2 s. No cooldown but the input gate. The player held Fire 68-102 ms, presses 0.46-2.5 s apart | trace, map |
| bombs | none in phase 1. Map: `MoveBomb0NStart` puts a bomb at (Y -1022 / -1410, Z 318), `MoveBomb0NA` sweeps it to (-1398 / -1029, Z 107) in 4.5 s (~84 u/s across, ~47 down), then home. No trigger, relay or dispatcher takes a bomb's touch or landing: harmless. A kill feeds `AddToScore` like an enemy's | map |
| what kills | an enemy touching down with the tank inside its lane's trigger: `En0NNukeDispatcher` -> `NukePlayer` (flicker, input off 1.5 s) -> `UpdateLifeCount` (`Life01Tank` hides at the game's start, then `Life02Tank`, `Life03Tank`, then `GameOverDispatcher`). **Every touch-down, in any lane,** also feeds `UpdateResourceCount` (`HideHumanRes01..05`, then `GameOverDispatcher`), reset only by `StartArcGameDispatcher`: the sixth landing of a game ends it. The traced game: six landings (lanes 4, 3, 5, 2, 3, 1), lives lost on the 1st and 4th (the tank 9.9 and 9.2 u off that lane), game over on the 6th (the tank 55 u away) | trace, map |
| the boss | `BigBadSoul` Health 99 (cap 99), last destruction level `HealthReq` 1: ~33 hits at 3 damage. Descends 8 u/s (`MoveBigBadDown`), moves in Y at 192 u/s, bouncing; `BigBadTouchDown` is game over | map |

**Score:** `Variable.Value` updates. Each kill's `AddToScore` is forwarded (ForwardTags) to
`AddToStage01Score`, `AddToStage02Score` and `AddToStage03Score` together, so `Stage01Score`, `Stage02Score` and
`BigBadSpawnScore` all gain 100 per kill (bombs included) and read 0 again at a new game. `TotalScore` stays 0: its only
modifier `AddToTotalScore` is fired by nothing. Phase 2 comes at 10 kills, phase 3 at 20, the boss at 25, the win on the boss's destruction (`Stage03Score` +1000).

**Pressing (the same path as the plunger, minigame-autoplay.md section 7).** Fire: the bind's execs
`DnControlFireEnable/Disable` on the player (the trace saw the player's 32 as `exec` rows). Left and Right have no exec:
`ControlRemapperEx.InputHook` calls `HandleInputEventsWrapper(2, bIStrafeLeft, ..)` and `(3, bIStrafeRight, ..)` ->
`HandleEvents(KeyType, 0 / 1 / 2)`; `dnControl.CREvent_KeyChange` maps KeyType 2 to EnumBase 26 and 3 to 29
(`tools/ubc.py`), and `dnControl_ArcadeMachine` overrides neither; `idle.ControlEventInternal` 26/27 sets `Left`, 28 clears
it (29-31 `Right`), then `DetermineOutEvent` fires `PressLeftEvent` / `ReleaseLeftEvent` on the edges. The trace's 76
pawn edges and 76 machine edges fell in the same frames, and no `ControlEvent` passed ProcessEvent (script to script).
The autoplay calls the same `HandleEvents(2 or 3, 0 / 1 / 2)` on the machine's `InternalControlRemapper` through
ProcessEvent: one press, one held call per frame, one release. Holding both directions cancels (horizontal input 0), so
the bot never presses one while the other is held, and reverses a frame after the release.

**The rule** (`arcbot.h arb_step`, tests 0568-0573), per frame in game time:
- **Target:** the live enemy (below home, not hit) landing soonest that the tank can still reach and shoot in time
  (travel at 96 u/s, the fire gap, 0.15 s of flight, a 0.05 s margin) and whose lane, and the lanes crossed on the
  way, no other threat makes unsafe (a landing within reach while the tank passes);
  with none, the boss. Kill everything: a landing anywhere costs a resource.
- **Move:** closed loop on `TankAvatarSoul.Y`: press when more than 3 u from the goal, release within 1.5 u or past
  it; each tolerance (and the fire tolerance) is at least half a frame's travel, for low frame rates and Game speed. The goal is clamped to the lanes +-12.
- **Fire:** when a live soul (target first, then any enemy, bomb or the boss) will be within 4 u of the tank, now and
  next frame, as the shot arrives (its Y plus its measured velocity times the flight to its height at ~1000 u/s from
  Z 120: the boss's lead); not when that flight exceeds the bullet's 0.2 s LifeSpan (above Z ~320). Held 80 ms, at most
  one press per 400 ms (one 0.2 s bullet at a time; the shortest gap in the trace was 463 ms), never in the frame of a
  release (the edges would cancel in one tick).
- **Dodge:** the tank's place is unsafe when an enemy (in its lane) or a bomb (at its predicted end) lands within 44 u
  (trigger 24 + soul 16 + 4) before the tank could leave, unless a shot will kill it first (aligned and allowed in time,
  or one on its way). Then the goal is the nearest lane safe on arrival; with none, the lane farthest from the landing.
- **Menu:** nothing live for 1.5 s (title, instructions): Fire once a second, as the player pressed through them. A stray
  rocket in a gap between attacks is harmless. No movement then.
- **Stops:** the switch off, the panel open or the camera flying, multiplayer, the machine not `idle`, leaving it, the
  game-over screen (7.5 s after `GameOverDispatcher` starts) and a win (for the rest of this use of the machine):
  everything held is released. Leaving the machine releases Fire on the player and drops Left/Right without a call (no
  machine to receive it); a level change forgets the presses (counted as dropped in the summary). A paused clock or a frame not read changes nothing.
- **Takeover:** the player's own strafe key on the held direction: let go without a release (it would clear theirs);
  on the other direction: the bot's is released so theirs goes through; while they hold one the bot does not move
  (`yield`). Their own Fire exec takes a held shot over the same way; a Fire flag the bot did not set blocks its shots.
- **Failures:** a call the engine refuses stops the bot until the player leaves the machine; a function, the remapper,
  the tank or the lanes that do not resolve make the row unavailable (logged `arcbot:`).

**Instrument:** bot rows (`E,..,bot,0,<action> <decision> target <e1..e5|b1|b2|boss|-> dodge <none|enemy|bomb> tank <y>
goal <y>`) on every press, release and takeover and on each change of decision, target or dodge reason, once the switch
has been on in the session. The `arcade:` line gains, when it was on (counts only while it is on): Fire / Left / Right presses and releases, taken
and dropped, `balanced` or `UNBALANCED`, kills, lives lost, wins and game overs while on, and the result (won, game over,
unfinished).

## Open questions

- Whether bombs really never hurt the tank
- The tank's reach (16 is `Barrels_Generic`'s class default; the instance has DrawScale 0.25).
- The bullet's reach at the boss's height (it is fired below Z ~320) and its hit tolerance.
- The boss's Y bounce range, and the phase 2-3 spawn cadence.
- Whether `bHasCheatedThisLevel` blocks the ego grant, as everywhere (autowin.md section 1). The panel does not exercise
  the award chain (section 3).

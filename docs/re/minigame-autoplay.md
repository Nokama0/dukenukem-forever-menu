# Legit autoplay of minigames: pinball (Balls of Steel) and the other candidates

Pinball autoplay is built (Game tab > AUTO-WIN, `pinbot.h/.cpp`, section 7) and measured in game: games of 617,600
in 99.5 s (372k a minute) and 529,100 in 138 s, hits 94 of 98, every game ended by 3 lost balls. A manual game scores
about 25,000 a minute; a bot that fires on first contact scored 3,820 a minute, so the bot shoots in a measured window
and cradles slow balls, and aims the lower left flipper's slow shots farther along it ("Lost balls" in section 7).
Flipper zones come from a traced manual game; the flippers are pressed by the binds' execs, the plunger by the
remapper's own `HandleEvents(5, ...)`; nothing is written. Air-Duke, I Need a Date and Drawrings are not automated.

A legit pinball bot is feasible. Everything it needs is readable by name (the ball actor's `Location`,
`KGetLinearVelocity`, the flippers' `bUp`, `bAllowPlunger`, `Score`) and the two flipper buttons are pressed by the
exec the key bind itself runs (`DnControlFireEnable/Disable` = left flippers, `DnControlMeleeEnable/Disable` = right
flipper), so the game cannot tell them from a player's. The ball is simulated natively (Meqon, `PHYS_Meqon` 18), at up
to 600 u/s on a table ~90 u long, and our hook runs once per presented frame; that is fast enough to keep a ball alive
(section 5). The plunger and nudges have no exec; the plunger goes through the remapper's `HandleEvents` (section 2).
Method: `tools/ubc.py` (every function decoded once into a scratch dump), a tagged-property reader over class
exports (`dnDecorations.u`, DLC03 packages, `Map02.dnf`), and the in-game pinball trace (section 6).

## 1. The table (`dnControl_Pinball`, dnDecorations.u; Map02 `___0`, Map09 `___1`; DLC `dnControl_Pinball2`)

- **Simulation is native.** The ball `dnControlHelper_Pinball_Ball` (`PrimaryBall`, spawned by `InitItems` from
  `MountOnSpawn` tag `PRIMARY_BALL`) is a Meqon rigid body: `Physics = 18`, `KRestitution 0.6`, `Mass 10`,
  `GravityScale 0`. Gravity is a `PhysicsAction_Acceleration` of `6 * table forward` set in `Ball.PostVerifySelf`,
  speed capped by `PhysicsAction_VelocityClamp(600, 600, 0)`. `Ball.PostPhysMeqon` is the only script on its step: it
  clamps the ball into table-local X -39..52, Y -23..23, Z 7..8 (teleports it back through `SetLocation`).
- **Readable by name:** `dnControl_Pinball.PrimaryBall` (ObjectProperty); the ball's `Actor.Location`; velocity via
  the native `KarmaActor.KGetLinearVelocity(out vector, BoneName)` (native 794, callable through ProcessEvent). Table
  space is `(Location - table.Location)` inverse-rotated by `table.Rotation` (the transform `PostPhysMeqon` uses).
  Map02 table: (214, 743, -500), Yaw 23552.
- **Table-local layout** (`MountOnSpawn` defaults; local X grows toward the player, launch velocity is -X):
  plunger lane at Y = -21 (`TrackCenter` (44,-21,8), `BallKeyframeLocation` (-20,-21,7.5)); drain `GutterCenter`
  (51,2,8) extent (1,12,2); left flippers (`PB_LEFT_FLIPPER`) pivot (46.5, 9) and (16, 18), right flipper
  (`PB_RIGHT_FLIPPER`) pivot (46.5, -5), mounted at Yaw -16384 / +16384 (they point at the centre line Y = 2).
  Bumpers (7,-5), (-7,1), (7,7); spinners (`BallGate`) at (-19,-11) and (-19,13); slot lights (-21; 7, 1, -5);
  hit boxes at X 22/20 and -16/-14; targets (-7.7,-14.8), (-8.4,20), (-21,21); kickers (`Spacer`) at (32,13), (32,-9).
- **Flippers:** `dnControlHelper_Pinball_Flipper`, Meqon bodies (`Mass 1600`) on an angular joint limited to 8192
  (45 deg, `KarmaSetConstraintProperties`). `FlipUp`/`FlipDown` only call `ConstVel.Setup(..., Vect(0,0,+-30))`: a
  constant angular velocity of 30 (unit unmeasured; ~26 ms per swing if rad/s); state `bUp`.
  **Length is not in script** (mesh/physics asset); the traced drains leave the tip at along 6.1..6.6 (section 7).
- **Input events** (`dnControl.EControlEvent`, read from Engine.u: 14 FirePressed, 16 FireReleased, 17/19 Melee, 20
  Forward, 23 Backward, 26 Left, 29 Right): `idle.ControlEventInternal` works the left flippers on Fire and the right on
  Melee (gamepad variants share the cases; the bodies decode with shifted offsets, so the Fire=left/Melee=right split
  comes from the HUD key info and the bind, and the trace confirms it), Forward/Left/Right nudge (`Nudge`: Up (-50,0,0), Left
  Y 25, Right Y -25 as a `KSetLinearVelocity` on the ball), Backward with `bAllowPlunger` (ball in the lane) releases
  the right flippers and enters `GrabPlunger`, whose `BeginState` plays the plunger animation; its notify calls
  `Table.LaunchBall`. **The launch is fixed**: `BallLaunchVelocity` 350, the ball is driven to the keyframe, then
  `KSetLinearVelocity(-350, 0, 0)` table-local (`PlungerPullSpeed` has no reader). Each flip rotates the slot lights.
- **Balls, drain, game over:** `BallsLeft` 3. The ball entering the `GutterSensor` runs `TriggerFunc_BallEnteredGutter`:
  while `GutterLight.bActivated` (lit on `ResetTable`, on `BallEnteredTrack`, and for 5 s after `BallLeftTrack`)
  the ball is saved (back to the lane, no ball lost); else `BallsLeft` drops, 0 = `bGameOver`, `DetachPawn`.
  **The score carries over until game over, not past it** (`AnimCallback_UserAttached` calls `ResetTable` only
  `if (bGameOver)`): leaving the table mid-game keeps `Score`, `BallsLeft` and `ScoreMultiplier`, so 1,000,000 must
  be reached inside one game of 3 balls, which may span several sittings. The traced game's start at 280,898 was not
  a carry-over: Auto-win minigames called `AwardPoints(280898)` on that attach (nukepanel.log, the line before the
  trace opened; highscore 280,897 + 1).
  DLC `Pinball2` saves the first two drains per ball (`BallSaveCount`), hides the kickers, awards once.
- **Tilt:** more than `NudgeMax` 3 nudges inside `TiltDecayRate` 3 s tilt the table (flippers dead until the ball
  drains). A bot that never nudges never tilts.
- **Scoring** (`AwardPoints(P)` adds `P * max(ScoreMultiplier, 1)`; class defaults): bumper 500, kicker 500, spinner
  250 per half turn, slot light 750, hit box 300, target 250 (unlit) / 20,000 red or blue key / 50,000 jackpot,
  complete hit-box set 5,000, complete slot set 7,500 and `ScoreMultiplier += 2` (2, 4, 6, 8; four completions).
  Jackpot chain: finish hit-box set 2 -> target 1 (blue key), set 1 -> target 2 (red key), both lit -> target 3
  (jackpot); at x8 one chain is ~720,000. `highscore` default 280,897 (award 9 on passing it); the display caps at
  99,999,999. **A human's time to 1M is not recorded in any file.**
- **Camera:** the control's remapper limits the view (`HeadAimMin/Max` a few degrees, `FOVScalar` 1.1875): fixed for
  practical purposes; the bot reads world data, not the screen.

## 2. Pressing the controls

- **Flippers: `PlayerPawn.DnControlFireEnable/Disable` (Fire) and `DnControlMeleeEnable/Disable` (Melee) through
  ProcessEvent.** They are the bound commands (`user.ini`, the game's key bindings: `LeftMouse=... | DnControlFireAction`, `RightMouse=... |
  DnControlMeleeAction`, aliases `DnControlFireEnable | OnRelease DnControlFireDisable`), exec, one assignment each.
  `ControlRemapperEx.InputHook` (per player tick) reads `bIDnControlFire`/`bIDnControlMelee` against the engine's own
  previous-frame copy, `HandleEvents` -> `CREvent_KeyChange` -> `ControlEventInternal(14 + type)`. Same path as the
  turret triggerbot (turrets.md section 4). Never release a press the player holds.
- **Plunger (Backward) and nudges (Forward/Left/Right): no exec.** `bIBackward` etc. are set natively from the
  movement axis (`user.ini`: `MoveBackward` = `Axis aForward Speed=-1.0`; no script assigns them). The panel uses the remapper's own `HandleEvents(5, EventType)` (section 7).
- Never used: `AwardPoints`, `Score`, `BallsLeft`, ball position/velocity writes, `LaunchBall`.

## 3. Timing constraints

The bot runs per presented frame, while the player's `InteractiveDecoration` is the table, `!bGameOver` and not
paused; it reads the ball's table-local position and velocity (a few guarded reads per frame, ~12 us each). Failure
modes:
(a) frame time: at 60 fps a frame is 16.7 ms = 10 u at the 600 u/s clamp, about a flipper length; 30 fps or a hitch
loses the ball; (b) `Location` may be interpolated (`bUsePhysMeqonInterp`) or lag the body, and physics may sub-step
(the traced hits fit without a correction); (c) our hook runs at `Present` (frame.cpp, after the tick): a press lands
in the next tick's `InputHook`, then the motor in that tick's physics; (d) random bounces: 3 balls, ball save only
5 s, so the bot must stay alive long enough (at x1 many minutes); (e) the player's own mouse buttons drive the same
flags: yield to them.

## 4. Other achievements

| id | achievement | autoplay would be | today's auto-win |
|---|---|---|---|
| 24 / 54 | Balls of Steel / Scientits | this bot (Pinball2 is the same script with more ball saves) | `AwardPoints(N)` earns it by writing score, which is rejected as cheating; stays the quick option |
| 23 | Air-Duke (7-0) | native table (`AirHockeyTable_NativeBase`: `Puck`, `PlayerInfo`, `Paddle*RB`); the paddle follows the mouse through `dnControl_AirHockeyPlayer.Play.InputHook_ViewRotationAdjust` (the event the aimbot already feeds on turrets); the opponent is `AIInputDispatcher_AirHockey`. A bot aims by view-rotation adjusts: a pursuit problem, harder than pinball | `GameWon(index)` **skips** the game: award 5; achievement 23 only if the opponent's score is 0 then, so it usually does not earn it |
| 42 | Lots of Whacking | already played by the game's own hit on each alien | earns it (score 99 of 99) |
| 28 | Drawrings | draw with a pen: `Drawing.Tick` awards on any pen stroke; a view-rotation scribble | not covered (autowin.md: no win) |
| 20 | I Need a Date | press Use six times on one calendar; `Used` flips a month, `UpdatedViewedMonths` counts 6 distinct months; Use is gated by the flip animation (`bUsable` false until `AnimEndEx`) | not covered; one Use press per animation end is legit (as autoexec presses Use) |
| 3 | Hippy-Stomper | no minigame (stomps on shrunk enemies) | n/a |

No other skill achievement involves a minigame; pool, slots and poker award only ego awards.

## 5. What the trace settled

- The flipper zones and the tip, in the flipper's frame (section 7).
- `bUp` rises in the frame of the exec; Fire = left, Melee = right.
- The ball moves at 350 u/s at most (p95 159).
- The plunger goes through `HandleEvents` (section 7).
- The hits fit without a `Location` correction, so `Location` does not visibly lag the body.

## 6. The pinball trace (instrument; reads only, presses nothing)

`pbtrace.cpp` records whenever the player's `Pawn.InteractiveDecoration` has `dnControl_Pinball` or
`dnControl_Pinball2` on its class chain. A **session** is one use of a table (attach to detach, so normally one
game: game over detaches the pawn); it also ends on a level change. Output: `System\NukePanel\pinball-trace.csv`,
started by the first session of the process (the last launch's file kept as `pinball-trace.prev.csv`, replacing an
older one) and appended to by the next ones, written from a 64 KB buffer at most once a second and at session end (the
game exits without `DLL_PROCESS_DETACH`: at most the last second is lost).
At most 200,000 rows per session (then one `cap_reached` row; the rest are counted, not written). Past 50 MB in total
the file starts over, the previous one kept as `pinball-trace.csv.old` (one only), the new one starting with the
session's header rows again so it parses on its own; a file that cannot be renamed (to `.old` or `.prev.csv`) is
never truncated: tracing goes on in a new `pinball-trace-<tick>.csv`, or stops if that does not open (logged); a write that keeps failing is
logged once per streak, and the streak's length when writes recover.

Each row's first field is its kind:
- **`H` header rows**, at session start: `H,session` (number, map, control class, local start time, the frame rate
  over our last 120 frames, how many properties did not resolve); `H,table` (Location, Rotation in UE units);
  `H,flipper_left,i` / `H,flipper_right,i` per `LeftFlippers[i]` / `RightFlippers[i]` and `H,ball,0` when
  `PrimaryBall` first shows: class, Location, table-local location, Rotation, `localYaw` (Yaw minus the table's),
  CollisionRadius, CollisionHeight, DrawScale, DrawScale3D, bCollideActors, bBlockKarma, Physics (-1: did not
  resolve); `H,processevent` (whether the control's ControlEvent and AwardPoints resolved); `H,columns` twice.
- **`F` frame rows**, one per presented frame while not paused: `frame` (session frame number), `gameTime`
  (`LevelInfo.TimeSeconds`), `ms` (our QPC time since session start), `dtMs` (game-time delta), `x,y,z` (ball,
  table-local: `(Location - table.Location) << table.Rotation`; `z` should stay in 7..8, the clamp of
  `PostPhysMeqon`, which checks the transform), `vx,vy,vz` (table-local), `vsrc` (`K` from
  `KarmaActor.KGetLinearVelocity`, Engine.dll!0x101b6100, which only reads the Meqon body and returns 0 without one;
  `D` the position delta over game time), `lUp` / `rUp` (bit i = flipper i's `bUp`), `lBtn` / `rBtn` (the
  control's `bLeftButton` / `bRightButton`: what the control received), `fire` / `melee` (the pawn's
  `bIDnControlFire` / `bIDnControlMelee`), `allowPlunger`, `score`, `ballsLeft`, `multiplier`.
- **`E` event rows**: `frame,gameTime,ms,event,value,detail`. Polled each frame: `input` (an edge of a pawn input
  flag; value = the control event it raises, 14/16 fire, 17/19 melee, 20/22 forward, 23/25 backward (the plunger
  key), 26/28 and 29/31 strafe, from `dnControl.CREvent_KeyChange`'s EnumBase + EventType), `button`, `score`
  (value = the delta, detail the multiplier), `ball_lost` (BallsLeft dropped), `ball_saved` (`bBallInGutter` rose
  with BallsLeft kept), `reset`, `game_over` (value = score), `tilt`, `ball`, `vsrc`, `state` (the control's
  `GetStateName`, e.g. `GrabPlunger`), `session_end`. Through the ProcessEvent slot (pehook): `exec`
  (`DnControlFire/MeleeEnable/Disable` on the player, value 1/0), `control_event` (`ControlEvent(N)` on the
  control), `award` (`AwardPoints(N)`). The key path (`InputHook` -> `HandleEvents` -> `CREvent_KeyChange` ->
  `ControlEventInternal`, and `AwardPoints` from the table's helpers) is script to script, so those two may never
  pass ProcessEvent: the polled rows are the record, the summary line counts what the slot saw.

- **`bot` event rows** (Pinball autoplay, section 7): value = side (0 left, 1 right, -1 plunger), detail
  `<decision> <side> pos <x> <y> vel <vx> <vy>` (table-local ball): decisions `press`, `press safety`, `cradle`,
  `release held`, `release settled`, `release cradle cap`, `release cradle lost`, `release stop`, `taken by the
  player`, `hit`, `miss`, `plunge`, `plunge release`, `plunge dropped`. The bot's execs also show as `exec` rows
  (they pass the same ProcessEvent slot as the binds').

The `H,session` row ends with `gamespeed,<on|off>,<percent>` (the panel's Game speed switch and setting). Before the
summary, one `pinball: game speed` line per session: the ratio of game time to wall time, and the mean and max
game-time frame delta.

One `pinball:` log line per session: why it ended (game over, left the table, level changed), real and
game duration, score, score gained since the session's first frame and points a minute, balls lost, saves, the
autoplay's rows and hits/misses per side, rows written and dropped, frames by velocity source, flushes and the
longest write, the ProcessEvent counts, and faults. Pinball autoplay adds its own `pinbot:` line per session (only
when it pressed anything): presses and releases per side, plunger press/held/release calls, presses dropped with a
level change, refused calls, faults; balanced presses and releases are the check.

## 7. Pinball autoplay

**Zones, from a manual game** (Map02 via cp02, 23 presses; `pinball-trace.csv`). Flipper rows of the
header: left flippers at table-local (46.5, 9.0) and (16.0, 18.0), right at (46.5, -5.0); localYaw -8188 / +8191
(+-45 deg at rest, pointing toward the drain and the centre line). In each flipper's frame (`along` its length from
the pivot, `perp` up the table toward -X), the ball at the press:

| outcome | presses | along | perp | ball velocity (table) |
|---|---|---|---|---|
| hit (vx < -60 within 11-40 ms; vx reached -125..-191) | 7 left, 6 right | -1.6 .. 3.8 | 2.3 .. 6.9 | +14..+69 toward the drain, 18-79 u/s |
| miss, early (f3638/3639, both buttons) | 2 | 0.2 / 1.7 | 8.0 / 9.9 | +47 |
| miss, late (f7697, ball lost) | 1 | 6.4 | 2.5 | +40 |
| pressed with the ball elsewhere (lane, mid-table) | 7 | - | - | - |

All three drains rolled down a resting flipper to its tip and dropped through the centre gap: the ball leaves the tip
at along 6.1..6.6, perp ~2. The zone is therefore along -3.0..5.8, perp 0.5..7.0 (`PBB_ALONG_*`, `PBB_PERP_*`).
`bUp` rose in the same frame as the exec; the manual hits held 200-383 ms.

**Shot quality (a manual game and an early bot session).** The manual game: 16 presses near a lower flipper. The
bot's session (a build that fired on first contact): 21 presses; 35 s, 2,250 points, left 7 hits / 11 misses, right
2 / 2, 1 ball lost. Per press: the contact along the flipper, the ball's speed in, the exit (the most negative vx
within 200 ms; a hit is < -60), and the points from the press to the next press, a drain or 3 s. The manual game's
trace file is gone (the trace truncates on each launch); its rows keep exits as vx only and lack the points of its last two shots.

| along | shots (manual / bot) | hits | ball speed in | mean exit vx (hits) | points a shot | hits that scored nothing |
|---|---|---|---|---|---|---|
| -3.5..-1.5 | 15 (1 / 14) | 2 | 10..51 | -122 | 33 (n 15) | 1 of 2 |
| -1.5..-0.5 | 7 (0 / 7) | 7 | 8..25 | -97 | 143 (n 7) | 6 of 7 |
| -0.5..0.5 | 5 (5 / 0) | 4 | 48..69 | -166 | 960 (n 5) | 0 of 4 |
| 0.5..1.5 | 5 (5 / 0) | 5 | 18..46 | -178 | 625 (n 4) | 1 of 4 |
| 1.5..3.0 | 2 (2 / 0) | 1 | 47 | -167 | 0 (n 1, an early press) | - |
| 3.0..4.5 | 2 (2 / 0) | 2 | 31..79 | -146 | 4,775 (n 2) | 0 of 2 |
| 4.5..7.0 | 1 (1 / 0) | 0 | 49 | - | 0 (the late miss) | - |

Reading: the bot pressed at the inlane mouth (along -3.1, perp 2: the ball still on the inlane guide, sent back up
it) and at -1.0 with the ball rolling at 8 u/s (exit ~95 u/s at +20 deg, nothing scored, the ball back at the same
flipper ~2.3 s later, six times). From along 0 on, shots leave at 146-178 u/s and score; the best points a shot were
at 3.0..4.5 (n 2, one of them the 5,300 set bonus). **Window chosen: along 0..4.5**; 4.5..5.8 is the drain safety.
Samples are small: the per-bin tally below collects more.

**Cradle (from the bot's session):** with the flipper up, balls arriving at the inlane mouth at 10-13 u/s settled
(speed < 3 for 100 ms) in 4 of 7 presses, at 18-25 u/s in 1 of 7; all settled in the crook at along -2.2, perp 2.1,
194-360 ms after the press. Trap speed `PBB_TRAP_V` = 13.

**The rule** (`pinbot.h pbb_step`, tests 0494-0509). Per frame, from the trace's read of that frame:
- **Shot:** a side fires when the ball's contact predicted 16 ms ahead (`PBB_LEAD_MS`), or its place now, is at along
  0..5.8 and perp 0.5..7 on one of its flippers, moving toward the drain (vx > 5): `press` in the window (0..4.5),
  `press safety` past it before the tip. Not on first touch: a ball at the mouth or the crook waits. One Fire exec
  flips both left flippers, as the key does; both sides fire only in the centre gap. A shot is held 200 ms.
- **Cradle:** a ball at the inlane mouth down to the crook (along -3.6..-1.5) at 13 u/s or slower, moving down,
  raises the flipper early (`cradle`). Held until the ball has been still (< 3 u/s) for 150 ms (`release settled`),
  at most 3 s (`release cradle cap`), or until it bounces away (`release cradle lost`). After a settled or capped
  cradle the side does not cradle again while the ball rolls down; it fires when the contact reaches the window.
- A side rearms 60 ms after its release.
- Plunger: pressed once when `bAllowPlunger`, the control's state is `idle`, the ball rests in the lane (x > 40,
  y < -19, < 5 u/s) and no flipper is held; held each frame for 600 ms (`GrabPlunger` lasted ~500 ms; a manual game held
  628 and 755 ms), released; not again for 2 s.
- A side whose button the player holds (a flag we did not set) is left to them; the player's own backward key blocks
  the plunger. A press is judged a hit when vx < -60 within 150 ms, else a miss (the trace's bot rows).
- **Takeover:** the player pressing a side the bot holds (an Enable exec the bot did not make, seen through pehook)
  makes the bot let go of it at once without a release call, which would clear the player's own flag; the side stays
  theirs until they release it. The player's backward key during a plunge, or leaving the table, drops the plunge
  without `HandleEvents(5, 2)` (no table to receive it, as with the real key).
- Stop (switch off, the panel open or the camera flying, game over, tilt, leaving the table): everything held is
  released once, inside the session (the trace closes after the autoplay's frame, so the `release stop` and `exec`
  rows land in it). A level change forgets the presses without calls (the pawn went with it). Paused: nothing is
  read or called; the presses hold.
- **Failures:** a function or property that does not resolve makes the row unavailable for the process (logged). A
  call the engine refuses is counted and stops the bot until the player leaves the table (chosen as the safer: the
  engine is not in the state the bot assumed); what it holds is released on the way out.

**The flippers' path:** the binds' execs `DnControlFireEnable/Disable` and `DnControlMeleeEnable/Disable` on the
player through ProcessEvent (scall), the exact calls the key binds make; the trace sees them as `exec` rows.

**The plunger's path, found in script** (`tools/ubc.py`): the backward key's axis sets `PlayerPawn.bIBackward`
natively; `ControlRemapperEx.InputHook` (per tick) calls `HandleInputEventsWrapper(5, bIBackward, bIWasBackward)`,
which calls `HandleEvents(5, 0)` on the press, `(5, 1)` each tick while held and `(5, 2)` on the release;
`HandleEvents` returns for a key type in `DisabledKeyTypes`, else calls `CREvent_KeyChange` on the control (EnumBase 23 + EventType) and on
each `NotificationActors` entry; `dnControl_Pinball.idle.ControlEventInternal(23)` with `bAllowPlunger` drops the
right flippers and enters `GrabPlunger`. The autoplay calls the same `HandleEvents(5, 0 / 1 / 2)` on the table's
`dnUsableSomething.InternalControlRemapper` through ProcessEvent, with the key's own timing (one press, one held call
per frame, one release). The game's AI presses keys on a used control the same way (`AIActor.UseX.Input`:
`InteractiveDecoration.InternalControlRemapper.HandleEvents(KeyType, 0/1/2)`). Only the pawn's flag write is skipped:
no engine memory is written, and nothing else reads `bIBackward` while the player is attached (its only other
readers are the jetpack fuel HUD checks). Rejected: writing `bIBackward` (engine memory the native sets from the
axis) and reporting the bound key through the `GetKeyState` gate (it needs the player's binding and
the gate zeroes keys while the panel is open).

**Lost balls (five sessions, 9 lost; `pinball-trace.csv`).** The last 2 s before each
`ball_lost`, against the lower flippers' pivots (left y 9, right y -5):

| cause | balls | what the trace shows |
|---|---|---|
| left outlane | 5 | down the outside of the left flipper (y 15.6 -> 10.6, 26-33 u/s); all five 1.5-1.8 s after the same left shot: the drop shot at along -0.3, perp 2.7, 25 u/s, out at (-150, +54), +19 deg |
| right outlane | 3 | down the outside of the right flipper (y -11.6 -> -6.6, 20-30 u/s); 2.5-4 s after the last shot (upstream play, no single shot) |
| centre gap | 1 | the table tilted (the player's own nudges, 4 in 0.3 s, then dead flippers); not the bot |
| past a tip unhit, missed or late flip, held at the wrong time | 0 | |

**Shots by side (same games, 75 shots near a lower flipper):** the left's drop shot (along -0.5..0, 15 shots) left at
+17..+25 deg (up the left side), scored 0-1,500 (mean 250) and was followed by a drain before the next press 6
times. The right's drop shot (along -0.4..-0.3, 15 shots) left at -17..-20 deg and scored 600-42,200 (mean 10,900),
no drain. The left's shots farther along (2.5..3.8, faster balls) left at -5..+39 deg and scored 500-29,600, no
drain. The tally's -0.5..0.5 bin that "dominates" is the right's drop shot.

**Rules the lost-ball data produced:**
- The lower left flipper fires a slow ball (< 40 u/s) only from along 2.5 (`PBB_AIM_LEFT`): the drop shot rolls on
  ~120 ms and leaves less to the left. The right's drop shot is kept as it is; fast balls and the upper left flipper
  keep the window from 0; the drain safety to the tip is unchanged. Test 0511.
- Centre drains and late flips: none in these games, so the lead and the both-flipper rule stay.
- **Nudging: not added.** It is the game's own control, so legitimate by the project's rule, but `Nudge` is a
  `KSetLinearVelocity` on the ball whose effect on an outlane ball is unmeasured, more than 3 in 3 s tilt the table
  (one game here was lost to the player's own nudges), and the outlanes are reached from upstream play the bot does
  not see coming.
- **Instrument:** one `pinball: ball lost` line per lost ball (and a `drain_cause` event row) with its cause
  (`pb_drain_cause`: tilt, left or right outlane, centre gap held / a flip within 500 ms / unhit), its last place and
  speed, and the last flipper press (the bot's or the player's). The summary's "gained" counts from the game's
  `ResetTable`, not from the last game's score.

**Game speed:** every bot timing (holds, settle, cradle cap, plunger hold and re-arm, judging, the
tally) runs on the game clock, `LevelInfo.TimeSeconds` in ms (the trace's `gameTime`), so Game speed scales them with
the ball. The lookahead is at least the last frame's game-time delta (at speed S a frame is S times more game time),
capped at 100 ms, so a press lands a frame early rather than late. A frame with no game time passed is a paused clock:
no press or release changes (a stop still releases). Tests 0515-0516.

**Tally (learn online, recorded only):** each shot's predicted contact along the flipper falls in one of the shot
table's bins; its points run from the press to the next press, a drain or 3 s. The `pinbot:` session line is followed
by `pinbot: points a shot by contact along the flipper:` with shots, points a shot and drains per bin. Nothing adapts
live yet.

**Guards:** the switch is off by default (`[autowin] pinball`); while on, Auto-win minigames never calls
`AwardPoints` on a table (`aw_pinball_awards`). It covers `dnControl_Pinball` (Map02, Map09) and, by the same
class-chain test and property names, `dnControl_Pinball2` (DLC03_Map03, untraced; a table whose flippers do not
resolve makes the row unavailable, logged).

## Open questions

- Time to 1,000,000 for a human (one manual game scored ~26k in 61 s with no multiplier); the bot's own rows against
  a manual game's hit rate.
- Whether a nudge moves an outlane ball usefully: trace a few manual nudges on an outlane ball before any rule uses
  them.
- The unit of the flipper's constant angular velocity (30; ~26 ms per swing if rad/s).
- `dnControl_Pinball2` (DLC03_Map03) has not been traced.


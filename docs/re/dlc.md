# DLC: chapters, progress, ego awards and achievements for "The Doctor Who Cloned Me" (build 244)

The DLC has its own parallel progress system. **DLC01 ("Hail to the Icons Parody Pack") is multiplayer only**
(`SPMapIds=()`, four DM maps, no campaign). **DLC03 is a 14-chapter single-player campaign** (`DukePlayerDLC03`).
Everything is readable through the same GConfig `Progress.ini` the base tabs use: chapter completion is 56 bools in
`[DLCProgress]`, the DLC has **19 ego awards of its own** (`[EgoAwards] DLC03EGOAWARD_*`, 35 cap points, 30 table
entries over 14 chapters), and it has **no helmets and no phone messages**. The base giver rules match the DLC
subclasses (a `_DLC03` toilet would show as base award 0), the base `GivePermanentEgoCapAward` is a no-op for a DLC
player, and the base `pg_chapter_of_map` cuts to 5 characters ("dlc03") so every DLC map would be chapter -1. The
panel therefore matches the whole map name and has its own DLC rules (section 6, built): the whole-name chapter
match, the DLC table (game first, this file's copy as fallback), the Progress tab's Base / DLC switch, the DLC ego
rule set, achievements 50-52, 57-62 and the two quest pickups. Achievement 61's ids are all 19 DLC awards in
`[ACH_EgoAwards]`; Hedonist's cabinet is `dnDecoration___0`.
Measured in game: a DLC checkpoint package `cp01` reads `LevelInfo.MapFileName` `DLC03_Map00`; `Pawn.EgoCap` is 70 with
nothing found and 71 with one award, matching the formula (preorder read 0 in the DLC).

Method: DLC03_Game/_Decorations/_AI/_Vehicles.u decoded with `tools/ubc.py` (`extracted-packages/dlc03/*.u` and
`dlc01/`, copies, gitignored); the 14 DLC03 map packages read in place from `DLC\DLC03\Maps` with `tools/tagprops.py`
(class defaults and instance properties; the array-size rule is in its header); Ghidra MCP on dnGame.dll:
`dnGame.dll!execHasCompletionForDLCLevel 0x100408F0`, `AddCompletionForLevel 0x1003E8B0`, `0x1003E370` (renamed
`DLCMapNameParse`; saved).

## 1. The campaign's chapters

- **Maps:** `DLC03_Map00..07` and `Map10..15` (no 08, 09; no B or C parts, no `DLC03_Map00b`). Four MP maps
  (`DLC03_DM-Rooftops/-Command/-Dropzone/-Biohazard`) are not chapters. Checkpoints: `Maps\checkpoints\DLC03_MapNN\cpNN.dns`.
- **The chapter list is `Engine.MapInfo.CampaignList`**, filled by the DLC mount from `System\DLC_Maps.ini`
  (`[Engine.MapInfo] CampaignList=(URL="DLC03_Map00", MapNameTag=.., SShotPath="DLC03_dt_mapshots.sp.", IsDLC=true,
  [CampaignStart=true])`). `UDukeSceneDLCChapterSelect.OnNavForward` (dnWindow.u) rebuilds the scene's `Chapters` from it
  entry by entry (instance property; the class default `UDukeSceneChapterSelect.Chapters` the panel reads is the base
  table and is untouched) and takes titles from `Localize('MapNames', MapNameTag, 'Maps')` (`DLC_DLC03_Maps.int`).
  `HasCompletionForDLCLevel(d, i)`, `IsDLCCampaignStart(i)` and the scene's lock rule all index this array. The base
  `CampaignList` default is empty (Engine.u; no `CampaignList` in system.ini).
- **Order = unlock order** (a chapter is offered when complete on difficulty 0, or `IsDLCCampaignStart`, or follows a
  complete one; same order in `HasFullDLC03Completion`):

| i | URL | title | table awards (`DLC03MapEgoCapAwards`) |
|---|---|---|---|
| 0 | DLC03_Map00 | Rude Awakening | 17 |
| 1 | DLC03_Map04 | Going Down | 10 |
| 2 | DLC03_Map05 | In Security | 8, 3 |
| 3 | DLC03_Map06 | The Clone Carousel | 11 |
| 4 | DLC03_Map07 | Me, Myself, and I | 15, 13, 5, 12, 14, 2, 7, 16, 18 |
| 5 | DLC03_Map10 | Meat Grinder | 3 |
| 6 | DLC03_Map11 | The Doctor Is In | 6, 18 |
| 7 | DLC03_Map12 | Magic Carpet Ride | 4, 10 |
| 8 | DLC03_Map13 | Drunken Crane Master | 3, 8 |
| 9 | DLC03_Map01 | All Your Base... | 11, 15, 13 |
| 10 | DLC03_Map02 | Tunnel Vision | -- |
| 11 | DLC03_Map03 | The Burning Bush | 3, 17, 1, 9, 18 |
| 12 | DLC03_Map14 | Teleportation Spire | -- |
| 13 | DLC03_Map15 | Duke Side of the Moon | 0 |

- **Map to chapter:** the map name equals the URL (case-insensitive, full 11 characters), not the base rule's 5-character
  cut (which gave `level package 'DLC03_Map00', LevelInfo.MapFileName 'DLC03_Map00', chapter -1` in nukepanel.log). A
  checkpoint's package is `cpNN` as in the base game (progress.md section 3); `MapFileName` reads `MapNN` after a
  base load and `DLC03_Map00` for the DLC checkpoint `cp01` (measured).
- The game's own LCD ego count is blind to the DLC: `SPSetEgoAwardsForLevel` cuts `DLC03_Map00` to "dlc03" and finds no
  base URL. The panel can do better than the game here.

## 2. DLC progress state (`[DLCProgress]`)

- **Key:** `[DLCProgress] GameCompletion_<URL>_<difficulty>` in Progress.ini, a bool, one per chapter and difficulty
  (14 x 4 = 56 keys); no bitmask. Read: script `DukePlayerDLC03.HasDLCMapCompletion(MapName, Difficulty)` =
  `GetConfigBool('DLCProgress', 'GameCompletion_' $ MapName $ '_' $ Difficulty, .., 'Progress.ini')`; native
  `execHasCompletionForDLCLevel` builds the same key from `CampaignList[i].URL` (`MapInfo` default +0x4A4, 0x30-byte
  entries, URL at +0x14), after `0x1003E370` accepted the name (`dlc%d_map%d` by `swscanf` on the lower-cased base file
  name; length 11 = part 0, 12 = a letter part, none exist) and reads with `GConfig->vt[1]` (GetBool, +4).
  `progress.cpp`'s `engine_config_get_bool(L"DLCProgress", key, &v, L"Progress.ini")` is the same call.
- **Write:** `DukePlayerDLC03.UpdateLevelEndAchievements` (script) calls `AddCompletionForLevel(i, -1, false)` for
  `i = [Difficulty] AcknowledgedDifficulty .. 0`; with Level -1 the native takes the **level's name**, finds no base chapter
  (`MapNameToChapterPart`), accepts it as a DLC name and sets `[DLCProgress] GameCompletion_<name>_<i>` (SetBool, vt+0x28).
  So finishing on a harder difficulty ticks the easier ones too (as the base). Which name a level loaded from a checkpoint
  has is unmeasured (open 1); the key uses the casing the level reports, the read uses the ini's `DLC03_MapNN`.
- **Whole campaign:** `HasFullDLC03Completion(d)` = all 14 chapters on `d`; awards 57..60 (Another Piece of Cake, Let's
  Rock Out, Come Get a Little More, Damn, I'm REALLY Good) in `UpdateLevelEndAchievements`, highest first, cheat-gated.
  `UDukeSceneSinglePlayerDifficultyDLC03.OnNavForward` unlocks the 4th difficulty with `HasFullDLC03Completion(0)` (a DLC
  Insane unlock, separate from the base's). `[ChapterSelect] DLC03_Available` shows the DLC Chapter Select button.
- **Scope:** profile, like the base ticks (nothing clears `[DLCProgress]`). Difficulty names are the base's (LCDHelper).

## 3. DLC ego awards

- **Own enum, own cap table, same sections.** `DukePlayerDLC03.EDLC03EgoAwardID` (19 ids; the key is the enum name):

| id | name (`DLC03EGOAWARD_` + ) | cap + | giver in script | chapter(s) in the table |
|---|---|---|---|---|
| 0 | AmericanFlag | 2 | `dnControl_HandSizeFlag.DetachUser.AnimCallback_UserDetached` | Map15 |
| 1 | ArcadeGame | 3 | `TriggerPlayerDLC03` Tag `ArcadeEgoTriggerPlayer` (`WinGameDispatcher` -> `TriggerDelay HackAwardEgo`); the script helper `dnControlHelper_ArcadeMachineGameShooter.GameClear` also grants it but is never placed (arcade.md) | Map03 |
| 2 | ArmWrestling | 2 | `dnControl_Armwrestle.FullyLifted` (the player wins) | Map07 |
| 3 | ComputerPorn | 1 | `Electronics_Generic_Computer_Monitor_DLC03.SwitchScreens` (needs `bCanSwitch`); Map13 by `TriggerPlayerDLC03` Tag `TriggerPlayer_ComputerEgo` | Map05, 10, 13, 03 (see the givers table) |
| 4 | DartBoard | 2 | `Details_Generic_Dart.KImpact`: a thrown dart lands inside the board's radius | Map12 |
| 5 | DeadLift | 2 | `dnControl_Deadlift.FullyLifted` | Map07 |
| 6 | DefeatProton | 4 | `TriggerPlayerDLC03` Tag `IncreaseEgo`, fired by `Dispatcher_ProtonDead` (boss death) | Map11 |
| 7 | FilmReel | 2 | `TriggerPlayerDLC03` Tag `TriggerPlayerDLC03_FilmReel_Ego`, fired by `Dispatcher_Reel_Placed` (reel put in the projector) | Map07 |
| 8 | HotSauce | 1 | `HotSauce.ActivateComplete.BeginState` (the weapon, from grabbing `Details_HotSauce`) | Map05, 13 |
| 9 | Pinball | 3 | `dnControl_Pinball2.AwardPoints`, any score above `highscore` | Map03 |
| 10 | PornMagazineE | 1 | `Details_Generic_PornMag_E.GrabComplete` | Map04, 12 |
| 11 | PornMagazineD | 1 | `Details_Generic_PornMag_D.GrabComplete` | Map06, 01 |
| 12 | PullUp | 2 | `dnControl_PullupBar.DetachUserLERP.BeginState` | Map07 |
| 13 | SpeedBag | 1 | `Settings_Workout_PunchingBag_DLC03.TakeDamage` (melee; the class is a punching bag) | Map07, 01 |
| 14 | WorkoutBench | 3 | `dnControl_WorkoutBench_DLC03.FullyLifted` | Map07 |
| 15 | HandWeight | 2 | `Settings_Workout_HandWeight_DLC03.HandleDecoAnimEvent` | Map07, 01 |
| 16 | BasketBall | 1 | `Settings_Workout_Basketball_Hoop_DLC03.Score` | Map07 |
| 17 | Toilet | 1 | `dnControl_Bathroom_Toilet_DLC03` / `_Urinal_DLC03` `.PressPee.BeginState` | Map00, 03 |
| 18 | Mirror | 1 | `dnBreakableGlass_Mirror_Usable_DLC03.Used` | Map07, 11, 03 |

  Cap values from `DukePlayerDLC03.DLC03EgoCapAwards` (class default, 19 floats, **sum 35**); all 19 ids appear in the
  table (30 entries, 19 distinct), so unlike the base none is orphaned.
- **Event-marker Tags** (read with `tools/tagprops.py instances <Map.dnf> <Class> Tag EgoCapAwardsDLC03` on
  the installed maps): DLC03_Map07 `Electronics_Generic_Projector___0` Tag `Projector_Unit`,
  `Electronics_Generic_Projector_Reel___0` Tag `Electronics_Generic_Projector_Reel`, `TriggerPlayerDLC03___2` Tag
  `TriggerPlayerDLC03_FilmReel_Ego` lists [7]; DLC03_Map03 `dnControl_ArcadeMachine___1` Tag `NukeTankArcadeMachine`,
  `TriggerPlayerDLC03___0` Tag `ArcadeEgoTriggerPlayer` lists [1]. The `egoesp: ... event award` line shows which actors resolve (whether the
  placed reel goes hidden is open 6). A chapter's "N/M": M = `Awards.Num()` of its
  `DLC03MapEgoCapAwards` entry (a `{MapName, Awards byte[]}` struct array on `DukePlayerDLC03`'s defaults; map names are
  full URLs), N = entries with `HasDiscoveredEgoAward_DLC03(id)` (`UDukeSceneChapterSelectDLC03`).
- **Where "found" lives:** `GivePermanentEgoCapAward_DLC03(id)` (script; returns on `bHasCheatedThisLevel` or MP, **no
  `EgoCapAwardsEnabled` gate**, so the Map00-style closed window does not exist in the DLC) sets `[EgoAwards]` and
  `[EgoAwardsSession]` `DLC03EGOAWARD_<name>`, `GiveEgoCapAward(DLC03EgoCapAwards[id])`, refreshes the LCD, then the profile
  copies `[ACH_EgoAwards]` / `[ACH_EgoAwardsSession]`. Per playthrough exactly as the base (progress.md section 5):
  `StartedNewGame` -> `ClearEgoAwards` clears the 19 DLC keys only; the native reload rollback and the checkpoint save
  clear/roll back the Session sections generically. `HasDiscoveredEgoAward_DLC03(id, bForAchievement)` reads `[EgoAwards]`
  or `[ACH_EgoAwards]`. `DukePlayerDLC03.HasDiscoveredEgoAward` and `GivePermanentEgoCapAward` are overridden to no-ops/false:
  **a base giver in a DLC level awards nothing**, and the base keys are never touched by the DLC.
- **Achievement 61** (`ACH_MaxEgoAchievedDLC03`): after a new award it checks `i = 18 .. 0` and returns at the first id
  whose `HasDiscoveredEgoAward_DLC03(i, true)` is false, else `AwardAchievement(61)`: **all 19 `[ACH_EgoAwards]
  DLC03EGOAWARD_*` keys**. Profile scope (survives New Game), like 29.
- **Ego cap:** `PreGameInit` sets `EgoCap = GetDefaultEgoCap()` (40, +5 with the preorder code), adds the cap of every
  found award, then **`GiveEgoCapAward(AutoEgoCapAward = 30)`**; `MaxEgoCap = GetMaxEgoCap()` = **`default.DukePlayer.MaxEgoCap`
  (100, +5)**, not the DLC class's own default of 105. `TriggerFunc_DisableFakeEgo` (the fake-ego restore) is registered by
  no DLC map (name scan of all 14), so the 30 stays. Permanent cap in the DLC = 40 (+5) + 30 + sum of found caps, 105 when
  everything is found: whether the engine clamps at 100 is open 2. `Pawn.EgoCap` live is the truth for "now".
- **Level-only changes:** no DLC `TriggerPlayer` raises the cap (`bIncreaseEgoCap`) or toggles `EgoCapAwardsEnabled`
  (instance scan of every `TriggerPlayer` in the 14 maps; one in Map15 did not parse); the 118 plain `TriggerPlayer`s are base-class and award nothing (`EgoCapAwards` base ids
  hit the no-op). `TriggerPlayerDLC03` carries `EgoCapAwardsDLC03` (byte[]) and `DropOnly`.

### Givers per map (instances, class through the super chain; map packages)

| map | awarding instances | note |
|---|---|---|
| 00 | `Toilet_DLC03` x2 [17] | its 7 monitors are `Monitor_Black_A` (base): award nothing |
| 01 | `PornMag_D` [11]; `HandWeight_DLC03` x5 [15]; `PunchingBag_DLC03` x1 [13] | base `Settings_Workout_SpeedBag` x1 beside it awards nothing |
| 02 | none | |
| 03 | `Toilet_DLC03` x3 [17]; `Mirror_DLC03` [18]; `Pinball2` [9]; `dnControl_ArcadeMachine___1` Tag `NukeTankArcadeMachine` (-3355, -2447, -160) [1]; `Details_Generic_Dart` x3 + `DartBoard___0` (-3380, -3090, -118) [4] | the table lists 3 here but the map has no DLC monitor; it lists no 4 |
| 04 | `PornMag_E` (-10909, 2936, -324) [10] | |
| 05 | `Details_HotSauce___0` (1246, 217, 176) [8]; `Monitor_DLC03` x8, four systems OfficeComp0..3 each with a CPU and a keyboard: all wired [3] | |
| 06 | `PornMag_D___1` (-11416, 3232, 165) [11] | `Monitor_DLC03` x2 on OfficeComp0 with a CPU and **no keyboard**: never usable |
| 07 | `HandWeight_DLC03` x9 [15]; `PunchingBag_DLC03` x5 [13]; `WorkoutBench_DLC03` x6 [14]; `dnControl_Deadlift___0` (-576, -245, -264) [5]; `dnControl_PullupBar___0` (-1847, 1088, -199) [12]; `Basketball_Hoop_DLC03` x4 [16]; `Mirror_DLC03` x2 [18]; `ArmwrestleClone___1` (-1785, 710, -277) [2]; `Electronics_Generic_Projector___0` + `_Reel___0` (1558, 1383, -250) [7] | 5 base `SpeedBag` holders award nothing |
| 10 | `Monitor_DLC03___0` NurseryPC with CPU and keyboard: wired [3] | |
| 11 | `Mirror_DLC03` [18]; `DrProton_SpiderBot___0` (304, -10272, -1040) [6, boss] | |
| 12 | `Details_Generic_Dart` x3 + `DartBoard___0` (4687, -6334, 360) [4]; `PornMag_E` (5002, -5442, 337) [10]; `Monitor_DLC03` x2 OfficeComp3, two CPUs, two keyboards: wired [3] | monitors not in the table |
| 13 | `Details_HotSauce___0` (11292, -4443, -215) [8]; event [3]: `TriggerRelay___2` Tag `OfficeMonitor1_RoundRobin` -> `TriggerPlayer_ComputerEgo` in group `02_CraneRoom`, started by a `DecoActivities_Events` of a ScriptObj | chain start not resolved (open 3) |
| 14 | none | |
| 15 | `dnControl_HandSizeFlag___0` (-1935, -4715, -2605) [0] | |

## 4. Collectibles

None. No DLC map holds a `Electronics_Generic_DeskPhone` with a `StartPlayingMessagesEvent` (the 13 desk phones in Maps 00,
01, 02, 06, 07, 13 are all decoys), no `ACH_DiscoveredHelmet*` or `ACH_PhoneMessage*` name appears in any DLC map name table
(only `ACH_Chauvinist` in Map03 and the placeholder `ACH_REPLACEME` in Map11), and `DukePlayerDLC03.InitializeAchievements`
registers only `ACH_Chauvinist` beside the base's. Helmets 3 and phones 12 stay base-only (the `MinerHelmet` name in Map03/14
is a mesh). DLC quest items (`GasCanPickup2` x3 Map03, `MoonBuggy_PowerCellPickup`, `dnControl_PowerCellPickup` Map15) have no
Progress.ini counter.

## 5. Achievements 50-62 against the DLC maps

Every `AwardAchievement(5x/6x)` caller in all packages was listed: 50 (`UpdateAchievementCounter(50, 10)`), 53 (`(53, 30)`),
51 (`UpdatePregnatorGunAchievement`), 52 (`dnSinglePlayerDLC03.Killed`, `(52, 20)`), 56 (`DrProton_SpiderBot.Die`), 57..60
(`UpdateLevelEndAchievements`), 61 (`GivePermanentEgoCapAward_DLC03`), 62 (`TriggerFunc_ACH_Chauvinist`); 54 is
`dnControl_Pinball2`'s class default. achievements.md's rows 50-62 hold; 55 (no karaoke actor in any DLC map) and 63 stay dead.

- **61:** all 19 ids (section 3), `[ACH_EgoAwards]`. **Closed.**
- **62 Hedonist, not Misogynist: the cabinet is `dnDecoration___0`** (exact class `dnDecoration`, Tag `BS3000_ArcadeMachine`,
  DLC03_Map03 at (-3263, -2262, -160)); the look-and-use `Trigger___31` (Tag `BS3000AchTrigger`, (-3263, -2281, -136)) fires
  `BS3000AchDispatcher`; `TriggerAssignUsableSomething___0` (Tag `DisableBS5KMachine`, event `BS3000_ArcadeMachine`) later
  disables it. It is **not** the Nuke Tank cabinet (`dnControl_ArcadeMachine___1`, ego 1) 190 units away. **Closed.**
- Counters (`[Achievements]` keys by `EAchievement` name): `ACH_BubbleBuster` 50 (10 melee kills on an expanded enemy,
  `BloatState != 0`, not `DukeRobot_Metallic`), `ACH_PassiveAggressive` 52 (20), `ACH_MoonBuggyKills` 53 (30);
  51 has no counter. Level-bound rows 53/54/56/62 and Turd Burglar's DLC maps (Map10 x2, Map11 x3 poo) are right.
- Weapon pickups placed (for the Items ESP): `ExpanderPickup` Maps 02 (1), 04 (2), 06 (1), 14 (1), 15 (1);
  `PregnatorGunPickup` Maps 07, 14, 15; `Details_HotSauce` Maps 05, 13 (a grabbed bottle becomes the `HotSauce` weapon).
  How the Expander and the Impregnator fire, bloat and pop, and how the aim classifies them: aim.md section 13.

## 6. How the panel uses it (built)

**Progress tab** (a "Doctor Who Cloned Me" section beside the base table; same scope split, progress.md section 5)
- Chapter table of 14 rows from section 1 (URL, localised title from `MapNames`, the order above). Columns: EGO `N/M` (M from
  the table, N from `[EgoAwards] DLC03EGOAWARD_*` through `DLC03MapEgoCapAwards`), DIFFICULTY pips from
  `[DLCProgress] GameCompletion_<URL>_<d>` (56 `GetBool`s); no HELM/PHONE columns (or "-").
- Run totals: awards `N/19`; cap permanent `40 (+5) + 30 + sum(found caps)` over max 100 (+5), clamp per open 2; now =
  `Pawn.EgoCap`. Profile totals: nothing (no helmets/phones); optionally the ever-found `[ACH_EgoAwards]` count for 61.
- The table is read from the game when possible: `DukePlayerDLC03` defaults (`DLC03EgoCapAwards` 19 floats,
  `DLC03MapEgoCapAwards`, `AutoEgoCapAward`) and `Engine.MapInfo` defaults (`CampaignList` URL/MapNameTag; empty until the
  DLC is mounted); a built-in copy of section 1 is the fallback. The DLC class is not loaded until the DLC is entered:
  the class scan tolerates absence (as `kClassNames`). Chapter-of-map in DLC mode = case-insensitive equality with the URL.
- The 4th difficulty label is the base's; `bDiff4Available` is `HasFullDLC03Completion(0)`.

**Ego boosts ESP** (DLC mode = player class `DukePlayerDLC03`, or the map starts `DLC03_`)
- A second rule set, because the base rules match the DLC subclasses with the wrong ids: Toilet_DLC03/Urinal_DLC03 (17),
  Mirror_DLC03 (18), HandWeight_DLC03 (15), PunchingBag_DLC03 (13, fixed id, not `PermanentEgoAwardID`), WorkoutBench_DLC03
  (14, one id), Basketball_Hoop_DLC03 (16), Pinball2 (9), PornMag_D (11) / _E (10), Monitor_DLC03 (3, the wired-keyboard rule
  of progress.md section 2 carries over: Map06's two never work), dnControl_Deadlift (5), dnControl_PullupBar (12),
  dnControl_HandSizeFlag (0), dnControl_ArcadeMachine (1, class + Tag `NukeTankArcadeMachine`), Details_Generic_DartBoard (4),
  Details_HotSauce (8), ArmwrestleClone (2), Electronics_Generic_Projector (7, the reel: `Electronics_Generic_Projector_Reel`
  until placed). Not markable: 6 (boss kill; the live `DrProton_SpiderBot` is an aim target), 3 in Map13 (open 3).
- Found state from `DLC03EGOAWARD_*` (19 flags, own array; the base `award[35]` stays for base levels). Labels from the enum
  names (`eg_label` handles "PornMagazineE", "DeadLift", "PullUp").
- The "awards open" gate in DLC mode is `bHasCheatedThisLevel` only: `EgoCapAwardsEnabled` is True by default, no DLC map
  clears it, and the DLC giver ignores it.
- Each event rule keeps its listed-by-TriggerPlayer test with `TriggerPlayerDLC03.EgoCapAwardsDLC03` (byte[], name differs from
  the base's `EgoCapAwards`).

**Achievements (achhelp)**
- Row 61 as a COLLECTED row with its own `AH_PROG_EGO_DLC` (19, profile `[ACH_EgoAwards]`), like 29/36/37.
- Rows 50 (profile `ACH_BubbleBuster`/10), 52 (`ACH_PassiveAggressive`/20), 51 (no progress) as `AH_SCOPE_COUNTER` with the
  DLC map list (`DLC03_Map00 .. DLC03_Map15` names) and no world kinds (50/51/52 depend on enemies and a weapon).
- Rows 57..60 as completion rows from the 14 `[DLCProgress]` ticks per difficulty (like 31..34 from the base ticks).
- Row 62 marks the visible cabinet, `{ "dnDecoration", AH_MATCH_TAG, "BS3000_ArcadeMachine", AH_K_EXACT }`, rather than
  the invisible `Trigger` kind (`BS3000AchTrigger`).
- `ah_map_in` takes `DLC03_Map03` style names.
- Items ESP: add `GasCanPickup2`, `MoonBuggy_PowerCellPickup` (IT_QUEST, they derive from `Pickup`/`dnControlQuestItem`, not the
  base quest pickups); the weapon pickups are generic `Pickup`.

## Open questions

1. The name `AddCompletionForLevel(-1)` uses on a level reached by a checkpoint, hence the `[DLCProgress]` key written: finish a
   chapter after a load and read the 4 keys.
2. Permanent ego cap in the DLC after all awards (105 or 100): 40 + 30 + sum against `MaxEgoCap` 100; 70 was measured at
   start with 0 awards.
3. Map13's ComputerPorn chain: what fires `OfficeMonitor1_RoundRobin` (a `DecoActivities_Events` in a ScriptObj of group
   `02_CraneRoom`); Map03's table lists 3 with no DLC monitor in it (N/M there can read x/5 only if 3 was found elsewhere).
4. Whether `Engine.MapInfo.CampaignList` is filled when the Progress tab opens before the DLC menu was ever visited (the
   panel needs the built-in order until then).
5. Armwrestle: which actor holds the award in play (the `ArmwrestleClone` pawn spawns the `dnControl_Armwrestle`; none is
   placed in a map).
6. Whether the placed reel in DLC03_Map07 goes hidden (the reel rule skips a hidden one).

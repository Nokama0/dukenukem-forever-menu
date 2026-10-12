# Achievements: how each of the 64 is awarded, and which level can earn it (build 244)

Of the 64 ids, **18 are bound to one named place in one map** (17, 20, 23, 24, 28, 35, 38..49), 3 are collected
across several maps (29, 36, 37), 18 are cumulative kill/event counters, 6 are "do it once, anywhere the item exists"
(14, 15, 21, 25, 26, 27), 5 are campaign completion (30..34), 13 are DLC (50..62) and 1 is dead (63). Display names and
descriptions are in the local Steam schema, so no runtime query is needed. Five examples: Pescaphobe = Map21,
Companion Barrel = Map17B, Party Animal = Map09, Turd Burglar = Map00/11/13 (and two DLC maps), Noms = a cumulative
counter (10 food items, no single level). The open questions are listed last.

Method: script packages decoded with `tools/ubc.py`, map packages read in place, Ghidra MCP on dnGame.dll, Steam's
local schema file. The Pescaphobe catfish row is verified in game (8 of 15 killed with the targeting).

## 1. Sources and how an award reaches Steam

- **Display strings:** Steam's `appcache\stats\UserGameStatsSchema_57900.bin` (the cached
  schema for the account, read with Python): English name and description per API name, 62 entries. **Not in
  it: `ACH_SingKaraoke` (55) and `ACH_Deprecated2` (63)**, so Steam answers false for both and nothing can unlock
  them. The game's own `.int` files hold none (grep of `System\*.int`: `dngame.int` has only a HUD string). The
  in-game fallback is `GetAchievementDisplayAttribute` (steam-stats.md section 2).
- **One choke point:** `DukePlayer.AwardAchievement(EAchievement)` is native, `ADukePlayer::execAwardAchievement`
  `dnGame.dll!0x1003DE90` (decompiled): it does nothing when `bHasCheatedThisLevel` is set (`this+0x1FE8` bit 0), in
  multiplayer, in **Map_mydigs** (`IsMyDigs`), or for an id >= 64; otherwise it calls the Steam manager's vtable
  slot 1 (`Engine.dll!0x1033A650`, steam-stats.md section 3). Duke's digs therefore never awards anything.
- **Counter path:** `DukePlayer.UpdateAchievementCounter(Id, Threshold)` (dnGame.u): returns on MP or cheating; reads
  `[Achievements] <EnumName>` (e.g. `ACH_PooGrabbed`) from Progress.ini, adds 1, writes it back, awards when the
  count reaches the threshold. The counter is profile-wide (survives New Game, progress.md section 5) and readable
  through GConfig like the helmet flags. The key `NumKills` feeds ids 0..2.
- **Event path:** `DukePlayer.InitializeAchievements` (dnGame.u, `PostBeginPlay`, SP only) registers
  `RegisterSpecialEvent('ACH_<Name>', ...)` for helmets, phones, `ACH_DrankAllBeer`, `ACH_CatfishKilled`,
  `ACH_BlimpDestroyed`, `ACH_TalentKnockedOut`, `ACH_AliensAborted`, `ACH_CompanionBarrel` and the six boss events;
  `DukePlayerDLC03.InitializeAchievements` adds `ACH_Chauvinist`. A map `GlobalTrigger` of that event name runs
  `TriggerFunc_ACH_<Name>`, one `AwardAchievement(N)` call. No gate beyond the native one.

## 2. The table (id = `EAchievement` index = dnGame.u enum order = Engine.dll table order)

Scope: **L** one place in one map; **S** spread over several maps (flags); **C** cumulative counter; **O** once,
wherever the item exists; **D** campaign completion; **DLC** `DLC03_*` maps; **x** unobtainable. Map names are
package names; chapters are progress.md section 1's table (MapNN = chapter NN, `B`/`C` parts share it).

| id | API name | display name | scope | levels | award path |
|---:|---|---|---|---|---|
| 0 | ACH_SomeKills | Extermination (50 aliens) | C | any | `UpdateKillAchievements`: `NumKills >= 50` |
| 1 | ACH_MoreKills | Annihilation (100) | C | any | same, 100 |
| 2 | ACH_ManyKills | Nuclear Devastation (250) | C | any | same, 250 |
| 3 | ACH_StompKills | Hippy-Stomper (12 stomps) | C | any | counter 3/12, `ShrunkSmashDamage` kill |
| 4 | ACH_Executions | Judge, Jury, Executioner (20) | C | any | counter 4/20, `ExecutionDamage` kill (executions.md) |
| 5 | ACH_TripMineKills | Trapper (10 trip mine kills) | C | any | counter 5/10, `LaserMineDamage` |
| 6 | ACH_FrozenKills | Freeze Well! (15 frozen) | C | any | counter 6/15, `Other.IsFrozen()` |
| 7 | ACH_MonsterTruckKills | Road Rage (15) | C | Map12, 12B, 13, 14, 14B, 15 | counter 7/15, `VehiclePhysicsImpactDamage`, vehicle class name exactly `Veh_MonsterTruck` (Map02's RC truck is `Veh_MonsterTruckRC`: no) |
| 8 | ACH_ForkliftKills | Fork the Pork (6) | C | Map17, 17B | counter 8/6, `Vehicle.IsA('Veh_Forklift')` vehicle impact |
| 9 | ACH_ExplosiveKills | Dead Useful (10) | C | any | counter 9/10, `DecoExplosionDamage` |
| 10 | ACH_PhysicsImpactKills | Tosser... (10) | C | any | counter 10/10, `ThrownObjectImpactDamage` |
| 11 | ACH_SteroidKills | Duke Angry, Duke Smash! (15) | C | any | counter 11/15, `MeleeDamage` while `bOnSteroids` |
| 12 | ACH_FoodEaten | Noms (10 food) | C | Map01, 08, 09, 19 (3.12) | counter 12/10 from `NotifyFoodEaten` (`Snacks_Generic.Consume`, `Snack_Popcorn.Grabbed`) |
| 13 | ACH_HeadShotKills | On the Noggin' (30) | C | any | counter 13/30, `BulletDamage` on a head body part |
| 14 | ACH_SubstanceAbuse | Substance Abuser | O | beer + steroids | `CheckSubstanceAbuse`: `bOnSteroids` and `DrunkLevel > 0`, from `StartSteroids` and `SetDrunkEffect` |
| 15 | ACH_TripleKill | Natural Disaster 3x | O | any | `UpdateKillAchievements`: `FrameKills >= 3` within 0.1 s (0.2 s console) |
| 16 | ACH_KnockedDown | Full Body Tourettes (10) | C | any | counter 16/10 from `dnControl_PlayerKnockdown.idle.AnimCallback_UserAnimEnd` |
| 17 | ACH_PooGrabbed | Turd Burglar | L | Map00, Map11, Map13 (+DLC03_Map10/11) | counter 17/1 from `Biology_Generic_Poop_A.GrabComplete` |
| 18 | ACH_JizzedOn | I Need a Towel (10) | C | any | counter 18/10 from `dnJizzBombFX_Impact_HUDFX.InRangeCallback` |
| 19 | ACH_AlienFighterKills | Baron von Nukem (20) | C | Map05 (one placed; others not settled) | counter 19/20 from `dnFlybyPlane_Fighter.TakeDamage` |
| 20 | ACH_CalendarFullyViewed | I Need a Date | L | Map16, Map17B | `Details_Generic_Calendar.UpdatedViewedMonths`: 6 months on one calendar |
| 21 | ACH_TripmineStuck | Sticky Bomb Like You! | O | any | `LaserMine.ArmMineOnPawn`: mine mounted on a hostile pawn |
| 22 | ACH_GunskipKill | Big Guns, Big Ships (5) | C | many (3.19) | counter 22/5 from `dnAlienGunship`/`dnAlienDropship.Destroyed` with an `EngagedPlayer` |
| 23 | ACH_AirHockeyWon | Air-Duke (7-0) | L | Map09 | `dnControl_AirHockeyPlayer.GameOverWon.BeginState` |
| 24 | ACH_PinballScore | Balls of Steel (1,000,000) | L | Map02, Map09 | `dnControl_Pinball.AwardPoints`: `Score >= 1000000`, id = class default 24 |
| 25 | ACH_BeerImbibed | Flagon of Chuckles | O | beer levels | `SetDrunkEffect` |
| 26 | ACH_SteroidsEaten | Juiced | O | steroid levels | `StartSteroids` |
| 27 | ACH_HolodukeActivated | He's Got a Hologram! | O | holoduke levels | `NotifyHoloDukeStart` (SP) |
| 28 | ACH_WhiteboardDrawing | Drawrings | L | Map00, 09, 16B, 19, 20 | `dnControl_Whiteboard.Drawing.Tick` |
| 29 | ACH_MaxEgoAchieved | I Am All That Is Man | S | ego givers (progress.md) | `NoteDiscoveredEgoAward` when all 35 `[ACH_EgoAwards]` are set |
| 30 | ACH_GoldPistolCompletion | Gunslinger | D | pickup in Map02, then every chapter | native, 3.16 |
| 31 | ACH_EasyCompleted | Piece of Cake | D | all | native `UpdateLevelEndAchievements`, `HasFullGameCompletion(0)` |
| 32 | ACH_NormalCompleted | Let's Rock | D | all | same, (1) |
| 33 | ACH_HardCompleted | Come Get Some | D | all | same, (2) |
| 34 | ACH_ExpertCompleted | Damn, I'm Good (Insane) | D | all | same, (3) |
| 35 | ACH_CreditsViewed | Special Thanks | L | Map22 | `LevelInfo.DrawActiveBink` -> `EndCreditsFullyViewed` for an end-credits bink |
| 36 | ACH_HelmetsDiscovered | Bucket Head | S | Map05, 07, 21 | `CheckHelmetsDiscovered` (progress.md section 6) |
| 37 | ACH_PhoneMessages | Call Waiting | S | 9 maps | `UpdatePhoneMessagesAchievement` (progress.md section 6) |
| 38 | ACH_DrankAllBeer | Party Animal | L | Map09 | event `ACH_DrankAllBeer` from `Counter___0` |
| 39 | ACH_CatfishKilled | Pescaphobe | L | Map21 | event `ACH_CatfishKilled` from `Counter___0` |
| 40 | ACH_BlimpDestroyed | Sunday, Black Sunday | L | Map00 | event, `Dispatcher___9` |
| 41 | ACH_TalentKnockedOut | Nobody Likes a Whiner | L | Map01 | event, `Dispatcher___199` |
| 42 | ACH_AliensAborted | Lots of Whacking | L | Map09 | event, `Abortion_ACH_Dispatcher` (`Dispatcher___21`) |
| 43 | ACH_CompanionBarrel | Companion Barrel | L | Map17B | event, `Dispatcher___19` |
| 44 | ACH_CycloidDefeated0 | One-Eyed Freak | L | Map00 | event, `Dispatcher___79` |
| 45 | ACH_BattlelordDefeated0 | Pit Champion | L | Map05 | event, `Dispatcher___32` |
| 46 | ACH_AlienQueenDefeated | Not Bad for a Human | L | Map08 | event, `Dispatcher___18` |
| 47 | ACH_BattlelordDefeated1 | A Good Dam Fight | L | Map15 | event, `Dispatcher___4` |
| 48 | ACH_OctakingDefeated | Octacide | L | Map18 | event, `Dispatcher___46` |
| 49 | ACH_LeechDefeated | Beating the One-Eyed Worm | L | Map21 | event, `Dispatcher___74` |
| 50 | ACH_BubbleBuster | Bubble Buster (10 melee kills on expanded enemies) | DLC | any DLC03 map | `DukePlayerDLC03.UpdateKillAchievements`: counter 50/10, melee on `BloatState != 0` except `DukeRobot_Metallic` |
| 51 | ACH_Threesome | Threesome (3 kills in 3 s, Impregnader) | DLC | any | `UpdatePregnatorGunAchievement` |
| 52 | ACH_PassiveAggressive | Passive Aggressive (20 deaths by others) | DLC | any | `dnSinglePlayerDLC03.Killed`: counter 52/20 when neither killer nor victim is Duke and their factions differ |
| 53 | ACH_MoonBuggyKills | Bloody Red Rover (30) | DLC | DLC03_Map15 (3 buggies) | counter 53/30, `Veh_MoonBuggy` impact kills |
| 54 | ACH_SciencePinballScore | Scientits (1,000,000) | DLC | DLC03_Map03 | `dnControl_Pinball2.AwardPoints` (id = class default 54) |
| 55 | ACH_SingKaraoke | (not in Steam's schema) | x | none | `dnControl_Karaoke_Machine.CheckAchievement` assigns a local and returns: the award call is gone; no karaoke machine instance in any DLC03 map |
| 56 | ACH_DefeatDrProton | Heart to Heart (Finish Doctor Proton) | DLC | DLC03_Map11 | `DrProton_SpiderBot.Die` calls `AwardAchievement(56)` |
| 57 | ACH_EasyCompletedDLC03 | Another Piece of Cake | DLC | DLC03 campaign | `DukePlayerDLC03.UpdateLevelEndAchievements`, `HasFullDLC03Completion(0)` |
| 58 | ACH_NormalCompletedDLC03 | Let's Rock Out | DLC | same | (1) |
| 59 | ACH_HardCompletedDLC03 | Come Get a Little More | DLC | same | (2) |
| 60 | ACH_ExpertCompletedDLC03 | Damn, I'm REALLY Good | DLC | same | (3) |
| 61 | ACH_MaxEgoAchievedDLC03 | I Am All That Is Man - Again! | DLC | DLC ego givers | `GivePermanentEgoCapAward_DLC03` when all `[ACH_EgoAwards]` DLC ids are set |
| 62 | ACH_Chauvinist | Hedonist, not Misogynist | DLC | DLC03_Map03 | event `ACH_Chauvinist`, `Dispatcher___97` |
| 63 | ACH_Deprecated2 | (not in Steam's schema) | x | none | no caller |

Every "event, `Dispatcher___N`" row names the dispatcher in that map whose `OutEvents` holds `ACH_<Name>` (found
with a tagged-property reader over the map package, instances only).

## 3. Level-bound targets

Positions are the placed `Location` (x, y, z) in the map package; moving actors move away from it. Class names are
exact export classes. "Readable" = a value the panel can read without writing anything.

### 3.1 Turd Burglar (17): Map00, Map11, Map13
- Target: `Biology_Generic_Poop_A` (Tag `Biology_Generic_Poop_A`). Map00 (ch 0, group `01_Showers`): `___0`
  (2447, -659, -47), `___2` (2512, -668, -44). Map11 (ch 11, `NEW_Intro_Hallway`): `___0` (-315.5, 534.2, 15394.1).
  Map13 (ch 13, `90_Ghost_Town`): `___1` (-9157, 40748, 16), `___0` (-9150, 40750, 18). DLC: DLC03_Map10 x2 and
  DLC03_Map11 x3 (group `SewerExit`).
- Action: grab (Use-hold) any piece. `GrabComplete` runs `PlayTurdLine`; if the instance's `HasBeenGrabbed` is false
  it sets it and calls `NotifyPooGrabbed` -> counter 17/1. Readable: per-instance `HasBeenGrabbed` (reflection);
  profile counter `[Achievements] ACH_PooGrabbed` (0/1). Missable: no, any one piece in any of the maps is enough.

### 3.2 Companion Barrel (43): Map17B (ch 17, The Forkstop)
- Target A: `Barrels_Generic_Clean01___2` (Tag `CompanionBarrel`, group `01_DamInt_Garage1_FirstHallway`) at
  (968, -9960, -7208), Mass 40. Target B: `KBoxSensor___1` (group `01_DamInt_Garage2_ExitHallway`) at
  (-1704, -9478, -7226), box 16 x 16 x 4, `OnlySenseTagged` = `CompanionBarrel`, `SensorDisableTag`
  `StopLookingForLove`.
- Condition: bring the tagged barrel into that box. `BeginSenseEvent` fires `CompanionBarrel_Dispatcher`
  (`Dispatcher___19`): `StopLookingForLove`, `CompanionBarrel_Hint`, `CompanionBarrelFixture`,
  `ToggleCompanionBarrelDoorLock`, `OpenCompanionBarrelDoor`, then `ACH_CompanionBarrel` (delays 0, 0, 2, 0, 0.12, 1).
  The door opens the closet the description names.
- Readable: the barrel's live `Location` against the sensor's; no counter. Missable: yes if the barrel is destroyed
  before delivery (whether it can be is open).

### 3.3 Party Animal (38): Map09 (ch 9, Titty City)
- Targets: 9 `Details_Generic_DrinkCan` (Tag `StripClubBeer`, `Event` `StripClubBeerDrank`): `___0`
  (-719, -597, 134.8), `___3` (1174, 1, -81), `___4` (-506, 185, -84), `___5` (205, 544, -84), `___6` (29, 641, -79),
  `___7` (198, 541, -84), `___8` (1454, 335, -82.3), `___10` (-580, 653, -40), `___11` (215, -42, -74). Sink:
  `Counter___0` (Tag `StripClubBeerDrank`, group `BeerAcheivementStuff`, `NumToCount` 9, `Event`
  `ACH_DrankAllBeer`) at (1043, 602, -75).
- Condition: nine can events reach the counter; `Counter.Trigger` decrements `NumToCount` and at 0 `CountComplete`
  fires `ACH_DrankAllBeer`. `Details_Generic_DrinkCan.GrabComplete` gives Duke a `DrinkCan` (a `FortyOunceBeer`
  weapon) and destroys the can. No decoded function raises the can's `Event`; whether it fires at pick-up or
  when the drink finishes is open.
- Readable: `NumToCount` (progress = 9 - value); cans left = alive `Details_Generic_DrinkCan` with that Tag.

### 3.4 Pescaphobe (39): Map21 (ch 21, Blowin' the Dam)
- Targets: the map's 15 `Biology_Generic_Catfish` (class count = 15, all with `Event` `catfishCounter`, group
  `02_DamExterior`; placed from (1752, 6816, -948) to (8184, 5960, -1016), depths -500 to -1516; fish swim). Sink
  `Counter___0` (Tag `catfishCounter`, `NumToCount` 15, `Event` `ACH_CatfishKilled`) at (10836, 6041, -1024).
- Condition: kill all 15; each death triggers its `Event`. Readable: `Counter___0.NumToCount` (progress = 15 - value) and
  the live catfish count. Missable: not by the script (no despawn found); a reload restores the level state.
- The fish (aim.md section 10): `Biology_Generic_Catfish` < `Biology_Generic` < `Nature_Generic` <
  `aFinalDecoration` < `dnDecoration`, not a pawn; `HealthPrefab` 1 (`HealthCap` 1), `PHYS_MovingBrush`, collision
  19 / 10, no damage type list or `bNoDamage` on the class or the instances: any hit kills. While this row is on
  (switch on, or auto while unearned) in Map21, Silent aim, the aimbot and the Triggerbot take the live fish as a
  last-resort target after pawns and ships (`AH_K_AIM`, `ah_aim_kinds`). Verified in game: 8 of 15.
- Checkpoint loads: after a checkpoint load (Map21 -> Entry -> Map21, serial 4) a scan that kept its name index
  found no fish and no Counter for the rest of the session, while class-matched kinds (the beer) still came back. Both
  lost kinds match by a key name (`catfishCounter`, the fish's Event and the Counter's Tag); the Counter's class is
  Engine's, so only the key can fail. The map's names go with the level: `catfishCounter` moved from 79488 to 86824
  across a checkpoint load (engine-map.md section 2). So achhelp looks every class and key name up before each pass
  (`ah_resolve_names`); `achhelp: scan filter, level serial N: map, kinds here, keys name=index (moved)` logs it per
  serial.

### 3.5 Blimp (40): Map00 (ch 0)
- Target: `dnDecoration___8` (Tag `blimp`, group `13_Blimp`, mesh `c_generic.blimp`, `Event`
  `Dispatcher_Blimp_Destroyed`), moving (placed (-3405, -3888, 3904), mounted on `DumbActor_Blimp_Rotator`),
  `Health` 5, `DamageTypesAcceptedOnly` = `DevastatorDamage` only, collision radius 1400.
- `Dispatcher___9` fires `ACH_BlimpDestroyed`. A debug `TestingTrigger___32` (-3020, 156, -1024) raises the same
  event. Readable: its `Health`. Missable: the Cycloid's death hides the blimp (`Dispatcher_Cycloid_Dead` fires
  `TriggerToggleHidden_Blimp_Hide`), so do it before; also needs a Devastator.

### 3.6 Talent (41): Map01 (ch 1)
- Target: `Workman___8` (Tag `Talent_Backstage`, group `05_Auditorium_Script_New`, placed (2258, 3452, 15), starts
  `bHidden`, Health 1, `Event` `Dispatcher_Rant_Punched`). `Dispatcher___199` (that Tag) fires the fist-disable
  events, dialogs and `ACH_TalentKnockedOut`. `TriggerDestroy___23` (group `06_Hallways`, `AdditionalEvents` incl.
  `Talent_Backstage` and `Dispatcher_Rant_Punched`) removes him when the studio door closes: missable.
- Readable: his `bHidden` (unhidden when the rant scene runs) and `bDeleteMe`.

### 3.7 Alien Abortion (42): Map09
- Target: `Switches_Generic_PushSwitch_B___1` (Tag `SwitchAlienAbortReset01`), which the ego ESP marks
  (progress.md section 7). Winning (score >= 85 of 99, autowin.md) fires `DisExpertScore` (`Dispatcher___164`) ->
  `Abortion_ACH_Dispatcher` (`Dispatcher___21`, once) -> `ACH_AliensAborted`. Readable: the game's score
  `Variable`s (autowin.md). Missable: no (the machine resets). Map_mydigs has the game and never awards.

### 3.8 Air-Duke (23): Map09
- `AirHockeyTable___1` (Tag `PuckIt`, group `04_GameRoom`) at (-542, 821, -63). `GameOverWon` awards 23 only when
  the opponent has 0 (autowin.md); ego award 5 comes with any win. Readable: `PlayerInfo[i].Score`. Not missable.

### 3.9 Balls of Steel (24) and Scientits (54)
- `dnControl_Pinball___0` Map02 (group `04_RecRoom`, (214, 743, -500)); `dnControl_Pinball___1` Map09 (group
  `04_GameRoom`, (-960.5, 397.8, -60.3)); DLC: `dnControl_Pinball2___1` DLC03_Map03 at (-2941, -2294, -164.3).
  `AwardPoints` adds to `Score`; at `>= 1000000` it awards the `Achievement` property (class default 24 for
  `dnControl_Pinball`, 54 for `dnControl_Pinball2`: the tagged default read from each class export's bytes).
  Readable: `Score`, `highscore`. Not missable. (Map_mydigs has a table; no award there.)

### 3.10 Drawrings (28)
- `dnControl_Whiteboard` Map00 `___0` (1415.1, -511.4, 6.4); Map09 `___1` (136, 712, -58); Map16B `___0`
  (-756, 592, 378); Map19 `___0` (-208, 528, -1192); Map20 `___1` (6679, -6336, -4642). DLC03_Map07's
  `dnControl_Whiteboard_DLC03` only sets `bAchievementAlreadyUnlocked`. Draw with a pen. Not missable.

### 3.11 I Need a Date (20)
- `Details_Generic_Calendar___1` Map16 (group `03a_EntranceHubRearOffice`, (1044, 588, 644)); `___0` Map17B (group
  `01_DamInt_Garage2_FinalRoom`, (-352, -10144, -7280)). `UpdatedViewedMonths` marks `MonthViewed[Month]`, counts
  `MonthsViewed`; at 6 it awards 20 to the `EventInstigator`. Readable: `MonthsViewed` per calendar (instance state:
  a reload rolls it back). Not missable.

### 3.12 Noms (12)
- Cumulative; profile counter `ACH_FoodEaten` / 10. Food in the base game (class counts through `Snacks_Generic`):
  Map01 `Snack_Popcorn` x5, `Snack_Donut` x6, `VendSnack_HotChihuahuas` x3; Map08 `VendSnack_Donuts` x2; Map09
  `Snack_Popcorn` x5, hot chihuahuas x1, `Details_Generic_ChineseToGoBox` x3; Map19 chinese box x1. A popcorn with
  `bQuestItemHack` becomes a quest item instead and is not counted. Chapter 1 alone has 14.

### 3.13 Bosses (44..49)
Each dispatcher fires on the boss's scripted death; the ESP target is the live boss pawn, not the dispatcher.
| id | map | dispatcher (Tag) | note |
|---:|---|---|---|
| 44 | Map00 | `Dispatcher___79` (`Dispatcher_Cycloid_Dead`), group `14_Field_Cycloid` | |
| 45 | Map05 | `Dispatcher___32` (`Battlelord_EndBattle_Dispatcher`, once), group `70_Arena` | also `Arena_Exit_IncreaseEgo` (ego 27) |
| 46 | Map08 | `Dispatcher___18` (`ACH_QueenDispatcher`, once), group `Arena_Queen` | |
| 47 | Map15 | `Dispatcher___4` (`Battle_Lord_Dead_Dispatcher`) | `TriggerEgo` (ego 32) |
| 48 | Map18 | `Dispatcher___46` (`Dispatcher_Add_Ego_OldGuyLines`) | `TriggerEgo` (ego 33) |
| 49 | Map21 | `Dispatcher___74` (`LeechDeathSFXDis`) | |

The autowin QTE finishers lead to the same events (autowin.md). Not missable (the fights are mandatory).

### 3.14 Credits (35): Map22
- `TriggerBink_Credits` and `bEndCredits` are names in Map22; `DrawActiveBink` calls `EndCreditsFullyViewed` when an
  end-credits bink plays to the end; skipping it does not count. Not missable.

### 3.15 Helmets (36) and phones (37)
progress.md section 6 (already marked by the panel).

### 3.16 Gunslinger (30)
- Pickup: `PistolPickup_Gold___0` (Map02, group `09_ReactorRoom_Gun`, Tag `PistolPickup_Gold_Ammo_Arm_01`, `Event`
  `Dispatcher_Ammo_Arm_01_Close`) at (-791, -1, -1370); items.md: the only one placed.
- Native `ADukePlayer::UpdateLevelEndAchievements` `dnGame.dll!0x10040200` (decompiled): at each level end, if the
  pawn holds an inventory item named `Pistol_Gold`, it records completion index 4 for the current level
  (`GameCompletion_4_<part>` in Progress.ini) and for levels 0 and 1 (`AddCompletionForLevel(4,0)`, `(4,1)`), then
  awards 30 when `HasFullGameCompletion(4)` (`0x1003EB90`: the same masks 0x7FFFFF / 0x3B58D0 / 0x810 as the real
  difficulties) holds. So: take the gold pistol in Map02 and still hold it at the end of every later level.
  Missable: swapping it out (the two-weapon limit drops the weapon, items.md) before a level end leaves that level
  uncounted.

### 3.17 Completion (31..34, 57..60)
Native, per level end: for each difficulty from `[Difficulty] AcknowledgedDifficulty` down to 0 the level's bit is set
in `GameCompletion_<d>_<part>`; then the four `HasFullGameCompletion(d)` checks call the award for 31..34. The DLC
does the same in script (`DukePlayerDLC03.UpdateLevelEndAchievements`, `[DLCProgress]`) for 57..60. Shared with the
Progress tab (progress.md section 3).

### 3.18 DLC level-bound
- **Hedonist, not Misogynist (62), DLC03_Map03:** `Trigger___31` (Tag `BS3000AchTrigger`, `TriggerType` 6, `LookUseTags`
  `BS3000_ArcadeMachine`, `LookUseEvents` `BS3000AchDispatcher`, once) at (-3263, -2281, -136) fires `Dispatcher___97`
  (`DisableBS5KMachine`, `BS3000DukeDialog`, `ACH_Chauvinist` after 3.5 s) at (-3213, -2252, -75). The player looks at
  and uses the machine tagged `BS3000_ArcadeMachine` (actor not located).
- **Heart to Heart (56), DLC03_Map11:** `DrProton_SpiderBot___0` (group `Proton_Arena`, `Event`
  `Dispatcher_ProtonDead`) at (304, -10272, -1040); `Die` awards 56. `Dispatcher___40` also fires `ACH_REPLACEME`,
  a placeholder no function registers.
- **Bloody Red Rover (53):** `Veh_MoonBuggy` x3 in DLC03_Map15 (`___0` (31368, -8700, -1748), `___1`
  (31368, -8300, -1760), `___2` Tag `Veh_MoonBuggy_Busted` (-1944, -3880, -2512)); 30 impact kills.
- 50 and 51 have no map instance to mark (they depend on enemies and the weapon).

### 3.19 Big Guns, Big Ships (22)
`dnAlienDropship`/`dnAlienGunship.Destroyed` count only when `DamageTaken >= HealthCap` and `EngagedPlayer` is set.
Placed: dropships in Map01 (1), 03 (4), 04B (4), 05 (6), 06 (3), 10 (4), 11C (6), 12 (3), 12B (3), 13 (3), 14 (1),
14B (1), 22 (3); gunships in Map05 (2), 06B (2), 10 (1), 12B (3), 14 (4), 14B (1), 15 (2). aircraft.md has the
damage-type limits (explosives and the MG42).


## 4. For the helper tab (design input)

- "This level's achievements" = the rows above whose map is the current package (checkpoint `cpNN`: use
  `LevelInfo.MapFileName`, progress.md section 3), minus those already unlocked in Steam
  (`GetAchievementAndUnlockTime`, steam-stats.md), and nothing in Map_mydigs, where nothing awards.
- Cumulative counters (kills, Noms, Dead Useful, ...) are listed with their Progress.ini counter
  (`[Achievements] <ACH_Name>`), not marked in the world.
- ESP rules that survive rescans: poo by class; the companion barrel by Tag; `Counter` by Tag with `NumToCount` as
  progress; catfish by `Event`; cans by Tag; pinball, whiteboard, calendar and air hockey by class; the gold pistol by
  class `PistolPickup_Gold`; the spider bot and moon buggies by class.

## Open questions

- Party Animal: whether pick-up or finishing the drink raises the can's `Event`. Grab one can and read
  `Counter___0.NumToCount` before and after drinking it.
- Companion Barrel: whether the barrel can be destroyed (its `bNoDamage` and `Health`, read live).
- Gunslinger: that `Pistol_Gold` survives level transitions (read `GameCompletion_4_0`, then `GameCompletion_4_*`,
  after the next level end).
- Chauvinist: the arcade cabinet tagged `BS3000_ArcadeMachine` in DLC03_Map03 (search by Tag at runtime).
- Baron von Nukem (19): only one `dnFlybyPlane_Fighter` is placed (Map05); script-spawned fighters elsewhere were
  not enumerated.
- Karaoke (55): confirmed absent from Steam's schema and stubbed in script; no native caller was searched beyond
  `execAwardAchievement`, whose only users are script calls.
- Blimp: whether it can still be shot after the Cycloid dies (read its `bHidden`).
- The Noms count assumes a vending-machine item reaches `Snacks_Generic.Consume`; not exercised.

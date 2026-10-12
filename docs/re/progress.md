# Progress: ego awards, chapter completion, award givers (build 244)

DNF single player keeps two kinds of progress, both in `Progress.ini` and both readable through the game's own
config cache without writing anything. **Ego awards** (35, `EEgoAwardID`) are global flags (`[EgoAwards]
EGOAWARD_<name>`), not per level and not per difficulty; the game's own Chapter Select shows "Ego Items
Discovered: N/M" per chapter from a per-chapter table of award ids held in class defaults (24 chapters, 65 entries,
34 distinct ids). **Chapter completion** is per difficulty: `[Progress] GameCompletion_<diff>_<part>` is a bitmask
over chapters 0..22, with part 0/1/2 = `mapNN` / `mapNNb` / `mapNNc`. DNF single player has **no secrets** (no
`SecretArea` in any SP map; the LCD's "N/M" is the ego-award count). Ego cap: 40 by default, each award raises it by
a fixed amount (sum 60), maximum 100 (105 with the preorder code). Award givers are 23 classes with a fixed award id
(the poker machine's `VideoPoker` included, section 7), two punching-bag classes with `PermanentEgoAwardID` in their
defaults, and map-placed `TriggerPlayer`s (`EgoCapAwards`) for the event awards; four of those (book signing, alien
abortion, porno, stripper) are started by using an actor, which the ESP marks (section 7).
**Save vs profile (section 5):** `[EgoAwards]` is per playthrough (New Game clears it, a reload rolls back what was
found since the last checkpoint); chapter completion, helmets and phones are per profile and survive a New Game.
The DLC's progress is built beside this (docs/re/dlc.md): `pg_chapter_of_map` matches the whole map name against a
URL before the 5-character cut below, so `DLC03_MapNN` finds its DLC chapter and every base map keeps its chapter.

Method: `extracted-packages/*.u` and `Maps\*.dnf` (both UE1 packages; maps read in place, read only) through
`tools/ubc.py` plus a scratch tagged-property reader; Ghidra MCP on dnGame.dll, Engine.dll, DukeForever.exe
(renames and plate comments saved); dnCommon.dll's export table read with Python.
**Property tag format:** a DNF dynamic-array tag (type 9) carries the inner property's class name
(compact name index: `ByteProperty`, `FloatProperty`, `StructProperty`) between the info byte and the size;
struct elements of an array are tagged property lists ending in `None`.

## 1. Ego awards

### The 35 ids (Engine.u enum `Object.EEgoAwardID`)

| id | name | cap + | giver | id | name | cap + | giver |
|---|---|---|---|---|---|---|---|
| 0 | Toilet | 1 | toilet/urinal (use) | 18 | PunchingBag | 1 | punching bag (melee) |
| 1 | PokerWin | 1 | video poker, a paying hand (section 7) | 19 | SpeedBag | 1 | speed bag (melee) |
| 2 | SlotsWin | 2 | slot machine win | 20 | WallBoobs | 1 | wall boobs (slap) |
| 3 | WorkoutBench | 1 | bench (with 4) | 21 | Mirror | 1 | usable mirror |
| 4 | WorkoutBenchMaxLift | 2 | bench, full lift | 22 | Microwave | 2 | microwave (finish nuking) |
| 5 | AirHockeyWin | 3 | air hockey win | 23 | PoolWin | 2 | pool table win |
| 6 | PayBinoculars | 1 | pay binoculars | 24 | BasketBall | 1 | basketball hoop (score) |
| 7 | CopyMachine | 1 | copier | 25 | ComputerPorn | 1 | computer monitor (switch screen) |
| 8 | GloryHole | 2 | glory hole | 26 | BookSigning | 2 | TriggerPlayer (Map01) |
| 9 | PinballHighScore | 3 | pinball | 27 | DefeatBattlelord1 | 3 | TriggerPlayer (Map05) |
| 10 | HandWeight | 2 | hand weights (curl) | 28 | DefeatAlienQueen | 4 | TriggerPlayer (Map08) |
| 11 | Calendar | 1 | calendar (use) | 29 | AlienAbortion | 1 | TriggerPlayer (Map09) |
| 12 | PaperAirPlane | 1 | paper airplane (throw) | 30 | DukePorno | 2 | TriggerPlayer (Map09) |
| 13 | Frisbee | 1 | frisbee / duct tape / plate (throw) | 31 | TipStripper | 1 | TriggerPlayer (Map09) |
| 14 | PornMagazineA | 1 | magazine A (grab) | 32 | DefeatBattlelord2 | 4 | TriggerPlayer (Map15) |
| 15 | PornMagazineB | 1 | magazine B (grab) | 33 | DefeatOctaking | 3 | TriggerPlayer (Map18) |
| 16 | PornMagazineC | 1 | magazine C (grab) | 34 | DefeatLeech | 4 | TriggerPlayer (Map21) |
| 17 | SmokeCigar | 1 | cigar (puff; `CigarPickup` only) | | | | |

"cap +" = `DukePlayer.EgoCapAwards[id]` (dnGame.u class defaults, a 35-float array, sum 60).

### Per-level or global

- **Global within a playthrough** (New Game clears it: section 5). `DukePlayer.HasDiscoveredEgoAward(id, optional bForAchievement)` = `GetConfigBool('EgoAwards',
  GetEnum(EEgoAwardID, id), Found, 'Progress.ini')` (dnGame.u, decoded). One key per award: finding a toilet in
  any level spends award 0 everywhere. Not per difficulty.
- **Write path (the game's, for reference):** `NoteDiscoveredEgoAward(id)` returns at once if
  `bHasCheatedThisLevel` (`PlayerPawn.AmICheating` is `return false`, but `DukePlayer.AmICheating` =
  `LevelInfo.InstaGib || bGodMode || bInfiniteAmmo`; `PreClientTravel` and `SaveComplete` store it, and the Extras
  menu's God mode / Infinite ammo / InstaGib boxes set the flag on leaving (`UDukeSceneExtraSettings.NavigateBack`).
  The same flag stops `UpdateKillAchievements` and `UpdateAchievementCounter`); otherwise sets
  `[EgoAwards]` and `[EgoAwardsSession]`, raises the cap (`GiveEgoCapAward(EgoCapAwards[id])`), refreshes the LCD,
  then the achievement sections `[ACH_EgoAwards]`/`[ACH_EgoAwardsSession]` and achievement 29 when all are found.
  The `*Session` sections are removed at a checkpoint save (`Engine.dll!0x1011938C`, the save path; also
  `dnSaveLoad_RemoveSessionConfigSections` `0x101191DE` xref).
- Natives route there: `ADukePlayer::execGivePermanentEgoCapAward` `dnGame.dll!0x10040060` (plate comment): single
  player only and only while bit 0 of `this+0x2530` is set (by its use `DukePlayer.EgoCapAwardsEnabled`, which a
  `TriggerPlayer` can toggle through `EgoCapAwardsEnabledModifier`), then event `NoteDiscoveredEgoAward`.

### The per-chapter table

Two copies of the same data, both in class **defaults** (serialized with the package, so present without any LCD):
- `Engine.LCDHelper.Chapters` (array of `LCDHelper.SChapterEntry {string URL; array<byte> Awards}`), the table
  `ULCDHelper::SPSetEgoAwardsForLevel` `Engine.dll!0x10365E20` searches at `this+0x28` (0x18-byte entries: URL
  dnString +0, Awards dnArray<byte> +0xC; the map name is lower-cased and cut to 5 chars, so `map04b` -> `map04`).
  That function only runs with an LCD (`DAT_1097CCEC`); the class default object holds the table regardless.
- `dnWindow.UDukeSceneChapterSelect.Chapters` (`SChapterEntry {URL, SShotPaackage, Shot, Awards, IsDLC}`), used by
  `CheckEgoAwardAndSetDifficulty`: count = awards of chapter i for which `Duke.HasDiscoveredEgoAward` is true;
  `Paint` draws `EgoItemText @ count / Awards.Num()` ("Ego Items Discovered: N/M"). Localised titles:
  `ChapterNames` (`System\dnwindow.int` `[UDukeSceneChapterSelect]`).

| ch | URL | title | award ids |
|---|---|---|---|
| 0 | map00 | Duke Lives | 21, 0, 23 |
| 1 | map01 | Damn! It's Late... | 21, 0, 26, 22 |
| 2 | map02 | The Duke Cave | 21, 10, 4, 9, 19, 18, 24 |
| 3 | map03 | Mothership Battle | -- |
| 4 | map04 | The Lady Killer | 2 |
| 5 | map05 | Vegas in Ruin | 27 |
| 6 | map06 | The Duke Dome | 7 |
| 7 | map07 | The Hive | 20 |
| 8 | map08 | Queen Bitch | 10, 4, 18, 20, 28 |
| 9 | map09 | Duke Nukem's Titty City | 21, 22, 9, 1, 2, 0, 7, 23, 24, 5, 29, 8, 30, 31 |
| 10 | map10 | Crash Course | 13 |
| 11 | map11 | The Duke Burger | 0, 21, 17, 14, 24 |
| 12 | map12 | The Mighty Foot | 25, 13, 6 |
| 13 | map13 | Ghost Town | 16 |
| 14 | map14 | Highway Battle | -- |
| 15 | map15 | Dam Top | 12, 32 |
| 16 | map16 | The Shrunk Machine | 12, 11 |
| 17 | map17 | The Forkstop | 15, 11 |
| 18 | map18 | Generator Room | 0, 21, 33 |
| 19 | map19 | Underground | 20, 12, 16, 25 |
| 20 | map20 | The Clarifier | 14, 17 |
| 21 | map21 | Blowin' the Dam | 15, 12, 34 |
| 22 | map22 | Final Battle | -- |
| 23 | map23 | Press Conference | -- |

- 65 entries, 34 distinct ids; **id 3 (WorkoutBench) is in no chapter** (the bench grants 3 and 4 together:
  `dnControl_WorkoutBench.FullyLifted`, at `LiftWeight >= 600` by the decode). A whole-game "35/35" therefore comes
  from the enum, a per-chapter "N/M" from this table.
- `Map_mydigs` (Duke's digs, 13 givers) and `Map_changeroom` are in no chapter.

### Reading found state, and its cost

- **Path A (preferred): the config cache directly.** `GConfig` = `dnCommon.dll!0x1009EA54`
  (`?GConfig@@3PAVdnConfigCache@@A`, export). `UObject::execGetConfigBool` `Engine.dll!0x10021E30` calls
  `GConfig->vt[1]` (offset +4), thiscall `(const wchar_t* Section, const wchar_t* Key, int* Value, const wchar_t*
  Filename)`, returns found (pushes read at 0x10021EEE-0x10021F15). Our call: `("EgoAwards", "EGOAWARD_Toilet",
  &v, "Progress.ini")`. Reads only.
- The cache is `FConfigCacheIni` in DukeForever.exe; `FConfigCacheIni_Find` `DukeForever.exe!0x1090B840` (plate
  comment) looks the file up by lower-cased clean file name (so `Progress.ini` and the player-path form share one
  entry) and **touches the disk only when the file is not cached** (`GFileManager` vt+0x20 FileSize, then load).
  The game reads Progress.ini at boot (chapter select, preorder flags), so it is cached; on a brand-new profile
  with no Progress.ini every Get would hit the disk: read only after the game has (any `GetBool` returning found).
- Cost (estimated from the code; the tab logs its refresh time as `progress: ... ms` lines in nukepanel.log): per Get a filename copy, three `wcsicmp`, `GetCleanFilename` +
  `ToLower` (two small string allocations), two map lookups. Microseconds; 35 keys well under 1 ms. No guarded
  engine-memory reads are involved. **Game thread only** (the cache has no lock; the panel draws on the game
  thread: engine-map.md section 5). Refresh on tab open and then at most once per second.
- **Path B:** `DukePlayer.HasDiscoveredEgoAward(id)` by ProcessEvent (parms: int AwardID, optional bool
  bForAchievement = 0, return bool): the same Get plus `GetEnum`; needs a DukePlayer, so not at the main menu.

### Ego cap now and its maximum

- `Pawn.Ego` (current), `Pawn.EgoCap` (current cap), `DukePlayer.MaxEgoCap` (all floats, by reflection).
- `DukePlayer.PreGameInit` (single player): `Ego = default.Ego; EgoCap = GetDefaultEgoCap(); MaxEgoCap =
  GetMaxEgoCap()`, then `GiveEgoCapAward(EgoCapAwards[i])` for every discovered award. Defaults: `DukePlayer`
  `EgoCap` = 40, `MaxEgoCap` = 100, `PreorderBonusEgo` = 5 (added to both when the preorder Ego code is in
  Progress.ini: big-package.md). 40 + 60 = 100: all 35 awards bring the cap to the maximum.
- `TriggerPlayer.bIncreaseEgoCap` adds `EgoCapIncrease` (`GiveEgoCapAward`, no Progress.ini write) for the level
  only: Map09 has three (+1/+2/+3, group `03_PornoRoom`), Map14 one. So `EgoCap` can exceed the sum of awards
  during a level; the permanent part is `GetDefaultEgoCap() + sum(EgoCapAwards[found])`.

## 2. Award givers in the level (ego-boost ESP)

Every script call that grants an award (all packages decoded; `GivePermanentEgoCapAward` callers):

| class (package) | award | trigger in script |
|---|---|---|
| `dnControl_Bathroom_UrinationReceptacle` (dnGame; `dnControl_Bathroom_Toilet`, `_Urinal`) | 0 | `PressPee.BeginState` |
| `VideoPoker` (dnGame; driven by `dnControl_CoinOp_Poker`, dnDecorations) | 1 | `UncoverCallback`, state 4 (after the draw) when `Payout() * CurrentBet > 0` |
| `dnControl_CoinOp_SlotMachine` (dnDecorations) | 2 | `Win` |
| `dnControl_WorkoutBench` | 3, 4 | `FullyLifted` |
| `dnControl_AirHockeyPlayer` (dnGame; spawned for `AirHockeyTable`) | 5 | `GameOverWon.BeginState` |
| `dnControl_CameraViewer_PayBinoculars` | 6 | `PlayerEnter` |
| `dnControl_Copier` | 7 | `MakeCopy` |
| `dnControl_GloryHole` | 8 | `idle.EndState` |
| `dnControl_Pinball` | 9 | `AwardPoints` |
| `Settings_Workout_HandWeights` (`_HandWeight_100lbs`) | 10 | `HandleDecoAnimEvent` |
| `Details_Generic_Calendar` | 11 | `Used` |
| `Details_Generic_PaperAirplane` (`_A`, `_B`) | 12 | `UnGrabbed` |
| `Details_Generic_Frisbee` (`Details_Generic_DuctTape`, `Settings_FoodService_Plate`) | 13 | `UnGrabbed` |
| `Details_Generic_PornMag_A` / `_B` / `_C` | 14 / 15 / 16 | `GrabComplete` |
| `CigarAttachment` (dnGame; the cigar in Duke's mouth; from a `CigarPickup`, an `AutoUseWeaponPickup` for `Cigar`) | 17 | `MakePawnPuff` |
| `Settings_Workout_PunchKick_EgoBooster` (`PunchingBag_Base` defaults id 18, `SpeedBag_Bag` id 19, both `bGivePermanentEgoAward`) | instance `PermanentEgoAwardID` | `TakeDamage` (melee) |
| `Biology_Generic_WallBoobs` | 20 | `Slapped` |
| `dnBreakableGlass_Mirror_Usable` (dnGame) | 21 | `Used` |
| `Electronics_Generic_Microwave` | 22 | `NukingState.ExecuteFinishedNuking` |
| `Furniture_Generic_PoolTable_A` | 23 | `TriggerFunc_WinEgoCapIncrease` (map event on a win) |
| `Settings_Workout_Basketball_Hoop` | 24 | `Score` |
| `Electronics_Generic_Computer_Monitor` (`_Black_A`, `_Black_A_Wall`) | 25 | `SwitchScreens` while `bCanSwitch`, reached only from a keyboard (below) |
| `TriggerPlayer` (dnGame) | each of `EgoCapAwards[]` | `Trigger` (map events) |

- **An actor's award id:** fixed by class (table), the instance's `PermanentEgoAwardID` for the boosters, the
  instance's `EgoCapAwards` (array<byte>) for TriggerPlayer. **Already used:** the global flag of that id (section
  1); the instance keeps no "used" state.
- **Monitors (in-game test: Map02's five could not be used):** using a
  monitor never grants 25. `Monitor.Used` only toggles power (`MonitorState` 0 -> `TriggerFunc_PowerOff`, 1 ->
  `TriggerFunc_PowerOn`, 2 nothing; raw bytecode, ubc.py stops at its switch). The only caller of
  `Monitor.SwitchScreens` is `Computer_CPU.SwitchScreens` (each of its `Monitors`), whose only caller is
  `Computer_Keyboard.KeyPress` (the keyboard's Used animation callback `CallbackScript 'KeyPress'`) through
  `Keyboard.CPU`. `CPU.FindComponents` (PostBeginPlay) gives every monitor and keyboard of its `SystemName`
  (None matches None) that CPU. `ScreenTextures` does not gate the award (an empty array only blanks the screen).
  **Marker rule:** `bCanSwitch`, `Monitor.CPU` set and that CPU's `Keyboards` not empty, and not `bHidden`
  (egoesp.h `eg_monitor_wired`). Per map (instances): Map02 5 wall monitors and Map04C 6 monitors + 6 keyboards
  have **no CPU** in the map: never usable. Map12B: two systems, all `bHidden` (Tag `Map10SecOffDetails`). Map19:
  two complete systems (`undGenPuter01`, `02`). Map20: `puzOffice02` complete (its keyboard starts `bUsable`
  false), `puzOffice01`'s monitor has no CPU.
- **Only while awards can be earned (in-game test):** the Map00 urinals before the Cycloid fight gave
  nothing, because `GivePermanentEgoCapAward` grants only while `DukePlayer.EgoCapAwardsEnabled` is set (Map00's
  `TriggerPlayer_Ego_Enable`/`_Disable`; autowin.md section 2). The ESP hides every marker while it is clear
  (`egoesp: awards open|closed` on each edge).
- **Cigar:** `Details_Generic_CigarBox` gives no cigar (the class declares no function; the ESP marked two in
  Map09 and Ego Items 14/14 was reached there without a cigar; chapter 9's list has no 17). `CigarPickup` is the
  only source of 17 (Map11, Map20).
- **Also closed while `PlayerPawn.bHasCheatedThisLevel` is set:** the Extras menu's god mode, infinite ammo and InstaGib make `NoteDiscoveredEgoAward` return early; the closed line names which reason.
- **TriggerPlayers are not ESP targets, but what starts them can be:** the TriggerPlayer is
  a hidden actor at the end of a map event chain. For 26, 29, 30 and 31 the chain starts at an actor the player
  uses, and the ESP marks that actor (section 7). The bosses (27, 28, 32, 33, 34) have nothing to use.
- **Instances per map** (map packages, export classes through the class chain; instances only, nothing spawned
  at runtime is counted):

| map | givers (award id x count) |
|---|---|
| Map00 | 0 x11, 23 x1, 21 x1 |
| Map01 | 22 x2, 21 x2, 0 x4; TP 26 |
| Map02 | 21 x1, 25 x5 (no CPU: never usable), 10 x20, 3/4 x1, 24 x1, 9 x1, punching bag x1 |
| Map04B / 04C | 2 x11 / 25 x6 (no CPU: never usable) |
| Map05 | TP 27 |
| Map06 | 7 x1 |
| Map07 / 07B | 20 x2 / 20 x7 |
| Map08 | 10 x4, 3/4 x1, 20 x1, punching bag x1; TP 28 |
| Map09 | 9, 8, 2 x4, 22 x2, 7, 23 x2, 0 x4, 21 x2, 24, 1 (poker) x3, air hockey table (2 cigar boxes: no award); TP 29, 30, 31; 3 cap triggers |
| Map10 | 13 x3 |
| Map11 / 11B | 0 x3, 21, 14, `CigarPickup` x1 (3 cigar boxes: no award) / 24 x2, 21, 22 |
| Map12B | 6 x2, 13, 25 x3 (wired, hidden) |
| Map13 | 22, 16, 12 |
| Map15 | 12; TP 32 |
| Map16 / 16B | 12 x3, 11 / 13 x2 |
| Map17 / 17B | 15 / 11 |
| Map18 | 0 x3, 21; TP 33 |
| Map19 | 20 x2, 25 x2, 16, 12 x2 |
| Map20 / 20B | 25 x2 (one wired), `CigarPickup` x1 / 14 |
| Map21 / 21B | TP 34 / 15, 12 |
| Map_mydigs | 15, 2 x2, 13 x2, 14, 24 x2, 10 x10, 3/4, 9, 23, punching bag, poker, air hockey table, `CigarPickup` x2 |

  A typical level holds 0 to 10 givers; the largest are Map02 (~31, 20 of them hand weights), Map09 (~25) and
  Map00 (13). Maps 03, 04, 06B, 11C, 12, 14, 14B, 19B, 22, 23 have none.

## 3. Other per-level stats

- **Chapter completion (per difficulty).** `ADukePlayer::execHasCompletionForLevel(Difficulty, Level)`
  `dnGame.dll!0x1003E630` (plate comment): `[Progress] GameCompletion_<diff>_<part>` read with `GConfig->vt[2]`
  (+8, GetInt) from the file `ADukePlayer::GetProgressFilePath` (vt +0x3EC; Engine's `g_BuildProgressIniName`
  `0x10108740` = player subdir + `\Progress.ini`, the same cache entry). The chapter counts as complete when, for
  each part p whose mask (`dnGame.dll!0x10097734`: 0x7FFFFF, 0x3B58D0, 0x810) has bit `Level`, the stored word
  has it. Part masks match the map files exactly: B maps for chapters 4, 6, 7, 11, 12, 14, 16, 17, 19, 20, 21; C
  maps for 4 and 11. Levels 0..22 only (Press Conference, 23, has none). `AddCompletionForLevel`
  `0x1003E8B0` is the writer. `MapNameToChapterPart` `0x1003E200` (renamed): `mapNN` -> (NN, 0), `mapNNx` -> (NN,
  x - 'a'). Difficulty 0..3 = the four icons `ChapterComplete_Easy/_Normal/_Hard/_Expert` (index = difficulty).
  **Read:** 4 x 3 = 12 GetInt keys give every chapter on every difficulty (apply the masks), or
  `HasCompletionForLevel(d, i)` by ProcessEvent (92 calls; needs the DukePlayer).
- **Current difficulty:** `GameInfo.Difficulty` (byte, `Level.Game`). Current chapter/part: `LevelInfo.MapFileName`
  through the rule above.
  **Checkpoint loads:** a checkpoint's level package is `cpNN`, not `MapNN`, so the package name finds no
  chapter (nukepanel.log: "map in no chapter 600"). The engine's own value is `LevelInfo.MapFileName`: the game passes
  it to `SPSetEgoAwardsForLevel` (the LCD, dnGame.u) and compares it with `'Map23'` (ubc.py decode of dnGame.u), so it
  must name the map on a checkpoint load too. The panel uses the package name when it is a chapter map, else
  `MapFileName` (`pg_pick_map_name`), and logs `progress: level package '..', LevelInfo.MapFileName '..', chapter N`
  per level. Measured in game: `MapFileName` reads `MapNN` after a checkpoint load (and `DLC03_Map00` for a DLC
  checkpoint, docs/re/dlc.md).
- **Secrets: none in single player.** No `SecretArea` export in any SP map (all `Maps\Map*.dnf` scanned); the class
  feeds the MP stat `PlayerProgression.Stat_SecretArea`. The LCD's level "N/M" is `SPSetEgoAwardsForLevel`
  (found/total ego awards for the map), not secrets.
- **Achievement collectibles in Progress.ini `[Achievements]`:** `HelmetA/B/C` (map events
  `TriggerFunc_ACH_DiscoveredHelmetA/B/C`; names in Map05, Map07, Map21), `PhoneMessage1..12`
  (`UpdatePhoneMessagesAchievement`; `ACH_PhoneMessage_NN` names in Map00, 06, 09 x3, 11, 12B, 16, 18, 20 x2, 20B),
  and counters per `EAchievement` name (`UpdateAchievementCounter`, e.g. kills by type, food eaten). Beer, steroids
  and holoduke are inventory pickups with no per-level tally (only achievement counters such as `ACH_DrankAllBeer`).
  `[ChapterSelect] Available`, `[Difficulty] AcknowledgedDifficulty`. All readable with the same GetBool/GetInt.

## 4. Chapter list

The 24 chapters in order are the table in section 1 (URL = `mapNN`, title from `dnwindow.int`
`[UDukeSceneChapterSelect] ChapterNames`, read only; read the localised `ChapterNames` from the class default
object at runtime rather than hardcoding the English). A chapter's maps: `mapNN` plus `mapNNb`/`mapNNc` where they
exist (`Maps\`: 04B, 04C, 06B, 07B, 11B, 11C, 12B, 14B, 16B, 17B, 19B, 20B, 21B).

The DLC chapters (`UDukeSceneDLCChapterSelect`, `HasCompletionForDLCLevel` with `[DLCProgress]`) are in
docs/re/dlc.md.

## 5. Save vs profile

Read from the script packages and Engine.dll; a test log agrees. Of what the Progress tab shows,
only the ego awards and what derives from them are per playthrough. **New Game clears `[EgoAwards]`**
(`DukePlayer.ClearEgoAwards`), so awards, per-chapter ego N/M and the permanent ego cap start over; chapter
completion ticks, helmets and phones live in profile sections nothing clears, and the game's own Chapter Select
keeps showing the completion icons too. The game keeps no per-playthrough helmets, phones or chapter ticks.

### Measured (a test log)

`progress: read` before the New Game: `awards 17/35 ... cap 71/100`; after it, in Map00: `awards
0/35, chapter ticks 32, helmets 1/3, phones 5/12 ... cap 40/100`; `awards 1/35 ... cap 41` after one
award.

### The New Game path

- `UDukeSceneSinglePlayer.NewGamePushed` opens the difficulty scene with `TravelURL =
  'map00?NewGame?Game=dnGame.dnSinglePlayer'` and `bResetEgo = true` (also `UDukeSceneMainMenuPAX`).
  `UDukeSceneSinglePlayerDifficulty.Tick` then calls `DukePlayer.ClearEgoAwards()` (`SetConfigBool('EgoAwards',
  EGOAWARD_<i>, false, 'Progress.ini')` for all 35). Backing out (`NavigateBack`/`OnNavForward`) drops the flag.
  Chapter Select does not set it: starting a chapter keeps the awards.
- `dnSinglePlayer.Login` with option `NewGame` calls `DukePlayer.StartedNewGame()`: only hint flags (`set
  DukePlayer bDisplayed...Hint false`), nothing in Progress.ini.
- Reload rollback: `APlayerPawn::execClientTravel` `Engine.dll!0x10263380` (plate comment) removes every key of
  `[EgoAwardsSession]` from `[EgoAwards]` and of `[ACH_EgoAwardsSession]` from `[ACH_EgoAwards]` before an SP
  travel; the checkpoint save `dnSaveLoad_CheckpointSave_ClearsEgoSession` `0x1011938C` (renamed) empties the
  Session sections. So `[EgoAwards]` always matches the last checkpoint of the current playthrough.
- `[ACH_EgoAwards]` is the profile copy (written beside `[EgoAwards]` by `NoteDiscoveredEgoAward`, never cleared by
  New Game): `HasDiscoveredEgoAward(id, bForAchievement=true)` reads it; it drives achievement 29 (all awards).

### Per value

| tab value | source | kept per | after New Game |
|---|---|---|---|
| per-chapter ego N/M | `[EgoAwards]` over the chapter table | playthrough | 0/M |
| awards N/35 | `[EgoAwards]` | playthrough | 0 |
| ego cap (permanent part) | `GetDefaultEgoCap()` + `EgoCapAwards[]` of `[EgoAwards]` (+5 preorder) | playthrough | 40 |
| ego cap now | `Pawn.EgoCap` (adds level-only TriggerPlayer increases) | level | |
| difficulty ticks | `[Progress] GameCompletion_<diff>_<part>` | profile | kept |
| helmets N/3 | `[Achievements] HelmetA/B/C` | profile | kept |
| phones N/12 | `[Achievements] PhoneMessage1..12` | profile | kept |
| ever-found awards (not shown now) | `[ACH_EgoAwards]` | profile | kept |

### What the game's own screens show after a New Game

- **Chapter Select** (`UDukeSceneChapterSelect.CheckEgoAwardAndSetDifficulty`): "Ego Items Discovered: N/M" counts
  `Duke.HasDiscoveredEgoAward(id)` with `bForAchievement` false, i.e. `[EgoAwards]`: **0/M** after a New Game. The
  four difficulty icons use `HasCompletionForLevel(d, i)`: **kept** (profile). `OnNavForward` also reads
  `HasCompletionForLevel(0, i)` per chapter (which chapters it offers: a chapter is listed when it is complete on difficulty 0,
  or when it is `map00` or follows a complete one).
- **Main menu / single player:** the Chapter Select button shows from `[ChapterSelect] Available` (profile,
  `dnUnlockMap`/`dnUnlockCurrentMap`); `UDukeRootWindow.bSaveExists` decides the New Game confirmation and Continue.
- **Extras** (`UDukeSceneExtras.ShouldShowMenuItem`, `UDukeSceneExtraSettings.OnNavForward`): unlocked by
  `DukePlayer.HasFullGameCompletion(d)` over the GameCompletion keys: profile, kept.
- Where a per-playthrough value can be read: `[EgoAwards]` and the live `Pawn.EgoCap`/`MaxEgoCap` (as now), the
  current chapter from the level, and the checkpoint's map (the Continue target). Helmets, phones and chapters
  reached have no per-playthrough record in the game.

### How the tab labels scope

The tab labels values by scope: a "This playthrough" block (awards, per-chapter ego, ego cap) and a "Profile" block
(difficulty ticks, helmets, phones), as the game's Chapter Select mixes them. A per-chapter "this run N/M, ever
N/M" column reads `[EgoAwards]` and `[ACH_EgoAwards]`. The game keeps no per-playthrough record of helmets, phones
or chapters reached, so a panel-kept record would start empty and could not be rebuilt for a playthrough already
under way.

## 6. Helmets and phone messages

Built: Helmets and Phone messages ESP rows, HELM/PHONE columns per chapter. Both are **map events that
`DukePlayer` catches by name**, and nothing about the actors changes once one counts (read from the script
packages, the map packages and Engine.dll).
`DukePlayer.InitializeAchievements` (dnGame.u, called from `PostBeginPlay`, skipped in MP) registers
`RegisterSpecialEvent(<func suffix>, <event>)` for `ACH_DiscoveredHelmetA/B/C` and `ACH_PhoneMessage_01..12`, so a
`GlobalTrigger` of that event name runs `DukePlayer.TriggerFunc_ACH_<suffix>`. **Helmets** (3): a placed `Trigger`
fires a `Dispatcher` whose `OutEvents` holds `ACH_DiscoveredHelmetX`; the helmet itself is a plain
`StaticMeshActor` (one is a `Bandit_Head`). **Phones** (12): `Electronics_Generic_DeskPhone` instances whose
`StartPlayingMessagesEvent` is `ACH_PhoneMessage_NN`; the event fires the moment the phone is used (playback
starts), not when the message ends. Found state: `[Achievements] HelmetA/B/C` and `PhoneMessage1..12` in
Progress.ini (profile, section 5), already read by progress.cpp. No collectible is destroyed, disabled or spawned
when found, none is breakable, and none spawns mid-level; one phone (7, Map12B) starts `bHidden` and is unhidden
by a proximity trigger.

### Script (dnGame.u, decoded with tools/ubc.py)

- `TriggerFunc_ACH_DiscoveredHelmetA` = `SetConfigBool('Achievements', 'HelmetA', true, 'Progress.ini')` then
  `CheckHelmetsDiscovered()` (all three -> `AwardAchievement(36)`); B and C the same with `HelmetB`/`HelmetC`.
- `TriggerFunc_ACH_PhoneMessagesNN` = `UpdatePhoneMessagesAchievement(NN)`: `SetConfigBool('Achievements',
  'PhoneMessage' $ NN, true, 'Progress.ini')` (int-to-string: `PhoneMessage7`, no leading zero, as progress.cpp
  builds it), then `AwardAchievement(37)` once all 12 are set.
- Event to function: `RegisterSpecialEvent('ACH_PhoneMessages01', 'ACH_PhoneMessage_01')` ... `_12`, and
  `('ACH_DiscoveredHelmetA', 'ACH_DiscoveredHelmetA')`, B, C. No cheat check and no `EgoCapAwardsEnabled` gate
  (unlike the ego awards): the flag is written whenever the event fires, and again on every repeat (harmless).
- `Electronics_Generic_DeskPhone` (dnDecorations.u, parent `Electronics_Generic`): `Used` -> if `Messages.Num() > 0`
  and not playing, `StartPlayingMessages` = `GlobalTrigger(StartPlayingMessagesEvent)`, then beep and messages;
  else `NoNewMessages`. `bUsable` is false for 1 s after a use (`Reenable`). `Destroyed` makes it unusable, but
  every message phone has `HealthPrefab 0` or `bNoDamage` in its map (class default `HealthPrefab 1`), so none can
  be broken. `Electronics_Generic_PayPhone_Black` is a separate class with no messages (Map09's five pay phones are
  decoration).
- `Engine.Trigger.TriggerTarget` sets `bTriggered = true` after a once-only fire (the level's own "already fired";
  the profile flag is the truth for the ESP). `TriggerType` enum: 0 `TT_PlayerProximity` (the class default; no
  `TriggerType` in the helmet A/B instances), 9 `TT_PlayerProximityAndLook`. `LookUseTags`/`LookUseEvents` are
  `name[16]` static arrays.

### Helmets (map packages, instance properties)

| id | key | map, ch | how it counts | trigger | dispatcher | the helmet |
|---|---|---|---|---|---|---|
| A | `HelmetA` | Map05, 5 Vegas in Ruin | walk within 256 (h 80) | `Trigger___33` (-4, 4744, 100), once | `Dispatcher___58` Tag `PowerArmor_Dispatcher`, once: `ACH_DiscoveredHelmetA` (0 s), `PowerArmor_Music`, `EDF04_PowerArmorScript1` | `StaticMeshActor___604` `PowerArmor_Helmet` (18, 4730, 95), group `10_Valet` |
| B | `HelmetB` | Map07, 7 The Hive | walk within 112 | `Trigger___33` (523, 1344, -8145), once, `bGoryActor` | `Dispatcher___30` Tag `SpaceMarine_Dispatcher`: `ACH_DiscoveredHelmetB`, `SpaceMarine_DukeLine` | `StaticMeshActor___290` `SpaceMarineHelmet` (515, 1343, -8179) beside `dnDecorativeCorpse___3` (Tag `SpaceMarine`) |
| C | `HelmetC` | Map21, 21 Blowin' the Dam | within 96 (h 64) **and look at** `LookInfoActor___0` (5497, 6897, -805) | `Trigger___203` (5540, 6795, -784), type 9, once, `LookUseTags[0]` `DaddySecretDialogLookInfo`, `LookUseEvents[0]` `DaddySecretDialog` | `Dispatcher___118` Tag `DaddySecretDialog`, once: `NULL` (0 s), `ACH_DiscoveredHelmetC` (+2.75 s); `TriggerDialog___0` (`DUKE_BorderHelmet_01`) shares the tag | `StaticMeshActor___18` `Bandit_Head` (5495, 6902, -816), group `02g_DamExterior_DaddySecret` |

- The three event names occur in no other map (name tables of every `Maps\Map*.dnf` scanned).
- Helmet B's trigger, mesh and dialog are `bGoryActor`, and its dispatcher sits in group `LowGorePickups`. The
  low-violence Steam depots (`InitSteamPreGame` `Engine.dll!0x10348150` logs "Using LowGore Depots") are a
  separate download; what `bGoryActor` does natively was not read. Not relevant to this English build.

### Phone messages (map packages)

| id | key | map, ch | instance (`Electronics_Generic_DeskPhone`) | location | group | message |
|---|---|---|---|---|---|---|
| 1 | `PhoneMessage1` | Map00, 0 Duke Lives | `___2` | (97, 3732, -133437) | `97_Penthouse` | `PhoneMSG_GiddyFloraFrancis` |
| 2 | `PhoneMessage2` | Map06, 6 The Duke Dome | `___0` | (771, 5268, -670) | -- | `PhoneMSG_PigNChick` |
| 3 | `PhoneMessage3` | Map09, 9 Titty City | `___2` | (406, 944, -77) | -- | `PhoneMSG_HeavyBreathing` |
| 4 | `PhoneMessage4` | Map09, 9 | `___3` | (63, 587, -69) | `03_Office` | `PhoneMSG_Vaginitis` |
| 5 | `PhoneMessage5` | Map09, 9 | `___0` | (1378, -156, 55) | `12_SecurityRoom` | `PhoneMSG_TonightYou` |
| 6 | `PhoneMessage6` | Map11, 11 The Duke Burger | `___0` | (434, 102, 15427) | `NEW_Intro_Hallway` | `PhoneMSG_RichSnob` |
| 7 | `PhoneMessage7` | Map12B, 12 The Mighty Foot | `___0`, **`bHidden`** | (6474, 1168, 292) | `27_Area01_GasStop_Combat` | `PhoneMSG_BridgeWorkPappy` |
| 8 | `PhoneMessage8` | Map16, 16 The Shrunk Machine | `___1` | (1257, 610, 622) | `03a_EntranceHubRearOffice` | `PhoneMSG_WaterNerd` |
| 9 | `PhoneMessage9` | Map18, 18 Generator Room | `___3` | (4, -2162, 341) | `P2_11_DamDetails` | `PhoneMSG_CodeRed`, `PhoneMSG_DorkDies` |
| 10 | `PhoneMessage10` | Map20, 20 The Clarifier | `___1` | (7779, -5041, -4667) | `005_CranePuzzleRoom` | `PhoneMSG_GameKid` |
| 11 | `PhoneMessage11` | Map20, 20 | `___5` | (6802, -6199, -4667) | `005_CranePuzzleRoom` | `PhoneMSG_DisgruntledDamWorker` |
| 12 | `PhoneMessage12` | Map20B, 20 | `___0` | (5261, -2109, -4450) | `015b_ClarifierBackStairs` | `PhoneMSG_4MrBrickShoes` |

- **Id = the two digits of `StartPlayingMessagesEvent`** (`ACH_PhoneMessage_NN`); each event name is on exactly one
  actor (raw name-index scan of every export).
- **Decoy desk phones** (no `StartPlayingMessagesEvent`): Map09 `___1` (Tag `ChampagnePhone`), Map12B `___1`
  (hidden), Map18 `___0`, Map20 `___2` (`Messages = Phone_NoMSG`, plays the "no messages" line). Not markers.
- **Phone 7 appears mid-level:** it and the whole office it sits in carry Tag `Map10SecOffDetails`, all `bHidden`;
  `Trigger___22` -> `Dispatcher___66` (`GasStopShowHideMiscDispatcher`, once) -> `ShowMap10SecOffDetails` ->
  `TriggerToggleHidden___21` (Event `Map10SecOffDetails`) shows them. Same office as section 2's hidden Map12B
  monitors.

### Phone audit (every `Maps\*.dnf`)

The panel's 12 is the game's 12, and Map09 has three counted phones. The wiki's
fourth Map09 voicemail and its total of 14 have no actor behind them in the build-244 data.

- **Total.** `UpdatePhoneMessagesAchievement(N)` sets `PhoneMessage<N>` then loops `i = 1..12` and calls
  `AwardAchievement(37)` only if all twelve are set (re-decoded: the loop bound is the literal 12). Twelve
  `TriggerFunc_ACH_PhoneMessages01..12` and twelve `RegisterSpecialEvent` pairs; no 13th or 14th key in any package.
- **Sources.** Every export in every `Map*.dnf` whose class has "phone", "answer", "message" or "machine" in its
  name was listed, plus every name-table entry `PhoneMSG_*`, `Phone_*` and `ACH_PhoneMessage*`. The only class that
  plays a voicemail is `Electronics_Generic_DeskPhone` (no subclass exists in any `.u`; its `Used` is the only
  reader of `Messages`/`StartPlayingMessagesEvent`). No answering-machine class, no `TriggerFunc_ACH_PhoneMessage`
  outside DukePlayer, no dispatcher chain: the "answering machine that says tonight... you" is the Map09 security
  room desk phone (id 5, `PhoneMSG_TonightYou`).

| map | desk phones | counted (ids) | decoys | message sounds |
|---|---|---|---|---|
| Map00 | 1 | 1 (1) | 0 | 1 |
| Map06 | 1 | 1 (2) | 0 | 1 |
| Map09 | 4 (+5 `PayPhone_Black`, no messages) | 3 (3, 4, 5) | 1: `___1`, Tag `ChampagnePhone`, (136, -567, -77), group `06_Hall` | 3 |
| Map11 | 1 | 1 (6) | 0 | 1 |
| Map12B | 2 | 1 (7, hidden until its office shows) | 1 (hidden) | 1 |
| Map16 | 1 | 1 (8) | 0 | 1 |
| Map18 | 2 | 1 (9) | 1 | 2 on one phone (`CodeRed`, `DorkDies`) |
| Map20 | 3 | 2 (10, 11) | 1 (`Phone_NoMSG`) | 2 |
| Map20B | 1 | 1 (12) | 0 | 1 |
| total | 16 | 12 | 4 | 13 |

- **Map09 against the wiki.** Our three: `___2` (406, 944, -77) HeavyBreathing; `___3` (63, 587, -69) in group
  `03_Office` (the back office behind the keypad) Vaginitis; `___0` (1378, -156, 55) in `12_SecurityRoom` TonightYou.
  The fourth desk phone `___1` carries no `Messages` and no `StartPlayingMessagesEvent` (131 bytes against 136-168):
  using it plays `NoNewMessages`. The only references to its Tag `ChampagnePhone` are a `ScriptMethod_TurnToActor`,
  a `ScriptMethod_SetHeadTarget` and a `PatchActor_Cylinder` of the champagne-dude gag, which looks at it. The pay
  phones have no message path at all. So the wiki's "reception desk near the end" is most likely `___1` (a desk
  phone that holds no voicemail) or a miscount; either way it cannot award anything.
- **Wiki 14 against 12.** The data give 12 achievement flags, 13 message sounds (Map18's phone holds two) and 16
  desk phones. 14 matches none; a guess is that the wiki counts the Map09 decoy plus Map18's second message. The
  game's own check is the 12 flags, so Call Waiting needs 12.
- **Enumeration.** The "Phone messages" table above and its decoy line enumerate by class,
  `Electronics_Generic_DeskPhone`, not by event name, and list the four decoys by name; the counts here come from
  export classes and name tables.
- **Guard.** `tb_0442_pg_phone_ids_partition` (each id in one chapter, Map09 = 3). The ESP, the PHONE column and
  `phones N/12` follow the 12 event names.

### Per chapter

| ch | title | helmets | phones (ids) |
|---|---|---|---|
| 0 | Duke Lives | -- | 1 (1) |
| 5 | Vegas in Ruin | 1 (A) | -- |
| 6 | The Duke Dome | -- | 1 (2) |
| 7 | The Hive | 1 (B) | -- |
| 9 | Duke Nukem's Titty City | -- | 3 (3, 4, 5) |
| 11 | The Duke Burger | -- | 1 (6) |
| 12 | The Mighty Foot | -- | 1 (7, in Map12B) |
| 16 | The Shrunk Machine | -- | 1 (8) |
| 18 | Generator Room | -- | 1 (9) |
| 20 | The Clarifier | -- | 3 (10, 11; 12 in Map20B) |
| 21 | Blowin' the Dam | 1 (C) | -- |

Every other chapter has none. Totals 3 and 12.

### ESP rule

- **Not found** = the id's `[Achievements]` key is false, read through GConfig exactly as progress.cpp does
  (`PgFlags.helmet[]`, `PgFlags.phone[]`, refreshed at most once a second on the game thread). Profile scope: a
  message heard in an earlier playthrough never shows again (section 5). Nothing on the actor records it; a found
  collectible stays in the level unchanged (the trigger's `bTriggered` only covers this visit).
- **Phones:** every actor that `IsA` `Electronics_Generic_DeskPhone` with `StartPlayingMessagesEvent` (name, by
  reflection) of the form `ACH_PhoneMessage_NN`, NN 1..12; marker at its live `Location` (a physics decoration);
  skip while `bHidden` (phone 7 until its office is shown), as egoesp does for hidden monitors. Read `bHidden`
  live, not only at the scan.
- **Helmets:** find the `Dispatcher` whose `OutEvents` (dynamic `name` array) holds `ACH_DiscoveredHelmetA/B/C`
  (the letter is the id); the marker goes on the `Trigger` whose `Event` or any `LookUseEvents[0..15]` equals that
  dispatcher's `Tag` (A, B: `Event`; C: `LookUseEvents[0]`). C needs a look, not just standing in the trigger:
  say so in its label. The dispatcher sits within ~45-110 units of each helmet: a fallback location if the trigger
  join fails. Compare names by name index, resolved once per level.
- Per-level scan as egoesp: one object-table pass at level change, a total beside the count in the log; nothing
  spawns, so no rescan beyond what egoesp already does.

### How the tab and ESP use them

- Mapping: phones by event-name suffix, helmets by the dispatcher's event letter (tables above); no hardcoded
  positions or instance names.
- Found read: the existing `PgFlags` from progress.cpp; no new config keys in Progress.ini.
- Progress tab per-chapter columns from the "Per chapter" table (helmets N/1, phones N/M), keyed by chapter index
  with the ids as fixed data in code (they are fixed in the packages).
- ESP rows "Helmets" and "Phone messages" (panel config `esp.helmets`, `esp.phones`), drawn as `ego_draw`:
  brackets round the model (else the clamped collision box; a Trigger has no model, so the clamped cylinder), name
  above, distance below. Labels "Helmet A", "Helmet C (look)", "Phone message 7". Colours are distinct from the
  Progress teal `C_PROGRESS` that Ego boosts use and from the rest of the ESP set (red, pink, purple, teal, blue,
  green, lavender, amber, bone): helmets a warm orange `0xFFE8873A` (`C_HELMET`), phones a pale cyan `0xFF9FE3F0`
  (`C_PHONE`).

## 7. Event awards and the chapter audit

Of the 35 awards, 30 have a usable source the ESP marks, and 5 (the boss kills) cannot be marked. The poker
machines (1), the cigar pickup (17) and the four event awards a player starts by using something (26, 29, 30, 31)
are all marked. Map09 has four such boosts (poker, the dance, Alien Abortion, the DVD); Map01 has its book
signing; Map11 and Map20 have cigar pickups. Read from the map packages and script packages.

### Decoder and survey pitfalls

- **PokerWin:** `tools/ubc.py` read the `switch` token (0x05) with a 1-byte size; DNF's carries 2 bytes. Every
  function with a switch stopped decoding there (177 of 15,472 before the fix; 21 still fail, none an award grant),
  including `VideoPoker.UncoverCallback`, which grants 1 after the switch. A partial decode looks like a complete
  one: a caller scan over it saw no call to `GivePermanentEgoCapAward` for id 1. Fixed in ubc.py.
- **Event awards:** a `TriggerPlayer` is only the last link of a map event chain, and the chain starts at an actor
  the player uses for 26, 29, 30 and 31. Treating every `TriggerPlayer` award as a hidden map event (the
  `eg_event_award` rule: "map events, never markers") drops them.
- **Cigar:** `CigarAttachment` is spawned for Duke, so a survey of the classes that call
  `GivePermanentEgoCapAward` finds no source. The source is the `Cigar` weapon's pickup (`CigarPickup`); the cigar
  box is not one.

### Poker

`dnControl_CoinOp_Poker.PostBeginPlay` finds a `VideoPoker` (`AllActors`) or spawns one: the machine is the display
and the controls of that shared game. `VideoPoker.UncoverCallback` (dnGame.u, decoded): `switch(VPState)`, case 4 (the
draw is uncovered): `VPState = 5`, `i = Payout() * CurrentBet`; if `i` is not 0, flash and sound the win and, when
`CurrentInstigator` is a DukePlayer, `GivePermanentEgoCapAward(1)`. Any paying hand earns PokerWin. Native code was
not read: the script grant settles it. Givers: Map09 `dnControl_CoinOp_Poker___1/2/3`, Map_mydigs one.

### The event chains

The marker goes on the actor in **bold**. The rule is class (super chain) + Tag, active only while a TriggerPlayer
in the level lists the award. Map_mydigs has the same Alien Abortion switch (`Switches_Generic_PushSwitch_B___1`, Tag
`SwitchAlienAbortReset01`) and no TriggerPlayer that grants it.

| id | award | map | chain (instances in that map package) |
|---|---|---|---|
| 26 | BookSigning | Map01 | `Trigger___48` (proximity) -> `Dispatcher_Book_Intro` -> the intro script `BookSigningIntroScript` -> `Dispatcher___181` (`Dispatcher_Book_Intro_End`): unhides **`dnControl_BookSigning___0`** (Tag `dnControl_BookSigning`, starts `bHidden`, unusable), makes it usable, and hides **`dnControl_BookSigning___2`** (Tag `dnControl_BookSigning_Mounted`, the book in the kid's hand). Using the table: its `StateEvents` `DetachUser` -> `Dispatcher_Book_Signed` (`Dispatcher___96`) -> dialogues `TriggerDialog___17`, `___113`, `___18`, `Dispatcher___2`, `___114`, `___15` (each `DonePlayingEvent`) -> `TriggerEgo_Book_Signed` = `TriggerPlayer___12` [26], which also sets `EgoCapAwardsEnabled`. Both book actors are marked; each only while not hidden. |
| 29 | AlienAbortion | Map09 | **`Switches_Generic_PushSwitch_B___1`** (Tag `SwitchAlienAbortReset01`, "Start Game", group `04_GameRoomAlienAbortion`): `SwitchUserStates[1].Event` `DisRound1` -> rounds 1-4 and the bonus (`RoundRobin___17`, `PeriodicTrigger___18..23`, `Variable`s) -> on a perfect game `Variable___52` (`PerfectGameVar`) -> `DisExpertScore` (`Dispatcher___164`) -> `AAWinEgoTriggerPlayer` = `TriggerPlayer___9` [29]. |
| 30 | DukePorno | Map09 | `Details_Generic_DVDCase_PootyParty___1` (Tag `DukesPorno`, `MovieInfo.StationEvent` `PronMasterDispatcher`) is grab-used into **`Electronics_Generic_DVDPlayer___1`** (Tag `PornoDVDPlayer`, `TVTags` `PornoTV`, group `03_Office_PornoParts`, behind keypad `dnControl_Panel_KeyPad___0`); the TV's channel change fires `GlobalTrigger(StationEvent)` (`Electronics_Generic_TV.UpdateChannel`) -> `Dispatcher___83` (`PronMasterDispatcher`) -> `StartPronBink`, a `ForwardTags` alias of `Dispatcher___81` -> `PornoEgoDispatcher` (`Dispatcher___41`, once) -> `PornoEgoAward` = `TriggerPlayer___10` [30]. |
| 31 | TipStripper | Map09 | **`LookInfoActor___11`** (Tag `ShakeItBabyLookInfo`, "Request a special dance") inside `Trigger___27` (`LookUseEvents` `DukeUseShakeItRelay`, enabled by the stripper's script `ScriptObj___64` through `EnableShakeItStuffDispatcher`) -> `RoundRobin___1` and the condition of `TriggerRelay___20` (`ConditionTag` `DukeUseShakeItRelay`); the script then fires `HasDukeAskedMeToShakeItRelay` = `TriggerRelay___20` -> `StripperSuperMoveRandom`, a `ForwardTags` alias of `Dispatcher___16` (once, 2 s) -> `ShakeItEgoAward` = `TriggerPlayer___8` [31]. This is the main-stage dance. |

`TriggerRelay.SConditionMod.ConditionStyle` (the stripper chain's relay uses style 2): `ModifyConditions` decodes as
style 0 enables, 1 disables, 2 toggles, 3 random, 4 restores `bEnabled_Orig`.

`ForwardTags` (Actor): `CreateForwardTags` registers each entry with `RegisterSpecialEvent('TriggerForward', Tag)`,
so an actor also answers to those tags. The bosses: `TriggerPlayer___7` (`Arena_Exit_IncreaseEgo`, Map05, 27),
`___4` (`TriggerPlayerBossEgo`, Map08, 28), `___6` (`TriggerEgo`, Map15, 32; Map18, 33), `___1`
(`TriggerPlayer_LeechBattle_EgoAward`, Map21, 34): fired by the boss's death; nothing to use.

### Per chapter

Status: **marked** (a giver class with instances there), **event** (a TriggerPlayer chain, marked on the used
actor), **unmarkable** (with the reason). No award is missing a giver.

| ch | award | status | giver or chain | maps |
|---|---|---|---|---|
| 0 | 21, 0, 23 | marked | mirror, urinals/toilets x11, pool table | Map00 |
| 1 | 21, 0, 22 | marked | mirror x2, toilets x4, microwave x2 | Map01 |
| 1 | 26 | event | book signing table / the kid's book | Map01 |
| 2 | 21, 10, 4, 9, 24 | marked | mirror, hand weights x20, bench, pinball, hoop | Map02 |
| 2 | 18 | marked | `Settings_Workout_PunchingBag_Pigcop` (class default id 18) | Map02 |
| 2 | 19 | marked | the speed bag `Settings_Workout_SpeedBag_Bag`, spawned at runtime (from the scripts) by `Settings_Workout_SpeedBag___1`'s class-default `MountOnSpawn` (`___0` overrides it with an empty list); the per-level scan sees spawned actors | Map02 |
| 4 | 2 | marked | slot machines x11 | Map04B |
| 5 | 27 | unmarkable | boss kill (Battlelord) | Map05 |
| 6 | 7 | marked | copier | Map06 |
| 7 | 20 | marked | wall boobs x2 / x7 | Map07, 07B |
| 8 | 10, 4, 18, 20 | marked | hand weights x4, bench, Pigcop bag, wall boobs | Map08 |
| 8 | 28 | unmarkable | boss kill (Alien Queen) | Map08 |
| 9 | 21, 22, 9, 2, 0, 7, 23, 24, 5, 8 | marked | mirrors x2, microwaves x2, pinball, slots x4, toilets x4, copier, pool x2, hoop, air hockey table, glory hole | Map09 |
| 9 | 1 | marked | `dnControl_CoinOp_Poker` x3 | Map09 |
| 9 | 29, 30, 31 | event | Alien Abortion switch, DVD player, the dance look-info | Map09 |
| 10 | 13 | marked | frisbees x3 | Map10 |
| 11 | 0, 21, 14, 24 | marked | toilets x3, mirrors, magazine A, hoops x2 | Map11, 11B |
| 11 | 17 | marked | `CigarPickup___0` only (the 3 cigar boxes give nothing) | Map11 |
| 12 | 25, 13, 6 | marked | monitors x3 (hidden until the office shows; read live), frisbee, binoculars x2 | Map12B |
| 13 | 16 | marked | magazine C | Map13 |
| 15 | 12 | marked | paper airplane | Map15 |
| 15 | 32 | unmarkable | boss kill (Battlelord 2) | Map15 |
| 16 | 12, 11 | marked | paper airplanes x3, calendar | Map16 |
| 17 | 15, 11 | marked | magazine B / calendar | Map17, 17B |
| 18 | 0, 21 | marked | toilets x3, mirror | Map18 |
| 18 | 33 | unmarkable | boss kill (Octaking) | Map18 |
| 19 | 20, 12, 16, 25 | marked | wall boobs x2, airplanes x2, magazine C, monitors x2 (two wired systems) | Map19 |
| 20 | 14 | marked | magazine A | Map20B |
| 20 | 17 | marked | `CigarPickup___0` | Map20 |
| 21 | 15, 12 | marked | magazine B, airplane | Map21B |
| 21 | 34 | unmarkable | boss kill (Leech) | Map21 |

Chapters 3, 14, 22 and 23 list no award. Chapter 9's list (14 awards) is the only one with poker and the three strip
club events; Map_mydigs (no chapter) has a poker machine and two cigar pickups, which the ESP also marks.

**Survey against the chapter table:** Map09's poker, Map20's 17 (the pickup; the cigar box is no source, Map09 has
no cigar) and Map02's 19 (the speed bag is spawned, not placed) are in the table but not in a survey of placed
givers. Givers outside
their chapter's list (marked anyway, since awards are global): Map02 25 x5 and Map04C 25 x6 (no CPU, never usable,
never marked), Map13 22 and 12, Map11B 22, Map16B 13 x2. `EG_CLASSES` now covers every `GivePermanentEgoCapAward`
caller in all script packages (scanned with the fixed decoder) except `CigarAttachment` and
`dnControl_AirHockeyPlayer` (spawned; their sources are marked) and `TriggerPlayer` (the event rules above).

### Log line

On each level build, one line per event rule the level touches: `egoesp: level serial N: event award 29 Alien
Abortion (Map09 Switches_Generic_PushSwitch_B Tag 'SwitchAlienAbortReset01'): 1 actor(s), marked while unfound`, or
`not resolved:` with the reason (Tag name not loaded, no such actor, no TriggerPlayer here lists the award). A level
with none: `egoesp: level serial N: no event-award markers here (TriggerPlayers list: 27)`.

## Open questions

- Map01: `TriggerEgo_Book_Signed` also sets `EgoCapAwardsEnabled` (modifier 1), so awards may be closed in Map01
  until the signing; the ESP hides every marker while closed (the `egoesp: awards` line says which). Not measured.

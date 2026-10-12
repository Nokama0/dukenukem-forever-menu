# Duke's Big Package unlock

Ownership is a Steam DLC check. When the
preorder DLC (app 57921 `DNF_Preorder_NA` or 57922 `DNF_Preorder_Other`) is installed, `RescanDLC` calls
`AChallengeInfo::UnlockPreorderBonuses(group)` for each group its `DLC_DlcInfo.ini` lists. That call unlocks the
group's multiplayer challenge items in memory and, for a group with `bUnlockDBP`, writes
`[PreorderBonus] BonusEgo=1` and `BigHead=1` into `Progress.ini` through the game's config cache, which is what the
single-player Big Head and Ego bonus read. **The panel's call:** each launch, on the game thread, once the main
menu is up, it calls the export `AChallengeInfo::UnlockPreorderBonuses` (`Engine.dll!0x1006E040`) with
`ECX = UClass::GetDefaultObject(&AChallengeInfo::PrivateStaticClass)` and the group **2 (`POG_Gamestop_USA`)**.
Verified in game: EXTRAS appeared on the main menu and the game persisted `progress.ini` itself. Every map load
resets the flags (section 6), so the panel re-applies them before each `DukePlayer.PreGameInit` (sections 6 and 8).
The console route is closed: `DebugUnlockPreorder` is gated off and does not set the bonus flags.

Method: Ghidra MCP on Engine.dll (decompile/disassembly, names saved in project `dnf`); the extracted packages
(`extracted-packages/*.u`, package version 156 / licensee 36) read with a small Python parser of the name and export
tables (UE1 layout; tagged default properties). Engine.dll and dnGame.dll export tables read with Python.

## 1. `DebugUnlockPreorder` (dnGame.u)

- Export 14927, class `Function`, outer **`DukeMultiPlayer`** (export 1, super `DukePlayer`: the multiplayer
  pawn). One parameter `POG` (export 8029, `ByteProperty`, the `EPreorderGroup`).
- FunctionFlags `0x20020203` = Final | Defined | **Exec** | Public | **0x20000000**. Bytecode (15 bytes):
  `12 20 A8 08 00 00 00 84 96 00 5D 7D 16 04 0B` = `class'ChallengeInfo'.static.UnlockPreorderGroup(POG)`:
  `0x12` class context on import -40 (`Engine.ChallengeInfo`), extended native `0x84 0x96` = native 1174
  (`UnlockPreorderGroup`, the iNative in Engine.u), local `POG`, end parms, return.
- So it calls `UnlockPreorderGroup` only (challenge items), **not** `UnlockPreorderBonuses` (no Big Head/Ego
  flags), and `ScriptConsoleExec` rejects flag 0x20000000 (`Engine.dll!0x10042997`, engine-map.md section 4): it
  cannot run from the console.

## 2. The natives (Engine.dll exports) and the data they touch

Script declarations (Engine.u, class `ChallengeInfo`): `UnlockPreorderGroup` iNative 1174,
`UnlockPreorderBonuses` 1173, `UnlockPreorderGroupIncludesDBP` 1172, `TryBonusCode` 1171; all flags `0x00222401`
(Final | Native | Static | Public | 0x200000). Every `exec*` thunk (`__fastcall`, ECX=this, EDX=FFrame&) steps one
byte parameter and calls the C++ method, which is `__thiscall` with one dword stack argument (`RET 4`).

- `ChallengeInfo` default object = `[0x1068B720]` (`g_dwChallengeInfoDefaultObject`) =
  `AChallengeInfo::PrivateStaticClass` (`0x1068B218`) + 0x508, the field `UClass::GetDefaultObject`
  (`0x1000D8D0`) returns. Every method below works on this object, not on `this`; it is read through
  `CastToChallengeInfo` (`0x1006CAE0`), which returns 0 for a null or wrong object and the callers dereference the
  result unchecked (a null default object would crash them).
- Default-object layout (Engine.u field chain anchored on the offsets the code reads): `ChallengesArray` +0x4A4
  (dnArray of 0x98-byte `ChallengeInfoStruct`, unlocked bit0 at +0x94), `NoDLCChallengeArraySize` +0x4B0, `bInit`
  +0x4CC, **`PreorderGroups` +0x4D0** (dnArray of 0x14-byte `PreorderGroupStruct` {byte `Group` +0; dnArray<int>
  `IDs` +4; bool `bUnlockDBP` bit0 +0x10}), **`DeveloperGroupIdx` +0x4DC**, `PlayerTitles` +0x4E0, `LevelingInfo`
  +0x4EC.
- `EPreorderGroup` (Engine.u export 764): **0 `POG_All`, 1 `POG_Developers`, 2 `POG_Gamestop_USA`,
  3 `POG_Stripper_Other`**. There is no DBP enum value: "includes DBP" is the per-group `bUnlockDBP` flag.
- `PreorderGroups` defaults (class defaults in Engine.u; the property is `config` 0x4000, no override in the
  ini or the installed DLC folders): `(Group=1, IDs=(-5,-6,-7,-8,-9), bUnlockDBP=False)`,
  `(Group=2, IDs=(-15), bUnlockDBP=True)`, `(Group=3, IDs=(-16), bUnlockDBP=True)`. `DeveloperGroupIdx` is
  `config` with no default tag, so 0 (the developer group).
- `UnlockPreorderGroup(g)` `0x1006D840`: logs "Unlocking preorders entries for: %d"; for each `PreorderGroups`
  entry whose `Group == g` (every entry when g == 0; stops after the first match otherwise) calls
  `UnlockPreorders(IDs)`; then always unlocks `PreorderGroups[DeveloperGroupIdx]` if in range.
- `UnlockPreorders(dnArray<int>)` `0x1006D390`: maps each challenge ID through a hash built by `InitStatic`
  (`0x1006DA50`) and sets bit0 at `ChallengesArray[i]+0x94`. In memory only; an ID not in the hash is skipped.
- `UnlockPreorderGroupIncludesDBP(g)` `0x1006CE20`: 1 when g == 0 or the group's entry has `bUnlockDBP`.
- `UnlockPreorderBonuses(g)` `0x1006E040`: `UnlockPreorderGroup(g)`; if `IncludesDBP(g)`:
  `GConfig->vt[0x2C] ("PreorderBonus", "BonusEgo", 1, "Progress.ini")` and the same for `"BigHead"`. `vt[0x2C]` is a
  SetInt (section 6): the value is stored as the text `1`.
- `AChallengeInfo::CheckPreorderAccess` (`0x10333420`) is an empty shared stub (the same address as
  `URenderDevice::BeginNonGPUPostProcessing`): nothing to call.
- Readers: dnGame.u `DukePlayer.HasEnteredPreorderBonusBigHeadCode` / `...EgoCode` call
  `GetConfigBool("PreorderBonus", "BigHead"|"BonusEgo", OutValue, "Progress.ini")` and return `OutValue != 0`
  (decoded with `tools/ubc.py`). The
  dnWindow.u menus call the Big Head one.

## 3. Where ownership is decided

- `SteamInit::PopuplateDLCAppIDS` `0x10343770`: 0xE241 (57921) `DNF_Preorder_NA`, 0xE242 (57922)
  `DNF_Preorder_Other`, 0xE243 `DLC01`, 0xE245 `DLC03`.
- `IDLCManager::RescanDLC` `0x10358DB0`, for each owned DLC folder: loads `DLC_DlcInfo.ini` into the `DlcInfo`
  defaults (and `DLC_ChallengeInfo.ini` into `ChallengeInfo`), `InitDLCChallenges`, `LoadPurchasedChallenges`
  (`System\UNLOCK.DAT` IDs), then for each byte of `DlcInfo.PreorderGroups` (+0x4B0, `config`) logs
  "Trying to unlock PreOrder group[ %d ]" and calls `UnlockPreorderBonuses`. The test install owns DLC01 and DLC03 only.
- Codes (console versions' path): `TryBonusCode` -> `TestPreorderUnlockCode` (`0x1006C7E0`, a
  `xxx-xxx-xxx` code) -> event `CodeUnlockedPreorderGroup` (script, static event) -> `UnlockPreorderBonuses`.

## 4. Persistence

- The game writes only through `GConfig` (`Progress.ini`: `[PreorderBonus] BonusEgo`, `BigHead`); the remote
  `progress.ini` is not plain text (obfuscated bytes). Challenge unlocks are memory only and are
  redone by `RescanDLC` on every map load (section 6). No `SaveConfig` call follows the unlock in the code read.
- Consequence: once applied, the game persists the Big Head/Ego flags itself (measured below), so
  "Off = stop re-applying" does not remove them.

## 5. The call

1. Wait until the menu exists (after the log's "DLC INIT DONE"); in practice the first game-thread frame after
   `GIsScriptable` (`Engine.dll!0x107702B0`) is 1 and the default object is non-null.
2. On the game thread (the `MainLoop` thread, which also runs the device Present: engine-map.md section 5):
   `cdo = UClass::GetDefaultObject(&AChallengeInfo::PrivateStaticClass)` (both by export name); check it is
   non-null and `IsA` ChallengeInfo (class name at `Class+0x20`), then call
   `AChallengeInfo::UnlockPreorderBonuses` (`?UnlockPreorderBonuses@AChallengeInfo@@QAEXW4EPreorderGroup@@@Z`)
   as `__thiscall`, ECX = cdo, one dword argument **2**, callee pops 4. Once per launch.
3. Group choice: 2 (`POG_Gamestop_USA`) is the first group with `bUnlockDBP`, which by its name and the DLC
   pairing is likely the NA preorder (Duke's Big Package); 3 is the other-region group; 0 (`POG_All`) unlocks
   every group's items (a superset). All three set the Big Head and Ego flags. Which group the real NA DLC lists
   is unknowable without the DLC's `DLC_DlcInfo.ini`; the flags do not depend on it.
4. Alternative with the same effect: `ProcessEvent(cdo, FindFunction(cdo, "UnlockPreorderBonuses"), &group, 0)`
   (parms block: one byte; engine-map.md section 3). More machinery for no gain.

## Measured on a test launch (build `6EBF51AD`)

- Our log: `bigpackage: scriptable at 47089453 ms`, then `cdo=108D0C00 groups=3 calling
  UnlockPreorderBonuses(2)` and `applied`; no freeze, no hitch beyond the startup one.
- The main menu gained **EXTRAS** (absent before the unlock): the Big Package unlock took.
- **The game persisted it:** the remote `progress.ini` (untouched by the earlier launch) changed at the minute of the call, with its size
  unchanged (4,193 bytes). So "Off" does
  not re-lock.

## 6. Every map load resets the flags

**Summary:** the menu-time call alone never gave the Ego Boost to the player. `UGameEngine::LoadMapInternal` calls
`IDLCManager::GetInstance()->RescanDLC()` on every map load, before it loads the level, and `RescanDLC` starts with
`ResetDLC`, which writes `BonusEgo=0` and `BigHead=0`. It then re-applies only the groups an owned DLC lists, which
for this install is none. So the menu-time call (section 5) lasts only until the next map load. `DukePlayer.PreGameInit`
reads the Ego flag during that same load, after the reset. The panel therefore calls the same
`UnlockPreorderBonuses(2)` again immediately before `DukePlayer.PreGameInit`, through the dnGame ProcessEvent slot,
on the thread that raises it. That is the moment, and the thread, at which a real owner's `RescanDLC` re-applies it.

Evidence (Ghidra MCP on Engine.dll and DukeForever.exe; script with tools/ubc.py):
- The reset. `LoadMapInternal` `0x1022EAA0` at `0x1022EE63`, skipped only when `GIsRequestingExit` (import slot
  `0x1041F210`) is set: `GetInstance()` (`0x10359910`; the singleton at `0x1097B034`, vtable `0x104E798C`), then
  `->vt[4]()` = `RescanDLC()` `0x10359510`. That calls `RescanDLC(path, dirs)` `0x10358DB0`, whose first act is
  `this->vt[0xC]()` = `0x103596F0` (clears an array) -> `IDLCManager::ResetDLC` `0x103586F0`. `ResetDLC` writes
  `GConfig->vt[0x2C] ("PreorderBonus", "BonusEgo", 0, "Progress.ini")` and the same for `BigHead`.
- The condition. `RescanDLC(path, dirs)`'s whole body, the reset included, runs only when the installed DLC folder
  list is non-empty (`if (Num(dirs) != 0)`). The test install has DLC01 and DLC03, so the reset runs on every load. With
  no DLC folder at all, the flags would survive, and the re-apply below is then an idempotent repeat.
- `GIsScriptable` (`0x107702B0`) stays 1 through a map load. LoadMap and LoadMapInternal never write it (its
  writers are StaticExit, DrawExtraStuff and Build), and ProcessEvent needs it (`0x10024CB5`) to run PreGameInit at
  all. So "scriptable" does not mean "the load is over". The load detector is Engine.dll's `g_LoadingOnThread`
  (`?g_LoadingOnThread@@3HA`, `0x107EBC24`), written only by `LoadMap` (`0x1022D9D4`, `0x1022E6CC`), which the
  engine itself reads to skip work while loading (`LoadMapInternal`, `FocusChanged`, `Repaint`).
- The write format. The cache's vtable is `DukeForever.exe!0x109134DC` (our log: `vtable 109134DC GetBool 10908210
  GetInt 109081B0`). `vt[0x2C]` = `0x10908400` formats the value with `"%i"` (`0x10913554`) and calls `vt[0x34]` =
  SetString `0x1090C8B0`, which replaces the key's text. GetInt `0x109081B0` returns `_wtoi`; GetBool `0x10908210`
  returns `"True"` or `_wtoi == 1`. So `1` reads as 1 through both readers. The format was never the problem.
- The reader. `DukePlayer.PreGameInit`, single player only (`!IsMP()`): `EgoCap = GetDefaultEgoCap()`,
  `MaxEgoCap = GetMaxEgoCap()`. Both add `PreorderBonusEgo` (+5) when `HasEnteredPreorderBonusEgoCode()`. These are
  its only callers. `PreGameInit` is raised by `ULevel::LoadMap_InitActors` (`0x1022AB9C`, `0x1022B16C`) and by
  `ULevel::SpawnActor` (`0x102379D3`), each as `actor->vt[1] (FindFunctionChecked(ENGINE_PreGameInit), 0, 0)`. For
  `ADukePlayer`, vt[1] is dnGame's ProcessEvent thunk, the pehook slot. `ENGINE_PreGameInit` is the name stored at
  `0x107E40B8`. Other actors' `PreGameInit` test `bNotCheckpointLoad` and `bStartup`, so it runs at every level start
  and checkpoint load. **The Ego Boost is an automatic +5 to both the ego cap and its maximum, applied at each level
  start or load. It is not a menu item and needs no new game.**
- With only the menu-time call (no re-apply), `nukepanel.log` shows `preorder ego 0` on every read after
  `applied`. The live `Pawn.EgoCap` was 70 with 17 awards (= 40 + awards, no +5) and 80 with 23. Remote
  `progress.ini` was untouched by that launch. With the re-apply on `PreGameInit` the +5 is present in the base game and
  the DLC (verified in game). The slot saw 14,188 events "elsewhere" in the 600 frames
  around the Entry -> Map09 load, so maps load on another thread (`g_LoadingOnThread`).
- Big Head shows without the flag for two reasons. `UDukeSceneExtras.ShouldShowMenuItem` shows the ExtraSettings entry when
  `HasEnteredPreorderBonusBigHeadCode()` **or** `HasFullGameCompletion(0)` is true, and the main menu shows EXTRAS
  for either. The cheat rows inside need `HasFullGameCompletion(1)`. So the Big Head row is visible without the flag
  once difficulty 0 is complete, and right after the menu call, before any map load.

The fix (bigpackage.cpp, pehook.cpp):
- `pehook_subscribe_any_thread(L"PreGameInit", ...)`. This is the one subscriber whose handler also runs off the game
  thread. `pe_next_call` lets only before-phase any-thread subscribers run there; everything else still passes
  straight through.
- The handler acts only when the event's UFunction Outer is `&ADukePlayer::PrivateStaticClass` (dnGame export
  `?PrivateStaticClass@ADukePlayer@@0VUClass@@A`, RVA 0x97740; the log's `DukePlayer=127B7740` at base 12720000).
  It also requires the menu call to be made and the switch to be on (`bp_armed`, published with
  `InterlockedExchangePointer`), and `GetDefaultObject` to still return the checked default object
  (`bp_reapply_ok`). It then calls `UnlockPreorderBonuses(2)`.
- Threads. The call repeats on the loading thread what the game's own `RescanDLC` did on that thread moments
  before. The handler reads the panel's state only as interlocked scalars, or as statics written before
  pehook_frame's `InterlockedExchange` barrier.
- Every game-thread config read pauses while `g_LoadingOnThread` is set (read interlocked, `engine_level_loading`):
  the Progress read behind the tab, Level stats and the ego, helmet and phone markers (`pg_cfg_read_allowed`; counted
  as `load skips` on the `progress: read` line), and the flags line below (`bp_flags_log_due`), so no game-thread
  Get overlaps the loading thread's writes to the unlocked cache.
- Log: `bigpackage: applied: [PreorderBonus] BonusEgo bool f/v int f/v, BigHead ...` after the menu call. On the
  first scriptable frame after a load (`g_LoadingOnThread` back to 0): `bigpackage: re-applied before
  DukePlayer.PreGameInit N time(s), M off the game thread, K refused; g_LoadingOnThread at the last re-apply L: ...`
  with the same four reads. L is captured inside the re-apply and is expected to be 1.

## 7. The T-shirts

**Multiplayer only.** The preorder groups' challenge IDs are character customisation items for the multiplayer
pawn. -15 is `POG_Gamestop_USA` and -16 is `POG_Stripper_Asia`: `Category` byte 5 = `CCC_ShirtLogo`, skins
`mt_skinsMaleBod.duke_mp_logos.duke_promo_d_GameStop_USA` / `..._Stripper_Asia`, `ChallObjClass`
`dnGame.ChallengeObject_Unlocked`. They are entries of the `ChallengesArray` defaults in Engine.u (tagged
properties at file offsets 0x78598 and 0x786AF), so `InitStatic`'s hash maps them and they are not skipped.
`UDukeSceneItemCustomization.BuildChallArrayShirts` lists them from
`DukeMultiPlayer.PlayerProgress.GetAllChallengeID(.., 4/5/6)` (shirts, logos, colours) and marks one worn when
`GetChallengeStatus == 3`. Single-player Duke has no shirt option. The unlock is memory only (bit0 of the entry's
+0x94). It is redone at the menu call and before each `DukePlayer.PreGameInit`, and `ResetDLC`'s `LoadConfig`
clears it on each map load. Whether the logo then shows as unlocked in the multiplayer Customize menu also depends
on `PlayerProgression`, which is not read here.

## 8. The DLC player and the re-apply

The DLC reads the preorder bonus exactly as the base game does. The panel's handler matches the event's Outer through
the `US_SUPER` chain, because an exact-Outer test never fires for `DukePlayerDLC03`.
- The script. `DukePlayerDLC03.PreGameInit` (DLC03_Game.u, decoded with tools/ubc.py) is an override that first calls
  `dnGame.DukePlayer.PreGameInit` (a direct final call, not a ProcessEvent), then on `!IsMP()` sets
  `EgoCap = GetDefaultEgoCap()` and `MaxEgoCap = GetMaxEgoCap()`. The DLC class does not override either getter, so
  the base `DukePlayer.GetDefaultEgoCap`/`GetMaxEgoCap` run: `default + PreorderBonusEgo` when
  `HasEnteredPreorderBonusEgoCode()`. So the DLC does add the +5 (cap 45 base + 30 auto), and then the 0 in the log is
  the reset flag, not design.
- Why an exact test fails. The engine raises `PreGameInit` as `actor->vt[1] (FindFunctionChecked(ENGINE_PreGameInit))` (section 6),
  which resolves the most-derived override: for the DLC pawn that is `DukePlayerDLC03.PreGameInit`, whose Outer is
  the DLC03_Game class, not `ADukePlayer::PrivateStaticClass`. An exact test (`outer != s_dukeClass`) returns
  without counting a refusal, so a DLC session logs no `re-applied` line; the handler therefore matches the superclass chain. The DLC's
  `ResetDLC` runs from the same `LoadMapInternal` `RescanDLC`, so nothing differs there.
- **The handler** (`on_pre_game_init`) accepts an Outer whose `US_SUPER` chain (uobj.h, +0x28)
  reaches `s_dukeClass` within a small bound (8 steps; the event's UFunction Outer is a UClass). `bp_reapply_ok` takes
  the matched class instead of the raw Outer (its unit test has a derived-class case). The re-apply runs at the DLC
  override's event, before its internal base call and its getters.

## Open questions

- `DukePlayer.PreGameInit` runs on the loading thread: `M off the game thread` > 0 (either thread works).
- `g_LoadingOnThread` covers the whole window of the loading thread's cache writes: `L` = 1 on the re-applied line.
  If it reads 0, the write happens outside LoadMap's flagged span, and the pause does not cover it.

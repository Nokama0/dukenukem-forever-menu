# Steam achievements: interface, game state, reset

The game's `steam_api.dll` (built 2011-04-13) hands out
`STEAMUSERSTATS_INTERFACE_VERSION009`. That version has `GetAchievementAndUnlockTime` (slot 9), so **unlock times
are available**, and `ResetAllStats` (slot 19) and `StoreStats` (slot 10). It has **no** `GetNumAchievements` or
`GetAchievementName`: the list of API names is the game's own 64-entry table in Engine.dll, copied into
`achreset.h`. The game requests the stats itself and pumps callbacks every tick, so the panel calls nothing until
Steam answers for at least one name. Reset = `ResetAllStats(true)` then `StoreStats()`, then `RequestCurrentStats()`
so the game's own handler reloads its cache. Progress.ini (helmets, phones, counters) is not touched by the reset.

Method: static reading of the game's `steam_api.dll` and Engine.dll and of Steam's `steamclient.dll`.

## 1. steam_api.dll

`System\steam_api.dll`, 124,712 bytes, PE timestamp 1302728131 (2011-04-13), image base 0x3B400000. Read with a
Python PE parser (exports and strings):

- Exports used: `SteamUserStats` (0x3B403740), `SteamUser`, `SteamUtils`, `SteamAPI_RunCallbacks`,
  `SteamAPI_RegisterCallback`. No flat API (`SteamAPI_ISteamUserStats_*`): calls go through the vtable.
- Interface strings in the image: `SteamClient010`, `SteamUser016`, `SteamFriends009`, `SteamUtils005`,
  `STEAMUSERSTATS_INTERFACE_VERSION009`, `STEAMAPPS_INTERFACE_VERSION004`, `STEAMREMOTESTORAGE_INTERFACE_VERSION004`.
  The accessor `SteamUserStats()` returns the 009 object; achreset.cpp refuses unless that string is in the loaded
  image (a replaced steam_api.dll may ask another version, with another slot order).

## 2. ISteamUserStats009 vtable (MSVC, `__thiscall`, `this` in ECX, callee pops)

| slot | +off | method | evidence |
|---:|---:|---|---|
| 0 | 0x00 | `bool RequestCurrentStats()` | Engine.dll!0x1033a8f0 tail-jumps `vt[0]` after `SteamUser()->BLoggedOn()` |
| 1-4 | | GetStat x2, SetStat x2 (overloads, MSVC order) | count only |
| 5 | | UpdateAvgRateStat | count only |
| 6 | 0x18 | `bool GetAchievement(name, bool*)` | 0x1033a730 (name, &entry.achieved) |
| 7 | 0x1c | `bool SetAchievement(name)` | 0x1033a610, then StoreStats |
| 8 | 0x20 | `bool ClearAchievement(name)` | 0x1033a6a0, then StoreStats |
| 9 | 0x24 | `bool GetAchievementAndUnlockTime(name, bool*, uint32*)` | the one slot between 8 and 10 |
| 10 | 0x28 | `bool StoreStats()` | 0x1033a610 / 0x1033a6a0 |
| 11 | | GetAchievementIcon | count only |
| 12 | 0x30 | `const char* GetAchievementDisplayAttribute(name, key)` | 0x1033a730 ("name", "desc") |
| 13-18 | | IndicateAchievementProgress, RequestUserStats, GetUserStat x2, GetUserAchievement, GetUserAchievementAndUnlockTime | count only |
| 19 | 0x4c | `bool ResetAllStats(bool bAchievementsToo)` | the slot before 20 |
| 20 | 0x50 | `SteamAPICall_t FindOrCreateLeaderboard(name, sort, display)` | 0x10348dc0 (name, 2, 1) |
| 26, 28, 29 | 0x68/0x70/0x74 | DownloadLeaderboardEntries, GetDownloadedLeaderboardEntry, UploadLeaderboardScore | 0x10349a60, 0x10348ea0, 0x10349540 |

Slots 9 and 19 are not called by the game; Steam's own code confirms them (static read of
steamclient.dll from the Steam install, image base 0x38000000). The RTTI type
`.?AVCAdapterSteamUserStats009@@` gives the adapter vtable at **0x3905b738**. Each adapter method forwards to the
inner `IClientUserStats` (`[ecx+4]`), whose IPC proxy (`.?AVIClientUserStatsMap@@`, vtable 0x39070190) names each
call by string:

| 009 slot | adapter | `ret` | forwards to | inner IPC name | method |
|---:|---|---|---|---|---|
| 0 | 0x38630ef0 | `ret` | +0x18 | "DeprecatedPublic_RequestCurrentStats" | RequestCurrentStats |
| 6 | 0x386341e0 | `ret 8` | +0x30, time pointer 0 | "GetAchievement" | GetAchievement |
| 9 | 0x38634240 | `ret 0xc` | +0x30, all three args | "GetAchievement" (1 byte to the bool*, 4 to the uint32*) | GetAchievementAndUnlockTime |
| 10 | 0x38634b90 | `ret` | +0x40 | "StoreStats" | StoreStats |
| 12 | 0x386342a0 | `ret 8` | +0x4c | "GetAchievementDisplayAttribute" | GetAchievementDisplayAttribute |
| 19 | 0x38634a00 | `ret 4` | +0x6c | "ResetAllStats" (serialises one byte, the bool) | ResetAllStats(bool) |
| 20 | 0x3862eeb0 | | jmp +0x70 | | FindOrCreateLeaderboard (the game's +0x50) |

The `ret n` values match achreset.cpp's `__fastcall` signatures (thiscall, callee pops). Cross-check:
`CAdapterSteamUserStats011` (vtable 0x3905b864) shares slots 0-13, inserts two entries after 13 and has 0x38634a00
at slot 21, the 011 layout. The game's steam_api.dll holds only the 009 string, so that string check is the real
version guard; achreset.cpp's one-module check of slots 0-20 only catches a hooked or foreign table.

**Unlock time:** available through slot 9 as a Unix time (seconds, UTC), 0 when Steam has none. The backup prints
"time unknown" for an unlocked achievement whose time is 0.

## 3. How the game uses it (Engine.dll, build 244)

- `dnSteamAchievementMgr` (vtable export `??_7dnSteamAchievementMgr@@6B@`, `GetInstance` exported). Its constructor
  0x1033ab20 registers three callbacks: UserStatsReceived -> 0x1033a730, UserStatsStored -> 0x1033a850,
  UserAchievementStored -> 0x1033a8c0. mgr+4 holds the app id (`SteamUtils()->GetAppID`, slot 9).
- 0x1033a8f0 (a mgr virtual) calls `RequestCurrentStats` when Steam is logged on.
- 0x1033a730 (UserStatsReceived, renamed in Ghidra): checks the game id, logs "Received stats and achievements from
  Steam" to the game's log, sets mgr+0xc (also set on a failed request, after logging "RequestStats - failed"),
  then fills the table at 0x10649f40 (64 entries, 400 bytes each: id, name pointer, display name[128],
  description[256], achieved) from slots 6 and 12.
- Award: 0x1033a650 `AwardAchievement(id)` -> 0x1033a610 = `SetAchievement(name)` + `StoreStats()`, gated only on
  mgr+0xc, **not** on the cached achieved flag. After a reset the game awards again without a restart.
- Callbacks: `SteamAPI_RunCallbacks` is called from 0x103401d0 (the engine's per-tick online update).

The 64 API names (0x10649f44, stride 400) are `ACH_SomeKills` (id 0) .. `ACH_Chauvinist` (62) and
`ACH_Deprecated2` (63); the list is in achreset.h. A name Steam does not know answers false and is listed in the
backup's "not answered" section.

## 4. Readiness

The panel never reads mgr+0xc (an engine offset). It asks Steam: if `GetAchievementAndUnlockTime` answers false
for every name, the stats are not loaded and the row refuses with that reason. Steam answers once the game's own
`RequestCurrentStats` has completed.

## 5. Persistence

- `ResetAllStats(true)` clears this user's stats and achievements for the app in the Steam client; the Steamworks
  documentation says it also stores them. The panel calls `StoreStats()` after it anyway, then
  `RequestCurrentStats()` so the game's 0x1033a730 reloads its table (the game writing its own memory).
- Steam stats are server-side. Steam Cloud (the `remote\` folder: system.ini, user.ini, Progress.ini, saves) is
  separate and untouched. Progress.ini's `[Achievements]` flags and counters (helmets, phones, kill and beer
  counters) stay, so an achievement whose counter is already complete may unlock again at its next increment
  (for example Helmets: finding any helmet again checks all three flags).
- Never automatic: the reset runs only from the confirmed Settings button, after the backup file is on disk. A
  confirm under 400 ms after the arm is ignored; the arm drops when its row leaves the screen (panel closed, another
  tab, scrolled out); a reset whose backup is not written within 10 s of the confirm is dropped.

## Open questions

- The log of a real run: the version check, the slot module, the collect time and counts.
- That a reset shows all achievements locked in the Steam overlay and that the game re-awards one.

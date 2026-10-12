# Steam CEG: Engine.dll hashes its own code and kills the game on a patch

Engine.dll carries Steam CEG tamper checks. Any inline patch of its code
(our SetMouseCapture detour) makes the game call `ExitProcess(0x8000DEAD)` from Engine.dll before
the D3D device exists: no exception, no crash event, no dump. **Never patch Engine.dll code.** Its import slots
(data) and code in system DLLs (d3d9.dll, dinput8.dll) are not checked: every build that hooked only those launched.

Method: the panel's log with its exit/exception logger; Engine.dll read in Ghidra (project dnf); an external CEG
analysis (below).

## Measured (nukepanel.log, with the exit/exception logger)

- `exit: ExitProcess(2147540653) from Engine.dll+0x36e730 tid=53980 exceptions=0 ; stack(0)`: 2147540653 =
  0x8000DEAD; zero exceptions in the session; the return address is inside the kill routine below. Two earlier
  launches ended the same way (log ends after `mousecap: hooked SetMouseCapture`, then `detach`).

## Read (Engine.dll in Ghidra, project dnf)

- `FUN_1036E6D0`: zeroes its stack page, `memset`, `ExitProcess`: the kill routine.
- `FUN_1036F0C0(char ok, int n, ...)`: when `ok == 0`, writes `FUN_1036E6D0` over a return address on the stack
  (one of n chosen by `GetTickCount() % (n-1)`), so the kill happens at a later, varying point.
- Checks calling it: `FUN_10369C10`, `FUN_1036A3F0`, `FUN_1036C450`. `FUN_1036C450` hashes 54 code ranges (offsets
  table `0x104EE968`, sizes `0x104EEA50`, relocation-aware: `0x10000000` image base, relocation block at
  `0x10A0ACF8`) and a 0x7000-byte block at `0x103D5C00`, then compares (`FUN_10375950`) and calls `FUN_1036F0C0(0,0)`
  on a mismatch.

## Rules that follow

- Hook Engine.dll only through its import slots, or by calling its exports. Never an inline detour, never a byte
  of its code. Other modules: no kill routine found in dnGame/dnAI/dnVehicles/D3DDrv9/dnCommon/DukeForever.exe
  (modules-ceg.md); a first detour there still needs one trial launch.
- Detours are fine in system DLLs reached through COM or exports (d3d9.dll's device, dinput8.dll's device).
- Writing engine *data* the engine itself writes stays governed by the project's memory rule (CEG does not hash it).

## What the main table covers (FUN_1036C450's 54 ranges)

Merged spans: `0x1036D400-0x10370C00`, `0x10373C00-0x10376000`, `0x103D9400-0x1041D200` (end of .text),
`0x10421000-0x10467000` and `0x1047A000-0x104A5A00` (.rdata), `0x104EBA00-0x104EEF78` (the CEG tables), 0x166978 bytes.
It covers **neither** `SetMouseCapture` (`0x102B9460`, yet patching it killed the game: another check, e.g.
`FUN_10369C10`/`FUN_1036A3F0`'s per-function ranges, covers it) **nor** our Engine.dll import slots
(`0x1041E150..0x1041E654`, KERNEL32 and USER32 thunks). Two full sessions with those slots patched (menu, a level
load) ran without a kill. Because the per-function checks cannot be enumerated from one table, the rule stays absolute
for code; the import slots are an observed-safe exception, watched by the exit/exception log.

## External reference

Rattpak, "CEG-Anti-Tamper-Analysis" (github.com/Rattpak/CEG-Anti-Tamper-Analysis; Black Ops II):
CEG CRCs code around protected functions and `.rdata`, some checks run per frame and some only on events (level
change, disconnect), kills by return-address smashing with 0x8000DEAD or by null-pointer-write kill switches.
Consequence here: a CEG kill can also appear as an access violation at a small address; the exception log shows it.

## Observed-safe slots and the normal breakpoint

- Engine.dll's ole32 `CoCreateInstance` import slot (RVA `0x420758`, outside the main table) patched for a full
  session with six panel opens: no kill. Observed-safe slots: `0x41E150..0x41E654` and `0x420758`; with the borderless build also `0x41E65C..0x41E70C` (USER32 SetWindowLongW/A, SetWindowPos, MoveWindow, ShowWindow; several sessions).
- Every launch raises one breakpoint exception inside CEG's own code: `exception: code=80000003
  addr=Engine.dll+0x36dd8d` (inside the hashed span `0x1036D400-0x10370C00`), handled by the game, which runs on.
  This is CEG's normal operation, not our fault: do not mistake it for a crash.
- Other modules' import slots patched every launch with no kill: DukeForever.exe, dnCommon.dll,
  BugHandler.dll and MSVCR90.dll (`ExitProcess`/`TerminateProcess`, file and cursor slots). CEG's tables cover
  Engine.dll only, as far as read.

# CEG per module: only Engine.dll carries the kill routine

The Steam CEG kill routine and its
return-address armer exist in **Engine.dll only**. dnGame.dll, dnAI.dll, dnVehicles.dll, D3DDrv9.dll, dnCommon.dll
and DukeForever.exe contain neither fingerprint, and none of them has Engine.dll's extra `.tls`/`.version`
sections. Engine.dll's checks hash its own image only (self-relative tables, image base constant 0x10000000). So an
inline detour in those modules is not blocked by any check we can see.

Method: read in Ghidra MCP, project `dnf` (Engine.dll, dnGame.dll, D3DDrv9.dll, dnCommon.dll, DukeForever.exe) plus dnAI.dll
and dnVehicles.dll loaded into the headless server from the install (read only). `search_byte_patterns` over each
whole program; `list_segments` per module. Renames and plate comments saved in Engine.dll and dnGame.dll.

## The fingerprints (read in Engine.dll)

| piece | bytes searched | Engine.dll hits |
|---|---|---|
| kill routine: zero the stack page, memset, `ExitProcess(0x8000DEAD)` via an XOR-obfuscated pointer (`Engine.dll!0x1036E6D0`, ceg.md) | `8D 8D 08 10 00 00 81 E1 00 F0 FF FF 8D 45 08 81 EC 00 08 00 00` (from +3) | 7 copies: 0x1036E530, 0x1036E6D0, 0x1036E790, 0x1036E970, 0x1036EB00, 0x1036EC10, 0x1036EDE0 (renamed `ceg_KillWipeStack*`) |
| the same, loose (page mask only) | `81 E1 00 F0 FF FF` | (used for the other modules) |
| return-address armer `FUN_1036F0C0` (`if (!ok)` write a kill routine over return address `GetTickCount() % (n-1)`) | `80 7D 08 00 75 ?? 56 8B 75 0C 85 F6 7F` (from +3) | 10 copies 0x1036EF40..0x1036F2A0, 0x60 apart (0x1036F0C0 renamed `ceg_ArmKillOnReturnAddress`) |
| the armer's `GetTickCount` modulo | `83 C6 FF 33 D2 F7 F6` | the same 10 |

The range checks (`FUN_10369C10`, renamed `ceg_CheckRange_A`, and `FUN_1036A3F0`) pass self-relative pointers
(`*p + p`) and the constant 0x10000000 to the hasher `FUN_1036F3F0`: they hash Engine.dll's own image.

## Per module

| module | kill routine | armer | loose patterns | extra sections | result |
|---|---|---|---|---|---|
| Engine.dll | 7 | 10 | yes | `.tls`, `.version` | **CEG: never patch code** (ceg.md) |
| dnGame.dll | 0 | 0 | 0 | none | no CEG seen: detour candidate (trial launch first) |
| dnAI.dll | 0 | 0 | 0 | none | no CEG seen: detour candidate (trial launch first) |
| dnVehicles.dll | 0 | 0 | 0 | none | no CEG seen: detour candidate (trial launch first) |
| D3DDrv9.dll | 0 | 0 | 0 | none | no CEG seen: detour candidate (trial launch first) |
| dnCommon.dll | 0 | 0 | 0 | `.tls` (the dnThread TLS), `.idata` | no CEG seen |
| DukeForever.exe | 0 | 0 | 0 | none | no CEG seen |

Not checked: dnAnimation.dll, meqon.dll, BugHandler.dll (no feature needs them yet).
A sanity search (`55 8B EC 83 EC`) returns many hits in dnVehicles.dll, so the byte search works on the freshly
loaded, unanalysed modules.

## Consequences

- The import-slot route stays the first choice (observed safe in every module, ceg.md). A dnGame.dll **import slot**
  already reaches most of what an Engine.dll detour would: dnGame's native class vtables call Engine's
  `AActor::ProcessEvent` through the thunk `dnGame.dll!0x10047CBA` (`JMP [0x10053054]`), and `ADukePlayer`'s vtable
  (`dnGame.dll!0x10059554`) holds that thunk in slot 1. dnAI.dll has the same shape: thunk `0x1006FA9C` ->
  `[0x100821E4]`, referenced by 14 vtables in its `.rdata` (whether `AAIActor`'s is one of them is open).
- An inline detour in dnGame/dnAI/dnVehicles/D3DDrv9 is the fallback, after one launch that patches a harmless
  byte range and shows no `exit: ExitProcess(2147540653)` line in nukepanel.log.

## Open questions

- The first inline detour in another module still needs one trial launch with the exit/exception log before a feature
  depends on it: CEG could hash another module from a place not read.
- Whether `AAIActor`'s vtable is one of the 14 in dnAI.dll's `.rdata` that reference the ProcessEvent thunk.
- dnAnimation.dll, meqon.dll and BugHandler.dll were not checked (no feature needs them).

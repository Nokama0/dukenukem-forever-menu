# Engine map (build 244): object and name tables, script calls, console, threads

The UE1 registries exist by
exported name. `UObject::GObjObjects` is a `dnArray<UObject*>` {data, count, max}; the UObject header is
Index +0x04, Outer +0x18, ObjectFlags +0x1C, Name +0x20, Class +0x24; names live in dnCommon's
`dnName::Names` (entry string at +0x0C, guarded by `NamesCS`). `ProcessEvent` is UObject vtable slot 1, thiscall
(UFunction*, Parms, Result). The console reaches script `exec` functions through `ScriptConsoleExec`, which
rejects any function carrying flag 0x20000000 (the shipping gate). One thread runs `MainLoop`, the engine tick,
the Windows message pump, `FinishRendering` and the device `Present`; with `r_ThreadRenderer=1` (the default)
only `RenderLevel` runs on a separate job thread. So the device Present runs on the
game/WndProc thread (section 5).

Method: read from the binaries with Ghidra MCP on the project `dnf` (Engine.dll, dnCommon.dll, D3DDrv9.dll, DukeForever.exe; decompile and
disassembly), export tables read with Python. Addresses are Ghidra's (image bases: DLLs 0x10000000,
DukeForever.exe 0x10900000). Renames and plate comments are saved in the project.

## 1. The object table

- Export `Engine.dll!0x1067F14C ?GObjObjects@UObject@@0V?$dnArray@PAVUObject@@@@A`.
- `UObject::GetIndexedObject` (`Engine.dll!0x1003D470`, cdecl, int index): `MOV ECX,0x1067F14C; CALL
  [dnArrayBase::Num]` then `MOV EAX,[0x1067F14C]; MOV EAX,[EAX+ESI*4]`. So data pointer at +0x00;
  `dnArrayBase::Num` (`dnCommon.dll!0x100016D6`) returns `[this+4]`, so count at +0x04 (max at +0x08 as in
  every dnArray). Slots may be null (UE1 free list `GObjAvailable` at `Engine.dll!0x1067F158`).
- UObject header, each read by an exported accessor (thiscall, ECX=this):

| field | offset | reader |
|---|---|---|
| Index | +0x04 | `GetIndex` `Engine.dll!0x100AA730`: `MOV EAX,[ECX+4]` |
| Outer | +0x18 | `GetOuter` `Engine.dll!0x10139130`: `[this+0x18]` |
| ObjectFlags | +0x1C | `GetFlags` `Engine.dll!0x1000CF10`: `[this+0x1C]` |
| Name (dnName, int index) | +0x20 | `GetFName` `Engine.dll!0x10009860`: `this+0x20`; `GetName` `0x1000CFE0`: `ADD ECX,0x20; JMP [dnName::operator*]` |
| Class | +0x24 | `GetClass` `Engine.dll!0x1000CFC0`: `[this+0x24]` |

- UStruct/UClass fields seen on the way: SuperField +0x28 (class-chain walk in `CastToChallengeInfo`
  `Engine.dll!0x1006CAE0`), the function hash `VfHash[256]` at +0xA0 with chain link at field+0x30
  (`FindObjectField` `Engine.dll!0x1003D3D0`), the class default object at UClass+0x508
  (`UClass::GetDefaultObject` export `Engine.dll!0x1000D8D0`). UFunction: FunctionFlags +0x88, ParmsSize
  (ushort) +0x92, ReturnValueOffset (ushort) +0x94, native thunk `Func` +0x98 (all read in `ProcessEvent`).
- Rule for our code: validate the array (data, count) once per walk, then read slots from that snapshot
  (one guarded read costs ~12 us).

## 2. The name table

- Export `dnCommon.dll!0x100A30D0 ?Names@dnName@@0V?$dnArray@PAUdnNameEntry@@@@A` (data +0, count +4),
  `?Available@dnName@@` at `0x100A30DC`, lock `?NamesCS@dnName@@0VdnThread_Semaphore@@A` at `0x100A30EC`,
  `?Initialized@dnName@@` at `0x100A30E8`.
- `dnName::operator*` (`dnCommon.dll!0x10002E28`, thiscall, ECX=&dnName): if not initialized returns
  `L"**uninitialized**"`; else `Lock(NamesCS)`, entry = `Names.data[*(int*)this]`, `UnLock`, returns
  `entry + 0x0C` (a `wchar_t*`). `dnName::GetEntry(int)` (`0x1000332D`) is the same lookup returning the entry.
  `GetNumNames` (`0x10003AA8`) = `Names.count - Available.count`.
- So a dnName is one 4-byte index; the string is UTF-16 at entry+0x0C. Off the game thread, call
  `operator*` (it takes the lock) or read `Names` once per walk under the same validation rule; the array can
  grow (reallocate) while names are added.
- Lookup by string: `??0dnName@@QAE@PB_WW4EFindName@@@Z` (`dnCommon.dll!0x10002162`) with FNAME_Find does not
  create names (used that way in `ScriptConsoleExec`).
- **Level names move across loads (measured at a checkpoint load, Map21 -> Entry -> Map21):**
  `catfishCounter` was index 79488 at level serial 2 and 86824 at serial 4. A map's own names (instance names, Tags,
  Events, `ACH_` events, map-package class names) are freed with the level and added again on the next load (the
  `Available` list above); engine and class names of the base packages stay put. Never keep such an index across a level
  serial: look the name up again before each scan pass (`lvnames.h`; `ah_resolve_names` in achhelp.h is the same rule
  for the achievement kinds), and read a lookup that finds nothing as absent. Per module: re-resolved
  each pass, logged as `<module>: level names, serial N: ...` (a key shows `moved from` or `gone`): breakables (stash
  Events, classes), collect (phone and helmet `ACH_` Events, classes), egoesp (class names, event classes and Tags),
  items and aircraft (class names), autowin (kind classes, on each serial; the Alien Abortion Tags and hit Events are
  looked up at use), achhelp (kinds), spawnbox (Octabrain) and pbtrace (pinball classes) on each serial. Stable, left
  alone: property and function names (`Event`, `Tag`, `Location`, `GetStateName`), `idle`, `Head`, the aim's head bones
  (cached per class and cleared by the aim's per-level reset), `dnControl_Turret`, `dnGrenadeLauncherProjectile`.

## 3. The script call path

- **FindFunction** `?FindFunction@UObject@@QBIPAVUFunction@@ABVdnName@@H@Z` `Engine.dll!0x10040AA0`.
  **__fastcall** (mangling `QBI`, and the code): ECX=this, EDX=`const dnName*`, `[esp+4]`=int bGlobal, `RET 4`.
  Body: `FindObjectField` (`0x1003D3D0`: bucket = name index & 0xFF; first this->StateFrame(+0x0C)->State(+0x14)
  ->VfHash unless bGlobal, then Class(+0x24)->VfHash at +0xA0) then `CastFieldToFunction` (`0x100130C0`).
  `FindFunctionChecked` (`0x100409E0`, same convention) returns 0 when `GIsScriptable`
  (`Engine.dll!0x107702B0`) is 0 and logs a miss.
- **ProcessEvent** `?ProcessEvent@UObject@@UAEXPAVUFunction@@PAX1@Z` `Engine.dll!0x10024C90`, **vtable slot 1
  (+4)**. thiscall: ECX=this (`MOV EDI,ECX` at 0x10024CB0), `[EBP+8]`=UFunction*, `[EBP+0xC]`=Parms,
  `[EBP+0x10]`=Result. Early outs: `GIsScriptable==0`; `IsProbing(Func->Name)` false; `this->vt[0x30]()`
  nonzero (pending kill); `FunctionFlags & 0x1000`. If `FunctionFlags & 0x400` (native) it first tries
  `this->vt[0x10]` (remote call). Then it copies `ParmsSize` bytes of Parms into a frame, calls `Func(+0x98)`
  and copies the parms back (out parms and return value land in the caller's Parms block). Natives work
  through it: their script is `EX_NativeParm` tokens (Engine.u bytes `29 <prop> 0B`).
- **Call site** `AChallengeInfo::eventCodeUnlockedPreorderGroup` `Engine.dll!0x1006C6B0` (fastcall:
  ECX=this, DL=group byte, two dnString& on the stack): at 0x1006C719 `MOV EDX,0x107E4698` (the dnName export
  `ENGINE_CodeUnlockedPreorderGroup`), `MOV ECX,ESI`, `PUSH 0`, `CALL FindFunctionChecked`; then `PUSH 0`
  (Result), `PUSH &parms`, `PUSH EAX` (UFunction*), `MOV ECX,ESI`, `CALL [vtable+4]` (0x1006C72F..0x1006C731).
  Its parms block: byte Group at +0, dnString at +4 and +0x10.

## 4. The console / command path

- `APlayerPawn::ConsoleCommand` `Engine.dll!0x10263630`, virtual thiscall
  `dnString (const dnString& Cmd, int, int)` with a hidden return pointer (mangling `UAE?AVdnString@@ABV2@HH@Z`):
  if `Player` (`this+0x12B8`) is set, calls the player's FExec subobject (`Player+0x2C`) slot 0 `Exec(Cmd, Ar)`.
- `AActor::ConsoleCommand` `Engine.dll!0x1027FBF0` (same signature; decompiled, the int-to-role mapping not
  checked in the disassembly): with the first int set it tries
  `this->vt[0x44]` (script exec) first; otherwise it routes to the level's engine FExec (`XLevel(+0x7C)+0x6C8`).
- `UObject::ScriptConsoleExec` `Engine.dll!0x10042860` (thiscall `(const wchar_t*, dnOutputDevice&, UObject*)`):
  needs `GIsScriptable`; finds the first word as a dnName (find only) in the state then class function hash;
  **gate at 0x1004298C-0x1004299C: requires `FunctionFlags & 0x200` (exec) and rejects `& 0x20000000`**; parses
  parameters and calls `ProcessEvent` (`[vtable+4]`, at 0x10042B0B).
- Other FExec handlers exported: `UGameEngine::Exec` 0x102297A0, `UEngine::Exec` 0x102206A0, `UObject::StaticExec`
  0x10046520, `FDnExec::Exec` 0x10009C60 (cvars such as `r_ThreadRenderer`, registered by static initializers,
  e.g. 0x103EB470).
- Consequence: script `exec` functions flagged 0x20000000 (the developers' debug/cheat execs, e.g.
  `DebugUnlockPreorder`, big-package.md) cannot run from any console in this build.

## 5. The game thread and the render thread

- `DukeForever.exe!0x1090DB30` (`WinMainGuarded`, called by `__tmainCRTStartup`) calls the Engine export
  `MainLoop(UEngine*, ulong)` (`Engine.dll!0x102444B0`) through the import at `0x1091216C` (call at 0x1090EAF1).
- `MainLoop` loop body on that one thread: `Engine->vt[0x5C] (GDeltaTime)` = `UGameEngine::Tick`
  (`Engine.dll!0x102311F0`, the vtable entry at 0x104A3E50), the engine's own frame limiter (`vt[0xA4]` max tick
  rate, then `appSleep`), then `PeekMessage/TranslateMessage/DispatchMessage`. **So the WndProc and the tick run
  on the same (main) thread.**
- Rendering: `UGameEngine::Draw` (vt +0x88) -> `dnRenderThreadMgr::RenderViewport` (`Engine.dll!0x10116490`):
  `WaitTillNotRendering` (`0x10115260`: waits on the job event, then `FinishRendering` on the calling thread),
  `Viewport->vt[0x90]` when `Viewport+0xB8` is set (the flag `UViewport::Present` clears, so probably a deferred
  present), then either `StartRenderingThread` (`0x101152F0`: hands the scene to
  the job) or, unthreaded, `RenderLevel` + `FinishRendering` inline.
- The job: `EnsureJob` (`0x10115860`) creates a `dnThreadMgr` job running `dnRenderThreadMgr_RenderJobLoop`
  (`0x10115710`) and stores its ID in `dnCommon.dll!0x100958EC ?g_RenderThreadID@@3KA`. The loop only does
  `RenDev->vt[0x19C]` (acquire), `RenderLevel` under a semaphore, `RenDev->vt[0x1A0]` (release).
- Gate: cvar `r_ThreadRenderer` = `Engine.dll!0x1063BCAC` (`g_dwThreadRendererCvar`), static value **1**; no
  override in `system.ini`/`user.ini` (read only).
- Present: `UViewport::Present` (`Engine.dll!0x102120C0`) -> `RenDev(+0x60)->vt[0x78]` =
  `UD3DRenderDevice9::Present` (`D3DDrv9.dll!0x1002F860`): presents under a `dnThread_Semaphore` and drains one
  entry of an `SLIST` upload queue. Reached from `FinishRendering`/`RenderViewport`, i.e. the game thread.
- **Threads:** device Present = game thread = WndProc thread. The scene's draw calls (RenderLevel) run on the
  render job thread, overlapped with the next tick; the game thread waits for the job before FinishRendering and
  Present. Our Present-time drawing is therefore serialized with the job. The d3dcap render watch logs the thread
  IDs (WndProc, engine Present, device Present, the job) next to `g_RenderThreadID`.

## 6. The config cache

- The `GConfig` vtable slot +0x2C used by `UnlockPreorderBonuses` is SetInt: it formats the value with `"%i"` and
  calls SetString (big-package.md section 6).

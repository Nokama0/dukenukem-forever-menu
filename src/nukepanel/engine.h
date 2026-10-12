#pragma once
// Engine.dll exports, resolved once by their exact decorated names (dumpbin /exports, build 244). A missing export
// is logged and leaves its slot null: only the feature that needs it is disabled.
#include <stddef.h>

struct EngineExports {
  const int* gIsScriptable;      // ?GIsScriptable@@3HA
  void* challengeInfoClass;      // ?PrivateStaticClass@AChallengeInfo@@0VUClass@@A
  void* getDefaultObject;        // ?GetDefaultObject@UClass@@QAEPAVUObject@@XZ
  void* unlockPreorderBonuses;   // ?UnlockPreorderBonuses@AChallengeInfo@@QAEXW4EPreorderGroup@@@Z
  const void* gObjObjects;       // ?GObjObjects@UObject@@0V?$dnArray@PAVUObject@@@@A: {data +0, count +4, max +8}
  void* findFunction;            // ?FindFunction@UObject@@QBIPAVUFunction@@ABVdnName@@H@Z
  // ?ProcessEvent@UObject@@UAEXPAVUFunction@@PAX1@Z: only compared with an object's vtable slot 1 in logs; calls
  // go through the slot, which a native class may route elsewhere (dnGame's thunk for the player).
  void* processEvent;
  void* globalSetProperty;       // ?GlobalSetProperty@UObject@@SAXPB_WPAVUClass@@PAVUProperty@@HHH@Z
  // dnCommon.dll (engine-map.md section 2)
  void* nameFind;                // ??0dnName@@QAE@PB_WW4EFindName@@@Z
  void* nameStr;                 // ??DdnName@@QBEPB_WXZ
  const void* names;             // ?Names@dnName@@0V?$dnArray@PAUdnNameEntry@@@@A: {data +0, count +4}
  // ?GConfig@@3PAVdnConfigCache@@A: the slot holding the game's config cache (FConfigCacheIni in DukeForever.exe).
  void* const* gConfig;
  // Not an export: r_AspectScale's float storage, Engine.dll RVA 0x6472B4 (justification in engine.cpp).
  const float* aspectScale;
  // The console-variable registry (docs/re/qol.md section 4): the FDnExec* global and its lookup.
  void* const* dnExec;           // ?GDnExec@@3PAVFDnExec@@A
  void* findVariable;            // ?FindVariable@FDnExec@@QAEPAVFExecVariable@@PA_W@Z
  // dnCommon.dll's g_IgnoreFocusChanges (qol.md section 1): appInit sets it for -IGNOREFOCUSCHANGES.
  volatile long* ignoreFocusChanges;
  // ?g_LoadingOnThread@@3HA (0x107EBC24): set by LoadMap while a level loads on the loading thread.
  volatile long* loadingOnThread;
  // dnCommon.dll's GEngine (an undecorated data export): the slot holding the UEngine*. Not counted below.
  void* const* gEngine;
};

// Game thread. Resolves on the first call (Engine.dll is loaded long before the first Present) and logs once.
const EngineExports* engine_exports(void);

// [p, p+len) is committed and readable, by VirtualQuery (a few regions at most). Callers cache the answer per
// pointer: it is a guarded read's cost (~12 us here), never a per-frame check.
bool engine_readable(const void* p, size_t len);

// UClass::GetDefaultObject: __thiscall, ECX = the class, no arguments (body `mov eax,[ecx+0x508]; ret`).
void* engine_get_default_object(void* cls);
// AChallengeInfo::UnlockPreorderBonuses(EPreorderGroup): __thiscall, one dword argument, `ret 4`.
void engine_unlock_preorder_bonuses(void* self, int group);

// UObject::FindFunction (Engine.dll!0x10040AA0): __fastcall, ECX = the object, EDX = &dnName, [esp+4] = bGlobal,
// `ret 4`; the UFunction or null. bGlobal=1 skips the object's current state (engine-map.md section 3).
void* engine_find_function(void* obj, int nameIdx, int global);
// UObject::ProcessEvent through the object's vtable slot 1: __thiscall (UFunction*, Parms, Result), `ret 0xC`
// (Engine.dll!0x10024E6C). Result is always null: return values land in Parms.
void engine_process_event(void* obj, void* func, void* parms);

// UObject::GlobalSetProperty (Engine.dll!0x10043400): static cdecl (value, class, property, offset, bAllInstances,
// bNoSave), caller pops. Always called with bAllInstances 0 (ImportText into the class default at
// [class+0x508] + offset only) and bNoSave 1 (its SaveConfig(CDO, 0x4000) is skipped): neither is a parameter here.
void engine_set_default_property(const wchar_t* value, void* cls, void* prop, int offset);
// The same call with bAllInstances 1: every object whose class chain names cls (FUN_1003f3f0: ImportText at
// object + offset, then PostEditChange: outside the editor AActor's returns after UObject::Modify, a no-op without
// GUndo), then the class default; bNoSave 1. For a property spawned actors read from their own copy (turretwep.cpp, MaxSpread).
void engine_set_property_all(const wchar_t* value, void* cls, void* prop, int offset);

// FDnExec::FindVariable (Engine.dll!0x100099B0): __thiscall, ECX = *GDnExec, one wchar_t* argument, `ret 4`; walks
// the list at +4 (next at entry +0x18) by _wcsicmp. Never reorders it (Exec moves a hit to the front; this does
// not). Null on a miss or while the registry does not exist.
void* engine_find_variable(const wchar_t* name);
// *GDnExec, or null.
void* engine_dn_exec(void);
// The config cache's reads: GConfig->vt[1] GetBool and vt[2] GetInt, __thiscall (Section, Key, int* Value, Filename),
// callee pops, nonzero when the key exists (UObject::execGetConfigBool, Engine.dll!0x10021EEE). Reads only, never Set
// or SaveConfig. Game thread (the cache has no lock). *value is left 0 on a miss.
int engine_config_get_bool(const wchar_t* section, const wchar_t* key, int* value, const wchar_t* file);
int engine_config_get_int(const wchar_t* section, const wchar_t* key, int* value, const wchar_t* file);
// The cache and its vtable are readable (checked once per cache pointer).
bool engine_config_ready(void);
// g_LoadingOnThread, read interlocked; any thread once engine_exports has run. 0 when the export is missing (logged).
int engine_level_loading(void);

// dnName(const wchar_t*, EFindName) with mode 0, FNAME_Find: the body returns index 0 (NAME_None) on a miss and
// only takes the add path for a nonzero mode (dnCommon.dll!0x100111E0; __thiscall, `ret 8`). Never creates a name.
int engine_name_find(const wchar_t* s);
// dnName::operator* (dnCommon.dll!0x10002E28, __thiscall, no arguments): the entry's UTF-16 string under NamesCS.
// The index is bounded by Names.count first; null when out of range or the slot is empty.
const wchar_t* engine_name_str(int idx);

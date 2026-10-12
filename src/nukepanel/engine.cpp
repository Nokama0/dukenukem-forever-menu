// Engine.dll exports by decorated name. Addresses and conventions: docs/re/engine-map.md, docs/re/big-package.md.
#include <windows.h>
#include <stdint.h>
#include "engine.h"
#include "bigpackage.h"   // bp_protect_readable, bp_span_inside
#include "log.h"

// __thiscall methods called through __fastcall: ECX carries `this`, EDX is unused, any further dword goes on the
// stack and the callee pops it, which matches each export's own `ret`/`ret 4` (read from the bytes, 2026-10-04).
typedef void* (__fastcall* GetDefaultObjectFn)(void* cls, void* edx);
typedef void (__fastcall* UnlockPreorderBonusesFn)(void* self, void* edx, int group);
typedef void* (__fastcall* NameFindFn)(int* self, void* edx, const wchar_t* s, int mode);
typedef const wchar_t* (__fastcall* NameStrFn)(const int* self, void* edx);
// FindFunction is __fastcall itself (mangling QBI): the name pointer is the EDX argument, bGlobal the stack one.
typedef void* (__fastcall* FindFunctionFn)(void* self, const int* name, int global);
typedef void (__fastcall* ProcessEventFn)(void* self, void* edx, void* func, void* parms, void* result);
typedef void* (__fastcall* FindVariableFn)(void* self, void* edx, const wchar_t* name);
// dnConfigCache::GetBool / GetInt: four stack arguments, `ret 0x10` (no caller cleanup after the calls cited in
// engine.h). Only the low byte of the result is trusted: the callers store it as a UBOOL.
typedef int (__fastcall* ConfigGetFn)(void* self, void* edx, const wchar_t* section, const wchar_t* key, int* value,
                                      const wchar_t* file);
typedef void (__cdecl* GlobalSetPropertyFn)(const wchar_t* value, void* cls, void* prop, int offset, int all,
                                            int noSave);

// EFindName's find-only value: the constructor adds a name only when the mode is nonzero (dnCommon.dll!0x100111E0).
static const int FNAME_FIND = 0;

static EngineExports s_ex;
static bool s_resolved;

static void* resolve(HMODULE eng, const char* name, int* found) {
  void* p = (void*)GetProcAddress(eng, name);
  if (p) ++*found;
  else np_log("engine: missing export %s", name);
  return p;
}

const EngineExports* engine_exports(void) {
  if (s_resolved) return &s_ex;
  s_resolved = true;
  const HMODULE eng = GetModuleHandleW(L"Engine.dll");
  if (!eng) {
    np_log("engine: Engine.dll is not loaded; its exports are unavailable");
    return &s_ex;
  }
  int found = 0;
  s_ex.gIsScriptable = (const int*)resolve(eng, "?GIsScriptable@@3HA", &found);
  s_ex.challengeInfoClass = resolve(eng, "?PrivateStaticClass@AChallengeInfo@@0VUClass@@A", &found);
  s_ex.getDefaultObject = resolve(eng, "?GetDefaultObject@UClass@@QAEPAVUObject@@XZ", &found);
  s_ex.unlockPreorderBonuses = resolve(eng, "?UnlockPreorderBonuses@AChallengeInfo@@QAEXW4EPreorderGroup@@@Z",
                                       &found);
  // Data exports are checked once here: GIsScriptable is read every frame, the class at +0x508 by
  // GetDefaultObject (engine-map.md section 1).
  if (s_ex.gIsScriptable && !engine_readable(s_ex.gIsScriptable, sizeof(int))) {
    np_log("engine: GIsScriptable at %p is not readable", (const void*)s_ex.gIsScriptable);
    s_ex.gIsScriptable = 0;
    --found;
  }
  // Not counted in the 10 below: a missing one only leaves the config reads ungated during loads.
  s_ex.loadingOnThread = (volatile long*)GetProcAddress(eng, "?g_LoadingOnThread@@3HA");
  if (s_ex.loadingOnThread && !engine_readable((const void*)s_ex.loadingOnThread, sizeof(long)))
    s_ex.loadingOnThread = 0;
  np_log("engine: g_LoadingOnThread=%p%s", (const void*)s_ex.loadingOnThread,
         s_ex.loadingOnThread ? "" : " MISSING: config reads are not paused during level loads");
  s_ex.gObjObjects = resolve(eng, "?GObjObjects@UObject@@0V?$dnArray@PAVUObject@@@@A", &found);
  s_ex.findFunction = resolve(eng, "?FindFunction@UObject@@QBIPAVUFunction@@ABVdnName@@H@Z", &found);
  s_ex.processEvent = resolve(eng, "?ProcessEvent@UObject@@UAEXPAVUFunction@@PAX1@Z", &found);
  s_ex.globalSetProperty = resolve(eng, "?GlobalSetProperty@UObject@@SAXPB_WPAVUClass@@PAVUProperty@@HHH@Z", &found);
  if (s_ex.gObjObjects && !engine_readable(s_ex.gObjObjects, 12)) {
    np_log("engine: GObjObjects at %p is not readable", s_ex.gObjObjects);
    s_ex.gObjObjects = 0;
    --found;
  }
  if (s_ex.challengeInfoClass && !engine_readable(s_ex.challengeInfoClass, 0x50C)) {
    np_log("engine: AChallengeInfo::PrivateStaticClass at %p is not readable", s_ex.challengeInfoClass);
    s_ex.challengeInfoClass = 0;
    --found;
  }
  // r_AspectScale has no export. Its storage is the float at 0x106472B4 (RVA 0x6472B4), the pointer handed to the
  // cvar's constructor at 0x103F29BA and the one FSceneNode::GetFOV multiplies by (0x10212671); the image holds 1.0f
  // there. Valid only for the stock build 244 Engine.dll (stockgate hashes it first, boot.cpp). Read, never written.
  const float* as = (const float*)((const char*)eng + 0x6472B4);
  s_ex.aspectScale = engine_readable(as, sizeof(float)) ? as : 0;
  np_log("engine: r_AspectScale storage %p (RVA 0x6472B4) %s, value %.3f", (const void*)as,
         s_ex.aspectScale ? "readable" : "NOT readable; assuming 1.0", s_ex.aspectScale ? *s_ex.aspectScale : 1.0);
  s_ex.dnExec = (void* const*)resolve(eng, "?GDnExec@@3PAVFDnExec@@A", &found);
  s_ex.findVariable = resolve(eng, "?FindVariable@FDnExec@@QAEPAVFExecVariable@@PA_W@Z", &found);
  if (s_ex.dnExec && !engine_readable(s_ex.dnExec, sizeof(void*))) {
    np_log("engine: GDnExec at %p is not readable", (const void*)s_ex.dnExec);
    s_ex.dnExec = 0;
    --found;
  }
  np_log("engine: cvar registry GDnExec=%p (-> %p) FindVariable=%p", (const void*)s_ex.dnExec,
         s_ex.dnExec ? *s_ex.dnExec : 0, s_ex.findVariable);
  np_log("engine: Engine.dll=%p exports %d/10 GIsScriptable=%p ChallengeInfo class=%p GetDefaultObject=%p "
         "UnlockPreorderBonuses=%p GObjObjects=%p FindFunction=%p ProcessEvent=%p GlobalSetProperty=%p", (void*)eng,
         found, (const void*)s_ex.gIsScriptable, s_ex.challengeInfoClass, s_ex.getDefaultObject,
         s_ex.unlockPreorderBonuses, s_ex.gObjObjects, s_ex.findFunction, s_ex.processEvent, s_ex.globalSetProperty);
  const HMODULE com = GetModuleHandleW(L"dnCommon.dll");
  if (!com) {
    np_log("engine: dnCommon.dll is not loaded; names are unavailable");
    return &s_ex;
  }
  int cfound = 0;
  s_ex.nameFind = resolve(com, "??0dnName@@QAE@PB_WW4EFindName@@@Z", &cfound);
  s_ex.nameStr = resolve(com, "??DdnName@@QBEPB_WXZ", &cfound);
  s_ex.names = resolve(com, "?Names@dnName@@0V?$dnArray@PAUdnNameEntry@@@@A", &cfound);
  if (s_ex.names && !engine_readable(s_ex.names, 8)) {
    np_log("engine: dnName::Names at %p is not readable", s_ex.names);
    s_ex.names = 0;
    --cfound;
  }
  s_ex.ignoreFocusChanges = (volatile long*)resolve(com, "g_IgnoreFocusChanges", &cfound);
  if (s_ex.ignoreFocusChanges && !engine_readable((const void*)s_ex.ignoreFocusChanges, sizeof(long))) {
    np_log("engine: g_IgnoreFocusChanges at %p is not readable", (const void*)s_ex.ignoreFocusChanges);
    s_ex.ignoreFocusChanges = 0;
    --cfound;
  }
  s_ex.gConfig = (void* const*)resolve(com, "?GConfig@@3PAVdnConfigCache@@A", &cfound);
  if (s_ex.gConfig && !engine_readable(s_ex.gConfig, sizeof(void*))) {
    np_log("engine: GConfig at %p is not readable", (const void*)s_ex.gConfig);
    s_ex.gConfig = 0;
    --cfound;
  }
  s_ex.gEngine = (void* const*)GetProcAddress(com, "GEngine");
  if (s_ex.gEngine && !engine_readable(s_ex.gEngine, sizeof(void*))) s_ex.gEngine = 0;
  np_log("engine: GEngine slot %p%s", (const void*)s_ex.gEngine, s_ex.gEngine ? "" : " MISSING: the fullscreen watch is off");
  np_log("engine: dnCommon.dll=%p exports %d/5 dnName(find)=%p operator*=%p Names=%p GConfig=%p "
         "g_IgnoreFocusChanges=%p (%ld)", (void*)com, cfound, s_ex.nameFind, s_ex.nameStr, s_ex.names,
         (const void*)s_ex.gConfig, (const void*)s_ex.ignoreFocusChanges,
         s_ex.ignoreFocusChanges ? *s_ex.ignoreFocusChanges : -1L);
  return &s_ex;
}

bool engine_readable(const void* p, size_t len) {
  if (!p) return false;
  uintptr_t at = (uintptr_t)p;
  const uintptr_t end = at + len;
  if (end < at) return false;
  for (int i = 0; i < 4 && at < end; i++) {   // an object spans a few regions at most
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((const void*)at, &mbi, sizeof mbi)) return false;
    if (mbi.State != MEM_COMMIT || !bp_protect_readable(mbi.Protect)) return false;
    const uintptr_t base = (uintptr_t)mbi.BaseAddress;
    if (!bp_span_inside(base, mbi.RegionSize, at, 1)) return false;
    at = base + mbi.RegionSize;
  }
  return at >= end;
}

void* engine_get_default_object(void* cls) {
  const GetDefaultObjectFn fn = (GetDefaultObjectFn)s_ex.getDefaultObject;
  return fn && cls ? fn(cls, 0) : 0;
}

void engine_unlock_preorder_bonuses(void* self, int group) {
  const UnlockPreorderBonusesFn fn = (UnlockPreorderBonusesFn)s_ex.unlockPreorderBonuses;
  if (fn && self) fn(self, 0, group);
}

void* engine_find_function(void* obj, int nameIdx, int global) {
  const FindFunctionFn fn = (FindFunctionFn)s_ex.findFunction;
  if (!fn || !obj || !nameIdx) return 0;
  return fn(obj, &nameIdx, global);
}

void engine_process_event(void* obj, void* func, void* parms) {
  if (!obj || !func) return;
  const ProcessEventFn pe = (ProcessEventFn)(*(void* const* const*)obj)[1];
  pe(obj, 0, func, parms, 0);
}

void* engine_dn_exec(void) { return s_ex.dnExec ? *(void* const volatile*)s_ex.dnExec : 0; }

void* engine_find_variable(const wchar_t* name) {
  const FindVariableFn fn = (FindVariableFn)s_ex.findVariable;
  void* const exec = engine_dn_exec();
  return fn && exec && name ? fn(exec, 0, name) : 0;
}

void engine_set_default_property(const wchar_t* value, void* cls, void* prop, int offset) {
  const GlobalSetPropertyFn fn = (GlobalSetPropertyFn)s_ex.globalSetProperty;
  if (fn && value && cls && prop) fn(value, cls, prop, offset, 0, 1);
}

void engine_set_property_all(const wchar_t* value, void* cls, void* prop, int offset) {
  const GlobalSetPropertyFn fn = (GlobalSetPropertyFn)s_ex.globalSetProperty;
  if (fn && value && cls && prop) fn(value, cls, prop, offset, 1, 1);
}

int engine_name_find(const wchar_t* s) {
  const NameFindFn fn = (NameFindFn)s_ex.nameFind;
  if (!fn || !s) return 0;
  int idx = 0;
  fn(&idx, 0, s, FNAME_FIND);
  return idx;
}

const wchar_t* engine_name_str(int idx) {
  const NameStrFn fn = (NameStrFn)s_ex.nameStr;
  if (!fn || !s_ex.names || idx < 0) return 0;
  // Names.count (+4) is a scalar; the data pointer is read by operator* under the engine's own lock.
  if (idx >= *(const volatile int*)((const char*)s_ex.names + 4)) return 0;
  const wchar_t* str = fn(&idx, 0);
  return (uintptr_t)str < 0x10000 ? 0 : str;   // an empty slot comes back as null + 0x0C
}

// The cache object is created at boot and kept; its pointer and vtable are checked once per pointer value.
static void* s_cfgChecked;
static bool s_cfgOk;

bool engine_config_ready(void) {
  if (!engine_exports()->gConfig) return false;
  void* cache = *s_ex.gConfig;
  if (!cache) return false;
  if (cache != s_cfgChecked) {
    s_cfgChecked = cache;
    const void* const* vt = engine_readable(cache, sizeof(void*)) ? *(const void* const* const*)cache : 0;
    s_cfgOk = vt && engine_readable(vt, 3 * sizeof(void*)) && vt[1] && vt[2];
    np_log("engine: config cache %p vtable %p GetBool %p GetInt %p %s", cache, (const void*)vt, vt ? vt[1] : 0,
           vt ? vt[2] : 0, s_cfgOk ? "usable" : "NOT usable");
  }
  return s_cfgOk;
}

int engine_level_loading(void) {
  volatile long* const p = s_ex.loadingOnThread;
  return p ? (InterlockedCompareExchange(p, 0, 0) != 0) : 0;
}

static int config_get(int slot, const wchar_t* section, const wchar_t* key, int* value, const wchar_t* file) {
  *value = 0;
  if (!engine_config_ready()) return 0;
  void* cache = *s_ex.gConfig;
  const ConfigGetFn fn = (ConfigGetFn)(*(void* const* const*)cache)[slot];
  return (fn(cache, 0, section, key, value, file) & 0xFF) != 0;
}

int engine_config_get_bool(const wchar_t* section, const wchar_t* key, int* value, const wchar_t* file) {
  return config_get(1, section, key, value, file);
}

int engine_config_get_int(const wchar_t* section, const wchar_t* key, int* value, const wchar_t* file) {
  return config_get(2, section, key, value, file);
}

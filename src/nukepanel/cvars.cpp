// Console variables through GDnExec. An FExecVariable (ctor Engine.dll!0x1000AEF0): name +0, description +4, flags +8,
// storage +0xC, type name +0x10, type handler +0x14, next +0x18. Every post-effect variable has flags 0; only
// SaveConfigVariables (flags bit 0) writes one to an ini. Game thread; the only write is to engine storage.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "cvars.h"
#include "engine.h"
#include "log.h"

enum { EV_NAME = 0x0, EV_FLAGS = 0x8, EV_PTR = 0xC, EV_TYPE = 0x10, EV_NEXT = 0x18, EV_SIZE = 0x1C };
enum { EXEC_VARS = 0x4 };   // FDnExec: the variable list head (FindVariable reads [this+4])
static const int LOG_ALL_CAP = 4096;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static bool read_entry(const void* var, unsigned char out[EV_SIZE]) {
  __try {
    memcpy(out, var, EV_SIZE);
    return true;
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

// An engine UTF-16 string as ASCII ('?' beyond), cut at cap - 1. False on a fault.
static bool read_wstr(const wchar_t* s, char* out, int cap) {
  out[0] = 0;
  if (!s) return true;
  __try {
    int i = 0;
    for (; i < cap - 1 && s[i]; i++) out[i] = s[i] < 0x80 ? (char)s[i] : '?';
    out[i] = 0;
    return true;
  } __except (av_only(GetExceptionCode())) {
    out[0] = 0;
    return false;
  }
}

static bool read_dword(const void* p, unsigned* out) {
  __try {
    memcpy(out, p, 4);
    return true;
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

static const void* field_ptr(const unsigned char* e, int ofs) {
  const void* p;
  memcpy(&p, e + ofs, sizeof p);
  return p;
}

CvarRef cvar_find(const wchar_t* name, const char** why) {
  CvarRef c = { 0, 0, 0, 0 };
  const char* dummy;
  if (!why) why = &dummy;
  *why = 0;
  const EngineExports* ex = engine_exports();
  if (!ex->findVariable || !engine_dn_exec()) { *why = "the engine's variable registry was not found"; return c; }
  const void* var = engine_find_variable(name);
  char n[64];
  read_wstr(name, n, sizeof n);
  if (!var) {
    *why = "the variable is not registered in this build";
    np_log("cvar: %s not registered", n);
    return c;
  }
  unsigned char e[EV_SIZE];
  char type[16];
  if (!read_entry(var, e) || !read_wstr((const wchar_t*)field_ptr(e, EV_TYPE), type, sizeof type)) {
    *why = "the variable's entry could not be read";
    np_log("cvar: %s entry %p not readable", n, var);
    return c;
  }
  unsigned flags;
  memcpy(&flags, e + EV_FLAGS, 4);
  volatile long* const ptr = (volatile long*)field_ptr(e, EV_PTR);
  // Static storage of a module that stays loaded: read every frame without a guard once this holds.
  HMODULE mod = 0;
  const bool inImage = ptr && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                                     GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                 (LPCWSTR)ptr, &mod) && engine_readable((const void*)ptr, 4);
  unsigned value = 0;
  if (inImage) read_dword((const void*)ptr, &value);
  wchar_t modPath[MAX_PATH] = L"";
  if (mod) GetModuleFileNameW(mod, modPath, MAX_PATH);
  const wchar_t* const modName = wcsrchr(modPath, L'\\') ? wcsrchr(modPath, L'\\') + 1 : modPath;
  const bool isFloat = _stricmp(type, "FLOAT") == 0;
  if (_stricmp(type, "UBOOL") != 0 && _stricmp(type, "INT") != 0 && !isFloat)
    *why = "the variable is not a UBOOL, INT or FLOAT";
  else if (flags & 1u) *why = "the variable is saved to the game's ini (flags bit 0)";
  else if (!inImage) *why = "the variable's storage is not in a loaded module";
  char shown[24];
  if (isFloat) _snprintf_s(shown, sizeof shown, _TRUNCATE, "%g", (double)cvar_float((int)value));
  else _snprintf_s(shown, sizeof shown, _TRUNCATE, "%u", value);
  np_log("cvar: %s type %s flags 0x%x storage %p (%ls) value %s%s%s", n, type, flags, (const void*)ptr, modName,
         shown, *why ? ": refused, " : "", *why ? *why : "");
  if (*why) return c;
  c.value = ptr;
  c.flags = flags;
  c.isFloat = isFloat ? 1 : 0;
  c.ok = 1;
  return c;
}

void cvar_set(const CvarRef& c, int v) {
  if (c.ok) InterlockedExchange(c.value, (long)v);
}

// Packs entries into lines of about 200 characters.
static void flush_line(char* line, int* len, int* lines) {
  if (!*len) return;
  np_log("cvar: registry %s", line);
  line[0] = 0;
  *len = 0;
  ++*lines;
}

void cvars_log_all(void) {
  static bool done;
  if (done) return;
  done = true;
  const unsigned char* exec = (const unsigned char*)engine_dn_exec();
  if (!exec) { np_log("cvar: registry not found; nothing listed"); return; }
  const void* var = 0;
  if (!read_dword(exec + EXEC_VARS, (unsigned*)&var)) { np_log("cvar: registry %p not readable", exec); return; }
  char line[320];
  int len = 0, lines = 0, n = 0;
  line[0] = 0;
  bool fault = false;
  for (; var && n < LOG_ALL_CAP; n++) {
    unsigned char e[EV_SIZE];
    char name[48], type[16], val[24];
    if (!read_entry(var, e) || !read_wstr((const wchar_t*)field_ptr(e, EV_NAME), name, sizeof name) ||
        !read_wstr((const wchar_t*)field_ptr(e, EV_TYPE), type, sizeof type)) { fault = true; break; }
    unsigned flags, raw;
    memcpy(&flags, e + EV_FLAGS, 4);
    const void* ptr = field_ptr(e, EV_PTR);
    const bool isInt = _stricmp(type, "UBOOL") == 0 || _stricmp(type, "INT") == 0;
    const bool isFloat = _stricmp(type, "FLOAT") == 0;
    if ((isInt || isFloat) && read_dword(ptr, &raw)) {
      float f;
      memcpy(&f, &raw, 4);
      if (isInt) _snprintf_s(val, sizeof val, _TRUNCATE, "%d", (int)raw);
      else _snprintf_s(val, sizeof val, _TRUNCATE, "%g", (double)f);
    } else {
      strcpy_s(val, sizeof val, "-");
    }
    char item[112];
    const int k = _snprintf_s(item, sizeof item, _TRUNCATE, "%s%s(%s%s)=%s", len ? "; " : "", name, type,
                              flags & 1u ? ",saved" : "", val);
    if (k > 0 && len + k > 200) {
      flush_line(line, &len, &lines);
      _snprintf_s(item, sizeof item, _TRUNCATE, "%s(%s%s)=%s", name, type, flags & 1u ? ",saved" : "", val);
    }
    strcat_s(line, sizeof line, item);
    len = (int)strlen(line);
    var = field_ptr(e, EV_NEXT);
  }
  flush_line(line, &len, &lines);
  np_log("cvar: registry %p: %d variable(s) in %d line(s)%s", exec, n, lines,
         fault ? " (stopped: an entry was not readable)" : n >= LOG_ALL_CAP ? " (stopped at the cap)" : "");
}

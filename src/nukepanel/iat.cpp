#include <windows.h>
#include <string.h>
#include "iat.h"
#include "hook.h"
#include "log.h"

void** iat_slot(HMODULE mod, const char* dll, const char* fn) {
  unsigned char* base = (unsigned char*)mod;
  if (!base || !dll || !fn || !hook_readable(base, sizeof(IMAGE_DOS_HEADER))) return 0;
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return 0;
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
  if (!hook_readable(nt, sizeof *nt) || nt->Signature != IMAGE_NT_SIGNATURE) return 0;
  const DWORD size = nt->OptionalHeader.SizeOfImage;
  const IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!dir.VirtualAddress || dir.VirtualAddress >= size || !hook_readable(base, size)) return 0;
  // The whole image is readable (one VirtualQuery walk above); every RVA below is still bounded by its size.
  for (const IMAGE_IMPORT_DESCRIPTOR* d = (const IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress);
       (unsigned char*)(d + 1) <= base + size && d->Name; ++d) {
    if (d->Name >= size || _stricmp((const char*)base + d->Name, dll) != 0) continue;
    const DWORD names = d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk;
    if (names >= size || d->FirstThunk >= size) return 0;
    const IMAGE_THUNK_DATA32* nm = (const IMAGE_THUNK_DATA32*)(base + names);
    IMAGE_THUNK_DATA32* iat = (IMAGE_THUNK_DATA32*)(base + d->FirstThunk);
    for (int i = 0; (unsigned char*)(nm + i + 1) <= base + size && (unsigned char*)(iat + i + 1) <= base + size &&
                    nm[i].u1.AddressOfData; ++i) {
      if (IMAGE_SNAP_BY_ORDINAL32(nm[i].u1.Ordinal) || nm[i].u1.AddressOfData >= size) continue;
      const IMAGE_IMPORT_BY_NAME* ibn = (const IMAGE_IMPORT_BY_NAME*)(base + nm[i].u1.AddressOfData);
      if (strcmp((const char*)ibn->Name, fn) == 0) return (void**)&iat[i].u1.Function;
    }
  }
  return 0;
}

int iat_patch(HMODULE mod, const char* dll, const char* fn, void* hook, void** orig, const char* who) {
  void** slot = iat_slot(mod, dll, fn);
  if (!slot) { np_log("%s: %s!%s: no import slot", who, dll, fn); return 0; }
  // Some modules (MSVCR90) keep their import table in .text: keep the execute bit, or code sharing the page
  // loses it for the duration of the write.
  MEMORY_BASIC_INFORMATION mbi = {};
  const bool exec = VirtualQuery(slot, &mbi, sizeof(mbi)) == sizeof(mbi) &&
                    (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                    PAGE_EXECUTE_WRITECOPY)) != 0;
  DWORD old;
  if (!VirtualProtect(slot, sizeof(void*), exec ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, &old)) {
    np_log("%s: %s!%s: VirtualProtect failed (%lu)", who, dll, fn, GetLastError());
    return 0;
  }
  *orig = *slot;
  InterlockedExchangePointer(slot, hook);
  VirtualProtect(slot, sizeof(void*), old, &old);
  const void* real = (const void*)GetProcAddress(GetModuleHandleA(dll), fn);
  np_log("%s: %s!%s slot %p was %p (%s)", who, dll, fn, (void*)slot, *orig,
         *orig == real ? "the export" : "not the export: a shim or another hook");
  return 1;
}

#pragma once
// Import slots: loader-written data, found by walking a module's import descriptors by name (no offsets).
// duke-dnmp's saveio walk, shared by the cursor gate (input.cpp) and the file-I/O timing.
#include <windows.h>

// The slot `mod` calls dll!fn through, or NULL (no such import, by-ordinal only, or an unreadable image).
void** iat_slot(HMODULE mod, const char* dll, const char* fn);

// Points `mod`'s dll!fn slot at `hook`, saving what was there in *orig first (a shim or another hook may own
// the slot: the hook calls whatever was there). Logged with `who`. Returns 1 when patched.
int iat_patch(HMODULE mod, const char* dll, const char* fn, void* hook, void** orig, const char* who);

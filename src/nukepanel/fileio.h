#pragma once
// File-I/O timing and the slow-open log, through KERNEL32 import slots of Engine.dll, dnCommon.dll and the game's
// MSVCR90.dll (loader-written data; no code byte changes).

// After the stock gate: patches the slots, each one logged.
void fileio_install(void);

// Frame thread, once per frame: the file calls made on the frame thread since the last take (count, total and
// longest in microseconds), then cleared. The first caller's thread is the frame thread; until then no call
// counts toward it (opens on every thread still feed the slow-open log).
void fileio_take(unsigned* calls, unsigned* total_us, unsigned* max_us);

// Session totals: frame-thread file calls (summed by the takes), opens on every thread, slow opens.
struct FileIoTotals { unsigned calls; double total_ms, max_ms; unsigned opens, slow; };
void fileio_totals(FileIoTotals* out);

// DLL_PROCESS_DETACH, when it comes (the game usually exits without one): logs the totals.
void fileio_detach(void);

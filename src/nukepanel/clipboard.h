#pragma once
// The Windows clipboard, and nothing else: AA's clipboard.h/.cpp, same Win32 sequence and error handling. DLL-only,
// never in the suite. Fail-closed: a clipboard another process holds, an absent format or a refused lock answers false
// and changes nothing; every path that opened it closes it. Main thread only: panel.cpp's entry field calls it.

// CF_TEXT into `out`, always NUL-terminated (empty on every failure); `cap` counts the terminator.
// Answers whether anything was read.
bool clipboard_read_text(char* out, int cap);

// `s` onto the clipboard as CF_TEXT. Answers whether it landed.
bool clipboard_write_text(const char* s);

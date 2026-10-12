#pragma once
// dnName lookups (game thread): by string without creating names, and index to string for logs and labels.

// The name's index, or 0 (NAME_None) when the name does not exist or names are unavailable.
int uname_find(const wchar_t* s);
const wchar_t* uname_str(int nameIdx);    // L"" when unknown; takes the engine's NamesCS lock

// Deferred log lines for code running under world.cpp's __try: a fault inside operator* would leave NamesCS held,
// so names are logged as tokens (uname_tok) and turned into strings by uname_dlog_flush, outside the guard.
const char* uname_tok(int nameIdx);       // a %s argument; valid for the next 16 calls
void uname_dlog(const char* fmt, ...);
void uname_dlog_flush(void);

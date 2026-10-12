#pragma once
// D8: every analysis is tied to build 244, so a non-stock binary gets a pass-through proxy and no
// detours. The comparison is the pure rule; stockgate.cpp hashes the files.

struct StockFile { const wchar_t* name; const char* sha256; };
static const StockFile kStockFiles[3] = {
  {L"DukeForever.exe", "577012caf587f2da08ccbbb4a3fe74d148d5186711d2e320e63ed25c8638b98e"},
  {L"Engine.dll",      "9ced8ab2ee3f2106fa8c35112a4b702d5c1222e74db4c46f3f5c201b06351e96"},
  {L"D3DDrv9.dll",     "7d816d67e6193f5b0de40a25e1b9b1eededa8aa6549b62790b71519e289efd0b"},
};

// Case-insensitive hex compare of a 32-byte digest; false unless `hex` is exactly 64 hex digits.
inline bool stock_digest_matches(const unsigned char* d, const char* hex) {
  if (!d || !hex) return false;
  for (int i = 0; i < 64; i++) {
    const char c = hex[i];
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else return false;   // also stops at the terminator of a short string
    const int want = (i & 1) ? (d[i >> 1] & 15) : (d[i >> 1] >> 4);
    if (v != want) return false;
  }
  return hex[64] == 0;
}

// Hashes the three files in the exe's folder once and logs the verdict; later calls return the cache.
bool stock_run(void);
// The cached verdict (false before stock_run).
bool stock_ok(void);

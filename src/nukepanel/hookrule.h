#pragma once
// Pure rules for the inline detour (hook.cpp), header-only so the suite can pin them.
#include <string.h>

// True when the bytes at a target may be patched: they equal the fingerprint, or someone (the Steam
// overlay) already put a 5-byte JMP over the start and the rest of the fingerprint is intact.
inline bool hook_prologue_ok(const unsigned char* cur, const unsigned char* fp, unsigned fpLen,
                             unsigned prologueLen) {
  if (!cur || !fp || fpLen < prologueLen || prologueLen < 5) return false;
  if (memcmp(cur, fp, fpLen) == 0) return true;
  return cur[0] == 0xE9 && memcmp(cur + 5, fp + 5, fpLen - 5) == 0;
}

// Bytes of one ModRM operand (ModRM + SIB + displacement), 32-bit addressing.
inline unsigned hook_modrm_len(const unsigned char* p) {
  const unsigned char m = p[0], mod = m >> 6, rm = m & 7;
  if (mod == 3) return 1;
  unsigned n = 1;
  if (rm == 4) {
    n++;
    if (mod == 0 && (p[1] & 7) == 5) n += 4;
  } else if (mod == 0 && rm == 5) {
    n += 4;
  }
  if (mod == 1) n += 1;
  if (mod == 2) n += 4;
  return n;
}

// Length of whole instructions covering at least 5 bytes, or 0 when an instruction is not one we
// know to be position-independent (relative calls and jumps must never be copied raw).
inline unsigned hook_prologue_len(const unsigned char* p, unsigned avail) {
  unsigned n = 0;
  while (n < 5) {
    if (n + 4 > avail) return 0;   // decoding reads at most prefix, opcode, ModRM and SIB
    const unsigned char* q = p + n;
    unsigned len = 0;
    unsigned pre = 0;
    if (q[0] == 0x64) { pre = 1; q++; }   // fs: (SEH prologues)
    const unsigned char op = q[0];
    if ((op >= 0x50 && op <= 0x5F) || op == 0x90) len = 1;
    else if (op == 0x6A) len = 2;
    else if (op == 0x68 || (op >= 0xB8 && op <= 0xBF) || op == 0xA1 || op == 0xA3) len = 5;
    else if (op == 0x8B || op == 0x89 || op == 0x8D || op == 0x33 || op == 0x31 || op == 0x2B ||
             op == 0x29 || op == 0x03 || op == 0x01 || op == 0x3B || op == 0x39 || op == 0x85 ||
             op == 0x84 || op == 0x0B || op == 0x09 || op == 0x23 || op == 0x21 || op == 0x8A ||
             op == 0x88)
      len = 1 + hook_modrm_len(q + 1);
    else if (op == 0x83 || op == 0x80 || op == 0xC6) len = 2 + hook_modrm_len(q + 1);
    else if (op == 0x81 || op == 0xC7) len = 5 + hook_modrm_len(q + 1);
    else if (op == 0xFF && ((q[1] >> 3) & 7) == 6) len = 1 + hook_modrm_len(q + 1);   // push r/m
    else return 0;
    n += pre + len;
  }
  return n <= avail ? n : 0;
}

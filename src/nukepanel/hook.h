#pragma once
// Inline detour, x86 only. Ported from duke-dnmp's hook.cpp, plus the foreign-JMP case (hookrule.h).

// JMPs `target` to `replacement` only if hook_prologue_ok(target, fp, fpLen, prologueLen) (fpLen >= prologueLen >= 5).
// When the target already starts with someone's JMP, only those 5 bytes are replaced and the trampoline replays that
// JMP. The trampoline is stored to *trampolineOut before the patch goes live. Returns 1 when installed.
int hook_install_at(void* target, const unsigned char* fp, unsigned fpLen,
                    unsigned prologueLen, void* replacement, void** trampolineOut);

// Detours a code address read at run time: fingerprints its current bytes, sizes the prologue by
// whole instructions, refuses (logged with `what`) anything that is neither that nor a JMP.
int hook_install_live(void* target, const char* what, void* replacement, void** trampolineOut);

// Committed, readable memory for [p, p+n): one VirtualQuery walk, for install-time checks only.
bool hook_readable(const void* p, unsigned n);

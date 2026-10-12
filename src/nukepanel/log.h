#pragma once
// Crude on purpose: raw kernel32, no allocation; runs during loader init and from any thread.
// The file is GAME\System\NukePanel\nukepanel.log (our module's folder, created on first write).
void np_log(const char* fmt, ...);

// Off until DllMain confirms the host is DukeForever.exe: any other host must never touch the log.
void np_log_enable(void);

// Pre-terminated lines in one open/write/close, for bursts logged after measured frames.
void np_log_block(const char* text, int len);

// Lines failed to write in full since load (interlocked; read once at detach).
long np_log_dropped(void);

// Write cost since the last call, then cleared: calls, total and longest single call in microseconds.
void np_log_take_write_stats(unsigned* calls, unsigned* total_us, unsigned* max_us);

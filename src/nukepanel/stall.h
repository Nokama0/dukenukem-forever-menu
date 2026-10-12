#pragma once
// The stall watchdog: a thread that samples the frame thread's EIP and EBP chain when a frame runs past 300 ms,
// to name the module behind a hitch. Measurement only.

// Frame thread, last thing on the frame path: publishes "a frame ended now". The first call records the calling
// thread as the one to watch and starts the watchdog, so it never runs before the first frame.
void stall_frame_end(void);

// Frame thread: the samples the watchdog took since the last stall_frame_end, for the hitch line's stall=.
int stall_frame_samples(void);

// Session counters (interlocked; the watchdog is their only writer). stalls is the cap's count.
struct StallTotals { long stalls, longFrames, samples, suspendFails, contextFails, moved; };
void stall_totals(StallTotals* out);

// The watchdog thread's id once it runs, else 0 (interlocked): crashlog stays silent on it, because it may hold the
// frame thread suspended, and logging there could wait on a lock that thread holds.
unsigned long stall_watchdog_tid(void);

// "<module>+0x<offset>" from the watchdog's module table, or the bare address before it is loaded. Any thread.
int stall_name_addr(char* out, int cap, unsigned addr);

// Another thread's stack from the calling thread (d3dcap's render watch): suspend, GetThreadContext, walk up to 16
// EBP frames inside [Esp, stackBase), resume, then one "stall: <tag>: eip .. top .. ; stack(n) .." line. stackBase 0
// logs EIP alone. 1 sampled, -1 SuspendThread failed, -2 GetThreadContext failed (both logged).
int stall_sample_thread(void* thread, unsigned stackBase, const char* tag);

// DLL_PROCESS_DETACH, when it comes (the game usually exits without one): logs the counters, stops the watchdog.
void stall_detach(void);

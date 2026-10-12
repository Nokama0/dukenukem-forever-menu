#pragma once
// Who ends the process and which exceptions came before it: a first vectored exception handler that logs the first
// 16 exceptions (observe only, never handles one), and ExitProcess/TerminateProcess import-slot hooks in the
// game's modules that log the caller and its EBP chain before calling the real function. Measurement only.

// np_first_call, first thing after the stock gate.
void crashlog_install(void);

// DLL_PROCESS_DETACH: the session's exception totals.
void crashlog_detach(void);

// First-call hook point: proxy.cpp calls this once, after the real winmm is resolved, outside the
// loader lock. A non-stock build gets the pass-through proxy and nothing else (D8).
#include <windows.h>
#include <intrin.h>
#include "stockgate.h"
#include "d3dcap.h"
#include "display.h"
#include "frame.h"
#include "fileio.h"
#include "dimouse.h"
#include "crashlog.h"
#include "log.h"

// True when this thread holds the loader lock (any module's DllMain or static constructor is running).
// PEB+0xA0 is LoaderLock on 32-bit Windows; winternl.h's PEB hides the field.
static bool loader_lock_held(void) {
  const unsigned char* peb = (const unsigned char*)__readfsdword(0x30);
  const RTL_CRITICAL_SECTION* cs = *(RTL_CRITICAL_SECTION* const*)(peb + 0xA0);
  return cs && (DWORD)(ULONG_PTR)cs->OwningThread == GetCurrentThreadId();
}

extern "C" void np_first_call(void) {
  frame_note_first_call_tid(GetCurrentThreadId());
  np_log("boot: first call tid=%lu loader_lock=%d", GetCurrentThreadId(), loader_lock_held() ? 1 : 0);
  if (!stock_run()) return;
  crashlog_install();   // before any patch, so a fault in one is seen
  fileio_install();
  display_boot();   // before the device: borderless is decided at launch
  d3dcap_install();
  dimouse_install();
}

#pragma once
// When config_write_changed may try the ini again after a failed write, and which attempts are logged. Without it a
// locked or read-only ini was retried and logged on every frame.

static const unsigned CFGR_BASE_MS = 1000, CFGR_MAX_MS = 30000;
enum { CFGR_LOG_NONE = 0, CFGR_LOG_FIRST = 1, CFGR_LOG_CEILING = 2 };

// fails: failed attempts in the current streak (0: none, write at once). Times are GetTickCount milliseconds.
struct CfgRetry { unsigned fails, delayMs, dueMs; };

inline CfgRetry cfgretry_init(void) { CfgRetry r = {0, 0, 0}; return r; }

// The signed difference keeps the comparison right across GetTickCount's 49-day wrap.
inline bool cfgretry_due(const CfgRetry* r, unsigned nowMs) {
  return r->fails == 0 || (int)(nowMs - r->dueMs) >= 0;
}

// A failed attempt: the next one waits 1 s, doubling to 30 s. Logged are the first failure and the attempt that
// reaches the 30 s ceiling (once per streak, with the count).
inline int cfgretry_on_fail(CfgRetry* r, unsigned nowMs) {
  const unsigned before = r->delayMs;
  r->fails++;
  r->delayMs = r->fails == 1 ? CFGR_BASE_MS : (before >= CFGR_MAX_MS / 2 ? CFGR_MAX_MS : before * 2);
  r->dueMs = nowMs + r->delayMs;
  if (r->fails == 1) return CFGR_LOG_FIRST;
  return (r->delayMs == CFGR_MAX_MS && before < CFGR_MAX_MS) ? CFGR_LOG_CEILING : CFGR_LOG_NONE;
}

// A write that went through: the streak's failure count (0: nothing to log) and the rule starts over.
inline unsigned cfgretry_on_success(CfgRetry* r) {
  const unsigned n = r->fails;
  *r = cfgretry_init();
  return n;
}

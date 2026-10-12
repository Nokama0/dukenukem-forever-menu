// Names through dnCommon's own exports (docs/re/engine-map.md section 2): no read of the name table's data
// without the engine's lock, and the find-only constructor mode, so no name is ever added.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#include "uname.h"
#include "engine.h"
#include "log.h"

int uname_find(const wchar_t* s) {
  engine_exports();
  return engine_name_find(s);
}

const wchar_t* uname_str(int nameIdx) {
  engine_exports();
  const wchar_t* s = engine_name_str(nameIdx);
  return s ? s : L"";
}

static const char TOK_OPEN = '\x01', TOK_CLOSE = '\x02';
static char s_tok[16][16];
static unsigned s_tokNext;

const char* uname_tok(int nameIdx) {
  char* t = s_tok[s_tokNext++ & 15];
  _snprintf(t, 16, "%c%d%c", TOK_OPEN, nameIdx, TOK_CLOSE);
  t[15] = 0;
  return t;
}

static char s_lines[64][384];   // a level's first frame logs every property and faction at once
static int s_lineN, s_dropped;

void uname_dlog(const char* fmt, ...) {
  if (s_lineN >= 64) {
    s_dropped++;
    return;
  }
  char* line = s_lines[s_lineN++];
  va_list ap;
  va_start(ap, fmt);
  _vsnprintf(line, 383, fmt, ap);
  va_end(ap);
  line[383] = 0;
}

void uname_dlog_flush(void) {
  for (int i = 0; i < s_lineN; i++) {
    char out[1024];
    int o = 0;
    for (const char* p = s_lines[i]; *p && o < (int)sizeof out - 1;) {
      const char* close = *p == TOK_OPEN ? strchr(p, TOK_CLOSE) : 0;
      if (!close) {
        out[o++] = *p++;
        continue;
      }
      const int n = WideCharToMultiByte(CP_UTF8, 0, uname_str(atoi(p + 1)), -1, out + o, (int)sizeof out - 1 - o,
                                        NULL, NULL);
      if (n > 0) o += n - 1;   // n counts the terminator
      p = close + 1;
    }
    out[o] = 0;
    np_log("%s", out);
  }
  s_lineN = 0;
  if (s_dropped) {
    np_log("uname: %d deferred log line(s) dropped", s_dropped);
    s_dropped = 0;
  }
}

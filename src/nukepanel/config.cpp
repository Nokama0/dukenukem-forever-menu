// nukepanel.ini: the shape of duke-dnmp's config.cpp (itself AA's). Written key by key with WritePrivateProfileStringW,
// never rewritten whole (a rewrite would drop the player's comments and keys a newer build wrote). Hand-rolled parse: a
// garbage or out-of-range value falls back to its default instead of whatever strtol makes of it.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "paths.h"
#include "config.h"
#include "ini.h"
#include "cfgretry.h"
#include "ui/bind.h"   // the BIND_ROW_* indexes
#include "ui/ui.h"     // UI_SCALE_MIN/MAX/DEFAULT
#include "esp_layout.h"   // ESP_DRAWDIST_MIN/MAX/DEFAULT
#include "fov.h"          // FOV_MAX_DEG, FOV_STOCK
#include "gamespeed.h"    // GS_PCT_MIN/MAX/DEF
#include "aim.h"          // AIM_RANGE_*
#include "tbot.h"         // TBOT_WHEN_* and TBOT_DELAY_MS_*
#include "aimbot.h"       // AIMBOT_SMOOTH_* and AIMBOT_KEY_DEF
#include "photo.h"        // PHOTO_MULT_MIN/MAX/DEF
#include "fblift.h"       // FBLIFT_MIN/MAX/DEF_PCT
#include "achhelp.h"      // AH_MODE_*, ah_mode_parse/text, AR_COUNT

Config g_cfg;
int g_cfgDirty = 0;

// What is on disk, so config_write_changed writes only what moved.
static Config s_onDisk;
static wchar_t s_path[MAX_PATH];
// A failed write waits before the next attempt, so a locked ini is not retried and logged every frame.
static CfgRetry s_retry;

// One table for reader and writer, with each key's legal range and default.
struct Field { const char* section; const char* key; int Config::* member; int lo, hi, def; };
static const Field kFields[] = {
  { "panel",     "key",        &Config::menukey,     1,  254,   0x2D },   // VK_INSERT
  { "panel",     "x",          &Config::panelx,     -1,  65535, -1 },
  { "panel",     "y",          &Config::panely,     -1,  65535, -1 },
  { "panel",     "w",          &Config::panelw,      0,  65535,  0 },
  { "panel",     "h",          &Config::panelh,      0,  65535,  0 },
  { "panel",     "animations", &Config::animations,  0,  1,      1 },
  { "panel",     "uiscale",    &Config::uiScale,     UI_SCALE_MIN, UI_SCALE_MAX, UI_SCALE_DEFAULT },   // AA's key
  { "framerate", "cap",        &Config::frameCapOn,  0,  1,      1 },
  { "framerate", "capfps",     &Config::frameCapFps, 0,  240,    0 },   // 0 = follow the monitor
  { "esp",       "enemies",       &Config::espOn,            0, 1, 1 },
  { "esp",       "boundingboxes", &Config::espBoundingBoxes, 0, 1, 1 },
  { "esp",       "healthbars",    &Config::espHealthBars,    0, 1, 1 },
  { "esp",       "names",         &Config::espNames,         0, 1, 1 },
  { "esp",       "distance",      &Config::espDistance,      0, 1, 1 },
  { "esp",       "allies",        &Config::espAllies,        0, 1, 0 },
  { "esp",       "dmgnumbers",    &Config::dmgNumbers,       0, 1, 1 },
  { "esp",       "drawdist",      &Config::espDrawDist,      ESP_DRAWDIST_MIN, ESP_DRAWDIST_MAX, ESP_DRAWDIST_DEFAULT },
  { "esp",       "nolimit",       &Config::espNoLimit,       0, 1, 0 },   // AA's espdrawdist/espnolimit
  { "esp",       "egoboosts",     &Config::espEgo,           0, 1, 1 },   // default on, as Enemies
  { "esp",       "items",         &Config::espItems,         0, 1, 1 },   // DNMP's keys and defaults
  { "esp",       "hidecanttake",  &Config::espHideTaken,     0, 1, 0 },
  { "esp",       "ammocrates",    &Config::espAmmoCrates,    0, 1, 0 },   // off by default
  { "esp",       "questitems",    &Config::espQuest,         0, 1, 0 },
  { "esp",       "breakables",    &Config::espBreakables,    0, 1, 0 },   // off by default
  { "esp",       "levelstats",    &Config::levelStats,       0, 1, 0 },   // duke-dnmp's key; default off
  { "esp",       "helmets",       &Config::espHelmets,       0, 1, 0 },   // off by default
  { "esp",       "phones",        &Config::espPhones,        0, 1, 0 },
  { "view",      "fov",        &Config::fovOn,       0,  1,      0 },   // duke-dnmp's keys and defaults
  { "view",      "fovdeg",     &Config::fovDeg,      0,  FOV_MAX_DEG, FOV_STOCK },   // 0 = the game's own
  { "game",      "speed",      &Config::gameSpeedPct, GS_PCT_MIN, GS_PCT_MAX, GS_PCT_DEF },
  { "game",      "speedkey",   &Config::gameSpeedKey, 0, 254,     0 },   // unbound
  { "weapons",   "accuracy",   &Config::accuracyOn,  0,  1,      0 },
  { "weapons",   "norecoil",   &Config::recoilOn,    0,  1,      0 },
  { "weapons",   "firerate",   &Config::fireRatePct, 100, 400,  100 },   // DNMP's keys and stops
  { "weapons",   "reload",     &Config::reloadPct,   100, 1100, 100 },   // 1100 = Max
  { "extras",    "bigpackage", &Config::bigPackage,  0,  1,      1 },
  { "display",   "borderless", &Config::borderless,  0,  1,      0 },   // DNMP's key; default off
  { "display",   "keeprunning", &Config::keepRunning, 0, 1,      0 },
  { "postfx",    "blur",            &Config::pfxBlur,            0, 1, 1 },
  { "postfx",    "motionblur",      &Config::pfxMotionBlur,      0, 1, 1 },
  { "postfx",    "pixelmotionblur", &Config::pfxPixelMotionBlur, 0, 1, 1 },
  { "postfx",    "dof",             &Config::pfxDof,             0, 1, 1 },
  { "postfx",    "bloom",           &Config::pfxBloom,           0, 1, 1 },
  { "postfx",    "tonemapping",     &Config::pfxToneMap,         0, 1, 1 },
  { "postfx",    "underwater",      &Config::pfxUnderWater,      0, 1, 1 },
  { "postfx",    "filmgrain",       &Config::pfxFilmGrain,       0, 1, 1 },
  { "postfx",    "pinkovision",     &Config::pfxPinkOVision,     0, 1, 1 },
  { "input",     "rawmouse",   &Config::rawMouse,    0,  1,      0 },
  { "view",      "noshake",    &Config::noShake,     0,  1,      0 },
  { "view",      "cleandukevision", &Config::cleanDukeVision, 0, 1, 0 },   // off by default
  { "autowin",   "qte",        &Config::autoQte,     0,  1,      0 },   // off by default
  { "autowin",   "minigames",  &Config::autoMinigames, 0, 1,     0 },
  { "autowin",   "pinball",    &Config::pinballAuto, 0,  1,      0 },   // off by default
  { "autowin",   "arcade",     &Config::arcadeAuto,  0,  1,      0 },   // off by default
  { "autowin",   "execute",    &Config::autoExecute, 0,  1,      0 },   // off by default
  { "aim",       "silent",     &Config::silentOn,    0,  1,      0 },   // DNMP's keys
  { "aim",       "point",      &Config::aimPoint,    AIM_AT_BODY, AIM_AT_HEAD, AIM_AT_BODY },
  { "aim",       "range",      &Config::aimRangeM,   AIM_RANGE_MIN_M, AIM_RANGE_MAX_M, AIM_RANGE_MAX_M },
  { "aim",       "offscreen",  &Config::aimOffscreen, 0, 1,      1 },
  { "aim",       "lead",       &Config::aimLead,     0,  1,      1 },   // DNMP's sub-row default
  { "aim",       "marker",     &Config::aimMarker,   0,  1,      0 },
  { "aim",       "triggerbot", &Config::tbotOn,      0,  1,      0 },
  { "aim",       "triggerwhen", &Config::tbotWhen,   TBOT_WHEN_CROSSHAIR, TBOT_WHEN_AIMBOT, TBOT_WHEN_CROSSHAIR },
  { "aim",       "triggerdelay", &Config::tbotDelay, TBOT_DELAY_MS_MIN, TBOT_DELAY_MS_MAX, TBOT_DELAY_MS_DEF },
  { "aim",       "aimon",      &Config::aimbotOn,    0,  1,      0 },   // AA's keys and defaults
  { "aim",       "aimhold",    &Config::aimHold,     0,  1,      1 },
  { "aim",       "bindaim",    &Config::aimbotKey,   0,  254,    AIMBOT_KEY_DEF },
  { "aim",       "aimsmooth",  &Config::aimSmooth,   AIMBOT_SMOOTH_MIN, AIMBOT_SMOOTH_MAX, AIMBOT_SMOOTH_DEF },
  { "aim",       "aimtarget",  &Config::aimTarget,   AIM_PICK_CROSSHAIR, AIM_PICK_NEAREST, AIM_PICK_CROSSHAIR },
  { "aim",       "aimcone",    &Config::aimCone,     AIM_CONE_MIN, AIM_CONE_MAX, AIM_CONE_DEF },
  { "aim",       "debug_double_trace", &Config::aimDebugDoubleTrace, 0, 1, 0 },   // hidden: aim.cpp trace_point
  { "freecam",   "on",         &Config::freecamOn,   0,  1,      1 },   // DNMP's keys and defaults
  { "freecam",   "key",        &Config::freecamKey,  0,  254,    0x56 },   // V
  { "freecam",   "speedpct",   &Config::freecamSpeedPct, FREECAM_SPEED_MIN_PCT, FREECAM_SPEED_MAX_PCT, FREECAM_SPEED_DEF_PCT },
  { "freecam",   "pause",      &Config::freecamPause, 0, 1,      0 },
  { "freecam",   "marker",     &Config::freecamMarker, 0, 1,     1 },
  { "freecam",   "debug",      &Config::freecamDebug, 0, 1,      0 },
  { "freecam",   "shot",       &Config::freecamShot, 0,  1,      0 },
  { "freecam",   "copy",       &Config::camCopyKey,  0,  254,    0x42 },   // B (V is the Camera key)
  { "freecam",   "paste",      &Config::camPasteKey, 0,  254,    0x4E },   // N
  { "freecam",   "teleport",   &Config::teleportKey, 0,  254,    0x54 },   // T
  { "freecam",   "teleportmarker", &Config::teleportMarker, 0, 1, 1 },
  { "freecam",   "photo",      &Config::photoKey,    0,  254,    0x7B },   // F12
  { "freecam",   "photosize",  &Config::photoMult,   PHOTO_MULT_MIN, PHOTO_MULT_MAX, PHOTO_MULT_DEF },
  { "freecam",   "nofog",      &Config::freecamNoFog, 0, 1,      0 },
  { "freecam",   "fullbright", &Config::freecamBright, 0, 1,     0 },
  { "freecam",   "fblift",     &Config::freecamLift, FBLIFT_MIN_PCT, FBLIFT_MAX_PCT, FBLIFT_DEF_PCT },
  { "freecam",   "noculling",  &Config::freecamNoCull, 0, 1,     0 },
  { "freecam",   "fullbright_net_delay", &Config::freecamNetDelay, 1, 600, 3 },    // hidden: FREECAM_STUCK_FRAMES
};
static const int kFieldCount = (int)(sizeof(kFields) / sizeof(kFields[0]));

// DNMP's string table: UTF-8 values, "" by default.
struct StrField { const char* section; const char* key; char (Config::*member)[CONFIG_STR_CAP]; };
static const StrField kStrFields[] = {
  { "freecam", "photofolder", &Config::photoFolder },   // "" = <Pictures>\Nuke Panel (photofolder.h)
};
static const int kStrFieldCount = (int)(sizeof(kStrFields) / sizeof(kStrFields[0]));

// The achievement switches: one word-valued key per id, not a kFields row (a table of 64 would only repeat the id).
static_assert(sizeof(((Config*)0)->achEsp) / sizeof(int) == AR_COUNT, "one switch per achievement id");
static const char kAchSection[] = "achievements";
// The id of "ach.esp.<id>", or -1.
static int ach_key_id(const char* key) {
  if (_strnicmp(key, "ach.esp.", 8) != 0) return -1;
  // Base 10 on purpose: ini_parse_int reads base 0, so "010" would be octal 8 and "08" refused.
  const char* d = key + 8;
  if (*d < '0' || *d > '9') return -1;
  char* end = NULL;
  const long id = strtol(d, &end, 10);
  return *end == 0 && id >= 0 && id < AR_COUNT ? (int)id : -1;
}

void config_defaults(void) {
  for (int i = 0; i < kFieldCount; i++) g_cfg.*(kFields[i].member) = kFields[i].def;
  for (int i = 0; i < kStrFieldCount; i++) (g_cfg.*(kStrFields[i].member))[0] = 0;
  for (int id = 0; id < AR_COUNT; id++) g_cfg.achEsp[id] = AH_MODE_AUTO;
}

bool config_key_known(const char* section, const char* key) {
  if (!section || !key) return false;
  for (int i = 0; i < kFieldCount; i++)
    if (_stricmp(section, kFields[i].section) == 0 && _stricmp(key, kFields[i].key) == 0) return true;
  for (int i = 0; i < kStrFieldCount; i++)
    if (_stricmp(section, kStrFields[i].section) == 0 && _stricmp(key, kStrFields[i].key) == 0) return true;
  return _stricmp(section, kAchSection) == 0 && ach_key_id(key) >= 0;
}

// ASCII only: every section and key above is, and the values are numbers.
static void widen(const char* s, wchar_t* out, int cap) {
  int i = 0;
  for (; s[i] && i < cap - 1; i++) out[i] = (wchar_t)(unsigned char)s[i];
  out[i] = 0;
}

static bool load_wide(const wchar_t* path) {
  config_defaults();
  g_cfgDirty = 0;
  s_retry = cfgretry_init();
  wcscpy_s(s_path, MAX_PATH, path ? path : L"");
  s_onDisk = g_cfg;
  FILE* f = NULL;
  if (!s_path[0] || _wfopen_s(&f, s_path, L"r") != 0 || !f) {
    np_log("config: no %ls, defaults", s_path[0] ? s_path : L"path");
    return false;   // no file yet is the normal first run
  }
  char line[INI_LINE_MAX], section[INI_SECTION_MAX] = "";
  int read = 0, refused = 0, lineNo = 0;
  for (;;) {
    line[INI_LINE_MAX - 1] = 1;   // stays 1 unless fgets filled the buffer (a NUL in the text cannot fool it)
    if (!fgets(line, sizeof line, f)) break;
    lineNo++;
    // A full chunk without '\n' is the whole line only when the file or the line ends right there; a longer line is
    // skipped to its end, never cut into a second line whose tail could read as another key (ini_peek_int agrees).
    if (line[INI_LINE_MAX - 1] == 0 && line[INI_LINE_MAX - 2] != '\n') {
      int c = fgetc(f);
      if (c != EOF && c != '\n') {
        while (c != EOF && c != '\n') c = fgetc(f);
        refused++;
        np_log("config: line %d is over %d bytes; skipped", lineNo, INI_LINE_MAX - 1);
        continue;
      }
    }
    const char *key = NULL, *val = NULL;
    if (!ini_line(line, section, sizeof section, &key, &val)) continue;
    // An unknown key is left alone: it may belong to a newer build.
    for (int i = 0; i < kFieldCount; i++) {
      if (_stricmp(section, kFields[i].section) != 0 || _stricmp(key, kFields[i].key) != 0) continue;
      int v;
      if (ini_parse_int(val, kFields[i].lo, kFields[i].hi, &v)) { g_cfg.*(kFields[i].member) = v; read++; }
      else { refused++; np_log("config: [%s] %s=\"%s\" is not %d..%d; default %d", section, key, val,
                                kFields[i].lo, kFields[i].hi, kFields[i].def); }
      break;
    }
    for (int i = 0; i < kStrFieldCount; i++) {
      if (_stricmp(section, kStrFields[i].section) != 0 || _stricmp(key, kStrFields[i].key) != 0) continue;
      if (ini_parse_str(val, g_cfg.*(kStrFields[i].member), CONFIG_STR_CAP)) read++;
      else { refused++; np_log("config: [%s] %s is %u bytes, over %d; default \"\"", section, key,
                                (unsigned)strlen(val), CONFIG_STR_CAP - 1); }
      break;
    }
    const int ach = _stricmp(section, kAchSection) == 0 ? ach_key_id(key) : -1;
    if (ach >= 0) {
      if (ah_mode_parse(val, &g_cfg.achEsp[ach])) read++;
      else { refused++; np_log("config: [%s] %s=\"%s\" is not auto, on or off; default auto", section, key, val); }
    }
  }
  fclose(f);
  s_onDisk = g_cfg;
  np_log("config: read %d key(s), refused %d, from %ls", read, refused, s_path);
  return true;
}

bool config_load_from(const char* path) {
  wchar_t w[MAX_PATH] = L"";
  if (path && !MultiByteToWideChar(CP_ACP, 0, path, -1, w, MAX_PATH)) w[0] = 0;
  return load_wide(w);
}

bool config_load(void) {
  wchar_t dir[MAX_PATH], path[MAX_PATH];
  if (!np_our_dir(dir, MAX_PATH) || wcslen(dir) + 14 > MAX_PATH) { load_wide(NULL); return false; }
  wcscpy_s(path, MAX_PATH, dir);
  wcscat_s(path, MAX_PATH, L"nukepanel.ini");
  return load_wide(path);
}

void config_write_changed(void) {
  if (!g_cfgDirty) return;
  const unsigned tick = GetTickCount();
  if (!cfgretry_due(&s_retry, tick)) return;   // stays dirty: written when the wait is over
  g_cfgDirty = 0;
  if (!s_path[0]) return;
  int wrote = 0, failed = 0;
  const char *failSec = 0, *failKey = 0;   // the first key that failed
  DWORD firstErr = 0;
  for (int i = 0; i < kFieldCount; i++) {
    const int now = g_cfg.*(kFields[i].member);
    if (now == s_onDisk.*(kFields[i].member)) continue;
    wchar_t sec[INI_SECTION_MAX], key[INI_SECTION_MAX], buf[16];
    widen(kFields[i].section, sec, INI_SECTION_MAX);
    widen(kFields[i].key, key, INI_SECTION_MAX);
    _snwprintf_s(buf, 16, _TRUNCATE, L"%d", now);
    if (WritePrivateProfileStringW(sec, key, buf, s_path)) {
      s_onDisk.*(kFields[i].member) = now;
      wrote++;
    } else {
      if (!failKey) { failSec = kFields[i].section; failKey = kFields[i].key; firstErr = GetLastError(); }
      failed++;
    }
  }
  // The UTF-8 bytes through the A call unconverted, as DNMP writes them (a single-byte code page passes them through
  // and the loader reads bytes); the ini path is ours, under the game's System folder.
  char pathA[MAX_PATH];
  BOOL lossy = FALSE;
  const bool pathOk = WideCharToMultiByte(CP_ACP, 0, s_path, -1, pathA, MAX_PATH, NULL, &lossy) > 0 && !lossy;
  for (int i = 0; i < kStrFieldCount; i++) {
    const char* now = g_cfg.*(kStrFields[i].member);
    char* disk = s_onDisk.*(kStrFields[i].member);
    if (strcmp(now, disk) == 0) continue;
    if (pathOk && WritePrivateProfileStringA(kStrFields[i].section, kStrFields[i].key, now, pathA)) {
      strcpy_s(disk, CONFIG_STR_CAP, now);
      wrote++;
    } else {
      if (!pathOk) {   // logged once: an ini path outside the code page will not become writable
        static int s_pathLogged;
        if (!s_pathLogged++) np_log("config: [%s] %s not written: the ini path is not in the ANSI code page",
                                    kStrFields[i].section, kStrFields[i].key);
        continue;
      }
      if (!failKey) { failSec = kStrFields[i].section; failKey = kStrFields[i].key; firstErr = GetLastError(); }
      failed++;
    }
  }
  for (int id = 0; id < AR_COUNT && pathOk; id++) {
    if (g_cfg.achEsp[id] == s_onDisk.achEsp[id]) continue;
    char key[INI_SECTION_MAX];
    _snprintf_s(key, sizeof key, _TRUNCATE, "ach.esp.%d", id);
    if (WritePrivateProfileStringA(kAchSection, key, ah_mode_text(g_cfg.achEsp[id]), pathA)) {
      s_onDisk.achEsp[id] = g_cfg.achEsp[id];
      wrote++;
    } else {
      if (!failKey) { failSec = kAchSection; failKey = "ach.esp"; firstErr = GetLastError(); }
      failed++;
    }
  }
  if (failed) {
    // Stays dirty in memory, so the panel still behaves as the player set it.
    g_cfgDirty = 1;
    const int act = cfgretry_on_fail(&s_retry, tick);
    if (act == CFGR_LOG_FIRST)
      np_log("config: could not write [%s] %s (%lu), %d key(s) in all; retrying in %u ms, doubling to %u",
             failSec, failKey, firstErr, failed, s_retry.delayMs,
             CFGR_MAX_MS);
    else if (act == CFGR_LOG_CEILING)
      np_log("config: still cannot write [%s] %s (%lu) after %u attempts; retrying every %u ms",
             failSec, failKey, firstErr, s_retry.fails, CFGR_MAX_MS);
  } else {
    const unsigned streak = cfgretry_on_success(&s_retry);
    if (streak) np_log("config: written after %u failed attempt(s)", streak);
  }
  if (wrote) np_log("config: wrote %d changed key(s)", wrote);
}

static const Field* borderless_field(void) {
  for (int i = 0; i < kFieldCount; i++)
    if (kFields[i].member == &Config::borderless) return &kFields[i];
  return NULL;
}

int config_peek_borderless_text(const char* text, int n) {
  const Field* fd = borderless_field();
  if (!fd || !text || n < 0) return -1;
  return ini_peek_int(text, n, fd->section, fd->key, fd->lo, fd->hi, fd->def);
}

int config_peek_borderless(void) {
  const Field* fd = borderless_field();
  wchar_t dir[MAX_PATH], path[MAX_PATH];
  if (!fd || !np_our_dir(dir, MAX_PATH) || wcslen(dir) + 14 > MAX_PATH) return -1;
  wcscpy_s(path, MAX_PATH, dir);
  wcscat_s(path, MAX_PATH, L"nukepanel.ini");
  const HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    const DWORD e = GetLastError();
    // No file (or no folder yet) is the first run: the default, as config_load gives it.
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return fd->def;
    np_log("config: peek: nukepanel.ini not opened (%lu)", e);
    return -1;
  }
  static char buf[65536];
  DWORD got = 0;
  const BOOL ok = ReadFile(h, buf, sizeof buf, &got, NULL);
  const DWORD size = GetFileSize(h, NULL);
  CloseHandle(h);
  if (!ok || size == INVALID_FILE_SIZE || size > got) {
    np_log("config: peek: nukepanel.ini not read whole (%lu of %lu bytes)", got, size);
    return -1;
  }
  return config_peek_borderless_text(buf, (int)got);
}

// Each bind.h row's field, indexed by row, so bind.h's appended-rows rule holds.
static int Config::* const kBindFields[BIND_ROW_COUNT] = { &Config::menukey, &Config::gameSpeedKey,
                                                           &Config::aimbotKey, &Config::freecamKey,
                                                           &Config::camCopyKey, &Config::camPasteKey,
                                                           &Config::teleportKey, &Config::photoKey };

int config_bind_get(int row) {
  if (row < 0 || row >= BIND_ROW_COUNT || !kBindFields[row]) return 0;
  return g_cfg.*(kBindFields[row]);
}

void config_bind_set(int row, int vk) {
  if (row < 0 || row >= BIND_ROW_COUNT || !kBindFields[row]) return;
  g_cfg.*(kBindFields[row]) = vk;
  g_cfgDirty = 1;
}

#pragma once
// Steam achievement backup and reset (docs/re/steam-stats.md). The rules here are pure; achreset.cpp is the
// Steam and file glue, on the game thread except for one worker that writes the file.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

const int AR_COUNT = 64;
const unsigned AR_CONFIRM_MS = 5000;   // the armed reset row lapses after this
const int AR_NAME_CAP = 128;           // the game's own display-name buffer is 128 bytes (steam-stats.md section 3)

// The game's API names in its id order, copied from Engine.dll!0x10649f44 (stride 400, build 244).
// ISteamUserStats009 has no GetNumAchievements/GetAchievementName, so this table is the only list.
inline const char* ar_name(int id) {
  static const char* const kNames[AR_COUNT] = {
    "ACH_SomeKills", "ACH_MoreKills", "ACH_ManyKills", "ACH_StompKills", "ACH_Executions", "ACH_TripMineKills",
    "ACH_FrozenKills", "ACH_MonsterTruckKills", "ACH_ForkliftKills", "ACH_ExplosiveKills", "ACH_PhysicsImpactKills",
    "ACH_SteroidKills", "ACH_FoodEaten", "ACH_HeadShotKills", "ACH_SubstanceAbuse", "ACH_TripleKill",
    "ACH_KnockedDown", "ACH_PooGrabbed", "ACH_JizzedOn", "ACH_AlienFighterKills", "ACH_CalendarFullyViewed",
    "ACH_TripmineStuck", "ACH_GunskipKill", "ACH_AirHockeyWon", "ACH_PinballScore", "ACH_BeerImbibed",
    "ACH_SteroidsEaten", "ACH_HolodukeActivated", "ACH_WhiteboardDrawing", "ACH_MaxEgoAchieved",
    "ACH_GoldPistolCompletion", "ACH_EasyCompleted", "ACH_NormalCompleted", "ACH_HardCompleted",
    "ACH_ExpertCompleted", "ACH_CreditsViewed", "ACH_HelmetsDiscovered", "ACH_PhoneMessages", "ACH_DrankAllBeer",
    "ACH_CatfishKilled", "ACH_BlimpDestroyed", "ACH_TalentKnockedOut", "ACH_AliensAborted", "ACH_CompanionBarrel",
    "ACH_CycloidDefeated0", "ACH_BattlelordDefeated0", "ACH_AlienQueenDefeated", "ACH_BattlelordDefeated1",
    "ACH_OctakingDefeated", "ACH_LeechDefeated", "ACH_BubbleBuster", "ACH_Threesome", "ACH_PassiveAggressive",
    "ACH_MoonBuggyKills", "ACH_SciencePinballScore", "ACH_SingKaraoke", "ACH_DefeatDrProton",
    "ACH_EasyCompletedDLC03", "ACH_NormalCompletedDLC03", "ACH_HardCompletedDLC03", "ACH_ExpertCompletedDLC03",
    "ACH_MaxEgoAchievedDLC03", "ACH_Chauvinist", "ACH_Deprecated2",
  };
  return id >= 0 && id < AR_COUNT ? kNames[id] : nullptr;
}

// --- The two-click reset -----------------------------------------------------------------------------------------
// Review 2026-10-08: a confirm closer than this to the arm is a double-click, not a decision.
const unsigned AR_CONFIRM_MIN_MS = 400;
// Review 2026-10-08: a confirmed reset whose backup lands later than this is dropped, not run late.
const unsigned AR_RESET_TTL_MS = 10000;
enum { AR_ACT_IGNORED = 0, AR_ACT_ARM = 1, AR_ACT_CONFIRM = 2 };
struct ArConfirm { int armed; unsigned armedAt; };

inline ArConfirm ar_confirm_init() { ArConfirm c = { 0, 0 }; return c; }
// An unsigned age, so GetTickCount's wrap is harmless.
inline bool ar_armed(const ArConfirm& c, unsigned now) { return c.armed && now - c.armedAt < AR_CONFIRM_MS; }
// A click on the reset row: confirms while armed (ignored inside AR_CONFIRM_MIN_MS of the arm), otherwise arms.
inline int ar_click(ArConfirm* c, unsigned now) {
  if (ar_armed(*c, now)) {
    if (now - c->armedAt < AR_CONFIRM_MIN_MS) return AR_ACT_IGNORED;
    c->armed = 0;
    return AR_ACT_CONFIRM;
  }
  c->armed = 1;
  c->armedAt = now;
  return AR_ACT_ARM;
}
// Once a frame, before the panel draws: a frame that did not show the armed row on screen (panel closed, another
// tab, scrolled out) disarms. True when a live arm was dropped, for the log.
inline bool ar_frame_check(ArConfirm* c, int rowShownLastFrame, unsigned now) {
  if (!c->armed || rowShownLastFrame) return false;
  const bool live = ar_armed(*c, now);
  c->armed = 0;
  return live;
}

// The reset runs only after the backup file was written, and only for a confirmed reset (never for a plain backup).
inline bool ar_reset_go(int resetPending, int backupWritten) { return resetPending && backupWritten; }
inline bool ar_reset_expired(unsigned confirmAt, unsigned now) { return now - confirmAt >= AR_RESET_TTL_MS; }
// ar_reset_go, and still inside AR_RESET_TTL_MS of the confirm.
inline bool ar_reset_now(int resetPending, int backupWritten, unsigned confirmAt, unsigned now) {
  return ar_reset_go(resetPending, backupWritten) && !ar_reset_expired(confirmAt, now);
}

// --- Times ---------------------------------------------------------------------------------------------------------
// A local civil time and its offset from UTC in minutes.
struct ArTime { int y, mo, d, h, mi, s, offMin; };

// Unix seconds shifted by the offset, to civil time (Hinnant's civil_from_days).
inline ArTime ar_time_from_unix(int64_t secs, int offMin) {
  int64_t t = secs + (int64_t)offMin * 60;
  int64_t days = t >= 0 ? t / 86400 : -((-t + 86399) / 86400);
  int64_t rem = t - days * 86400;
  ArTime r;
  r.h = (int)(rem / 3600); r.mi = (int)(rem % 3600 / 60); r.s = (int)(rem % 60);
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const int64_t doe = days - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  r.d = (int)(doy - (153 * mp + 2) / 5 + 1);
  r.mo = (int)(mp < 10 ? mp + 3 : mp - 9);
  r.y = (int)(yoe + era * 400 + (r.mo <= 2 ? 1 : 0));
  r.offMin = offMin;
  return r;
}

// "2011-04-13 23:55:31 UTC+03:00"
inline void ar_time_text(const ArTime& t, char* out, int cap) {
  const int a = t.offMin < 0 ? -t.offMin : t.offMin;
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "%04d-%02d-%02d %02d:%02d:%02d UTC%c%02d:%02d", t.y, t.mo, t.d, t.h, t.mi,
              t.s, t.offMin < 0 ? '-' : '+', a / 60, a % 60);
}

// "achievements-backup-2026-10-08_09-05-07.txt"; n > 1 adds "-n" for a second file in the same second.
inline void ar_file_name(const ArTime& t, int n, char* out, int cap) {
  char suffix[8] = "";
  if (n > 1) _snprintf_s(suffix, sizeof suffix, _TRUNCATE, "-%d", n);
  _snprintf_s(out, (size_t)cap, _TRUNCATE, "achievements-backup-%04d-%02d-%02d_%02d-%02d-%02d%s.txt", t.y, t.mo, t.d,
              t.h, t.mi, t.s, suffix);
}

// A display name from Steam (UTF-8) made safe for one line: control characters become spaces, a missing name is
// "(no name)", and a cut to fit `cap` never leaves half of a multi-byte character.
inline void ar_clean(const char* src, char* out, int cap) {
  if (cap <= 0) return;
  if (!src || !*src) { strncpy_s(out, (size_t)cap, "(no name)", _TRUNCATE); return; }
  int n = 0;
  while (src[n] && n < cap - 1) {
    const unsigned char c = (unsigned char)src[n];
    out[n] = c < 0x20 || c == 0x7F ? ' ' : (char)c;
    ++n;
  }
  if (src[n]) {   // cut: drop a trailing partial sequence
    int k = n;
    while (k > 0 && ((unsigned char)out[k - 1] & 0xC0) == 0x80) --k;
    if (k > 0 && ((unsigned char)out[k - 1] & 0xC0) == 0xC0) {
      const unsigned char lead = (unsigned char)out[k - 1];
      const int want = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
      if (n - (k - 1) < want) n = k - 1;
    }
  }
  out[n] = 0;
}

// --- The backup ----------------------------------------------------------------------------------------------------
// One achievement as Steam answered it. `unlock` is Unix seconds (0: Steam has no time); `offMin` is the local
// offset at that moment (DST), filled by the glue.
struct ArEntry { int id; const char* api; char name[AR_NAME_CAP]; int answered, achieved; uint32_t unlock; int offMin; };
enum { AR_SEC_UNLOCKED, AR_SEC_LOCKED, AR_SEC_UNANSWERED };

inline int ar_section_of(const ArEntry& e) {
  return !e.answered ? AR_SEC_UNANSWERED : e.achieved ? AR_SEC_UNLOCKED : AR_SEC_LOCKED;
}
// Unlocked: oldest first, unknown times last, ties by id. The other sections: id order.
inline bool ar_before(const ArEntry& a, const ArEntry& b) {
  if (ar_section_of(a) == AR_SEC_UNLOCKED && a.unlock != b.unlock) {
    if (!a.unlock || !b.unlock) return a.unlock != 0;
    return a.unlock < b.unlock;
  }
  return a.id < b.id;
}
// The indices of one section's entries in print order (an insertion sort: 64 entries at most). Returns the count.
inline int ar_section(const ArEntry* e, int n, int section, int* idx, int cap) {
  int k = 0;
  for (int i = 0; i < n && k < cap; ++i) {
    if (ar_section_of(e[i]) != section) continue;
    int j = k++;
    while (j > 0 && ar_before(e[i], e[idx[j - 1]])) { idx[j] = idx[j - 1]; --j; }
    idx[j] = i;
  }
  return k;
}

// The file's text, CRLF lines. Returns its length, or -1 when it does not fit `cap` (never a cut file).
inline int ar_backup_text(const ArEntry* e, int n, const ArTime& gen, char* out, int cap) {
  int len = 0;
  bool fits = true;
  auto put = [&](const char* fmt, auto... args) {
    if (!fits) return;
    const int w = _snprintf_s(out + len, (size_t)(cap - len), _TRUNCATE, fmt, args...);
    if (w < 0) fits = false; else len += w;
  };
  if (cap <= 0) return -1;
  out[0] = 0;
  int idx[AR_COUNT];
  const int m = n < AR_COUNT ? n : AR_COUNT;
  const int nu = ar_section(e, m, AR_SEC_UNLOCKED, idx, AR_COUNT);
  const int nl = ar_section(e, m, AR_SEC_LOCKED, idx, AR_COUNT);
  const int na = ar_section(e, m, AR_SEC_UNANSWERED, idx, AR_COUNT);
  char t[48];
  ar_time_text(gen, t, sizeof t);
  put("Nuke Panel achievement backup\r\nGenerated: %s\r\nGame: Duke Nukem Forever (Steam app 57900)\r\n", t);
  put("Achievements: %d in the game's list; %d unlocked, %d locked, %d not answered by Steam\r\n", m, nu, nl, na);
  put("\r\nUNLOCKED (%d), oldest first: API name | name | unlocked (local time)\r\n", nu);
  int k = ar_section(e, m, AR_SEC_UNLOCKED, idx, AR_COUNT);
  for (int i = 0; i < k; ++i) {
    const ArEntry& a = e[idx[i]];
    if (a.unlock) ar_time_text(ar_time_from_unix((int64_t)a.unlock, a.offMin), t, sizeof t);
    put("%s | %s | %s\r\n", a.api, a.name, a.unlock ? t : "time unknown");
  }
  if (!k) put("(none)\r\n");
  put("\r\nLOCKED (%d): API name | name\r\n", nl);
  k = ar_section(e, m, AR_SEC_LOCKED, idx, AR_COUNT);
  for (int i = 0; i < k; ++i) put("%s | %s\r\n", e[idx[i]].api, e[idx[i]].name);
  if (!k) put("(none)\r\n");
  put("\r\nNOT ANSWERED BY STEAM (%d): API name\r\n", na);
  k = ar_section(e, m, AR_SEC_UNANSWERED, idx, AR_COUNT);
  for (int i = 0; i < k; ++i) put("%s\r\n", e[idx[i]].api);
  if (!k) put("(none)\r\n");
  return fits ? len : -1;
}

// --- The glue (achreset.cpp; the Settings rows call these on the game thread) -------------------------------------
// Why the rows cannot act (steam_api.dll, the ISteamUserStats009 interface or its table missing), else null.
const char* achreset_refused(void);
void achreset_backup_click(void);
void achreset_reset_click(void);   // arms, or within AR_CONFIRM_MS of the arm, backs up then resets
bool achreset_armed(void);
void achreset_row_shown(void);     // the reset row drew fully inside the open panel's body this frame
bool achreset_busy(void);          // a backup is being written
const char* achreset_note(void);   // the last backup's file name or the last failure, else null
void achreset_frame(void);         // each frame: takes the worker's result and runs a confirmed reset
// The Achievements tab's reads (achhelp.cpp), game thread. One id's state: 1 answered, 0 Steam has none, -1 refused
// or faulted. unlock is Unix seconds, 0 when Steam keeps no time.
int achreset_state(int id, int* achieved, uint32_t* unlock);
// A display attribute ("name", "desc") of one id, ar_clean'd; false when refused, faulted or empty.
bool achreset_attr(int id, const char* key, char* out, int cap);
// The local offset from UTC in minutes at that Unix time (its DST rule).
int achreset_offset_at(uint32_t unix);

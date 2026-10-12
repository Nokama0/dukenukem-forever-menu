// Game speed through PlayerPawn.DebugSlomo: a transient property the engine itself writes; UpdateGameSpeed turns it
// into TimeDilation each tick, so ProcessEvent runs once after each write. No path reaches SaveConfig. Audio hook:
// MilesAudio.dll's import slot for AIL_set_sample_playback_rate (0x100104C8 in build 244), on Miles' thread.
#include <windows.h>
#include <string.h>
#include "gamespeed.h"
#include "optrestore.h"
#include "scall.h"
#include "world.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "config.h"
#include "input.h"
#include "iat.h"
#include "log.h"
#include "mpgate.h"

int g_gameSpeedOn;

// The factor the audio divides by: our DebugSlomo while it is the value in place, else 1.0. Float bits.
static volatile LONG s_factorBits = 0x3F800000;
static const char* const MSS_IMPORT = "_AIL_set_sample_playback_rate@8";
static void* s_rateOrig;
static const char* s_milesBase;
static int s_audioTried;
// Per 600 frames, from any thread: calls per site, divided calls, clamps, ignore-flag skips, the rates in and out.
static volatile LONG s_siteCalls[GS_SITE_COUNT], s_divided, s_clamps, s_ignored, s_faultsAudio;
static volatile LONG s_inMin = 0x7FFFFFFF, s_inMax = -1, s_outMin = 0x7FFFFFFF, s_outMax = -1;
static unsigned s_audioFrames;

static void lmin(volatile LONG* p, LONG v) {
  for (LONG o = *p; v < o;) {
    const LONG prev = InterlockedCompareExchange(p, v, o);
    if (prev == o) break;
    o = prev;
  }
}
static void lmax(volatile LONG* p, LONG v) {
  for (LONG o = *p; v > o;) {
    const LONG prev = InterlockedCompareExchange(p, v, o);
    if (prev == o) break;
    o = prev;
  }
}

static int av_only_audio(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

// Whether the caller's sound (Sample_UpdateParams: flags +0x20 bit 0x80000, bIgnoreTimeDilation) or stream
// (UpdateStream: +0x2C, IgnoreTimeDilation) got dilation 1.0. The caller read these same fields just before.
static int ignores_dilation(int site, const unsigned char* esi) {
  unsigned v = 0;
  __try {
    if (site == GS_SITE_SAMPLE) v = *(const unsigned*)(esi + 0x20) & 0x80000u;
    else if (site == GS_SITE_STREAM) v = *(const unsigned*)(esi + 0x2C);
  } __except (av_only_audio(GetExceptionCode())) {
    InterlockedIncrement(&s_faultsAudio);
    return 1;   // unknown: leave the rate as the game set it
  }
  return v != 0;
}

static int __stdcall rate_adjust(const char* ret, const unsigned char* esi, int rate) {
  const LONG bits = s_factorBits;
  float f;
  memcpy(&f, &bits, 4);
  const int site = gs_rate_site((unsigned)(ret - s_milesBase));
  InterlockedIncrement(&s_siteCalls[site]);
  if (f == 1.f || !gs_rate_divides(site, 0)) return rate;
  if (ignores_dilation(site, esi)) {
    InterlockedIncrement(&s_ignored);
    return rate;
  }
  int clamped = 0;
  const int out = gs_audio_rate(rate, f, &clamped);
  InterlockedIncrement(&s_divided);
  if (clamped) InterlockedIncrement(&s_clamps);
  lmin(&s_inMin, rate);
  lmax(&s_inMax, rate);
  lmin(&s_outMin, out);
  lmax(&s_outMax, out);
  return out;
}

// stdcall (HSAMPLE, S32 rate), void: the callers push the rate as an int (0x1000D45A, 0x1000B17B). The stub hands
// the caller's ESI and return address to rate_adjust, puts the result in the rate's stack slot and jumps to the
// original, which returns straight to the caller.
static __declspec(naked) void rate_stub(void) {
  __asm {
    push dword ptr [esp + 8]
    push esi
    push dword ptr [esp + 8]
    call rate_adjust
    mov dword ptr [esp + 8], eax
    jmp dword ptr [s_rateOrig]
  }
}

// Once, when MilesAudio.dll is first seen loaded (it may come after our first frame).
static void audio_install(void) {
  const HMODULE miles = GetModuleHandleW(L"MilesAudio.dll");
  if (!miles) return;
  s_audioTried = 1;
  s_milesBase = (const char*)miles;
  if (!iat_patch(miles, "mss32.dll", MSS_IMPORT, (void*)&rate_stub, &s_rateOrig, "speed")) {
    np_log("speed: audio rate hook not installed: the sound pitch follows game speed");
    return;
  }
  const void* slot = iat_slot(miles, "mss32.dll", MSS_IMPORT);
  np_log("speed: audio rate hook: MilesAudio.dll %p slot %p (RVA 0x%lx; 0x104C8 in build 244), original %p",
         (void*)miles, slot, (unsigned long)((const char*)slot - s_milesBase), s_rateOrig);
}

static void audio_publish(float f) {
  LONG bits;
  memcpy(&bits, &f, 4);
  const LONG old = InterlockedExchange(&s_factorBits, bits);
  if (old != bits) np_log("speed: audio rate divided by %.2f", f);
  if (++s_audioFrames < 600) return;
  s_audioFrames = 0;
  LONG calls[GS_SITE_COUNT];
  for (int i = 0; i < GS_SITE_COUNT; i++) calls[i] = InterlockedExchange(&s_siteCalls[i], 0);
  const LONG div = InterlockedExchange(&s_divided, 0), ign = InterlockedExchange(&s_ignored, 0);
  const LONG clamps = InterlockedExchange(&s_clamps, 0), faults = InterlockedExchange(&s_faultsAudio, 0);
  const LONG inMin = InterlockedExchange(&s_inMin, 0x7FFFFFFF), inMax = InterlockedExchange(&s_inMax, -1);
  const LONG outMin = InterlockedExchange(&s_outMin, 0x7FFFFFFF), outMax = InterlockedExchange(&s_outMax, -1);
  if (!div && f == 1.f) return;
  np_log("speed: audio 600 frames at /%.2f: calls update %ld stream %ld start %ld voip %ld other %ld; %ld divided, "
         "%ld ignore dilation, %ld clamp(s), %ld fault(s); rate in %ld..%ld out %ld..%ld", f, calls[GS_SITE_SAMPLE],
         calls[GS_SITE_STREAM], calls[GS_SITE_START], calls[GS_SITE_VOIP], calls[GS_SITE_OTHER], div, ign, clamps,
         faults, inMin, inMax, outMin, outMax);
}


static const void* s_checkedCls;
static const char* s_refused;
static ScallFn s_update;
static int s_nSlomo;
static OptRestore s_r;
static unsigned s_level, s_appliesThisLevel;

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static const void* class_of(const void* obj) {
  __try {
    return uobj_class(obj);
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
}

static UProp prop_get(const void* cls, int nameIdx) {
  __try {
    return uprop_get(cls, nameIdx);
  } __except (av_only(GetExceptionCode())) {
    UProp none = { 0, 0, UW_OTHER, 0, 0, 0 };
    return none;
  }
}

static bool read_float(const void* obj, int ofs, float* out) {
  __try {
    memcpy(out, (const char*)obj + ofs, 4);
    return true;
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

static bool write_float(void* obj, int ofs, float v) {
  __try {
    memcpy((char*)obj + ofs, &v, 4);
    return true;
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

static UProp slomo_prop(const void* cls) {
  if (!s_nSlomo) s_nSlomo = uname_find(L"DebugSlomo");
  if (s_nSlomo) return prop_get(cls, s_nSlomo);
  UProp none = { 0, 0, UW_OTHER, 0, 0, 0 };
  return none;
}

static const char* check(void* player, const void* cls) {
  const UProp p = slomo_prop(cls);
  if (!p.ok || p.type != UW_FLOAT || p.elemSize != 4) return "PlayerPawn.DebugSlomo did not resolve as a float";
  // Optional: without it the engine's next tick applies the write anyway.
  s_update = scall_find(player, L"UpdateGameSpeed");
  if (s_update.ok && s_update.parmsSize) {
    np_log("speed: UpdateGameSpeed has parameters (ParmsSize %u); not called", (unsigned)s_update.parmsSize);
    s_update.ok = 0;
  }
  np_log("speed: path checked on player class %p: DebugSlomo +0x%x, UpdateGameSpeed %s", cls, (unsigned)p.offset,
         s_update.ok ? "found" : "not used");
  return 0;
}

// The write, then the event that rebuilds TimeDilation from it in this frame.
static bool put(void* player, int ofs, float v) {
  if (!write_float(player, ofs, v)) return false;
  if (s_update.ok) scall_call(player, s_update, 0, 0);
  return true;
}

static float speed_body(void);

void gamespeed_frame(void) {
  if (!s_audioTried) audio_install();
  audio_publish(speed_body());
}

// The key, then apply or release; returns the DebugSlomo of ours in place this frame (1.0 when none).
static float speed_body(void) {
  const int presses = input_gamespeed_take();
  if (presses) {
    if ((presses & 1) && !mp_refused()) g_gameSpeedOn = !g_gameSpeedOn;
    if (mp_refused()) np_log("speed: key pressed %d time(s), refused: not in multiplayer", presses);
    else np_log("speed: key pressed %d time(s), switch %s", presses, g_gameSpeedOn ? "on" : "off");
  }
  void* player = world_player();
  if (!player) return 1.f;
  const void* cls = class_of(player);
  if (!cls) return 1.f;
  const unsigned level = world_level_serial();
  if (level != s_level) {
    if (s_appliesThisLevel) np_log("speed: level ended with %u apply(s)", s_appliesThisLevel);
    s_level = level;
    s_r = optrestore_level(s_r, level);
    s_appliesThisLevel = 0;
    if (s_refused) s_checkedCls = 0;   // a refusal (a fault included) is checked again once per level
  }
  if (cls != s_checkedCls) {
    s_checkedCls = cls;
    const char* why = check(player, cls);
    if (why && why != s_refused) np_log("speed: unavailable: %s; nothing is written", why);
    s_refused = why;
  }
  if (s_refused) return 1.f;
  const UProp p = slomo_prop(cls);
  if (!p.ok || p.type != UW_FLOAT) return 1.f;
  float cur;
  if (!read_float(player, p.offset, &cur)) return 1.f;
  const int on = g_gameSpeedOn != 0 && !mp_refused();   // off in multiplayer: the game's value is put back
  const float want = gs_slomo(g_cfg.gameSpeedPct);
  const int act = opt_step(s_r, on, cur, want);
  if (act == OPT_APPLY) {
    const bool reset = s_r.applied && cur != s_r.ours;
    s_r = optrestore_apply(s_r, level, cur, want);
    const bool ok = put(player, p.offset, want);
    // Anything rewriting it every frame would log every frame: the first eight per level; the total at the
    // level's end and at release.
    if (++s_appliesThisLevel <= 8)
      np_log("speed: %s: DebugSlomo %.2f -> %.2f (%d%%, times the game's own speed)%s; apply %u this level",
             reset ? "re-applied" : "on", cur, want, gs_clamp_step(g_cfg.gameSpeedPct),
             ok ? (s_update.ok ? ", UpdateGameSpeed called" : "") : " WRITE FAULTED", s_appliesThisLevel);
    return ok ? want : 1.f;
  } else if (act == OPT_RELEASE) {
    float back;
    if (optrestore_release(s_r, cur, &back)) {
      const bool ok = put(player, p.offset, back);
      np_log("speed: off: DebugSlomo %.2f -> %.2f, the game's value%s; %u apply(s) this level", cur, back,
             ok ? "" : " (WRITE FAULTED)", s_appliesThisLevel);
    } else {
      np_log("speed: off: DebugSlomo %.2f is the game's (ours was %.2f): left; %u apply(s) this level", cur,
             s_r.ours, s_appliesThisLevel);
    }
    s_r = optrestore_level(optrestore_init(), level);
    return 1.f;
  }
  return on && cur == want ? want : 1.f;
}

const char* gamespeed_refused(void) { return s_refused ? s_refused : mp_refused(); }

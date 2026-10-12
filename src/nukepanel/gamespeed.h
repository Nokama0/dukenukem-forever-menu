#pragma once
// Game speed (Settings > GAME SPEED): PlayerPawn.DebugSlomo = pct / 100, which the engine's per-tick
// UpdateGameSpeed multiplies with the game's own Extras game speed into TimeDilation (docs/re/player-options.md
// "Game speed"). duke-dnmp's speedrule.h row rules; its clock is not needed here.

enum { GS_PCT_MIN = 10, GS_PCT_MAX = 400, GS_PCT_STEP = 5, GS_PCT_DEF = 50 };

// The row's value on the 5 % steps, inside 10-400 % (duke-dnmp's gs_pct_snap).
inline int gs_clamp_step(int pct) {
  if (pct < GS_PCT_MIN) pct = GS_PCT_MIN;
  if (pct > GS_PCT_MAX) pct = GS_PCT_MAX;
  return (pct + GS_PCT_STEP / 2) / GS_PCT_STEP * GS_PCT_STEP;
}

// DebugSlomo for a row value: 0.10 .. 4.00.
inline float gs_slomo(int pct) { return (float)gs_clamp_step(pct) / 100.f; }

// The audio keeps its pitch (player-options.md): MilesAudio multiplies each playback rate by LevelInfo.TimeDilation, so
// the rate passed to AIL_set_sample_playback_rate is divided by our DebugSlomo; the game's own dilation keeps its
// pitch. Miles' limits were not read: the result stays inside 1..192 kHz and never passes the game's own value.
enum { GS_RATE_MIN = 1000, GS_RATE_MAX = 192000 };
inline int gs_audio_rate(int rate, float f, int* clamped) {
  if (!(f > 0.f) || f == 1.f || rate <= 0) return rate;
  double out = (double)rate / (double)f;
  if (f < 1.f) {
    const double cap = rate > GS_RATE_MAX ? (double)rate : (double)GS_RATE_MAX;
    if (out > cap) { out = cap; *clamped = 1; }
  } else {
    const double floor = rate < GS_RATE_MIN ? (double)rate : (double)GS_RATE_MIN;
    if (out < floor) { out = floor; *clamped = 1; }
  }
  return (int)(out + 0.5);
}

// The slot's callers in MilesAudio.dll, by return address (RVA, build 244): Sample_UpdateParams (call 0x1000D45C,
// rate x dilation, ESI = the sound), UpdateStream (0x1000B17D, ESI = the stream entry), sample start (0x1000C1B0,
// the raw SampleRate) and InitializeVoIPSamples (0x10005B8F, multiplayer).
enum { GS_SITE_OTHER, GS_SITE_SAMPLE, GS_SITE_STREAM, GS_SITE_START, GS_SITE_VOIP, GS_SITE_COUNT };
inline int gs_rate_site(unsigned rva) {
  switch (rva) {
    case 0xD462: return GS_SITE_SAMPLE;
    case 0xB183: return GS_SITE_STREAM;
    case 0xC1B6: return GS_SITE_START;
    case 0x5B95: return GS_SITE_VOIP;
    default: return GS_SITE_OTHER;
  }
}
// Only the two dilated sites, and only for a sound or stream that does not ignore dilation (it got 1.0, not ours).
inline int gs_rate_divides(int site, int ignoresDilation) {
  return (site == GS_SITE_SAMPLE || site == GS_SITE_STREAM) && !ignoresDilation;
}

// The switch, 0/1. Not saved: off at every launch (duke-dnmp, user 2026-10-03). Written by its row and the key.
extern int g_gameSpeedOn;

// Game thread, once a frame after world_frame: the key's presses, then apply or release.
void gamespeed_frame(void);
// Null while the path is usable (or not yet checked); else why the row is unavailable.
const char* gamespeed_refused(void);

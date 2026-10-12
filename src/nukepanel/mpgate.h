#pragma once
// The panel is a single-player tool: every feature that gives an edge over other players refuses outside standalone
// play. LevelInfo.NetMode is Engine.u's ENetMode (NM_Standalone 0, NM_DedicatedServer 1, NM_ListenServer 2,
// NM_Client 3); the game's own AActor::IsMP (Engine.dll!0x102F9D20) is "Level != null && Level.NetMode != 0".
enum { MP_NET_UNREAD = -1, MP_NM_STANDALONE = 0, MP_NM_DEDICATED = 1, MP_NM_LISTEN = 2, MP_NM_CLIENT = 3 };

#define MP_WHY "Not in multiplayer"

// A level is held and its mode is not standalone. An unread mode in a level fails closed; no level (the Entry menu,
// a load) has nothing to gate.
inline int mp_active(int levelHeld, int netMode) { return levelHeld && netMode != MP_NM_STANDALONE; }
inline const char* mp_reason(int levelHeld, int netMode) { return mp_active(levelHeld, netMode) ? MP_WHY : 0; }

// A flight ends only after multiplayer read on this many frames in a row, so one bad read in single player cannot end it.
enum { MP_SUSTAIN_FRAMES = 3 };
inline int mp_streak(int streak, int mpNow) { return mpNow ? (streak < MP_SUSTAIN_FRAMES ? streak + 1 : streak) : 0; }
inline int mp_sustained(int streak) { return streak >= MP_SUSTAIN_FRAMES; }

// world.cpp: MP_WHY while the current level is not standalone, else null. Safe from any thread.
const char* mp_refused(void);

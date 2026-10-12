# Multiplayer gate

The panel is a single-player tool. Every feature that gives an edge over other players refuses unless the held
level's `NetMode` is standalone (using them against players risks the account).

- **Detection:** `LevelInfo.NetMode`, a ByteProperty read by name each frame (cached property, one byte). Engine.u's
  `ENetMode`: NM_Standalone 0, NM_DedicatedServer 1, NM_ListenServer 2, NM_Client 3 (the name table lists them in
  reverse; `Pickup.HandleRespawn` tests `NetMode == 0`, items.md). The game's `AActor::IsMP` (Engine.dll!0x102F9D20)
  is `Level != null && byte[Level+0x918] != 0`, the same test.
- **Fails closed:** a held level whose NetMode cannot be read counts as multiplayer. No level (Entry, loads) allows,
  as nothing runs there. Rule and test: `mpgate.h`, `tb_0561`. One log line per mode change (`world: NetMode N`).
- **Refused (greyed "Not in multiplayer", skipped by the live count, inert):** Aimbot, Silent aim, Triggerbot,
  Auto-execute, All weapons, No recoil, Fire rate, Reload speed (also the mounted gun's spread and rate), Game speed
  (key ignored), Auto-win QTEs and minigames, Pinball autoplay, Clean Duke Vision, the free camera (a flight ends), every ESP row,
  Damage numbers and the achievement markers (their switch ignores clicks). Patched values are put back as when a row
  is switched off; a held trigger press is released by the Triggerbot's own gate.
- **Kept:** FOV, frame cap, raw mouse, post effects, No screen shake, Level stats, photo, the
  panel and the hotkeys.
- Each feature's `*_refused()` returns its own reason first, else `mp_refused()` (world.cpp). Marker passes
  (worldscan, items, aircraft, spawnbox) are skipped in frame.cpp.

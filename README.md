<div align="center">

# Duke Nukem Forever Mod Menu

**Outlines, aim assist, a free camera and a progress checklist, one key away.**

A mod menu for Duke Nukem Forever (2011) on Steam, including The Doctor Who Cloned Me DLC.

![Game](https://img.shields.io/badge/Game-Duke%20Nukem%20Forever-FFB000?labelColor=0E0F13&style=flat)
![Platform](https://img.shields.io/badge/Platform-Windows-FFB000?labelColor=0E0F13&style=flat)
![Release](https://img.shields.io/github/v/release/Nokama0/dukenukem-forever-menu?label=Release&color=FFB000&labelColor=0E0F13&style=flat)
![Build](https://img.shields.io/github/actions/workflow/status/Nokama0/dukenukem-forever-menu/build.yml?branch=main&label=Build&labelColor=0E0F13&style=flat)
![License](https://img.shields.io/badge/License-Unlicense-FFB000?labelColor=0E0F13&style=flat)

</div>

---

A mod menu for **Duke Nukem Forever** (2011), Steam build 244 (2011.12.01), including the
*The Doctor Who Cloned Me* DLC. Press **Insert** in game to open it.

It loads as a `winmm.dll` next to the game's own files. It does not change any game file, and it reads the game's
state through the engine's own object tables, finding things by name wherever it can.

## What it does

The menu has eight tabs.

**Visuals**
- Enemy outlines with health bars, names and distance; allies drawn apart from enemies
- Markers for items and pickups, ammo crates, quest items, breakables, ego boosts, helmets and phone messages
- Damage numbers, level stats and draw distance

**Combat**
- Aimbot, with a hold key and smoothing
- Silent aim: target choice, aim point, cone, range, lead for moving targets
- Triggerbot, and auto-execute for stunned or frozen enemies

**Weapons**
- Perfect accuracy and no recoil
- Fire rate and reload speed sliders

**View**
- Custom field of view
- A free camera: flight speed, pause the game, teleport, fullbright, no fog or culling, screenshot mode
- Photos larger than the screen, with a photo folder you can choose
- No screen shake, clean Duke Vision

**Game**
- Game speed
- Auto-win for button-mash QTEs and minigames, and pinball and Nuke Tank (arcade) autoplay
- The pre-order Big Package bonus

**Progress**
- Ego awards, ego cap, helmets and phone messages, per chapter and for the whole profile, read from the game's own
  save data. Base game and DLC.

**Achievements**
- A checklist of this level's Steam achievements, what you have collected across levels, and counters

**Settings**
- Menu key and scale, raw mouse input, frame cap, borderless window
- The Steam achievement backup and reset

Cheat-style features start off; display and quality-of-life options start on. Hover a row for a short explanation.

## Get it

You need Duke Nukem Forever on Steam, build 244 (`Buckeye.244--2011.12.01`). Other builds are not supported. Use the
stock game: replacement System folders such as DNF: Enhanced are untested.

1. **Download** `winmm.dll` from the [latest release](https://github.com/Nokama0/dukenukem-forever-menu/releases/latest).
2. **Copy** it into `<game folder>\System`, next to `DukeForever.exe`.
3. **Start** the game and press **Insert**.

To remove it, delete `System\winmm.dll` and the `System\NukePanel` folder.

## Good to know

- **Achievement reset is permanent.** Settings > Steam achievements can lock every Duke Nukem Forever achievement
  on your Steam account again, after a second click within five seconds. The backup it writes first is only a
  record (names, unlock dates and times) and **cannot restore** anything.
- **Some features earn real achievements.** Auto-win and autoplay use the game's own success paths, so the
  achievements they lead to are real.
- **Where files go.** Photos go to `<Pictures>\Nuke Panel` by default, or to the folder you choose in the
  menu. Everything else (settings, log, backups) stays under the game's `System\NukePanel\`. The menu never rewrites
  the game's own `.ini` files.

---

## Build it

You need Windows with Visual Studio 2022 (the MSVC x86 32-bit toolset) and a Windows 10 SDK. The scripts find them
with `vswhere`.

```powershell
powershell -File src/nukepanel/build.ps1
powershell -File src/nukepanel/deploy.ps1 -GameDir "<game folder>\System"
```

### Tests

`build.ps1` runs a unit-test host of the pure rules first, then compiles `build\winmm.dll`. A red test fails the
build; a good run prints a line ending in `N passed, 0 failed`.

### Deploy

`deploy.ps1` copies the DLL into the game. `-GameDir` can be left out if the `NUKEPANEL_GAME_DIR` environment
variable holds the same path. Deploy refuses unless that folder contains `DukeForever.exe` and `Engine.dll`, so a
typo cannot drop a DLL somewhere else. It creates only `System\winmm.dll` and `System\NukePanel\`, never
overwrites a file it did not create, and refuses while the game is running. It prints `MATCH` once every copied
file has been hash-checked.

Design notes and reverse-engineering findings are in [`docs/`](docs/README.md).

## Other menus

The same menu, ported to other Duke Nukem games.

| Game | Repository |
|---|---|
| Duke Nukem: Manhattan Project | [dukenukem-manhattanproject-menu](https://github.com/Nokama0/dukenukem-manhattanproject-menu) |
| Alien Armageddon on EDuke32 | [eduke32-aa-menu](https://github.com/Nokama0/eduke32-aa-menu) |
| Duke Nukem 3D on EDuke32 with the High Resolution Pack | [eduke32-hrp-menu](https://github.com/Nokama0/eduke32-hrp-menu) |
| Duke Nukem 3D: 20th Anniversary World Tour | [duke3d-worldtour-menu](https://github.com/Nokama0/duke3d-worldtour-menu) |
| Duke Nukem 3D: Megaton Edition | [duke3d-megaton-menu](https://github.com/Nokama0/duke3d-megaton-menu) |

## License

[The Unlicense](LICENSE): public domain, no warranty.

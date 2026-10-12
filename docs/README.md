# Research notes

One note per topic in [re/](re/), each with a summary at the top. They record what was read from the game's script
packages, the map files and the binaries (build 244), what was measured at runtime, and what is still open. Addresses are
for the Steam build 244 modules; the panel resolves properties by name at runtime wherever it can. A path such as
`extracted-packages/*.u` means a local folder of the game's script packages, extracted with `tools/mpkgx`.

## Engine and loader

- [re/engine-map.md](re/engine-map.md) -- the object and name tables, `FindFunction`/`ProcessEvent`, the console exec gate, and the game and render threads.
- [re/ceg.md](re/ceg.md) -- Steam CEG in Engine.dll: a code patch kills the game (`ExitProcess` 0x8000DEAD); import slots observed safe.
- [re/modules-ceg.md](re/modules-ceg.md) -- CEG per module: the kill routine is in Engine.dll only; dnGame, dnAI, dnVehicles and D3DDrv9 carry none.
- [re/input.md](re/input.md) -- the mouse is an exclusive DirectInput device; release it through dinput8.dll's methods, not Engine.dll code.
- [re/display.md](re/display.md) -- fullscreen is a D3DDrv9 flag plus an Engine.dll restyle; borderless, focus loss and the windowed-at-launch case.
- [re/multiplayer.md](re/multiplayer.md) -- the multiplayer gate: edge-giving features refuse unless `LevelInfo.NetMode` is standalone.

## Player options

- [re/player-options.md](re/player-options.md) -- FOV, game speed and perfect accuracy: the path each takes, none reaching SaveConfig; the bytecode decoder.
- [re/accuracy.md](re/accuracy.md) -- what moves a shot: the zeroed spreads are `PerformFire`'s only random term; the visible pistol spread is its view kick.
- [re/handling.md](re/handling.md) -- fire rate and reload speed ride the held weapon's animation controller and the `LockoutFireTime` timer.
- [re/qol.md](re/qol.md) -- background running, raw mouse, screen shake and post effects.
- [re/pause.md](re/pause.md) -- pausing without the menu: `ForcePause_NoFade` / `ForceUnPause`, and what freezes.
- [re/dukevision.md](re/dukevision.md) -- Duke Vision's scanlines and distortion are one HUD effect that `DukeHUD.RemoveHudEffect` removes.
- [re/big-package.md](re/big-package.md) -- Duke's Big Package: the preorder unlock and why every map load resets it.

## Aim, targets and ESP

- [re/aim.md](re/aim.md) -- silent aim and the triggerbot: the `CurrentShake` bracket in the dnGame ProcessEvent slot, visibility, projectile models and the intercept.
- [re/turrets.md](re/turrets.md) -- mounted guns (MG42, grenade launcher, SuperTurret): aimed through the turret's remapper, shot by the control's own trace.
- [re/aircraft.md](re/aircraft.md) -- aircraft are decorations, not pawns; which weapons hurt them and how the aim reaches them.
- [re/spawns.md](re/spawns.md) -- drop-in spawns, AI teleports, invulnerable pawns and mounted collision: where the body really is.
- [re/factions.md](re/factions.md) -- who is an enemy: each pawn's own relationships, not the faction classes.
- [re/items.md](re/items.md) -- every single-player pickup class, the game's own take-now gate, breakables and per-map counts.
- [re/modelbox.md](re/modelbox.md) -- marker boxes from the model's render bounds instead of the collision cylinder.
- [re/executions.md](re/executions.md) -- finish-me executions: `CanBeUsedBy` then `Used(player, player)` by ProcessEvent.

## Free camera

- [re/freecam.md](re/freecam.md) -- free camera, teleport, photo, screenshot mode, fog, culling and Fullbright.

## Progress, achievements and minigames

- [re/progress.md](re/progress.md) -- ego awards, chapter completion, award givers, helmets and phones.
- [re/dlc.md](re/dlc.md) -- the DLC campaign: chapters, ego awards, achievements and where the base rules differ.
- [re/achievements.md](re/achievements.md) -- all 64 achievements: how each is awarded and which level can earn it.
- [re/steam-stats.md](re/steam-stats.md) -- how the game talks to Steam stats, and resetting achievements.
- [re/autowin.md](re/autowin.md) -- button-mash QTEs and the minigames that grant an ego award from one callable function.
- [re/minigame-autoplay.md](re/minigame-autoplay.md) -- legitimate pinball autoplay: the ball, the flippers, the plunger and the measured shot zones.
- [re/arcade.md](re/arcade.md) -- the Nuke Tank cabinet: map-scripted rules, the trace format and the autoplay.

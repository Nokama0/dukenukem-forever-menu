#pragma once
// nukepanel.ini in our own folder (np_our_dir), never beside the exe and never the game's own ini files.
// The shape of duke-dnmp's config.h (plain int fields, one field table for reader and writer), reduced to the
// rows this port has.

struct Config {
  // [panel]
  int menukey;     // key: virtual-key code of the panel key, default VK_INSERT
  // x, y, w, h: stored panel box, logical pixels. -1/-1/0/0 is "unset" and
  // panel_layout picks the default box for the screen (0,0 is a real corner).
  int panelx, panely, panelw, panelh;
  int animations;  // 0/1, the panel's springs and fades
  int uiScale;     // UI_SCALE_MIN..MAX %, default UI_SCALE_DEFAULT; menu.cpp caps what it applies per screen
  // [framerate]
  int frameCapOn;  // 0/1, the Frame cap switch (Settings > FRAME RATE), default 1
  // 30..240 is the player's explicit choice; 0 is the sentinel "follow the
  // monitor's refresh rate". Never write a computed default here -- config
  // diffs every field on any change and would leak it into the ini.
  int frameCapFps;
  // [esp] (Visuals tab; duke-dnmp's keys and defaults)
  int espOn;            // 0/1, Enemies: boxes on hostile pawns
  int espBoundingBoxes; // 0/1, the corner brackets; off keeps the labels and health bars
  int espHealthBars;    // 0/1, the bar left of the box
  int espNames;         // 0/1, Name and health: the class name and cur/max above the box
  int espDistance;      // 0/1, metres below the box
  int espAllies;        // 0/1, friendly pawns too, in the friendly colour; a sub-row of Enemies, default off
  int dmgNumbers;       // 0/1, numbers over enemies that lose health
  int espDrawDist;      // metres, ESP_DRAWDIST_MIN..MAX (esp_layout.h): enemies and damage numbers beyond are not drawn
  int espNoLimit;       // 0/1, Unlimited range: ignores espDrawDist entirely
  int espEgo;           // 0/1, Ego boosts: unfound award givers (egoesp.h), default on as Enemies
  // Visuals WORLD (items.h)
  int espItems;         // 0/1, Items and pickups (crates by contents), default on
  int espHideTaken;     // 0/1, its sub-switch Hide what you can't take (CanPickupClass), default off as DNMP
  int espAmmoCrates;    // 0/1, Ammo crates: infinite ammo crates and piles, default off
  int espQuest;         // 0/1, Quest items: battery, gas can, energy cell until carried, default off
  int espBreakables;    // 0/1, Breakables: padlocks, stash openers and Event-firing breakables (E5), default off
  int levelStats;       // 0/1, Visuals WORLD Level stats: this chapter's ego items top left, default off
  int espHelmets;       // 0/1, Helmets: unfound helmets (collect.h), off
  int espPhones;        // 0/1, Phone messages: unheard message phones, default off
  // [view] Custom field of view (View > FIELD OF VIEW, fov.h)
  int fovOn;            // 0/1, the Custom field of view switch, default off
  int fovDeg;           // FOV_MIN_DEG..FOV_MAX_DEG degrees; 0 = the game's own DefaultFOV (the reset), default 0
  // [game] Game speed (Settings > GAME SPEED, gamespeed.h). The switch itself is not saved: off at every launch.
  int gameSpeedPct;     // GS_PCT_MIN..MAX %, snapped to 5 % steps, default 50
  int gameSpeedKey;     // BIND_ROW_GAMESPEED: toggles the Game speed switch, default 0 (unbound)
  // [weapons] Perfect accuracy (Weapons > PERFECT ACCURACY, accuracy.h)
  int accuracyOn;       // 0/1, default off
  int recoilOn;         // 0/1, No recoil (its second row), default off
  // [weapons] HANDLING (handling.h): DNMP's keys and stops
  int fireRatePct;      // 100..400 %, 50 % stops, default 100 (stock)
  int reloadPct;        // 100..1000 %, 1100 = Max (20x); default 100 (stock)
  // [extras]
  int bigPackage;  // 0/1, Duke's Big Package: the pre-order bonus unlocked every launch, default 1
  // [display]
  int borderless;  // 0/1, Borderless window: read at launch only (display.h), default 0
  int keepRunning; // 0/1, Keep running when alt-tabbed (focuskeep.h), default 0
  // [postfx] View > POST PROCESSING (postfx.h): 1 shows the effect (the game's own value, the default), 0 forces it off
  int pfxBlur, pfxMotionBlur, pfxPixelMotionBlur, pfxDof, pfxBloom, pfxToneMap, pfxUnderWater, pfxFilmGrain,
      pfxPinkOVision;
  int rawMouse;    // [input] 0/1, Raw mouse input (rawmouse.h), default 0
  int noShake;     // [view] 0/1, No screen shake (noshake.h), default 0
  int cleanDukeVision; // [view] 0/1, Clean Duke Vision (dukevision.h), default 0
  // [autowin] Settings > AUTO-WIN (autowin.h), both default off
  int autoQte;       // 0/1, Auto-win QTEs: Jump pressed for the player through each button-mash QTE
  int autoMinigames; // 0/1, Auto-win minigames: pool, pinball, slots and air hockey win once per use
  int pinballAuto;   // 0/1, Pinball autoplay: the table's own flippers and plunger pressed (pinbot.h), default off
  int arcadeAuto;    // 0/1, Arcade autoplay: Nuke Tank's own Fire, Left and Right pressed (arcbot.h), default off
  int autoExecute;   // 0/1, Auto-execute enemies: a stunned or frozen enemy in reach (autoexec.h)
  // [aim] Combat > TARGETING (aim.h): DNMP's keys, ranges and defaults
  int silentOn;      // 0/1, Silent aim, default off: the slot subscriptions are not even enabled until it is on
  int aimPoint;      // AIM_AT_BODY (0, default) or AIM_AT_HEAD (1)
  int aimRangeM;     // metres, AIM_RANGE_MIN_M..AIM_RANGE_MAX_M, default the maximum
  int aimOffscreen;  // 0/1, Off-screen targets, default on
  int aimLead;       // 0/1, Lead moving targets (projectile weapons), default on as DNMP's
  int aimMarker;     // 0/1, Aim calibration marker, default off
  int tbotOn;        // 0/1, Triggerbot (tbot.h), default off
  int tbotWhen;      // TBOT_WHEN_CROSSHAIR (0, default), TBOT_WHEN_LOCKED (1) or TBOT_WHEN_AIMBOT (2)
  int tbotDelay;     // TBOT_DELAY_MS_MIN..MAX ms the condition must hold first, default TBOT_DELAY_MS_DEF
  // Combat > AIMBOT (aimbot.h): AA's keys and defaults
  int aimbotOn;      // 0/1, Aimbot, default off
  int aimHold;       // 0/1, Hold key: only while the Aimbot button is held, default on
  int aimbotKey;     // BIND_ROW_AIMBOT, default AIMBOT_KEY_DEF (mouse button 4)
  int aimSmooth;     // 0..100 %, Smoothing, default 0 (a snap)
  int aimTarget;     // AIM_PICK_CROSSHAIR (0, default) or AIM_PICK_NEAREST (1); Silent aim's too
  int aimCone;       // AIM_CONE_MIN..MAX degrees, default AIM_CONE_DEF; Crosshair mode only (AA)
  // [freecam] View > FREE CAMERA (freecam.h): DNMP's keys and defaults
  int freecamOn;         // 0/1, the Free camera switch: arms the camera's keys, starts no flight; default on
  int freecamKey;        // BIND_ROW_CAMERA: the toggle, default V
  int freecamSpeedPct;   // FREECAM_SPEED_MIN_PCT..MAX_PCT, Flight speed, default FREECAM_SPEED_DEF_PCT
  int freecamPause;      // 0/1, Pause the game while flying, default off
  int freecamMarker;     // 0/1, Show player: a marker on Duke while flying, default on
  int freecamDebug;      // 0/1, Debug readout: the camera's position and angle, default off
  int freecamShot;       // 0/1, Screenshot mode: no HUD and none of our overlays while flying, default off
  int camCopyKey;        // BIND_ROW_CAMCOPY: the view line to the clipboard, default B
  int camPasteKey;       // BIND_ROW_CAMPASTE: flies to a clipboard view line, default N
  int teleportKey;       // BIND_ROW_TELEPORT: moves Duke to the spot under the camera's crosshair, default T
  int teleportMarker;    // 0/1, Teleport marker: where Duke would land, red where he cannot; default on
  int photoKey;          // BIND_ROW_PHOTO: takes the photo while flying, again cancels; default F12
  int photoMult;         // PHOTO_MULT_MIN..MAX, Photo size: the screen times this on each side, default 4
  int freecamNoFog;      // 0/1, No distance fog while flying (r_DrawDistanceFog, r_UseFarPlane), default off
  int freecamBright;     // 0/1, Fullbright while flying (the game's unlit RendMap, freecam.md section 7), default off
  int freecamLift;       // FBLIFT_MIN_PCT..MAX_PCT, Brightness lift under Fullbright (fblift.h, 7g), default 60
  int aimDebugDoubleTrace;   // 0/1, hidden ([aim] debug_double_trace): re-trace each clear point with the old blocking flags
  int freecamNetDelay;   // frames, hidden: the Fullbright stuck net's patience (freecam.h fc_stuck_step_n), default 3
  int freecamNoCull;     // 0/1, No culling while flying (far plane, occlusion, anti-portals; section 8), default off
  // [achievements] ach.esp.<id>: AH_MODE_AUTO (0, default), _ON or _OFF per achievement id (achhelp.h), stored as
  // the words auto, on, off.
  int achEsp[64];
  // String fields (config.cpp's kStrFields): UTF-8, default "", a value longer than CONFIG_STR_CAP - 1 is refused.
  char photoFolder[260]; // [freecam] photofolder: "" is the default, <Pictures>\Nuke Panel (photofolder.h)
};

enum { CONFIG_STR_CAP = (int)sizeof(((Config*)0)->photoFolder) };

// Flight speed: AA's percent range; 100 % is DNMP's 8 m/s.
enum { FREECAM_SPEED_MIN_PCT = 1, FREECAM_SPEED_MAX_PCT = 500, FREECAM_SPEED_DEF_PCT = 100, FREECAM_SPEED_BASE_M = 8 };

extern Config g_cfg;

// Set by every row that changes a value; config_write_changed writes only the
// keys that moved since the last load or write.
extern int g_cfgDirty;

// bind.h addresses binds by row (config.cpp's kBindFields): the panel key, the Game speed key, the Aimbot button and
// the free camera's keys. A row outside the table reads 0 (unbound).
int  config_bind_get(int row);
void config_bind_set(int row, int vk);

void config_defaults(void);
// Whether the loader reads [section] key; a removed row's key is not read (old ini values are ignored).
bool config_key_known(const char* section, const char* key);
// Loads NukePanel\nukepanel.ini beside our module. False when there is no file
// (the normal first run): g_cfg then holds the defaults.
bool config_load(void);
// The same from an explicit path; the suite's entry point.
bool config_load_from(const char* path);
// Writes each changed key with WritePrivateProfileStringW, never the whole file, so comments and keys this build does
// not know survive. No-op unless dirty, and after a failed write until cfgretry.h's wait is over. The game ends without
// DLL_PROCESS_DETACH (docs/re/engine-map.md section 5), so this runs on every change, never only at exit.
void config_write_changed(void);
// [display] borderless as config_load_from would leave it, from the file's first n bytes (ini_peek_int).
int config_peek_borderless_text(const char* text, int n);
// The same from nukepanel.ini through kernel32 file I/O only, without touching g_cfg: the device is created
// before our first frame loads the config. 0/1 (no file gives the default), -1 when the file could not be
// read whole.
int config_peek_borderless(void);

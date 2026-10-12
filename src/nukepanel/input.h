#pragma once
// D5: the game window's WndProc subclass (the panel key, the panel's input) and the cursor gate on the engine's
// user32 import slots. Plain statics: the window, CreateDevice and every Present run on one thread
// (docs/re/engine-map.md section 5); input.cpp logs a THREAD MISMATCH if the WndProc ever runs on another.
#include <windows.h>

// Frame path, every frame before the panel draws (frame.cpp, inside the device Present detour). Installs the
// subclass and the cursor gate on the first frame d3dcap_window() is known (never in DllMain) and maps the
// cursor to the W x H back buffer.
void input_frame(int W, int H);

bool input_panel_open(void);

// The subclassed game window (the device's focus window), or NULL before input_frame found it.
HWND input_window(void);
// 1 while the game window is the foreground window and, since its last deactivation (WM_ACTIVATE inactive or
// WM_ACTIVATEAPP 0), a WM_ACTIVATE activation has been handled (the engine's resume has run); 0 otherwise or before
// there is a window.
int input_game_front(void);

// The cursor in back-buffer pixels, as of the last input_frame.
void input_cursor(int* x, int* y);
// 0 left, 1 right: held right now.
bool input_mouse_down(int button);
// 0 left, 1 right: button-down messages since the last call, so a click that
// goes down and up between two frames is still seen.
int input_press_take(int button);
// Game speed key presses (BIND_ROW_GAMESPEED) since the last call, taken with the panel shut.
int input_gamespeed_take(void);
// Camera key presses (BIND_ROW_CAMERA, while the Free camera switch arms it) taken with the panel shut, and the Copy
// view, Paste view, Teleport and Photo keys taken while flying; each since the last call.
int input_camera_take(void);
int input_camcopy_take(void);
int input_campaste_take(void);
int input_teleport_take(void);
int input_photo_take(void);
// Around a modal dialog owned by the game window (the Photo folder picker): the cursor hold and our clip let go and
// the OS cursor shown; afterwards the shows are undone and the panel's state cleared.
void input_modal_begin(void);
void input_modal_end(void);
// The free camera's gate (freecam.cpp): while on, GetKeyState reads 0 for the engine, its mouse reads hand it zeros
// (the counts stay the camera's) and keyboard and mouse messages are swallowed, so Duke stands and no binding fires.
void input_set_flight(int on);
// Focus losses (WM_ACTIVATEAPP 0, WM_KILLFOCUS) since load: a change ends a flight.
unsigned input_focus_losses(void);
// Wheel movement since the last call, in WHEEL_DELTA units (120 a notch).
int input_wheel_take(void);
// The oldest queued key-down while the panel is open (the panel key excluded); false when the queue is empty.
// `repeat` is the auto-repeat bit; `mods` is INPUT_MOD_* as held when the message arrived.
enum { INPUT_MOD_SHIFT = 1, INPUT_MOD_CTRL = 2 };
bool input_key_take(int* vk, int* repeat, int* mods);
// The oldest queued WM_CHAR while the panel is open; false when empty.
bool input_char_take(unsigned* ch);

// 1 when GetCursorInfo reported the OS cursor hidden after the last open: the panel then draws its own.
int input_software_cursor(void);

// What stops drawing (drawgate.h), set by the frame path every frame and after a failed Reset; NULL when the panel
// can draw. While it is set the panel key does not open the panel, and an open panel is closed.
void input_set_draw_disabled(const char* reason);

// The panel key, a virtual key; panel_key_effective (ui/menu.h) decides the
// fallback, so the status line and the toggle cannot name different keys.
void input_set_panel_key(int vk);

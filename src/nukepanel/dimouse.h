#pragma once
// DirectInput mouse release rule (input.md). The engine asks for an exclusive DirectInput mouse; dimouse.cpp lowers it
// to shared. Open unacquires it; the engine's own Acquire/Unacquire are recorded and held while open and its
// GetDeviceState reads zeros; close re-acquires if the engine still wants it. No Engine.dll code is touched (ceg.md).

enum { DM_KIND_UNKNOWN = -1, DM_KIND_OTHER = 0, DM_KIND_MOUSE = 1 };
// What a transition asks dimouse.cpp to call on the tracked device through the real method.
enum { DM_CALL_NONE = 0, DM_CALL_UNACQUIRE = 1, DM_CALL_ACQUIRE = 2 };
// The device's own answer at open (a real GetDeviceState), when it gives one.
enum { DM_SEEN_UNKNOWN = -1, DM_SEEN_RELEASED = 0, DM_SEEN_ACQUIRED = 1 };

struct DmState {
  int open;
  void* device;   // the engine's latest DirectInput device; 0 before the first or once forgotten
  int kind;       // DM_KIND_* of `device`
  int wants;      // the engine's acquire intent on `device`: its latest Acquire (1) or Unacquire (0)
};

inline DmState dm_init(void) { DmState s = { 0, 0, DM_KIND_UNKNOWN, 0 }; return s; }

// A device from CoCreateInstance replaces the tracked one, which is never called again (the engine released it).
inline void dm_on_created(DmState* s, void* dev) { s->device = dev; s->kind = DM_KIND_UNKNOWN; s->wants = 0; }
// Its window is going away: nothing is called on the device again until the engine creates one.
inline void dm_forget(DmState* s) { dm_on_created(s, 0); }
// Release on `self` returned `refs`: the tracked device at 0 is freed and forgotten. Returns 1 when it was.
inline int dm_on_released(DmState* s, const void* self, unsigned long refs) {
  if (refs != 0 || !self || self != s->device) return 0;
  dm_forget(s);
  return 1;
}

// GetDeviceState's size: DIMOUSESTATE (0x10) or DIMOUSESTATE2 (0x14).
// DIMOUSESTATE(2).rgbButtons[0] (+12, high bit): the fire button as the engine's own read received it.
inline int dm_button0(const void* buf, unsigned size) {
  return buf && size >= 13 && (((const unsigned char*)buf)[12] & 0x80) != 0;
}
// Every button's high bit as a mask (bit i: rgbButtons[i]): four in DIMOUSESTATE, eight in DIMOUSESTATE2.
inline unsigned dm_buttons(const void* buf, unsigned size) {
  if (!buf || size < 0x10) return 0;
  const unsigned n = size >= 0x14 ? 8 : 4;
  unsigned m = 0;
  for (unsigned i = 0; i < n; i++)
    if (((const unsigned char*)buf)[12 + i] & 0x80) m |= 1u << i;
  return m;
}
inline int dm_kind_by_size(unsigned size) { return size == 0x10 || size == 0x14 ? DM_KIND_MOUSE : DM_KIND_OTHER; }
// GetDeviceInfo's dwDevType: the low byte is the type, DI8DEVTYPE_MOUSE = 0x12.
inline int dm_kind_by_devtype(unsigned devType) { return (devType & 0xFF) == 0x12 ? DM_KIND_MOUSE : DM_KIND_OTHER; }

inline int dm_needs_kind(const DmState* s, const void* self) {
  return self && self == s->device && s->kind == DM_KIND_UNKNOWN;
}

// Classifies the tracked device once. A mouse found while open, with the engine holding it, is released now.
inline int dm_set_kind(DmState* s, const void* self, int kind) {
  if (!dm_needs_kind(s, self)) return DM_CALL_NONE;
  s->kind = kind;
  return s->open && kind == DM_KIND_MOUSE && s->wants ? DM_CALL_UNACQUIRE : DM_CALL_NONE;
}

// Acquire (1) or Unacquire (0) on `self`: recorded as the engine's intent for the tracked device whatever its kind. Any
// caller on that pointer counts; a wrong intent recovers through UpdateInput's NOTACQUIRED re-acquire and the device's
// own answer at open. Returns 1 when the call reaches the real method, 0 when it is held (DI_OK).
inline int dm_on_engine_acquire(DmState* s, const void* self, int acquire) {
  if (!self || self != s->device) return 1;
  s->wants = acquire ? 1 : 0;
  return s->open && s->kind == DM_KIND_MOUSE ? 0 : 1;
}

// GetDeviceState on `self`: 1 reaches the real method, 0 reads zeros (dm_zero_state).
inline int dm_on_getstate(const DmState* s, const void* self) {
  return self && self == s->device && s->open && s->kind == DM_KIND_MOUSE ? 0 : 1;
}

// Flying (the free camera): the read is real, so its counts are the camera's, and the engine is handed zeros
// afterwards. Never while open, which reads nothing at all.
inline int dm_getstate_hides(const DmState* s, const void* self, int flying) {
  return flying && self && self == s->device && s->kind == DM_KIND_MOUSE && !s->open;
}

// The held read: zeros and DI_OK, or DIERR_INVALIDPARAM for a buffer past any mouse format (as the real method
// refuses a size that is not its format's).
inline long dm_zero_state(void* buf, unsigned size) {
  if (!buf || size == 0 || size > 0x400) return (long)0x80070057L;
  unsigned char* p = (unsigned char*)buf;
  for (unsigned i = 0; i < size; i++) p[i] = 0;
  return 0;
}

// GetDeviceState's HRESULT: DI_OK is acquired; DIERR_NOTACQUIRED / DIERR_INPUTLOST is not; anything else is unknown.
inline int dm_seen_from_hr(long hr) {
  if (hr == 0) return DM_SEEN_ACQUIRED;
  if (hr == (long)0x8007000CL || hr == (long)0x8007001EL) return DM_SEEN_RELEASED;
  return DM_SEEN_UNKNOWN;
}

// `seen` is the device's own answer: it overrides a recorded intent, which goes stale when the engine drops its
// flag on a device dinput already unacquired (no Unacquire call to record). Unknown keeps the recorded intent.
inline int dm_on_open(DmState* s, int seen) {
  s->open = 1;
  if (s->device && s->kind == DM_KIND_MOUSE && seen != DM_SEEN_UNKNOWN) s->wants = seen;
  return s->device && s->kind == DM_KIND_MOUSE && s->wants ? DM_CALL_UNACQUIRE : DM_CALL_NONE;
}

// The engine's latest intent, including one recorded while open.
inline int dm_on_close(DmState* s) {
  s->open = 0;
  return s->device && s->kind == DM_KIND_MOUSE && s->wants ? DM_CALL_ACQUIRE : DM_CALL_NONE;
}

// ------------------------------------------------------------------ dimouse.cpp (game thread)
// Patches Engine.dll's ole32 CoCreateInstance import slot; boot.cpp, after the stock gate. Logs if it cannot.
void dimouse_install(void);
// input.cpp's open edge, before its cursor gate turns on.
void dimouse_open(void);
// input.cpp's close edge, after its cursor gate is off and its own replay is done.
void dimouse_close(void);
// input.cpp's flight gate: while on, the engine's real reads keep feeding dimouse_counts_take and the engine gets zeros.
void dimouse_set_flight(int on);
// The device's window is being destroyed (input.cpp WM_NCDESTROY), before its close.
void dimouse_forget(void);
// Engine Acquire/Unacquire calls held and GetDeviceState reads zeroed since the last take (the input line).
void dimouse_take(unsigned* acquireHeld, unsigned* zeroed);
// The mouse's lX/lY summed over the engine's real GetDeviceState reads since the last take (none while the panel
// holds the mouse: those reads are zeroed). Raw mouse input's source (rawmouse.h).
void dimouse_counts_take(int* dx, int* dy);
// The wheel's lZ summed over the same reads since the last take (the free camera's FOV; the engine gets zeros while
// flying).
int dimouse_wheel_take(void);
// 1 while the engine's latest call on its DirectInput mouse was Acquire (it holds the mouse); 0 after its Unacquire or
// with no classified mouse. Game thread. Raw mouse passes the game's turn through while the mouse is not tracked, so a
// refused hook never freezes the view.
int dimouse_tracking_mouse(void);
// Button 0 in the engine's latest read of its mouse (0 while those reads are zeroed or no mouse is tracked).
int dimouse_button0(void);
// Button i (0..7) in the engine's latest read of its mouse, as dimouse_button0 (the Aimbot button: aimbot.h).
int dimouse_button(int i);
int dimouse_engine_wants(void);
// Once a frame from input_frame: logs the process's raw-input rows on each change, from the first mouse on.
void dimouse_frame(void);

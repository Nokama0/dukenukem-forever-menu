#pragma once
// The per-frame path, called from d3dcap's device detours on the thread that presents.
struct IDirect3DDevice9;

// Before the real Present: hitch and frame lines, then our drawing inside our own BeginScene/EndScene.
void frame_on_present(IDirect3DDevice9* dev);
// After the real Present: the stall watchdog's frame end.
void frame_after_present(void);
// Around the real Reset of the captured device.
void frame_on_reset_before(void);
void frame_on_reset_after(bool ok);
// Before a CreateDevice while a device is captured: releases our objects on the old device, so the
// game's last Release can destroy it (each of our objects holds a reference on it).
void frame_on_device_replaced(void);

// From np_first_call: the thread of the first forwarded winmm call, for the thread log.
void frame_note_first_call_tid(unsigned long tid);
// The thread of the first Present of the captured device (0 before): the game's main thread, which the
// hooks that keep plain statics check themselves against.
unsigned long frame_thread_id(void);

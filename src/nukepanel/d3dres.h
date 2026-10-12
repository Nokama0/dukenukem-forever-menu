#pragma once
// Which of our D3D objects may exist on the captured device. Reset demands the state block go (it is device state)
// while a managed-pool texture survives; after a failed Reset nothing is drawn until one succeeds. stateBlock means a
// frame may create one: it lives only from prim_begin to prim_end.
struct D3dRes { bool stateBlock, texture; };

inline D3dRes d3dres_init(void) { D3dRes r = {false, false}; return r; }
inline D3dRes d3dres_on_created(D3dRes r) { r.stateBlock = true; r.texture = true; return r; }
inline D3dRes d3dres_on_reset_before(D3dRes r) { r.stateBlock = false; return r; }
inline D3dRes d3dres_on_reset_after(D3dRes r, bool ok) { if (ok) r.stateBlock = true; return r; }
// The game replaces its device: everything of ours on the old one is released (each object holds a
// device reference, so keeping them would keep the old device alive); a new device starts empty.
inline D3dRes d3dres_on_device_replaced(D3dRes r) { r.stateBlock = false; r.texture = false; return r; }

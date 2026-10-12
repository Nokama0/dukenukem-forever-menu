#pragma once
// The panel on the frame path: fills the menu's input from the WndProc subclass,
// draws the menu and the software cursor, saves changed settings.

// Frame path only, after input_frame and after config_load (frame.cpp's first frame). Draws through the canvas
// on the device canvas_device was given this frame.
void panel_frame(int W, int H);

// Something to draw this frame (the panel open or still animating shut): frame.cpp opens a scene only then.
bool panel_wants_draw(void);

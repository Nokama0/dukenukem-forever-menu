#pragma once
// Main thread, frame path (frame.cpp), after world_frame and before the panel: enemy (and ally) boxes, health bars,
// names and distance. Draws nothing without a level and a player view.
void esp_frame(int W, int H);

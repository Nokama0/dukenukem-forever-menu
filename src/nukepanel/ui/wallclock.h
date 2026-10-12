// Verbatim from Megaton's src/duke3dmod/wallclock.h; kept identical on purpose -- do not edit here.
#pragma once

// A free-running millisecond counter for the render and tic clocks. UNSIGNED AND IT WRAPS, by design: consumers measure
// an AGE as the unsigned difference `now - t0`, exact across the wrap, so no epoch is needed. wallclock.cpp is on the
// DLL compile line only (one performance-counter call); tests hand their own `now` to the pure rules.
unsigned wallclock_ms();

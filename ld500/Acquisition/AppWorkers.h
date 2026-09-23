#pragma once

// Windows-specific background thread trampolines that periodically tick a RadarGridModel's
// decay/fresh-marker upkeep. Kept separate from SerialPort.h since these aren't acquisition I/O.

#include <windows.h>

class RadarGridModel;

// Background thread: continuously decays the intensity grid (lpParam is a RadarGridModel*) so
// stale cells fade to zero.
DWORD WINAPI DecayThread(LPVOID lpParam);

// Background thread: counts down the new-detection marker (lpParam is a RadarGridModel*) at a
// steady, paint-independent pace.
DWORD WINAPI FreshMarkerDecayThread(LPVOID lpParam);

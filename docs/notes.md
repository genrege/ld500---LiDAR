# Refactor notes: split ld500.cpp into logical modules

Goal: separate data acquisition, captured data model, rendering, and UI; keep Windows-specific
code distinct from portable code, since this may be ported to a microcontroller eventually.

## Status
- [x] Acquisition/RadarTypes.h, RadarProtocol.h/.cpp (portable protocol parsing)
- [x] Model/RadarGridModel.h/.cpp, ObjectTracking.h/.cpp (portable data model + tracking)
- [x] Acquisition/SerialPort.h/.cpp, AppWorkers.h/.cpp (Windows-specific acquisition threads)
- [x] Rendering/RadarRenderer.h/.cpp (Windows GDI rendering)
- [x] UI/Dialogs.h/.cpp, MainWindow.h/.cpp (Windows UI)
- [x] Rewrite ld500.cpp down to WinMain wiring only
- [x] Update ld500.vcxproj / ld500.vcxproj.filters to add the new files + filter groups
- [x] Build (Debug|x64 and Release|x64) - both succeed with 0 errors via msbuild
- [ ] Manual smoke test (serial connect, zoom slider, tracking toggle, Manage Ports, About)

## Target module map
Portable (no windows.h, candidates to reuse on a microcontroller port):
- `Acquisition/RadarTypes.h` - RadarReading, LdPoint, LdPacketLayout (#pragma pack), LD_PACKET_SIZE, PI.
- `Acquisition/RadarProtocol.h/.cpp` - CalcCrc8, ParseRadarStream.
- `Model/RadarGridModel.h/.cpp` - GRID_SIZE + decay/zoom constants, `RadarGridModel` class wrapping the
  intensity/persistence/fresh grids + mutex; IngestReadings(), TickDecay(), TickFreshMarkers(),
  SetZoomMeters()/GetZoomMeters(), GetResetGeneration(), Snapshot().
  **TODO (flagged in-code, not yet implemented):** `IngestReadings()` calls `cos()`/`sin()` per point
  in double precision. Before a microcontroller port (no hardware FP64), precompute a sin/cos lookup
  table keyed to the sensor's discrete angle output resolution (LD500 start/end angle fields are
  0.01-degree units) instead of calling `cos()`/`sin()` at runtime.
- `Model/ObjectTracking.h/.cpp` - TrackedObject struct, tracking tuning constants, `ObjectTracker` class
  (persistent track IDs across frames via connected-component clustering + greedy nearest-centroid match).

Windows-specific:
- `Acquisition/SerialPort.h/.cpp` - ComPortInfo, EnumerateComPorts, OpenAndConfigureSerialPort,
  ResetComPort, g_hSerial/g_ComPortName/g_BaudRate/g_ComSettingsMutex, g_KeepRunning, SerialReadThread
  (bridges bytes -> RadarProtocol::ParseRadarStream -> RadarGridModel::IngestReadings).
- `Acquisition/AppWorkers.h/.cpp` - DecayThread/FreshMarkerDecayThread trampolines (DWORD WINAPI) that
  just call RadarGridModel tick methods on an interval.
- `Rendering/RadarRenderer.h/.cpp` - off-screen DIB lifecycle (Init/Shutdown), UI font lifecycle,
  LayoutZoomSlider/ZoomFromSliderY/HitTestZoomSlider, and PaintRadar() extracted from WM_PAINT
  (grid->pixel writes, fresh-point markers, track markers, rings/crosshair, HUD text, zoom slider, blit).
- `UI/Dialogs.h/.cpp` - RefreshPortsListBox, PortManagerDlgProc, AboutDlgProc.
- `UI/MainWindow.h/.cpp` - WndProc, g_GridModel/g_Tracker/g_TrackingEnabled, window-class registration;
  calls into RadarRenderer::PaintRadar and Dialogs:: for menu commands.
- `ld500.cpp` - trimmed to WinMain only: open initial port, RadarRenderer::Init, spawn
  SerialReadThread/DecayThread/FreshMarkerDecayThread, create/show MainWindow, message pump, teardown.

## Remaining steps
1. Rewrite `ld500.cpp` down to WinMain wiring only (open port, RadarRenderer::Init, spawn the three
   worker threads passing `&g_GridModel` as lpParam, RegisterMainWindowClass + CreateMainWindow,
   message pump, join threads, RadarRenderer::Shutdown, close port).
2. Update `ld500.vcxproj` and `ld500.vcxproj.filters`:
   - Add `AdditionalIncludeDirectories` entries for `Acquisition;Model;Rendering;UI` so plain
     `#include "X.h"` works across folders.
   - Add ClInclude/ClCompile entries for every new file.
   - Add filter groups mirroring the folders (Acquisition / Model / Rendering / UI).
3. Build Debug|x64 and Release|x64; fix any compile/link errors surfaced by the split.
4. Manual smoke test: serial connect/disconnect, zoom slider drag, tracking toggle, Manage Ports
   dialog, About dialog - behavior should be unchanged from the pre-refactor single-file version.

## Decisions
- Pure file/structure reorganization - no behavior changes in this pass.
- Dead code removed: unused `ID_ZOOM_SLIDER` constant (leftover from an earlier real trackbar
  control, superseded by the custom-drawn slider) was dropped during the split.
- Trig lookup table precache (see TODO above) is recorded as a follow-up only, not implemented here.

## Further follow-ups (not in scope for this refactor)
1. Graphics-primitives abstraction layer: introduce an interface (DrawLine/DrawEllipse/DrawText/
   FillRect/Blit-style) that all UI rendering goes through, with `RadarRenderer.cpp` becoming the
   first (Win32 GDI) implementation behind it. Goal: let a future microcontroller port swap in a
   different backend (e.g. a small LCD/framebuffer driver) without touching `PaintRadar()`'s drawing
   logic. Do this as a later pass once the current file-split lands and builds cleanly.

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
1. Graphics-primitives abstraction layer: DONE for the HUD status line only (per user's scoped-down
   request) - `Rendering/IHudSurface.h` defines `DrawHudText()`, `Rendering/GdiHudSurface.h/.cpp` is
   the Win32 GDI implementation, and `RadarRenderer.cpp`'s HUD block now goes through it instead of
   calling `TextOutW` directly. Everything else in `PaintRadar()` (grid blit, ellipses, lines, zoom
   slider) still calls GDI directly - not abstracted, per user's explicit scope-down.
2. Trig lookup table: DONE - `RadarGridModel.cpp` now has an anonymous-namespace `TrigLookupTable()`
   (36000 entries, one per 0.01-degree step the LD500 can output) built once on first use;
   `IngestReadings()` looks up cos/sin from it via `AngleToLutIndex()` instead of calling cos()/sin()
   per point.

Build not re-verified after these two changes yet (user skipped the msbuild re-run) - do that before
considering this fully done.

## LIDAR orientation offset (new feature)
- `Model/RadarGridModel.h/.cpp`: `SetAngleOffsetDegrees()`/`GetAngleOffsetDegrees()`, applied to every
  reading's angle in `IngestReadings()` before the LUT lookup; wraps to [0, 360) and clears the grids
  on change (shares a new `ResetGridsLocked()` helper with `SetZoomMeters()`).
- `Acquisition/AppSettings.h/.cpp` (new): `LoadAngleOffsetDegrees()`/`SaveAngleOffsetDegrees()`,
  registry-backed at `HKCU\Software\LD500\AngleOffsetDegrees` (REG_DWORD, whole degrees). Saved
  immediately on dialog OK; loaded once in `WinMain` before the acquisition threads start.
- `UI/Dialogs.h/.cpp`: new `SettingsDlgProc` - edit box for the offset, applies to `g_GridModel` and
  persists to the registry on OK.
- New `File > Settings...` menu item (`IDM_SETTINGS`) and `IDD_SETTINGS` dialog template in `ld500.rc`.
- Build not yet re-verified for this feature either (user skipped the queued rebuild) - do that plus
  a manual test (change offset, restart app, confirm it's remembered) before considering this done.

## Radar shadow cast + registry-persisted toggles (new features)
- `Rendering/RadarRenderer.cpp`: `ComputeShadowMask()` flags background cells occluded by a nearer
  object, painted very dark green (`RGB(0,6,0)`, darker than the `RGB(10,16,10)` background) instead
  of the normal background color; `PaintRadar()` takes a `shadowCastEnabled` param.
  - Reworked from an initial ray-marching approach (720 rays) that left angular gaps/moire at larger
    radii - now a gap-free full raster scan: a precomputed per-cell angle-bucket table (built once,
    3600 buckets) plus a per-bucket "nearest hit radius" pass, so every cell is visited exactly once
    with no aliasing. Explicitly clipped to the grid's inscribed circle (`radiusSq <= center^2`) so
    shadows never extend past the circular HUD.
- Menu: `Settings > Enable Tracking` / `Settings > Show Radar Shadow` (moved off the old separate
  Tracking/View menus into one `&Settings` menu alongside `Orientation...`), `IDM_TOGGLE_SHADOW`
  mirrors the Tracking toggle: `g_ShadowCastEnabled` atomic in `UI/MainWindow.h/.cpp`.
- Registry persistence extended to all three toggles/settings (`Acquisition/AppSettings.h/.cpp`,
  `HKCU\Software\LD500`): `AngleOffsetDegrees` (DWORD, whole degrees), `TrackingEnabled` (DWORD 0/1),
  `ShadowCastEnabled` (DWORD 0/1). Tracking/Shadow are saved immediately on menu toggle and loaded
  once in `WinMain` right after `CreateMainWindow`, then reflected in the menu checkmarks.
- Verified: Debug|x64 builds with 0 errors/warnings after the shadow-mask rework.
- Still TODO: manual smoke test (toggle Tracking/Shadow, restart app, confirm state and shadow
  rendering are remembered/correct and no longer patchy).

## Persistence highlighting toggle (new feature)
- `Model/RadarGridModel.h/.cpp`: `SetPersistenceEnabled()`/`GetPersistenceEnabled()` (default true,
  matching prior always-on behavior). When disabled, `IngestReadings()` skips persistence growth
  (cells never trend toward brighter green / get excluded from tracking as "static"), and disabling
  immediately clears `m_PersistenceGrid` so existing highlighting reverts right away. No renderer or
  tracker changes needed - both already just read `snapshot.persistence`, which is now always zero
  when the feature is off.
- Menu: `Settings > Persistence Highlighting` (`IDM_TOGGLE_PERSISTENCE`), same pattern as Tracking/
  Shadow but reads/writes `g_GridModel` directly rather than a separate UI-owned atomic, since
  persistence enablement is inherently model state (like zoom/orientation).
- Registry: `PersistenceEnabled` (DWORD 0/1) in the same `HKCU\Software\LD500` key, defaulting to 1
  (on) when unset - the only one of the four settings that defaults on.
- Verified: Debug|x64 builds with 0 errors/warnings.

- Still TODO: manual smoke test (toggle Tracking/Shadow, restart app, confirm state and shadow
  rendering are remembered/correct and no longer patchy).

## Mouse-wheel zoom + grid size/tracking tuning settings (new features)
- `UI/MainWindow.cpp`: `WM_MOUSEWHEEL` adjusts the zoom slider directly when the cursor is over its
  hit-test region (`RadarRenderer::HitTestZoomSlider`) - 1 meter per wheel notch, clamped to
  `[ZOOM_MIN_METERS, ZOOM_MAX_METERS]`. No registry persistence needed since zoom already isn't saved.
- `Model/RadarGridModel.h/.cpp`: `GRID_SIZE` changed from `constexpr` to a runtime global (default
  1000, was a hardcoded 1200), plus a new `SetGridSizeCells()` that resizes/clears the three grids.
  Applied once in `WinMain` before `RadarRenderer::Init()`, since the renderer's off-screen DIB
  surface is sized once at startup - **changing grid size requires an app restart** to take effect;
  the Settings dialog only persists the new value, it doesn't call `SetGridSizeCells()` live.
- `Model/ObjectTracking.h/.cpp`: the five tracking tuning constants (`MIN_CLUSTER_CELLS`,
  `MAX_MATCH_DIST_CELLS`, `MAX_MISSED_FRAMES`, `MIN_CONFIRM_FRAMES`,
  `MAX_STATIC_PERSISTENCE_FOR_TRACKING`) changed from `constexpr` to mutable globals - these apply
  live on dialog OK since they're just read per-frame in `ObjectTracker::Update()`, no buffer sizing
  implications.
- `Acquisition/AppSettings.h/.cpp`: added Load/Save pairs for all six values above, same
  `HKCU\Software\LD500` registry key (REG_DWORD; the two `double` tracking params round to the
  nearest whole number on save, same lossy-but-fine precision as `AngleOffsetDegrees`).
- `UI/Dialogs.cpp` / `ld500.rc`: `IDD_SETTINGS` dialog grew six more label+edit rows for the new
  values; `SettingsDlgProc` populates them on init and validates/applies/saves them on OK.
- Verified: Debug|x64 and Release|x64 both build with 0 errors.
- Still TODO: manual smoke test (mouse-wheel zoom, edit each new Settings field, restart to confirm
  grid size took effect, confirm tracking behavior changes live without restart).


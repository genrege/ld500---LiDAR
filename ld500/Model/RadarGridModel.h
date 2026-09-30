#pragma once

// Portable captured-data model: the cartesian intensity/persistence/freshness grid that radar
// readings are plotted into. No Windows dependency, so this is a candidate to reuse as-is on a
// future microcontroller port.

#include <cstdint>
#include <vector>
#include <mutex>
#include <atomic>
#include "RadarTypes.h"

// Grid is GRID_SIZE x GRID_SIZE cells. A persisted setting (default 1000); can change live via
// SetGridSizeCells() paired with RadarRenderer::ResizeGridSurface().
extern int GRID_SIZE;
constexpr int CELL_DECAY_INTERVAL_MS = 12;       // How often (ms) each nonzero cell decrements by 1 (~3s full fade)
constexpr double ZOOM_MIN_METERS = 1;          // Nearest zoom range selectable on the slider
constexpr double ZOOM_MAX_METERS = 12.0;         // Farthest zoom range selectable on the slider
constexpr double ZOOM_DEFAULT_METERS = 4.0;      // Initial physical distance (m) spanned by half the grid
constexpr uint8_t STATIC_GROWTH_PER_HIT = 8;     // Persistence gained per re-hit on an already-active cell
constexpr int FRESH_MARKER_FRAMES = 50;          // How many paint frames the green "new detection" marker stays visible

// Read-only copy of the grid state for a single render/tracking pass.
struct RadarGridSnapshot {
    std::vector<uint8_t> intensity;
    std::vector<uint8_t> persistence;
    std::vector<uint8_t> fresh;
    int resetGeneration = 0;
};

// Cartesian occupancy/fade grid that radar readings are plotted into. Owns its own locking so
// acquisition threads, decay threads, and the renderer can all touch it safely.
//
// IngestReadings() looks up cos()/sin() from a table precomputed at construction time (see
// RadarGridModel.cpp), keyed to the sensor's discrete angle output resolution (LD500 start/end
// angle fields are 0.01-degree units), instead of calling cos()/sin() per point at runtime - this
// keeps the hot ingestion path free of floating-point trig calls for a future microcontroller port.
class RadarGridModel {
public:
    RadarGridModel();

    // Plots each in-range reading into the grid at the current zoom scale, growing persistence on
    // repeat hits and arming the fresh-detection marker on cells transitioning from cold to hot.
    void IngestReadings(const std::vector<RadarReading>& readings);

    // Decays every active cell's intensity by one step; cells reaching zero reset persistence/fresh.
    void TickDecay();

    // Counts down the fresh-detection marker on cells that still have one armed.
    void TickFreshMarkers();

    // Updates the zoom scale and, if it actually changed, clears all grids and bumps the reset
    // generation so consumers (e.g. object tracking) know to drop stale state.
    void SetZoomMeters(double zoomMeters);
    double GetZoomMeters() const;

    // Applies a new grid size (cells per side), resizing and clearing all grids. Caller must also
    // call RadarRenderer::ResizeGridSurface() afterward to match the renderer's DIB surface to it.
    void SetGridSizeCells(int cells);

    // Updates the LIDAR mounting/orientation offset (degrees, wraps to [0, 360)) added to every
    // incoming reading's angle before it's plotted. Clears all grids on change like SetZoomMeters()
    // does, since previously plotted points are no longer valid at the new orientation.
    void SetAngleOffsetDegrees(double offsetDegrees);
    double GetAngleOffsetDegrees() const;

    // Enables/disables persistence growth on repeat hits (the effect that trends static objects
    // toward brighter green and excludes them from object tracking). Disabling immediately clears
    // the persistence grid so any existing highlighting reverts right away.
    void SetPersistenceEnabled(bool enabled);
    bool GetPersistenceEnabled() const;

    int GetResetGeneration() const;

    // Thread-safe copy of the grids for a single paint/tracking pass.
    RadarGridSnapshot Snapshot() const;

private:
    // Clears all grids and bumps the reset generation; caller must hold m_Mutex.
    void ResetGridsLocked();

    mutable std::mutex   m_Mutex;
    std::vector<uint8_t>  m_IntensityGrid;
    std::vector<uint8_t>  m_PersistenceGrid;
    std::vector<uint8_t>  m_FreshGrid;
    // Ticks since each cell's last hit, with persistence off: a cell holds full brightness while
    // still within one expected rotation of its last hit, and only starts fading once overdue (see
    // TickDecay) - otherwise a linear per-tick decay makes most of a rotation's sweep imperceptibly
    // dark well before it's actually stale, since color-curve brightness isn't linear in intensity.
    std::vector<uint16_t> m_TicksSinceHit;
    std::atomic<double>  m_ZoomMeters;
    std::atomic<double>  m_AngleOffsetDegrees;
    std::atomic<bool>    m_PersistenceEnabled;
    std::atomic<int>     m_ResetGeneration;
    // Latest known full-rotation duration (ms), derived from readings' reported motor speed; used
    // to size persistence-off decay to one rotation instead of a fixed fast halving (see TickDecay).
    std::atomic<double>  m_RotationPeriodMs;
};

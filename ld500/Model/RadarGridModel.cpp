#include "RadarGridModel.h"
#include <cmath>
#include <algorithm>

RadarGridModel::RadarGridModel()
    : m_IntensityGrid(static_cast<size_t>(GRID_SIZE)* GRID_SIZE, 0)
    , m_PersistenceGrid(static_cast<size_t>(GRID_SIZE)* GRID_SIZE, 0)
    , m_FreshGrid(static_cast<size_t>(GRID_SIZE)* GRID_SIZE, 0)
    , m_ZoomMeters(ZOOM_DEFAULT_METERS)
    , m_ResetGeneration(0) {
}

void RadarGridModel::IngestReadings(const std::vector<RadarReading>& readings) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    double currentMaxDistanceMm = m_ZoomMeters.load(std::memory_order_relaxed) * 1000.0;

    for (const auto& reading : readings) {
        if (reading.isOutOfRange || reading.distanceMm == 0) continue;

        // Convert polar reading into cartesian grid cell coordinates and mark it hot.
        double rad = (reading.angleDegrees - 90.0) * PI / 180.0; // Align 0-degree vector north
        double normDist = static_cast<double>(reading.distanceMm) / currentMaxDistanceMm;
        if (normDist > 1.0) normDist = 1.0;

        int gx = static_cast<int>(GRID_SIZE / 2 + cos(rad) * normDist * (GRID_SIZE / 2));
        int gy = static_cast<int>(GRID_SIZE / 2 + sin(rad) * normDist * (GRID_SIZE / 2));
        if (gx < 0) gx = 0; else if (gx >= GRID_SIZE) gx = GRID_SIZE - 1;
        if (gy < 0) gy = 0; else if (gy >= GRID_SIZE) gy = GRID_SIZE - 1;

        int cellIndex = gy * GRID_SIZE + gx;

        // Track how long this cell has been continuously re-hit (static objects
        // stay hot and grow persistence; a cell that fully decayed starts fresh).
        if (m_IntensityGrid[cellIndex] > 0) {
            int grown = static_cast<int>(m_PersistenceGrid[cellIndex]) + STATIC_GROWTH_PER_HIT;
            m_PersistenceGrid[cellIndex] = static_cast<uint8_t>(grown > 255 ? 255 : grown);
        }
        else {
            m_PersistenceGrid[cellIndex] = 0;
            // Genuine transition from no point to a point: hold the green "new
            // detection" marker for a fixed number of paint frames so it renders
            // as a steady circle instead of flickering with the fast decay tick.
            m_FreshGrid[cellIndex] = FRESH_MARKER_FRAMES;
        }

        m_IntensityGrid[cellIndex] = 255;
    }
}

void RadarGridModel::TickDecay() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    for (size_t i = 0; i < m_IntensityGrid.size(); ++i) {
        if (m_IntensityGrid[i] > 0) {
            --m_IntensityGrid[i];
            if (m_IntensityGrid[i] == 0) {
                m_PersistenceGrid[i] = 0; // Object is gone; next hit here starts fresh (red)
                m_FreshGrid[i] = 0;
            }
        }
    }
}

void RadarGridModel::TickFreshMarkers() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    for (size_t i = 0; i < m_FreshGrid.size(); ++i) {
        if (m_FreshGrid[i] > 0) {
            --m_FreshGrid[i];
        }
    }
}

void RadarGridModel::SetZoomMeters(double zoomMeters) {
    double oldZoomMeters = m_ZoomMeters.exchange(zoomMeters, std::memory_order_relaxed);
    if (oldZoomMeters != zoomMeters) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        std::fill(m_IntensityGrid.begin(), m_IntensityGrid.end(), 0);
        std::fill(m_PersistenceGrid.begin(), m_PersistenceGrid.end(), 0);
        std::fill(m_FreshGrid.begin(), m_FreshGrid.end(), 0);
        m_ResetGeneration.fetch_add(1, std::memory_order_relaxed);
    }
}

double RadarGridModel::GetZoomMeters() const {
    return m_ZoomMeters.load(std::memory_order_relaxed);
}

int RadarGridModel::GetResetGeneration() const {
    return m_ResetGeneration.load(std::memory_order_relaxed);
}

RadarGridSnapshot RadarGridModel::Snapshot() const {
    RadarGridSnapshot snapshot;
    std::lock_guard<std::mutex> lock(m_Mutex);
    snapshot.intensity = m_IntensityGrid;
    snapshot.persistence = m_PersistenceGrid;
    snapshot.fresh = m_FreshGrid;
    snapshot.resetGeneration = m_ResetGeneration.load(std::memory_order_relaxed);
    return snapshot;
}

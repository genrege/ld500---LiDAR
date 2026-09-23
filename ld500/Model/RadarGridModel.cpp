#include "RadarGridModel.h"
#include <cmath>
#include <algorithm>
#include <array>

namespace {
    // Sensor angle output resolution: LD500 start/end angle fields are integers in 0.01-degree
    // units, so the lookup table below has one entry per possible 0.01-degree step.
    constexpr int ANGLE_LUT_STEPS = 36000;
    constexpr int QUARTER_TURN_STEPS = ANGLE_LUT_STEPS / 4; // 90 degrees worth of steps

    // Precomputed cos of the grid-aligned angle for every discrete step the sensor can output,
    // built once on first use so IngestReadings() never needs a runtime cos()/sin() call. sin()
    // isn't stored separately: sin(x) == cos(x - 90 degrees), so it's derived by indexing this same
    // table a quarter turn earlier, halving the table's memory footprint.
    const std::array<double, ANGLE_LUT_STEPS>& CosLookupTable() {
        static const std::array<double, ANGLE_LUT_STEPS> table = [] {
            std::array<double, ANGLE_LUT_STEPS> t{};
            for (int i = 0; i < ANGLE_LUT_STEPS; ++i) {
                double angleDegrees = static_cast<double>(i) / 100.0;
                double rad = (angleDegrees - 90.0) * PI / 180.0; // Align 0-degree vector north
                t[i] = std::cos(rad);
            }
            return t;
            }();
        return table;
    }

    // Snaps a decoded angle (degrees, [0, 360)) to its nearest 0.01-degree lookup table index.
    int AngleToLutIndex(double angleDegrees) {
        int index = static_cast<int>(std::lround(angleDegrees * 100.0)) % ANGLE_LUT_STEPS;
        if (index < 0) index += ANGLE_LUT_STEPS;
        return index;
    }

    // sin(x) == cos(x - 90 degrees): wrap a cos-table index back a quarter turn instead of keeping
    // a second table.
    double SinFromCosTable(int cosIndex) {
        int sinIndex = (cosIndex - QUARTER_TURN_STEPS) % ANGLE_LUT_STEPS;
        if (sinIndex < 0) sinIndex += ANGLE_LUT_STEPS;
        return CosLookupTable()[sinIndex];
    }
}

RadarGridModel::RadarGridModel()
    : m_IntensityGrid(static_cast<size_t>(GRID_SIZE)* GRID_SIZE, 0)
    , m_PersistenceGrid(static_cast<size_t>(GRID_SIZE)* GRID_SIZE, 0)
    , m_FreshGrid(static_cast<size_t>(GRID_SIZE)* GRID_SIZE, 0)
    , m_ZoomMeters(ZOOM_DEFAULT_METERS)
    , m_AngleOffsetDegrees(0.0)
    , m_ResetGeneration(0) {
}

void RadarGridModel::IngestReadings(const std::vector<RadarReading>& readings) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    double currentMaxDistanceMm = m_ZoomMeters.load(std::memory_order_relaxed) * 1000.0;
    double offsetDegrees = m_AngleOffsetDegrees.load(std::memory_order_relaxed);

    for (const auto& reading : readings) {
        if (reading.isOutOfRange || reading.distanceMm == 0) continue;

        // Convert polar reading into cartesian grid cell coordinates and mark it hot.
        double adjustedAngle = reading.angleDegrees + offsetDegrees;
        if (adjustedAngle >= 360.0) adjustedAngle -= 360.0;
        else if (adjustedAngle < 0.0) adjustedAngle += 360.0;
        int angleIndex = AngleToLutIndex(adjustedAngle);
        double cosValue = CosLookupTable()[angleIndex];
        double sinValue = SinFromCosTable(angleIndex);
        double normDist = static_cast<double>(reading.distanceMm) / currentMaxDistanceMm;
        if (normDist > 1.0) normDist = 1.0;

        int gx = static_cast<int>(GRID_SIZE / 2 + cosValue * normDist * (GRID_SIZE / 2));
        int gy = static_cast<int>(GRID_SIZE / 2 + sinValue * normDist * (GRID_SIZE / 2));
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
        ResetGridsLocked();
    }
}

double RadarGridModel::GetZoomMeters() const {
    return m_ZoomMeters.load(std::memory_order_relaxed);
}

void RadarGridModel::SetAngleOffsetDegrees(double offsetDegrees) {
    double wrapped = std::fmod(offsetDegrees, 360.0);
    if (wrapped < 0.0) wrapped += 360.0;
    double oldOffset = m_AngleOffsetDegrees.exchange(wrapped, std::memory_order_relaxed);
    if (oldOffset != wrapped) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        ResetGridsLocked();
    }
}

double RadarGridModel::GetAngleOffsetDegrees() const {
    return m_AngleOffsetDegrees.load(std::memory_order_relaxed);
}

void RadarGridModel::ResetGridsLocked() {
    std::fill(m_IntensityGrid.begin(), m_IntensityGrid.end(), 0);
    std::fill(m_PersistenceGrid.begin(), m_PersistenceGrid.end(), 0);
    std::fill(m_FreshGrid.begin(), m_FreshGrid.end(), 0);
    m_ResetGeneration.fetch_add(1, std::memory_order_relaxed);
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

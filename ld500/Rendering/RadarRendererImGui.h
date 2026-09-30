#pragma once

// Dear ImGui / OpenGL rendering for the ImGui shell: a 1:1-in-spirit port of the GDI
// RadarRenderer. The grid itself is drawn by uploading the same per-cell pixel buffer the GDI
// version builds into an OpenGL texture (the equivalent of GDI's DIB section + StretchBlt);
// everything else (fresh/track markers, rings, HUD, zoom slider) uses ImDrawList primitives.

#include "RadarGridModel.h"
#include "ObjectTracking.h"

namespace RadarRendererImGui {
    // Creates the grid texture. Call once before the first frame.
    void Init();
    // Releases the grid texture. Call once during application shutdown.
    void Shutdown();

    // Recreates the grid texture at the current GRID_SIZE. Call after
    // RadarGridModel::SetGridSizeCells() changes the grid size while the app is running.
    void ResizeGridSurface();

    // Repositions the zoom slider's track rectangle for the current display size.
    void LayoutZoomSlider();
    // Converts a Y pixel coordinate within the slider track into a zoom value (top = max, bottom = min).
    double ZoomFromSliderY(float y);
    // True if (x, y) falls within the slider's track rectangle, expanded by a small grab margin.
    bool HitTestZoomSlider(float x, float y);

    // Renders one full frame onto ImGui's background draw list (grid, fresh/track markers,
    // rings, HUD, zoom slider). portName/baudRate/isConnected describe the active ISerialPort
    // connection (owned by the caller) and are folded into the HUD status line along with the
    // visible-point count computed during this same pass.
    void PaintRadar(RadarGridModel& model, ObjectTracker& tracker,
                     bool trackingEnabled, bool shadowCastEnabled,
                     const char* portName, uint32_t baudRate, bool isConnected);
}

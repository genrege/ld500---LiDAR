#pragma once

// Windows-specific GDI rendering: turns a RadarGridModel snapshot + ObjectTracker tracks into
// pixels. Owns the off-screen DIB grid surface, the UI font, and the custom zoom slider control.

#include <windows.h>
#include "RadarGridModel.h"
#include "ObjectTracking.h"

namespace RadarRenderer {
    constexpr double BACKGROUND_INTENSITY_MIN = 0.0;   // Nearest intensity selectable on the slider
    constexpr double BACKGROUND_INTENSITY_MAX = 2.0;   // Farthest intensity selectable on the slider

    // Creates the off-screen DIB grid surface and UI font. Call once before the first paint.
    void Init();
    // Releases the DIB surface and UI font. Call once during application shutdown.
    void Shutdown();

    // Recreates the off-screen DIB grid surface at the current GRID_SIZE. Call after
    // RadarGridModel::SetGridSizeCells() changes the grid size while the app is running.
    void ResizeGridSurface();

    // Repositions the zoom slider's track rectangle for the window's current client size.
    void LayoutZoomSlider(HWND hwnd);
    // Converts a Y pixel coordinate within the slider track into a zoom value (top = max, bottom = min).
    double ZoomFromSliderY(int y);
    // True if (x, y) falls within the slider's track rectangle, expanded by a small grab margin.
    bool HitTestZoomSlider(int x, int y);

    // Repositions the background intensity slider's track rectangle for the window's current client size.
    void LayoutIntensitySlider(HWND hwnd);
    // Converts an X pixel coordinate within the slider track into an intensity multiplier (left = min, right = max).
    double IntensityFromSliderX(int x);
    // True if (x, y) falls within the intensity slider's track rectangle, expanded by a small grab margin.
    bool HitTestIntensitySlider(int x, int y);
    // Gets/sets the brightness multiplier applied to non-shadow empty-cell background color.
    double GetBackgroundIntensity();
    void SetBackgroundIntensity(double intensity);

    // Renders one full frame (grid, fresh/track markers, rings, HUD, zoom slider) into hdc.
    // shadowCastEnabled shades cells occluded from the sensor by a nearer object very dark green.
    void PaintRadar(HDC hdc, HWND hwnd, RadarGridModel& model, ObjectTracker& tracker, bool trackingEnabled, bool shadowCastEnabled);
}

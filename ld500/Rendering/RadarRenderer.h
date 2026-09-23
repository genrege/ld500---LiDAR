#pragma once

// Windows-specific GDI rendering: turns a RadarGridModel snapshot + ObjectTracker tracks into
// pixels. Owns the off-screen DIB grid surface, the UI font, and the custom zoom slider control.

#include <windows.h>
#include "RadarGridModel.h"
#include "ObjectTracking.h"

namespace RadarRenderer {
    // Creates the off-screen DIB grid surface and UI font. Call once before the first paint.
    void Init();
    // Releases the DIB surface and UI font. Call once during application shutdown.
    void Shutdown();

    // Repositions the zoom slider's track rectangle for the window's current client size.
    void LayoutZoomSlider(HWND hwnd);
    // Converts a Y pixel coordinate within the slider track into a zoom value (top = max, bottom = min).
    double ZoomFromSliderY(int y);
    // True if (x, y) falls within the slider's track rectangle, expanded by a small grab margin.
    bool HitTestZoomSlider(int x, int y);

    // Renders one full frame (grid, fresh/track markers, rings, HUD, zoom slider) into hdc.
    void PaintRadar(HDC hdc, HWND hwnd, RadarGridModel& model, ObjectTracker& tracker, bool trackingEnabled);
}

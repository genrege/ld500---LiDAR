#include "RadarRenderer.h"
#include <cmath>
#include <tuple>
#include <vector>
#include <mutex>
#include <climits>
#include "SerialPort.h"
#include "GdiHudSurface.h"

namespace {
    // Off-screen DIB section used to write grid pixel colors directly, then blit onto the display buffer
    HDC      s_hGridDC = NULL;
    HBITMAP  s_hGridBitmap = NULL;
    uint8_t* s_pGridBits = nullptr;

    HFONT s_hUiFont = NULL; // Arial, used for all on-screen text

    // Custom-drawn (non-common-control) vertical zoom slider state: an open green frame with white
    // tick marks and a green thumb, tracked entirely via mouse messages in the caller's WndProc.
    RECT s_ZoomSliderRect = { 0, 0, 0, 0 };

    constexpr int SHADOW_ANGLE_BUCKETS = 180; // 2-degree buckets

    // Precomputed angle bucket for every grid cell relative to the grid center, rebuilt whenever
    // GRID_SIZE changes (live grid-size updates) so ComputeShadowMask() below never needs a
    // per-frame atan2() call.
    const std::vector<uint16_t>& ShadowAngleBucketTable() {
        static std::vector<uint16_t> table;
        static int tableGridSize = -1;
        if (tableGridSize == GRID_SIZE) return table;

        table.assign(static_cast<size_t>(GRID_SIZE) * GRID_SIZE, 0);
        const int center = GRID_SIZE / 2;
        for (int y = 0; y < GRID_SIZE; ++y) {
            for (int x = 0; x < GRID_SIZE; ++x) {
                double angle = std::atan2(static_cast<double>(y - center), static_cast<double>(x - center));
                if (angle < 0.0) angle += 2.0 * PI;
                int bucket = static_cast<int>(angle / (2.0 * PI) * SHADOW_ANGLE_BUCKETS);
                if (bucket >= SHADOW_ANGLE_BUCKETS) bucket = SHADOW_ANGLE_BUCKETS - 1;
                table[static_cast<size_t>(y) * GRID_SIZE + x] = static_cast<uint16_t>(bucket);
            }
        }
        tableGridSize = GRID_SIZE;
        return table;
    }

    // Marks background cells occluded from the sensor by a nearer object: a 2D LIDAR only reports
    // the first return along each ray, so anything past that hit is unknown, not empty. For every
    // angular bucket, finds the nearest hit's radius and shades every farther background cell in
    // that bucket - a full raster scan rather than ray-marching, so there are no angular gaps
    // (moire/patchiness) at larger radii. Explicitly clipped to the grid's inscribed circle so
    // shadows never bleed into the square grid's corners, outside the circular HUD.
    std::vector<uint8_t> ComputeShadowMask(const std::vector<uint8_t>& intensityGrid) {
        const int center = GRID_SIZE / 2;
        const int maxRadiusSq = center * center;
        const std::vector<uint16_t>& angleBucket = ShadowAngleBucketTable();

        std::vector<int> nearestHitRadiusSq(SHADOW_ANGLE_BUCKETS, INT_MAX);
        for (int y = 0; y < GRID_SIZE; ++y) {
            int dy = y - center;
            for (int x = 0; x < GRID_SIZE; ++x) {
                size_t cellIndex = static_cast<size_t>(y) * GRID_SIZE + x;
                if (intensityGrid[cellIndex] == 0) continue;
                int dx = x - center;
                int radiusSq = dx * dx + dy * dy;
                if (radiusSq > maxRadiusSq) continue;
                uint16_t bucket = angleBucket[cellIndex];
                if (radiusSq < nearestHitRadiusSq[bucket]) nearestHitRadiusSq[bucket] = radiusSq;
            }
        }

        std::vector<uint8_t> shadowMask(static_cast<size_t>(GRID_SIZE) * GRID_SIZE, 0);
        for (int y = 0; y < GRID_SIZE; ++y) {
            int dy = y - center;
            for (int x = 0; x < GRID_SIZE; ++x) {
                size_t cellIndex = static_cast<size_t>(y) * GRID_SIZE + x;
                if (intensityGrid[cellIndex] != 0) continue;
                int dx = x - center;
                int radiusSq = dx * dx + dy * dy;
                if (radiusSq > maxRadiusSq) continue;
                if (radiusSq > nearestHitRadiusSq[angleBucket[cellIndex]]) {
                    shadowMask[cellIndex] = 1;
                }
            }
        }
        return shadowMask;
    }

    // Returns the thumb's center Y pixel coordinate for the current zoom value.
    int SliderYFromZoom(double zoomMeters) {
        int top = s_ZoomSliderRect.top;
        int bottom = s_ZoomSliderRect.bottom;
        double t = (ZOOM_MAX_METERS > ZOOM_MIN_METERS)
            ? (ZOOM_MAX_METERS - zoomMeters) / (ZOOM_MAX_METERS - ZOOM_MIN_METERS)
            : 0.0;
        return top + static_cast<int>(t * (bottom - top));
    }

    // Draws the open green-frame zoom slider with white tick marks and a green thumb, plus its
    // current-value label, directly onto the memory DC during WM_PAINT.
    void DrawZoomSlider(HDC hdcMem, double zoomMeters) {
        const RECT& r = s_ZoomSliderRect;

        HFONT hOldSliderFont = (HFONT)SelectObject(hdcMem, s_hUiFont);

        HPEN hFramePen = CreatePen(PS_SOLID, 1, RGB(0, 255, 0));
        HGDIOBJ hOldPen = SelectObject(hdcMem, hFramePen);
        HGDIOBJ hOldBrush = SelectObject(hdcMem, GetStockObject(NULL_BRUSH));
        Rectangle(hdcMem, r.left, r.top, r.right, r.bottom);

        // White tick marks at every whole metre along the track, labeled to the right of the track.
        SetTextColor(hdcMem, RGB(255, 255, 255));
        SetBkMode(hdcMem, TRANSPARENT);
        HPEN hTickPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
        SelectObject(hdcMem, hTickPen);
        int firstTick = static_cast<int>(std::ceil(ZOOM_MIN_METERS));
        int lastTick = static_cast<int>(std::floor(ZOOM_MAX_METERS));
        for (int m = firstTick; m <= lastTick; ++m) {
            int y = SliderYFromZoom(static_cast<double>(m));
            MoveToEx(hdcMem, r.right, y, NULL);
            LineTo(hdcMem, r.right + 5, y);
            wchar_t tickLabel[8];
            swprintf_s(tickLabel, L"%d", m);
            TextOutW(hdcMem, r.right + 9, y - 7, tickLabel, lstrlenW(tickLabel));
        }
        DeleteObject(hTickPen);

        // Green thumb showing the current zoom position.
        int thumbY = SliderYFromZoom(zoomMeters);
        int thumbHalfHeight = 5;
        HBRUSH hThumbBrush = CreateSolidBrush(RGB(0, 255, 0));
        SelectObject(hdcMem, hThumbBrush);
        Rectangle(hdcMem, r.left - 4, thumbY - thumbHalfHeight, r.right + 4, thumbY + thumbHalfHeight);
        DeleteObject(hThumbBrush);

        wchar_t valueLabel[16];
        swprintf_s(valueLabel, L"%.1fm", zoomMeters);
        TextOutW(hdcMem, r.right + 9, r.bottom + 6, valueLabel, lstrlenW(valueLabel));

        SelectObject(hdcMem, hOldBrush);
        SelectObject(hdcMem, hOldPen);
        DeleteObject(hFramePen);
        SelectObject(hdcMem, hOldSliderFont);
    }
}

namespace RadarRenderer {

    void Init() {
        s_hUiFont = CreateFontW(14, 0, 0, 0, FW_LIGHT, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");

        s_hGridDC = CreateCompatibleDC(NULL);
        ResizeGridSurface();
    }

    void Shutdown() {
        if (s_hUiFont) { DeleteObject(s_hUiFont); s_hUiFont = NULL; }
        if (s_hGridBitmap) { DeleteObject(s_hGridBitmap); s_hGridBitmap = NULL; }
        if (s_hGridDC) { DeleteDC(s_hGridDC); s_hGridDC = NULL; }
    }

    // (Re)creates the off-screen DIB section at the current GRID_SIZE, dropping any previous one.
    void ResizeGridSurface() {
        if (s_hGridBitmap) { DeleteObject(s_hGridBitmap); s_hGridBitmap = NULL; s_pGridBits = nullptr; }

        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = GRID_SIZE;
        bmi.bmiHeader.biHeight = -GRID_SIZE; // Negative = top-down DIB, matches screen Y orientation
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;

        s_hGridBitmap = CreateDIBSection(s_hGridDC, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&s_pGridBits), NULL, 0);
        SelectObject(s_hGridDC, s_hGridBitmap);
    }

    void LayoutZoomSlider(HWND hwnd) {
        RECT rc; GetClientRect(hwnd, &rc);
        const int sliderWidth = 6;
        const int rightMargin = 50; // Leaves room for the tick labels drawn to the right of the track
        int clientHeight = rc.bottom - rc.top;
        int sliderHeight = clientHeight / 2;

        s_ZoomSliderRect.right = (rc.right - rc.left) - rightMargin;
        s_ZoomSliderRect.left = s_ZoomSliderRect.right - sliderWidth;
        s_ZoomSliderRect.top = (clientHeight - sliderHeight) / 2;
        s_ZoomSliderRect.bottom = s_ZoomSliderRect.top + sliderHeight;
    }

    double ZoomFromSliderY(int y) {
        int top = s_ZoomSliderRect.top;
        int bottom = s_ZoomSliderRect.bottom;
        if (y < top) y = top;
        if (y > bottom) y = bottom;
        double t = (bottom > top) ? static_cast<double>(y - top) / (bottom - top) : 0.0;
        return ZOOM_MAX_METERS - t * (ZOOM_MAX_METERS - ZOOM_MIN_METERS);
    }

    bool HitTestZoomSlider(int x, int y) {
        RECT hitRect = s_ZoomSliderRect;
        hitRect.left -= 10; hitRect.right += 10; hitRect.top -= 10; hitRect.bottom += 10;
        POINT pt = { x, y };
        return PtInRect(&hitRect, pt) != FALSE;
    }

    void PaintRadar(HDC hdc, HWND hwnd, RadarGridModel& model, ObjectTracker& tracker, bool trackingEnabled, bool shadowCastEnabled) {
        // Double buffering layer instantiation to block window monitor screen flickers
        RECT rect;
        GetClientRect(hwnd, &rect);
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbmMem = CreateCompatibleBitmap(hdc, width + 1, height + 1);
        HANDLE hOld = SelectObject(hdcMem, hbmMem);
        HFONT hOldFont = (HFONT)SelectObject(hdcMem, s_hUiFont); // Arial for all text drawn this frame

        // Configure dynamic scalar center nodes
        int centerX = width / 2;
        int centerY = height / 2;
        int maxRadius = (min(width, height) / 2) - 40;

        RadarGridSnapshot snapshot = model.Snapshot();
        tracker.Update(snapshot, trackingEnabled);
        const std::vector<TrackedObject>& tracks = tracker.Tracks();

        // Write pixel colors directly into the off-screen DIB buffer. Fresh hits render bright
        // green, drifting through grey, and finally fading to black as intensity decays. Cells
        // that are repeatedly re-hit (static, non-moving objects) trend toward brighter green instead.
        // Cells that are newly detected (freshness countdown still active) are drawn separately
        // as a steady green marker circle instead, so their grid pixel is left as background here.
        int visiblePointCount = 0;
        std::vector<std::tuple<int, int, uint8_t>> freshPoints;
        // Marks cells that are newly detected this frame (transition from no point to a point),
        // used to suppress duplicate markers for cells belonging to the same physical cluster.
        std::vector<uint8_t> freshMask(static_cast<size_t>(GRID_SIZE) * GRID_SIZE, 0);
        std::vector<uint8_t> shadowMask;
        if (shadowCastEnabled) {
            shadowMask = ComputeShadowMask(snapshot.intensity);
        }
        const int stride = GRID_SIZE * 3; // 24bpp, GRID_SIZE*3 is already 4-byte aligned
        const int gridCenter = GRID_SIZE / 2;
        const int gridMaxRadiusSq = gridCenter * gridCenter; // circular HUD boundary in grid space
        for (int y = 0; y < GRID_SIZE; ++y) {
            uint8_t* row = s_pGridBits + static_cast<size_t>(y) * stride;
            int dy = y - gridCenter;
            for (int x = 0; x < GRID_SIZE; ++x) {
                size_t cellIndex = static_cast<size_t>(y) * GRID_SIZE + x;
                uint8_t intensity = snapshot.intensity[cellIndex];
                BYTE r, g, b;
                if (intensity == 0) {
                    int dx = x - gridCenter;
                    if (dx * dx + dy * dy > gridMaxRadiusSq) {
                        r = 0; g = 0; b = 0; // Outside the circular HUD: solid black, not the greenish background
                    }
                    else if (shadowCastEnabled && shadowMask[cellIndex]) {
                        r = 0; g = 6; b = 0; // Very dark green, darker than the plain background: occluded from the sensor
                    }
                    else {
                        r = 10; g = 16; b = 10; // Background color
                    }
                }
                // A cell still counting down its freshness marker is a recent new detection (transition
                // from no point to a point); it holds steady for several frames instead of flickering.
                else if (snapshot.fresh[cellIndex] > 0) {
                    r = 10; g = 16; b = 10; // Background color here; drawn as a green circle below
                    freshMask[cellIndex] = 1;

                    // Only place a marker if none of the already-scanned neighbours (up, left,
                    // upper-left, upper-right) are also part of this same new-detection cluster,
                    // so one contiguous blob of newly detected cells gets a single green circle.
                    bool hasEarlierNeighbourInCluster = false;
                    for (int dy = -1; dy <= 0 && !hasEarlierNeighbourInCluster; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dy == 0 && dx >= 0) continue; // only cells already scanned in raster order
                            int nx = x + dx, ny = y + dy;
                            if (nx < 0 || nx >= GRID_SIZE || ny < 0 || ny >= GRID_SIZE) continue;
                            if (freshMask[static_cast<size_t>(ny) * GRID_SIZE + nx]) {
                                hasEarlierNeighbourInCluster = true;
                                break;
                            }
                        }
                    }
                    if (!hasEarlierNeighbourInCluster) {
                        freshPoints.emplace_back(x, y, snapshot.fresh[cellIndex]);
                    }
                    ++visiblePointCount;
                }
                else {
                    // Dynamic fade path: green (fresh) -> grey (mid decay) -> black (fully decayed)
                    double t = 1.0 - (static_cast<double>(intensity) / 255.0);
                    double dr, dg, db;
                    if (t < 0.5) {
                        double localT = t / 0.5;
                        dr = 0.0 + localT * (128.0 - 0.0);
                        dg = 255.0 + localT * (128.0 - 255.0);
                        db = 0.0 + localT * (128.0 - 0.0);
                    }
                    else {
                        double localT = (t - 0.5) / 0.5;
                        dr = 128.0 + localT * (0.0 - 128.0);
                        dg = 128.0 + localT * (0.0 - 128.0);
                        db = 128.0 + localT * (0.0 - 128.0);
                    }

                    // Blend toward bright green based on how static/persistent this cell has been
                    double persistFactor = static_cast<double>(snapshot.persistence[cellIndex]) / 255.0;
                    double fr = dr + persistFactor * (0.0 - dr);
                    double fg = dg + persistFactor * (255.0 - dg);
                    double fb = db + persistFactor * (0.0 - db);

                    r = static_cast<BYTE>(fr);
                    g = static_cast<BYTE>(fg);
                    b = static_cast<BYTE>(fb);
                    ++visiblePointCount;
                }
                uint8_t* px = row + x * 3;
                px[0] = b; // BGR order for 24bpp DIB
                px[1] = g;
                px[2] = r;
            }
        }

        // Fill the window background black; only the circular HUD blitted below gets any color
        HBRUSH hBackground = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(hdcMem, &rect, hBackground);
        DeleteObject(hBackground);

        // Blit the intensity grid, scaled to fit the display circle, onto the display buffer
        StretchBlt(hdcMem, centerX - maxRadius, centerY - maxRadius, maxRadius * 2, maxRadius * 2,
            s_hGridDC, 0, 0, GRID_SIZE, GRID_SIZE, SRCCOPY);

        // When a point is initially detected, display a green circle with radius 3 pixels that
        // shrinks and dims as its freshness countdown runs out, before the existing fade takes over.
        if (!freshPoints.empty()) {
            HGDIOBJ hOldBrush = SelectObject(hdcMem, GetStockObject(NULL_BRUSH));
            HGDIOBJ hOldPen = SelectObject(hdcMem, GetStockObject(NULL_PEN));
            for (const auto& pt : freshPoints) {
                int gx = std::get<0>(pt);
                int gy = std::get<1>(pt);
                uint8_t remaining = std::get<2>(pt);
                double t = static_cast<double>(remaining) / static_cast<double>(FRESH_MARKER_FRAMES); // 1.0 = brand new, 0.0 = about to hand off

                int radius = 1 + static_cast<int>(std::lround(t * 2.0)); // shrinks from 3px down to 1px
                BYTE greenLevel = static_cast<BYTE>(16 + t * (255 - 16)); // dims from bright green toward background

                HBRUSH hFreshBrush = CreateSolidBrush(RGB(0, greenLevel, 0));
                HGDIOBJ hPrevBrush = SelectObject(hdcMem, hFreshBrush);

                int sx = centerX - maxRadius + (gx * (maxRadius * 2)) / GRID_SIZE;
                int sy = centerY - maxRadius + (gy * (maxRadius * 2)) / GRID_SIZE;
                Ellipse(hdcMem, sx - radius, sy - radius, sx + radius, sy + radius);

                SelectObject(hdcMem, hPrevBrush);
                DeleteObject(hFreshBrush);
            }
            SelectObject(hdcMem, hOldBrush);
            SelectObject(hdcMem, hOldPen);
        }

        // Draw tracked objects: persistent IDs from the clustering/tracking pass are rendered as a
        // red marker circle with a leader line to the northeast, labeled in white on a transparent
        // background so it stays visually distinct from the green LIDAR display.
        if (!tracks.empty()) {
            HPEN hTrackPen = CreatePen(PS_SOLID, 1, RGB(255, 0, 0));
            HGDIOBJ hOldTrackBrush = SelectObject(hdcMem, GetStockObject(NULL_BRUSH));
            HGDIOBJ hOldTrackPen = SelectObject(hdcMem, hTrackPen);
            int oldBkMode = SetBkMode(hdcMem, TRANSPARENT);
            SetTextColor(hdcMem, RGB(255, 255, 255));

            const int trackRadius = 8;
            const int leaderLength = 30; // pixels, pointing northeast at 45 degrees
            const int leaderOffset = static_cast<int>(leaderLength / 1.41421356); // ~30px diagonal component

            for (const auto& track : tracks) {
                if (track.hitStreak < MIN_CONFIRM_FRAMES) continue; // Skip unconfirmed, likely-noise tracks
                int sx = centerX - maxRadius + (static_cast<int>(track.gx) * (maxRadius * 2)) / GRID_SIZE;
                int sy = centerY - maxRadius + (static_cast<int>(track.gy) * (maxRadius * 2)) / GRID_SIZE;

                Ellipse(hdcMem, sx - trackRadius, sy - trackRadius, sx + trackRadius, sy + trackRadius);

                int lx = sx + leaderOffset;
                int ly = sy - leaderOffset; // Northeast: +x, -y
                MoveToEx(hdcMem, sx, sy, NULL);
                LineTo(hdcMem, lx, ly);

                wchar_t idLabel[16];
                swprintf_s(idLabel, L"#%d", track.id);
                TextOutW(hdcMem, lx + 2, ly - 16, idLabel, lstrlenW(idLabel));
            }

            SetBkMode(hdcMem, oldBkMode);
            SetTextColor(hdcMem, RGB(0, 255, 0)); // Restore green for the HUD text drawn below
            SelectObject(hdcMem, hOldTrackBrush);
            SelectObject(hdcMem, hOldTrackPen);
            DeleteObject(hTrackPen);
        }

        // Render circular polar radar lines maps, labeled in white with the distance auto-scaled
        // to the current zoom range selected via the vertical slider.
        double currentZoomMeters = model.GetZoomMeters();
        HPEN hGridPen = CreatePen(PS_SOLID, 1, RGB(0, 80, 0));
        SelectObject(hdcMem, hGridPen);
        SelectObject(hdcMem, GetStockObject(NULL_BRUSH));
        int ringStep = maxRadius / 4;
        SetTextColor(hdcMem, RGB(255, 255, 255));
        SetBkMode(hdcMem, TRANSPARENT);
        for (int r = ringStep; r <= maxRadius; r += ringStep) {
            Ellipse(hdcMem, centerX - r, centerY - r, centerX + r, centerY + r);

            double ringDistanceM = (static_cast<double>(r) / maxRadius) * currentZoomMeters;
            wchar_t ringLabel[16];
            swprintf_s(ringLabel, L"%.1fm", ringDistanceM);
            TextOutW(hdcMem, centerX + r + 4, centerY - 8, ringLabel, lstrlenW(ringLabel));
        }
        DeleteObject(hGridPen);

        // Render Crosshair Axes
        HPEN hAxisPen = CreatePen(PS_DOT, 1, RGB(0, 60, 0));
        SelectObject(hdcMem, hAxisPen);
        MoveToEx(hdcMem, centerX - maxRadius, centerY, NULL); LineTo(hdcMem, centerX + maxRadius, centerY);
        MoveToEx(hdcMem, centerX, centerY - maxRadius, NULL); LineTo(hdcMem, centerX, centerY + maxRadius);
        DeleteObject(hAxisPen);

        // Draw Configuration Metrics Data HUD Elements Overlay
        SetTextColor(hdcMem, RGB(0, 255, 0));
        SetBkMode(hdcMem, TRANSPARENT);
        wchar_t hudText[160];
        std::wstring hudPortName; DWORD hudBaudRate;
        {
            std::lock_guard<std::mutex> comLock(g_ComSettingsMutex);
            hudPortName = g_ComPortName;
            hudBaudRate = g_BaudRate;
        }
        bool hudPortConnected = g_hSerial.load(std::memory_order_acquire) != INVALID_HANDLE_VALUE;
        swprintf_s(hudText, L"LD500 SCOPE %s | %s @ %u BAUD | MAX: %.1fm | DATA POINTS: %d",
            hudPortConnected ? L"ACTIVE" : L"DISCONNECTED", hudPortName.c_str(), hudBaudRate, currentZoomMeters, visiblePointCount);
        GdiHudSurface hudSurface(hdcMem);
        hudSurface.DrawHudText(15, 15, hudText);

        // Custom green-frame vertical zoom slider with white tick marks, on the RHS of the HUD.
        DrawZoomSlider(hdcMem, currentZoomMeters);

        // BitBlt final output surface frames up onto core windows viewport layer instantly
        BitBlt(hdc, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, hOldFont);
        SelectObject(hdcMem, hOld);
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);
    }

}
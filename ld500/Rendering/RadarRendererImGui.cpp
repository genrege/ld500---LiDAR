#include "RadarRendererImGui.h"
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <climits>
#include <tuple>
#include <vector>
#include "ImGuiHudSurface.h"

#if defined(_WIN32)
#include <windows.h>
#include <GL/gl.h>
#elif defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

namespace {
    GLuint s_GridTexture = 0;
    std::vector<uint8_t> s_GridPixels; // RGB, top-down, tightly packed (3 bytes/pixel)
    int s_TextureGridSize = 0;

    ImVec2 s_ZoomSliderTop = { 0, 0 };
    ImVec2 s_ZoomSliderBottom = { 0, 0 };
    float s_ZoomSliderX = 0; // track's single X coordinate (it's a thin vertical line)

    constexpr int SHADOW_ANGLE_BUCKETS = 180; // 2-degree buckets

    // Precomputed angle bucket for every grid cell relative to the grid center, rebuilt whenever
    // GRID_SIZE changes, so ComputeShadowMask() below never needs a per-frame atan2() call. Kept
    // as an independent copy of RadarRenderer.cpp's version since the two renderers share no code.
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

    float SliderYFromZoom(double zoomMeters) {
        float top = s_ZoomSliderTop.y;
        float bottom = s_ZoomSliderBottom.y;
        double t = (ZOOM_MAX_METERS > ZOOM_MIN_METERS)
            ? (ZOOM_MAX_METERS - zoomMeters) / (ZOOM_MAX_METERS - ZOOM_MIN_METERS)
            : 0.0;
        return top + static_cast<float>(t * (bottom - top));
    }

    // Approximates GDI's PS_DOT crosshair pen with short dashes, since ImDrawList has no
    // built-in dashed-line style.
    void AddDottedLine(ImDrawList* dl, ImVec2 p1, ImVec2 p2, ImU32 col) {
        constexpr float dashLen = 3.0f, gapLen = 3.0f;
        float dx = p2.x - p1.x, dy = p2.y - p1.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.0f) return;
        float ux = dx / len, uy = dy / len;
        float pos = 0.0f;
        while (pos < len) {
            float segEnd = std::min(pos + dashLen, len);
            dl->AddLine(ImVec2(p1.x + ux * pos, p1.y + uy * pos), ImVec2(p1.x + ux * segEnd, p1.y + uy * segEnd), col);
            pos = segEnd + gapLen;
        }
    }
}

namespace RadarRendererImGui {

    void Init() {
        glGenTextures(1, &s_GridTexture);
        ResizeGridSurface();
    }

    void Shutdown() {
        if (s_GridTexture) { glDeleteTextures(1, &s_GridTexture); s_GridTexture = 0; }
        s_GridPixels.clear();
    }

    void ResizeGridSurface() {
        s_GridPixels.assign(static_cast<size_t>(GRID_SIZE) * GRID_SIZE * 3, 0);
        glBindTexture(GL_TEXTURE_2D, s_GridTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, GRID_SIZE, GRID_SIZE, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        s_TextureGridSize = GRID_SIZE;
    }

    void LayoutZoomSlider() {
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        const int rightMargin = 50; // Leaves room for the tick labels drawn to the right of the track
        float clientHeight = displaySize.y;
        float sliderHeight = clientHeight / 2.0f;

        s_ZoomSliderX = displaySize.x - rightMargin;
        s_ZoomSliderTop = ImVec2(s_ZoomSliderX, (clientHeight - sliderHeight) / 2.0f);
        s_ZoomSliderBottom = ImVec2(s_ZoomSliderX, s_ZoomSliderTop.y + sliderHeight);
    }

    double ZoomFromSliderY(float y) {
        float top = s_ZoomSliderTop.y;
        float bottom = s_ZoomSliderBottom.y;
        if (y < top) y = top;
        if (y > bottom) y = bottom;
        double t = (bottom > top) ? static_cast<double>(y - top) / (bottom - top) : 0.0;
        return ZOOM_MAX_METERS - t * (ZOOM_MAX_METERS - ZOOM_MIN_METERS);
    }

    bool HitTestZoomSlider(float x, float y) {
        return x >= s_ZoomSliderX - 10 && x <= s_ZoomSliderX + 10 &&
               y >= s_ZoomSliderTop.y - 10 && y <= s_ZoomSliderBottom.y + 10;
    }

    void PaintRadar(RadarGridModel& model, ObjectTracker& tracker,
                     bool trackingEnabled, bool shadowCastEnabled,
                     const char* portName, uint32_t baudRate, bool isConnected) {
        if (s_TextureGridSize != GRID_SIZE) ResizeGridSurface();

        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        int width = static_cast<int>(displaySize.x);
        int height = static_cast<int>(displaySize.y);
        ImDrawList* dl = ImGui::GetBackgroundDrawList();

        int centerX = width / 2;
        int centerY = height / 2;
        int maxRadius = (std::min(width, height) / 2) - 40;
        if (maxRadius < 1) maxRadius = 1;

        RadarGridSnapshot snapshot = model.Snapshot();
        tracker.Update(snapshot, trackingEnabled);
        const std::vector<TrackedObject>& tracks = tracker.Tracks();

        int visiblePointCount = 0;
        std::vector<std::tuple<int, int, uint8_t>> freshPoints;
        std::vector<uint8_t> freshMask(static_cast<size_t>(GRID_SIZE) * GRID_SIZE, 0);
        std::vector<uint8_t> shadowMask;
        if (shadowCastEnabled) {
            shadowMask = ComputeShadowMask(snapshot.intensity);
        }

        const int stride = GRID_SIZE * 3;
        const int gridCenter = GRID_SIZE / 2;
        const int gridMaxRadiusSq = gridCenter * gridCenter;
        for (int y = 0; y < GRID_SIZE; ++y) {
            uint8_t* row = s_GridPixels.data() + static_cast<size_t>(y) * stride;
            int dy = y - gridCenter;
            for (int x = 0; x < GRID_SIZE; ++x) {
                size_t cellIndex = static_cast<size_t>(y) * GRID_SIZE + x;
                uint8_t intensity = snapshot.intensity[cellIndex];
                uint8_t r, g, b;
                if (intensity == 0) {
                    int dx = x - gridCenter;
                    if (dx * dx + dy * dy > gridMaxRadiusSq) {
                        r = 0; g = 0; b = 0;
                    } else if (shadowCastEnabled && shadowMask[cellIndex]) {
                        r = 0; g = 6; b = 0;
                    } else {
                        r = 10; g = 16; b = 10;
                    }
                } else if (snapshot.fresh[cellIndex] > 0) {
                    r = 10; g = 16; b = 10;
                    freshMask[cellIndex] = 1;

                    bool hasEarlierNeighbourInCluster = false;
                    for (int fy = -1; fy <= 0 && !hasEarlierNeighbourInCluster; ++fy) {
                        for (int fx = -1; fx <= 1; ++fx) {
                            if (fy == 0 && fx >= 0) continue;
                            int nx = x + fx, ny = y + fy;
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
                } else {
                    double t = 1.0 - (static_cast<double>(intensity) / 255.0);
                    double dr, dg, db;
                    if (t < 0.5) {
                        double localT = t / 0.5;
                        dr = localT * 128.0;
                        dg = 255.0 + localT * (128.0 - 255.0);
                        db = localT * 128.0;
                    } else {
                        double localT = (t - 0.5) / 0.5;
                        dr = 128.0 + localT * (0.0 - 128.0);
                        dg = 128.0 + localT * (0.0 - 128.0);
                        db = 128.0 + localT * (0.0 - 128.0);
                    }

                    double persistFactor = static_cast<double>(snapshot.persistence[cellIndex]) / 255.0;
                    double fr = dr + persistFactor * (0.0 - dr);
                    double fg = dg + persistFactor * (255.0 - dg);
                    double fb = db + persistFactor * (0.0 - db);

                    r = static_cast<uint8_t>(fr);
                    g = static_cast<uint8_t>(fg);
                    b = static_cast<uint8_t>(fb);
                    ++visiblePointCount;
                }
                uint8_t* px = row + x * 3;
                px[0] = r; px[1] = g; px[2] = b;
            }
        }

        glBindTexture(GL_TEXTURE_2D, s_GridTexture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GRID_SIZE, GRID_SIZE, GL_RGB, GL_UNSIGNED_BYTE, s_GridPixels.data());

        // Window background, then the grid texture scaled to fit the display circle (the
        // texture-blit equivalent of GDI's StretchBlt from its DIB section).
        dl->AddRectFilled(ImVec2(0, 0), displaySize, IM_COL32(0, 0, 0, 255));
        dl->AddImage(static_cast<ImTextureID>(s_GridTexture),
            ImVec2(static_cast<float>(centerX - maxRadius), static_cast<float>(centerY - maxRadius)),
            ImVec2(static_cast<float>(centerX + maxRadius), static_cast<float>(centerY + maxRadius)));

        // Fresh-detection marker circles.
        for (const auto& pt : freshPoints) {
            int gx = std::get<0>(pt);
            int gy = std::get<1>(pt);
            uint8_t remaining = std::get<2>(pt);
            double t = static_cast<double>(remaining) / static_cast<double>(FRESH_MARKER_FRAMES);
            float radius = 1.0f + static_cast<float>(std::lround(t * 2.0));
            uint8_t greenLevel = static_cast<uint8_t>(16 + t * (255 - 16));

            float sx = centerX - maxRadius + (gx * (maxRadius * 2)) / static_cast<float>(GRID_SIZE);
            float sy = centerY - maxRadius + (gy * (maxRadius * 2)) / static_cast<float>(GRID_SIZE);
            dl->AddCircleFilled(ImVec2(sx, sy), radius, IM_COL32(0, greenLevel, 0, 255));
        }

        // Tracked objects: red marker circle with a leader line to the northeast, labeled.
        if (!tracks.empty()) {
            constexpr float trackRadius = 8.0f;
            constexpr float leaderLength = 30.0f;
            constexpr float leaderOffset = leaderLength / 1.41421356f;

            for (const auto& track : tracks) {
                if (track.hitStreak < MIN_CONFIRM_FRAMES) continue;
                float sx = centerX - maxRadius + (static_cast<float>(track.gx) * (maxRadius * 2)) / static_cast<float>(GRID_SIZE);
                float sy = centerY - maxRadius + (static_cast<float>(track.gy) * (maxRadius * 2)) / static_cast<float>(GRID_SIZE);

                dl->AddCircle(ImVec2(sx, sy), trackRadius, IM_COL32(255, 0, 0, 255));

                float lx = sx + leaderOffset;
                float ly = sy - leaderOffset;
                dl->AddLine(ImVec2(sx, sy), ImVec2(lx, ly), IM_COL32(255, 0, 0, 255));

                char idLabel[16];
                std::snprintf(idLabel, sizeof(idLabel), "#%d", track.id);
                dl->AddText(ImVec2(lx + 2, ly - 16), IM_COL32(255, 255, 255, 255), idLabel);
            }
        }

        // Polar range rings, labeled with distance auto-scaled to the current zoom.
        double currentZoomMeters = model.GetZoomMeters();
        int ringStep = maxRadius / 4;
        if (ringStep < 1) ringStep = 1;
        for (int r = ringStep; r <= maxRadius; r += ringStep) {
            dl->AddCircle(ImVec2(static_cast<float>(centerX), static_cast<float>(centerY)), static_cast<float>(r), IM_COL32(0, 80, 0, 255));

            double ringDistanceM = (static_cast<double>(r) / maxRadius) * currentZoomMeters;
            char ringLabel[16];
            std::snprintf(ringLabel, sizeof(ringLabel), "%.1fm", ringDistanceM);
            dl->AddText(ImVec2(static_cast<float>(centerX + r + 4), static_cast<float>(centerY - 8)), IM_COL32(255, 255, 255, 255), ringLabel);
        }

        // Crosshair axes.
        AddDottedLine(dl, ImVec2(static_cast<float>(centerX - maxRadius), static_cast<float>(centerY)),
            ImVec2(static_cast<float>(centerX + maxRadius), static_cast<float>(centerY)), IM_COL32(0, 60, 0, 255));
        AddDottedLine(dl, ImVec2(static_cast<float>(centerX), static_cast<float>(centerY - maxRadius)),
            ImVec2(static_cast<float>(centerX), static_cast<float>(centerY + maxRadius)), IM_COL32(0, 60, 0, 255));

        // HUD status line (behind the existing IHudSurface abstraction). Built as a wchar_t
        // buffer via a plain ASCII widen (every field here is guaranteed ASCII) rather than a
        // wide-printf %s, since wide/narrow %s argument-type conventions aren't portable.
        char hudTextUtf8[160];
        std::snprintf(hudTextUtf8, sizeof(hudTextUtf8), "LD500 SCOPE %s | %s @ %u BAUD | MAX: %.1fm | DATA POINTS: %d",
            isConnected ? "ACTIVE" : "DISCONNECTED", portName, baudRate, currentZoomMeters, visiblePointCount);
        wchar_t hudText[160];
        size_t hudTextLen = std::strlen(hudTextUtf8);
        for (size_t i = 0; i < hudTextLen; ++i) hudText[i] = static_cast<wchar_t>(static_cast<unsigned char>(hudTextUtf8[i]));
        hudText[hudTextLen] = L'\0';

        ImGuiHudSurface hudSurface;
        // Offset below the main menu bar, which is drawn on top of this background draw list.
        hudSurface.DrawHudText(15, 15 + static_cast<int>(ImGui::GetFrameHeight()), hudText);

        // Zoom slider: open green-frame track with white tick marks and a green thumb.
        dl->AddLine(s_ZoomSliderTop, s_ZoomSliderBottom, IM_COL32(0, 255, 0, 255));
        int firstTick = static_cast<int>(std::ceil(ZOOM_MIN_METERS));
        int lastTick = static_cast<int>(std::floor(ZOOM_MAX_METERS));
        for (int m = firstTick; m <= lastTick; ++m) {
            float y = SliderYFromZoom(static_cast<double>(m));
            dl->AddLine(ImVec2(s_ZoomSliderX, y), ImVec2(s_ZoomSliderX + 5, y), IM_COL32(255, 255, 255, 255));
            char tickLabel[8];
            std::snprintf(tickLabel, sizeof(tickLabel), "%d", m);
            dl->AddText(ImVec2(s_ZoomSliderX + 9, y - 7), IM_COL32(255, 255, 255, 255), tickLabel);
        }

        float thumbY = SliderYFromZoom(currentZoomMeters);
        dl->AddRectFilled(ImVec2(s_ZoomSliderX - 4, thumbY - 5), ImVec2(s_ZoomSliderX + 4, thumbY + 5), IM_COL32(0, 255, 0, 255));

        char valueLabel[16];
        std::snprintf(valueLabel, sizeof(valueLabel), "%.1fm", currentZoomMeters);
        dl->AddText(ImVec2(s_ZoomSliderX + 9, s_ZoomSliderBottom.y + 6), IM_COL32(0, 255, 0, 255), valueLabel);
    }
}

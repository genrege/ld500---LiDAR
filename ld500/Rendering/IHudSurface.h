#pragma once

// Minimal graphics-primitive abstraction for the HUD status line. A future non-GDI backend (e.g.
// a microcontroller's framebuffer/LCD driver) implements this instead of touching PaintRadar().
class IHudSurface {
public:
    virtual ~IHudSurface() = default;
    virtual void DrawHudText(int x, int y, const wchar_t* text) = 0;
};

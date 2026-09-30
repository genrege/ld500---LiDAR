#pragma once

// Dear ImGui implementation of IHudSurface, used by the ImGui shell on both platforms.

#include "IHudSurface.h"

class ImGuiHudSurface : public IHudSurface {
public:
    void DrawHudText(int x, int y, const wchar_t* text) override;

    // Color applied to the next DrawHudText() call, mirroring GDI's SetTextColor-before-TextOutW
    // pattern since ImDrawList::AddText takes its color as an explicit argument, not DC state.
    static void SetTextColor(unsigned int color);

private:
    static unsigned int s_TextColor;
};

#pragma once

// Dear ImGui implementation of IHudSurface, used by the ImGui shell on both platforms.

#include "IHudSurface.h"

class ImGuiHudSurface : public IHudSurface {
public:
    void DrawHudText(int x, int y, const wchar_t* text) override;
};

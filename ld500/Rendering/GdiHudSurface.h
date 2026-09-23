#pragma once

// Win32 GDI implementation of IHudSurface.

#include <windows.h>
#include "IHudSurface.h"

class GdiHudSurface : public IHudSurface {
public:
    explicit GdiHudSurface(HDC hdc) : m_hdc(hdc) {}
    void DrawHudText(int x, int y, const wchar_t* text) override;

private:
    HDC m_hdc;
};

#include "GdiHudSurface.h"

void GdiHudSurface::DrawHudText(int x, int y, const wchar_t* text) {
    TextOutW(m_hdc, x, y, text, lstrlenW(text));
}

#include "ImGuiHudSurface.h"
#include <imgui.h>
#include <string>
#include <cwchar>
#include <climits>

namespace {
    // Portable wchar_t -> UTF-8 conversion: wchar_t is UTF-16 on Windows and UTF-32 on
    // macOS/Linux, so this handles both without relying on the deprecated
    // std::wstring_convert or a Windows-only API.
    std::string ToUtf8(const wchar_t* text) {
        std::string out;
        size_t len = std::wcslen(text);
        for (size_t i = 0; i < len; ++i) {
            uint32_t codepoint;
#if WCHAR_MAX > 0xFFFFu
            codepoint = static_cast<uint32_t>(text[i]); // UTF-32 wchar_t
#else
            uint32_t unit = static_cast<uint16_t>(text[i]);
            if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < len) { // UTF-16 surrogate pair
                uint32_t low = static_cast<uint16_t>(text[i + 1]);
                codepoint = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            } else {
                codepoint = unit;
            }
#endif
            if (codepoint < 0x80) {
                out += static_cast<char>(codepoint);
            } else if (codepoint < 0x800) {
                out += static_cast<char>(0xC0 | (codepoint >> 6));
                out += static_cast<char>(0x80 | (codepoint & 0x3F));
            } else if (codepoint < 0x10000) {
                out += static_cast<char>(0xE0 | (codepoint >> 12));
                out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (codepoint & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (codepoint >> 18));
                out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (codepoint & 0x3F));
            }
        }
        return out;
    }
}

void ImGuiHudSurface::DrawHudText(int x, int y, const wchar_t* text) {
    std::string utf8 = ToUtf8(text);
    ImGui::GetBackgroundDrawList()->AddText(ImVec2(static_cast<float>(x), static_cast<float>(y)),
        IM_COL32(0, 255, 0, 255), utf8.c_str());
}

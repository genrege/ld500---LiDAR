#include "Shell/ImGuiShell.h"

#if defined(_WIN32)
#include <windows.h>
#include <string>
#include "Platform/ISettingsStore.h"
#include "Shell/Win32Shell.h"

namespace {
    // Reads the persisted UiBackend setting (shared with the ImGui shell's own settings store,
    // both backed by the same HKCU\Software\LD500 registry hive), overridable by a "--ui=win32"
    // or "--ui=imgui" command-line flag so either shell can be forced without touching settings.
    bool WantsImGuiShell(LPSTR lpCmdLine) {
        std::string cmdLine(lpCmdLine ? lpCmdLine : "");
        if (cmdLine.find("--ui=imgui") != std::string::npos) return true;
        if (cmdLine.find("--ui=win32") != std::string::npos) return false;

        auto settings = CreatePlatformSettingsStore();
        return settings->GetString("UiBackend", "win32") == "imgui";
    }
}

// Entry Point Application Architecture
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    if (WantsImGuiShell(lpCmdLine)) {
        return RunImGuiShell();
    }
    return RunWin32Shell(hInstance, nCmdShow);
}

#else // macOS/Linux: only the ImGui shell exists.

int main(int /*argc*/, char** /*argv*/) {
    return RunImGuiShell();
}

#endif

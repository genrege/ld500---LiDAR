#include <windows.h>
#include <commctrl.h>
#include <mutex>

#include "Resource.h"
#include "SerialPort.h"
#include "AppWorkers.h"
#include "AppSettings.h"
#include "RadarRenderer.h"
#include "MainWindow.h"

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
    {
        std::lock_guard<std::mutex> lock(g_ComSettingsMutex);
        HANDLE hInitialPort = OpenAndConfigureSerialPort(g_ComPortName, g_BaudRate);
        g_hSerial.store(hInitialPort, std::memory_order_release);
        if (hInitialPort == INVALID_HANDLE_VALUE) {
            wchar_t warnMsg[256];
            swprintf_s(warnMsg, L"Failed to connect to Radar on %s. Use Ports > Manage Ports... to select and reset a port once connected.", g_ComPortName.c_str());
            MessageBox(NULL, warnMsg, L"Hardware Warning", MB_ICONWARNING);
            // Continue running rather than exiting, so the user can fix the connection via the Manage Ports dialog.
        }
    }

    RadarRenderer::Init();
    g_GridModel.SetAngleOffsetDegrees(LoadAngleOffsetDegrees());

    HANDLE hThread = CreateThread(NULL, 0, SerialReadThread, &g_GridModel, 0, NULL);
    HANDLE hDecayThread = CreateThread(NULL, 0, DecayThread, &g_GridModel, 0, NULL);
    HANDLE hFreshDecayThread = CreateThread(NULL, 0, FreshMarkerDecayThread, &g_GridModel, 0, NULL);

    RegisterMainWindowClass(hInstance);
    HWND hwnd = CreateMainWindow(hInstance, nCmdShow);
    if (hwnd == NULL) return 0;

    // Restore the persisted Tracking/Shadow toggles and reflect them in the menu checkmarks.
    g_TrackingEnabled.store(LoadTrackingEnabled(), std::memory_order_relaxed);
    g_ShadowCastEnabled.store(LoadShadowCastEnabled(), std::memory_order_relaxed);
    HMENU hMenu = GetMenu(hwnd);
    if (hMenu) {
        CheckMenuItem(hMenu, IDM_TOGGLE_TRACKING,
            MF_BYCOMMAND | (g_TrackingEnabled.load(std::memory_order_relaxed) ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(hMenu, IDM_TOGGLE_SHADOW,
            MF_BYCOMMAND | (g_ShadowCastEnabled.load(std::memory_order_relaxed) ? MF_CHECKED : MF_UNCHECKED));
    }

    // Dynamic UI refresh pump using a basic WM_PAINT trigger loop
    MSG msg = { 0 };
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            InvalidateRect(hwnd, NULL, FALSE); // Triggers constant visual repaint
            Sleep(16); // Target ~60 FPS
        }
    }
    return RunWin32Shell(hInstance, nCmdShow);
}

#else // macOS/Linux: only the ImGui shell exists.

int main(int /*argc*/, char** /*argv*/) {
    return RunImGuiShell();
}

#endif

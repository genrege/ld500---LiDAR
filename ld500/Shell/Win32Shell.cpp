#include "Win32Shell.h"
#include <commctrl.h>
#include <mutex>
#include <thread>

#include "Resource.h"
#include "SerialPort.h"
#include "AppWorkers.h"
#include "AppSettings.h"
#include "RadarRenderer.h"
#include "MainWindow.h"

#pragma comment(lib, "comctl32.lib")

// Moved from ld500.cpp verbatim (aside from CreateThread -> std::thread) when the ImGui shell
// was added alongside this one; behavior is unchanged from the original Win32-only app.
int RunWin32Shell(HINSTANCE hInstance, int nCmdShow) {
    {
        std::lock_guard<std::mutex> lock(g_ComSettingsMutex);
        g_ComPortName = LoadComPortName();
        HANDLE hInitialPort = OpenAndConfigureSerialPort(g_ComPortName, g_BaudRate);
        g_hSerial.store(hInitialPort, std::memory_order_release);
        // No popup on failure: the HUD and Manage Ports dialog both surface a bad/disconnected port.
    }

    g_GridModel.SetGridSizeCells(LoadGridSizeCells());
    MIN_CLUSTER_CELLS = LoadMinClusterCells();
    MAX_MATCH_DIST_CELLS = LoadMaxMatchDistCells();
    MAX_MISSED_FRAMES = LoadMaxMissedFrames();
    MIN_CONFIRM_FRAMES = LoadMinConfirmFrames();
    MAX_STATIC_PERSISTENCE_FOR_TRACKING = LoadMaxStaticPersistenceForTracking();

    RadarRenderer::Init();
    g_GridModel.SetAngleOffsetDegrees(LoadAngleOffsetDegrees());
    g_GridModel.SetPersistenceEnabled(LoadPersistenceEnabled());
    g_GridModel.SetZoomMeters(LoadZoomMeters());
    RadarRenderer::SetBackgroundIntensity(LoadBackgroundIntensity());

    std::thread serialThread(SerialReadThread, &g_GridModel);
    std::thread decayThread(DecayThread, &g_GridModel);
    std::thread freshDecayThread(FreshMarkerDecayThread, &g_GridModel);

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
        CheckMenuItem(hMenu, IDM_TOGGLE_PERSISTENCE,
            MF_BYCOMMAND | (g_GridModel.GetPersistenceEnabled() ? MF_CHECKED : MF_UNCHECKED));
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

    // Cleanup resources
    g_KeepRunning = false;
    serialThread.join();
    decayThread.join();
    freshDecayThread.join();
    HANDLE hFinalPort = g_hSerial.exchange(INVALID_HANDLE_VALUE, std::memory_order_acq_rel);
    if (hFinalPort != INVALID_HANDLE_VALUE) CloseHandle(hFinalPort);
    RadarRenderer::Shutdown();

    return 0;
}

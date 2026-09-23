#include "MainWindow.h"
#include <windowsx.h>
#include "Resource.h"
#include "RadarRenderer.h"
#include "Dialogs.h"
#include "SerialPort.h"
#include "AppSettings.h"

RadarGridModel    g_GridModel;
ObjectTracker     g_Tracker;
std::atomic<bool> g_TrackingEnabled(false);
std::atomic<bool> g_ShadowCastEnabled(false);

namespace {
    bool s_ZoomSliderDragging = false;

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_CREATE: {
            RadarRenderer::LayoutZoomSlider(hwnd);
            return 0;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
            case IDM_SETTINGS:
                DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_SETTINGS), hwnd, SettingsDlgProc);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            case IDM_MANAGE_PORTS:
                DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_PORTS), hwnd, PortManagerDlgProc);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            case IDM_TOGGLE_TRACKING: {
                bool newState = !g_TrackingEnabled.load(std::memory_order_relaxed);
                g_TrackingEnabled.store(newState, std::memory_order_relaxed);
                SaveTrackingEnabled(newState);
                HMENU hMenu = GetMenu(hwnd);
                if (hMenu) {
                    CheckMenuItem(hMenu, IDM_TOGGLE_TRACKING, MF_BYCOMMAND | (newState ? MF_CHECKED : MF_UNCHECKED));
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            case IDM_TOGGLE_SHADOW: {
                bool newState = !g_ShadowCastEnabled.load(std::memory_order_relaxed);
                g_ShadowCastEnabled.store(newState, std::memory_order_relaxed);
                SaveShadowCastEnabled(newState);
                HMENU hMenu = GetMenu(hwnd);
                if (hMenu) {
                    CheckMenuItem(hMenu, IDM_TOGGLE_SHADOW, MF_BYCOMMAND | (newState ? MF_CHECKED : MF_UNCHECKED));
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            case IDM_ABOUT:
                DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_ABOUTBOX), hwnd, AboutDlgProc);
                return 0;
            case IDM_EXIT:
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }
        case WM_SIZE:
            RadarRenderer::LayoutZoomSlider(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            if (RadarRenderer::HitTestZoomSlider(x, y)) {
                s_ZoomSliderDragging = true;
                SetCapture(hwnd);
                g_GridModel.SetZoomMeters(RadarRenderer::ZoomFromSliderY(y));
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (s_ZoomSliderDragging) {
                int y = GET_Y_LPARAM(lParam);
                g_GridModel.SetZoomMeters(RadarRenderer::ZoomFromSliderY(y));
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (s_ZoomSliderDragging) {
                s_ZoomSliderDragging = false;
                ReleaseCapture();
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RadarRenderer::PaintRadar(hdc, hwnd, g_GridModel, g_Tracker,
                g_TrackingEnabled.load(std::memory_order_relaxed),
                g_ShadowCastEnabled.load(std::memory_order_relaxed));
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            g_KeepRunning = false;
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

ATOM RegisterMainWindowClass(HINSTANCE hInstance) {
    const wchar_t CLASS_NAME[] = L"RadarWindow";
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszMenuName = MAKEINTRESOURCE(IDC_LD500); // Adds the File/Ports/Help menu bar, including Manage Ports...
    return RegisterClass(&wc);
}

HWND CreateMainWindow(HINSTANCE hInstance, int nCmdShow) {
    HWND hwnd = CreateWindowEx(0, L"RadarWindow", L"LD500 Lidar Visualizer", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 800, NULL, NULL, hInstance, NULL);
    if (hwnd != NULL) {
        ShowWindow(hwnd, nCmdShow);
    }
    return hwnd;
}

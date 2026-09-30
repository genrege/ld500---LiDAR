#include "Dialogs.h"
#include "Resource.h"
#include "SerialPort.h"
#include "AppSettings.h"
#include "MainWindow.h"
#include "RadarRenderer.h"

// Refreshes the Manage Ports dialog's list box with the currently detected serial ports and their
// connected device descriptions, preserving the current connection's selection where possible.
static void RefreshPortsListBox(HWND hDlg) {
    HWND hList = GetDlgItem(hDlg, IDC_PORTS_LIST);
    SendMessage(hList, LB_RESETCONTENT, 0, 0);

    std::vector<ComPortInfo> ports = EnumerateComPorts();
    std::wstring activePortName;
    {
        std::lock_guard<std::mutex> lock(g_ComSettingsMutex);
        activePortName = g_ComPortName;
    }

    for (const auto& port : ports) {
        bool isActive = (port.portName == activePortName);
        bool isConnected = isActive && g_hSerial.load(std::memory_order_acquire) != INVALID_HANDLE_VALUE;
        wchar_t entry[600];
        swprintf_s(entry, L"%s - %s%s", port.portName.c_str(), port.deviceDesc.c_str(),
            isActive ? (isConnected ? L" [ACTIVE]" : L" [ACTIVE, DISCONNECTED]") : L"");
        int index = static_cast<int>(SendMessageW(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry)));
        if (isActive) {
            SendMessage(hList, LB_SETCURSEL, index, 0);
        }
    }

    if (ports.empty()) {
        SendMessageW(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No serial ports detected"));
    }
}

INT_PTR CALLBACK PortManagerDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    const UINT_PTR PORTS_REFRESH_TIMER_ID = 1;
    const UINT PORTS_REFRESH_INTERVAL_MS = 2000;

    switch (message) {
    case WM_INITDIALOG:
        RefreshPortsListBox(hDlg);
        SetTimer(hDlg, PORTS_REFRESH_TIMER_ID, PORTS_REFRESH_INTERVAL_MS, NULL);
        return (INT_PTR)TRUE;

    case WM_TIMER:
        if (wParam == PORTS_REFRESH_TIMER_ID) {
            RefreshPortsListBox(hDlg);
        }
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_PORTS_REFRESH:
            RefreshPortsListBox(hDlg);
            return (INT_PTR)TRUE;

        case IDC_PORTS_RESET: {
            bool ok = ResetComPort();
            MessageBoxW(hDlg,
                ok ? L"Port reset and reconnected successfully." : L"Failed to reopen the port after reset.",
                L"Reset Port", ok ? MB_ICONINFORMATION : MB_ICONERROR);
            RefreshPortsListBox(hDlg);
            return (INT_PTR)TRUE;
        }

        case IDOK:
        case IDCANCEL:
            KillTimer(hDlg, PORTS_REFRESH_TIMER_ID);
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;

    case WM_CLOSE:
        KillTimer(hDlg, PORTS_REFRESH_TIMER_ID);
        EndDialog(hDlg, IDCANCEL);
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

// Simple About dialog procedure, matching the pre-existing IDD_ABOUTBOX resource.
INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

// Settings dialog procedure: edits the LIDAR orientation offset, saving to the registry and
// applying it to the live grid model immediately on OK.
INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG:
        SetDlgItemInt(hDlg, IDC_SETTINGS_ANGLE_OFFSET,
            static_cast<UINT>(g_GridModel.GetAngleOffsetDegrees()), FALSE);
        SetDlgItemInt(hDlg, IDC_SETTINGS_GRID_SIZE, static_cast<UINT>(GRID_SIZE), FALSE);
        SetDlgItemInt(hDlg, IDC_SETTINGS_MIN_CLUSTER_CELLS, static_cast<UINT>(MIN_CLUSTER_CELLS), FALSE);
        SetDlgItemInt(hDlg, IDC_SETTINGS_MAX_MATCH_DIST, static_cast<UINT>(MAX_MATCH_DIST_CELLS), FALSE);
        SetDlgItemInt(hDlg, IDC_SETTINGS_MAX_MISSED_FRAMES, static_cast<UINT>(MAX_MISSED_FRAMES), FALSE);
        SetDlgItemInt(hDlg, IDC_SETTINGS_MIN_CONFIRM_FRAMES, static_cast<UINT>(MIN_CONFIRM_FRAMES), FALSE);
        SetDlgItemInt(hDlg, IDC_SETTINGS_MAX_STATIC_PERSISTENCE,
            static_cast<UINT>(MAX_STATIC_PERSISTENCE_FOR_TRACKING), FALSE);
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
            BOOL translated = FALSE;
            UINT rawValue = GetDlgItemInt(hDlg, IDC_SETTINGS_ANGLE_OFFSET, &translated, FALSE);
            double offsetDegrees = translated ? static_cast<double>(rawValue % 360) : 0.0;
            g_GridModel.SetAngleOffsetDegrees(offsetDegrees);
            SaveAngleOffsetDegrees(offsetDegrees);

            UINT gridSize = GetDlgItemInt(hDlg, IDC_SETTINGS_GRID_SIZE, &translated, FALSE);
            if (translated && gridSize > 0) {
                g_GridModel.SetGridSizeCells(static_cast<int>(gridSize));
                RadarRenderer::ResizeGridSurface();
                SaveGridSizeCells(static_cast<int>(gridSize));
            }

            UINT minClusterCells = GetDlgItemInt(hDlg, IDC_SETTINGS_MIN_CLUSTER_CELLS, &translated, FALSE);
            if (translated) {
                MIN_CLUSTER_CELLS = static_cast<int>(minClusterCells);
                SaveMinClusterCells(MIN_CLUSTER_CELLS);
            }

            UINT maxMatchDist = GetDlgItemInt(hDlg, IDC_SETTINGS_MAX_MATCH_DIST, &translated, FALSE);
            if (translated) {
                MAX_MATCH_DIST_CELLS = static_cast<double>(maxMatchDist);
                SaveMaxMatchDistCells(MAX_MATCH_DIST_CELLS);
            }

            UINT maxMissedFrames = GetDlgItemInt(hDlg, IDC_SETTINGS_MAX_MISSED_FRAMES, &translated, FALSE);
            if (translated) {
                MAX_MISSED_FRAMES = static_cast<int>(maxMissedFrames);
                SaveMaxMissedFrames(MAX_MISSED_FRAMES);
            }

            UINT minConfirmFrames = GetDlgItemInt(hDlg, IDC_SETTINGS_MIN_CONFIRM_FRAMES, &translated, FALSE);
            if (translated) {
                MIN_CONFIRM_FRAMES = static_cast<int>(minConfirmFrames);
                SaveMinConfirmFrames(MIN_CONFIRM_FRAMES);
            }

            UINT maxStaticPersistence = GetDlgItemInt(hDlg, IDC_SETTINGS_MAX_STATIC_PERSISTENCE, &translated, FALSE);
            if (translated) {
                MAX_STATIC_PERSISTENCE_FOR_TRACKING = static_cast<double>(maxStaticPersistence);
                SaveMaxStaticPersistenceForTracking(MAX_STATIC_PERSISTENCE_FOR_TRACKING);
            }

            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

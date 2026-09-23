#include "Dialogs.h"
#include "Resource.h"
#include "SerialPort.h"

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

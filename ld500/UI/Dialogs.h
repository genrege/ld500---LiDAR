#pragma once

// Windows-specific dialog procedures: the Manage Ports dialog and the About box.

#include <windows.h>

INT_PTR CALLBACK PortManagerDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

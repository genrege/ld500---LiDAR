#pragma once

// Windows entry point for the native Win32/GDI UI shell. Contains the same startup sequence
// that lived in ld500.cpp prior to the ImGui shell being added, unchanged.

#include <windows.h>

int RunWin32Shell(HINSTANCE hInstance, int nCmdShow);

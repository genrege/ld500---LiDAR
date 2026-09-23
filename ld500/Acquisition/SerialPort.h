#pragma once

// Windows-specific serial port acquisition: enumeration, open/reset, and the background read
// thread that feeds parsed readings into a RadarGridModel.

#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

struct ComPortInfo {
    std::wstring portName;     // e.g. "COM3"
    std::wstring deviceDesc;   // Friendly device description, e.g. "USB-SERIAL CH340 (COM3)"
};

constexpr DWORD DEFAULT_BAUD_RATE = 230400;

// Set false to signal SerialReadThread/AppWorkers threads to exit.
extern bool g_KeepRunning;

// Active serial connection settings, editable at runtime via the Manage Ports dialog's reset action.
extern std::mutex          g_ComSettingsMutex;
extern std::wstring        g_ComPortName;
extern DWORD                g_BaudRate;
extern std::atomic<HANDLE>  g_hSerial; // Swapped safely when the port is reset from the Manage Ports dialog

// Scans installed serial port devices via SetupAPI and returns their port name plus friendly
// device description (what's actually connected), for display in the Manage Ports dialog.
std::vector<ComPortInfo> EnumerateComPorts();

// Opens and configures the given COM port at the given baud rate, returning INVALID_HANDLE_VALUE
// on failure. Shared by initial startup and by port reset so both paths stay in sync.
HANDLE OpenAndConfigureSerialPort(const std::wstring& portName, DWORD baudRate);

// Closes the currently active port (cancelling any pending read first so SerialReadThread's
// blocking ReadFile call returns promptly) and reopens it with the same settings. Used by the
// "Reset Selected Port" button in the Manage Ports dialog.
bool ResetComPort();

class RadarGridModel;

// Background thread: constantly reads bytes from the active COM port, parses them into radar
// readings, and ingests them into the RadarGridModel passed as lpParam. Tolerates a live port
// reset (handle swap) triggered from the Manage Ports dialog.
DWORD WINAPI SerialReadThread(LPVOID lpParam);

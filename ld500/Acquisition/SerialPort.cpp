#include "SerialPort.h"
#include <setupapi.h>
#include <devguid.h>
#include <algorithm>
#include "RadarProtocol.h"
#include "RadarGridModel.h"

#pragma comment(lib, "setupapi.lib")

bool                      g_KeepRunning = true;

std::mutex                g_ComSettingsMutex;
std::wstring               g_ComPortName = L"COM3";
DWORD                       g_BaudRate = DEFAULT_BAUD_RATE;
std::atomic<HANDLE>       g_hSerial(INVALID_HANDLE_VALUE);

std::vector<ComPortInfo> EnumerateComPorts() {
    std::vector<ComPortInfo> results;

    HDEVINFO hDevInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) return results;

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); ++i) {
        wchar_t friendlyName[512] = { 0 };
        if (!SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME,
            NULL, reinterpret_cast<PBYTE>(friendlyName), sizeof(friendlyName), NULL)) {
            // Fall back to the device description if no friendly name is registered
            SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfoData, SPDRP_DEVICEDESC,
                NULL, reinterpret_cast<PBYTE>(friendlyName), sizeof(friendlyName), NULL);
        }

        HKEY hDeviceKey = SetupDiOpenDevRegKey(hDevInfo, &devInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (hDeviceKey != INVALID_HANDLE_VALUE) {
            wchar_t portName[64] = { 0 };
            DWORD portNameSize = sizeof(portName);
            DWORD type = 0;
            if (RegQueryValueExW(hDeviceKey, L"PortName", NULL, &type,
                reinterpret_cast<LPBYTE>(portName), &portNameSize) == ERROR_SUCCESS) {
                ComPortInfo info;
                info.portName = portName;
                info.deviceDesc = friendlyName[0] ? friendlyName : L"Unknown device";
                results.push_back(info);
            }
            RegCloseKey(hDeviceKey);
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);

    std::sort(results.begin(), results.end(), [](const ComPortInfo& a, const ComPortInfo& b) {
        return a.portName < b.portName;
        });
    return results;
}

HANDLE OpenAndConfigureSerialPort(const std::wstring& portName, DWORD baudRate) {
    std::wstring devicePath = L"\\\\.\\" + portName;
    HANDLE hPort = CreateFileW(devicePath.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPort == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    DCB dcb = { 0 }; dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(hPort, &dcb)) { CloseHandle(hPort); return INVALID_HANDLE_VALUE; }
    dcb.BaudRate = baudRate; dcb.ByteSize = 8; dcb.StopBits = ONESTOPBIT; dcb.Parity = NOPARITY;
    if (!SetCommState(hPort, &dcb)) { CloseHandle(hPort); return INVALID_HANDLE_VALUE; }

    COMMTIMEOUTS tm = { 0 }; tm.ReadIntervalTimeout = MAXDWORD; SetCommTimeouts(hPort, &tm);
    return hPort;
}

bool ResetComPort() {
    std::lock_guard<std::mutex> lock(g_ComSettingsMutex);

    HANDLE hOldPort = g_hSerial.exchange(INVALID_HANDLE_VALUE, std::memory_order_acq_rel);
    if (hOldPort != INVALID_HANDLE_VALUE) {
        CancelIoEx(hOldPort, NULL);
        CloseHandle(hOldPort);
    }

    Sleep(250); // Give the OS/driver a moment to release the port before reopening it

    HANDLE hNewPort = OpenAndConfigureSerialPort(g_ComPortName, g_BaudRate);
    g_hSerial.store(hNewPort, std::memory_order_release);
    return hNewPort != INVALID_HANDLE_VALUE;
}

DWORD WINAPI SerialReadThread(LPVOID lpParam) {
    RadarGridModel* model = static_cast<RadarGridModel*>(lpParam);
    std::vector<uint8_t> streamAccumulator;

    const int BUF_SZ = 2048;
    char rxBuffer[BUF_SZ];
    DWORD bytesRead;

    while (g_KeepRunning) {
        HANDLE hCurrentPort = g_hSerial.load(std::memory_order_acquire);
        if (hCurrentPort == INVALID_HANDLE_VALUE) {
            Sleep(100); // No port open (disconnected/resetting); wait and re-check
            continue;
        }

        if (ReadFile(hCurrentPort, rxBuffer, BUF_SZ, &bytesRead, NULL) && bytesRead > 0) {
            streamAccumulator.insert(streamAccumulator.end(), rxBuffer, rxBuffer + bytesRead);

            std::vector<RadarReading> parsedReadings;
            size_t consumed = 0;

            while (ParseRadarStream(streamAccumulator, parsedReadings, consumed)) {
                if (consumed == LD_PACKET_SIZE && !parsedReadings.empty()) {
                    model->IngestReadings(parsedReadings);
                }
                streamAccumulator.erase(streamAccumulator.begin(), streamAccumulator.begin() + consumed);
                consumed = 0;
            }
        }
        Sleep(1); // Relieve Windows thread management pressure
    }
    return 0;
}

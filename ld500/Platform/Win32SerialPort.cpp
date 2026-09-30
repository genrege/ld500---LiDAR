#include "Win32SerialPort.h"
#include <setupapi.h>
#include <devguid.h>
#include <algorithm>

namespace {
    std::string NarrowUtf8(const std::wstring& wide) {
        if (wide.empty()) return {};
        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), NULL, 0, NULL, NULL);
        std::string result(sizeNeeded, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), sizeNeeded, NULL, NULL);
        return result;
    }

    std::wstring WidenUtf8(const std::string& narrow) {
        if (narrow.empty()) return {};
        int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), static_cast<int>(narrow.size()), NULL, 0);
        std::wstring result(sizeNeeded, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), static_cast<int>(narrow.size()), result.data(), sizeNeeded);
        return result;
    }
}

std::vector<SerialPortInfo> EnumerateSerialPorts() {
    std::vector<SerialPortInfo> results;

    HDEVINFO hDevInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) return results;

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); ++i) {
        wchar_t friendlyName[512] = { 0 };
        if (!SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME,
            NULL, reinterpret_cast<PBYTE>(friendlyName), sizeof(friendlyName), NULL)) {
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
                SerialPortInfo info;
                info.portName = NarrowUtf8(portName);
                info.deviceDesc = friendlyName[0] ? NarrowUtf8(friendlyName) : "Unknown device";
                results.push_back(info);
            }
            RegCloseKey(hDeviceKey);
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);

    std::sort(results.begin(), results.end(), [](const SerialPortInfo& a, const SerialPortInfo& b) {
        return a.portName < b.portName;
        });
    return results;
}

Win32SerialPort::~Win32SerialPort() {
    Close();
}

bool Win32SerialPort::Open(const std::string& portName, uint32_t baudRate) {
    Close();

    std::wstring devicePath = L"\\\\.\\" + WidenUtf8(portName);
    HANDLE hPort = CreateFileW(devicePath.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPort == INVALID_HANDLE_VALUE) return false;

    DCB dcb = { 0 }; dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(hPort, &dcb)) { CloseHandle(hPort); return false; }
    dcb.BaudRate = baudRate; dcb.ByteSize = 8; dcb.StopBits = ONESTOPBIT; dcb.Parity = NOPARITY;
    if (!SetCommState(hPort, &dcb)) { CloseHandle(hPort); return false; }

    COMMTIMEOUTS tm = { 0 }; tm.ReadIntervalTimeout = MAXDWORD; SetCommTimeouts(hPort, &tm);

    m_hPort = hPort;
    m_PortName = portName;
    m_BaudRate = baudRate;
    return true;
}

void Win32SerialPort::Close() {
    if (m_hPort != INVALID_HANDLE_VALUE) {
        CancelIoEx(m_hPort, NULL);
        CloseHandle(m_hPort);
        m_hPort = INVALID_HANDLE_VALUE;
    }
}

bool Win32SerialPort::IsOpen() const {
    return m_hPort != INVALID_HANDLE_VALUE;
}

int Win32SerialPort::Read(uint8_t* buffer, size_t bufferSize) {
    if (m_hPort == INVALID_HANDLE_VALUE) return -1;
    DWORD bytesRead = 0;
    if (!ReadFile(m_hPort, buffer, static_cast<DWORD>(bufferSize), &bytesRead, NULL)) return -1;
    return static_cast<int>(bytesRead);
}

bool Win32SerialPort::Reset() {
    std::string portName = m_PortName;
    uint32_t baudRate = m_BaudRate;
    Close();
    Sleep(250); // Give the OS/driver a moment to release the port before reopening it
    return Open(portName, baudRate);
}

std::unique_ptr<ISerialPort> CreatePlatformSerialPort() {
    return std::make_unique<Win32SerialPort>();
}

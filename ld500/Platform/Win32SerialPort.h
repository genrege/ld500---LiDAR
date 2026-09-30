#pragma once

// Windows implementation of ISerialPort for the ImGui shell, independent of the legacy
// Acquisition/SerialPort.h globals used by the Win32 shell (kept unmodified).

#include <windows.h>
#include <string>
#include "ISerialPort.h"

class Win32SerialPort : public ISerialPort {
public:
    Win32SerialPort() = default;
    ~Win32SerialPort() override;

    bool Open(const std::string& portName, uint32_t baudRate) override;
    void Close() override;
    bool IsOpen() const override;
    int Read(uint8_t* buffer, size_t bufferSize) override;
    bool Reset() override;

private:
    HANDLE m_hPort = INVALID_HANDLE_VALUE;
    std::string m_PortName;
    uint32_t m_BaudRate = 0;
};

#pragma once

// POSIX (macOS/Linux) implementation of ISerialPort for the ImGui shell, using termios.

#include <string>
#include "ISerialPort.h"

class PosixSerialPort : public ISerialPort {
public:
    PosixSerialPort() = default;
    ~PosixSerialPort() override;

    bool Open(const std::string& portName, uint32_t baudRate) override;
    void Close() override;
    bool IsOpen() const override;
    int Read(uint8_t* buffer, size_t bufferSize) override;
    bool Reset() override;

private:
    int m_Fd = -1;
    std::string m_PortName;
    uint32_t m_BaudRate = 0;
};

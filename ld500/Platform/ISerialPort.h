#pragma once

// Portable serial-port abstraction used only by the ImGui shell (both Windows and macOS). The
// legacy Win32 shell keeps using Acquisition/SerialPort.h's globals directly, untouched.

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

struct SerialPortInfo {
    std::string portName;    // e.g. "COM3" (Windows) or "/dev/cu.usbserial-XXXX" (macOS/Linux)
    std::string deviceDesc;  // Best-effort friendly description
};

class ISerialPort {
public:
    virtual ~ISerialPort() = default;

    // Opens and configures the given port (8N1) at the given baud rate.
    virtual bool Open(const std::string& portName, uint32_t baudRate) = 0;
    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;

    // Reads up to bufferSize bytes without blocking indefinitely. Returns bytes read (0 if none
    // currently available), or -1 if the port has failed/disconnected.
    virtual int Read(uint8_t* buffer, size_t bufferSize) = 0;

    // Closes and reopens the port with its last-configured name/baud rate.
    virtual bool Reset() = 0;
};

// Scans for available serial ports on this platform.
std::vector<SerialPortInfo> EnumerateSerialPorts();

// Constructs the ISerialPort implementation for the current platform (Win32SerialPort on
// Windows, PosixSerialPort on macOS/Linux).
std::unique_ptr<ISerialPort> CreatePlatformSerialPort();

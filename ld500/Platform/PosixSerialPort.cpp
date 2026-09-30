#include "PosixSerialPort.h"
#include <dirent.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <algorithm>

namespace {
    // Maps the handful of baud rates this app actually uses to termios' speed_t constants.
    // B230400 (the LD500's default) is a POSIX/BSD extension available on both macOS and Linux.
    speed_t ToSpeedT(uint32_t baudRate) {
        switch (baudRate) {
            case 9600:   return B9600;
            case 19200:  return B19200;
            case 38400:  return B38400;
            case 57600:  return B57600;
            case 115200: return B115200;
            case 230400: return B230400;
            default:     return B230400;
        }
    }

    // True if this /dev entry name looks like a serial device we should offer for connection:
    // macOS callout devices ("cu.*") and common Linux USB-serial/ACM device names.
    bool LooksLikeSerialDevice(const std::string& name) {
        static const char* kPrefixes[] = { "cu.", "ttyUSB", "ttyACM", "ttyS" };
        for (const char* prefix : kPrefixes) {
            if (name.rfind(prefix, 0) == 0) return true;
        }
        return false;
    }
}

std::vector<SerialPortInfo> EnumerateSerialPorts() {
    std::vector<SerialPortInfo> results;

    DIR* dir = opendir("/dev");
    if (dir == nullptr) return results;

    while (dirent* entry = readdir(dir)) {
        std::string name = entry->d_name;
        if (!LooksLikeSerialDevice(name)) continue;

        SerialPortInfo info;
        info.portName = "/dev/" + name;
        info.deviceDesc = name; // No IOKit/udev friendly-name lookup; the device path is descriptive enough
        results.push_back(info);
    }
    closedir(dir);

    std::sort(results.begin(), results.end(), [](const SerialPortInfo& a, const SerialPortInfo& b) {
        return a.portName < b.portName;
        });
    return results;
}

PosixSerialPort::~PosixSerialPort() {
    Close();
}

bool PosixSerialPort::Open(const std::string& portName, uint32_t baudRate) {
    Close();

    int fd = open(portName.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return false;

    termios tty{};
    if (tcgetattr(fd, &tty) != 0) { close(fd); return false; }

    cfmakeraw(&tty);
    speed_t speed = ToSpeedT(baudRate);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~PARENB;   // No parity
    tty.c_cflag &= ~CSTOPB;   // One stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;       // 8 data bits

    // VMIN=0/VTIME=1 (0.1s) approximates the Windows side's ReadIntervalTimeout=MAXDWORD
    // polling-read behavior: return whatever bytes are available rather than blocking to fill
    // the buffer.
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) { close(fd); return false; }

    m_Fd = fd;
    m_PortName = portName;
    m_BaudRate = baudRate;
    return true;
}

void PosixSerialPort::Close() {
    if (m_Fd >= 0) {
        close(m_Fd);
        m_Fd = -1;
    }
}

bool PosixSerialPort::IsOpen() const {
    return m_Fd >= 0;
}

int PosixSerialPort::Read(uint8_t* buffer, size_t bufferSize) {
    if (m_Fd < 0) return -1;
    ssize_t bytesRead = read(m_Fd, buffer, bufferSize);
    if (bytesRead < 0) {
        // EAGAIN just means "nothing available right now", not a real error/disconnect.
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
    }
    return static_cast<int>(bytesRead);
}

bool PosixSerialPort::Reset() {
    std::string portName = m_PortName;
    uint32_t baudRate = m_BaudRate;
    Close();
    usleep(250 * 1000); // Give the OS/driver a moment to release the port before reopening it
    return Open(portName, baudRate);
}

std::unique_ptr<ISerialPort> CreatePlatformSerialPort() {
    return std::make_unique<PosixSerialPort>();
}

#pragma once

// Portable radar protocol/data types shared by acquisition and the data model. No Windows
// dependency, so this header can move as-is to a future microcontroller port.

#include <cstdint>
#include <cstddef>

constexpr double PI = 3.14159265358979323846;

// Decoded, scaled measurement produced by ParseRadarStream() from a single packet point.
struct RadarReading {
    double   angleDegrees;
    uint16_t distanceMm;
    bool     isOutOfRange;
    uint16_t speedDegPerSec; // Motor speed this packet was captured at, straight from LdPacketLayout
};

// Internal hardware representations for exact block parsing alignment
#pragma pack(push, 1)
struct LdPoint {
    uint16_t distance;   // Distance value in mm
    uint8_t  intensity;  // Signal confidence score
};

struct LdPacketLayout {
    uint8_t  header;       // Fixed byte sequence 0x54
    uint8_t  ver_len;      // Fixed length descriptor 0x2C (Represents 12 points payload)
    uint16_t speed;        // Motor speed (degrees per second)
    uint16_t start_angle;  // Start angle in 0.01 degree intervals
    LdPoint  points[12];   // Compressed data stream array (12 points * 3 bytes = 36 bytes)
    uint16_t end_angle;    // End angle in 0.01 degree intervals
    uint16_t timestamp;    // Millisecond internal system counter loop
    uint8_t  crc8;         // Checksum byte validation
};
#pragma pack(pop)

constexpr size_t LD_PACKET_SIZE = sizeof(LdPacketLayout);

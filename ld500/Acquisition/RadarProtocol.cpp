#include "RadarProtocol.h"
#include <cstring>

// FIXED: Exact LD500 standard CRC-8 calculation (MSB-first, Polynomial 0x4D)
uint8_t CalcCrc8(const uint8_t* data, uint16_t len) {
    uint8_t crc = 0;
    for (uint16_t i = 0; i < len; i++) {
        crc = crc ^ data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x4D; // Correct LD500 hardware characteristic polynomial
            }
            else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

bool ParseRadarStream(const std::vector<uint8_t>& rawBuffer, std::vector<RadarReading>& outReadings, size_t& outBytesConsumed) {
    outReadings.clear();

    if (rawBuffer.size() < LD_PACKET_SIZE) return false;

    for (size_t i = 0; i <= rawBuffer.size() - LD_PACKET_SIZE; ++i) {
        // Match LD500 Header signature criteria
        if (rawBuffer[i] == 0x54 && rawBuffer[i + 1] == 0x2C) {

            // Align byte stream boundaries if signature isn't at index front
            if (i > 0) {
                outBytesConsumed = i;
                return true;
            }

            // Verify CRC validation byte across the target 46 trailing structural bytes
            uint8_t expectedCrc = rawBuffer[i + LD_PACKET_SIZE - 1];
            uint8_t calculatedCrc = CalcCrc8(&rawBuffer[i], static_cast<uint16_t>(LD_PACKET_SIZE - 1));

            if (calculatedCrc != expectedCrc) {
                // Checksum failed: throw away just the single corrupted header byte to align stream
                outBytesConsumed = 1;
                return true;
            }

            // Map buffer segment directly into structural layout safely
            LdPacketLayout packet;
            std::memcpy(&packet, &rawBuffer[i], LD_PACKET_SIZE);

            // Decode and scale angular metrics
            double startAngle = static_cast<double>(packet.start_angle) / 100.0;
            double endAngle = static_cast<double>(packet.end_angle) / 100.0;

            double angleDiff = endAngle - startAngle;
            if (angleDiff < 0) angleDiff += 360.0;

            // Linearly interpolate angles across all 12 packed sampling targets
            double angleStep = angleDiff / 11.0;

            for (int p = 0; p < 12; ++p) {
                RadarReading rd;
                rd.angleDegrees = startAngle + (angleStep * p);
                if (rd.angleDegrees >= 360.0) rd.angleDegrees -= 360.0;
                rd.speedDegPerSec = packet.speed;

                uint16_t dist = packet.points[p].distance;

                // Hardware standard signal error / out-of-bounds ceiling metric check
                if (dist == 0 || dist >= 65280) {
                    rd.distanceMm = 0;
                    rd.isOutOfRange = true;
                }
                else {
                    rd.distanceMm = dist;
                    rd.isOutOfRange = false;
                }
                outReadings.push_back(rd);
            }

            outBytesConsumed = LD_PACKET_SIZE;
            return true;
        }
    }

    if (rawBuffer.size() > 4096) { outBytesConsumed = 1; return true; }
    return false;
}

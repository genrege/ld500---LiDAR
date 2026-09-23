#pragma once

// Portable LD500 wire-protocol parsing: CRC and packet framing only, no Windows dependency.

#include <vector>
#include "RadarTypes.h"

uint8_t CalcCrc8(const uint8_t* data, uint16_t len);

// Scans rawBuffer for a valid LD500 packet, filling outReadings with its 12 decoded points and
// outBytesConsumed with how many leading bytes to drop from the caller's accumulator (either a
// full packet, a single byte to resync past corruption/noise, or a resync-alignment offset).
// Returns false only when rawBuffer is too short to contain a packet yet.
bool ParseRadarStream(const std::vector<uint8_t>& rawBuffer, std::vector<RadarReading>& outReadings, size_t& outBytesConsumed);

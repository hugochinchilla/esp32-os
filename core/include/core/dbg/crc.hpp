#pragma once

#include <cstddef>
#include <cstdint>

namespace dbg {

// CRC-16/CCITT-FALSE: polynomial 0x1021, start value 0xFFFF.
uint16_t crc16(const uint8_t *data, size_t size);

// CRC-32 as used by zlib and PNG. Pass the previous result to continue.
uint32_t crc32(const uint8_t *data, size_t size, uint32_t previous = 0);

} // namespace dbg

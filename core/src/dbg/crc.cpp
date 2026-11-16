#include "core/dbg/crc.hpp"

namespace dbg {

uint16_t crc16(const uint8_t *data, size_t size)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < size; i++) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

uint32_t crc32(const uint8_t *data, size_t size, uint32_t previous)
{
    // Half-byte table: small, and fast enough for a 300 KB screen.
    static const uint32_t table[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
        0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
        0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
    };
    uint32_t crc = ~previous;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        crc = table[crc & 0x0F] ^ (crc >> 4);
        crc = table[crc & 0x0F] ^ (crc >> 4);
    }
    return ~crc;
}

} // namespace dbg

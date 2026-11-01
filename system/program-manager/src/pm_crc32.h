// CRC-32/ISO-HDLC (IEEE 802.3, reflected polynomial 0xEDB88320, init and final XOR
// 0xFFFFFFFF; check value of "123456789" is 0xCBF43926). Bitwise, no table.
// Guards the .dsy header against truncation and bit errors; the payload is
// protected by SHA-256 (pm_sha256.h).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace pm {

inline uint32_t Crc32Update(uint32_t crc, const uint8_t* data, size_t len) {
    for(size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for(int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

inline uint32_t Crc32(const uint8_t* data, size_t len) {
    return Crc32Update(0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu;
}

} // namespace pm

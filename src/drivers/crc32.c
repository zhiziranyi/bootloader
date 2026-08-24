/**
 * CRC32 Software Implementation
 * Polynomial: 0xEDB88320 (reflected IEEE 802.3 / zlib CRC-32)
 */

#include "crc32.h"

/**
 * Calculate CRC32 for a buffer (standard CRC32, matches zlib.crc32).
 */
uint32_t CRC32_Calculate(const uint8_t *data, size_t len)
{
    return CRC32_Update(CRC32_INIT_VALUE, data, len) ^ 0xFFFFFFFFU;
}

/**
 * Update a CRC32 value incrementally. A bitwise implementation avoids a
 * hand-maintained lookup table, which had become truncated and corrupted.
 */
uint32_t CRC32_Update(uint32_t crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint32_t bit = 0U; bit < 8U; bit++) {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320U) : (crc >> 1);
        }
    }
    return crc;
}

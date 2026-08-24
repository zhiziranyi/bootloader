/**
 * CRC32 Software Implementation
 */

#ifndef __CRC32_H
#define __CRC32_H

#include <stdint.h>
#include <stddef.h>

/* CRC32 Functions */
uint32_t CRC32_Calculate(const uint8_t *data, size_t len);
uint32_t CRC32_Update(uint32_t crc, const uint8_t *data, size_t len);

/* Standard CRC32 init value */
#define CRC32_INIT_VALUE    0xFFFFFFFF

#endif /* __CRC32_H */

// SPDX-License-Identifier: MIT
// CRC32 implementation for bootloader and applications

#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>
#include <stddef.h>

// Calculate CRC32 of a buffer (polynomial: 0xEDB88320, IEEE 802.3)
uint32_t crc32(const void *data, size_t len);

// Calculate CRC32 incrementally (for streaming data)
uint32_t crc32_init(void);
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);
uint32_t crc32_final(uint32_t crc);

#endif // CRC32_H

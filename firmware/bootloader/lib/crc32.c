// SPDX-License-Identifier: MIT
// CRC32 implementation (IEEE 802.3 polynomial)

#include "crc32.h"

// Byte-wise CRC32 calculation (no lookup table to save flash)
// Uses polynomial 0xEDB88320 (reversed form of 0x04C11DB7)
static uint32_t crc32_byte(uint32_t crc, uint8_t byte) {
  crc ^= byte;
  for (int i = 0; i < 8; i++) {
    if (crc & 1)
      crc = (crc >> 1) ^ 0xEDB88320;
    else
      crc >>= 1;
  }
  return crc;
}

uint32_t crc32_init(void) {
  return 0xFFFFFFFF;
}

uint32_t crc32_update(uint32_t crc, const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  while (len--)
    crc = crc32_byte(crc, *p++);
  return crc;
}

uint32_t crc32_final(uint32_t crc) {
  return crc ^ 0xFFFFFFFF;
}

uint32_t crc32(const void *data, size_t len) {
  return crc32_final(crc32_update(crc32_init(), data, len));
}

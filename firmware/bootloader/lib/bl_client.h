// SPDX-License-Identifier: MIT
// Bootloader client API for applications

#ifndef BL_CLIENT_H
#define BL_CLIENT_H

#include <stdint.h>
#include "bl_protocol.h"

// Initialize bootloader client
// Call this early in main() before other initialization
void bl_client_init(void);

// Read a bootloader client register (0xE0-0xE7)
// Returns register value, or 0xFF for invalid registers
uint8_t bl_client_read_register(uint8_t reg);

// Write a bootloader client register (0xE0-0xE7)
// Returns 1 if write was valid and processed, 0 otherwise
// Note: Writing 0xAA to REG_APP_UPDATE_CMD triggers reset into bootloader
uint8_t bl_client_write_register(uint8_t reg, uint8_t value);

// Process multi-byte write to a register (called from I2C handler)
// buf contains the data bytes (not including register address)
// len is the number of data bytes
void bl_client_process_write(uint8_t reg, const volatile uint8_t *buf, uint8_t len);

// Check if this register is handled by bl_client
// Returns 1 if reg is in 0xE0-0xE7 range
static inline uint8_t bl_client_handles_register(uint8_t reg) {
  return (reg >= REG_APP_BL_VERSION && reg <= REG_APP_UPDATE_CRC_3);
}

// Get stored update size (for verification after update)
uint16_t bl_client_get_update_size(void);

// Get stored update CRC (for verification after update)
uint32_t bl_client_get_update_crc(void);

#endif // BL_CLIENT_H

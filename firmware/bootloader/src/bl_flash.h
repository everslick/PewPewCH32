// SPDX-License-Identifier: MIT
// Flash operations for bootloader

#ifndef BL_FLASH_H
#define BL_FLASH_H

#include <stdint.h>

// Initialize flash module
void bl_flash_init(void);

// Unlock flash for writing
// Returns 1 on success, 0 on failure
uint8_t bl_flash_unlock(void);

// Lock flash (re-enable write protection)
void bl_flash_lock(void);

// Erase application area (from BL_APP_HEADER_ADDR to BL_FLASH_END)
// Returns 1 on success, 0 on failure
uint8_t bl_flash_erase_app(void);

// Erase a single 64-byte page
// addr must be page-aligned
// Returns 1 on success, 0 on failure
uint8_t bl_flash_erase_page(uint32_t addr);

// Write a 64-byte page
// addr must be page-aligned
// data must point to 64 bytes of data
// Returns 1 on success, 0 on failure
uint8_t bl_flash_write_page(uint32_t addr, const uint8_t *data);

// Verify written data using CRC32
// Calculates CRC32 from start_addr for size bytes
// Returns calculated CRC32
uint32_t bl_flash_calculate_crc(uint32_t start_addr, uint32_t size);

// Clear boot state (erase boot state page)
// Called after successful boot or failed update
// Returns 1 on success, 0 on failure
uint8_t bl_flash_clear_boot_state(void);

#endif // BL_FLASH_H

// SPDX-License-Identifier: MIT
// I2C slave for bootloader

#ifndef BL_I2C_H
#define BL_I2C_H

#include <stdint.h>

// Initialize I2C slave
void bl_i2c_init(uint8_t address);

// Process pending I2C commands (call from main loop)
void bl_i2c_process_commands(void);

// Get current bootloader status
uint8_t bl_i2c_get_status(void);

// Get last error code
uint8_t bl_i2c_get_error(void);

#endif // BL_I2C_H

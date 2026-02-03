// SPDX-License-Identifier: MIT
// Bootloader configuration

#ifndef BL_CONFIG_H
#define BL_CONFIG_H

#include <stdint.h>
#include "../lib/bl_protocol.h"

// System configuration
#define SYSTEM_CORE_CLOCK     48000000   // 48MHz
#define SYSTICK_FREQ          1000       // 1kHz (1ms tick)

// I2C configuration
#define I2C_ADDRESS           BL_I2C_ADDRESS  // 0x42

// Pin assignments (CH32V003J4M6 SOP-8 package)
// Same as emonio-wd for compatibility
// Pin 1: PD6 - STATUS_LED (active low, push-pull)
// Pin 3: PA2 - ERROR_LED (active low, push-pull)
// Pin 5: PC1 - I2C SDA (open-drain)
// Pin 6: PC2 - I2C SCL (open-drain)

// LED timing (bootloader uses rapid double-blink pattern)
#define LED_DOUBLE_BLINK_ON   100        // 100ms on
#define LED_DOUBLE_BLINK_OFF  100        // 100ms off
#define LED_DOUBLE_BLINK_GAP  500        // 500ms gap between pairs

// Internal watchdog (IWDG)
#define IWDG_TIMEOUT_MS       2000       // 2 second timeout
#define IWDG_PRESCALER        0x03       // 32 prescaler
#define IWDG_RELOAD_VALUE     2500       // 2s at 40kHz/32 = 1.25kHz

// Hardware type (from emonio-wd config)
#define HW_TYPE_X4            4

// Bootloader version
#define BL_VERSION_MAJOR      1
#define BL_VERSION_MINOR      0

// Flash timing
#define FLASH_WRITE_TIMEOUT   1000       // 1 second timeout for flash operations

// Page data buffer size
#define PAGE_BUFFER_SIZE      BL_FLASH_PAGE_SIZE  // 64 bytes

#endif // BL_CONFIG_H

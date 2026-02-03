// SPDX-License-Identifier: MIT
// Bootloader protocol definitions shared between bootloader and applications

#ifndef BL_PROTOCOL_H
#define BL_PROTOCOL_H

#include <stdint.h>

// Memory layout
#define BL_FLASH_BASE         0x00000000
#define BL_BOOTLOADER_SIZE    0x00000C00  // 3KB bootloader
#define BL_BOOT_STATE_ADDR    0x00000C00  // 64B boot state page
#define BL_APP_HEADER_ADDR    0x00000C40  // 64B app header
#define BL_APP_CODE_ADDR      0x00000C80  // Application code start
#define BL_FLASH_END          0x00004000  // 16KB total flash
#define BL_APP_MAX_SIZE       (BL_FLASH_END - BL_APP_CODE_ADDR)  // ~12.9KB

// Flash page size for CH32V003
#define BL_FLASH_PAGE_SIZE    64

// Protocol version
#define BL_PROTOCOL_VERSION   1

// I2C address (shared with application)
#define BL_I2C_ADDRESS        0x42

// HW_TYPE register bit indicating bootloader mode
#define BL_MODE_FLAG          0x80

// Magic values
#define BL_APP_MAGIC          0x454D4F57  // "WOME" (little-endian)
#define BL_BOOT_STATE_MAGIC   0x424F4F54  // "BOOT" (little-endian)

// Boot state values
#define BL_STATE_NORMAL       0x00
#define BL_STATE_UPDATE       0x01

// Update command trigger value
#define BL_UPDATE_TRIGGER     0xAA

// Common registers (0x00-0x0F) - same in app and bootloader
#define REG_HW_TYPE           0x00  // Hardware type (R) - bit 7 set in bootloader mode
#define REG_FW_VER_MAJOR      0x01  // Firmware major version (R)
#define REG_FW_VER_MINOR      0x02  // Firmware minor version (R)

// Application update registers (0xE0-0xE7) - handled by bl_client in apps
#define REG_APP_BL_VERSION    0xE0  // Bootloader version readback (R)
#define REG_APP_UPDATE_CMD    0xE1  // Write 0xAA to enter update mode (W)
#define REG_APP_UPDATE_SIZE_L 0xE2  // Expected firmware size low byte (W)
#define REG_APP_UPDATE_SIZE_H 0xE3  // Expected firmware size high byte (W)
#define REG_APP_UPDATE_CRC_0  0xE4  // Expected CRC32 byte 0 (LSB) (W)
#define REG_APP_UPDATE_CRC_1  0xE5  // Expected CRC32 byte 1 (W)
#define REG_APP_UPDATE_CRC_2  0xE6  // Expected CRC32 byte 2 (W)
#define REG_APP_UPDATE_CRC_3  0xE7  // Expected CRC32 byte 3 (MSB) (W)

// Bootloader registers (0xF0-0xFF) - only available in bootloader mode
#define REG_BL_VERSION        0xF0  // Bootloader protocol version (R)
#define REG_BL_STATUS         0xF1  // Status: 0=idle, 1=busy, 0x40=success, 0x80=error (R)
#define REG_BL_ERROR          0xF2  // Last error code (R)
#define REG_BL_RESERVED_F3    0xF3  // Reserved
#define REG_BL_RESERVED_F4    0xF4  // Reserved
#define REG_BL_RESERVED_F5    0xF5  // Reserved
#define REG_BL_RESERVED_F6    0xF6  // Reserved
#define REG_BL_RESERVED_F7    0xF7  // Reserved
#define REG_BL_CMD            0xF8  // Command (W)
#define REG_BL_ADDR_L         0xF9  // Page address low byte (W)
#define REG_BL_ADDR_H         0xFA  // Page address high byte (W)
#define REG_BL_DATA           0xFB  // 64-byte page data buffer (W)
#define REG_BL_CRC_0          0xFC  // Expected CRC32 byte 0 (LSB) (R/W)
#define REG_BL_CRC_1          0xFD  // Expected CRC32 byte 1 (R/W)
#define REG_BL_CRC_2          0xFE  // Expected CRC32 byte 2 (R/W)
#define REG_BL_CRC_3          0xFF  // Expected CRC32 byte 3 (MSB) (R/W)

// Bootloader commands
#define BL_CMD_ERASE          0x01  // Erase app area
#define BL_CMD_WRITE          0x02  // Write page from data buffer
#define BL_CMD_VERIFY         0x03  // Verify CRC of written data
#define BL_CMD_BOOT           0x04  // Boot application

// Bootloader status values
#define BL_STATUS_IDLE        0x00
#define BL_STATUS_BUSY        0x01
#define BL_STATUS_SUCCESS     0x40
#define BL_STATUS_ERROR       0x80

// Bootloader error codes
#define BL_ERR_NONE           0x00
#define BL_ERR_INVALID_CMD    0x01
#define BL_ERR_INVALID_ADDR   0x02
#define BL_ERR_FLASH_ERASE    0x03
#define BL_ERR_FLASH_WRITE    0x04
#define BL_ERR_CRC_MISMATCH   0x05
#define BL_ERR_APP_INVALID    0x06
#define BL_ERR_TIMEOUT        0x07

// App header structure (64 bytes at 0x0C40)
typedef struct __attribute__((packed)) {
  uint32_t magic;           // 0x454D4F57 ("WOME")
  uint8_t  fw_ver_major;    // Firmware major version
  uint8_t  fw_ver_minor;    // Firmware minor version
  uint8_t  bl_ver_min;      // Minimum bootloader version required
  uint8_t  hw_type;         // Hardware type (must match, or 0 for generic)
  uint32_t app_size;        // Application code size in bytes
  uint32_t app_crc32;       // CRC32 of application code
  uint32_t entry_point;     // Entry address (typically 0x0C80)
  uint32_t header_crc32;    // CRC32 of header bytes 0-23
  uint8_t  reserved[40];    // Reserved for future use (pad to 64 bytes)
} app_header_t;

// Boot state structure (64 bytes at 0x0C00)
typedef struct __attribute__((packed)) {
  uint32_t magic;           // 0x424F4F54 ("BOOT") when update requested
  uint8_t  state;           // 0=normal, 1=update
  uint8_t  reserved[59];    // Pad to 64 bytes (flash page size)
} boot_state_t;

// Compile-time assertions
_Static_assert(sizeof(app_header_t) == 64, "app_header_t must be 64 bytes");
_Static_assert(sizeof(boot_state_t) == 64, "boot_state_t must be 64 bytes");

#endif // BL_PROTOCOL_H

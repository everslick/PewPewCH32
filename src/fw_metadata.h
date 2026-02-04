// Firmware metadata structure for reading embedded metadata from CH32 binaries
// This is a copy of the structure defined in emonio-ext/common/fw_metadata.h

#ifndef FW_METADATA_H
#define FW_METADATA_H

#include <stdint.h>
#include <string.h>

// Magic value "KEXT" in little-endian
#define FW_METADATA_MAGIC     0x5458454B

// Fixed offset from firmware start (256 bytes, after interrupt vector table)
#define FW_METADATA_OFFSET    0x100

// Hardware types
#define FW_HW_TYPE_GENERIC    0x00
#define FW_HW_TYPE_WATCHDOG   0x04

// Firmware types (stored in flags field, bit 0)
#ifndef FW_TYPE_BOOT
#define FW_TYPE_BOOT          0x00  // Standalone firmware (bootloader or standalone app)
#endif
#ifndef FW_TYPE_APP
#define FW_TYPE_APP           0x01  // Application firmware (runs under bootloader, needs app header)
#endif

// Load addresses (derived from type)
#define FW_LOAD_ADDR_BOOT     0x00000000  // BOOT type loads at 0x0000
#define FW_LOAD_ADDR_APP      0x00000C80  // APP type loads at 0x0C80

// App header address (for APP type firmware)
#define FW_APP_HEADER_ADDR    0x00000C40

// Firmware metadata structure (32 bytes)
typedef struct __attribute__((packed)) {
  uint32_t magic;          // 0x5458454B "KEXT" (little-endian)
  uint32_t load_addr;      // Load address (0x0000 for BOOT, 0x0C80 for APP)
  uint8_t  hw_type;        // Hardware/extension type (0=generic, 4=watchdog, etc.)
  uint8_t  version_major;  // Firmware major version
  uint8_t  version_minor;  // Firmware minor version
  uint8_t  flags;          // Bit 0: firmware type (0=BOOT, 1=APP)
  char     name[16];       // Null-terminated firmware name
  uint32_t reserved;       // Reserved for future use
} fw_metadata_t;

// App header structure (64 bytes at 0x0C40) - must match bootloader's app_header_t
#define APP_HEADER_MAGIC      0x454D4F57  // "WOME" in little-endian

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

// Read embedded firmware metadata from binary at fixed offset
// Returns true if valid metadata found (magic matches), false otherwise
static inline bool read_fw_metadata(const uint8_t *data, uint32_t size, fw_metadata_t *meta) {
  if (!data || !meta) return false;
  if (size < FW_METADATA_OFFSET + sizeof(fw_metadata_t)) return false;

  memcpy(meta, data + FW_METADATA_OFFSET, sizeof(fw_metadata_t));

  return (meta->magic == FW_METADATA_MAGIC);
}

// Get firmware type from flags
static inline uint8_t get_fw_type(const fw_metadata_t *meta) {
  return meta->flags & 0x01;
}

// Check if firmware is APP type (needs app header)
static inline bool is_app_firmware(const fw_metadata_t *meta) {
  return (meta->flags & FW_TYPE_APP) != 0;
}

#endif // FW_METADATA_H

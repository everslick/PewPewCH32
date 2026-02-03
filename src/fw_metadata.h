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

// Flags
#define FW_FLAG_BOOTLOADER    0x01  // Firmware is bootloader-compatible

// Load addresses
#define FW_LOAD_ADDR_STANDALONE   0x00000000
#define FW_LOAD_ADDR_BOOTLOADER   0x00000C80

// Firmware metadata structure (32 bytes)
typedef struct __attribute__((packed)) {
  uint32_t magic;          // 0x5458454B "KEXT" (little-endian)
  uint32_t load_addr;      // 0x00000000 (standalone) or 0x00000C80 (bootloader)
  uint8_t  hw_type;        // Hardware/extension type (0=generic, 4=watchdog, etc.)
  uint8_t  version_major;  // Firmware major version
  uint8_t  version_minor;  // Firmware minor version
  uint8_t  flags;          // Bit 0: bootloader-compatible
  char     name[16];       // Null-terminated firmware name
  uint32_t reserved;       // Reserved for future use
} fw_metadata_t;

// Read embedded firmware metadata from binary at fixed offset
// Returns true if valid metadata found (magic matches), false otherwise
static inline bool read_fw_metadata(const uint8_t *data, uint32_t size, fw_metadata_t *meta) {
  if (!data || !meta) return false;
  if (size < FW_METADATA_OFFSET + sizeof(fw_metadata_t)) return false;

  memcpy(meta, data + FW_METADATA_OFFSET, sizeof(fw_metadata_t));

  return (meta->magic == FW_METADATA_MAGIC);
}

#endif // FW_METADATA_H

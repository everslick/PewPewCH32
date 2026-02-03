// SPDX-License-Identifier: MIT
// Bootloader client implementation for applications

#include "bl_client.h"
#include "bl_protocol.h"
#include "ch32fun.h"

// Boot state page location in flash (for writing update request)
#define BOOT_STATE_FLASH_ADDR  BL_BOOT_STATE_ADDR

// Update parameters stored by master before triggering update
static uint16_t update_size = 0;
static uint32_t update_crc = 0;

// Read bootloader version from flash (stored in bootloader area)
static uint8_t read_bootloader_version(void) {
  // Bootloader stores its version at a fixed location
  // For now return protocol version; actual version could be stored in bootloader
  return BL_PROTOCOL_VERSION;
}

void bl_client_init(void) {
  // Nothing to initialize currently
  // Could read boot state to detect if we just came from bootloader
}

uint8_t bl_client_read_register(uint8_t reg) {
  switch (reg) {
    case REG_APP_BL_VERSION:
      return read_bootloader_version();

    case REG_APP_UPDATE_SIZE_L:
      return update_size & 0xFF;

    case REG_APP_UPDATE_SIZE_H:
      return (update_size >> 8) & 0xFF;

    case REG_APP_UPDATE_CRC_0:
      return update_crc & 0xFF;

    case REG_APP_UPDATE_CRC_1:
      return (update_crc >> 8) & 0xFF;

    case REG_APP_UPDATE_CRC_2:
      return (update_crc >> 16) & 0xFF;

    case REG_APP_UPDATE_CRC_3:
      return (update_crc >> 24) & 0xFF;

    default:
      return 0xFF;
  }
}

// Write boot state to flash to request update mode
static void write_boot_state_update(void) {
  boot_state_t state;
  state.magic = BL_BOOT_STATE_MAGIC;
  state.state = BL_STATE_UPDATE;

  // Fill reserved bytes with 0xFF (erased state)
  for (int i = 0; i < (int)sizeof(state.reserved); i++)
    state.reserved[i] = 0xFF;

  // Unlock flash
  FLASH->KEYR = 0x45670123;
  FLASH->KEYR = 0xCDEF89AB;

  // Wait for flash to be ready
  while (FLASH->STATR & FLASH_STATR_BSY);

  // Erase boot state page (64 bytes)
  FLASH->CTLR |= FLASH_CTLR_PER;
  FLASH->ADDR = BOOT_STATE_FLASH_ADDR;
  FLASH->CTLR |= FLASH_CTLR_STRT;
  while (FLASH->STATR & FLASH_STATR_BSY);
  FLASH->CTLR &= ~FLASH_CTLR_PER;

  // Write boot state (word by word)
  FLASH->CTLR |= FLASH_CTLR_PG;
  const uint8_t *src = (const uint8_t *)&state;
  volatile uint32_t *dst = (volatile uint32_t *)BOOT_STATE_FLASH_ADDR;
  for (int i = 0; i < (int)(sizeof(state) / 4); i++) {
    // Assemble word from bytes to avoid unaligned access
    uint32_t word = src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24);
    *dst++ = word;
    src += 4;
    while (FLASH->STATR & FLASH_STATR_BSY);
  }
  FLASH->CTLR &= ~FLASH_CTLR_PG;

  // Lock flash
  FLASH->CTLR |= FLASH_CTLR_LOCK;
}

// Trigger system reset
static void system_reset(void) {
  NVIC_SystemReset();
  while (1);  // Wait for reset
}

uint8_t bl_client_write_register(uint8_t reg, uint8_t value) {
  switch (reg) {
    case REG_APP_UPDATE_CMD:
      if (value == BL_UPDATE_TRIGGER) {
        // Write boot state to request update mode
        write_boot_state_update();
        // Trigger reset - will boot into bootloader
        system_reset();
        // Never returns
      }
      return 1;

    case REG_APP_UPDATE_SIZE_L:
      update_size = (update_size & 0xFF00) | value;
      return 1;

    case REG_APP_UPDATE_SIZE_H:
      update_size = (update_size & 0x00FF) | ((uint16_t)value << 8);
      return 1;

    case REG_APP_UPDATE_CRC_0:
      update_crc = (update_crc & 0xFFFFFF00) | value;
      return 1;

    case REG_APP_UPDATE_CRC_1:
      update_crc = (update_crc & 0xFFFF00FF) | ((uint32_t)value << 8);
      return 1;

    case REG_APP_UPDATE_CRC_2:
      update_crc = (update_crc & 0xFF00FFFF) | ((uint32_t)value << 16);
      return 1;

    case REG_APP_UPDATE_CRC_3:
      update_crc = (update_crc & 0x00FFFFFF) | ((uint32_t)value << 24);
      return 1;

    default:
      return 0;
  }
}

void bl_client_process_write(uint8_t reg, const volatile uint8_t *buf, uint8_t len) {
  // Process sequential writes starting from reg
  for (uint8_t i = 0; i < len; i++) {
    bl_client_write_register(reg + i, buf[i]);
  }
}

uint16_t bl_client_get_update_size(void) {
  return update_size;
}

uint32_t bl_client_get_update_crc(void) {
  return update_crc;
}

// SPDX-License-Identifier: MIT
// Flash operations for bootloader

#include "bl_flash.h"
#include "bl_config.h"
#include "../lib/bl_protocol.h"
#include "../lib/crc32.h"
#include "ch32fun.h"

void bl_flash_init(void) {
  // Ensure flash is locked on init
  bl_flash_lock();
}

uint8_t bl_flash_unlock(void) {
  // Check if already unlocked
  if (!(FLASH->CTLR & FLASH_CTLR_LOCK))
    return 1;

  // Unlock sequence
  FLASH->KEYR = 0x45670123;
  FLASH->KEYR = 0xCDEF89AB;

  // Wait for unlock
  uint32_t timeout = FLASH_WRITE_TIMEOUT * 1000;
  while ((FLASH->CTLR & FLASH_CTLR_LOCK) && timeout--)
    ;

  return !(FLASH->CTLR & FLASH_CTLR_LOCK);
}

void bl_flash_lock(void) {
  FLASH->CTLR |= FLASH_CTLR_LOCK;
}

static uint8_t wait_for_flash(void) {
  uint32_t timeout = FLASH_WRITE_TIMEOUT * 1000;
  while ((FLASH->STATR & FLASH_STATR_BSY) && timeout--)
    ;

  if (FLASH->STATR & FLASH_STATR_BSY)
    return 0;

  // Check for write protection error
  if (FLASH->STATR & FLASH_STATR_WRPRTERR) {
    FLASH->STATR = FLASH_STATR_WRPRTERR;  // Clear error
    return 0;
  }

  return 1;
}

uint8_t bl_flash_erase_page(uint32_t addr) {
  // Ensure page-aligned
  if (addr & (BL_FLASH_PAGE_SIZE - 1))
    return 0;

  // Don't allow erasing bootloader area
  if (addr < BL_BOOT_STATE_ADDR)
    return 0;

  if (!wait_for_flash())
    return 0;

  // Set page erase mode
  FLASH->CTLR |= FLASH_CTLR_PER;
  FLASH->ADDR = addr;
  FLASH->CTLR |= FLASH_CTLR_STRT;

  if (!wait_for_flash()) {
    FLASH->CTLR &= ~FLASH_CTLR_PER;
    return 0;
  }

  FLASH->CTLR &= ~FLASH_CTLR_PER;
  return 1;
}

uint8_t bl_flash_erase_app(void) {
  if (!bl_flash_unlock())
    return 0;

  // Erase from app header to end of flash
  // This includes boot state, app header, and app code
  for (uint32_t addr = BL_BOOT_STATE_ADDR; addr < BL_FLASH_END;
       addr += BL_FLASH_PAGE_SIZE) {
    if (!bl_flash_erase_page(addr)) {
      bl_flash_lock();
      return 0;
    }
  }

  bl_flash_lock();
  return 1;
}

uint8_t bl_flash_write_page(uint32_t addr, const uint8_t *data) {
  // Ensure page-aligned
  if (addr & (BL_FLASH_PAGE_SIZE - 1))
    return 0;

  // Don't allow writing to bootloader area
  if (addr < BL_BOOT_STATE_ADDR)
    return 0;

  // Don't allow writing past end of flash
  if (addr >= BL_FLASH_END)
    return 0;

  if (!bl_flash_unlock())
    return 0;

  if (!wait_for_flash()) {
    bl_flash_lock();
    return 0;
  }

  // Enable programming mode
  FLASH->CTLR |= FLASH_CTLR_PG;

  // Write 32-bit words (CH32V003 requires word writes)
  const uint32_t *src = (const uint32_t *)data;
  volatile uint32_t *dst = (volatile uint32_t *)addr;

  for (int i = 0; i < BL_FLASH_PAGE_SIZE / 4; i++) {
    *dst++ = *src++;
    if (!wait_for_flash()) {
      FLASH->CTLR &= ~FLASH_CTLR_PG;
      bl_flash_lock();
      return 0;
    }
  }

  FLASH->CTLR &= ~FLASH_CTLR_PG;
  bl_flash_lock();

  // Verify write
  const uint8_t *verify = (const uint8_t *)addr;
  for (int i = 0; i < BL_FLASH_PAGE_SIZE; i++) {
    if (verify[i] != data[i])
      return 0;
  }

  return 1;
}

uint32_t bl_flash_calculate_crc(uint32_t start_addr, uint32_t size) {
  return crc32((const void *)start_addr, size);
}

uint8_t bl_flash_clear_boot_state(void) {
  if (!bl_flash_unlock())
    return 0;

  // Erase boot state page - this clears the update request
  uint8_t result = bl_flash_erase_page(BL_BOOT_STATE_ADDR);

  bl_flash_lock();
  return result;
}

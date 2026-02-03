// SPDX-License-Identifier: MIT
// I2C slave for bootloader

#include "bl_i2c.h"
#include "bl_config.h"
#include "bl_flash.h"
#include "../lib/bl_protocol.h"
#include "ch32fun.h"

// Bootloader state
static volatile uint8_t bl_status = BL_STATUS_IDLE;
static volatile uint8_t bl_error = BL_ERR_NONE;

// Page buffer and addressing
static uint8_t page_buffer[PAGE_BUFFER_SIZE];
static volatile uint8_t page_index = 0;
static volatile uint16_t page_addr = 0;

// CRC for verification
static volatile uint32_t expected_crc = 0;

// I2C state
static volatile uint8_t reg_addr = 0;
static volatile uint8_t addr_received = 0;
static volatile uint8_t tx_mode = 0;
static volatile uint8_t pending_command = 0;

// Forward declaration
static void execute_command(uint8_t cmd);

static uint8_t get_hw_type(void) {
  // Read HW type from app header if valid, otherwise return 0
  const app_header_t *hdr = (const app_header_t *)BL_APP_HEADER_ADDR;
  if (hdr->magic == BL_APP_MAGIC)
    return hdr->hw_type;
  return 0;  // Unknown/generic
}

static uint8_t read_register(uint8_t reg) {
  switch (reg) {
    case REG_HW_TYPE:
      return get_hw_type() | BL_MODE_FLAG;  // Bit 7 set = bootloader mode

    case REG_FW_VER_MAJOR:
      return BL_VERSION_MAJOR;

    case REG_FW_VER_MINOR:
      return BL_VERSION_MINOR;

    case REG_BL_VERSION:
      return BL_PROTOCOL_VERSION;

    case REG_BL_STATUS:
      return bl_status;

    case REG_BL_ERROR:
      return bl_error;

    case REG_BL_CRC_0:
      return expected_crc & 0xFF;

    case REG_BL_CRC_1:
      return (expected_crc >> 8) & 0xFF;

    case REG_BL_CRC_2:
      return (expected_crc >> 16) & 0xFF;

    case REG_BL_CRC_3:
      return (expected_crc >> 24) & 0xFF;

    default:
      return 0xFF;
  }
}

void bl_i2c_init(uint8_t address) {
  // Enable clocks for I2C1 and GPIOC
  RCC->APB2PCENR |= RCC_APB2Periph_GPIOC;
  RCC->APB1PCENR |= RCC_APB1Periph_I2C1;

  // Configure PC1 (SDA) and PC2 (SCL) as alternate function open-drain
  GPIOC->CFGLR &= ~(0xF << (1 * 4));
  GPIOC->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_OD_AF) << (1 * 4);

  GPIOC->CFGLR &= ~(0xF << (2 * 4));
  GPIOC->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_OD_AF) << (2 * 4);

  I2C1->CTLR1 = 0;
  I2C1->CTLR2 = (SYSTEM_CORE_CLOCK / 1000000) & 0x3F;
  I2C1->OADDR1 = (address << 1);

  // CCR = PCLK1 / (2 * 100kHz) = 48MHz / 200kHz = 240
  I2C1->CKCFGR = 0xF0;

  I2C1->CTLR2 |= I2C_CTLR2_ITEVTEN | I2C_CTLR2_ITBUFEN | I2C_CTLR2_ITERREN;
  I2C1->CTLR1 |= I2C_CTLR1_PE;
  I2C1->CTLR1 |= I2C_CTLR1_ACK;

  NVIC_EnableIRQ(I2C1_EV_IRQn);
  NVIC_EnableIRQ(I2C1_ER_IRQn);
}

void I2C1_EV_IRQHandler(void) __attribute__((interrupt));
void I2C1_EV_IRQHandler(void) {
  uint32_t star1 = I2C1->STAR1;

  // Address matched
  if (star1 & I2C_STAR1_ADDR) {
    uint32_t star2 = I2C1->STAR2;
    tx_mode = (star2 & I2C_STAR2_TRA) ? 1 : 0;

    if (tx_mode) {
      I2C1->DATAR = read_register(reg_addr);
      reg_addr++;
    } else {
      addr_received = 0;
      page_index = 0;
    }
  }

  // Receive buffer not empty
  if (star1 & I2C_STAR1_RXNE) {
    uint8_t data = I2C1->DATAR;

    if (!addr_received) {
      reg_addr = data;
      addr_received = 1;
      page_index = 0;
    } else {
      // Handle write based on register
      if (reg_addr == REG_BL_DATA) {
        // Page data buffer - accumulate bytes
        if (page_index < PAGE_BUFFER_SIZE)
          page_buffer[page_index++] = data;
      } else if (reg_addr == REG_BL_ADDR_L) {
        page_addr = (page_addr & 0xFF00) | data;
      } else if (reg_addr == REG_BL_ADDR_H) {
        page_addr = (page_addr & 0x00FF) | ((uint16_t)data << 8);
      } else if (reg_addr == REG_BL_CRC_0) {
        expected_crc = (expected_crc & 0xFFFFFF00) | data;
      } else if (reg_addr == REG_BL_CRC_1) {
        expected_crc = (expected_crc & 0xFFFF00FF) | ((uint32_t)data << 8);
      } else if (reg_addr == REG_BL_CRC_2) {
        expected_crc = (expected_crc & 0xFF00FFFF) | ((uint32_t)data << 16);
      } else if (reg_addr == REG_BL_CRC_3) {
        expected_crc = (expected_crc & 0x00FFFFFF) | ((uint32_t)data << 24);
      } else if (reg_addr == REG_BL_CMD) {
        pending_command = data;
      }
    }
  }

  // Transmit buffer empty
  if ((star1 & I2C_STAR1_TXE) && tx_mode && !(star1 & I2C_STAR1_ADDR)) {
    I2C1->DATAR = read_register(reg_addr);
    reg_addr++;
  }

  // Stop condition
  if (star1 & I2C_STAR1_STOPF) {
    I2C1->CTLR1 |= I2C_CTLR1_PE;  // Clears STOPF
    tx_mode = 0;
    addr_received = 0;
  }
}

void I2C1_ER_IRQHandler(void) __attribute__((interrupt));
void I2C1_ER_IRQHandler(void) {
  uint32_t star1 = I2C1->STAR1;

  if (star1 & I2C_STAR1_BERR)
    I2C1->STAR1 &= ~I2C_STAR1_BERR;
  if (star1 & I2C_STAR1_ARLO)
    I2C1->STAR1 &= ~I2C_STAR1_ARLO;
  if (star1 & I2C_STAR1_AF)
    I2C1->STAR1 &= ~I2C_STAR1_AF;
  if (star1 & I2C_STAR1_OVR)
    I2C1->STAR1 &= ~I2C_STAR1_OVR;

  tx_mode = 0;
  addr_received = 0;
}

static void execute_command(uint8_t cmd) {
  bl_status = BL_STATUS_BUSY;
  bl_error = BL_ERR_NONE;

  switch (cmd) {
    case BL_CMD_ERASE:
      if (!bl_flash_erase_app()) {
        bl_error = BL_ERR_FLASH_ERASE;
        bl_status = BL_STATUS_ERROR;
      } else {
        bl_status = BL_STATUS_SUCCESS;
      }
      break;

    case BL_CMD_WRITE: {
      // Calculate actual flash address from page_addr
      // page_addr is the offset from app header area
      uint32_t flash_addr = BL_APP_HEADER_ADDR + page_addr;

      // Validate address
      if (flash_addr < BL_APP_HEADER_ADDR || flash_addr >= BL_FLASH_END ||
          (flash_addr & (BL_FLASH_PAGE_SIZE - 1))) {
        bl_error = BL_ERR_INVALID_ADDR;
        bl_status = BL_STATUS_ERROR;
        break;
      }

      // Erase page first (if not already erased by BL_CMD_ERASE)
      // The page might have leftover data if partial update
      if (!bl_flash_write_page(flash_addr, page_buffer)) {
        bl_error = BL_ERR_FLASH_WRITE;
        bl_status = BL_STATUS_ERROR;
      } else {
        bl_status = BL_STATUS_SUCCESS;
      }
      break;
    }

    case BL_CMD_VERIFY: {
      // Verify CRC of application area
      // Read app header to get size
      const app_header_t *header = (const app_header_t *)BL_APP_HEADER_ADDR;

      if (header->magic != BL_APP_MAGIC) {
        bl_error = BL_ERR_APP_INVALID;
        bl_status = BL_STATUS_ERROR;
        break;
      }

      uint32_t calc_crc = bl_flash_calculate_crc(BL_APP_CODE_ADDR, header->app_size);
      if (calc_crc != expected_crc) {
        bl_error = BL_ERR_CRC_MISMATCH;
        bl_status = BL_STATUS_ERROR;
      } else {
        bl_status = BL_STATUS_SUCCESS;
      }
      break;
    }

    case BL_CMD_BOOT:
      // Set status to success - main loop will handle actual boot
      bl_status = BL_STATUS_SUCCESS;
      break;

    default:
      bl_error = BL_ERR_INVALID_CMD;
      bl_status = BL_STATUS_ERROR;
      break;
  }
}

void bl_i2c_process_commands(void) {
  if (pending_command) {
    uint8_t cmd = pending_command;
    pending_command = 0;
    execute_command(cmd);
  }
}

uint8_t bl_i2c_get_status(void) {
  return bl_status;
}

uint8_t bl_i2c_get_error(void) {
  return bl_error;
}

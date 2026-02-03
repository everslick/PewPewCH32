// SPDX-License-Identifier: MIT
// CH32V003 I2C Bootloader

#include "ch32fun.h"
#include "bl_config.h"
#include "bl_flash.h"
#include "bl_i2c.h"
#include "../lib/bl_protocol.h"
#include "../lib/crc32.h"

#define POST_NO_APP         1
#define POST_INVALID_HEADER 2
#define POST_CRC_MISMATCH   3

static volatile uint32_t tick_counter = 0;
static uint8_t post_code = 0;

void SysTick_Handler(void) __attribute__((interrupt));
void SysTick_Handler(void) {
  SysTick->CMP += DELAY_MS_TIME;
  SysTick->SR = 0;
  tick_counter++;
}

static void led_init(void) {
  RCC->APB2PCENR |= RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOA;
  GPIOD->CFGLR &= ~(0xF << (6 * 4));
  GPIOD->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP) << (6 * 4);
  GPIOD->BSHR = (1 << 6);
  GPIOA->CFGLR &= ~(0xF << (2 * 4));
  GPIOA->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP) << (2 * 4);
  GPIOA->BSHR = (1 << 2);
}

static uint8_t validate_app(void) {
  const app_header_t *hdr = (const app_header_t *)BL_APP_HEADER_ADDR;

  if (hdr->magic == 0xFFFFFFFF)
    return POST_NO_APP;
  if (hdr->magic != BL_APP_MAGIC)
    return POST_INVALID_HEADER;
  if (hdr->entry_point != BL_APP_CODE_ADDR)
    return POST_INVALID_HEADER;
  if (hdr->app_size == 0 || hdr->app_size > BL_APP_MAX_SIZE)
    return POST_INVALID_HEADER;
  if (crc32(hdr, 24) != hdr->header_crc32)
    return POST_INVALID_HEADER;
  if (crc32((const void *)BL_APP_CODE_ADDR, hdr->app_size) != hdr->app_crc32)
    return POST_CRC_MISMATCH;

  return 0;
}

static void jump_to_app(void) __attribute__((noreturn));
static void jump_to_app(void) {
  __disable_irq();
  SysTick->CTLR = 0;
  GPIOD->BSHR = (1 << 6);
  GPIOA->BSHR = (1 << 2);
  ((void (*)(void))BL_APP_CODE_ADDR)();
  while(1);
}

int main(void) {
  SystemInit();

  // SysTick 1kHz
  SysTick->CTLR = 0;
  SysTick->CMP = DELAY_MS_TIME - 1;
  SysTick->CNT = 0;
  SysTick->CTLR = SYSTICK_CTLR_STE | SYSTICK_CTLR_STIE | SYSTICK_CTLR_STCLK;
  NVIC_EnableIRQ(SysTick_IRQn);

  led_init();

  // Startup pattern: 3 fast alternating blinks
  for (int i = 0; i < 6; i++) {
    if (i % 2) {
      GPIOD->BCR = (1 << 6);   // Status on
      GPIOA->BSHR = (1 << 2);  // Error off
    } else {
      GPIOD->BSHR = (1 << 6);  // Status off
      GPIOA->BCR = (1 << 2);   // Error on
    }
    Delay_Ms(100);
  }
  GPIOD->BSHR = (1 << 6);
  GPIOA->BSHR = (1 << 2);
  Delay_Ms(200);

  // Validate app
  post_code = validate_app();

  if (post_code == 0) {
    jump_to_app();
  }

  // Initialize I2C for bootloader mode
  bl_flash_init();
  bl_i2c_init(BL_I2C_ADDRESS);

  // Bootloader mode - show POST code and process I2C
  while (1) {
    // Process I2C commands
    bl_i2c_process_commands();

    // Check if boot command was received and app is now valid
    if (bl_i2c_get_status() == BL_STATUS_SUCCESS) {
      if (validate_app() == 0) {
        Delay_Ms(10);  // Allow I2C to complete
        jump_to_app();
      }
    }

    // Status LED solid on
    GPIOD->BCR = (1 << 6);

    // Error LED: POST code flashes
    uint32_t cycle = tick_counter % 2000;
    uint32_t flash_time = post_code * 300;

    if (cycle < flash_time) {
      if ((cycle % 300) < 150)
        GPIOA->BCR = (1 << 2);
      else
        GPIOA->BSHR = (1 << 2);
    } else {
      GPIOA->BSHR = (1 << 2);
    }
  }

  return 0;
}

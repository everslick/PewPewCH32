// Bootloader-compatible blink example
// Provides minimal I2C registers for identification and firmware update

#include "ch32fun.h"
#include "bl_client.h"
#include "bl_protocol.h"

// Hardware type and version
#define HW_TYPE_BLINK     0x00  // Generic blink example
#define FW_VERSION_MAJOR  1
#define FW_VERSION_MINOR  0

// I2C state
static volatile uint8_t reg_addr = 0;
static volatile uint8_t write_buf[8];
static volatile uint8_t write_index = 0;
static volatile uint8_t pending_action = 0;
static volatile uint8_t addr_received = 0;
static volatile uint8_t tx_mode = 0;

static uint8_t read_register(uint8_t reg) {
  // Handle bootloader client registers (0xE0-0xE7)
  if (bl_client_handles_register(reg))
    return bl_client_read_register(reg);

  switch (reg) {
    case REG_HW_TYPE:       return HW_TYPE_BLINK;
    case REG_FW_VER_MAJOR:  return FW_VERSION_MAJOR;
    case REG_FW_VER_MINOR:  return FW_VERSION_MINOR;
    default:                return 0xFF;
  }
}

static void i2c_init(uint8_t address) {
  // Enable clocks for I2C1 and GPIOC
  RCC->APB2PCENR |= RCC_APB2Periph_GPIOC;
  RCC->APB1PCENR |= RCC_APB1Periph_I2C1;

  // Configure PC1 (SDA) and PC2 (SCL) as alternate function open-drain
  GPIOC->CFGLR &= ~(0xF << (1 * 4));
  GPIOC->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_OD_AF) << (1 * 4);

  GPIOC->CFGLR &= ~(0xF << (2 * 4));
  GPIOC->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_OD_AF) << (2 * 4);

  I2C1->CTLR1 = 0;
  I2C1->CTLR2 = 48;  // 48MHz / 1MHz = 48
  I2C1->OADDR1 = (address << 1);

  // CCR for 100kHz: 48MHz / (2 * 100kHz) = 240
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

  if (star1 & I2C_STAR1_ADDR) {
    uint32_t star2 = I2C1->STAR2;
    tx_mode = (star2 & I2C_STAR2_TRA) ? 1 : 0;

    if (tx_mode) {
      I2C1->DATAR = read_register(reg_addr);
      reg_addr++;
    } else {
      addr_received = 0;
      write_index = 0;
    }
  }

  if (star1 & I2C_STAR1_RXNE) {
    uint8_t data = I2C1->DATAR;

    if (!addr_received) {
      reg_addr = data;
      addr_received = 1;
      write_index = 0;
    } else {
      if (write_index < sizeof(write_buf))
        write_buf[write_index++] = data;
    }
  }

  if ((star1 & I2C_STAR1_TXE) && tx_mode && !(star1 & I2C_STAR1_ADDR)) {
    I2C1->DATAR = read_register(reg_addr);
    reg_addr++;
  }

  if (star1 & I2C_STAR1_STOPF) {
    I2C1->CTLR1 |= I2C_CTLR1_PE;

    if (!tx_mode && addr_received && write_index > 0)
      pending_action = 1;

    tx_mode = 0;
    addr_received = 0;
  }
}

void I2C1_ER_IRQHandler(void) __attribute__((interrupt));
void I2C1_ER_IRQHandler(void) {
  uint32_t star1 = I2C1->STAR1;

  if (star1 & I2C_STAR1_BERR) I2C1->STAR1 &= ~I2C_STAR1_BERR;
  if (star1 & I2C_STAR1_ARLO) I2C1->STAR1 &= ~I2C_STAR1_ARLO;
  if (star1 & I2C_STAR1_AF)   I2C1->STAR1 &= ~I2C_STAR1_AF;
  if (star1 & I2C_STAR1_OVR)  I2C1->STAR1 &= ~I2C_STAR1_OVR;

  tx_mode = 0;
  addr_received = 0;
}

static void process_commands(void) {
  if (!pending_action) return;
  pending_action = 0;

  // Handle bootloader client registers
  if (bl_client_handles_register(reg_addr)) {
    bl_client_process_write(reg_addr, write_buf, write_index);
  }
}

int main(void) {
  SystemInit();

  // Initialize bootloader client
  bl_client_init();

  // Enable GPIOD
  RCC->APB2PCENR |= RCC_APB2Periph_GPIOD;

  // PD6 push-pull output (STATUS LED)
  GPIOD->CFGLR &= ~(0xF << (4 * 6));
  GPIOD->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP) << (4 * 6);

  // Initialize I2C slave
  i2c_init(BL_I2C_ADDRESS);

  // Enable interrupts
  __enable_irq();

  while (1) {
    // Process any pending I2C commands
    process_commands();

    // Toggle LED
    GPIOD->OUTDR ^= 1 << 6;
    Delay_Ms(1000);
  }
}

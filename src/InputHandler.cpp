#include "InputHandler.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

#include "main.h"

// Encoder quadrature state, updated from GPIO interrupt
static volatile int32_t encoder_count = 0;
static volatile uint8_t encoder_state = 0;

// Indexed by (previous AB << 2) | current AB; invalid transitions count 0
static const int8_t ENCODER_TABLE[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
};

static uint8_t readEncoderPins() {
    return (gpio_get(PIN_ENC_CLK) << 1) | gpio_get(PIN_ENC_DIR);
}

static void encoderIrqHandler(uint gpio, uint32_t events) {
    (void)gpio;
    (void)events;
    encoder_state = ((encoder_state << 2) | readEncoderPins()) & 0x0F;
    encoder_count += ENCODER_TABLE[encoder_state];
}

InputHandler::InputHandler() 
    : last_trigger_time(0),
      enc_button_pressed(false),
      last_enc_button_time(0),
      bootsel_pressed(false),
      bootsel_press_start(0),
      long_press_triggered(false) {
}

InputHandler::~InputHandler() {
}

void InputHandler::init() {
    // Initialize trigger pin with pull-up
    gpio_init(PIN_TRIGGER);
    gpio_set_dir(PIN_TRIGGER, GPIO_IN);
    gpio_pull_up(PIN_TRIGGER);

    // Initialize rotary encoder pins. The board has external pull-ups and
    // series resistors (RC filter), internal pull-ups would form a voltage
    // divider with the series resistors and prevent a valid low level.
    const uint enc_pins[] = { PIN_ENC_ENTER, PIN_ENC_DIR, PIN_ENC_CLK };
    for (uint pin : enc_pins) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_disable_pulls(pin);
    }

    // Decode encoder quadrature on every edge of both phases
    encoder_state = readEncoderPins();
    gpio_set_irq_enabled_with_callback(PIN_ENC_CLK,
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &encoderIrqHandler);
    gpio_set_irq_enabled(PIN_ENC_DIR,
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
}

bool InputHandler::checkTriggerButton() {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    // Check for trigger button press (active low with debounce)
    if (!gpio_get(PIN_TRIGGER)) {
        if ((now - last_trigger_time) > TRIGGER_DEBOUNCE_MS) {
            last_trigger_time = now;
            return true;
        }
    }
    return false;
}

bool InputHandler::checkEncoderButton() {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool pressed = !gpio_get(PIN_ENC_ENTER);  // Active low
    bool event = false;

    // Report press edge only, ignore changes within debounce window
    if (pressed != enc_button_pressed &&
        (now - last_enc_button_time) > TRIGGER_DEBOUNCE_MS) {
        enc_button_pressed = pressed;
        last_enc_button_time = now;
        event = pressed;
    }
    return event;
}

int InputHandler::getEncoderSteps() {
    uint32_t flags = save_and_disable_interrupts();
    int32_t count = encoder_count;
    int steps = count / ENCODER_STEPS_PER_DETENT;
    encoder_count = count - steps * ENCODER_STEPS_PER_DETENT;
    restore_interrupts(flags);
    return steps;
}

bool __no_inline_not_in_flash_func(InputHandler::getBootselButtonState)() {
    const uint CS_PIN_INDEX = 1;

    uint32_t flags = save_and_disable_interrupts();

    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
        GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
        IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    for (volatile int i = 0; i < 1000; ++i);

#if PICO_RP2040
    #define CS_BIT (1u << 1)
#else
    #define CS_BIT SIO_GPIO_HI_IN_QSPI_CSN_BITS
#endif
    bool button_state = !(sio_hw->gpio_hi_in & CS_BIT);

    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
        GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
        IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);

    return button_state;
}

bool InputHandler::checkBootselButton() {
    return getBootselButtonState();
}

InputHandler::ButtonEvent InputHandler::getBootselEvent() {
    bool bootsel_current = checkBootselButton();
    uint32_t now = to_ms_since_boot(get_absolute_time());
    ButtonEvent event = BUTTON_NONE;
    
    if (bootsel_current && !bootsel_pressed) {
        // Button just pressed - record start time
        bootsel_pressed = true;
        bootsel_press_start = now;
        long_press_triggered = false;
    } else if (bootsel_current && bootsel_pressed && !long_press_triggered) {
        // Button still held - check if we've reached long press threshold
        if ((now - bootsel_press_start) >= BOOTSEL_LONG_PRESS_MS) {
            long_press_triggered = true;
            event = BUTTON_LONG_PRESS;
        } else {
            event = BUTTON_HELD;
        }
    } else if (!bootsel_current && bootsel_pressed) {
        // Button just released
        uint32_t press_duration = now - bootsel_press_start;
        bootsel_pressed = false;
        
        if (!long_press_triggered && press_duration < BOOTSEL_SHORT_PRESS_MS) {
            event = BUTTON_SHORT_PRESS;
        }
        
        long_press_triggered = false;
    }
    
    return event;
}

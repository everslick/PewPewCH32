#include "InputHandler.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

InputHandler::InputHandler() 
    : last_trigger_time(0),
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
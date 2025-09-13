#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include <stdint.h>

// Pin definitions
#define PIN_TRIGGER     1   // Active low

// Timing definitions
#define TRIGGER_DEBOUNCE_MS     50
#define BOOTSEL_SHORT_PRESS_MS  250
#define BOOTSEL_LONG_PRESS_MS   750

class InputHandler {
public:
    InputHandler();
    ~InputHandler();
    
    void init();
    
    // Check for input events
    bool checkTriggerButton();
    bool checkBootselButton();
    
    // Get button states
    enum ButtonEvent {
        BUTTON_NONE,
        BUTTON_SHORT_PRESS,
        BUTTON_LONG_PRESS,
        BUTTON_HELD
    };
    
    ButtonEvent getBootselEvent();
    
private:
    // Trigger button state
    uint32_t last_trigger_time;
    
    // Bootsel button state
    bool bootsel_pressed;
    uint32_t bootsel_press_start;
    bool long_press_triggered;
    
    // Helper function for BOOTSEL
    bool getBootselButtonState();
};

#endif // INPUT_HANDLER_H
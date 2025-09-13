#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <stdint.h>
#include "pico/stdlib.h"

// Pin definitions
#define PIN_WS2812      16  // WS2812 RGB LED on Waveshare Pico Zero
#define PIN_LED_GREEN   27
#define PIN_LED_YELLOW  28
#define PIN_LED_RED     29

// WS2812 brightness (0-255)
#define LED_BRIGHTNESS 64

// Timing definitions
#define HEARTBEAT_PERIOD_MS     3000
#define LED_FLASH_DURATION_MS   100

// LED state structure
struct led_state_t {
    uint32_t timer;
    bool active;
    bool flash_on;
    int flash_count;
    int flashes_done;
    uint32_t flash_duration_ms;
};

class LedController {
public:
    LedController();
    ~LedController();
    
    // Initialize all LEDs
    void init();
    
    // WS2812 RGB LED control
    void setRgbColor(uint8_t r, uint8_t g, uint8_t b);
    void rgbOff();
    void rainbowAnimation();
    
    // GPIO LED control
    void setGreenLed(bool state);
    void setYellowLed(bool state);
    void setRedLed(bool state);
    void setAllGpioLeds(bool state);
    
    // Combined LED patterns
    void startHeartbeat();
    void updateHeartbeat();
    void stopHeartbeat();
    
    void startProgrammingBlink();
    void updateProgrammingBlink();
    void stopProgrammingBlink();
    
    void startErrorIndication();
    void updateErrorIndication();
    void stopErrorIndication();
    
    void startFirmwareIndication(int firmware_index);
    void updateFirmwareIndication();
    
    // Main update function
    void update();
    
    // Check if firmware indication is active
    bool isFirmwareIndicationActive() const { return firmware_led.active; }
    
private:
    // LED states
    led_state_t heartbeat_led;
    led_state_t programming_led;
    led_state_t error_led;
    led_state_t firmware_led;
    
    // Helper functions
    void hsvToRgb(float h, float s, float v, uint8_t* r, uint8_t* g, uint8_t* b);
};

#endif // LED_CONTROLLER_H
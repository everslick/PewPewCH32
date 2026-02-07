#include "LedController.h"
#include <math.h>
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"

// WS2812 PIO implementation
static PIO ws2812_pio = pio1;
static uint ws2812_sm = 0;

LedController::LedController() {
    // Initialize LED states
    heartbeat_led = {0, false, false, 0, 0, 100};
    programming_led = {0, false, false, 0, 0, 100};
    error_led = {0, false, false, 0, 0, 0};
    firmware_led = {0, false, false, 0, 0, 100};
}

LedController::~LedController() {
    // Cleanup
    rgbOff();
    setAllGpioLeds(false);
}

void LedController::init() {
    // Initialize WS2812 RGB LED
    uint offset = pio_add_program(ws2812_pio, &ws2812_program);
    ws2812_program_init(ws2812_pio, ws2812_sm, offset, PIN_WS2812, 800000, false);
    rgbOff();
    
    // Initialize GPIO LEDs (active low)
    gpio_init(PIN_LED_GREEN);
    gpio_set_dir(PIN_LED_GREEN, GPIO_OUT);
    gpio_put(PIN_LED_GREEN, true);  // Off (active low)
    
    gpio_init(PIN_LED_YELLOW);
    gpio_set_dir(PIN_LED_YELLOW, GPIO_OUT);
    gpio_put(PIN_LED_YELLOW, true);  // Off (active low)
    
    gpio_init(PIN_LED_RED);
    gpio_set_dir(PIN_LED_RED, GPIO_OUT);
    gpio_put(PIN_LED_RED, true);  // Off (active low)
}

void LedController::setRgbColor(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t pixel = ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;
    pio_sm_put_blocking(ws2812_pio, ws2812_sm, pixel << 8u);
}

void LedController::rgbOff() {
    setRgbColor(0, 0, 0);
}

void LedController::setGreenLed(bool state) {
    gpio_put(PIN_LED_GREEN, !state);  // Active low
}

void LedController::setYellowLed(bool state) {
    gpio_put(PIN_LED_YELLOW, !state);  // Active low
}

void LedController::setRedLed(bool state) {
    gpio_put(PIN_LED_RED, !state);  // Active low
}

void LedController::setAllGpioLeds(bool state) {
    setGreenLed(state);
    setYellowLed(state);
    setRedLed(state);
}

void LedController::hsvToRgb(float h, float s, float v, uint8_t* r, uint8_t* g, uint8_t* b) {
    float c = v * s;
    float x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1));
    float m = v - c;
    
    float r_prime, g_prime, b_prime;
    
    if (h >= 0 && h < 60) {
        r_prime = c; g_prime = x; b_prime = 0;
    } else if (h >= 60 && h < 120) {
        r_prime = x; g_prime = c; b_prime = 0;
    } else if (h >= 120 && h < 180) {
        r_prime = 0; g_prime = c; b_prime = x;
    } else if (h >= 180 && h < 240) {
        r_prime = 0; g_prime = x; b_prime = c;
    } else if (h >= 240 && h < 300) {
        r_prime = x; g_prime = 0; b_prime = c;
    } else {
        r_prime = c; g_prime = 0; b_prime = x;
    }
    
    *r = (uint8_t)((r_prime + m) * 255);
    *g = (uint8_t)((g_prime + m) * 255);
    *b = (uint8_t)((b_prime + m) * 255);
}

void LedController::rainbowAnimation() {
    const int duration_ms = 3000;
    const int steps = 150;
    const int step_delay = duration_ms / steps;
    
    for (int step = 0; step < steps; step++) {
        float hue = (step * 360.0f) / steps;
        float progress = (float)step / (steps - 1);
        float brightness;
        
        if (progress <= 0.5f) {
            brightness = progress * 2.0f;
        } else {
            brightness = 2.0f - (progress * 2.0f);
        }
        
        uint8_t r, g, b;
        hsvToRgb(hue, 1.0f, brightness, &r, &g, &b);
        setRgbColor(r, g, b);
        sleep_ms(step_delay);
    }
    
    rgbOff();
}

void LedController::startHeartbeat() {
    heartbeat_led.active = true;
    heartbeat_led.timer = to_ms_since_boot(get_absolute_time());
}

void LedController::updateHeartbeat() {
    if (!heartbeat_led.active) return;
    
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    if (heartbeat_led.timer == 0) {
        heartbeat_led.timer = now;
    }
    
    if (!heartbeat_led.flash_on && (now - heartbeat_led.timer) >= HEARTBEAT_PERIOD_MS) {
        heartbeat_led.flash_on = true;
        heartbeat_led.timer = now;
        setRgbColor(0, 32, 0);   // Green heartbeat
        setGreenLed(true);
    }
    
    if (heartbeat_led.flash_on && (now - heartbeat_led.timer) >= 100) {
        heartbeat_led.flash_on = false;
        heartbeat_led.timer = now;
        rgbOff();
        setGreenLed(false);
    }
}

void LedController::stopHeartbeat() {
    heartbeat_led.active = false;
    heartbeat_led.flash_on = false;
    setGreenLed(false);
}

void LedController::startProgrammingBlink() {
    programming_led.active = true;
    programming_led.timer = to_ms_since_boot(get_absolute_time());
    programming_led.flash_on = false;
    programming_led.flash_duration_ms = 100;
}

void LedController::updateProgrammingBlink() {
    if (!programming_led.active) return;
    
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((now - programming_led.timer) >= programming_led.flash_duration_ms) {
        programming_led.flash_on = !programming_led.flash_on;
        setYellowLed(programming_led.flash_on);
        
        if (programming_led.flash_on) {
            setRgbColor(LED_BRIGHTNESS, LED_BRIGHTNESS, 0);  // Yellow
        } else {
            rgbOff();
        }
        
        programming_led.timer = now;
    }
}

void LedController::stopProgrammingBlink() {
    programming_led.active = false;
    setYellowLed(false);
    rgbOff();
}

void LedController::startErrorIndication() {
    error_led.active = true;
    error_led.timer = to_ms_since_boot(get_absolute_time());
    setRgbColor(255, 0, 0);  // Bright red
    setRedLed(true);
}

void LedController::updateErrorIndication() {
    if (!error_led.active) return;
    
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((now - error_led.timer) >= 2000) {  // 2 second error display
        stopErrorIndication();
    }
}

void LedController::stopErrorIndication() {
    error_led.active = false;
    setRedLed(false);
    rgbOff();
}

void LedController::startFirmwareIndication(int firmware_index) {
    firmware_led.active = true;
    firmware_led.flash_count = firmware_index + 1;
    firmware_led.timer = to_ms_since_boot(get_absolute_time());
    firmware_led.flash_on = false;
    firmware_led.flashes_done = 0;
    firmware_led.flash_duration_ms = 100;
    indication_r = 0; indication_g = 0; indication_b = 255;  // Blue
}

void LedController::startWipeIndication() {
    firmware_led.active = true;
    firmware_led.flash_count = 3;
    firmware_led.timer = to_ms_since_boot(get_absolute_time());
    firmware_led.flash_on = false;
    firmware_led.flashes_done = 0;
    firmware_led.flash_duration_ms = 100;
    indication_r = 255; indication_g = 0; indication_b = 0;  // Red
}

void LedController::startRebootIndication() {
    firmware_led.active = true;
    firmware_led.flash_count = 2;
    firmware_led.timer = to_ms_since_boot(get_absolute_time());
    firmware_led.flash_on = false;
    firmware_led.flashes_done = 0;
    firmware_led.flash_duration_ms = 100;
    indication_r = 0; indication_g = 255; indication_b = 0;  // Green
}

void LedController::updateFirmwareIndication() {
    if (!firmware_led.active) return;
    
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((now - firmware_led.timer) >= firmware_led.flash_duration_ms) {
        firmware_led.flash_on = !firmware_led.flash_on;
        if (firmware_led.flash_on) {
            setRgbColor(indication_r, indication_g, indication_b);
            if (indication_r > 0 && indication_b == 0) setRedLed(true);
            firmware_led.flashes_done++;
        } else {
            rgbOff();
            setRedLed(false);
            if (firmware_led.flashes_done >= firmware_led.flash_count) {
                firmware_led.active = false;
                firmware_led.flashes_done = 0;
                firmware_led.flash_on = false;
            }
        }
        firmware_led.timer = now;
    }
}

void LedController::update() {
    updateHeartbeat();
    updateProgrammingBlink();
    updateErrorIndication();
    updateFirmwareIndication();
}
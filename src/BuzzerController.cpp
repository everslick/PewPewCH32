#include "BuzzerController.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

BuzzerController::BuzzerController() : buzzer_slice(0) {
}

BuzzerController::~BuzzerController() {
    off();
}

void BuzzerController::init() {
    // Configure PWM for buzzer
    gpio_set_function(PIN_BUZZER, GPIO_FUNC_PWM);
    buzzer_slice = pwm_gpio_to_slice_num(PIN_BUZZER);
    
    // Set initial frequency (will be updated by setFrequency)
    pwm_set_wrap(buzzer_slice, 0);
    pwm_set_chan_level(buzzer_slice, PWM_CHAN_A, 0);
    pwm_set_enabled(buzzer_slice, false);
}

void BuzzerController::setFrequency(uint32_t frequency) {
    if (frequency == 0) {
        pwm_set_enabled(buzzer_slice, false);
        return;
    }
    
    // Calculate divider and wrap values for desired frequency
    // PWM frequency = 125MHz / (divider * wrap)
    uint32_t clock_freq = 125000000;
    uint32_t divider = 1;
    uint32_t wrap = clock_freq / (frequency * divider);
    
    // Adjust divider if wrap is too large
    while (wrap > 65535 && divider < 256) {
        divider++;
        wrap = clock_freq / (frequency * divider);
    }
    
    pwm_set_clkdiv(buzzer_slice, (float)divider);
    pwm_set_wrap(buzzer_slice, wrap);
    pwm_set_chan_level(buzzer_slice, PWM_CHAN_A, wrap / 2);  // 50% duty cycle
}

void BuzzerController::on(uint32_t frequency) {
    setFrequency(frequency);
    pwm_set_enabled(buzzer_slice, true);
}

void BuzzerController::off() {
    pwm_set_enabled(buzzer_slice, false);
}

void BuzzerController::beep(uint32_t frequency, int duration_ms) {
    on(frequency);
    sleep_ms(duration_ms);
    off();
}

void BuzzerController::beepStart() {
    beep(BUZZER_FREQ_START, BUZZER_DURATION_MS);
}

void BuzzerController::beepSuccess() {
    beep(BUZZER_FREQ_SUCCESS, BUZZER_DURATION_MS);
}

void BuzzerController::beepFailure() {
    beep(BUZZER_FREQ_FAILURE, 300);
}

void BuzzerController::beepWarning() {
    beep(BUZZER_FREQ_WARNING, 150);
}
#ifndef BUZZER_CONTROLLER_H
#define BUZZER_CONTROLLER_H

#include <stdint.h>
#include "pico/types.h"

// Pin definition
#define PIN_BUZZER      0

// Buzzer frequencies (Hz)
#define BUZZER_FREQ_DEFAULT     4000   // 4KHz default
#define BUZZER_FREQ_START       2000   // Start tone
#define BUZZER_FREQ_SUCCESS     4000   // Success tone
#define BUZZER_FREQ_FAILURE     1000   // Failure tone
#define BUZZER_FREQ_WARNING     3000   // Warning tone

// Duration
#define BUZZER_DURATION_MS      500

class BuzzerController {
public:
    BuzzerController();
    ~BuzzerController();
    
    void init();
    
    // Basic control
    void on(uint32_t frequency);
    void off();
    
    // Convenience functions
    void beep(uint32_t frequency, int duration_ms);
    void beepStart();
    void beepSuccess();
    void beepFailure();
    void beepWarning();
    
private:
    uint buzzer_slice;
    void setFrequency(uint32_t frequency);
};

#endif // BUZZER_CONTROLLER_H
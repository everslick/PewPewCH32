#ifndef SETUP_SCREEN_H
#define SETUP_SCREEN_H

#include <stdint.h>

class Settings;
class DisplayController;
struct PicoSWIO;
class RVDebug;
class StateMachine;

// Sleep timeout options (milliseconds); index 0 = off
inline constexpr uint32_t SLEEP_TIMEOUT_OPTIONS[] = { 0, 60000, 180000, 300000, 600000 };
inline constexpr const char* SLEEP_TIMEOUT_LABELS[] = { "off", "1 min", "3 min", "5 min", "10 min" };
inline constexpr int SLEEP_TIMEOUT_COUNT = sizeof(SLEEP_TIMEOUT_OPTIONS) / sizeof(SLEEP_TIMEOUT_OPTIONS[0]);

// Usable GPIO pins for SWIO (excludes pins used by other peripherals)
inline constexpr uint8_t SWIO_PIN_OPTIONS[] = {
    2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 17, 18, 19, 20, 21, 22, 23, 24, 25, 27, 28, 29
};
inline constexpr int SWIO_PIN_COUNT = sizeof(SWIO_PIN_OPTIONS) / sizeof(SWIO_PIN_OPTIONS[0]);

enum SetupResult {
    RESULT_PENDING,
    RESULT_SAVED,
    RESULT_CANCELLED
};

class SetupScreen {
public:
    SetupScreen();

    void enter(Settings* settings);
    SetupResult processInput(int c);
    void applyToHardware(Settings* settings, DisplayController* display,
                         PicoSWIO* swio, RVDebug* rvd, StateMachine* state_machine,
                         int* swio_pin_out);
    void drawTerminal();

private:
    static const int NUM_ROWS = 3;

    int selected_row;
    bool edit_display_flip;
    int edit_sleep_timeout_idx;
    int edit_swio_pin_idx;

    int findSwioPinIndex(uint8_t pin);
};

#endif // SETUP_SCREEN_H

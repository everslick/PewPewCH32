#ifndef SETUP_SCREEN_H
#define SETUP_SCREEN_H

#include <stdint.h>

class Settings;
class DisplayController;

// Sleep timeout options (milliseconds); index 0 = off
inline constexpr uint32_t SLEEP_TIMEOUT_OPTIONS[] = { 0, 60000, 180000, 300000, 600000 };
inline constexpr const char* SLEEP_TIMEOUT_LABELS[] = { "off", "1 min", "3 min", "5 min", "10 min" };
inline constexpr int SLEEP_TIMEOUT_COUNT = sizeof(SLEEP_TIMEOUT_OPTIONS) / sizeof(SLEEP_TIMEOUT_OPTIONS[0]);

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
    void applyToHardware(Settings* settings, DisplayController* display);
    void drawTerminal();

private:
    static const int NUM_ROWS = 2;

    int selected_row;
    bool edit_display_flip;
    int edit_sleep_timeout_idx;
};

#endif // SETUP_SCREEN_H

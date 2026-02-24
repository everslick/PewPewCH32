#include "SetupScreen.h"
#include "Settings.h"
#include "DisplayController.h"
#include "StateMachine.h"
#include "PicoSWIO.h"
#include "RVDebug.h"
#include "pico/stdlib.h"
#include <stdio.h>

extern const char* const PROGRAMMER_VERSION;

SetupScreen::SetupScreen()
    : selected_row(0), edit_display_flip(false),
      edit_sleep_timeout_idx(3), edit_swio_pin_idx(0) {
}

int SetupScreen::findSwioPinIndex(uint8_t pin) {
    for (int i = 0; i < SWIO_PIN_COUNT; i++) {
        if (SWIO_PIN_OPTIONS[i] == pin) return i;
    }
    return 4;  // default to GPIO 8 (index 4)
}

void SetupScreen::enter(Settings* settings) {
    selected_row = 0;
    edit_display_flip = settings->getDisplayFlip();
    edit_sleep_timeout_idx = settings->getSleepTimeoutIndex();
    if (edit_sleep_timeout_idx >= SLEEP_TIMEOUT_COUNT)
        edit_sleep_timeout_idx = 3;
    edit_swio_pin_idx = findSwioPinIndex(settings->getSwioPin());
    drawTerminal();
}

void SetupScreen::drawTerminal() {
    printf("\033[2J\033[H");  // Clear screen + cursor home
    printf("//===========================================================\n");
    printf("//\n");
    printf("// PewPewCH32 %s SETUP\n", PROGRAMMER_VERSION);
    printf("//\n");

    // Row 0: Display orientation
    const char* flip_label = edit_display_flip ? "flipped" : "normal";
    printf("// %s Display orientation:  < %-8s >\n",
           (selected_row == 0) ? "-->" : "   ", flip_label);

    // Row 1: Screensaver timeout
    printf("// %s Screensaver timeout:  < %-8s >\n",
           (selected_row == 1) ? "-->" : "   ",
           SLEEP_TIMEOUT_LABELS[edit_sleep_timeout_idx]);

    // Row 2: SWIO pin
    char pin_buf[12];
    snprintf(pin_buf, sizeof(pin_buf), "GPIO %d", SWIO_PIN_OPTIONS[edit_swio_pin_idx]);
    printf("// %s SWIO pin:             < %-8s >\n",
           (selected_row == 2) ? "-->" : "   ", pin_buf);

    printf("//\n");
    printf("// [UP/DN] SELECT  [LEFT/RIGHT] CHANGE VALUE\n");
    printf("// [ENTER] SAVE    [ESC] CANCEL\n");
    printf("//\n");
    printf("//===========================================================\n");
}

SetupResult SetupScreen::processInput(int c) {
    // Handle ESC sequences for arrow keys
    if (c == 0x1B) {
        // Check for '[' within 10ms — if not, bare ESC = cancel
        int c2 = getchar_timeout_us(10000);
        if (c2 != '[') {
            return RESULT_CANCELLED;
        }
        int c3 = getchar_timeout_us(10000);
        switch (c3) {
            case 'A':  // UP
                if (selected_row > 0) selected_row--;
                drawTerminal();
                break;
            case 'B':  // DOWN
                if (selected_row < NUM_ROWS - 1) selected_row++;
                drawTerminal();
                break;
            case 'C':  // RIGHT — next value
            case 'D':  // LEFT — previous value
            {
                int dir = (c3 == 'C') ? 1 : -1;
                switch (selected_row) {
                    case 0:  // Display flip is boolean toggle
                        edit_display_flip = !edit_display_flip;
                        break;
                    case 1:  // Sleep timeout index
                        edit_sleep_timeout_idx += dir;
                        if (edit_sleep_timeout_idx < 0)
                            edit_sleep_timeout_idx = SLEEP_TIMEOUT_COUNT - 1;
                        if (edit_sleep_timeout_idx >= SLEEP_TIMEOUT_COUNT)
                            edit_sleep_timeout_idx = 0;
                        break;
                    case 2:  // SWIO pin index
                        edit_swio_pin_idx += dir;
                        if (edit_swio_pin_idx < 0)
                            edit_swio_pin_idx = SWIO_PIN_COUNT - 1;
                        if (edit_swio_pin_idx >= SWIO_PIN_COUNT)
                            edit_swio_pin_idx = 0;
                        break;
                }
                drawTerminal();
                break;
            }
            default:
                break;
        }
        return RESULT_PENDING;
    }

    // ENTER = save
    if (c == '\r' || c == '\n') {
        return RESULT_SAVED;
    }

    return RESULT_PENDING;
}

void SetupScreen::applyToHardware(Settings* settings, DisplayController* display,
                                  PicoSWIO* swio, RVDebug* rvd,
                                  StateMachine* state_machine,
                                  int* swio_pin_out) {
    uint8_t new_pin = SWIO_PIN_OPTIONS[edit_swio_pin_idx];

    settings->setDisplayFlip(edit_display_flip);
    settings->setSleepTimeoutIndex(edit_sleep_timeout_idx);
    settings->setSwioPin(new_pin);
    settings->save();

    // Apply display orientation
    display->setFlipped(edit_display_flip);

    // Apply sleep timeout
    display->setSleepTimeout(SLEEP_TIMEOUT_OPTIONS[edit_sleep_timeout_idx]);

    // Apply SWIO pin change
    *swio_pin_out = new_pin;
    swio->reset(new_pin);
    rvd->init();
    state_machine->setDebugBus(swio, new_pin);
}

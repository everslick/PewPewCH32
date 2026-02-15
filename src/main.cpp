#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "tusb.h"

// Custom modules
#include "LedController.h"
#include "StateMachine.h"
#include "BuzzerController.h"
#include "InputHandler.h"
#include "Settings.h"
#include "DisplayController.h"

// Debug modules
#include "PicoSWIO.h"
#include "RVDebug.h"
#include "WCHFlash.h"
#include "SoftBreak.h"
#include "Console.h"
#include "GDBServer.h"
#include "debug_defines.h"
#include "utils.h"

#ifdef FIRMWARE_INVENTORY_ENABLED
#include "firmware_inventory.h"
#endif

// Configuration
#define PIN_PRG_SWIO    8   // To SDI on CH32
#define PIN_TEST       10   // Test pin for toggling

const int ch32v003_flash_size = 16*1024;
extern const char* const PROGRAMMER_VERSION = "1.0.0";

// Fallback firmware if no external firmware repositories are available
const uint8_t fallback_firmware[] = {
    // Minimal valid RISC-V reset vector
    0x37, 0x01, 0x00, 0x08,  // lui sp, 0x80000
    0x13, 0x01, 0x01, 0x00,  // addi sp, sp, 0
    0x6F, 0x00, 0x00, 0x00,  // j . (infinite loop)
};
const size_t fallback_firmware_size = sizeof(fallback_firmware);

// Global pointers for terminal UI redraw
static StateMachine* g_state_machine = nullptr;

// Draw the full terminal UI (replaces print_header + list_firmware)
void drawTerminalUI() {
    printf("\033[2J\033[H");  // Clear screen + cursor home
    printf("//===========================================================\n");
    printf("//\n");
    printf("// PewPewCH32 %s\n", PROGRAMMER_VERSION);
    printf("//\n");

#ifdef FIRMWARE_INVENTORY_ENABLED
    int selected = g_state_machine->getCurrentFirmwareIndex();
    printf("// %s [0] WIPE FLASH\n", (selected == 0) ? "-->" : "   ");
    for (int i = 0; i < firmware_count; i++) {
        printf("// %s [%d] %s\n", (selected == i + 1) ? "-->" : "   ",
               i + 1, firmware_list[i].name);
    }
    printf("// %s [9] REBOOT\n", (selected == 9) ? "-->" : "   ");
    printf("//\n");
    printf("// [UP/DN] SELECT     [ENTER] FLASH     [0-9] QUICK SELECT\n");
    printf("// [T] TOGGLE GPIO%d  [F] FLIP DISPLAY  [R] REFRESH\n", PIN_TEST);
#else
    printf("//     [0] fallback (built-in minimal firmware)\n");
    printf("//\n");
    printf("// [ENTER] FLASH  [F] FLIP DISPLAY  [R] REFRESH\n");
#endif

    printf("//\n");
    printf("// Status: %s\n", StateMachine::getStateName(g_state_machine->getCurrentState()));
    printf("//\n");
    printf("//===========================================================\n");
}

// Flag for terminal redraw from main loop
static bool needs_terminal_redraw = false;

int main() {
    stdio_init_all();

    // Give USB serial time to initialize
    sleep_ms(1000);

    // Initialize persistent settings (first — display depends on it)
    Settings* settings = new Settings();
    settings->init();

    // Initialize display
    DisplayController* display = new DisplayController();
    display->init(settings->getDisplayFlip());

    // Initialize controllers
    LedController* led = new LedController();
    led->init();

    BuzzerController* buzzer = new BuzzerController();
    buzzer->init();

    InputHandler* input = new InputHandler();
    input->init();

    // Rainbow startup animation
    led->rainbowAnimation();

    // Quick LED test on startup
    led->setAllGpioLeds(true);
    sleep_ms(200);
    led->setAllGpioLeds(false);

    // Initialize debug interfaces
    printf_g("// Initializing PicoSWIO on GPIO%d\n", PIN_PRG_SWIO);
    PicoSWIO* swio = new PicoSWIO();
    swio->reset(PIN_PRG_SWIO);

    printf_g("// Initializing RVDebug\n");
    RVDebug* rvd = new RVDebug(swio, 16);
    rvd->init();

    printf_g("// Initializing WCHFlash\n");
    WCHFlash* flash = new WCHFlash(rvd, ch32v003_flash_size);
    flash->reset();

    printf_g("// Initializing SoftBreak\n");
    SoftBreak* soft = new SoftBreak(rvd, flash);
    soft->init();

    printf_g("// Initializing GDBServer\n");
    GDBServer* gdb = new GDBServer(rvd, flash, soft);
    gdb->reset();

    printf_g("// Initializing Console\n");
    Console* console = new Console(rvd, flash, soft);
    console->reset();

    // Initialize state machine
    StateMachine* state_machine = new StateMachine(led, rvd, flash);
    state_machine->setDisplayController(display);
    state_machine->setDebugBus(swio, PIN_PRG_SWIO);
    g_state_machine = state_machine;

    // Restore last firmware selection from settings
    int last_idx = settings->getLastFirmwareIndex();
#ifdef FIRMWARE_INVENTORY_ENABLED
    if (last_idx >= 0 && (last_idx <= firmware_count || last_idx == 9)) {
        state_machine->setCurrentFirmwareIndex(last_idx);
    } else {
        state_machine->setCurrentFirmwareIndex(1);
    }
#else
    state_machine->setCurrentFirmwareIndex(0);
#endif

    // Set initial display content
    display->setMenuEntry(state_machine->getCurrentMenuName());
    display->setSystemState(STATE_IDLE);

    printf_g("// CH32V003 Programmer Ready!\n");

    console->start();

    // Initial terminal UI draw
    drawTerminalUI();

    // Track state changes for terminal redraw + sounds
    SystemState last_state = STATE_IDLE;

    // Main loop
    while (1) {
        // Update all controllers
        led->update();
        display->update();
        state_machine->process();

        // Check for state changes (sounds + terminal redraw)
        SystemState current_state = state_machine->getCurrentState();
        if (current_state != last_state) {
            switch (current_state) {
                case STATE_SUCCESS:
                    buzzer->beepSuccess();
                    break;
                case STATE_ERROR:
                    buzzer->beepFailure();
                    break;
                default:
                    break;
            }
            last_state = current_state;
            needs_terminal_redraw = true;
        }

        // Handle input only in IDLE state
        if (state_machine->getCurrentState() == STATE_IDLE) {
            // Check trigger button
            if (input->checkTriggerButton()) {
                if (display->isSleeping()) {
                    display->forceRedraw();
                } else {
                    printf_g("\n// Trigger detected! Starting flash sequence...\n");
                    buzzer->beepStart();
                    state_machine->startProgramming();
                }
            }

            // Check BOOTSEL button
            InputHandler::ButtonEvent bootsel_event = input->getBootselEvent();
            if (bootsel_event != InputHandler::BUTTON_NONE && display->isSleeping()) {
                display->forceRedraw();
            } else {
                switch (bootsel_event) {
                    case InputHandler::BUTTON_SHORT_PRESS:
                        state_machine->startProgramming();
                        break;

                    case InputHandler::BUTTON_LONG_PRESS:
                        buzzer->beepWarning();
                        state_machine->cycleFirmware();
                        settings->setLastFirmwareIndex(state_machine->getCurrentFirmwareIndex());
                        settings->save();
                        needs_terminal_redraw = true;
                        break;

                    default:
                        break;
                }
            }

            // Check for UART input
            int c = getchar_timeout_us(0);
            if (c != PICO_ERROR_TIMEOUT && (c == 't' || c == 'T')) {
                printf_g("// TEST PIN: Toggling GPIO%d for 5 seconds...\n", PIN_TEST);
                buzzer->beepWarning();
                gpio_init(PIN_TEST);
                gpio_set_dir(PIN_TEST, GPIO_OUT);
                for (int i = 0; i < 20; i++) {
                    gpio_put(PIN_TEST, i & 1);
                    led->update();
                    sleep_ms(250);
                }
                gpio_put(PIN_TEST, 0);
                gpio_deinit(PIN_TEST);
                printf_g("// TEST PIN: Done\n");
                needs_terminal_redraw = true;
            } else if (c != PICO_ERROR_TIMEOUT && (c == 'f' || c == 'F')) {
                bool flipped = !settings->getDisplayFlip();
                settings->setDisplayFlip(flipped);
                settings->save();
                display->setFlipped(flipped);
                printf_g("// Display flip: %s\n", flipped ? "ON" : "OFF");
                needs_terminal_redraw = true;
            } else if (c != PICO_ERROR_TIMEOUT && (c == 'r' || c == 'R')) {
                display->forceRedraw();
                needs_terminal_redraw = true;
            } else if (c != PICO_ERROR_TIMEOUT && c >= '0' && c <= '9') {
                int index = c - '0';
#ifdef FIRMWARE_INVENTORY_ENABLED
                bool valid = (index == 0 || index == 9 ||
                              (index >= 1 && index <= firmware_count));
#else
                bool valid = (index == 0);
#endif
                if (valid) {
                    state_machine->setCurrentFirmwareIndex(index);
                    display->setMenuEntry(state_machine->getCurrentMenuName());
                    settings->setLastFirmwareIndex(index);
                    settings->save();
                    buzzer->beepStart();
                    state_machine->startProgramming();
                } else {
                    printf_g("// Invalid selection [%d]\n", index);
                }
            } else if (c != PICO_ERROR_TIMEOUT && c == 0x1B) {
                // Escape sequence: read remaining bytes for arrow keys
                int c2 = getchar_timeout_us(10000);
                if (c2 == '[') {
                    int c3 = getchar_timeout_us(10000);
#ifdef FIRMWARE_INVENTORY_ENABLED
                    if (c3 == 'A' || c3 == 'B') {
                        int idx = state_machine->getCurrentFirmwareIndex();
                        if (c3 == 'A') {
                            // UP: previous entry
                            if (idx == 0) idx = 9;
                            else if (idx == 9) idx = firmware_count;
                            else idx--;
                        } else {
                            // DOWN: next entry
                            if (idx == 9) idx = 0;
                            else if (idx >= firmware_count) idx = 9;
                            else idx++;
                        }
                        state_machine->setCurrentFirmwareIndex(idx);
                        display->setMenuEntry(state_machine->getCurrentMenuName());
                        settings->setLastFirmwareIndex(idx);
                        needs_terminal_redraw = true;
                    }
#endif
                }
            } else if (c != PICO_ERROR_TIMEOUT && (c == '\r' || c == '\n')) {
                settings->save();
                buzzer->beepStart();
                state_machine->startProgramming();
            }
        }

        // Deferred terminal redraw
        if (needs_terminal_redraw) {
            needs_terminal_redraw = false;
            drawTerminalUI();
        }

        // Small delay to prevent CPU hogging
        sleep_ms(10);
    }

    // Cleanup (never reached)
    delete console;
    delete gdb;
    delete soft;
    delete flash;
    delete rvd;
    delete swio;
    delete state_machine;
    delete display;
    delete settings;
    delete input;
    delete buzzer;
    delete led;

    return 0;
}

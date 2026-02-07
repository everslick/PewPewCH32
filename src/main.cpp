#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "tusb.h"

// Custom modules
#include "LedController.h"
#include "StateMachine.h"
#include "BuzzerController.h"
#include "InputHandler.h"

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

const int ch32v003_flash_size = 16*1024;
const char* PROGRAMMER_VERSION = "1.0.0";

// Fallback firmware if no external firmware repositories are available
const uint8_t fallback_firmware[] = {
    // Minimal valid RISC-V reset vector
    0x37, 0x01, 0x00, 0x08,  // lui sp, 0x80000
    0x13, 0x01, 0x01, 0x00,  // addi sp, sp, 0
    0x6F, 0x00, 0x00, 0x00,  // j . (infinite loop)
};
const size_t fallback_firmware_size = sizeof(fallback_firmware);

// Function prototypes
void list_firmware();
void print_header();

int main() {
    stdio_init_all();
    
    // Give USB serial time to initialize
    sleep_ms(1000);
    
    // Initialize controllers
    LedController* led = new LedController();
    led->init();
    
    BuzzerController* buzzer = new BuzzerController();
    buzzer->init();
    
    InputHandler* input = new InputHandler();
    input->init();
    
    // Rainbow startup animation
    led->rainbowAnimation();
    
    print_header();
    
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
    
    printf_g("// CH32V003 Programmer Ready!\n");
    list_firmware();
    printf_g("// Waiting for BOOTSEL button or trigger (GPIO%d)...\n\n", PIN_TRIGGER);
    
    console->start();
    
    // Main loop
    while (1) {
        // Update all controllers
        led->update();
        state_machine->process();
        
        // Handle input only in IDLE state
        if (state_machine->getCurrentState() == STATE_IDLE) {
            // Check trigger button
            if (input->checkTriggerButton()) {
                printf_g("\n// Trigger detected! Starting flash sequence...\n");
                buzzer->beepStart();
                state_machine->startTargetCheck();
            }
            
            // Check BOOTSEL button
            InputHandler::ButtonEvent bootsel_event = input->getBootselEvent();
            switch (bootsel_event) {
                case InputHandler::BUTTON_SHORT_PRESS:
                    printf_g("// Checking target...\n");
                    state_machine->startProgramming();
                    break;

                case InputHandler::BUTTON_LONG_PRESS:
                    buzzer->beepWarning();
                    state_machine->cycleFirmware();
                    break;

                default:
                    break;
            }

            // Check for UART firmware selection
            int c = getchar_timeout_us(0);
            if (c != PICO_ERROR_TIMEOUT && c >= '0' && c <= '9') {
                int index = c - '0';
#ifdef FIRMWARE_INVENTORY_ENABLED
                int max_index = 9; // [0]=wipe, [1..firmware_count]=firmware, [9]=reboot
#else
                int max_index = 0;
#endif
                if (index <= max_index) {
                    state_machine->setCurrentFirmwareIndex(index);
#ifdef FIRMWARE_INVENTORY_ENABLED
                    if (index == 0) {
                        printf_g("// UART selected [0] WIPE FLASH\n");
                    } else if (index == 9) {
                        printf_g("// UART selected [9] REBOOT\n");
                    } else {
                        printf_g("// UART selected [%d] %s\n", index, firmware_list[index - 1].name);
                    }
#else
                    printf_g("// UART selected [%d] fallback firmware\n", index);
#endif
                    buzzer->beepStart();
                    state_machine->startProgramming();
                } else {
                    printf_g("// Invalid selection [%d], valid range is 0-%d\n", index, max_index);
                }
            }
        }
        
        // Handle state-specific sounds
        static SystemState last_state = STATE_IDLE;
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
    delete input;
    delete buzzer;
    delete led;
    
    return 0;
}

void print_header() {
    printf("\n\n");
    printf_g("\n\n\n");
    printf_g("//==============================================================================\n");
    printf_g("// PewPewCH32 Programmer v%s\n", PROGRAMMER_VERSION);
    printf_g("// Based on PicoRVD\n\n");
}

void list_firmware() {
    printf_g("// Available firmware:\n");
#ifdef FIRMWARE_INVENTORY_ENABLED
    printf_g("//   [0] WIPE FLASH\n");
    for (int i = 0; i < firmware_count; i++) {
        printf_g("//   [%d] %s\n", i + 1, firmware_list[i].name);
    }
    printf_g("//   [9] REBOOT\n");
#else
    printf_g("//   [0] fallback (built-in minimal firmware)\n");
#endif
}

#include "StateMachine.h"
#include "PicoSWIO.h"
#include "DisplayController.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "utils.h"

#ifdef FIRMWARE_INVENTORY_ENABLED
  #include "firmware_inventory.h"
#else
  extern const uint8_t fallback_firmware[];
  extern const size_t  fallback_firmware_size;
#endif

StateMachine::StateMachine(LedController* led, RVDebug* rvd, WCHFlash* flash)
    : state_timer(0),
      current_firmware_index(0),
      led_controller(led),
      display_controller(nullptr),
      rv_debug(rvd),
      debug_swio(nullptr),
      swio_pin(-1),
      wch_flash(flash) {
    // Initialize to IDLE state properly (triggers state entry actions)
    current_state = (SystemState)-1; // Set to invalid state first
    setState(STATE_IDLE);
}

StateMachine::~StateMachine() {
}

void StateMachine::setState(SystemState state) {
    // Handle state exit actions
    switch (current_state) {
        case STATE_PROGRAMMING:
            led_controller->stopProgrammingBlink();
            break;
        case STATE_ERROR:
            led_controller->stopErrorIndication();
            break;
        case STATE_IDLE:
            led_controller->stopHeartbeat();
            break;
        default:
            break;
    }
    
    current_state = state;
    state_timer = to_ms_since_boot(get_absolute_time());
    
    // Handle state entry actions
    switch (state) {
        case STATE_IDLE:
            led_controller->startHeartbeat();
            break;
        case STATE_PROGRAMMING:
            led_controller->startProgrammingBlink();
            break;
        case STATE_ERROR:
            led_controller->startErrorIndication();
            break;
        case STATE_CYCLING_FIRMWARE:
#ifdef FIRMWARE_INVENTORY_ENABLED
            if (current_firmware_index == 0) {
                led_controller->startWipeIndication();
            } else if (current_firmware_index == 9) {
                led_controller->startRebootIndication();
            } else
#endif
            {
                led_controller->startFirmwareIndication(current_firmware_index - 1);
            }
            break;
        default:
            break;
    }

    // Notify display
    if (display_controller) {
        display_controller->setSystemState(state);
    }
}

void StateMachine::process() {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    switch (current_state) {
        case STATE_CHECKING_TARGET:
            if (haltWithTimeout(100)) {  // 100ms timeout
                printf_g("// Target detected - starting programming...\n");
                setState(STATE_PROGRAMMING);
            } else {
                printf_g("// ERROR: No CH32V003 target detected.\n");
                setState(STATE_ERROR);
            }
            break;
            
        case STATE_PROGRAMMING:
            {
                bool success = false;

                // Select firmware to program (or wipe/reboot)
#ifdef FIRMWARE_INVENTORY_ENABLED
                if (current_firmware_index == 0) {
                    success = wipeChip();
                } else if (current_firmware_index == 9) {
                    success = rebootChip();
                } else if (current_firmware_index <= firmware_count) {
                    const firmware_info_t* fw = &firmware_list[current_firmware_index - 1];
                    printf_g("// Programming firmware: %s (@ 0x%08lX)\n",
                             fw->name, (unsigned long)fw->load_addr);
                    success = programFirmware(fw);
                } else {
                    printf_g("// Invalid index\n");
                }
#else
                printf_g("// Programming fallback firmware\n");
                success = programFlash(fallback_firmware, fallback_firmware_size, 0);
#endif

                if (success) {
                    printf_g("// SUCCESS!\n\n");
                    setState(STATE_SUCCESS);
                } else {
                    printf_g("// ERROR!\n\n");
                    setState(STATE_ERROR);
                }
            }
            break;
            
        case STATE_ERROR:
            // Auto-transition back to IDLE after 2 seconds
            if ((now - state_timer) >= 2000) {
                setState(STATE_IDLE);
            }
            break;
            
        case STATE_SUCCESS:
            // Auto-transition back to IDLE after 3 seconds
            if ((now - state_timer) >= 3000) {
                setState(STATE_IDLE);
            }
            break;
            
        case STATE_CYCLING_FIRMWARE:
            // Handled by LED controller, transition back when done
            if (!led_controller->isFirmwareIndicationActive()) {
                setState(STATE_IDLE);
            }
            break;
            
        default:
            break;
    }
}

void StateMachine::startProgramming() {
    startTargetCheck();
}

void StateMachine::startTargetCheck() {
    if (current_state == STATE_IDLE) {
        setState(STATE_CHECKING_TARGET);
    }
}

void StateMachine::cycleFirmware() {
#ifdef FIRMWARE_INVENTORY_ENABLED
    // Cycle through: [0] WIPE FLASH, [1..firmware_count] firmware entries, [9] REBOOT
    if (current_firmware_index == 9) {
        current_firmware_index = 0;
    } else if (current_firmware_index >= firmware_count) {
        current_firmware_index = 9;
    } else {
        current_firmware_index++;
    }
    if (current_firmware_index == 0) {
        printf_g("// Selected: [0] WIPE FLASH\n");
    } else if (current_firmware_index == 9) {
        printf_g("// Selected: [9] REBOOT\n");
    } else {
        const firmware_info_t* fw = &firmware_list[current_firmware_index - 1];
        printf_g("// Firmware selected: [%d] %s\n", current_firmware_index, fw->name);
    }
#else
    current_firmware_index = 0;
    printf_g("// Firmware selected: [0] fallback\n");
#endif

    // Notify display
    if (display_controller) {
        display_controller->setMenuEntry(getCurrentMenuName());
    }

    setState(STATE_CYCLING_FIRMWARE);
}

const char* StateMachine::getStateName(SystemState state) {
    switch (state) {
        case STATE_IDLE:             return "READY";
        case STATE_CHECKING_TARGET:  return "CHECKING...";
        case STATE_PROGRAMMING:      return "PROGRAMMING...";
        case STATE_SUCCESS:          return "SUCCESS";
        case STATE_ERROR:            return "ERROR";
        case STATE_CYCLING_FIRMWARE: return "SELECTING...";
        default:                     return "UNKNOWN";
    }
}

const char* StateMachine::getCurrentMenuName() const {
#ifdef FIRMWARE_INVENTORY_ENABLED
    if (current_firmware_index == 0) return "WIPE FLASH";
    if (current_firmware_index == 9) return "REBOOT";
    if (current_firmware_index >= 1 && current_firmware_index <= firmware_count) {
        return firmware_list[current_firmware_index - 1].name;
    }
    return "???";
#else
    return "fallback";
#endif
}

bool StateMachine::haltWithTimeout(uint32_t timeout_ms) {
    // Re-initialize SWIO bus before each attempt so a freshly connected
    // target receives the reset pulse and config sequence.
    if (debug_swio) {
        debug_swio->reset(swio_pin);
        rv_debug->init();
    }

    uint32_t start_time = to_ms_since_boot(get_absolute_time());

    rv_debug->set_dmcontrol(0x80000001);

    while (true) {
        Reg_DMSTATUS status = rv_debug->get_dmstatus();

        // Reject obviously invalid responses (no chip connected returns
        // all-ones or all-zeros depending on pin state)
        uint32_t raw = status.raw;
        if (raw == 0xFFFFFFFF || raw == 0x00000000 ||
            (status.ALLHALTED && status.ALLRUNNING)) {
            rv_debug->set_dmcontrol(0x00000001);
            return false;
        }

        if (status.ALLHALTED) {
            rv_debug->set_dmcontrol(0x00000001);
            return true;
        }

        uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - start_time;
        if (elapsed > timeout_ms) {
            rv_debug->set_dmcontrol(0x00000001);
            return false;
        }
        sleep_ms(1);
    }
}

bool StateMachine::programFlash(const uint8_t* data, size_t size, uint32_t base_address) {
    if (!data || !size) {
        return false;
    }

    printf_g("// Starting flash programming...\n");
    printf_g("// Firmware size: %d bytes at base 0x%08X\n", size, base_address);

    if (!rv_debug->halt()) {
        printf_g("// ERROR: Could not halt target\n");
        return false;
    }

    printf_g("// Unlocking flash...\n");
    wch_flash->unlock_flash();

    // Sector-based erasure: only erase sectors being written
    // CH32V003 has 1024-byte sectors
    const uint32_t sector_size = wch_flash->get_sector_size();
    uint32_t first_sector = base_address / sector_size;
    uint32_t last_sector = (base_address + size - 1) / sector_size;

    printf_g("// Erasing sectors %d to %d...\n", first_sector, last_sector);
    for (uint32_t sector = first_sector; sector <= last_sector; sector++) {
        wch_flash->wipe_sector(sector * sector_size);
    }

    size_t aligned_size = (size + 3) & ~3;
    printf_g("// Writing %d bytes to flash (aligned to %d)...\n", size, aligned_size);

    uint8_t* aligned_data = (uint8_t*)data;
    bool need_free = false;
    if (size != aligned_size) {
        aligned_data = new uint8_t[aligned_size];
        memcpy(aligned_data, data, size);
        memset(aligned_data + size, 0xFF, aligned_size - size);
        need_free = true;
    }

    wch_flash->write_flash(base_address, aligned_data, aligned_size);

    printf_g("// Verifying flash...\n");
    bool success = wch_flash->verify_flash(base_address, aligned_data, aligned_size);

    if (need_free) {
        delete[] aligned_data;
    }

    if (!success) {
        printf_g("// ERROR: Flash verification failed\n");
    } else {
        printf_g("// Flash programming and verification complete\n");
    }

    // Always clean up: lock flash and reset target
    wch_flash->lock_flash();
    rv_debug->reset();
    rv_debug->resume();

    return success;
}

bool StateMachine::wipeChip() {
    printf_g("// WIPING ENTIRE FLASH\n");

    if (!rv_debug->halt()) {
        printf_g("// ERROR: Could not halt target\n");
        return false;
    }

    wch_flash->unlock_flash();
    printf_g("// Erasing all 16KB flash (MER)...\n");
    wch_flash->wipe_chip();

    wch_flash->lock_flash();
    rv_debug->reset();
    rv_debug->resume();

    printf_g("// Chip wipe complete\n");
    return true;
}

bool StateMachine::rebootChip() {
    printf_g("// REBOOTING TARGET\n");

    rv_debug->reset();
    rv_debug->resume();

    printf_g("// Target rebooted\n");
    return true;
}

#ifdef FIRMWARE_INVENTORY_ENABLED
bool StateMachine::programFirmware(const firmware_info_t* fw) {
    if (!fw || !fw->data || !fw->size) {
        return false;
    }

    // Binary is self-contained (header + code), flash at load_addr
    return programFlash(fw->data, fw->size, fw->load_addr);
}
#endif

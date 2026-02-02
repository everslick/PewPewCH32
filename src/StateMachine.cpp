#include "StateMachine.h"
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
      rv_debug(rvd),
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
            led_controller->startFirmwareIndication(current_firmware_index);
            break;
        default:
            break;
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
                const uint8_t* firmware_data = nullptr;
                size_t firmware_size = 0;
                
                // Select firmware to program
#ifdef FIRMWARE_INVENTORY_ENABLED
                if (current_firmware_index < firmware_count) {
                    firmware_data = firmware_list[current_firmware_index].data;
                    firmware_size = firmware_list[current_firmware_index].size;
                    printf_g("// Programming firmware: %s\n", firmware_list[current_firmware_index].name);
                } else {
                    printf_g("// Invalid index\n");
                }
#else
                firmware_data = fallback_firmware;
                firmware_size = fallback_firmware_size;
                printf_g("// Programming fallback firmware\n");
#endif
                
                success = programFlash(firmware_data, firmware_size);
                
                if (success) {
                    printf_g("// Programming SUCCESSFUL!\n");
                    setState(STATE_SUCCESS);
                } else {
                    printf_g("// Programming FAILED!\n");
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
    if (current_state == STATE_IDLE) {
        setState(STATE_CHECKING_TARGET);
    }
}

void StateMachine::startTargetCheck() {
    if (current_state == STATE_IDLE) {
        setState(STATE_CHECKING_TARGET);
    }
}

void StateMachine::cycleFirmware() {
#ifdef FIRMWARE_INVENTORY_ENABLED
    if (firmware_count > 1) {
        current_firmware_index = (current_firmware_index + 1) % firmware_count;
    }
    printf_g("// Firmware selected: [%d] %s\n", current_firmware_index, 
             firmware_list[current_firmware_index].name);
#else
    current_firmware_index = 0;
    printf_g("// Firmware selected: [0] fallback\n");
#endif
    
    setState(STATE_CYCLING_FIRMWARE);
}

bool StateMachine::haltWithTimeout(uint32_t timeout_ms) {
    uint32_t start_time = to_ms_since_boot(get_absolute_time());
    
    rv_debug->set_dmcontrol(0x80000001);
    
    while (!rv_debug->get_dmstatus().ALLHALTED) {
        uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - start_time;
        if (elapsed > timeout_ms) {
            rv_debug->set_dmcontrol(0x00000001);
            return false;
        }
        sleep_ms(1);
    }
    
    rv_debug->set_dmcontrol(0x00000001);
    return true;
}

bool StateMachine::programFlash(const uint8_t* data, size_t size) {
    if (!data || !size) {
        return false;
    }

    printf_g("// Starting flash programming...\n");
    printf_g("// Firmware size: %d bytes\n", size);
    
    if (!rv_debug->halt()) {
        printf_g("// ERROR: Could not halt target\n");
        return false;
    }
    
    printf_g("// Unlocking and erasing flash...\n");
    wch_flash->unlock_flash();
    wch_flash->wipe_chip();
    
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
    
    wch_flash->write_flash(wch_flash->get_flash_base(), aligned_data, aligned_size);
    
    printf_g("// Verifying flash...\n");
    bool success = wch_flash->verify_flash(wch_flash->get_flash_base(), aligned_data, aligned_size);
    
    if (need_free) {
        delete[] aligned_data;
    }
    
    if (!success) {
        printf_g("// ERROR: Flash verification failed\n");
        return false;
    }
    
    printf_g("// Flash programming and verification complete\n");
    
    wch_flash->lock_flash();
    rv_debug->reset();
    rv_debug->resume();

    return true;
}

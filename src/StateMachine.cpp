#include "StateMachine.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "utils.h"
#include "fw_metadata.h"
#include "crc32.h"

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
#ifdef FIRMWARE_INVENTORY_ENABLED
            if (current_firmware_index == firmware_count) {
                led_controller->startWipeIndication();
            } else
#endif
            {
                led_controller->startFirmwareIndication(current_firmware_index);
            }
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

                // Select firmware to program (or wipe)
#ifdef FIRMWARE_INVENTORY_ENABLED
                if (current_firmware_index == firmware_count) {
                    success = wipeChip();
                } else if (current_firmware_index < firmware_count) {
                    const firmware_info_t* fw = &firmware_list[current_firmware_index];
                    const char* type_str = (fw->fw_type == FW_TYPE_APP) ? "APP" : "BOOT";
                    if (fw->has_metadata) {
                        printf_g("// Programming firmware: %s v%d.%d (%s @ 0x%08lX)\n",
                                 fw->name, fw->version_major, fw->version_minor,
                                 type_str, (unsigned long)fw->load_addr);
                    } else {
                        printf_g("// Programming firmware: %s (%s @ 0x%08lX, no metadata)\n",
                                 fw->name, type_str, (unsigned long)fw->load_addr);
                    }
                    success = programFirmware(fw);
                } else {
                    printf_g("// Invalid index\n");
                }
#else
                printf_g("// Programming fallback firmware\n");
                success = programFlash(fallback_firmware, fallback_firmware_size, 0);
#endif

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
    // Cycle through firmware entries + one extra "Wipe Flash" entry
    current_firmware_index = (current_firmware_index + 1) % (firmware_count + 1);
    if (current_firmware_index == firmware_count) {
        printf_g("// Selected: [%d] *** WIPE FLASH ***\n", current_firmware_index);
    } else {
        const firmware_info_t* fw = &firmware_list[current_firmware_index];
        if (fw->has_metadata) {
            printf_g("// Firmware selected: [%d] %s v%d.%d\n", current_firmware_index,
                     fw->name, fw->version_major, fw->version_minor);
        } else {
            printf_g("// Firmware selected: [%d] %s\n", current_firmware_index, fw->name);
        }
    }
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
    printf_g("// *** WIPING ENTIRE FLASH ***\n");

    if (!rv_debug->halt()) {
        printf_g("// ERROR: Could not halt target\n");
        return false;
    }

    wch_flash->unlock_flash();
    printf_g("// Erasing all 16KB flash (MER)...\n");
    wch_flash->wipe_chip();
    printf_g("// Flash erased\n");

    wch_flash->lock_flash();
    rv_debug->reset();
    rv_debug->resume();

    printf_g("// Chip wipe complete\n");
    return true;
}

#ifdef FIRMWARE_INVENTORY_ENABLED
bool StateMachine::programFirmware(const firmware_info_t* fw) {
    if (!fw || !fw->data || !fw->size) {
        return false;
    }

    // For APP type firmware, we need to:
    // 1. Erase all sectors (header at 0x0C40 and code at 0x0C80 share the same sector!)
    // 2. Write the app header at 0x0C40 first
    // 3. Write the firmware data at load_addr (0x0C80)
    if (fw->fw_type == FW_TYPE_APP) {
        printf_g("// APP firmware - will write app header at 0x%08X\n", FW_APP_HEADER_ADDR);

        // Generate app header first (we need it before writing)
        app_header_t header;
        memset(&header, 0, sizeof(header));
        header.magic = APP_HEADER_MAGIC;
        header.fw_ver_major = fw->version_major;
        header.fw_ver_minor = fw->version_minor;
        header.bl_ver_min = 1;
        header.hw_type = fw->hw_type;
        header.app_size = fw->size;
        header.entry_point = fw->load_addr;
        header.app_crc32 = crc32(fw->data, fw->size);
        // CRC of first 24 bytes with header_crc32 = 0 (it's already 0 from memset)
        header.header_crc32 = crc32(&header, 24);

        printf_g("// App CRC32: 0x%08lX, Header CRC32: 0x%08lX\n",
                 (unsigned long)header.app_crc32, (unsigned long)header.header_crc32);

        // Halt and unlock flash
        if (!rv_debug->halt()) {
            printf_g("// ERROR: Could not halt target\n");
            return false;
        }
        wch_flash->unlock_flash();

        // Erase all needed sectors (header and firmware share sector 3)
        const uint32_t sector_size = wch_flash->get_sector_size();
        uint32_t first_sector = FW_APP_HEADER_ADDR / sector_size;  // Sector containing header
        uint32_t last_sector = (fw->load_addr + fw->size - 1) / sector_size;

        printf_g("// Erasing sectors %lu to %lu...\n",
                 (unsigned long)first_sector, (unsigned long)last_sector);
        for (uint32_t sector = first_sector; sector <= last_sector; sector++) {
            wch_flash->wipe_sector(sector * sector_size);
        }

        // Write app header first (at 0x0C40)
        printf_g("// Writing app header at 0x%08X...\n", FW_APP_HEADER_ADDR);
        wch_flash->write_flash(FW_APP_HEADER_ADDR, (void*)&header, sizeof(header));

        // Now write firmware data (at 0x0C80)
        size_t aligned_size = (fw->size + 3) & ~3;
        printf_g("// Writing firmware at 0x%08lX (%d bytes, aligned to %d)...\n",
                 (unsigned long)fw->load_addr, (int)fw->size, (int)aligned_size);

        uint8_t* aligned_data = (uint8_t*)fw->data;
        bool need_free = false;
        if (fw->size != aligned_size) {
            aligned_data = new uint8_t[aligned_size];
            memcpy(aligned_data, fw->data, fw->size);
            memset(aligned_data + fw->size, 0xFF, aligned_size - fw->size);
            need_free = true;
        }

        wch_flash->write_flash(fw->load_addr, aligned_data, aligned_size);

        // Verify both header and firmware
        printf_g("// Verifying...\n");
        bool header_ok = wch_flash->verify_flash(FW_APP_HEADER_ADDR, (void*)&header, sizeof(header));
        bool firmware_ok = wch_flash->verify_flash(fw->load_addr, aligned_data, aligned_size);

        if (need_free) {
            delete[] aligned_data;
        }

        // Clean up
        wch_flash->lock_flash();
        rv_debug->reset();
        rv_debug->resume();

        if (!header_ok) {
            printf_g("// ERROR: App header verification failed\n");
            return false;
        }
        if (!firmware_ok) {
            printf_g("// ERROR: Firmware verification failed\n");
            return false;
        }

        printf_g("// APP firmware and header written successfully\n");
        return true;
    } else {
        // BOOT type - just flash directly
        return programFlash(fw->data, fw->size, fw->load_addr);
    }
}

bool StateMachine::writeAppHeader(const firmware_info_t* fw) {
    printf_g("// Generating app header...\n");

    // Create app header structure
    app_header_t header;
    memset(&header, 0, sizeof(header));

    header.magic = APP_HEADER_MAGIC;  // "WOME"
    header.fw_ver_major = fw->version_major;
    header.fw_ver_minor = fw->version_minor;
    header.bl_ver_min = 1;  // Require bootloader v1.0+
    header.hw_type = fw->hw_type;
    header.app_size = fw->size;
    header.entry_point = fw->load_addr;

    // Calculate CRC32 of application code
    header.app_crc32 = crc32(fw->data, fw->size);
    printf_g("// App CRC32: 0x%08lX\n", (unsigned long)header.app_crc32);

    // Calculate CRC32 of header (first 24 bytes)
    header.header_crc32 = crc32(&header, 24);
    printf_g("// Header CRC32: 0x%08lX\n", (unsigned long)header.header_crc32);

    printf_g("// Writing app header at 0x%08X (%d bytes)...\n",
             FW_APP_HEADER_ADDR, (int)sizeof(header));

    // Halt target again (programFlash released it)
    if (!rv_debug->halt()) {
        printf_g("// ERROR: Could not halt target for header write\n");
        return false;
    }

    // Unlock flash
    wch_flash->unlock_flash();

    // Erase the sector containing the app header (0x0C00-0x0FFF is one sector)
    // Note: The app header is at 0x0C40, boot state at 0x0C00
    // We erase the whole sector and only write the header portion
    const uint32_t sector_size = wch_flash->get_sector_size();
    uint32_t header_sector = FW_APP_HEADER_ADDR / sector_size;
    printf_g("// Erasing sector %lu for app header...\n", (unsigned long)header_sector);
    wch_flash->wipe_sector(header_sector * sector_size);

    // Write the app header (aligned to 4 bytes, header is 64 bytes)
    // Cast away const - WCHFlash API doesn't modify source but has non-const signature
    wch_flash->write_flash(FW_APP_HEADER_ADDR, (void*)&header, sizeof(header));

    // Verify header was written correctly
    bool success = wch_flash->verify_flash(FW_APP_HEADER_ADDR, (void*)&header, sizeof(header));

    // Clean up
    wch_flash->lock_flash();
    rv_debug->reset();
    rv_debug->resume();

    if (success) {
        printf_g("// App header written and verified successfully\n");
    } else {
        printf_g("// ERROR: App header verification failed\n");
    }

    return success;
}
#endif

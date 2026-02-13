#include "Settings.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "utils.h"

// Settings stored in last sector of flash
#define SETTINGS_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define SETTINGS_FLASH_ADDR   (XIP_BASE + SETTINGS_FLASH_OFFSET)

Settings::Settings() : dirty(false) {
    loadDefaults();
}

Settings::~Settings() {
}

void Settings::loadDefaults() {
    memset(&data, 0, sizeof(data));
    data.magic = SETTINGS_MAGIC;
    data.display_flip = false;
    data.last_firmware_idx = 1;
    data.crc = calculateCrc(&data);
}

uint32_t Settings::calculateCrc(const settings_data_t* d) const {
    // Simple CRC32 over all bytes before the crc field
    const uint8_t* bytes = (const uint8_t*)d;
    size_t len = offsetof(settings_data_t, crc);
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= bytes[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

bool Settings::validate(const settings_data_t* d) const {
    if (d->magic != SETTINGS_MAGIC) return false;
    if (d->crc != calculateCrc(d)) return false;
    return true;
}

void Settings::init() {
    // Read from flash via XIP
    const settings_data_t* flash_data = (const settings_data_t*)SETTINGS_FLASH_ADDR;
    if (validate(flash_data)) {
        memcpy(&data, flash_data, sizeof(data));
        printf_g("// Settings loaded from flash (flip=%d, fw=%d)\n",
                 data.display_flip, data.last_firmware_idx);
    } else {
        loadDefaults();
        printf_g("// Settings: using defaults (no valid data in flash)\n");
    }
    dirty = false;
}

// Callback for flash_safe_execute — runs with interrupts disabled
struct flash_write_context_t {
    uint32_t offset;
    const uint8_t* data;
    size_t len;
};

static void flash_write_callback(void* param) {
    flash_write_context_t* ctx = (flash_write_context_t*)param;
    flash_range_erase(ctx->offset, FLASH_SECTOR_SIZE);
    flash_range_program(ctx->offset, ctx->data, ctx->len);
}

void Settings::save() {
    if (!dirty) return;

    data.crc = calculateCrc(&data);

    // Prepare a 256-byte page-aligned buffer
    uint8_t buf[FLASH_PAGE_SIZE];
    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, &data, sizeof(data));

    flash_write_context_t ctx;
    ctx.offset = SETTINGS_FLASH_OFFSET;
    ctx.data = buf;
    ctx.len = FLASH_PAGE_SIZE;

    int rc = flash_safe_execute(flash_write_callback, &ctx, UINT32_MAX);
    if (rc == PICO_OK) {
        dirty = false;
        printf_g("// Settings saved to flash\n");
    } else {
        printf_g("// WARNING: Settings save failed (rc=%d)\n", rc);
    }
}

void Settings::setDisplayFlip(bool flip) {
    if (data.display_flip != flip) {
        data.display_flip = flip;
        dirty = true;
    }
}

void Settings::setLastFirmwareIndex(int index) {
    if (data.last_firmware_idx != index) {
        data.last_firmware_idx = index;
        dirty = true;
    }
}

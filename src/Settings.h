#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stddef.h>

#define SETTINGS_MAGIC 0x50575358  // "PWSX" — v2: added swio_pin + sleep_timeout

struct __attribute__((packed)) settings_data_t {
    uint32_t magic;              // 4
    uint8_t  display_flip;       // 1
    uint8_t  swio_pin;           // 1  (GPIO number, default 8)
    uint16_t sleep_timeout_idx;  // 2  (index into timeout table)
    int32_t  last_firmware_idx;  // 4
    uint8_t  _reserved[12];     // 12
    uint32_t crc;                // 4
};                               // = 28 bytes
static_assert(sizeof(settings_data_t) == 28, "settings_data_t layout changed");

class Settings {
public:
    Settings();
    ~Settings();

    void init();
    void save();

    bool getDisplayFlip() const { return data.display_flip; }
    void setDisplayFlip(bool flip);

    uint8_t getSwioPin() const { return data.swio_pin; }
    void setSwioPin(uint8_t pin);

    uint16_t getSleepTimeoutIndex() const { return data.sleep_timeout_idx; }
    void setSleepTimeoutIndex(uint16_t idx);

    int getLastFirmwareIndex() const { return data.last_firmware_idx; }
    void setLastFirmwareIndex(int index);

private:
    settings_data_t data;
    bool dirty;

    void loadDefaults();
    uint32_t calculateCrc(const settings_data_t* d) const;
    bool validate(const settings_data_t* d) const;
};

#endif // SETTINGS_H

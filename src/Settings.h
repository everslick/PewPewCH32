#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stddef.h>

#define SETTINGS_MAGIC 0x50575357  // "PWSW"

struct __attribute__((packed)) settings_data_t {
    uint32_t magic;
    uint8_t  display_flip;
    uint8_t  _reserved_pin;   // was swio_pin, kept for layout compatibility
    int32_t  last_firmware_idx;
    uint8_t  _reserved[16];
    uint32_t crc;
};
static_assert(sizeof(settings_data_t) == 30, "settings_data_t layout changed");

class Settings {
public:
    Settings();
    ~Settings();

    void init();
    void save();

    bool getDisplayFlip() const { return data.display_flip; }
    void setDisplayFlip(bool flip);

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

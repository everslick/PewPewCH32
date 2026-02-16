#ifndef DISPLAY_CONTROLLER_H
#define DISPLAY_CONTROLLER_H

#include <stdint.h>
#include "StateMachine.h"

// I2C configuration
#define DISPLAY_SDA_PIN   6
#define DISPLAY_SCL_PIN   7
#define DISPLAY_I2C_FREQ  400000
#define DISPLAY_ADDR      0x3C

// Display dimensions
#define DISPLAY_WIDTH     128
#define DISPLAY_HEIGHT    32
#define DISPLAY_PAGES     (DISPLAY_HEIGHT / 8)
#define DISPLAY_BUF_SIZE  (DISPLAY_WIDTH * DISPLAY_PAGES)

// Screensaver default: blank display after 5 minutes of inactivity
#define DISPLAY_SLEEP_MS_DEFAULT  (5 * 60 * 1000)

// Font dimensions
#define FONT_WIDTH        8
#define FONT_HEIGHT       8
#define FONT_CHARS_PER_LINE (DISPLAY_WIDTH / FONT_WIDTH)  // 16

class DisplayController {
public:
    DisplayController();
    ~DisplayController();

    void init(bool flipped);
    void update();

    void setMenuEntry(const char* name);
    void setSystemState(SystemState state);
    void setFlipped(bool flipped);
    void setSleepTimeout(uint32_t ms);
    void forceRedraw() { wake(); needs_redraw = true; }

    bool isPresent() const { return display_present; }
    bool isSleeping() const { return is_sleeping; }

private:
    uint8_t framebuffer[DISPLAY_BUF_SIZE];
    bool display_present;
    bool needs_redraw;
    bool is_flipped;
    bool is_sleeping;
    uint32_t last_activity_ms;
    uint32_t sleep_timeout_ms;

    // Cached display content
    char menu_line[FONT_CHARS_PER_LINE + 1];
    char state_line[FONT_CHARS_PER_LINE + 1];
    char info_line[FONT_CHARS_PER_LINE + 1];

    // Low-level SSD1306 operations
    bool probe();
    void sendCommand(uint8_t cmd);
    void sendCommands(const uint8_t* cmds, size_t len);
    void initDisplay(bool flipped);
    void flush();

    // Drawing operations
    void clear();
    void drawString(int x, int y, const char* str);
    void drawStringInverted(int x, int y, const char* str);
    void drawStringPixel(int x, int y, const char* str);

    void render();
    void wake();
};

#endif // DISPLAY_CONTROLLER_H

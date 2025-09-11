#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "tusb.h"

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

// Pin definitions
#define PIN_BUZZER      0
#define PIN_TRIGGER     1   // Active low
#define PIN_PRG_SWIO    9   // To SDI on CH32
#define PIN_WS2812      16  // WS2812 RGB LED on Waveshare Pico Zero
#define PIN_LED_GREEN   27
#define PIN_LED_YELLOW  28
#define PIN_LED_RED     29

// Timing definitions (in milliseconds)
#define BUZZER_DURATION_MS      500
#define LED_SUCCESS_DURATION_MS 3000
#define LED_FLASH_PERIOD_MS     300
#define TRIGGER_DEBOUNCE_MS     50
#define HEARTBEAT_PERIOD_MS     1000
#define WS2812_FLASH_DURATION_MS 200

// Buzzer frequencies (Hz) - different notes for different states
#define BUZZER_FREQ_DEFAULT     4000   // 4KHz default
#define BUZZER_FREQ_START       2000   // Start tone
#define BUZZER_FREQ_SUCCESS     4000   // Success tone
#define BUZZER_FREQ_FAILURE     1000   // Failure tone
#define BUZZER_FREQ_WARNING     3000   // Warning tone

// CH32V003 specifications
const int ch32v003_flash_size = 16*1024;
const char* PROGRAMMER_VERSION = "1.0.0";

// Global state
static bool programming_active = false;
static uint32_t led_flash_timer = 0;
static uint32_t success_led_timer = 0;
static bool led_yellow_state = false;
static uint buzzer_slice = 0;
static int current_firmware_index = 0;
static uint32_t heartbeat_timer = 0;
static uint32_t ws2812_flash_timer = 0;
static int ws2812_flash_count = 0;
static bool ws2812_flashing = false;

void delay_us(int us) {
    auto now = time_us_32();
    while(time_us_32() < (now + us));
}

void delay_ms(int ms) {
    sleep_ms(ms);
}

// WS2812 RGB LED Control
typedef struct {
    uint8_t r, g, b;
} rgb_t;

void ws2812_send_byte(uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        if (byte & (1 << i)) {
            // Send '1' bit: 800ns high, 450ns low
            gpio_put(PIN_WS2812, 1);
            busy_wait_us_32(1);  // ~800ns high
            gpio_put(PIN_WS2812, 0);
            busy_wait_us_32(1);  // ~450ns low
        } else {
            // Send '0' bit: 400ns high, 850ns low
            gpio_put(PIN_WS2812, 1);
            busy_wait_us_32(0);  // ~400ns high
            gpio_put(PIN_WS2812, 0);
            busy_wait_us_32(1);  // ~850ns low
        }
    }
}

void ws2812_set_color(uint8_t r, uint8_t g, uint8_t b) {
    // WS2812 expects GRB order
    // Disable interrupts for timing-critical section
    uint32_t interrupts = save_and_disable_interrupts();
    ws2812_send_byte(g);
    ws2812_send_byte(r);
    ws2812_send_byte(b);
    restore_interrupts(interrupts);
    
    // Reset signal (>50us low)
    gpio_put(PIN_WS2812, 0);
    busy_wait_us_32(60);
}

void ws2812_off() {
    ws2812_set_color(0, 0, 0);
}

void init_ws2812() {
    gpio_init(PIN_WS2812);
    gpio_set_dir(PIN_WS2812, GPIO_OUT);
    ws2812_off();
}

void init_buzzer_pwm() {
    // Configure PWM for buzzer
    gpio_set_function(PIN_BUZZER, GPIO_FUNC_PWM);
    buzzer_slice = pwm_gpio_to_slice_num(PIN_BUZZER);
    
    // Set initial frequency (will be updated by set_buzzer_frequency)
    pwm_set_wrap(buzzer_slice, 0);
    pwm_set_chan_level(buzzer_slice, PWM_CHAN_A, 0);
    pwm_set_enabled(buzzer_slice, false);
}

void set_buzzer_frequency(uint32_t frequency) {
    if (frequency == 0) {
        // Turn off buzzer
        pwm_set_enabled(buzzer_slice, false);
        return;
    }
    
    // Calculate divider and wrap values for desired frequency
    // PWM frequency = 125MHz / (divider * wrap)
    uint32_t clock_freq = 125000000;
    uint32_t divider = 1;
    uint32_t wrap = clock_freq / (frequency * divider);
    
    // Adjust divider if wrap is too large
    while (wrap > 65535 && divider < 256) {
        divider++;
        wrap = clock_freq / (frequency * divider);
    }
    
    pwm_set_clkdiv(buzzer_slice, (float)divider);
    pwm_set_wrap(buzzer_slice, wrap);
    pwm_set_chan_level(buzzer_slice, PWM_CHAN_A, wrap / 2);  // 50% duty cycle
}

void buzzer_on(uint32_t frequency) {
    set_buzzer_frequency(frequency);
    pwm_set_enabled(buzzer_slice, true);
}

void buzzer_off() {
    pwm_set_enabled(buzzer_slice, false);
}

void buzzer_beep(uint32_t frequency, int duration_ms) {
    buzzer_on(frequency);
    sleep_ms(duration_ms);
    buzzer_off();
    printf_g("// BUZZER: %dHz for %dms\\n", frequency, duration_ms);
}

void start_firmware_indication(int firmware_index) {
    ws2812_flashing = true;
    ws2812_flash_count = firmware_index + 1;  // Flash N+1 times for index N
    ws2812_flash_timer = to_ms_since_boot(get_absolute_time());
}

void update_ws2812() {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    if (ws2812_flashing) {
        // Firmware selection indication - flash blue N times
        static bool flash_state = false;
        static int flashes_done = 0;
        
        if ((now - ws2812_flash_timer) >= (WS2812_FLASH_DURATION_MS / 2)) {
            flash_state = !flash_state;
            if (flash_state) {
                ws2812_set_color(0, 0, 255);  // Blue
                flashes_done++;
            } else {
                ws2812_off();
            }
            ws2812_flash_timer = now;
            
            if (flashes_done >= ws2812_flash_count) {
                ws2812_flashing = false;
                flashes_done = 0;
                flash_state = false;
            }
        }
    } else {
        // Heartbeat - green flash every second
        if ((now - heartbeat_timer) >= HEARTBEAT_PERIOD_MS) {
            ws2812_set_color(0, 255, 0);  // Green
            sleep_ms(50);  // Brief flash
            ws2812_off();
            heartbeat_timer = now;
        }
    }
}

void init_gpio() {
    // Initialize WS2812 RGB LED (onboard on Waveshare Pico Zero)
    init_ws2812();
    
    // Initialize buzzer PWM
    init_buzzer_pwm();
    
    // Initialize trigger pin with pull-up
    gpio_init(PIN_TRIGGER);
    gpio_set_dir(PIN_TRIGGER, GPIO_IN);
    gpio_pull_up(PIN_TRIGGER);
    
    // Initialize LED pins
    gpio_init(PIN_LED_GREEN);
    gpio_set_dir(PIN_LED_GREEN, GPIO_OUT);
    gpio_put(PIN_LED_GREEN, true);  // Off (active low)
    
    gpio_init(PIN_LED_YELLOW);
    gpio_set_dir(PIN_LED_YELLOW, GPIO_OUT);
    gpio_put(PIN_LED_YELLOW, true);  // Off (active low)
    
    gpio_init(PIN_LED_RED);
    gpio_set_dir(PIN_LED_RED, GPIO_OUT);
    gpio_put(PIN_LED_RED, true);  // Off (active low)
}

void set_all_leds(bool state) {
    // Active low LEDs
    bool led_level = !state;
    gpio_put(PIN_LED_GREEN, led_level);
    gpio_put(PIN_LED_YELLOW, led_level);
    gpio_put(PIN_LED_RED, led_level);
}

void update_leds() {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    if (programming_active) {
        // Yellow flashing during programming
        if ((now - led_flash_timer) >= LED_FLASH_PERIOD_MS) {
            led_yellow_state = !led_yellow_state;
            gpio_put(PIN_LED_YELLOW, !led_yellow_state);  // Active low
            led_flash_timer = now;
        }
        // Keep other LEDs off during programming
        gpio_put(PIN_LED_GREEN, true);
        gpio_put(PIN_LED_RED, true);
    } else if ((success_led_timer != 0) && ((now - success_led_timer) < LED_SUCCESS_DURATION_MS)) {
        // Green LED on for success duration
        gpio_put(PIN_LED_GREEN, false);  // Active low - ON
        gpio_put(PIN_LED_YELLOW, true);
        gpio_put(PIN_LED_RED, true);
    } else {
        // Normal state - all LEDs off
        success_led_timer = 0;
        gpio_put(PIN_LED_GREEN, true);
        gpio_put(PIN_LED_YELLOW, true);
        gpio_put(PIN_LED_RED, true);
    }
}

bool wait_for_trigger() {
    static uint32_t last_trigger_time = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    // Check for trigger button press (active low with debounce)
    if (!gpio_get(PIN_TRIGGER)) {
        if ((now - last_trigger_time) > TRIGGER_DEBOUNCE_MS) {
            last_trigger_time = now;
            return true;
        }
    }
    return false;
}

void list_firmware() {
    printf_g("// Available firmware:\\n");
#ifdef FIRMWARE_INVENTORY_ENABLED
    for (int i = 0; i < firmware_count; i++) {
        printf_g("//   [%d] %s\\n", i, firmware_list[i].name);
    }
#else
    printf_g("//   [0] fallback (built-in minimal firmware)\\n");
#endif
}

bool check_bootsel_button() {
    // BOOTSEL button support - requires additional implementation
    // For now, simulate button press every 5 seconds for demo
    static uint32_t last_demo_time = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    if ((now - last_demo_time) > 5000) { // Every 5 seconds
        last_demo_time = now;
        return true;
    }
    return false;
}

void cycle_firmware() {
#ifdef FIRMWARE_INVENTORY_ENABLED
    // Cycle through available firmware
    current_firmware_index = (current_firmware_index + 1) % firmware_count;
#else
    // Only fallback firmware available
    current_firmware_index = 0;
#endif
    
    printf_g("// BOOTSEL pressed - selected firmware index: %d\\n", current_firmware_index);
    buzzer_beep(BUZZER_FREQ_WARNING, 150);
    start_firmware_indication(current_firmware_index);
}

bool program_flash(RVDebug* rvd, WCHFlash* flash, const uint8_t* data, size_t size) {
    printf_g("// Starting flash programming...\\n");
    printf_g("// Firmware size: %d bytes\\n", size);
    
    // Halt the target
    if (!rvd->halt()) {
        printf_g("// ERROR: Could not halt target\\n");
        return false;
    }
    
    // Unlock and erase flash
    printf_g("// Unlocking and erasing flash...\\n");
    flash->unlock_flash();
    flash->wipe_chip();
    
    // Write flash (ensure size is multiple of 4)
    size_t aligned_size = (size + 3) & ~3;  // Round up to multiple of 4
    printf_g("// Writing %d bytes to flash (aligned to %d)...\\n", size, aligned_size);
    
    // Create aligned buffer if needed
    uint8_t* aligned_data = (uint8_t*)data;
    bool need_free = false;
    if (size != aligned_size) {
        aligned_data = new uint8_t[aligned_size];
        memcpy(aligned_data, data, size);
        memset(aligned_data + size, 0xFF, aligned_size - size);  // Fill with 0xFF
        need_free = true;
    }
    
    flash->write_flash(flash->get_flash_base(), aligned_data, aligned_size);
    
    // Verify flash
    printf_g("// Verifying flash...\\n");
    bool success = flash->verify_flash(flash->get_flash_base(), aligned_data, aligned_size);
    
    if (need_free) {
        delete[] aligned_data;
    }
    
    if (!success) {
        printf_g("// ERROR: Flash verification failed\\n");
        return false;
    }
    
    printf_g("// Flash programming and verification complete\\n");
    
    // Lock flash and reset
    flash->lock_flash();
    rvd->reset();
    
    return true;
}

// Fallback firmware if no firmware submodules are available
const uint8_t fallback_firmware[] = {
    // Minimal valid RISC-V reset vector
    0x37, 0x01, 0x00, 0x08,  // lui sp, 0x80000
    0x13, 0x01, 0x01, 0x00,  // addi sp, sp, 0
    0x6F, 0x00, 0x00, 0x00,  // j . (infinite loop)
};
const size_t fallback_firmware_size = sizeof(fallback_firmware);

int main() {
    stdio_init_all();
    init_gpio();
    
    printf_g("\\n\\n\\n");
    printf_g("//==============================================================================\\n");
    printf_g("// CH32V003 Programmer v%s\\n", PROGRAMMER_VERSION);
    printf_g("// Based on PicoRVD\\n\\n");
    
    // Quick LED test on startup
    set_all_leds(true);
    sleep_ms(200);
    set_all_leds(false);
    
    printf_g("// Initializing PicoSWIO on GPIO%d\\n", PIN_PRG_SWIO);
    PicoSWIO* swio = new PicoSWIO();
    swio->reset(PIN_PRG_SWIO);
    
    printf_g("// Initializing RVDebug\\n");
    RVDebug* rvd = new RVDebug(swio, 16);
    rvd->init();
    
    printf_g("// Initializing WCHFlash\\n");
    WCHFlash* flash = new WCHFlash(rvd, ch32v003_flash_size);
    flash->reset();
    
    printf_g("// Initializing SoftBreak\\n");
    SoftBreak* soft = new SoftBreak(rvd, flash);
    soft->init();
    
    printf_g("// Initializing GDBServer\\n");
    GDBServer* gdb = new GDBServer(rvd, flash, soft);
    gdb->reset();
    
    printf_g("// Initializing Console\\n");
    Console* console = new Console(rvd, flash, soft);
    console->reset();
    
    printf_g("// CH32V003 Programmer Ready!\\n");
    list_firmware();
    printf_g("// Waiting for trigger (GPIO%d)...\\n\\n", PIN_TRIGGER);
    
    console->start();
    
    while (1) {
        // Update LED states
        update_leds();
        
        // Update WS2812 RGB LED
        update_ws2812();
        
        // Check BOOTSEL button for firmware cycling
        if (check_bootsel_button()) {
            cycle_firmware();
        }
        
        // Check for trigger
        if (wait_for_trigger() && !programming_active) {
            printf_g("\\n// Trigger detected! Starting flash sequence...\\n");
            
            // Set programming state
            programming_active = true;
            
            // Start buzzer with start tone
            buzzer_beep(BUZZER_FREQ_START, BUZZER_DURATION_MS);
            
            // Clear all LEDs and start yellow flashing
            set_all_leds(false);
            
            // Attempt programming
            bool success = false;
            const uint8_t* firmware_data = nullptr;
            size_t firmware_size = 0;
            
            // Select firmware to program
#ifdef FIRMWARE_INVENTORY_ENABLED
            if (current_firmware_index < firmware_count) {
                firmware_data = firmware_list[current_firmware_index].data;
                firmware_size = firmware_list[current_firmware_index].size;
                printf_g("// Programming firmware: %s\\n", firmware_list[current_firmware_index].name);
            } else {
                // Fallback if index is invalid
                firmware_data = fallback_firmware;
                firmware_size = fallback_firmware_size;
                printf_g("// Invalid index, using fallback firmware\\n");
            }
#else
            // Use fallback firmware
            firmware_data = fallback_firmware;
            firmware_size = fallback_firmware_size;
            printf_g("// Programming fallback firmware\\n");
#endif
            
            // Check if target is connected
            if (rvd->halt()) {
                printf_g("// Target connected and halted\\n");
                
                // Program the selected firmware
                success = program_flash(rvd, flash, firmware_data, firmware_size);
            } else {
                printf_g("// ERROR: Could not connect to target\\n");
                success = false;
            }
            
            // Clear programming state
            programming_active = false;
            
            // Show result
            if (success) {
                printf_g("// Programming SUCCESSFUL!\\n");
                success_led_timer = to_ms_since_boot(get_absolute_time());
                // Success buzzer tone
                buzzer_beep(BUZZER_FREQ_SUCCESS, BUZZER_DURATION_MS);
            } else {
                printf_g("// Programming FAILED!\\n");
                // Failure buzzer tone
                buzzer_beep(BUZZER_FREQ_FAILURE, BUZZER_DURATION_MS);
                // Red LED on for error
                gpio_put(PIN_LED_RED, false);  // Active low - ON
                sleep_ms(LED_SUCCESS_DURATION_MS);
                gpio_put(PIN_LED_RED, true);   // Off
            }
            
            printf_g("// Flash sequence complete. Waiting for next trigger...\\n");
        }
        
        // Small delay to prevent CPU hogging
        sleep_ms(10);
    }
    
    return 0;
}
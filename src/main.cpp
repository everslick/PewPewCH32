#include <stdio.h>
#include <string.h>
#include <math.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"
#include "tusb.h"

#include "PicoSWIO.h"
#include "RVDebug.h"
#include "WCHFlash.h"
#include "SoftBreak.h"
#include "Console.h"
#include "GDBServer.h"
#include "debug_defines.h"
#include "utils.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

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
#define TRIGGER_DEBOUNCE_MS     50
#define HEARTBEAT_PERIOD_MS     3000  // 3 seconds between heartbeat flashes

// Buzzer frequencies (Hz) - different notes for different states
#define BUZZER_FREQ_DEFAULT     4000   // 4KHz default
#define BUZZER_FREQ_START       2000   // Start tone
#define BUZZER_FREQ_SUCCESS     4000   // Success tone
#define BUZZER_FREQ_FAILURE     1000   // Failure tone
#define BUZZER_FREQ_WARNING     3000   // Warning tone

// CH32V003 specifications
const int ch32v003_flash_size = 16*1024;
const char* PROGRAMMER_VERSION = "1.0.0";

// WS2812 brightness (0-255, where 255 is blindingly bright)
#define LED_BRIGHTNESS 64

// System States
enum SystemState {
    STATE_IDLE,
    STATE_CHECKING_TARGET,
    STATE_PROGRAMMING, 
    STATE_CYCLING_FIRMWARE,
    STATE_SUCCESS,
    STATE_ERROR
};

// Global state
static SystemState current_state = STATE_IDLE;
static uint32_t state_timer = 0;
static uint buzzer_slice = 0;
static int current_firmware_index = 0;

// LED state management
struct led_state_t {
    uint32_t timer;
    bool active;
    bool flash_on;
    int flash_count;
    int flashes_done;
    uint32_t flash_duration_ms;
};

// LED state instances for each function
static struct led_state_t heartbeat_led = {0, false, false, 0, 0, 100};
static struct led_state_t firmware_led = {0, false, false, 0, 0, 100};
static struct led_state_t error_led = {0, false, false, 0, 0, 0};
static struct led_state_t programming_led = {0, false, false, 0, 0, 100};

void delay_us(int us) {
    auto now = time_us_32();
    while(time_us_32() < (now + us));
}

void delay_ms(int ms) {
    sleep_ms(ms);
}

// Add timeout-based halt function
bool halt_with_timeout(RVDebug* rvd, uint32_t timeout_ms) {
    uint32_t start_time = to_ms_since_boot(get_absolute_time());
    
    // Checking target connection...
    
    rvd->set_dmcontrol(0x80000001);
    
    int attempts = 0;
    while (!rvd->get_dmstatus().ALLHALTED) {
        uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - start_time;
        if (elapsed > timeout_ms) {
            // Timeout - no target connected
            // Target detection timeout
            rvd->set_dmcontrol(0x00000001);  // Clean up
            return false;
        }
        attempts++;
        // Progress check removed - too verbose
        sleep_ms(1);  // Small delay to prevent busy waiting
    }
    
    // Target halted successfully
    rvd->set_dmcontrol(0x00000001);
    return true;
}

// WS2812 RGB LED Control
// WS2812 PIO implementation
static PIO ws2812_pio = pio1;
static uint ws2812_sm = 0;

void ws2812_set_color(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t pixel = ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;
    // Send GRB data to PIO
    pio_sm_put_blocking(ws2812_pio, ws2812_sm, pixel << 8u);
}

void ws2812_off() {
    ws2812_set_color(0, 0, 0);
}

// Convert HSV to RGB for rainbow effects
void hsv_to_rgb(float h, float s, float v, uint8_t* r, uint8_t* g, uint8_t* b) {
    float c = v * s;
    float x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1));
    float m = v - c;
    
    float r_prime, g_prime, b_prime;
    
    if (h >= 0 && h < 60) {
        r_prime = c; g_prime = x; b_prime = 0;
    } else if (h >= 60 && h < 120) {
        r_prime = x; g_prime = c; b_prime = 0;
    } else if (h >= 120 && h < 180) {
        r_prime = 0; g_prime = c; b_prime = x;
    } else if (h >= 180 && h < 240) {
        r_prime = 0; g_prime = x; b_prime = c;
    } else if (h >= 240 && h < 300) {
        r_prime = x; g_prime = 0; b_prime = c;
    } else {
        r_prime = c; g_prime = 0; b_prime = x;
    }
    
    *r = (uint8_t)((r_prime + m) * 255);
    *g = (uint8_t)((g_prime + m) * 255);
    *b = (uint8_t)((b_prime + m) * 255);
}

// Rainbow startup animation
void startup_rainbow_animation() {
    
    const int duration_ms = 3000;  // 3 seconds
    const int steps = 150;  // 150 steps = 20ms per step
    const int step_delay = duration_ms / steps;
    
    for (int step = 0; step < steps; step++) {
        // Calculate hue (0-360 degrees) cycling through rainbow
        float hue = (step * 360.0f) / steps;
        
        // Calculate brightness fade in/out
        float progress = (float)step / (steps - 1);
        float brightness;
        
        if (progress <= 0.5f) {
            // Fade in during first half
            brightness = progress * 2.0f;
        } else {
            // Fade out during second half  
            brightness = 2.0f - (progress * 2.0f);
        }
        
        // Convert HSV to RGB
        uint8_t r, g, b;
        hsv_to_rgb(hue, 1.0f, brightness, &r, &g, &b);
        
        // Set LED color
        ws2812_set_color(r, g, b);
        
        // Small delay for smooth animation
        sleep_ms(step_delay);
    }
    
    // Turn off LED at end
    ws2812_off();
}

void init_ws2812() {
    // Load the PIO program
    uint offset = pio_add_program(ws2812_pio, &ws2812_program);
    
    // Initialize the PIO state machine for WS2812 - RGB mode for 3-color LED
    ws2812_program_init(ws2812_pio, ws2812_sm, offset, PIN_WS2812, 800000, false);
    
    // Turn off LED initially
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
}

void start_firmware_indication(int firmware_index) {
    firmware_led.active = true;
    firmware_led.flash_count = firmware_index + 1;  // Flash N+1 times for index N
    firmware_led.timer = to_ms_since_boot(get_absolute_time());
    firmware_led.flash_on = false;
    firmware_led.flashes_done = 0;
    firmware_led.flash_duration_ms = 100;  // 100ms per flash state
}

void update_ws2812() {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    // Handle firmware flashing (can happen in any state)
    if (firmware_led.active) {
        if ((now - firmware_led.timer) >= firmware_led.flash_duration_ms) {
            firmware_led.flash_on = !firmware_led.flash_on;
            if (firmware_led.flash_on) {
                ws2812_set_color(0, 0, 255);  // Bright blue flash
                firmware_led.flashes_done++;
            } else {
                ws2812_off();
                // Check if we're done flashing (after turning OFF)
                if (firmware_led.flashes_done >= firmware_led.flash_count) {
                    firmware_led.active = false;
                    firmware_led.flashes_done = 0;
                    firmware_led.flash_on = false;
                    // Return to idle state after flashing
                    current_state = STATE_IDLE;
                    heartbeat_led.timer = now; // Reset heartbeat timer to current time
                    // Don't return here - let the state machine handle IDLE state
                }
            }
            firmware_led.timer = now;
        }
        if (firmware_led.active) {
            return; // Only return if still actively flashing
        }
    }
    
    // State-based WS2812 control
    switch (current_state) {
        case STATE_IDLE:
            // Initialize heartbeat timer if not set
            if (heartbeat_led.timer == 0) {
                heartbeat_led.timer = now;
            }
            
            // Heartbeat - green flash every 3 seconds
            if (!heartbeat_led.active && (now - heartbeat_led.timer) >= HEARTBEAT_PERIOD_MS) {
                heartbeat_led.active = true;
                heartbeat_led.flash_on = true;
                heartbeat_led.timer = now;
                ws2812_set_color(0, 32, 0);   // Green heartbeat (brightness 32)
                gpio_put(PIN_LED_GREEN, false);  // Turn on green GPIO LED (active low)
            }
            // Turn off heartbeat after 100ms
            if (heartbeat_led.active && heartbeat_led.flash_on && (now - heartbeat_led.timer) >= 100) {
                heartbeat_led.flash_on = false;
                heartbeat_led.active = false;
                heartbeat_led.timer = now; // Reset for next heartbeat period
                ws2812_off();
                gpio_put(PIN_LED_GREEN, true);  // Turn off green GPIO LED (active low)
            }
            break;
            
        case STATE_CYCLING_FIRMWARE:
            // Handled above in firmware_led logic
            break;
            
        case STATE_CHECKING_TARGET:
            // Keep WS2812 off during target checking
            ws2812_off();
            break;
            
        case STATE_PROGRAMMING:
            // RGB LED is controlled by update_programming_led() during programming
            // Don't interfere with it here
            break;
            
        case STATE_SUCCESS:
            // Keep WS2812 off during success state
            ws2812_off();
            break;
            
        case STATE_ERROR:
            // Show red LED for error state - set once when entering state
            if (!error_led.active) {
                ws2812_set_color(255, 0, 0);  // Bright red error indication on RGB
                gpio_put(PIN_LED_RED, false);  // Turn on red GPIO LED (active low)
                error_led.active = true;
                error_led.timer = now;
            }
            
            // Auto-transition back to IDLE after 2 seconds
            if ((now - error_led.timer) >= 2000) {
                current_state = STATE_IDLE;
                heartbeat_led.timer = 0; // Reset heartbeat timer for clean idle state
                error_led.active = false; // Reset error LED state
                gpio_put(PIN_LED_RED, true);  // Turn off red GPIO LED (active low)
                ws2812_off();
            }
            break;
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

void start_programming_led() {
    programming_led.active = true;
    programming_led.timer = to_ms_since_boot(get_absolute_time());
    programming_led.flash_on = false;
    programming_led.flash_duration_ms = 100; // 100ms on, 100ms off = 200ms period, 50% duty
}

void stop_programming_led() {
    programming_led.active = false;
    gpio_put(PIN_LED_YELLOW, true); // Turn off (active low)
    ws2812_off(); // Also turn off RGB LED
}

void update_programming_led() {
    if (!programming_led.active) return;
    
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((now - programming_led.timer) >= programming_led.flash_duration_ms) {
        programming_led.flash_on = !programming_led.flash_on;
        gpio_put(PIN_LED_YELLOW, !programming_led.flash_on); // Active low logic
        
        // Also control RGB LED - bright yellow when on
        if (programming_led.flash_on) {
            ws2812_set_color(LED_BRIGHTNESS, LED_BRIGHTNESS, 0); // Yellow (R+G)
        } else {
            ws2812_off();
        }
        
        programming_led.timer = now;
    }
}

// update_leds() removed - all LED control now through WS2812

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
    printf_g("// Available firmware:\n");
#ifdef FIRMWARE_INVENTORY_ENABLED
    for (int i = 0; i < firmware_count; i++) {
        printf_g("//   [%d] %s\n", i, firmware_list[i].name);
    }
#else
    printf_g("//   [0] fallback (built-in minimal firmware)\n");
#endif
}

// Read BOOTSEL button state using proper Pico SDK method
bool __no_inline_not_in_flash_func(get_bootsel_button)() {
    const uint CS_PIN_INDEX = 1;

    uint32_t flags = save_and_disable_interrupts();

    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
        GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
        IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    for (volatile int i = 0; i < 1000; ++i);

#if PICO_RP2040
    #define CS_BIT (1u << 1)
#else
    #define CS_BIT SIO_GPIO_HI_IN_QSPI_CSN_BITS
#endif
    bool button_state = !(sio_hw->gpio_hi_in & CS_BIT);

    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
        GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
        IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);

    return button_state;
}

bool check_bootsel_button() {
    return get_bootsel_button();
}

// Forward declaration
bool program_flash(RVDebug* rvd, WCHFlash* flash, const uint8_t* data, size_t size);

// Process state machine
void process_state_machine(RVDebug* rvd, WCHFlash* flash) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    switch (current_state) {
        case STATE_CHECKING_TARGET:
            // Non-blocking target check with timeout
            if (halt_with_timeout(rvd, 100)) {  // 100ms timeout
                printf_g("// Target detected - starting programming...\n");
                current_state = STATE_PROGRAMMING;
                state_timer = now;
                start_programming_led(); // Start blinking yellow LED
                buzzer_beep(BUZZER_FREQ_START, BUZZER_DURATION_MS);
            } else {
                printf_g("// ERROR: No CH32V003 target detected.\n");
                current_state = STATE_ERROR;
                state_timer = now;
                buzzer_beep(BUZZER_FREQ_FAILURE, 300);
            }
            break;
            
        case STATE_PROGRAMMING:
            // Do actual programming
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
                    // Fallback firmware is defined later in file
                    extern const uint8_t fallback_firmware[];
                    extern const size_t fallback_firmware_size;
                    firmware_data = fallback_firmware;
                    firmware_size = fallback_firmware_size;
                    printf_g("// Invalid index, using fallback firmware\n");
                }
#else
                // Fallback firmware is defined later in file  
                extern const uint8_t fallback_firmware[];
                extern const size_t fallback_firmware_size;
                firmware_data = fallback_firmware;
                firmware_size = fallback_firmware_size;
                printf_g("// Programming fallback firmware\n");
#endif
                
                success = program_flash(rvd, flash, firmware_data, firmware_size);
                
                if (success) {
                    printf_g("// Programming SUCCESSFUL!\n");
                    current_state = STATE_SUCCESS;
                    stop_programming_led(); // Stop blinking yellow LED
                    buzzer_beep(BUZZER_FREQ_SUCCESS, BUZZER_DURATION_MS);
                } else {
                    printf_g("// Programming FAILED!\n");
                    current_state = STATE_ERROR;
                    stop_programming_led(); // Stop blinking yellow LED
                    buzzer_beep(BUZZER_FREQ_FAILURE, BUZZER_DURATION_MS);
                }
                state_timer = now;
            }
            break;
            
        default:
            // No processing needed for other states
            break;
    }
}

void cycle_firmware() {
#ifdef FIRMWARE_INVENTORY_ENABLED
    if (firmware_count > 1) {
        // Cycle through available firmware
        current_firmware_index = (current_firmware_index + 1) % firmware_count;
    }
    // Always show current firmware index (even if only 1 available)
    printf_g("// Firmware selected: [%d] %s\n", current_firmware_index, firmware_list[current_firmware_index].name);
#else
    // Only fallback firmware available
    current_firmware_index = 0;
    printf_g("// Firmware selected: [0] fallback\n");
#endif
    
    buzzer_beep(BUZZER_FREQ_WARNING, 150);
    
    // Enter firmware cycling state
    current_state = STATE_CYCLING_FIRMWARE;
    state_timer = to_ms_since_boot(get_absolute_time());
    start_firmware_indication(current_firmware_index);
}

bool program_flash(RVDebug* rvd, WCHFlash* flash, const uint8_t* data, size_t size) {
    printf_g("// Starting flash programming...\n");
    printf_g("// Firmware size: %d bytes\n", size);
    
    // Halt the target
    if (!rvd->halt()) {
        printf_g("// ERROR: Could not halt target\n");
        return false;
    }
    
    // Unlock and erase flash
    printf_g("// Unlocking and erasing flash...\n");
    flash->unlock_flash();
    flash->wipe_chip();
    
    // Write flash (ensure size is multiple of 4)
    size_t aligned_size = (size + 3) & ~3;  // Round up to multiple of 4
    printf_g("// Writing %d bytes to flash (aligned to %d)...\n", size, aligned_size);
    
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
    printf_g("// Verifying flash...\n");
    bool success = flash->verify_flash(flash->get_flash_base(), aligned_data, aligned_size);
    
    if (need_free) {
        delete[] aligned_data;
    }
    
    if (!success) {
        printf_g("// ERROR: Flash verification failed\n");
        return false;
    }
    
    printf_g("// Flash programming and verification complete\n");
    
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
    
    // Give USB serial time to initialize
    sleep_ms(1000);
    
    init_gpio();
    
    // Rainbow startup animation
    startup_rainbow_animation();
    
    // Clear screen
    printf("\n\n");
    printf_g("\n\n\n");
    printf_g("//==============================================================================\n");
    printf_g("// PewPewCH32 Programmer v%s\n", PROGRAMMER_VERSION);
    printf_g("// Based on PicoRVD\n\n");
    
    // Quick LED test on startup
    set_all_leds(true);
    sleep_ms(200);
    set_all_leds(false);
    
    // Initialize timers
    uint32_t now = to_ms_since_boot(get_absolute_time());
    state_timer = now;
    // LED timers initialized in their structs
    
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
    
    printf_g("// CH32V003 Programmer Ready!\n");
    list_firmware();
    printf_g("// Waiting for BOOTSEL button or trigger (GPIO%d)...\n\n", PIN_TRIGGER);
    
    console->start();
    
    while (1) {
        // Update WS2812 RGB LED
        update_ws2812();
        
        // Update programming LED (yellow LED blinking during programming)
        update_programming_led();
        
        // Process state machine
        process_state_machine(rvd, flash);
        
        // Only handle button input when in IDLE state
        if (current_state == STATE_IDLE) {
            static bool bootsel_pressed = false;
            static uint32_t bootsel_press_start = 0;
            static bool long_press_triggered = false;
            bool bootsel_current = check_bootsel_button();
            uint32_t now = to_ms_since_boot(get_absolute_time());
        
        if (bootsel_current && !bootsel_pressed) {
            // Button just pressed - record start time
            bootsel_pressed = true;
            bootsel_press_start = now;
            long_press_triggered = false;
        } else if (bootsel_current && bootsel_pressed && !long_press_triggered) {
            // Button still held - check if we've reached long press threshold
            if ((now - bootsel_press_start) >= 750) {
                // Long press triggered while button is held
                cycle_firmware();
                long_press_triggered = true;  // Prevent multiple triggers
            }
        } else if (!bootsel_current && bootsel_pressed) {
            // Button just released - check what to do based on duration and whether long press was triggered
            uint32_t press_duration = now - bootsel_press_start;
            bootsel_pressed = false;
            long_press_triggered = false;
            
            if (press_duration < 250) {
                // Short press (<250ms) - start target check
                if (true) {  // Already in IDLE state check above
                    printf_g("// Checking target...\n");
                    
                    // Quick connection test before starting programming
                    if (!halt_with_timeout(rvd, 100)) {
                        printf_g("// ERROR: No CH32V003 target detected. Please connect target and try again.\n");
                        current_state = STATE_ERROR;
                        state_timer = to_ms_since_boot(get_absolute_time());
                        buzzer_beep(BUZZER_FREQ_FAILURE, 300);
                    } else {
                        printf_g("// Target detected via timeout function!\n");
                        current_state = STATE_PROGRAMMING;
                        state_timer = to_ms_since_boot(get_absolute_time());
                        start_programming_led(); // Start blinking yellow LED
                    }
                }
            }
            // Long press handling moved to while button is held
        }
        // End of IDLE state button handling
        }
        
        // Check for trigger
        if (wait_for_trigger() && current_state == STATE_IDLE) {
            printf_g("\n// Trigger detected! Starting flash sequence...\n");
            
            // Set programming state
            current_state = STATE_CHECKING_TARGET;
            state_timer = to_ms_since_boot(get_absolute_time());
            
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
                printf_g("// Programming firmware: %s\n", firmware_list[current_firmware_index].name);
            } else {
                // Fallback if index is invalid
                firmware_data = fallback_firmware;
                firmware_size = fallback_firmware_size;
                printf_g("// Invalid index, using fallback firmware\n");
            }
#else
            // Use fallback firmware
            firmware_data = fallback_firmware;
            firmware_size = fallback_firmware_size;
            printf_g("// Programming fallback firmware\n");
#endif
            
            // Check if target is connected
            if (rvd->halt()) {
                printf_g("// Target connected and halted\n");
                
                // Program the selected firmware
                success = program_flash(rvd, flash, firmware_data, firmware_size);
            } else {
                printf_g("// ERROR: Could not connect to target\n");
                success = false;
            }
            
            // Clear programming state
            // State handled by state machine
            
            // Show result
            if (success) {
                printf_g("// Programming SUCCESSFUL!\n");
                current_state = STATE_SUCCESS;
                state_timer = to_ms_since_boot(get_absolute_time());
                stop_programming_led(); // Stop blinking yellow LED
                // Success buzzer tone
                buzzer_beep(BUZZER_FREQ_SUCCESS, BUZZER_DURATION_MS);
            } else {
                printf_g("// Programming FAILED!\n");
                stop_programming_led(); // Stop blinking yellow LED
                // Failure buzzer tone
                buzzer_beep(BUZZER_FREQ_FAILURE, BUZZER_DURATION_MS);
                // Red LED is now handled by STATE_ERROR in update_ws2812()
                current_state = STATE_ERROR;
                state_timer = to_ms_since_boot(get_absolute_time());
            }
            
            printf_g("// Flash sequence complete. Waiting for next trigger...\n");
        }
        
        // Small delay to prevent CPU hogging
        sleep_ms(10);
    }
    
    return 0;
}

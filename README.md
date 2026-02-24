# PewPewCH32 Programmer

A standalone CH32V003 microcontroller programmer using Raspberry Pi Pico, based on PicoRVD.

## Purpose

PewPewCH32 is designed for developers and manufacturers who need to:

- **Rapidly program CH32V003 microcontrollers** with different firmware versions
- **Switch between multiple firmware images** without reflashing the programmer
- **Create standalone programming stations** for production environments

### Key Features

- **Multi-firmware storage**: Store multiple firmware images in RP2040's 2MB flash
- **No PC required**: Once configured, works as a standalone programmer
- **OLED display**: Optional 128x32 SSD1306 display shows menu and status
- **Setup screen**: Configure display orientation, screensaver timeout, and SWIO pin
- **Persistent settings**: Configuration survives power cycles (stored in flash)
- **Visual and audio feedback**: WS2812 RGB LED, discrete LEDs, and buzzer
- **Multiple input methods**: Hardware button, BOOTSEL button, and USB serial

## Hardware Requirements

- **Raspberry Pi Pico** (or compatible RP2040 board)
- **Waveshare Pico Zero** recommended (has onboard WS2812 RGB LED)

### Optional Components

- **SSD1306 OLED display** (128x32, I2C, address 0x3C)
- **Buzzer** (passive, connected to GPIO0)
- **Trigger button** (active low, connected to GPIO1)
- **Discrete LEDs** (active low: green, yellow, red)

### Pin Connections

| Pico Pin | Function | Description |
|----------|----------|-------------|
| GPIO0    | Buzzer   | PWM buzzer output (optional) |
| GPIO1    | Trigger  | Programming trigger button, active low (optional) |
| GPIO6    | OLED SDA | I2C data for SSD1306 display (optional) |
| GPIO7    | OLED SCL | I2C clock for SSD1306 display (optional) |
| GPIO8    | SWIO     | CH32V003 SDI pin (configurable in setup) |
| GPIO14   | Green LED| Heartbeat / ready indication, active low (optional) |
| GPIO15   | Yellow LED| Programming in progress, active low (optional) |
| GPIO16   | WS2812   | RGB LED status (onboard on Waveshare Pico Zero) |
| GPIO26   | Red LED  | Error indication, active low (optional) |

## Building

### Prerequisites

- **ARM GCC toolchain** (`arm-none-eabi-gcc`)
- **CMake** 3.13+
- **Git** (for cloning dependencies)
- **xxd** (for firmware conversion)

### Installing Dependencies (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install cmake build-essential git xxd gcc-arm-none-eabi
```

### Quick Start

```bash
git clone https://github.com/your-repo/PewPewCH32.git
cd PewPewCH32
./build.sh
```

The build script automatically:
- Checks for required dependencies
- Clones build dependencies (Pico SDK, picorvd)
- Verifies firmware binaries from `firmware.txt`
- Generates the `.uf2` file ready for flashing

### Build Options

```bash
./build.sh              # Normal build
./build.sh clean        # Remove build directory
./build.sh distclean    # Remove all generated files and dependencies
./build.sh install      # Build and install to Pico in BOOTSEL mode
```

## Firmware Management

### firmware.txt Format

The programmer loads firmware binaries listed in `firmware.txt`:

```
# Format: NAME PATH ADDRESS
#
# NAME:    Firmware identifier (used in menu)
# PATH:    Relative path to binary file
# ADDRESS: Flash target address (hex)

BootLoader     ../emonio-fw/ext/bootloader/bootloader.bin  0x0000
X3[SD-WD]      ../emonio-fw/ext/bin/x3-sd-wd-1.0.bin       0x1040
X4[SD-WD-IN]   ../emonio-fw/ext/bin/x4-sd-wd-in-1.0.bin    0x1040
X3[BLINK]      ../emonio-fw/ext/bin/x3-blink-1.0.bin       0x1040
X4[BLINK]      ../emonio-fw/ext/bin/x4-blink-1.0.bin       0x1040
```

PewPewCH32 treats each binary as opaque data and flashes it at the specified address. It has no knowledge of the CH32 flash layout — the binary is expected to be self-contained (e.g., APP binaries include their own XAPP header).

A fallback firmware (minimal RISC-V reset vector) is included for standalone operation when no external firmware binaries are available.

## Flashing and Usage

### 1. Flash to Pico

**Option A: Automatic install (Linux)**
```bash
# Put Pico in BOOTSEL mode (hold button while connecting USB)
./build.sh install
```

**Option B: Manual copy**
- Hold BOOTSEL button while connecting Pico to USB
- Copy `build/PewPewCH32.uf2` to the RPI-RP2 drive

### 2. Connect CH32V003 Target

- CH32V003 SDI pin -> Pico GPIO8 (or configured SWIO pin)
- Common ground connection

### 3. Monitor via USB Serial

```bash
screen /dev/ttyACM0 115200
```

### 4. Programming

**BOOTSEL Button Controls:**
- **Short press (<250ms)**: Program selected firmware to CH32V003
- **Long press (>=750ms)**: Cycle through available firmware

**Trigger Button (GPIO1):**
- Press to immediately program the selected firmware

**Serial Controls:**

| Key | Action |
|-----|--------|
| `0`-`9` | Quick-select and program firmware entry |
| Up/Down | Navigate firmware list |
| Enter | Program selected firmware |
| `S` | Enter setup screen |
| `R` | Refresh display |

## Setup Screen

Press `S` in the serial terminal to enter the setup screen. Configure:

| Setting | Options |
|---------|---------|
| Display orientation | Normal / Flipped |
| Screensaver timeout | Off / 1 min / 3 min / 5 min / 10 min |
| SWIO pin | GPIO 2-29 (excluding reserved pins) |

**Setup controls:** Up/Down to select setting, Left/Right to change value, Enter to save, Esc to cancel.

Settings are persisted to flash and survive power cycles.

## Status Indicators

### WS2812 RGB LED

- **Rainbow fade**: Startup animation (3 seconds)
- **Green pulse**: System ready (flash every 3 seconds)
- **Blue flashes**: Firmware selection (count = firmware index)
- **Red solid**: Error state (2 seconds)

### Discrete LEDs

- **Green (GPIO14)**: Heartbeat (blinks every 3 seconds when ready)
- **Yellow (GPIO15)**: Programming in progress
- **Red (GPIO26)**: Error occurred

### Buzzer

- **2 kHz**: Programming started
- **4 kHz**: Success
- **1 kHz**: Failure
- **3 kHz**: Warning

### OLED Display

When connected, the SSD1306 display shows:
- Current firmware selection and menu
- Programming status and progress
- Enters screensaver mode after the configured timeout (button press wakes it)

## Project Structure

```
PewPewCH32/
├── firmware.txt            # Firmware manifest
├── manifest.cmake          # CMake firmware build system
├── build.sh                # Build script
├── CMakeLists.txt          # Main CMake configuration
├── src/
│   ├── main.cpp            # Entry point, event loop, terminal UI
│   ├── StateMachine.cpp/h  # Programming state machine
│   ├── LedController.cpp/h # WS2812 RGB and GPIO LED control
│   ├── DisplayController.cpp/h # SSD1306 OLED driver
│   ├── BuzzerController.cpp/h  # PWM buzzer control
│   ├── InputHandler.cpp/h  # Button debouncing and events
│   ├── Settings.cpp/h      # Flash-backed persistent settings
│   ├── SetupScreen.cpp/h   # Terminal-based setup menu
│   └── ws2812.pio          # PIO assembly for WS2812 protocol
├── picorvd/                # PicoRVD debug interface (cloned)
├── pico-sdk/               # Raspberry Pi Pico SDK (cloned)
└── build/                  # Generated build files
    └── PewPewCH32.uf2      # Programmer firmware for Pico
```

## I2C Bootloader

The CH32V003 bootloader enables over-the-air updates via I2C.

### Memory Layout (16KB Flash)

```
0x0000 ┌──────────────────┐
       │   Bootloader     │  4KB (fixed, flash via SWIO only)
0x1000 ├──────────────────┤
       │   Boot State     │  64B (update request flag)
0x1040 ├──────────────────┤
       │   App Header     │  64B (magic, version, CRC, entry)
0x1080 ├──────────────────┤
       │   Application    │  ~11.9KB (updatable via I2C or SWIO)
0x4000 └──────────────────┘
```

### App Header Structure

The app header at 0x1040 is part of the application binary (generated at build time by emonio-fw, not by PewPewCH32):

```c
typedef struct __attribute__((packed)) {
  uint32_t magic;           // 0x50504158 ("XAPP")
  uint8_t  fw_ver_major;
  uint8_t  fw_ver_minor;
  uint8_t  bl_ver_min;      // Minimum bootloader version
  uint8_t  hw_type;
  uint32_t app_size;        // Application code size in bytes
  uint32_t app_crc32;       // CRC32 of application code
  uint32_t entry_point;     // Entry address (0x1080)
  uint32_t header_crc32;    // CRC32 of header bytes 0-19
  uint8_t  reserved[40];
} app_header_t;  // 64 bytes
```

### I2C Protocol

Both bootloader and application use I2C address **0x42**. The mode is detected via register 0x00:

- **Application mode**: Returns HW_TYPE (e.g., `0x04` for watchdog)
- **Bootloader mode**: Returns HW_TYPE | 0x80 (e.g., `0x84`)

### POST Codes (Error LED)

When the bootloader cannot boot an application:

| Flashes | Meaning |
|---------|---------|
| 1 | No application firmware |
| 2 | Invalid app header |
| 3 | App code CRC mismatch |

## Based On

This project uses source code from [PicoRVD](https://github.com/aappleby/PicoRVD) by Adam Appleby.

## License

MIT

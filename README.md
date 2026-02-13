# PewPewCH32 Programmer

A standalone CH32V003 microcontroller programmer using Raspberry Pi Pico, based on PicoRVD.

## Purpose

PewPewCH32 is designed for developers and manufacturers who need to:

- **Rapidly program CH32V003 microcontrollers** with different firmware versions
- **Switch between multiple firmware images** without reflashing the programmer
- **Create standalone programming stations** for production environments

### Key Benefits

- **Multi-firmware storage**: Store multiple firmware images in RP2040's 2MB flash
- **No PC required**: Once configured, works as a standalone programmer
- **Explicit addressing**: Each firmware specifies its own flash target address
- **Instant switching**: Select different firmware via button press
- **Visual feedback**: RGB LED for clear programming status

## Hardware Requirements

- **Raspberry Pi Pico** (or compatible RP2040 board)
- **Waveshare Pico Zero** recommended (has onboard WS2812 RGB LED)

### Pin Connections

| Pico Pin | Function | Description |
|----------|----------|-------------|
| GPIO8    | PRG      | Connect to CH32V003 SDI pin (single-wire debug) |
| GPIO16   | WS2812   | RGB LED status indicator (onboard on Waveshare Pico Zero) |
| GPIO0    | Buzzer   | PWM buzzer output (optional) |
| GPIO1    | Trigger  | Programming trigger button (active low, optional) |

## Building

### Prerequisites

- **ARM GCC toolchain** (`arm-none-eabi-gcc`)
- **CMake** 3.13+
- **Git** (for cloning dependencies)
- **xxd** (for firmware conversion)
- **RISC-V toolchain** (`riscv64-unknown-elf-gcc`) for building CH32 firmware

### Quick Start

```bash
git clone https://github.com/your-repo/PewPewCH32.git
cd PewPewCH32
./build.sh
```

The build script automatically:
- Checks for required dependencies
- Clones build dependencies (Pico SDK, picorvd)
- Builds emonio-ext firmware (requires RISC-V toolchain)
- Generates the `.uf2` file ready for flashing

### Build Options

```bash
./build.sh              # Normal build
./build.sh clean        # Remove build directory
./build.sh distclean    # Remove all generated files
./build.sh install      # Build and install to Pico in BOOTSEL mode
```

### Installing Dependencies (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install cmake build-essential git xxd gcc-arm-none-eabi gcc-riscv64-unknown-elf
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

bootloader ../emonio-ext/bin/bootloader.bin 0x0000
blink ../emonio-ext/bin/blink.bin 0x1040
watchdog ../emonio-ext/bin/watchdog.bin 0x1040
```

PewPewCH32 treats each binary as opaque data and flashes it at the specified address. It has no knowledge of the CH32 flash layout — the binary is expected to be self-contained (e.g., APP binaries include their own XAPP header).

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

- CH32V003 SDI pin → Pico GPIO8
- Common ground connection

### 3. Monitor via USB Serial

```bash
screen /dev/ttyACM0 115200
```

### 4. Programming

**BOOTSEL Button Controls:**
- **Short press (<250ms)**: Program selected firmware to CH32V003
- **Long press (≥750ms)**: Cycle through available firmware
- LED shows selected firmware (N blue flashes for index N)

**Serial Selection:**

When the programmer is idle, you can send a single digit (`0`-`9`) over the USB serial console to select and immediately program a firmware entry. The available indices are shown in the menu printed at boot:

```
// Available firmware:
//   [0] WIPE FLASH
//   [1] bootloader
//   [2] blink
```

Sending `1` over serial will select and flash entry `[1]` immediately. Out-of-range digits are rejected with an error message.

## Status Indicators

**WS2812 RGB LED:**
- **Rainbow fade**: Startup animation (3 seconds)
- **Green pulse**: System ready (flash every 3 seconds)
- **Blue flashes**: Firmware selection (count = firmware index)
- **Red solid**: Error state (2 seconds)

## Available Firmware

| Firmware | Address | Description |
|----------|---------|-------------|
| bootloader | 0x0000 | I2C bootloader (flash first) |
| blink | 0x1040 | LED blink (includes app header) |
| watchdog | 0x1040 | I2C watchdog (includes app header) |

**Typical workflow:**
1. Flash **bootloader** at 0x0000 (only needed once per device)
2. Flash application firmware (blink, watchdog) at 0x1040 — binaries include app header, bootloader boots them immediately

## Project Structure

```
PewPewCH32/
├── firmware.txt          # Firmware manifest
├── manifest.cmake        # CMake firmware build system
├── build.sh              # Build script
├── CMakeLists.txt        # Main CMake configuration
├── src/                  # Programmer source code
│   ├── main.cpp
│   ├── StateMachine.cpp  # Programming state machine
│   ├── LedController.cpp # WS2812 LED control
│   └── ...
├── picorvd/              # PicoRVD debug interface (cloned)
├── pico-sdk/             # Raspberry Pi Pico SDK (cloned)
└── build/                # Generated build files
    ├── PewPewCH32.uf2    # Programmer firmware for Pico
    └── src/firmware_*.c  # Generated firmware arrays
```

Firmware binaries are sourced from the sibling `emonio-ext` directory:
```
../emonio-ext/bin/
├── bootloader.bin
├── blink.bin
└── watchdog.bin
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

The app header at 0x1040 is part of the application binary (generated at build time by emonio-ext, not by PewPewCH32):

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

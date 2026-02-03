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
- **Embedded metadata**: Firmware version and load address read directly from binaries
- **Instant switching**: Select different firmware via button press
- **Visual feedback**: RGB LED for clear programming status

## Hardware Requirements

- **Raspberry Pi Pico** (or compatible RP2040 board)
- **Waveshare Pico Zero** recommended (has onboard WS2812 RGB LED)

### Pin Connections

| Pico Pin | Function | Description |
|----------|----------|-------------|
| GPIO9    | PRG      | Connect to CH32V003 SDI pin (single-wire debug) |
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
- Reads embedded metadata from firmware binaries
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
# Format: NAME PATH [LOAD_ADDR]
#
# NAME:      Firmware identifier (displayed in menu)
# PATH:      Relative path to binary file
# LOAD_ADDR: Optional fallback load address (hex) if no embedded metadata

bootloader ../emonio-ext/bootloader/bootloader.bin
blink-bl ../emonio-ext/blink/blink-bl.bin
blink-sa ../emonio-ext/blink/blink-sa.bin
watchdog-bl ../emonio-ext/watchdog/watchdog-bl.bin
watchdog-sa ../emonio-ext/watchdog/watchdog-sa.bin
```

### Embedded Firmware Metadata

Firmware binaries can embed metadata at offset 0x100 (256 bytes from start):

```c
typedef struct __attribute__((packed)) {
  uint32_t magic;          // 0x5458454B "KEXT" (little-endian)
  uint32_t load_addr;      // 0x00000000 (standalone) or 0x00000C80 (bootloader)
  uint8_t  hw_type;        // Hardware type (0=generic, 4=watchdog)
  uint8_t  version_major;
  uint8_t  version_minor;
  uint8_t  flags;          // Bit 0: bootloader-compatible
  char     name[16];       // Null-terminated firmware name
  uint32_t reserved;
} fw_metadata_t;  // 32 bytes
```

When metadata is present (magic == "KEXT"):
- Load address, version, and hw_type are read from the binary
- The `LOAD_ADDR` field in firmware.txt is ignored

When metadata is absent:
- `LOAD_ADDR` from firmware.txt is used (defaults to 0x0)
- Version shows as 0.0

### Standalone vs Bootloader-Compatible Firmware

| Mode | Load Address | Metadata | Use Case |
|------|--------------|----------|----------|
| **Standalone** (-sa) | 0x00000000 | No | Direct flash, full chip |
| **Bootloader-compatible** (-bl) | 0x00000C80 | Yes | Works with I2C bootloader |

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

- CH32V003 SDI pin → Pico GPIO9
- Common ground connection

### 3. Monitor via USB Serial

```bash
screen /dev/ttyACM0 115200
```

### 4. Programming

**BOOTSEL Button Controls:**
- **Short press (<250ms)**: Program selected firmware to CH32V003
- **Long press (≥750ms)**: Cycle through available firmware
- LED shows selected firmware (N+1 blue flashes for index N)

## Status Indicators

**WS2812 RGB LED:**
- **Rainbow fade**: Startup animation (3 seconds)
- **Green pulse**: System ready (flash every 3 seconds)
- **Blue flashes**: Firmware selection (count = firmware index + 1)
- **Red solid**: Error state (2 seconds)

## Available Firmware

| Firmware | Load Addr | Has Metadata | Description |
|----------|-----------|--------------|-------------|
| bootloader | 0x0000 | Yes | I2C bootloader (flash first) |
| blink-bl | 0x0C80 | Yes | LED blink (bootloader-compatible) |
| blink-sa | 0x0000 | No | LED blink (standalone) |
| watchdog-bl | 0x0C80 | Yes | I2C watchdog (bootloader-compatible) |
| watchdog-sa | 0x0000 | No | I2C watchdog (standalone) |

**Typical workflow:**
1. Flash **bootloader** first (only needed once)
2. Flash **-bl** variants for devices with bootloader
3. Use **-sa** variants for devices without bootloader

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
│   ├── fw_metadata.h     # Firmware metadata structures
│   └── ...
├── picorvd/              # PicoRVD debug interface (cloned)
├── pico-sdk/             # Raspberry Pi Pico SDK (cloned)
└── build/                # Generated build files
    ├── PewPewCH32.uf2    # Programmer firmware for Pico
    └── src/firmware_*.c  # Generated firmware arrays
```

Firmware binaries are sourced from the sibling `emonio-ext` directory:
```
../emonio-ext/
├── bootloader/bootloader.bin
├── blink/blink.bin, blink-bl.bin, blink-sa.bin
└── watchdog/watchdog.bin, watchdog-bl.bin, watchdog-sa.bin
```

## I2C Bootloader

The CH32V003 bootloader enables over-the-air updates via I2C.

### Memory Layout (16KB Flash)

```
0x0000 ┌──────────────────┐
       │   Bootloader     │  3KB (fixed, flash via SWIO only)
0x0C00 ├──────────────────┤
       │   Boot State     │  64B (update request flag)
0x0C40 ├──────────────────┤
       │   App Header     │  64B (magic, version, CRC, entry)
0x0C80 ├──────────────────┤
       │   Application    │  ~12.9KB (updatable via I2C)
0x4000 └──────────────────┘
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

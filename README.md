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
- **Embedded metadata**: Firmware version, type, and load address read directly from binaries
- **App header generation**: Automatically generates bootloader app header for APP type firmware
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
# Format: NAME PATH [TYPE]
#
# NAME:  Firmware identifier (displayed in menu)
# PATH:  Relative path to binary file
# TYPE:  BOOT (default) or APP
#        BOOT: Standalone firmware, flash at 0x0000, no app header
#        APP:  Application firmware, flash at 0x1080, write app header at 0x1040

bootloader ../emonio-ext/bootloader/bootloader.bin
blink-bl ../emonio-ext/blink/blink-bl.bin APP
blink-sa ../emonio-ext/blink/blink-sa.bin
watchdog-bl ../emonio-ext/watchdog/watchdog-bl.bin APP
watchdog-sa ../emonio-ext/watchdog/watchdog-sa.bin
```

### Firmware Types

| Type | Load Address | App Header | Use Case |
|------|--------------|------------|----------|
| **BOOT** | 0x00000000 | No | Bootloader or standalone firmware |
| **APP** | 0x00000C80 | Yes (auto-generated) | Application firmware for bootloader |

### Embedded Firmware Metadata

Firmware binaries can embed metadata at offset 0x100 (256 bytes from start):

```c
typedef struct __attribute__((packed)) {
  uint32_t magic;          // 0x5458454B "KEXT" (little-endian)
  uint32_t load_addr;      // 0x00000000 (BOOT) or 0x00000C80 (APP)
  uint8_t  hw_type;        // Hardware type (0=generic, 4=watchdog)
  uint8_t  version_major;
  uint8_t  version_minor;
  uint8_t  flags;          // Bit 0: firmware type (0=BOOT, 1=APP)
  char     name[16];       // Null-terminated firmware name
  uint32_t reserved;
} fw_metadata_t;  // 32 bytes
```

When metadata is present (magic == "KEXT"):
- Type, load address, version, and hw_type are read from the binary
- The `TYPE` field in firmware.txt is ignored

When metadata is absent:
- `TYPE` from firmware.txt determines behavior (defaults to BOOT)
- Version shows as 0.0

### App Header Generation

When flashing **APP** type firmware, PewPewCH32 automatically:
1. Flashes the firmware code at 0x1080
2. Calculates CRC32 of the firmware data
3. Generates a valid app header at 0x1040

This means you can flash bootloader-compatible firmware directly via SWIO without needing I2C updates - the bootloader will recognize and boot the application immediately.

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

| Firmware | Type | Has Metadata | Description |
|----------|------|--------------|-------------|
| bootloader | BOOT | Yes | I2C bootloader (flash first) |
| blink-bl | APP | Yes | LED blink (bootloader-compatible) |
| blink-sa | BOOT | No | LED blink (standalone) |
| watchdog-bl | APP | Yes | I2C watchdog (bootloader-compatible) |
| watchdog-sa | BOOT | No | I2C watchdog (standalone) |

**Typical workflow:**
1. Flash **bootloader** first (only needed once per device)
2. Flash **-bl** (APP) variants - app header is auto-generated, bootloader boots them immediately
3. Use **-sa** (BOOT) variants for devices without bootloader

## Project Structure

```
PewPewCH32/
├── firmware.txt          # Firmware manifest
├── manifest.cmake        # CMake firmware build system
├── build.sh              # Build script
├── CMakeLists.txt        # Main CMake configuration
├── src/                  # Programmer source code
│   ├── main.cpp
│   ├── StateMachine.cpp  # Programming state machine (incl. app header generation)
│   ├── LedController.cpp # WS2812 LED control
│   ├── fw_metadata.h     # Firmware metadata structures
│   ├── crc32.h           # CRC32 for app header generation
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
├── blink/blink-bl.bin, blink-sa.bin
└── watchdog/watchdog-bl.bin, watchdog-sa.bin
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

The app header at 0x1040 is generated by PewPewCH32 when flashing APP type firmware:

```c
typedef struct __attribute__((packed)) {
  uint32_t magic;           // 0x50504158 ("XAPP")
  uint8_t  fw_ver_major;    // From firmware metadata
  uint8_t  fw_ver_minor;
  uint8_t  bl_ver_min;      // Minimum bootloader version (1)
  uint8_t  hw_type;         // From firmware metadata
  uint32_t app_size;        // Firmware size in bytes
  uint32_t app_crc32;       // CRC32 of application code
  uint32_t entry_point;     // Load address (0x1080)
  uint32_t header_crc32;    // CRC32 of header bytes 0-23
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

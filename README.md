# PewPewCH32 Programmer

A standalone CH32V003 microcontroller programmer using Raspberry Pi Pico, based on PicoRVD.

## Purpose & Target Audience

PewPewCH32 is designed for developers and manufacturers who need to:

- **Rapidly program CH32V003 microcontrollers** with different firmware versions
- **Switch between multiple firmware images** without reflashing the programmer
- **Create standalone programming stations** for production environments
- **Distribute programmers to third parties** for field updates or mass production

### Key Benefits

- **Multi-firmware storage**: Store up to 100+ firmware images (RP2040's 2MB flash can hold ~128 max-size CH32V003 firmwares)
- **No PC required**: Once configured, works as a standalone programmer
- **Production-ready**: Give pre-loaded programmers to assembly houses or field technicians
- **Instant switching**: Select different firmware via button press - no reflashing needed
- **Visual feedback**: RGB LED and optional buzzer for clear programming status

### Ideal For

- **Product Development**: Test different firmware versions quickly
- **Manufacturing**: Program devices in production with verified firmware
- **Field Service**: Update deployed devices with multiple firmware options
- **Education**: Teaching environments where students program CH32V003 projects

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

**Note:** GPIO27-29 status LEDs removed - all visual feedback now through WS2812 RGB LED.

## Features

- **Single-wire debug interface** for CH32V003 programming
- **WS2812 RGB LED** status indication:
  - Rainbow animation on startup (3 seconds)
  - Green heartbeat flash every 3 seconds (system ready)
  - Blue flashes for firmware selection (100ms per flash)
  - Red LED for error states (2 seconds)
- **Firmware selection** via BOOTSEL button
- **USB serial console** for monitoring and control
- **Multiple firmware support** with automatic builds
- **Firmware manifest system** for easy firmware management
- **PIO-based WS2812 control** on PIO1 (isolated from programmer on PIO0)

## Building

### Prerequisites

- **ARM GCC toolchain** (`arm-none-eabi-gcc`)
- **CMake** 3.13+
- **Git** (for cloning dependencies)
- **xxd** (for firmware conversion)
- **Optional**: RISC-V toolchain (`gcc-riscv64-unknown-elf`) for firmware builds

### Quick Start

1. **Clone and build** (one command does everything):
   ```bash
   git clone https://github.com/your-repo/PewPewCH32.git
   cd PewPewCH32
   ./build.sh
   ```

The build script automatically:
- Checks for all required dependencies
- Clones build dependencies (Pico SDK, ch32v003fun)
- Clones external firmware repositories from `firmware.txt`
- Builds all firmware (if RISC-V toolchain available)
- Configures and builds the programmer
- Generates the `.uf2` file ready for flashing

### Manual Build Steps (if needed)

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Installing Dependencies

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install cmake build-essential git xxd gcc-arm-none-eabi gcc-riscv64-unknown-elf
```

### Build Options

```bash
./build.sh              # Normal build
./build.sh clean        # Clean build (remove build artifacts first)
./build.sh distclean    # Remove all generated files for git commit
./build.sh install      # Build (if needed) and install to Pico in BOOTSEL mode
```

## Firmware Management

The programmer uses a **firmware manifest system** defined in `firmware.txt` at the project root:

### firmware.txt Format

```
# CH32V003 Firmware Manifest
# Format: NAME SOURCE_DIR BINARY_NAME [GIT_URL [GIT_BRANCH]]

# Built-in example firmware
blink examples/blink blink.bin

# External firmware repositories (cloned)
ext-fw fw fw.bin https://github.com/user/fw.git
```

**Fields:**
- `NAME`: Firmware identifier used in code
- `SOURCE_DIR`: Directory path relative to `firmware/`
- `BINARY_NAME`: Name of the compiled binary file
- `GIT_URL`: Optional git repository URL (cloned during build, not tracked by git)
- `GIT_BRANCH`: Optional branch/tag (defaults to main/master)

### Adding New Firmware

1. Add entry to `firmware.txt` with git URL
2. Run `./build.sh` - repository is cloned and built automatically
3. Firmware is embedded and selectable via BOOTSEL button

**Note:** External firmware repositories are cloned directly. They are listed in `.gitignore` to keep the main repository clean.

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

### 3. Monitor via USB Serial (115200 baud)
```bash
screen /dev/ttyACM0 115200
# or
minicom -D /dev/ttyACM0 -b 115200
```

### 4. Programming

**BOOTSEL Button Controls:**
- **Short press (<250ms)**: Program the currently selected firmware to CH32V003
- **Long press (≥750ms)**: Cycle through available firmware images
- WS2812 LED shows selected firmware (N+1 blue flashes for index N)

**Alternative triggers:**
- External trigger button on GPIO1 (optional)
- Console commands via USB serial

## Status Indicators

**WS2812 RGB LED:**
- **Rainbow fade**: Startup animation (3 seconds)
- **Green pulse**: System ready (100ms flash every 3 seconds, brightness 32/255)
- **Blue flashes**: Firmware selection (100ms flashes, count = firmware index + 1)
- **Red solid**: Error state (2 seconds)

## Available Firmware

The programmer currently includes:

- **bootloader** - I2C bootloader (flash first, enables OTA updates)
- **blink** - Simple LED blink example (bootloader-compatible)
- **watchdog** - I2C multi-channel watchdog timer

Additional firmware can be added by editing `firmware.txt` and running `./build.sh`.

## I2C Bootloader

The CH32V003 firmware supports over-the-air updates via I2C, enabling firmware updates without physical access to the SWIO debug interface.

### Architecture

The bootloader occupies the first 3KB of flash and is never updated OTA. Applications are linked at offset 0x0C80 and can be updated via I2C from a host controller.

**Memory Layout (16KB Flash):**

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

### Boot Flow

1. **Power-on** → Bootloader runs first
2. **Check boot state** (0x0C00): If `UPDATE` flag set → stay in bootloader mode
3. **Validate app**: Check header magic, CRC32 of header and app code
4. **If valid** → Jump to application at entry point (0x0C80)
5. **If invalid** → Stay in bootloader mode, signal via POST codes

### POST Codes (Error LED)

When the bootloader cannot boot an application, the error LED blinks a diagnostic pattern:

| Flashes | Meaning |
|---------|---------|
| 1 | No application firmware (flash erased) |
| 2 | Invalid app header (bad magic or header CRC) |
| 3 | App code CRC mismatch |

Pattern: 150ms on, 150ms off per flash, repeats every 2 seconds.

### I2C Protocol

Both bootloader and application use I2C address **0x42**. The mode is detected via register 0x00:

- **Application mode**: Returns HW_TYPE (e.g., `0x04` for watchdog)
- **Bootloader mode**: Returns HW_TYPE | 0x80 (e.g., `0x84`)

### Application Registers (0xE0-0xE7)

Applications include bootloader client code that handles update requests:

| Register | Name | R/W | Description |
|----------|------|-----|-------------|
| 0xE0 | APP_BL_VERSION | R | Bootloader protocol version |
| 0xE1 | APP_UPDATE_CMD | W | Write 0xAA to enter bootloader mode |
| 0xE2-E3 | APP_UPDATE_SIZE | W | Expected firmware size (little-endian) |
| 0xE4-E7 | APP_UPDATE_CRC | W | Expected firmware CRC32 (little-endian) |

### Bootloader Registers (0xF0-0xFF)

Active only in bootloader mode:

| Register | Name | R/W | Description |
|----------|------|-----|-------------|
| 0xF0 | BL_VERSION | R | Bootloader protocol version (currently 1) |
| 0xF1 | BL_STATUS | R | 0=idle, 1=busy, 0x40=success, 0x80+=error |
| 0xF2 | BL_ERROR | R | Last error code |
| 0xF8 | BL_CMD | W | Command byte (see below) |
| 0xF9 | BL_ADDR_L | W | Page address low byte |
| 0xFA | BL_ADDR_H | W | Page address high byte |
| 0xFB | BL_DATA | W | 64-byte page data buffer |
| 0xFC-FF | BL_CRC | R/W | Expected CRC32 (4 bytes, little-endian) |

**Commands (write to 0xF8):**

| Value | Command | Description |
|-------|---------|-------------|
| 0x01 | ERASE | Erase application area |
| 0x02 | WRITE | Write 64-byte page at BL_ADDR |
| 0x03 | VERIFY | Verify app CRC matches BL_CRC |
| 0x04 | BOOT | Boot application |

### Firmware Update Sequence

From the host's perspective:

```
1. Read register 0x00 → verify app mode (value < 0x80)
2. Write expected size to 0xE2-0xE3 (little-endian)
3. Write expected CRC32 to 0xE4-0xE7 (little-endian)
4. Write 0xAA to 0xE1 → device resets into bootloader
5. Wait 100ms, read 0x00 → verify bootloader mode (value >= 0x80)
6. Read 0xF0 → check bootloader protocol version
7. Write 0x01 to 0xF8 (erase), poll 0xF1 until idle
8. For each 64-byte page:
   a. Write page address to 0xF9-0xFA
   b. Write 64 bytes to 0xFB
   c. Write 0x02 to 0xF8 (write page)
   d. Poll 0xF1 until idle (status 0x00)
9. Write expected CRC32 to 0xFC-0xFF
10. Write 0x03 to 0xF8 (verify), poll until idle
11. If status 0x40 (success): Write 0x04 to 0xF8 (boot)
12. Read 0x00 → verify back in app mode
```

### App Header Format

The 64-byte application header at 0x0C40:

```c
typedef struct {
  uint32_t magic;           // 0x454D4F57 ("WOME" little-endian)
  uint8_t  fw_ver_major;    // Firmware major version
  uint8_t  fw_ver_minor;    // Firmware minor version
  uint8_t  bl_ver_min;      // Minimum bootloader version required
  uint8_t  hw_type;         // Hardware type (0=generic, 4=watchdog)
  uint32_t app_size;        // Application code size in bytes
  uint32_t app_crc32;       // CRC32 of application code
  uint32_t entry_point;     // Entry address (normally 0x0C80)
  uint32_t header_crc32;    // CRC32 of header bytes 0-23
  uint8_t  reserved[40];    // Padding to 64 bytes (0xFF)
} app_header_t;
```

### Build Outputs

Each bootloader-compatible firmware produces two files:

- **`.bin`** - Raw binary for SWIO flashing (no header)
- **`.upd`** - Update file with 64-byte header prepended (for I2C updates)

The `.upd` file is generated by `bootloader/tools/mkupd.py`:

```bash
python3 mkupd.py firmware.bin firmware.upd --major 1 --minor 0 --hw-type 4
```

### Initial Setup

To enable I2C updates on a new CH32V003:

1. Flash the bootloader via SWIO (once, never updated OTA)
2. Flash the application via SWIO, or update via I2C

```bash
# Flash bootloader
cd firmware/bootloader && make flash

# Flash application (either method)
cd firmware/emonio-wd && make flash        # Via SWIO
# OR send .upd file via I2C from host      # Via bootloader
```

## Project Structure

```
PewPewCH32/
├── firmware.txt              # Firmware manifest (defines available firmware)
├── build.sh                  # Automated build script
├── CMakeLists.txt            # Main CMake configuration
├── src/                      # Programmer source code
│   ├── main.cpp
│   ├── Console.cpp
│   ├── GDBServer.cpp
│   ├── PicoSWIO.cpp
│   ├── RVDebug.cpp
│   ├── SoftBreak.cpp
│   ├── WCHFlash.cpp
│   └── utils.cpp
├── firmware/                 # Firmware directory
│   ├── manifest.cmake        # CMake firmware build system
│   ├── bootloader/           # I2C bootloader (flash once via SWIO)
│   │   ├── src/              # Bootloader source (bl_main, bl_flash, bl_i2c)
│   │   ├── lib/              # Shared library (bl_client, crc32, app.ld)
│   │   ├── linker/           # Bootloader linker script
│   │   └── tools/            # mkupd.py for generating .upd files
│   ├── examples/             # Built-in example firmware
│   │   └── blink/
│   ├── emonio-wd/            # I2C watchdog firmware (cloned)
│   ├── ch32v003fun/          # CH32V003 SDK (cloned)
│   └── ext-fw/               # Example external firmware (cloned, not tracked)
├── pico-sdk/                 # Raspberry Pi Pico SDK (cloned)
└── build/                    # Generated build files
    ├── PewPewCH32.uf2
    └── src/firmware_*.c      # Generated firmware arrays
```

## Development

### Adding Custom Firmware

1. **Local firmware**: Place in `firmware/examples/your-firmware/`
2. **External firmware**: Add git URL to `firmware.txt` (will be cloned but not tracked)
3. **Build requirements**: Must output `.bin` file and use ch32v003fun framework

**For bootloader-compatible firmware:**
- Include `bootloader/lib/` in your build (bl_client.c, crc32.c)
- Use `bootloader/lib/app.ld` as linker script (links at 0x0C80)
- Handle I2C registers 0xE0-0xE7 for update requests
- Call `bl_client_init()` at startup
- Generate `.upd` file using `bootloader/tools/mkupd.py`

### Build System Details

The build system uses:
- **CMake** for main project configuration
- **Custom manifest system** (`firmware/manifest.cmake`) for firmware integration
- **xxd** for binary-to-C-array conversion
- **Git clone** for build dependencies (Pico SDK, ch32v003fun, picorvd)
- **Git clone** for external firmware (not tracked in main repository)

## Based On

This project uses source code from [PicoRVD](https://github.com/aappleby/PicoRVD) by Adam Appleby.

## License

MIT

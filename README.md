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

- **blink** - Simple LED blink example (built-in)

Additional firmware can be added by editing `firmware.txt` and running `./build.sh`.

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
│   ├── examples/             # Built-in example firmware
│   │   └── blink/
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

# PewPewCH32 Programmer

A standalone CH32V003 microcontroller programmer using Raspberry Pi Pico, based on PicoRVD.

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
| GPIO27   | LED_G    | Green status LED (optional) |
| GPIO28   | LED_Y    | Yellow status LED (optional) |
| GPIO29   | LED_R    | Red status LED (optional) |

## Features

- **Single-wire debug interface** for CH32V003 programming
- **WS2812 RGB LED** status indication:
  - Green heartbeat flash (system ready)
  - Blue firmware selection indication
- **Firmware selection** via BOOTSEL button
- **USB serial console** for monitoring and control
- **Multiple firmware support** with automatic builds
- **Firmware manifest system** for easy firmware management

## Building

### Prerequisites

- **ARM GCC toolchain** (`arm-none-eabi-gcc`)
- **CMake** 3.13+
- **Git** (for submodules)
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
- Initializes git submodules (Pico SDK, CH32V003Fun)
- Fetches firmware submodules from `firmware.txt`
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
```

## Firmware Management

The programmer uses a **firmware manifest system** defined in `firmware.txt` at the project root:

### firmware.txt Format

```
# CH32V003 Firmware Manifest
# Format: NAME SOURCE_DIR BINARY_NAME [GIT_URL [GIT_BRANCH]]

# Built-in example firmware
blink examples/blink blink.bin

# External firmware submodules
ext-fw fw fw.bin https://github.com/user/fw.git
```

**Fields:**
- `NAME`: Firmware identifier used in code
- `SOURCE_DIR`: Directory path relative to `firmware/`
- `BINARY_NAME`: Name of the compiled binary file
- `GIT_URL`: Optional git submodule URL (auto-fetched during build)
- `GIT_BRANCH`: Optional branch/tag (defaults to main/master)

### Adding New Firmware

1. Add entry to `firmware.txt` with git URL
2. Run `./build.sh` - submodule is fetched and built automatically
3. Firmware is embedded and selectable via BOOTSEL button

## Flashing and Usage

### 1. Flash to Pico
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
- Press BOOTSEL button to cycle through firmware
- WS2812 LED shows selected firmware (N+1 blue flashes for index N)
- Trigger programming via console commands or external trigger

## Status Indicators

- **Green heartbeat**: System operational, waiting for commands
- **Blue flashes**: Firmware selection (count = firmware index + 1)
- **Red**: Error state
- **Yellow**: Programming in progress

## Available Firmware

The programmer currently includes:

1. **blink** - Simple LED blink example (616 bytes)
2. **watchdog** - I2C watchdog firmware from emonio-wd project (1952 bytes)

Additional firmware can be added by editing `firmware.txt` and running `./build.sh`.

## Project Structure

```
CH32V003_Programmer_Clean/
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
│   ├── ch32v003fun/          # CH32V003 SDK (submodule)
│   └── emonio-wd/            # External firmware (submodule)
├── pico-sdk/                 # Raspberry Pi Pico SDK (submodule)
└── build/                    # Generated build files
    ├── PewPewCH32.uf2
    └── src/firmware_*.c      # Generated firmware arrays
```

## Development

### Adding Custom Firmware

1. **Local firmware**: Place in `firmware/examples/your-firmware/`
2. **External firmware**: Add git URL to `firmware.txt`
3. **Build requirements**: Must output `.bin` file and use ch32v003fun framework

### Build System Details

The build system uses:
- **CMake** for main project configuration
- **Custom manifest system** (`firmware/manifest.cmake`) for firmware integration
- **xxd** for binary-to-C-array conversion
- **Git submodules** for dependency management

## Based On

This project uses source code from [PicoRVD](https://github.com/aappleby/PicoRVD) by Adam Appleby.

## License

MIT

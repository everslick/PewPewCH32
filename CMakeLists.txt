cmake_minimum_required(VERSION 3.13)

# Force ARM toolchain before any project() call
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

# Set PICO_SDK_PATH to local submodule
set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/pico-sdk)

# Set board type to enable WS2812 support
set(PICO_BOARD waveshare_rp2040_zero)

# Include Pico SDK
include(pico_sdk_import.cmake)

project(PewPewCH32 C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# Add main executable
add_executable(PewPewCH32
    src/main.cpp
    # Use picorvd sources from submodule (excluding their main.cpp)
    picorvd/src/Console.cpp
    picorvd/src/GDBServer.cpp
    picorvd/src/Packet.cpp
    picorvd/src/PicoSWIO.cpp
    picorvd/src/RVDebug.cpp
    picorvd/src/SoftBreak.cpp
    picorvd/src/WCHFlash.cpp
    picorvd/src/utils.cpp
    # Include test file from picorvd
    picorvd/test/picorvd_tests.cpp
    # Our additional modules
    src/LedController.cpp
    src/StateMachine.cpp
    src/BuzzerController.cpp
    src/InputHandler.cpp
)

# Include directories
target_include_directories(PewPewCH32 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}            # Include build directory for generated PIO headers
)

# Add picorvd includes AFTER the SDK includes to avoid conflicts with tusb_config.h
target_include_directories(PewPewCH32 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/picorvd/src  # Include picorvd headers
    ${CMAKE_CURRENT_LIST_DIR}/picorvd      # Include picorvd root for test/ includes
)

# Generate PIO headers
# picorvd expects singlewire.pio.h to be in a bin/ subdirectory
pico_generate_pio_header(PewPewCH32 ${CMAKE_CURRENT_LIST_DIR}/picorvd/src/singlewire.pio OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/bin)
pico_generate_pio_header(PewPewCH32 ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)

# Link libraries
target_link_libraries(PewPewCH32
    pico_stdlib
    hardware_gpio
    hardware_pio
    hardware_pwm
    hardware_clocks
)

# Enable USB output, disable UART output
pico_enable_stdio_usb(PewPewCH32 1)
pico_enable_stdio_uart(PewPewCH32 0)

# Include firmware build system
include(firmware/manifest.cmake)

# Build firmware inventory (optional - comment out if no RISC-V toolchain)
if(CMAKE_CROSSCOMPILING AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/firmware/examples/blink/blink.bin)
    build_firmware_inventory(PewPewCH32)
    target_compile_definitions(PewPewCH32 PRIVATE FIRMWARE_INVENTORY_ENABLED)
    message(STATUS "Firmware inventory enabled")
else()
    message(STATUS "Firmware inventory disabled - using fallback firmware only")
endif()

# Create UF2 file
pico_add_extra_outputs(PewPewCH32)

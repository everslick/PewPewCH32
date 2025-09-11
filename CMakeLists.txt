cmake_minimum_required(VERSION 3.13)

# Force ARM toolchain before any project() call
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

# Set PICO_SDK_PATH to local submodule
set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/pico-sdk)

# Include Pico SDK
include(pico_sdk_import.cmake)

project(ch32v003_programmer C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# Add main executable
add_executable(ch32v003_programmer
    src/main.cpp
    src/Console.cpp
    src/GDBServer.cpp
    src/PicoSWIO.cpp
    src/RVDebug.cpp
    src/SoftBreak.cpp
    src/WCHFlash.cpp
    src/utils.cpp
)

# Include directories
target_include_directories(ch32v003_programmer PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

# Generate PIO headers
pico_generate_pio_header(ch32v003_programmer ${CMAKE_CURRENT_LIST_DIR}/src/singlewire.pio)

# Link libraries
target_link_libraries(ch32v003_programmer
    pico_stdlib
    hardware_gpio
    hardware_pio
    hardware_pwm
    hardware_clocks
)

# Enable USB output, disable UART output
pico_enable_stdio_usb(ch32v003_programmer 1)
pico_enable_stdio_uart(ch32v003_programmer 0)

# Include firmware build system
include(firmware/manifest.cmake)

# Build firmware inventory (optional - comment out if no RISC-V toolchain)
if(CMAKE_CROSSCOMPILING AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/firmware/examples/blink/blink.bin)
    build_firmware_inventory(ch32v003_programmer)
    target_compile_definitions(ch32v003_programmer PRIVATE FIRMWARE_INVENTORY_ENABLED)
    message(STATUS "Firmware inventory enabled")
else()
    message(STATUS "Firmware inventory disabled - using fallback firmware only")
endif()

# Create UF2 file
pico_add_extra_outputs(ch32v003_programmer)
#!/bin/bash
# PewPewCH32 Programmer Build Script
# Usage: ./build.sh [clean]

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_header() {
    echo -e "${GREEN}"
    echo "=============================================================================="
    echo " PewPewCH32 Programmer Build Script"
    echo "=============================================================================="
    echo -e "${NC}"
}

# Check if we're in the right directory
check_directory() {
    if [[ ! -f "CMakeLists.txt" ]] || [[ ! -d "src" ]]; then
        print_error "This script must be run from the PewPewCH32 root directory"
        exit 1
    fi
}

# Check for required tools
check_dependencies() {
    print_status "Checking build dependencies..."
    
    local missing_deps=()
    
    # Check for ARM GCC toolchain
    if ! command -v arm-none-eabi-gcc &> /dev/null; then
        missing_deps+=("arm-none-eabi-gcc (ARM cross-compiler toolchain)")
    fi
    
    # Check for CMake
    if ! command -v cmake &> /dev/null; then
        missing_deps+=("cmake (build system)")
    fi
    
    # Check for make
    if ! command -v make &> /dev/null; then
        missing_deps+=("make (build tool)")
    fi
    
    # Check for git (needed for cloning dependencies)
    if ! command -v git &> /dev/null; then
        missing_deps+=("git (version control)")
    fi
    
    # Check for xxd (needed for firmware conversion)
    if ! command -v xxd &> /dev/null; then
        missing_deps+=("xxd (for firmware conversion)")
    fi
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        print_error "Missing required dependencies:"
        for dep in "${missing_deps[@]}"; do
            echo "  - $dep"
        done
        echo
        print_status "To install on Ubuntu/Debian:"
        echo "  sudo apt update"
        echo "  sudo apt install cmake build-essential git xxd gcc-arm-none-eabi"
        exit 1
    fi
    
    print_success "All required build dependencies found"
}

# Clone firmware repositories from firmware.txt
clone_firmware_repos() {
    if [[ -f "firmware.txt" ]]; then
        print_status "Processing firmware.txt for firmware repositories..."
        
        # Read firmware.txt line by line
        while IFS= read -r line || [[ -n "$line" ]]; do
            # Skip comments and empty lines
            if [[ "$line" =~ ^[[:space:]]*# ]] || [[ -z "${line// }" ]]; then
                continue
            fi
            
            # Parse line: NAME SOURCE_DIR BINARY_NAME [GIT_URL [GIT_BRANCH]]
            read -r name source_dir binary_name git_url git_branch <<< "$line"
            
            if [[ -n "$git_url" ]]; then
                local firmware_path="firmware/$source_dir"
                
                # Check if directory already exists
                if [[ -d "$firmware_path/.git" ]]; then
                    print_status "Firmware repository '$name' already cloned"
                    # Pull latest changes
                    print_status "Updating firmware repository '$name'..."
                    cd "$firmware_path"
                    if [[ -n "$git_branch" ]]; then
                        git checkout "$git_branch" > /dev/null 2>&1
                    fi
                    git pull > /dev/null 2>&1
                    cd - > /dev/null
                    print_success "Firmware repository '$name' updated"
                else
                    print_status "Cloning firmware repository '$name' from $git_url"
                    
                    # Remove directory if it exists but isn't a git repo
                    if [[ -d "$firmware_path" ]]; then
                        rm -rf "$firmware_path"
                    fi
                    
                    # Clone the repository
                    if [[ -n "$git_branch" ]]; then
                        git clone -b "$git_branch" "$git_url" "$firmware_path" > /dev/null 2>&1
                    else
                        git clone "$git_url" "$firmware_path" > /dev/null 2>&1
                    fi
                    
                    if [[ -d "$firmware_path/.git" ]]; then
                        print_success "Firmware repository '$name' cloned successfully"
                    else
                        print_warning "Failed to clone firmware repository '$name'"
                    fi
                fi
            fi
        done < "firmware.txt"
    else
        print_warning "firmware.txt not found - skipping firmware repository setup"
    fi
}

# Apply patches to picorvd for SDK compatibility
apply_picorvd_patches() {
    if [[ -d "picorvd" ]]; then
        print_status "Applying picorvd patches for SDK compatibility..."
        
        # Fix SDK compatibility - replace iobank0_hw with io_bank0_hw
        if [[ -f "picorvd/src/PicoSWIO.cpp" ]]; then
            if grep -q "iobank0_hw" picorvd/src/PicoSWIO.cpp; then
                sed -i 's/iobank0_hw/io_bank0_hw/g' picorvd/src/PicoSWIO.cpp
                print_status "Applied SDK compatibility fix: iobank0_hw -> io_bank0_hw"
            fi
        fi
        
        # Disable conflicting tusb_config.h
        if [[ -f "picorvd/src/tusb_config.h" && ! -f "picorvd/src/tusb_config.h.bak" ]]; then
            mv picorvd/src/tusb_config.h picorvd/src/tusb_config.h.bak
            print_status "Disabled conflicting tusb_config.h"
        fi
        
    fi
}

# Initialize dependencies
init_dependencies() {
    print_status "Initializing build dependencies..."
    
    # Clone required repositories if missing
    if [[ ! -d "pico-sdk" ]]; then
        print_status "Cloning Pico SDK..."
        git clone --recursive https://github.com/raspberrypi/pico-sdk.git pico-sdk
    fi
    
    if [[ ! -d "firmware/ch32v003fun" ]]; then
        print_status "Cloning CH32V003 framework..."
        git clone https://github.com/cnlohr/ch32v003fun.git firmware/ch32v003fun
    fi
    
    if [[ ! -d "picorvd" ]]; then
        print_status "Cloning picorvd..."
        git clone https://github.com/aappleby/picorvd.git picorvd
        apply_picorvd_patches
    else
        apply_picorvd_patches
    fi
    
    # Clone firmware repositories from firmware.txt
    clone_firmware_repos
    
    print_success "Build dependencies initialized"
}

# Check for optional RISC-V toolchain
check_riscv_toolchain() {
    print_status "Checking for RISC-V toolchain (optional for firmware builds)..."
    
    if command -v riscv64-unknown-elf-gcc &> /dev/null; then
        print_success "RISC-V toolchain found - firmware builds enabled"
        return 0
    else
        print_warning "RISC-V toolchain not found - using fallback firmware only"
        print_status "To enable firmware builds, install RISC-V toolchain:"
        print_status "  sudo apt install gcc-riscv64-unknown-elf"
        return 1
    fi
}

# Build firmware from firmware.txt manifest
build_firmware_from_manifest() {
    if [[ ! -f "firmware.txt" ]]; then
        print_warning "firmware.txt not found - skipping firmware builds"
        return
    fi
    
    if ! check_riscv_toolchain; then
        return
    fi
    
    print_status "Building firmware from manifest..."
    
    # Read firmware.txt line by line
    while IFS= read -r line || [[ -n "$line" ]]; do
        # Skip comments and empty lines
        if [[ "$line" =~ ^[[:space:]]*# ]] || [[ -z "${line// }" ]]; then
            continue
        fi
        
        # Parse line: NAME SOURCE_DIR BINARY_NAME [GIT_URL [GIT_BRANCH]]
        read -r name source_dir binary_name git_url git_branch <<< "$line"
        
        if [[ -n "$name" && -n "$source_dir" && -n "$binary_name" ]]; then
            local firmware_path="firmware/$source_dir"
            
            if [[ -d "$firmware_path" ]]; then
                print_status "Building firmware '$name' in $source_dir..."
                
                cd "$firmware_path"
                
                # Check if firmware is already built and up to date
                if [[ -f "$binary_name" ]] && find . -name "*.c" -newer "$binary_name" | grep -q .; then
                    print_status "Firmware '$name' needs rebuilding"
                elif [[ -f "$binary_name" ]]; then
                    print_status "Firmware '$name' already up to date"
                    cd - > /dev/null
                    continue
                fi
                
                # Try to build firmware
                print_status "Compiling firmware '$name'..."
                if [[ -f "Makefile" ]]; then
                    # Use existing Makefile, set CH32V003FUN to our dependency
                    if CH32V003FUN=../ch32v003fun make > /dev/null 2>&1; then
                        if [[ -f "$binary_name" ]]; then
                            local size=$(stat -c%s "$binary_name")
                            print_success "Firmware '$name' built successfully (${size} bytes)"
                        else
                            print_warning "Firmware '$name' build completed but binary not found"
                        fi
                    else
                        print_warning "Firmware '$name' build failed, but continuing..."
                    fi
                elif [[ -f "${name}.c" ]]; then
                    # Try to compile single C file firmware
                    if make > /dev/null 2>&1; then
                        # Create binary manually if make didn't work perfectly
                        if [[ ! -f "$binary_name" ]] && [[ -f "${name}.elf" ]]; then
                            riscv64-unknown-elf-objcopy -O binary "${name}.elf" "$binary_name"
                        fi
                        
                        if [[ -f "$binary_name" ]]; then
                            local size=$(stat -c%s "$binary_name")
                            print_success "Firmware '$name' built successfully (${size} bytes)"
                        else
                            print_warning "Firmware '$name' build had issues, but continuing..."
                        fi
                    else
                        print_warning "Firmware '$name' build failed, but continuing..."
                    fi
                else
                    print_warning "No build system found for firmware '$name', skipping..."
                fi
                
                cd - > /dev/null
            else
                print_warning "Firmware directory '$firmware_path' not found, skipping '$name'"
            fi
        fi
    done < "firmware.txt"
}

# Build example firmware if RISC-V toolchain is available
build_firmware() {
    build_firmware_from_manifest
}

# Create build directory and configure
configure_build() {
    print_status "Configuring build..."
    
    # Clean build if requested
    if [[ "$1" == "clean" ]] && [[ -d "build" ]]; then
        print_status "Cleaning previous build..."
        rm -rf build
    fi
    
    # Create build directory
    mkdir -p build
    cd build
    
    # Configure with CMake
    print_status "Running CMake configuration..."
    if cmake .. > cmake_output.log 2>&1; then
        # Check if firmware inventory was enabled
        if grep -q "Firmware inventory enabled" cmake_output.log; then
            print_success "Configuration complete (with firmware inventory)"
        else
            print_success "Configuration complete (fallback firmware only)"
        fi
    else
        print_error "CMake configuration failed"
        print_status "CMake output:"
        cat cmake_output.log
        exit 1
    fi
}

# Build the project
build_project() {
    print_status "Building PewPewCH32 programmer..."
    
    # Determine number of CPU cores for parallel build
    local num_cores=$(nproc 2>/dev/null || echo "4")
    
    print_status "Using $num_cores parallel jobs"
    
    if make -j"$num_cores" > build_output.log 2>&1; then
        print_success "Build completed successfully!"
        
        # Show generated files
        if [[ -f "PewPewCH32.uf2" ]]; then
            local uf2_size=$(stat -c%s "PewPewCH32.uf2")
            print_success "Generated PewPewCH32.uf2 (${uf2_size} bytes)"
        fi
        
        if [[ -f "PewPewCH32.elf" ]]; then
            local elf_size=$(stat -c%s "PewPewCH32.elf")
            print_success "Generated PewPewCH32.elf (${elf_size} bytes)"
        fi
    else
        print_error "Build failed"
        print_status "Build output:"
        tail -50 build_output.log
        exit 1
    fi
}

# Install firmware to Pico in BOOTSEL mode
install_firmware() {
    local mount_path="/media/$USER/RPI-RP2"
    local uf2_file="build/PewPewCH32.uf2"
    
    print_status "Looking for Raspberry Pi Pico in BOOTSEL mode..."
    
    # Check if UF2 file exists
    if [[ ! -f "$uf2_file" ]]; then
        print_error "PewPewCH32.uf2 not found. Please build the project first."
        print_status "Run: $0 (without arguments) to build"
        return 1
    fi
    
    # Check if Pico is mounted
    if [[ ! -d "$mount_path" ]]; then
        print_error "Pico not found at $mount_path"
        print_status "Please:"
        echo "  1. Hold BOOTSEL button on your Pico"
        echo "  2. Connect Pico to USB while holding BOOTSEL"
        echo "  3. Wait for RPI-RP2 drive to appear"
        echo "  4. Run '$0 install' again"
        return 1
    fi
    
    # Check if it's actually a Pico (look for INFO_UF2.TXT)
    if [[ ! -f "$mount_path/INFO_UF2.TXT" ]]; then
        print_error "$mount_path doesn't appear to be a Pico in BOOTSEL mode"
        return 1
    fi
    
    # Copy the UF2 file
    print_status "Installing PewPewCH32 firmware to Pico..."
    if cp "$uf2_file" "$mount_path/"; then
        print_success "Firmware installed successfully!"
        print_status "The Pico will reboot automatically"
        print_status "Monitor via: screen /dev/ttyACM0 115200"
        return 0
    else
        print_error "Failed to copy firmware to Pico"
        return 1
    fi
}

# Show usage instructions
show_usage() {
    echo
    print_success "Build complete! Next steps:"
    echo
    echo "1. Flash to Raspberry Pi Pico:"
    echo "   - Hold BOOTSEL button while connecting Pico to USB"
    echo "   - Copy build/PewPewCH32.uf2 to the RPI-RP2 drive"
    echo
    echo "2. Hardware connections:"
    echo "   - PewPewCH32 SDI pin → Pico GPIO9"
    echo "   - Common ground connection"
    echo
    echo "3. Monitor via USB serial (115200 baud):"
    echo "   screen /dev/ttyACM0 115200"
    echo "   # or"
    echo "   minicom -D /dev/ttyACM0 -b 115200"
    echo
    print_status "Press BOOTSEL button on Pico to cycle through firmware"
    print_status "WS2812 LED shows: Green=ready, Blue flashes=firmware selection"
}

# Main build function
main() {
    print_header
    
    # Handle command line arguments
    local clean_build=""
    if [[ "$1" == "clean" ]]; then
        print_status "Clean requested"
        # Just remove build directory and exit
        if [[ -d "build" ]]; then
            print_status "Removing build/ directory"
            rm -rf build
            print_success "Build directory cleaned!"
        else
            print_status "Build directory doesn't exist, nothing to clean"
        fi
        exit 0
    elif [[ "$1" == "distclean" ]]; then
        print_status "Distribution clean requested - removing all generated files"
        
        # First do a regular clean (remove build directory)
        if [[ -d "build" ]]; then
            print_status "Removing build/ directory"
            rm -rf build
        fi
        
        # Remove firmware binaries
        print_status "Cleaning firmware binaries"
        find firmware/ -name "*.bin" -delete 2>/dev/null || true
        find firmware/ -name "*.elf" -delete 2>/dev/null || true
        find firmware/ -name "*.hex" -delete 2>/dev/null || true
        find firmware/ -name "*.map" -delete 2>/dev/null || true
        find firmware/ -name "*.lst" -delete 2>/dev/null || true
        find firmware/ -name "*.o" -delete 2>/dev/null || true
        find firmware/ -name "temp_*.c" -delete 2>/dev/null || true
        
        # Remove generated linker files from ch32v003fun
        find firmware/ch32v003fun/ -name "generated__.ld" -delete 2>/dev/null || true
        
        # Remove any stray CMake files in root (but preserve our frontend Makefile)
        rm -f CMakeCache.txt cmake_install.cmake 2>/dev/null || true
        rm -rf CMakeFiles/ 2>/dev/null || true
        
        # Clean firmware examples
        if [[ -d "firmware/examples" ]]; then
            find firmware/examples/ -name "*.bin" -delete 2>/dev/null || true
            find firmware/examples/ -name "*.elf" -delete 2>/dev/null || true
            find firmware/examples/ -name "*.hex" -delete 2>/dev/null || true
            find firmware/examples/ -name "*.map" -delete 2>/dev/null || true
            find firmware/examples/ -name "*.lst" -delete 2>/dev/null || true
            find firmware/examples/ -name "*.o" -delete 2>/dev/null || true
        fi
        
        # Remove dependency directories
        print_status "Removing dependency directories"
        
        # Remove pico-sdk
        if [[ -d "pico-sdk" ]]; then
            print_status "Removing pico-sdk"
            rm -rf pico-sdk
        fi
        
        # Remove ch32v003fun
        if [[ -d "firmware/ch32v003fun" ]]; then
            print_status "Removing ch32v003fun"
            rm -rf firmware/ch32v003fun
        fi
        
        # Remove picorvd
        if [[ -d "picorvd" ]]; then
            print_status "Removing picorvd"
            rm -rf picorvd
        fi
        
        
        # Remove any other firmware repositories (check for .git directories)
        if [[ -d "firmware" ]]; then
            for dir in firmware/*/; do
                if [[ -d "$dir" && ("$dir" != "firmware/examples/") ]]; then
                    # Check if it's a git repository (has .git directory)
                    if [[ -d "${dir}.git" ]]; then
                        dirname=$(basename "$dir")
                        print_status "Removing firmware repository: $dirname"
                        rm -rf "$dir"
                    fi
                fi
            done
        fi
        
        print_success "Distribution clean completed!"
        print_status "Repository is now ready for git commit"
        exit 0
    elif [[ "$1" == "install" ]]; then
        # Install firmware to Pico
        if [[ ! -f "build/PewPewCH32.uf2" ]]; then
            print_error "No firmware found to install"
            print_status "Building project first..."
            check_directory
            check_dependencies
            init_dependencies
            build_firmware
            configure_build
            build_project
        fi
        install_firmware
        exit $?
    elif [[ "$1" == "--help" ]] || [[ "$1" == "-h" ]]; then
        echo "PewPewCH32 Programmer Build Script"
        echo
        echo "Usage: $0 [OPTIONS]"
        echo
        echo "OPTIONS:"
        echo "  clean      Remove build directory only"
        echo "  distclean  Remove all generated files for git commit"
        echo "  install    Copy firmware to Pico in BOOTSEL mode"
        echo "  -h, --help Show this help message"
        echo
        echo "This script will:"
        echo "  1. Check for required dependencies"
        echo "  2. Clone build dependencies (Pico SDK, CH32V003 framework, picorvd)"
        echo "  3. Build example firmware (if RISC-V toolchain available)"
        echo "  4. Configure and build the PewPewCH32 programmer"
        echo "  5. Generate .uf2 file ready for flashing to Pico"
        echo
        echo "Requirements:"
        echo "  - cmake, make, git, xxd"
        echo "  - ARM cross-compiler: arm-none-eabi-gcc"
        echo "  - Optional: RISC-V cross-compiler for firmware builds"
        echo
        exit 0
    elif [[ -n "$1" ]]; then
        print_error "Unknown option: $1"
        echo "Usage: $0 [clean|distclean|install]"
        echo "Use $0 --help for more information"
        exit 1
    fi
    
    check_directory
    check_dependencies
    init_dependencies
    build_firmware
    configure_build "$clean_build"
    build_project
    show_usage
    
    cd ..  # Return to project root
    
    print_success "All done! 🚀"
}

# Run main function with all arguments
main "$@"

#!/bin/bash
# CH32V003 Programmer Build Script
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
    echo " CH32V003 Programmer Build Script"
    echo "=============================================================================="
    echo -e "${NC}"
}

# Check if we're in the right directory
check_directory() {
    if [[ ! -f "CMakeLists.txt" ]] || [[ ! -d "src" ]]; then
        print_error "This script must be run from the CH32V003 programmer root directory"
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
    
    # Check for git (needed for submodules)
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
    
    print_success "All required dependencies found"
}

# Fetch firmware submodules from firmware.txt
fetch_firmware_submodules() {
    if [[ -f "firmware.txt" ]]; then
        print_status "Processing firmware.txt for submodules..."
        
        # Read firmware.txt line by line
        while IFS= read -r line || [[ -n "$line" ]]; do
            # Skip comments and empty lines
            if [[ "$line" =~ ^[[:space:]]*# ]] || [[ -z "${line// }" ]]; then
                continue
            fi
            
            # Parse line: NAME SOURCE_DIR BINARY_NAME [GIT_URL [GIT_BRANCH]]
            read -r name source_dir binary_name git_url git_branch <<< "$line"
            
            if [[ -n "$git_url" ]]; then
                local submodule_path="firmware/$source_dir"
                
                # Check if submodule already exists and is checked out
                if [[ -d "$submodule_path/.git" ]] || [[ -f "$submodule_path/.git" ]]; then
                    print_status "Firmware submodule '$name' already initialized"
                elif git submodule status "$submodule_path" &>/dev/null; then
                    # Submodule exists in git but not checked out
                    print_status "Updating existing firmware submodule '$name'"
                    git submodule update --init "$submodule_path"
                    print_success "Firmware submodule '$name' updated successfully"
                else
                    print_status "Adding firmware submodule '$name' from $git_url"
                    
                    # Add git submodule (with --force to handle cached directories)
                    if [[ -n "$git_branch" ]]; then
                        git submodule add --force -b "$git_branch" "$git_url" "$submodule_path"
                    else
                        git submodule add --force "$git_url" "$submodule_path"
                    fi
                    
                    # Initialize the submodule
                    git submodule update --init "$submodule_path"
                    
                    print_success "Firmware submodule '$name' added successfully"
                fi
            fi
        done < "firmware.txt"
    else
        print_warning "firmware.txt not found - skipping firmware submodule setup"
    fi
}

# Initialize git submodules
init_submodules() {
    print_status "Initializing git submodules..."
    
    if [[ ! -d "pico-sdk/.git" ]]; then
        print_status "Initializing Pico SDK submodule..."
        git submodule update --init --recursive pico-sdk
    else
        print_status "Pico SDK submodule already initialized"
    fi
    
    if [[ ! -d "firmware/ch32v003fun/.git" ]]; then
        print_status "Initializing CH32V003 framework submodule..."
        git submodule update --init firmware/ch32v003fun
    else
        print_status "CH32V003 framework submodule already initialized"
    fi
    
    # Fetch firmware submodules from firmware.txt
    fetch_firmware_submodules
    
    print_success "Git submodules initialized"
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
                    # Use existing Makefile, set CH32V003FUN to our submodule
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
    print_status "Building CH32V003 programmer..."
    
    # Determine number of CPU cores for parallel build
    local num_cores=$(nproc 2>/dev/null || echo "4")
    
    print_status "Using $num_cores parallel jobs"
    
    if make -j"$num_cores" > build_output.log 2>&1; then
        print_success "Build completed successfully!"
        
        # Show generated files
        if [[ -f "ch32v003_programmer.uf2" ]]; then
            local uf2_size=$(stat -c%s "ch32v003_programmer.uf2")
            print_success "Generated ch32v003_programmer.uf2 (${uf2_size} bytes)"
        fi
        
        if [[ -f "ch32v003_programmer.elf" ]]; then
            local elf_size=$(stat -c%s "ch32v003_programmer.elf")
            print_success "Generated ch32v003_programmer.elf (${elf_size} bytes)"
        fi
    else
        print_error "Build failed"
        print_status "Build output:"
        tail -50 build_output.log
        exit 1
    fi
}

# Show usage instructions
show_usage() {
    echo
    print_success "Build complete! Next steps:"
    echo
    echo "1. Flash to Raspberry Pi Pico:"
    echo "   - Hold BOOTSEL button while connecting Pico to USB"
    echo "   - Copy build/ch32v003_programmer.uf2 to the RPI-RP2 drive"
    echo
    echo "2. Hardware connections:"
    echo "   - CH32V003 SDI pin → Pico GPIO9"
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
        clean_build="clean"
        print_status "Clean build requested"
    elif [[ "$1" == "distclean" ]]; then
        print_status "Distribution clean requested - removing all generated files"
        
        # Remove build directory
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
        
        # Remove CMake cache and generated files (but preserve .gitignore patterns)
        rm -f CMakeCache.txt cmake_install.cmake Makefile 2>/dev/null || true
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
        
        # Remove git submodules
        print_status "Removing git submodules"
        
        # Remove pico-sdk submodule
        if [[ -d "pico-sdk" ]]; then
            print_status "Removing pico-sdk submodule"
            rm -rf pico-sdk
        fi
        
        # Remove ch32v003fun submodule
        if [[ -d "firmware/ch32v003fun" ]]; then
            print_status "Removing ch32v003fun submodule"
            rm -rf firmware/ch32v003fun
        fi
        
        # Remove any other firmware submodules (check for .git files/directories)
        if [[ -d "firmware" ]]; then
            for dir in firmware/*/; do
                if [[ -d "$dir" && ("$dir" != "firmware/examples/") ]]; then
                    # Check if it's a submodule (has .git file or directory)
                    if [[ -e "${dir}.git" ]] || [[ -d "${dir}.git" ]]; then
                        dirname=$(basename "$dir")
                        print_status "Removing firmware submodule: $dirname"
                        rm -rf "$dir"
                    fi
                fi
            done
        fi
        
        # Clean up .gitmodules entries (reset to clean state)
        if [[ -f ".gitmodules" ]]; then
            print_status "Resetting .gitmodules file"
            cat > .gitmodules << 'EOF'
[submodule "pico-sdk"]
	path = pico-sdk
	url = https://github.com/raspberrypi/pico-sdk.git
	branch = master
[submodule "firmware/ch32v003fun"]
	path = firmware/ch32v003fun
	url = https://github.com/cnlohr/ch32v003fun.git
	branch = master
EOF
        fi
        
        print_success "Distribution clean completed!"
        print_status "Repository is now ready for git commit"
        exit 0
    elif [[ "$1" == "--help" ]] || [[ "$1" == "-h" ]]; then
        echo "CH32V003 Programmer Build Script"
        echo
        echo "Usage: $0 [OPTIONS]"
        echo
        echo "OPTIONS:"
        echo "  clean      Clean previous build before building"
        echo "  distclean  Remove all generated files for git commit"
        echo "  -h, --help Show this help message"
        echo
        echo "This script will:"
        echo "  1. Check for required dependencies"
        echo "  2. Initialize git submodules (Pico SDK, CH32V003 framework)"
        echo "  3. Build example firmware (if RISC-V toolchain available)"
        echo "  4. Configure and build the CH32V003 programmer"
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
        echo "Usage: $0 [clean|distclean]"
        echo "Use $0 --help for more information"
        exit 1
    fi
    
    check_directory
    check_dependencies
    init_submodules
    build_firmware
    configure_build "$clean_build"
    build_project
    show_usage
    
    cd ..  # Return to project root
    
    print_success "All done! 🚀"
}

# Run main function with all arguments
main "$@"
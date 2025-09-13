# PewPewCH32 Programmer Makefile
# This Makefile provides convenient targets that delegate to build.sh

.PHONY: all clean distclean install help
.DEFAULT_GOAL := all

# Default target - build the project
all:
	@./build.sh

# Clean only the build directory
clean:
	@./build.sh clean

# Full distribution clean (removes all generated files)
distclean:
	@./build.sh distclean

# Install firmware to connected Pico
install:
	@./build.sh install

# Show help information
help:
	@./build.sh --help

# Additional convenience targets
.PHONY: build rebuild flash

# Alias for all
build: all

# Clean and build
rebuild: clean all

# Alias for install
flash: install
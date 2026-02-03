# Bootloader-compatible blink example

TARGET := blink
TARGET_MCU ?= CH32V003

# Firmware version
FW_VERSION_MAJOR = 1
FW_VERSION_MINOR = 0
HW_TYPE = 0  # Generic blink example

# Bootloader lib and tools location
BOOTLOADER_DIR = ../../bootloader
BOOTLOADER_LIB = $(BOOTLOADER_DIR)/lib
MKUPD = $(BOOTLOADER_DIR)/tools/mkupd.py

# Additional source files
ADDITIONAL_C_FILES := $(BOOTLOADER_LIB)/bl_client.c $(BOOTLOADER_LIB)/crc32.c

# Include paths
EXTRA_CFLAGS = -I$(BOOTLOADER_LIB)

# Use bootloader-compatible linker script
LINKER_SCRIPT = $(BOOTLOADER_LIB)/app.ld

# Override linker script before including ch32fun.mk
GENERATED_LD_FILE = $(LINKER_SCRIPT)

# Include ch32v003fun build system
include ../../ch32v003fun/ch32fun/ch32fun.mk

all: $(TARGET).bin $(TARGET).upd
	@echo ""
	@echo "Build complete (bootloader-compatible):"
	@$(PREFIX)-size $(TARGET).elf
	@echo ""
	@echo "Output files:"
	@echo "  $(TARGET).bin - Flash via SWIO programmer"
	@echo "  $(TARGET).upd - I2C bootloader update file"

# Generate update file with app header
$(TARGET).upd: $(TARGET).bin
	@python3 $(MKUPD) $< $@ \
		--major $(FW_VERSION_MAJOR) \
		--minor $(FW_VERSION_MINOR) \
		--hw-type $(HW_TYPE)

flash: cv_flash

clean: cv_clean
	@rm -f $(TARGET).upd

.PHONY: all clean flash

# CH32V003 I2C Bootloader Makefile

TARGET = bootloader
TARGET_MCU = CH32V003

# Source files (bl_main.c is included via bootloader.c wrapper)
ADDITIONAL_C_FILES := src/bl_flash.c src/bl_i2c.c lib/crc32.c

# Include paths
EXTRA_CFLAGS = -Isrc -Ilib

# Custom linker script (3KB bootloader area)
LINKER_SCRIPT = linker/bootloader.ld

# CH32V003fun location
CH32V003FUN ?= ../ch32v003fun/

# Override linker script before including ch32fun.mk
GENERATED_LD_FILE = $(LINKER_SCRIPT)

# Include ch32v003fun build system (sets up compiler, flags, etc.)
include $(CH32V003FUN)/ch32fun/ch32fun.mk

# Build targets
all: $(TARGET).bin $(TARGET).hex
	@echo ""
	@echo "Bootloader build complete:"
	@$(PREFIX)-size $(TARGET).elf
	@echo ""
	@echo "Memory usage (must fit in 3KB = 3072 bytes):"
	@$(PREFIX)-size $(TARGET).elf | tail -1 | awk '{print "  Text: " $$1 " bytes, Data: " $$2 " bytes, Total: " $$1+$$2 " bytes"}'

# Flash bootloader (via SWIO programmer)
flash: cv_flash

# Clean
clean: cv_clean
	rm -f src/*.o lib/*.o

.PHONY: all clean flash

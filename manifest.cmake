# CH32V003 Firmware Manifest
# This file reads firmware definitions from firmware.txt and generates firmware inventory
# Supports embedded fw_metadata_t in binaries for version/type info

# Set the firmware base directory (project root)
set(FIRMWARE_BASE_DIR ${CMAKE_CURRENT_LIST_DIR})

set(FIRMWARE_LIST "")
set(FIRMWARE_SOURCES "")

# Metadata constants (must match fw_metadata.h)
set(FW_METADATA_MAGIC "0x5458454B")  # "KEXT" in little-endian
set(FW_METADATA_OFFSET 256)          # 0x100 bytes from start (after vector table)

# Firmware types
set(FW_TYPE_BOOT 0)
set(FW_TYPE_APP 1)

# Load addresses (derived from type)
set(FW_LOAD_ADDR_BOOT "0x00000000")
set(FW_LOAD_ADDR_APP "0x00000C80")

# Function to read a little-endian uint32 from file at offset
function(read_le32 FILE_PATH OFFSET OUT_VAR)
    file(READ "${FILE_PATH}" FILE_BYTES OFFSET ${OFFSET} LIMIT 4 HEX)
    if(FILE_BYTES)
        # Convert from little-endian hex string to value
        string(SUBSTRING "${FILE_BYTES}" 0 2 B0)
        string(SUBSTRING "${FILE_BYTES}" 2 2 B1)
        string(SUBSTRING "${FILE_BYTES}" 4 2 B2)
        string(SUBSTRING "${FILE_BYTES}" 6 2 B3)
        # Reassemble as big-endian for CMake hex parsing
        set(${OUT_VAR} "0x${B3}${B2}${B1}${B0}" PARENT_SCOPE)
    else()
        set(${OUT_VAR} "0" PARENT_SCOPE)
    endif()
endfunction()

# Function to read a byte from file at offset
function(read_byte FILE_PATH OFFSET OUT_VAR)
    file(READ "${FILE_PATH}" FILE_BYTES OFFSET ${OFFSET} LIMIT 1 HEX)
    if(FILE_BYTES)
        math(EXPR VAL "0x${FILE_BYTES}")
        set(${OUT_VAR} ${VAL} PARENT_SCOPE)
    else()
        set(${OUT_VAR} "0" PARENT_SCOPE)
    endif()
endfunction()

# Function to read null-terminated string from file at offset (max 16 chars)
function(read_string16 FILE_PATH OFFSET OUT_VAR)
    # Read as hex to handle null bytes properly
    file(READ "${FILE_PATH}" FILE_HEX OFFSET ${OFFSET} LIMIT 16 HEX)
    if(FILE_HEX)
        # Convert hex pairs to characters, stop at null (00)
        set(RESULT "")
        string(LENGTH "${FILE_HEX}" HEX_LEN)
        math(EXPR MAX_CHARS "${HEX_LEN} / 2")
        foreach(I RANGE 0 ${MAX_CHARS})
            math(EXPR POS "${I} * 2")
            if(POS LESS HEX_LEN)
                string(SUBSTRING "${FILE_HEX}" ${POS} 2 HEX_BYTE)
                if("${HEX_BYTE}" STREQUAL "00")
                    break()
                endif()
                # Convert hex to decimal then to ASCII
                math(EXPR CHAR_CODE "0x${HEX_BYTE}")
                if(CHAR_CODE GREATER 31 AND CHAR_CODE LESS 127)
                    string(ASCII ${CHAR_CODE} CHAR)
                    string(APPEND RESULT "${CHAR}")
                endif()
            endif()
        endforeach()
        set(${OUT_VAR} "${RESULT}" PARENT_SCOPE)
    else()
        set(${OUT_VAR} "" PARENT_SCOPE)
    endif()
endfunction()

# Function to extract metadata from binary file
function(extract_fw_metadata BINARY_PATH OUT_PREFIX)
    # Check file size
    file(SIZE "${BINARY_PATH}" FILE_SIZE)
    math(EXPR MIN_SIZE "${FW_METADATA_OFFSET} + 32")  # metadata struct is 32 bytes

    if(FILE_SIZE LESS MIN_SIZE)
        set(${OUT_PREFIX}_HAS_METADATA FALSE PARENT_SCOPE)
        return()
    endif()

    # Read magic at offset 0x100
    read_le32("${BINARY_PATH}" ${FW_METADATA_OFFSET} MAGIC)

    # Compare case-insensitively (CMake reads hex as lowercase)
    string(TOUPPER "${MAGIC}" MAGIC_UPPER)
    string(TOUPPER "${FW_METADATA_MAGIC}" EXPECTED_UPPER)
    if(NOT "${MAGIC_UPPER}" STREQUAL "${EXPECTED_UPPER}")
        set(${OUT_PREFIX}_HAS_METADATA FALSE PARENT_SCOPE)
        return()
    endif()

    # Valid metadata found - extract fields
    set(${OUT_PREFIX}_HAS_METADATA TRUE PARENT_SCOPE)

    # load_addr at offset +4
    math(EXPR OFFSET "${FW_METADATA_OFFSET} + 4")
    read_le32("${BINARY_PATH}" ${OFFSET} LOAD_ADDR)
    set(${OUT_PREFIX}_LOAD_ADDR "${LOAD_ADDR}" PARENT_SCOPE)

    # hw_type at offset +8
    math(EXPR OFFSET "${FW_METADATA_OFFSET} + 8")
    read_byte("${BINARY_PATH}" ${OFFSET} HW_TYPE)
    set(${OUT_PREFIX}_HW_TYPE ${HW_TYPE} PARENT_SCOPE)

    # version_major at offset +9
    math(EXPR OFFSET "${FW_METADATA_OFFSET} + 9")
    read_byte("${BINARY_PATH}" ${OFFSET} VER_MAJOR)
    set(${OUT_PREFIX}_VERSION_MAJOR ${VER_MAJOR} PARENT_SCOPE)

    # version_minor at offset +10
    math(EXPR OFFSET "${FW_METADATA_OFFSET} + 10")
    read_byte("${BINARY_PATH}" ${OFFSET} VER_MINOR)
    set(${OUT_PREFIX}_VERSION_MINOR ${VER_MINOR} PARENT_SCOPE)

    # flags at offset +11 (contains fw_type in bit 0)
    math(EXPR OFFSET "${FW_METADATA_OFFSET} + 11")
    read_byte("${BINARY_PATH}" ${OFFSET} FLAGS)
    set(${OUT_PREFIX}_FLAGS ${FLAGS} PARENT_SCOPE)

    # Extract fw_type from flags (bit 0)
    math(EXPR FW_TYPE_VAL "${FLAGS} & 1")
    set(${OUT_PREFIX}_FW_TYPE ${FW_TYPE_VAL} PARENT_SCOPE)

    # name at offset +12 (16 bytes)
    math(EXPR OFFSET "${FW_METADATA_OFFSET} + 12")
    read_string16("${BINARY_PATH}" ${OFFSET} NAME)
    set(${OUT_PREFIX}_NAME "${NAME}" PARENT_SCOPE)
endfunction()

# Function to add a firmware to the build
function(add_firmware NAME BINARY_PATH FALLBACK_TYPE)
    # Add to firmware list
    list(APPEND FIRMWARE_LIST ${NAME})

    # Make path absolute if relative
    if(NOT IS_ABSOLUTE "${BINARY_PATH}")
        set(BINARY_PATH ${FIRMWARE_BASE_DIR}/${BINARY_PATH})
    endif()

    # Try to extract embedded metadata
    extract_fw_metadata("${BINARY_PATH}" META)

    if(META_HAS_METADATA)
        # Use metadata values
        set(LOAD_ADDR "${META_LOAD_ADDR}")
        set(HW_TYPE ${META_HW_TYPE})
        set(VERSION_MAJOR ${META_VERSION_MAJOR})
        set(VERSION_MINOR ${META_VERSION_MINOR})
        set(FW_TYPE ${META_FW_TYPE})
        set(META_NAME "${META_NAME}")
        set(HAS_METADATA TRUE)
        if(FW_TYPE EQUAL 1)
            set(TYPE_STR "APP")
        else()
            set(TYPE_STR "BOOT")
        endif()
        message(STATUS "Added firmware: ${NAME} with metadata (${META_NAME} v${VERSION_MAJOR}.${VERSION_MINOR} ${TYPE_STR} @ ${LOAD_ADDR})")
    else()
        # Fall back to firmware.txt values
        if("${FALLBACK_TYPE}" STREQUAL "APP")
            set(FW_TYPE ${FW_TYPE_APP})
            set(LOAD_ADDR "${FW_LOAD_ADDR_APP}")
        else()
            set(FW_TYPE ${FW_TYPE_BOOT})
            set(LOAD_ADDR "${FW_LOAD_ADDR_BOOT}")
        endif()
        set(HW_TYPE 0)
        set(VERSION_MAJOR 0)
        set(VERSION_MINOR 0)
        set(META_NAME "")
        set(HAS_METADATA FALSE)
        message(STATUS "Added firmware: ${NAME} (${BINARY_PATH} @ ${LOAD_ADDR} ${FALLBACK_TYPE}) - no metadata")
    endif()

    # Set variables for this firmware
    set(FIRMWARE_${NAME}_BINARY_PATH ${BINARY_PATH} CACHE INTERNAL "")
    set(FIRMWARE_${NAME}_LOAD_ADDR ${LOAD_ADDR} CACHE INTERNAL "")
    set(FIRMWARE_${NAME}_HW_TYPE ${HW_TYPE} CACHE INTERNAL "")
    set(FIRMWARE_${NAME}_VERSION_MAJOR ${VERSION_MAJOR} CACHE INTERNAL "")
    set(FIRMWARE_${NAME}_VERSION_MINOR ${VERSION_MINOR} CACHE INTERNAL "")
    set(FIRMWARE_${NAME}_FW_TYPE ${FW_TYPE} CACHE INTERNAL "")
    set(FIRMWARE_${NAME}_HAS_METADATA ${HAS_METADATA} CACHE INTERNAL "")
    set(FIRMWARE_${NAME}_META_NAME "${META_NAME}" CACHE INTERNAL "")

    # Update parent scope
    set(FIRMWARE_LIST ${FIRMWARE_LIST} PARENT_SCOPE)
endfunction()

# Function to parse firmware.txt
function(load_firmware_manifest)
    set(MANIFEST_FILE ${FIRMWARE_BASE_DIR}/firmware.txt)

    if(NOT EXISTS ${MANIFEST_FILE})
        message(WARNING "firmware.txt not found, using empty firmware list")
        return()
    endif()

    file(STRINGS ${MANIFEST_FILE} FIRMWARE_LINES)

    foreach(LINE ${FIRMWARE_LINES})
        # Skip comments and empty lines
        string(REGEX MATCH "^[ \t]*#" IS_COMMENT ${LINE})
        string(REGEX MATCH "^[ \t]*$" IS_EMPTY ${LINE})

        if(NOT IS_COMMENT AND NOT IS_EMPTY)
            # Parse the line: NAME PATH [TYPE]
            string(REGEX REPLACE "[ \t]+" ";" LINE_PARTS ${LINE})
            list(LENGTH LINE_PARTS NUM_PARTS)

            if(NUM_PARTS GREATER_EQUAL 2)
                list(GET LINE_PARTS 0 FW_NAME)
                list(GET LINE_PARTS 1 FW_PATH)

                # Optional 3rd field is TYPE (BOOT or APP, defaults to BOOT)
                set(FW_TYPE "BOOT")
                if(NUM_PARTS GREATER_EQUAL 3)
                    list(GET LINE_PARTS 2 FIELD3)
                    if("${FIELD3}" STREQUAL "APP")
                        set(FW_TYPE "APP")
                    endif()
                endif()

                # Check if the binary exists
                set(BINARY_PATH ${FIRMWARE_BASE_DIR}/${FW_PATH})
                if(EXISTS ${BINARY_PATH})
                    add_firmware(${FW_NAME} ${FW_PATH} "${FW_TYPE}")
                else()
                    message(WARNING "Firmware binary not found: ${BINARY_PATH}")
                endif()

            else()
                message(WARNING "Invalid firmware.txt line (need at least 2 parts): ${LINE}")
            endif()
        endif()
    endforeach()

    # Update parent scope with the firmware list
    set(FIRMWARE_LIST ${FIRMWARE_LIST} PARENT_SCOPE)
endfunction()

# Function to build all firmware and generate C arrays
function(build_firmware_inventory TARGET_NAME)
    set(GENERATED_SOURCES "")

    foreach(FIRMWARE ${FIRMWARE_LIST})
        set(BINARY_PATH ${FIRMWARE_${FIRMWARE}_BINARY_PATH})

        # Get filename from path
        get_filename_component(BINARY_NAME ${BINARY_PATH} NAME)

        # Sanitize firmware name for C identifier
        string(REGEX REPLACE "[^a-zA-Z0-9_]" "_" FIRMWARE_SAFE ${FIRMWARE})
        set(ARRAY_NAME firmware_${FIRMWARE_SAFE}_bin)
        set(OUTPUT_BASE ${CMAKE_CURRENT_BINARY_DIR}/src/firmware_${FIRMWARE_SAFE})

        # Generate C array from binary using xxd
        string(REGEX REPLACE "\\.bin$" "" BINARY_BASE ${BINARY_NAME})
        string(REGEX REPLACE "[^a-zA-Z0-9_]" "_" XXD_ARRAY_NAME ${BINARY_BASE})
        set(XXD_ARRAY_NAME "${XXD_ARRAY_NAME}_bin")

        # Get file size for the length variable
        file(SIZE ${BINARY_PATH} BINARY_SIZE)

        add_custom_command(
            OUTPUT ${OUTPUT_BASE}.h ${OUTPUT_BASE}.c
            COMMAND ${CMAKE_COMMAND} -E echo "// Generated from ${BINARY_NAME}" > ${OUTPUT_BASE}.c
            COMMAND ${CMAKE_COMMAND} -E echo "const unsigned char ${ARRAY_NAME}[] = {" >> ${OUTPUT_BASE}.c
            COMMAND xxd -i < ${BINARY_PATH} >> ${OUTPUT_BASE}.c
            COMMAND ${CMAKE_COMMAND} -E echo "}\;" >> ${OUTPUT_BASE}.c
            COMMAND ${CMAKE_COMMAND} -E echo "const unsigned int ${ARRAY_NAME}_len = ${BINARY_SIZE}\;" >> ${OUTPUT_BASE}.c
            COMMAND ${CMAKE_COMMAND} -E echo "// Generated from ${BINARY_NAME}" > ${OUTPUT_BASE}.h
            COMMAND ${CMAKE_COMMAND} -E echo "#pragma once" >> ${OUTPUT_BASE}.h
            COMMAND ${CMAKE_COMMAND} -E echo "extern const unsigned char ${ARRAY_NAME}[]\;" >> ${OUTPUT_BASE}.h
            COMMAND ${CMAKE_COMMAND} -E echo "extern const unsigned int ${ARRAY_NAME}_len\;" >> ${OUTPUT_BASE}.h
            DEPENDS ${BINARY_PATH}
            COMMENT "Converting ${FIRMWARE} binary to C array"
        )

        list(APPEND GENERATED_SOURCES ${OUTPUT_BASE}.c)
    endforeach()

    # Generate inventory header and source
    set(INVENTORY_HEADER ${CMAKE_CURRENT_BINARY_DIR}/src/firmware_inventory.h)
    set(INVENTORY_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/src/firmware_inventory.c)

    set(HEADER_CONTENT "// Generated firmware inventory\\n#pragma once\\n\\n#include <stdint.h>\\n#include <stdbool.h>\\n\\n")
    set(SOURCE_CONTENT "// Generated firmware inventory\\n#include \"firmware_inventory.h\"\\n\\n")

    foreach(FIRMWARE ${FIRMWARE_LIST})
        string(REGEX REPLACE "[^a-zA-Z0-9_]" "_" FIRMWARE_SAFE ${FIRMWARE})
        set(HEADER_CONTENT "${HEADER_CONTENT}extern const unsigned char firmware_${FIRMWARE_SAFE}_bin[];\\nextern const unsigned int firmware_${FIRMWARE_SAFE}_bin_len;\\n\\n")
        set(SOURCE_CONTENT "${SOURCE_CONTENT}#include \"firmware_${FIRMWARE_SAFE}.h\"\\n")
    endforeach()

    # Firmware type enum
    set(HEADER_CONTENT "${HEADER_CONTENT}// Firmware types\\n#define FW_TYPE_BOOT 0  // Standalone: flash at load_addr, no app header\\n#define FW_TYPE_APP  1  // Application: flash at load_addr, write app header\\n\\n")

    set(HEADER_CONTENT "${HEADER_CONTENT}typedef struct {\\n    const char* name;\\n    const unsigned char* data;\\n    unsigned int size;\\n    uint32_t load_addr;\\n    uint8_t  hw_type;\\n    uint8_t  version_major;\\n    uint8_t  version_minor;\\n    uint8_t  fw_type;       // FW_TYPE_BOOT or FW_TYPE_APP\\n    bool     has_metadata;\\n} firmware_info_t;\\n\\nextern const firmware_info_t firmware_list[];\\nextern const int firmware_count;\\n")

    set(SOURCE_CONTENT "${SOURCE_CONTENT}\\nconst firmware_info_t firmware_list[] = {\\n")
    foreach(FIRMWARE ${FIRMWARE_LIST})
        string(REGEX REPLACE "[^a-zA-Z0-9_]" "_" FIRMWARE_SAFE ${FIRMWARE})
        file(SIZE ${FIRMWARE_${FIRMWARE}_BINARY_PATH} FIRMWARE_FILE_SIZE)
        if(${FIRMWARE_${FIRMWARE}_HAS_METADATA})
            set(HAS_META_STR "true")
        else()
            set(HAS_META_STR "false")
        endif()
        set(SOURCE_CONTENT "${SOURCE_CONTENT}    {\"${FIRMWARE}\", firmware_${FIRMWARE_SAFE}_bin, ${FIRMWARE_FILE_SIZE}, ${FIRMWARE_${FIRMWARE}_LOAD_ADDR}, ${FIRMWARE_${FIRMWARE}_HW_TYPE}, ${FIRMWARE_${FIRMWARE}_VERSION_MAJOR}, ${FIRMWARE_${FIRMWARE}_VERSION_MINOR}, ${FIRMWARE_${FIRMWARE}_FW_TYPE}, ${HAS_META_STR}},\\n")
    endforeach()
    set(SOURCE_CONTENT "${SOURCE_CONTENT}};\\n\\nconst int firmware_count = sizeof(firmware_list) / sizeof(firmware_list[0]);\\n")

    string(REPLACE "\\n" "\n" HEADER_CONTENT_FORMATTED "${HEADER_CONTENT}")
    string(REPLACE "\\n" "\n" SOURCE_CONTENT_FORMATTED "${SOURCE_CONTENT}")
    file(WRITE ${INVENTORY_HEADER} "${HEADER_CONTENT_FORMATTED}")
    file(WRITE ${INVENTORY_SOURCE} "${SOURCE_CONTENT_FORMATTED}")

    list(APPEND GENERATED_SOURCES ${INVENTORY_SOURCE})
    target_sources(${TARGET_NAME} PRIVATE ${GENERATED_SOURCES})
    target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/src)

    message(STATUS "Configured firmware inventory with ${CMAKE_LIST_LENGTH} firmware images")
endfunction()

# Load firmware definitions
load_firmware_manifest()

message(STATUS "Firmware manifest loaded")

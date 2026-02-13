# CH32V003 Firmware Manifest
# Reads firmware definitions from firmware.txt (NAME PATH ADDRESS) and generates
# a C firmware inventory.

# Set the firmware base directory (project root)
set(FIRMWARE_BASE_DIR ${CMAKE_CURRENT_LIST_DIR})

set(FIRMWARE_LIST "")
set(FIRMWARE_SOURCES "")

# Function to add a firmware to the build
function(add_firmware NAME BINARY_PATH LOAD_ADDR)
    # Add to firmware list
    list(APPEND FIRMWARE_LIST ${NAME})

    # Make path absolute if relative
    if(NOT IS_ABSOLUTE "${BINARY_PATH}")
        set(BINARY_PATH ${FIRMWARE_BASE_DIR}/${BINARY_PATH})
    endif()

    message(STATUS "Added firmware: ${NAME} (@ ${LOAD_ADDR})")

    # Set variables for this firmware
    set(FIRMWARE_${NAME}_BINARY_PATH ${BINARY_PATH} CACHE INTERNAL "")
    set(FIRMWARE_${NAME}_LOAD_ADDR ${LOAD_ADDR} CACHE INTERNAL "")

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
            # Parse the line: NAME PATH ADDRESS
            string(REGEX REPLACE "[ \t]+" ";" LINE_PARTS ${LINE})
            list(LENGTH LINE_PARTS NUM_PARTS)

            if(NUM_PARTS GREATER_EQUAL 3)
                list(GET LINE_PARTS 0 FW_NAME)
                list(GET LINE_PARTS 1 FW_PATH)
                list(GET LINE_PARTS 2 FW_ADDR)

                # Check if the binary exists
                set(BINARY_PATH ${FIRMWARE_BASE_DIR}/${FW_PATH})
                if(EXISTS ${BINARY_PATH})
                    add_firmware(${FW_NAME} ${FW_PATH} ${FW_ADDR})
                else()
                    message(WARNING "Firmware binary not found: ${BINARY_PATH}")
                endif()

            else()
                message(WARNING "Invalid firmware.txt line (need NAME PATH ADDRESS): ${LINE}")
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

    set(HEADER_CONTENT "${HEADER_CONTENT}typedef struct {\\n    const char* name;\\n    const unsigned char* data;\\n    unsigned int size;\\n    uint32_t load_addr;\\n} firmware_info_t;\\n\\nextern const firmware_info_t firmware_list[];\\nextern const int firmware_count;\\n")

    set(SOURCE_CONTENT "${SOURCE_CONTENT}\\nconst firmware_info_t firmware_list[] = {\\n")
    foreach(FIRMWARE ${FIRMWARE_LIST})
        string(REGEX REPLACE "[^a-zA-Z0-9_]" "_" FIRMWARE_SAFE ${FIRMWARE})
        file(SIZE ${FIRMWARE_${FIRMWARE}_BINARY_PATH} FIRMWARE_FILE_SIZE)
        set(SOURCE_CONTENT "${SOURCE_CONTENT}    {\"${FIRMWARE}\", firmware_${FIRMWARE_SAFE}_bin, ${FIRMWARE_FILE_SIZE}, ${FIRMWARE_${FIRMWARE}_LOAD_ADDR}},\\n")
    endforeach()
    set(SOURCE_CONTENT "${SOURCE_CONTENT}};\\n\\nconst int firmware_count = sizeof(firmware_list) / sizeof(firmware_list[0]);\\n")

    string(REPLACE "\\n" "\n" HEADER_CONTENT_FORMATTED "${HEADER_CONTENT}")
    string(REPLACE "\\n" "\n" SOURCE_CONTENT_FORMATTED "${SOURCE_CONTENT}")
    file(WRITE ${INVENTORY_HEADER} "${HEADER_CONTENT_FORMATTED}")
    file(WRITE ${INVENTORY_SOURCE} "${SOURCE_CONTENT_FORMATTED}")

    list(APPEND GENERATED_SOURCES ${INVENTORY_SOURCE})
    target_sources(${TARGET_NAME} PRIVATE ${GENERATED_SOURCES})
    target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/src)

    list(LENGTH FIRMWARE_LIST FW_COUNT)
    message(STATUS "Configured firmware inventory with ${FW_COUNT} firmware images")
endfunction()

# Load firmware definitions
load_firmware_manifest()

message(STATUS "Firmware manifest loaded")

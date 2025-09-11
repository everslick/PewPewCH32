# CH32V003 Firmware Manifest
# This file reads firmware definitions from firmware.txt and manages git submodules

# Set the firmware base directory (the directory containing this file)
set(FIRMWARE_BASE_DIR ${CMAKE_CURRENT_LIST_DIR})

set(FIRMWARE_LIST "")
set(FIRMWARE_SOURCES "")

# Function to add a firmware to the build
function(add_firmware NAME SOURCE_DIR BINARY_NAME)
    # Add to firmware list
    list(APPEND FIRMWARE_LIST ${NAME})
    
    # Set variables for this firmware
    set(FIRMWARE_${NAME}_SOURCE_DIR ${SOURCE_DIR} CACHE INTERNAL "")
    set(FIRMWARE_${NAME}_BINARY_NAME ${BINARY_NAME} CACHE INTERNAL "")
    set(FIRMWARE_${NAME}_BINARY_PATH ${FIRMWARE_BASE_DIR}/${SOURCE_DIR}/${BINARY_NAME} CACHE INTERNAL "")
    
    # Update parent scope
    set(FIRMWARE_LIST ${FIRMWARE_LIST} PARENT_SCOPE)
    
    message(STATUS "Added firmware: ${NAME} (${SOURCE_DIR}/${BINARY_NAME})")
endfunction()

# Function to parse firmware.txt and add submodules if needed
function(load_firmware_manifest)
    set(MANIFEST_FILE ${FIRMWARE_BASE_DIR}/../firmware.txt)
    
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
            # Parse the line: NAME SOURCE_DIR BINARY_NAME [GIT_URL [GIT_BRANCH]]
            string(REGEX REPLACE "[ \t]+" ";" LINE_PARTS ${LINE})
            list(LENGTH LINE_PARTS NUM_PARTS)
            
            if(NUM_PARTS GREATER_EQUAL 3)
                list(GET LINE_PARTS 0 FW_NAME)
                list(GET LINE_PARTS 1 FW_SOURCE_DIR)
                list(GET LINE_PARTS 2 FW_BINARY_NAME)
                
                set(SHOULD_ADD_FIRMWARE TRUE)
                
                # Check if git URL is specified
                if(NUM_PARTS GREATER_EQUAL 4)
                    list(GET LINE_PARTS 3 FW_GIT_URL)
                    set(FW_GIT_BRANCH "main")
                    
                    # Check if branch is specified
                    if(NUM_PARTS GREATER_EQUAL 5)
                        list(GET LINE_PARTS 4 FW_GIT_BRANCH)
                    endif()
                    
                    # Check if submodule exists, add if not
                    set(SUBMODULE_PATH ${FIRMWARE_BASE_DIR}/${FW_SOURCE_DIR})
                    if(NOT EXISTS ${SUBMODULE_PATH}/.git)
                        message(STATUS "Adding git submodule: ${FW_NAME} from ${FW_GIT_URL}")
                        execute_process(
                            COMMAND git submodule add -b ${FW_GIT_BRANCH} ${FW_GIT_URL} firmware/${FW_SOURCE_DIR}
                            WORKING_DIRECTORY ${FIRMWARE_BASE_DIR}/..
                            RESULT_VARIABLE SUBMODULE_RESULT
                            OUTPUT_QUIET
                            ERROR_QUIET
                        )
                        
                        if(NOT SUBMODULE_RESULT EQUAL 0)
                            message(WARNING "Failed to add submodule ${FW_NAME}, skipping")
                            set(SHOULD_ADD_FIRMWARE FALSE)
                        endif()
                    else()
                        message(STATUS "Submodule already exists: ${FW_NAME}")
                    endif()
                endif()
                
                # Add the firmware to the build only if successful
                if(SHOULD_ADD_FIRMWARE)
                    add_firmware(${FW_NAME} ${FW_SOURCE_DIR} ${FW_BINARY_NAME})
                endif()
                
            else()
                message(WARNING "Invalid firmware.txt line (need at least 3 parts): ${LINE}")
            endif()
        endif()
    endforeach()
    
    # Update parent scope with the firmware list
    set(FIRMWARE_LIST ${FIRMWARE_LIST} PARENT_SCOPE)
endfunction()

# Function to build all firmware and generate C arrays
function(build_firmware_inventory TARGET_NAME)
    set(GENERATED_SOURCES "")
    
    # Build each firmware
    foreach(FIRMWARE ${FIRMWARE_LIST})
        set(SOURCE_DIR ${FIRMWARE_${FIRMWARE}_SOURCE_DIR})
        set(BINARY_NAME ${FIRMWARE_${FIRMWARE}_BINARY_NAME})
        set(BINARY_PATH ${FIRMWARE_${FIRMWARE}_BINARY_PATH})
        
        # Custom target to build the firmware
        add_custom_target(build_${FIRMWARE}
            COMMAND echo "Firmware ${FIRMWARE} already built"
            COMMENT "Checking firmware: ${FIRMWARE}"
        )
        
        # Generate C array from binary
        set(ARRAY_NAME firmware_${FIRMWARE}_bin)
        set(OUTPUT_BASE ${CMAKE_CURRENT_BINARY_DIR}/src/firmware_${FIRMWARE})
        
        # Extract the base name from BINARY_NAME (remove .bin extension and convert to xxd format)
        string(REGEX REPLACE "\\.bin$" "" BINARY_BASE ${BINARY_NAME})
        string(REGEX REPLACE "[^a-zA-Z0-9_]" "_" XXD_ARRAY_NAME ${BINARY_BASE})
        set(XXD_ARRAY_NAME "${XXD_ARRAY_NAME}_bin")
        
        add_custom_command(
            OUTPUT ${OUTPUT_BASE}.h ${OUTPUT_BASE}.c
            COMMAND cd ${FIRMWARE_BASE_DIR}/${SOURCE_DIR} && xxd -i ${BINARY_NAME} | sed '1i// Generated from ${BINARY_NAME}' | sed 's/unsigned char/const unsigned char/g' | sed 's/unsigned int/const unsigned int/g' | sed 's/${XXD_ARRAY_NAME}/${ARRAY_NAME}/g' > ${OUTPUT_BASE}.c
            COMMAND echo "// Generated from ${BINARY_NAME}" > ${OUTPUT_BASE}.h
            COMMAND echo "#pragma once" >> ${OUTPUT_BASE}.h
            COMMAND echo "extern const unsigned char ${ARRAY_NAME}[];" >> ${OUTPUT_BASE}.h
            COMMAND echo "extern const unsigned int ${ARRAY_NAME}_len;" >> ${OUTPUT_BASE}.h
            DEPENDS build_${FIRMWARE} ${BINARY_PATH}
            COMMENT "Converting ${FIRMWARE} binary to C array using xxd"
        )
        
        # Add generated source to list
        list(APPEND GENERATED_SOURCES ${OUTPUT_BASE}.c)
        
        # Make target depend on firmware build
        add_dependencies(${TARGET_NAME} build_${FIRMWARE})
    endforeach()
    
    # Generate inventory header
    set(INVENTORY_HEADER ${CMAKE_CURRENT_BINARY_DIR}/src/firmware_inventory.h)
    set(INVENTORY_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/src/firmware_inventory.c)
    
    # Create inventory header content
    set(HEADER_CONTENT "// Generated firmware inventory\n")
    set(HEADER_CONTENT "${HEADER_CONTENT}#pragma once\n")
    set(HEADER_CONTENT "${HEADER_CONTENT}\n")
    set(HEADER_CONTENT "${HEADER_CONTENT}#include <stdint.h>\n")
    set(HEADER_CONTENT "${HEADER_CONTENT}\n")
    
    set(SOURCE_CONTENT "// Generated firmware inventory\n")
    set(SOURCE_CONTENT "${SOURCE_CONTENT}#include \"firmware_inventory.h\"\n")
    set(SOURCE_CONTENT "${SOURCE_CONTENT}\n")
    
    foreach(FIRMWARE ${FIRMWARE_LIST})
        set(HEADER_CONTENT "${HEADER_CONTENT}extern const unsigned char firmware_${FIRMWARE}_bin[];\nextern const unsigned int firmware_${FIRMWARE}_bin_len;\n\n")
        set(SOURCE_CONTENT "${SOURCE_CONTENT}#include \"firmware_${FIRMWARE}.h\"\n")
    endforeach()
    
    # Create firmware info structure
    set(HEADER_CONTENT "${HEADER_CONTENT}typedef struct {\n    const char* name;\n    const unsigned char* data;\n    unsigned int size;\n} firmware_info_t;\n\nextern const firmware_info_t firmware_list[];\nextern const int firmware_count;\n")
    
    # Create firmware list array
    set(SOURCE_CONTENT "${SOURCE_CONTENT}\nconst firmware_info_t firmware_list[] = {\n")
    foreach(FIRMWARE ${FIRMWARE_LIST})
        set(SOURCE_CONTENT "${SOURCE_CONTENT}    {\"${FIRMWARE}\", firmware_${FIRMWARE}_bin, 0}, // Size filled at runtime\n")
    endforeach()
    set(SOURCE_CONTENT "${SOURCE_CONTENT}};\n\nconst int firmware_count = sizeof(firmware_list) / sizeof(firmware_list[0]);\n")
    
    # Write inventory files
    string(REPLACE "\\n" "\n" HEADER_CONTENT_FORMATTED "${HEADER_CONTENT}")
    string(REPLACE "\\n" "\n" SOURCE_CONTENT_FORMATTED "${SOURCE_CONTENT}")
    file(WRITE ${INVENTORY_HEADER} "${HEADER_CONTENT_FORMATTED}")
    file(WRITE ${INVENTORY_SOURCE} "${SOURCE_CONTENT_FORMATTED}")
    
    # Add generated sources to target
    list(APPEND GENERATED_SOURCES ${INVENTORY_SOURCE})
    target_sources(${TARGET_NAME} PRIVATE ${GENERATED_SOURCES})
    
    # Add include directory for generated headers
    target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/src)
    
    message(STATUS "Configured firmware inventory with ${CMAKE_LIST_LENGTH} firmware images")
endfunction()

#------------------------------------------------------------------------------
# Load firmware from firmware.txt
#------------------------------------------------------------------------------

# Load firmware definitions from firmware.txt
load_firmware_manifest()

message(STATUS "Firmware manifest loaded with ${CMAKE_LIST_LENGTH} firmware(s)")
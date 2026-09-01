# =============================================================================
# SML reading head - CH552 port
# =============================================================================
#
# Build:
#
#   cmake -S . -B build
#   cmake --build build
#
# After a successful build, build/firmware.bin and build/firmware.hex are
# produced. Run "cmake --build build --target flash" to upload via the WCH
# bootloader (hold BOOT, plug USB, then start the command).
#
# Requirements: sdcc-sdcc, sdcc-packihx, sdcc-makebin, python3 + pyusb (flash).
# =============================================================================

cmake_minimum_required(VERSION 3.20)

# SDCC is not a hosted compiler; skip CMake's compiler tests entirely.
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_C_COMPILER_ID    GNU)         # placeholder; we never invoke it directly
set(CMAKE_SYSTEM_NAME      Generic)
project(smrh_ch552 NONE)
enable_language(C OPTIONAL)

# -----------------------------------------------------------------------------
# MCU / build settings
# -----------------------------------------------------------------------------
set(F_CPU          16000000  CACHE STRING "CH552 system clock in Hz")
set(CODE_SIZE      0x3800    CACHE STRING "Flash code size budget (14KB, CH552 has 16KB)")

set(TARGET_NAME    firmware)

# Match the conventional NDEBUG behavior despite using a custom SDCC pipeline.
# An empty build type is treated as a debug build, as NDEBUG is not defined.
string(TOUPPER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_UPPER)
if(BUILD_TYPE_UPPER MATCHES "^(RELEASE|MINSIZEREL|RELWITHDEBINFO)$")
    set(CDC_DEBUG_BUILD OFF)
    set(XRAM_LOC  0x0000 CACHE STRING "XRAM start address" FORCE)
    set(XRAM_SIZE 0x0400 CACHE STRING "CH552 XRAM size" FORCE)
else()
    set(CDC_DEBUG_BUILD ON)
    # The CDC endpoint DMA buffers occupy XRAM 0x0000..0x00ff.
    set(XRAM_LOC  0x0100 CACHE STRING "XRAM start address" FORCE)
    set(XRAM_SIZE 0x0300 CACHE STRING "XRAM available above USB DMA buffers" FORCE)
endif()

# Tool binaries (Debian/Ubuntu install SDCC with an "sdcc-" prefix)
find_program(SDCC_CC      NAMES sdcc-sdcc      sdcc      REQUIRED)
find_program(SDCC_PACKIHX NAMES sdcc-packihx   packihx   REQUIRED)
find_program(SDCC_MAKEBIN NAMES sdcc-makebin   makebin   REQUIRED)

# -----------------------------------------------------------------------------
# Sources
# -----------------------------------------------------------------------------
set(SOURCES
    src/main.c
    src/modbus_rtu.c
    src/sml.c
    src/systick.c
    src/delay.c
)

if(CDC_DEBUG_BUILD)
    list(APPEND SOURCES
        src/cdc_debug.c
        CH552/libraries/usb/usb_cdc/usb_cdc.c
        CH552/libraries/usb/usb_cdc/usb_descr.c
        CH552/libraries/usb/usb_cdc/usb_handler.c
    )
endif()

# Custom SDCC compile commands do not infer header dependencies.  Track project
# headers explicitly so configuration changes (such as the SML ring size)
# reliably rebuild the affected object files.
file(GLOB PROJECT_HEADERS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/include/*.h")

# -----------------------------------------------------------------------------
# SDCC build pipeline (custom commands)
# -----------------------------------------------------------------------------
# Compiler model notes for SDCC mcs51:
#   --model-small : data/locals in DSEG (fastest, but only 128 byte budget)
#   --stack-auto  : pass function parameters on the stack instead of fixed
#                   per-function DSEG slots. We need this because the modbus
#                   register helpers have 4 parameters each and quickly burn
#                   through the 128-byte DSEG budget otherwise.
set(SDCC_CFLAGS
    -mmcs51 --model-small --stack-auto --int-long-reent --std-c99 --opt-code-size
    -DF_CPU=${F_CPU}UL
    -I${CMAKE_SOURCE_DIR}/include
)
if(CDC_DEBUG_BUILD)
    list(APPEND SDCC_CFLAGS -I${CMAKE_SOURCE_DIR}/CH552/libraries/usb/usb_cdc)
else()
    list(APPEND SDCC_CFLAGS -DNDEBUG)
endif()
set(SDCC_LDFLAGS
    -mmcs51 --model-small --stack-auto --int-long-reent
    --xram-loc  ${XRAM_LOC}
    --xram-size ${XRAM_SIZE}
    --code-size ${CODE_SIZE}
    -l liblonglong       # 64-bit integer math (energy registers)
)
set(REL_FILES)
foreach(src ${SOURCES})
    get_filename_component(name ${src} NAME_WE)
    set(rel ${CMAKE_BINARY_DIR}/${name}.rel)

    add_custom_command(
        OUTPUT  ${rel}
        COMMAND ${SDCC_CC} -c ${SDCC_CFLAGS} ${CMAKE_SOURCE_DIR}/${src} -o ${rel}
        DEPENDS ${CMAKE_SOURCE_DIR}/${src} ${PROJECT_HEADERS}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "CC ${src}"
        VERBATIM
    )
    list(APPEND REL_FILES ${rel})
endforeach()

# Link
set(IHX_FILE ${CMAKE_BINARY_DIR}/${TARGET_NAME}.ihx)
set(HEX_FILE ${CMAKE_BINARY_DIR}/${TARGET_NAME}.hex)
set(BIN_FILE ${CMAKE_BINARY_DIR}/${TARGET_NAME}.bin)
set(MEM_FILE ${CMAKE_BINARY_DIR}/${TARGET_NAME}.mem)

add_custom_command(
    OUTPUT  ${IHX_FILE}
    BYPRODUCTS ${MEM_FILE}
    COMMAND ${SDCC_CC} ${SDCC_LDFLAGS} ${REL_FILES} -o ${IHX_FILE}
    DEPENDS ${REL_FILES}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "LD ${TARGET_NAME}.ihx"
    VERBATIM
)

add_custom_command(
    OUTPUT  ${HEX_FILE}
    COMMAND ${SDCC_PACKIHX} ${IHX_FILE} > ${HEX_FILE}
    DEPENDS ${IHX_FILE}
    COMMENT "HEX ${TARGET_NAME}.hex"
    VERBATIM
)

add_custom_command(
    OUTPUT  ${BIN_FILE}
    COMMAND ${SDCC_MAKEBIN} -p < ${IHX_FILE} > ${BIN_FILE}
    DEPENDS ${IHX_FILE}
    COMMENT "BIN ${TARGET_NAME}.bin"
    VERBATIM
)

add_custom_target(firmware ALL
    DEPENDS ${HEX_FILE} ${BIN_FILE}
    COMMENT "See ${MEM_FILE} for memory usage details"
)

# Flash target
add_custom_target(flash
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/chprog.py ${BIN_FILE}
    DEPENDS firmware
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Flashing CH552 (hold BOOT and plug USB before invoking)"
    VERBATIM
)

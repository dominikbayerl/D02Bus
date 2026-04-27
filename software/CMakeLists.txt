cmake_minimum_required(VERSION 3.10)
project(D02Bus C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(CMAKE_C_COMPILER_ID MATCHES "SDCC" OR CMAKE_SYSTEM_PROCESSOR MATCHES "8051")
    message(STATUS "Configuring firmware build for CH55x via SDCC...")

    set(FREQ_SYS "16000000")
    set(XRAM_LOC "0x0000")
    set(XRAM_SIZE "0x0400")
    set(CODE_SIZE "0x3800")

    add_compile_options(
        -mmcs51
        --model-small
        --no-xinit-opt
        --xram-size ${XRAM_SIZE}
        --xram-loc ${XRAM_LOC}
        --code-size ${CODE_SIZE}
        --opt-code-size
        --stack-auto
        --float-reent
        --int-long-reent
    )
    add_compile_definitions(F_CPU=${FREQ_SYS})

    file(GLOB FIRMWARE_SOURCES 
        "src/*.c"
        "src/usb/*.c"
    )
    list(FILTER FIRMWARE_SOURCES EXCLUDE REGEX "src/test/.*")

    add_executable(firmware ${FIRMWARE_SOURCES})
    target_include_directories(firmware PRIVATE include src .)

    # Set extension to .ihx for SDCC linker output
    set_target_properties(firmware PROPERTIES SUFFIX ".ihx")

    # Generate .hex
    add_custom_command(TARGET firmware POST_BUILD
        COMMAND ${PACK_HEX_PROGRAM} $<TARGET_FILE:firmware> > ${CMAKE_CURRENT_BINARY_DIR}/firmware.hex
        BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/firmware.hex
        COMMENT "Building firmware.hex ..."
    )

    # Generate .bin
    add_custom_command(TARGET firmware POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -I ihex -O binary $<TARGET_FILE:firmware> ${CMAKE_CURRENT_BINARY_DIR}/firmware.bin
        BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/firmware.bin
        COMMENT "Building firmware.bin ..."
    )

    # Size output (requires awk)
    find_program(AWK_PROGRAM NAMES awk)
    if(AWK_PROGRAM)
        string(CONCAT SIZE_AWK_PROGRAM
            "\$1 == \"ROM/EPROM/FLASH\" { flash = \$4 } "
            "\$1 == \"Stack\"           { iram  = 248 - \$10 } "
            "\$1 == \"EXTERNAL\"        { xram  = XRAM_LOC + \$5 } "
            "END { "
                "printf \"===============\\n\" \;"
                "printf \"FLASH: %d bytes\\n\", flash \;"
                "printf \"IRAM:  %d bytes\\n\", iram \;"
                "printf \"XRAM:  %d bytes\\n\", xram \;"
                "printf \"===============\\n\" \;"
            "}"
        )
        add_custom_command(TARGET firmware POST_BUILD
            COMMAND ${AWK_PROGRAM} -v XRAM_LOC=${XRAM_LOC} ${SIZE_AWK_PROGRAM} $<TARGET_FILE_DIR:firmware>/firmware.mem
            COMMENT "Calculating memory usage:"
            VERBATIM
        )
    else()
        message(WARNING "awk not found, size output will not be generated")
    endif()

    # Flash command
    find_program(PYTHON_EXECUTABLE NAMES python3 python)
    add_custom_target(flash
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/chprog.py ${CMAKE_CURRENT_BINARY_DIR}/firmware.bin
        DEPENDS firmware
        COMMENT "Uploading to CH55x ..."
    )

else()
    message(STATUS "Configuring host unit tests...")

    # Unity framework (header-only)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

    # Add Unity framework
    add_library(unity STATIC
        unity/unity.c
    )
    target_include_directories(unity PUBLIC unity)

    # Modbus library
    add_library(modbus STATIC
        src/modbus.c
    )
    target_include_directories(modbus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

    # SML library
    add_library(sml STATIC
        src/sml.c
    )
    target_include_directories(sml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

    # Test executable for Modbus
    add_executable(test_modbus
        src/test/test_modbus.c
    )
    target_link_libraries(test_modbus PRIVATE modbus unity)

    # Test executable for SML
    add_executable(test_sml
        src/test/test_sml.c
    )
    target_link_libraries(test_sml PRIVATE sml unity)

    # Optional SML VM debug tool
    option(BUILD_VM_DEBUG "Build SML VM debug tool" OFF)
    if(BUILD_VM_DEBUG)
        add_executable(vm_debug
            src/test/vm_debug.c
        )
        target_link_libraries(vm_debug PRIVATE sml)
    endif()

    # Optional SML GetList.Res debug tool
    option(BUILD_SML_DEBUG "Build SML debug tool" OFF)
    if(BUILD_SML_DEBUG)
        add_executable(sml_debug
            src/test/sml_debug.c
        )
        target_link_libraries(sml_debug PRIVATE sml)
    endif()

    # Enable testing
    enable_testing()
    add_test(NAME modbus_tests COMMAND test_modbus)
    add_test(NAME sml_tests COMMAND test_sml)
endif()

cmake_minimum_required(VERSION 3.10)
project(modbus_tests)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

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

# Test executable
add_executable(test_modbus
    src/test/test_modbus.c
)
target_link_libraries(test_modbus PRIVATE modbus unity)

# Enable testing
enable_testing()
add_test(NAME modbus_tests COMMAND test_modbus)

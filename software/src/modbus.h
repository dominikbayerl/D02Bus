// SPDX-License-Identifier: CC-BY-SA-4.0
#pragma once
#include <stdint.h>
#include <stddef.h>


union modbus_value {
    uint8_t  u8;
    uint16_t u16;
    uint32_t u32;
    int8_t   i8;
    int16_t  i16;
    int32_t  i32;
    float    f32;
};

typedef enum {
    MODBUS_SUCCESS = 0x00,
    MODBUS_ERROR_ILLEGAL_FUNCTION     = 0x01,
    MODBUS_ERROR_ILLEGAL_DATA_ADDRESS = 0x02,
    MODBUS_ERROR_ILLEGAL_DATA_VALUE   = 0x03,
    MODBUS_ERROR_SLAVE_DEVICE_FAILURE = 0x04,
    MODBUS_ERROR_ACKNOWLEDGE          = 0x05,
    MODBUS_ERROR_SLAVE_DEVICE_BUSY    = 0x06,
    MODBUS_ERROR_MEMORY_PARITY_ERROR  = 0x08,
    MODBUS_ERROR_GATEWAY_PATH_UNAVAILABLE = 0x0A,
    // Custom errors
    MODBUS_ERROR_ADDRESS_MISMATCH = 0xFF,
} modbus_rc;

enum modbus_function_code {
    MODBUS_FUNC_READ_HOLDING_REGISTERS = 0x03,
    MODBUS_FUNC_DIAGNOSTICS = 0x08
};

// Length Requirements for validation
enum {
    MODBUS_RTU_READ_REQUEST_LEN = 8u,
};


typedef struct {
    uint16_t register_address;
    enum {
        REG_TYPE_UINT16,
        REG_TYPE_UINT32,
        REG_TYPE_INT16,
        REG_TYPE_INT32,
        REG_TYPE_FLOAT
    } type;
    union modbus_value value;
} modbus_register;

typedef struct {
    uint8_t device_address;
    modbus_register *registers;
    size_t register_count;
} modbus_ctx;

modbus_rc modbus_process_frame(modbus_ctx *ctx, uint8_t *buffer, size_t buffer_capacity, size_t *length);
extern uint16_t modbus_crc16(const uint8_t *data, size_t size);

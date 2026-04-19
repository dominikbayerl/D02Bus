// SPDX-License-Identifier: CC-BY-SA-4.0
#include <unity.h>
#include "modbus.h"
#include <string.h>

/* Test fixtures */
static modbus_register test_registers[] = {
    { .register_address = 0x0000, .value.u16 = 0x1234 },
    { .register_address = 0x0001, .value.u16 = 0x5678 },
    { .register_address = 0x0002, .value.u16 = 0xABCD },
};

static modbus_ctx test_ctx = {
    .device_address = 0x01,
    .registers = test_registers,
    .register_count = 3
};

static uint8_t buffer[256];
static size_t buffer_length;

void setUp(void) {
    memset(buffer, 0, sizeof(buffer));
    buffer_length = 0;
}

void tearDown(void) {
    memset(buffer, 0, sizeof(buffer));
}

/* Helper function to calculate and set CRC in buffer */
static void set_crc_in_buffer(uint8_t *buf, size_t data_length) {
    uint16_t crc = modbus_crc16(buf, data_length);
    buf[data_length] = crc & 0xFF;
    buf[data_length + 1] = (crc >> 8) & 0xFF;
}

/* Test: Frame too short (less than 4 bytes) */
void test_frame_too_short(void) {
    buffer_length = 3;
    buffer[0] = 0x01;
    buffer[1] = 0x03;
    buffer[2] = 0x00;
    int result = modbus_process_frame(&test_ctx, buffer, sizeof(buffer), &buffer_length);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERROR_ILLEGAL_DATA_VALUE, result);
}

/* Test: Frame exactly 8 bytes is valid for structure */
void test_frame_minimum_length(void) {
    buffer_length = 8;
    buffer[0] = 0x01;  // Device address
    buffer[1] = 0x03;  // Function code: Read Holding Registers
    buffer[2] = 0x00;  // Register address high byte
    buffer[3] = 0x00;  // Register address low byte
    buffer[4] = 0x00;  // Register count high byte
    buffer[5] = 0x01;  // Register count low byte
    set_crc_in_buffer(buffer, 6);
    
    int result = modbus_process_frame(&test_ctx, buffer, sizeof(buffer), &buffer_length);
    TEST_ASSERT_EQUAL_INT(MODBUS_SUCCESS, result);
}

/* Test: Address mismatch */
void test_address_mismatch(void) {
    buffer_length = 8;
    buffer[0] = 0x02;  // Wrong device address (should be 0x01)
    buffer[1] = 0x03;  // Function code
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    buffer[4] = 0x00;
    buffer[5] = 0x01;
    set_crc_in_buffer(buffer, 6);
    
    int result = modbus_process_frame(&test_ctx, buffer, sizeof(buffer), &buffer_length);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERROR_ADDRESS_MISMATCH, result);
}

/* Test: CRC mismatch */
void test_crc_mismatch(void) {
    buffer_length = 8;
    buffer[0] = 0x01;  // Device address
    buffer[1] = 0x03;  // Function code
    buffer[2] = 0x00;  // Register address
    buffer[3] = 0x00;
    buffer[4] = 0x00;  // Register count
    buffer[5] = 0x01;
    buffer[6] = 0xFF;  // Wrong CRC
    buffer[7] = 0xFF;
    
    int result = modbus_process_frame(&test_ctx, buffer, sizeof(buffer), &buffer_length);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERROR_MEMORY_PARITY_ERROR, result);
}

/* Test: Unsupported function code */
void test_unsupported_function_code(void) {
    buffer_length = 8;
    buffer[0] = 0x01;  // Device address
    buffer[1] = 0x08;  // Function code: Diagnostics (unsupported)
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    buffer[4] = 0x00;
    buffer[5] = 0x01;
    set_crc_in_buffer(buffer, 6);
    
    int result = modbus_process_frame(&test_ctx, buffer, sizeof(buffer), &buffer_length);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERROR_ILLEGAL_FUNCTION, result);
}

/* Test: Read single register - success */
void test_read_single_register(void) {
    buffer_length = 8;
    buffer[0] = 0x01;  // Device address
    buffer[1] = 0x03;  // Function code: Read Holding Registers
    buffer[2] = 0x00;  // Register address high byte
    buffer[3] = 0x00;  // Register address low byte (address 0x0000)
    buffer[4] = 0x00;  // Register count high byte
    buffer[5] = 0x01;  // Register count low byte (read 1 register)
    set_crc_in_buffer(buffer, 6);
    
    int result = modbus_process_frame(&test_ctx, buffer, sizeof(buffer), &buffer_length);
    
    TEST_ASSERT_EQUAL_INT(MODBUS_SUCCESS, result);
    TEST_ASSERT_EQUAL_INT(0x01, buffer[0]);      // Device address
    TEST_ASSERT_EQUAL_INT(0x03, buffer[1]);      // Function code
    TEST_ASSERT_EQUAL_INT(0x02, buffer[2]);      // Byte count (1 register = 2 bytes)
    TEST_ASSERT_EQUAL_INT(0x12, buffer[3]);      // Register value high byte (0x1234)
    TEST_ASSERT_EQUAL_INT(0x34, buffer[4]);      // Register value low byte
    TEST_ASSERT(buffer_length > 0);
}

/* Test: Read multiple consecutive registers */
void test_read_multiple_registers(void) {
    buffer_length = 8;
    buffer[0] = 0x01;  // Device address
    buffer[1] = 0x03;  // Function code: Read Holding Registers
    buffer[2] = 0x00;  // Register address
    buffer[3] = 0x00;  // Register address 0x0000
    buffer[4] = 0x00;  // Register count high byte
    buffer[5] = 0x02;  // Register count low byte (read 2 registers)
    set_crc_in_buffer(buffer, 6);
    
    int result = modbus_process_frame(&test_ctx, buffer, sizeof(buffer), &buffer_length);
    
    TEST_ASSERT_EQUAL_INT(MODBUS_SUCCESS, result);
    TEST_ASSERT_EQUAL_INT(0x01, buffer[0]);      // Device address
    TEST_ASSERT_EQUAL_INT(0x03, buffer[1]);      // Function code
    TEST_ASSERT_EQUAL_INT(0x04, buffer[2]);      // Byte count (2 registers = 4 bytes)
    TEST_ASSERT_EQUAL_INT(0x12, buffer[3]);      // First register value high
    TEST_ASSERT_EQUAL_INT(0x34, buffer[4]);      // First register value low
    TEST_ASSERT_EQUAL_INT(0x56, buffer[5]);      // Second register value high
    TEST_ASSERT_EQUAL_INT(0x78, buffer[6]);      // Second register value low
    TEST_ASSERT(buffer_length > 0);
}

/* Test: Register not found */
void test_register_not_found(void) {
    buffer_length = 8;
    buffer[0] = 0x01;  // Device address
    buffer[1] = 0x03;  // Function code: Read Holding Registers
    buffer[2] = 0x00;  // Register address
    buffer[3] = 0x05;  // Register address 0x0005 (doesn't exist)
    buffer[4] = 0x00;  // Register count
    buffer[5] = 0x01;
    set_crc_in_buffer(buffer, 6);
    
    int result = modbus_process_frame(&test_ctx, buffer, sizeof(buffer), &buffer_length);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERROR_ILLEGAL_DATA_ADDRESS, result);
}

/* Test: Read middle register */
void test_read_middle_register(void) {
    buffer_length = 8;
    buffer[0] = 0x01;  // Device address
    buffer[1] = 0x03;  // Function code: Read Holding Registers
    buffer[2] = 0x00;  // Register address
    buffer[3] = 0x01;  // Register address 0x0001
    buffer[4] = 0x00;  // Register count
    buffer[5] = 0x01;  // Read 1 register
    set_crc_in_buffer(buffer, 6);
    
    int result = modbus_process_frame(&test_ctx, buffer, sizeof(buffer), &buffer_length);
    
    TEST_ASSERT_EQUAL_INT(MODBUS_SUCCESS, result);
    TEST_ASSERT_EQUAL_INT(0x56, buffer[3]);      // Register value at 0x0001 (0x5678)
    TEST_ASSERT_EQUAL_INT(0x78, buffer[4]);
}

/* Test: Read UINT32 register pair */
void test_read_uint32_register_pair(void) {
    modbus_register registers_32[] = {
        { .register_address = 0x0100, .type = REG_TYPE_UINT32, .value.u32 = 0x11223344 },
        { .register_address = 0x0101, .type = REG_TYPE_UINT16, .value.u16 = 0x0000 },
    };
    modbus_ctx ctx_32 = {
        .device_address = 0x01,
        .registers = registers_32,
        .register_count = 2
    };

    buffer_length = 8;
    buffer[0] = 0x01;  // Device address
    buffer[1] = 0x03;  // Function code: Read Holding Registers
    buffer[2] = 0x01;  // Register address 0x0100
    buffer[3] = 0x00;  
    buffer[4] = 0x00;
    buffer[5] = 0x02;  // Register count = 2
    set_crc_in_buffer(buffer, 6);

    int result = modbus_process_frame(&ctx_32, buffer, sizeof(buffer), &buffer_length);

    TEST_ASSERT_EQUAL_INT(MODBUS_SUCCESS, result);
    TEST_ASSERT_EQUAL_INT(0x01, buffer[0]);
    TEST_ASSERT_EQUAL_INT(0x03, buffer[1]);
    TEST_ASSERT_EQUAL_INT(0x04, buffer[2]);  // 2 registers -> 4 bytes
    TEST_ASSERT_EQUAL_INT(0x11, buffer[3]);
    TEST_ASSERT_EQUAL_INT(0x22, buffer[4]);
    TEST_ASSERT_EQUAL_INT(0x33, buffer[5]);
    TEST_ASSERT_EQUAL_INT(0x44, buffer[6]);
}

/* Test: Read INT32 register pair */
void test_read_int32_register_pair(void) {
    modbus_register registers_32[] = {
        { .register_address = 0x0200, .type = REG_TYPE_INT32, .value.i32 = -2 },
        { .register_address = 0x0201, .type = REG_TYPE_UINT16, .value.u16 = 0x0000 },
    };
    modbus_ctx ctx_32 = {
        .device_address = 0x01,
        .registers = registers_32,
        .register_count = 2
    };

    buffer_length = 8;
    buffer[0] = 0x01;  // Device address
    buffer[1] = 0x03;  // Function code: Read Holding Registers
    buffer[2] = 0x02;  // Register address 0x0200
    buffer[3] = 0x00; 
    buffer[4] = 0x00;
    buffer[5] = 0x02;  // Register count = 2
    set_crc_in_buffer(buffer, 6);

    int result = modbus_process_frame(&ctx_32, buffer, sizeof(buffer), &buffer_length);

    TEST_ASSERT_EQUAL_INT(MODBUS_SUCCESS, result);
    const uint8_t expected[] = {0x01, 0x03, 0x04, 0xFF, 0xFF, 0xFF, 0xFE, 0x3a, 0x67};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buffer, buffer_length);
}

/* Test: Read FLOAT register pair */
void test_read_float_register_pair(void) {
    modbus_register float_registers[] = {
        { .register_address = 0x0300, .type = REG_TYPE_FLOAT, .value.f32 = 1.5f },
        { .register_address = 0x0301, .type = REG_TYPE_UINT16, .value.u16 = 0x0000 },
    };
    modbus_ctx float_ctx = {
        .device_address = 0x01,
        .registers = float_registers,
        .register_count = 2
    };

    uint32_t expected_float_bits = 0;
    memcpy(&expected_float_bits, &float_registers[0].value.f32, sizeof(expected_float_bits));

    buffer_length = 8;
    buffer[0] = 0x01;
    buffer[1] = 0x03;
    buffer[2] = 0x03;
    buffer[3] = 0x00; // Register address 0x0300 (big-endian)
    buffer[4] = 0x00;  
    buffer[5] = 0x02; // Register count = 2 (big-endian)
    set_crc_in_buffer(buffer, 6);

    int result = modbus_process_frame(&float_ctx, buffer, sizeof(buffer), &buffer_length);

    TEST_ASSERT_EQUAL_INT(MODBUS_SUCCESS, result);
    TEST_ASSERT_EQUAL_INT(0x01, buffer[0]);
    TEST_ASSERT_EQUAL_INT(0x03, buffer[1]);
    TEST_ASSERT_EQUAL_INT(0x04, buffer[2]);
    TEST_ASSERT_EQUAL_INT((expected_float_bits >> 24) & 0xFF, (uint8_t)buffer[3]);
    TEST_ASSERT_EQUAL_INT((expected_float_bits >> 16) & 0xFF, (uint8_t)buffer[4]);
    TEST_ASSERT_EQUAL_INT((expected_float_bits >> 8) & 0xFF, (uint8_t)buffer[5]);
    TEST_ASSERT_EQUAL_INT(expected_float_bits & 0xFF, (uint8_t)buffer[6]);
}

/* Test: Next register can start at +2 after a float pair */
void test_read_float_register_pair_then_next_register(void) {
    modbus_register float_registers[] = {
        { .register_address = 0x0400, .type = REG_TYPE_FLOAT, .value.f32 = -12.75f },
        { .register_address = 0x0402, .type = REG_TYPE_UINT16, .value.u16 = 0x0000 },
    };
    modbus_ctx float_ctx = {
        .device_address = 0x01,
        .registers = float_registers,
        .register_count = 2
    };

    buffer_length = 8;
    buffer[0] = 0x01;
    buffer[1] = 0x03;
    buffer[2] = 0x04;  // Register address 0x0400 (big-endian)
    buffer[3] = 0x00;
    buffer[4] = 0x00;  // Register count = 3 (float consumes two addresses, then read 0x0402)
    buffer[5] = 0x03;
    set_crc_in_buffer(buffer, 6);

    int result = modbus_process_frame(&float_ctx, buffer, sizeof(buffer), &buffer_length);

    TEST_ASSERT_EQUAL_INT(MODBUS_SUCCESS, result);
    TEST_ASSERT_EQUAL_INT(0x06, buffer[2]);
    TEST_ASSERT_EQUAL_INT(0x00, buffer[7]);
    TEST_ASSERT_EQUAL_INT(0x00, buffer[8]);
}

/* Test: 32-bit values cannot be partially read from first address */
void test_read_float_register_pair_partial_read_rejected(void) {
    modbus_register float_registers[] = {
        { .register_address = 0x0400, .type = REG_TYPE_FLOAT, .value.f32 = -12.75f },
    };
    modbus_ctx float_ctx = {
        .device_address = 0x01,
        .registers = float_registers,
        .register_count = 1
    };

    buffer_length = 8;
    buffer[0] = 0x01;
    buffer[1] = 0x03;
    buffer[2] = 0x04;
    buffer[3] = 0x00; // Register address 0x0400
    buffer[4] = 0x00;
    buffer[5] = 0x01; // Partial read request
    set_crc_in_buffer(buffer, 6);

    int result = modbus_process_frame(&float_ctx, buffer, sizeof(buffer), &buffer_length);

    TEST_ASSERT_EQUAL_INT(MODBUS_ERROR_ILLEGAL_DATA_ADDRESS, result);
}

/* Test: 32-bit values cannot be read from second register address */
void test_read_float_register_pair_from_second_address_rejected(void) {
    modbus_register float_registers[] = {
        { .register_address = 0x0400, .type = REG_TYPE_FLOAT, .value.f32 = -12.75f },
    };
    modbus_ctx float_ctx = {
        .device_address = 0x01,
        .registers = float_registers,
        .register_count = 1
    };

    buffer_length = 8;
    buffer[0] = 0x01;
    buffer[1] = 0x03;
    buffer[2] = 0x04;
    buffer[3] = 0x01; // Second address of 0x0400 pair
    buffer[4] = 0x00;
    buffer[5] = 0x01;
    set_crc_in_buffer(buffer, 6);

    int result = modbus_process_frame(&float_ctx, buffer, sizeof(buffer), &buffer_length);

    TEST_ASSERT_EQUAL_INT(MODBUS_ERROR_ILLEGAL_DATA_ADDRESS, result);
}

/* Test: Read 3 consecutive float registers */
void test_read_three_consecutive_float_registers(void) {
    modbus_register float_registers[] = {
        { .register_address =  0, .type = REG_TYPE_FLOAT, .value.f32 = 230.0f },
        { .register_address =  2, .type = REG_TYPE_FLOAT, .value.f32 = 231.0f },
        { .register_address =  4, .type = REG_TYPE_FLOAT, .value.f32 = 232.0f },
    };
    modbus_ctx float_ctx = {
        .device_address = 0x01,
        .registers = float_registers,
        .register_count = 6
    };

    buffer_length = 8;
    buffer[0] = 0x01;
    buffer[1] = 0x03;
    buffer[2] = 0x00;
    buffer[3] = 0x00; // Register address 0x0000
    buffer[4] = 0x00;
    buffer[5] = 0x06; // Register count = 6
    set_crc_in_buffer(buffer, 6);

    int result = modbus_process_frame(&float_ctx, buffer, sizeof(buffer), &buffer_length);
    printf("Result: %d, Buffer Length: %zu\n", result, buffer_length);

    TEST_ASSERT_EQUAL_INT(MODBUS_SUCCESS, result);
    TEST_ASSERT_EQUAL_INT(0x01, buffer[0]);
    TEST_ASSERT_EQUAL_INT(0x03, buffer[1]);
    TEST_ASSERT_EQUAL_INT(0x0C, buffer[2]); // Byte count (6 registers = 12 bytes)

    // Check first float (230.0f = 0x43800000)
    TEST_ASSERT_EQUAL_INT(0x43, buffer[3]);
    TEST_ASSERT_EQUAL_INT(0x66, buffer[4]);
    TEST_ASSERT_EQUAL_INT(0x00, buffer[5]);
    TEST_ASSERT_EQUAL_INT(0x00, buffer[6]);

    // Check second float (231.0f = 0x43670000)
    TEST_ASSERT_EQUAL_INT(0x43, buffer[7]);
    TEST_ASSERT_EQUAL_INT(0x67, buffer[8]);
    TEST_ASSERT_EQUAL_INT(0x00, buffer[9]);
    TEST_ASSERT_EQUAL_INT(0x00, buffer[10]);

    // Check third float (232.0f = 0x43680000)
    TEST_ASSERT_EQUAL_INT(0x43, buffer[11]);
    TEST_ASSERT_EQUAL_INT(0x68, buffer[12]);
    TEST_ASSERT_EQUAL_INT(0x00, buffer[13]);
    TEST_ASSERT_EQUAL_INT(0x00, buffer[14]);
}

/* Test: CRC calculation correctness */
void test_crc_calculation(void) {
    // Test CRC for a known frame
    uint8_t test_data[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x01 };
    uint16_t crc = modbus_crc16(test_data, 6);
    
    // CRC should not be 0 for valid data
    TEST_ASSERT_NOT_EQUAL(0, crc);
}

/* Test: CRC with NULL pointer */
void test_crc_null_pointer(void) {
    uint16_t crc = modbus_crc16(NULL, 6);
    // Should return initial CRC value
    TEST_ASSERT_EQUAL_INT(0xFFFF, crc);
}

/* Test: Empty frame (length 0) */
void test_empty_frame(void) {
    buffer_length = 0;
    int result = modbus_process_frame(&test_ctx, buffer, sizeof(buffer), &buffer_length);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERROR_ILLEGAL_DATA_VALUE, result);
}

/* Test: Read request length must be exactly 8 bytes */
void test_read_request_length_not_exact(void) {
    buffer_length = 9;
    buffer[0] = 0x01;
    buffer[1] = 0x03;
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    buffer[4] = 0x00;
    buffer[5] = 0x01;
    set_crc_in_buffer(buffer, 6);
    buffer[8] = 0xAA; // Extra trailing byte should cause rejection

    int result = modbus_process_frame(&test_ctx, buffer, sizeof(buffer), &buffer_length);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERROR_ILLEGAL_DATA_VALUE, result);
}

/* Test: Requested span must fit register table */
void test_read_span_out_of_bounds(void) {
    buffer_length = 8;
    buffer[0] = 0x01;
    buffer[1] = 0x03;
    buffer[2] = 0x00;
    buffer[3] = 0x02; // Start at last register (0x0002)
    buffer[4] = 0x00;
    buffer[5] = 0x02; // Request two registers -> out of bounds
    set_crc_in_buffer(buffer, 6);

    int result = modbus_process_frame(&test_ctx, buffer, sizeof(buffer), &buffer_length);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERROR_ILLEGAL_DATA_ADDRESS, result);
}

/* Test: Null context is rejected */
void test_null_context(void) {
    buffer_length = 8;
    buffer[0] = 0x01;
    buffer[1] = 0x03;
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    buffer[4] = 0x00;
    buffer[5] = 0x01;
    set_crc_in_buffer(buffer, 6);

    int result = modbus_process_frame(NULL, buffer, sizeof(buffer), &buffer_length);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERROR_SLAVE_DEVICE_FAILURE, result);
}

/* Test: Null buffer is rejected */
void test_null_buffer(void) {
    buffer_length = 8;
    int result = modbus_process_frame(&test_ctx, NULL, sizeof(buffer), &buffer_length);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERROR_SLAVE_DEVICE_FAILURE, result);
}

/* Test: Null length is rejected */
void test_null_length(void) {
    int result = modbus_process_frame(&test_ctx, buffer, sizeof(buffer), NULL);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERROR_SLAVE_DEVICE_FAILURE, result);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_frame_too_short);
    RUN_TEST(test_frame_minimum_length);
    RUN_TEST(test_address_mismatch);
    RUN_TEST(test_crc_mismatch);
    RUN_TEST(test_unsupported_function_code);
    RUN_TEST(test_read_single_register);
    RUN_TEST(test_read_multiple_registers);
    RUN_TEST(test_register_not_found);
    RUN_TEST(test_read_middle_register);
    RUN_TEST(test_read_uint32_register_pair);
    RUN_TEST(test_read_int32_register_pair);
    RUN_TEST(test_read_float_register_pair);
    RUN_TEST(test_read_float_register_pair_then_next_register);
    RUN_TEST(test_read_float_register_pair_partial_read_rejected);
    RUN_TEST(test_read_float_register_pair_from_second_address_rejected);
    RUN_TEST(test_read_three_consecutive_float_registers);
    RUN_TEST(test_crc_calculation);
    RUN_TEST(test_crc_null_pointer);
    RUN_TEST(test_empty_frame);
    RUN_TEST(test_read_request_length_not_exact);
    RUN_TEST(test_read_span_out_of_bounds);
    RUN_TEST(test_null_context);
    RUN_TEST(test_null_buffer);
    RUN_TEST(test_null_length);
    
    return UNITY_END();
}

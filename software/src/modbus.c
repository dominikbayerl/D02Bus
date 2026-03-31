// SPDX-License-Identifier: CC-BY-SA-4.0
#include <string.h>
#include "modbus.h"

static const uint16_t CRC16_VALID = 0x0000;


modbus_rc modbus_process_frame(modbus_ctx *ctx, uint8_t *buffer, size_t buffer_capacity, size_t *length) {
    if (ctx == NULL || buffer == NULL || length == NULL) {
        return MODBUS_ERROR_SLAVE_DEVICE_FAILURE;
    }

    if (buffer_capacity == 0u || *length > buffer_capacity) {
        return MODBUS_ERROR_ILLEGAL_DATA_VALUE;
    }

    if (ctx->register_count > 0u && ctx->registers == NULL) {
        return MODBUS_ERROR_SLAVE_DEVICE_FAILURE;
    }

    if (*length < 4) {
        return MODBUS_ERROR_ILLEGAL_DATA_VALUE;
    }

    uint8_t device_address = (uint8_t)buffer[0];
    uint8_t function_code = (uint8_t)buffer[1];
    if (device_address != ctx->device_address) {
        return MODBUS_ERROR_ADDRESS_MISMATCH;
    }

    // Process the frame based on the function code
    switch (function_code) {
        case MODBUS_FUNC_READ_HOLDING_REGISTERS:
            if (*length != MODBUS_RTU_READ_REQUEST_LEN) {
                return MODBUS_ERROR_ILLEGAL_DATA_VALUE;
            }

            uint16_t crc = modbus_crc16(buffer, *length);
            if (crc != CRC16_VALID) {
                return MODBUS_ERROR_MEMORY_PARITY_ERROR;
            }

            uint16_t register_address = ((uint16_t)(uint8_t)buffer[2] << 8) | (uint16_t)(uint8_t)buffer[3];
            uint16_t register_count = ((uint16_t)(uint8_t)buffer[4] << 8) | (uint16_t)(uint8_t)buffer[5];

            if (register_count == 0 || register_count > 125) {
                return MODBUS_ERROR_ILLEGAL_DATA_ADDRESS;
            }

            if (ctx->register_count == 0u) {
                return MODBUS_ERROR_ILLEGAL_DATA_ADDRESS;
            }

            // Binary Search in registers
            int left = 0;
            int right = (int)ctx->register_count - 1;
            
            while (left <= right) {
                int mid = left + (right - left) / 2;
                uint16_t mid_address = ctx->registers[mid].register_address;
                
                if (mid_address == register_address) {
                    size_t start_index = (size_t)mid;
                    if (start_index + (size_t)register_count > ctx->register_count) {
                        return MODBUS_ERROR_ILLEGAL_DATA_ADDRESS;
                    }

                    size_t response_size = 3u + ((size_t)register_count * 2u) + 2u;
                    if (response_size > buffer_capacity) {
                        return MODBUS_ERROR_SLAVE_DEVICE_FAILURE;
                    }

                    // Found the register
                    // Build response frame
                    size_t ptr = 0;
                    buffer[ptr++] = ctx->device_address;
                    buffer[ptr++] = function_code;
                    buffer[ptr++] = (uint8_t)(register_count * 2u);
                    for (size_t i = 0; i < register_count; i++) {
                        size_t reg_index = start_index + i;

                        if (ctx->registers[reg_index].register_address != (uint16_t)(register_address + i)) {
                            return MODBUS_ERROR_ILLEGAL_DATA_ADDRESS;
                        }
                        switch (ctx->registers[reg_index].type) {
                            case REG_TYPE_UINT16:
                            case REG_TYPE_INT16:
                                buffer[ptr++] = (uint8_t)((ctx->registers[reg_index].value.u16 >> 8) & 0xFFu);
                                buffer[ptr++] = (uint8_t)(ctx->registers[reg_index].value.u16 & 0xFFu);
                                break;
                            case REG_TYPE_UINT32:
                            case REG_TYPE_INT32:
                                if ((i + 1u) >= register_count) {
                                    return MODBUS_ERROR_ILLEGAL_DATA_ADDRESS;
                                }
                                if (ctx->registers[reg_index + 1u].register_address != (uint16_t)(register_address + i + 1u)) {
                                    return MODBUS_ERROR_ILLEGAL_DATA_ADDRESS;
                                }
                                buffer[ptr++] = (uint8_t)((ctx->registers[reg_index].value.u32 >> 24) & 0xFFu);
                                buffer[ptr++] = (uint8_t)((ctx->registers[reg_index].value.u32 >> 16) & 0xFFu);
                                buffer[ptr++] = (uint8_t)((ctx->registers[reg_index].value.u32 >> 8) & 0xFFu);
                                buffer[ptr++] = (uint8_t)(ctx->registers[reg_index].value.u32 & 0xFFu);
                                i++; // Skip next register since we read 2 registers for 32-bit values
                                break;
                            case REG_TYPE_FLOAT:
                                // Assuming IEEE 754 float representation
                                if ((i + 1u) >= register_count) {
                                    return MODBUS_ERROR_ILLEGAL_DATA_ADDRESS;
                                }
                                if (ctx->registers[reg_index + 1u].register_address != (uint16_t)(register_address + i + 1u)) {
                                    return MODBUS_ERROR_ILLEGAL_DATA_ADDRESS;
                                }
                                uint32_t fval = 0u;
                                memcpy(&fval, &ctx->registers[reg_index].value.f32, sizeof(fval));
                                buffer[ptr++] = (uint8_t)((fval >> 24) & 0xFFu);
                                buffer[ptr++] = (uint8_t)((fval >> 16) & 0xFFu);
                                buffer[ptr++] = (uint8_t)((fval >> 8) & 0xFFu);
                                buffer[ptr++] = (uint8_t)(fval & 0xFFu);
                                i++; // Skip next register since we read 2 registers for float values
                                break;
                            default:
                                return MODBUS_ERROR_ILLEGAL_DATA_VALUE;
                        }
                    }
                    crc = modbus_crc16(buffer, ptr);
                    buffer[ptr++] = (uint8_t)(crc & 0xFFu);
                    buffer[ptr++] = (uint8_t)((crc >> 8) & 0xFFu);
                    *length = ptr;
                    return MODBUS_SUCCESS;
                }
                else if (mid_address < register_address) {
                    left = mid + 1;
                }
                else {
                    right = mid - 1;
                }
            } 
            return MODBUS_ERROR_ILLEGAL_DATA_ADDRESS;
        case MODBUS_FUNC_DIAGNOSTICS: // Diagnostics
            // Todo
            return MODBUS_ERROR_ILLEGAL_FUNCTION;
        default:
            return MODBUS_ERROR_ILLEGAL_FUNCTION;
    }
    return MODBUS_SUCCESS;
}

uint16_t modbus_crc16(const uint8_t *data, size_t size) {
    static const uint16_t s_lookup[256] = {
        0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
        0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
        0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
        0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
        0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
        0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
        0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
        0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
        0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
        0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
        0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
        0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
        0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
        0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
        0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
        0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
        0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
        0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
        0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
        0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
        0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
        0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
        0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
        0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
        0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
        0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
        0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
        0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
        0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
        0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
        0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
        0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
    };
	uint16_t crc = 0xFFFFu; /* Modbus CRC initial value */

    if (data == NULL) { 
        return crc;
    }

	for (size_t i = 0; i < size; ++i) {
		uint8_t lookup_ix = (crc ^ data[i]) & 0xFFu;
		crc = (crc >> 8) ^ s_lookup[lookup_ix];
	}

	return crc;
}

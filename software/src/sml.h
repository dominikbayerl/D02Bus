// SPDX-License-Identifier: CC-BY-SA-4.0
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifndef __SDCC
#ifndef __xdata
#define __xdata
#endif
#endif

/**
 * SML return codes
 */
typedef enum {
    SML_SUCCESS = 0,
    SML_ERROR_INVALID_FRAME = 1,
    SML_ERROR_CRC_MISMATCH = 2,
    SML_ERROR_OBIS_NOT_FOUND = 3,
    SML_ERROR_PARSE_ERROR = 4,
    SML_ERROR_BUFFER_TOO_SMALL = 5,
} sml_rc;

/**
 * SML value types
 */
typedef enum {
    SML_TYPE_UNSIGNED = 1,
    SML_TYPE_SIGNED = 2,
    SML_TYPE_FLOAT = 3,
    SML_TYPE_STRING = 4,
    SML_TYPE_TIMESTAMP = 5,
} sml_value_type_t;

/**
 * SML TL (Type-Length) encoded type values
 */
typedef enum {
    SML_TL_TYPE_STRING = 0x04,      // Octet string
    SML_TL_TYPE_SIGNED = 0x05,      // Signed integer
    SML_TL_TYPE_UNSIGNED = 0x06,    // Unsigned integer
    SML_TL_TYPE_FLOAT = 0x07,       // Float
    SML_TL_TYPE_LIST = 0x09,        // List structure
} sml_tl_type_t;

/**
 * SML frame markers
 */
typedef enum {
    SML_FRAME_START_BYTE1 = 0x1B,   // Escape byte
    SML_FRAME_START_BYTE2 = 0x01,   // Start marker
    SML_FRAME_END_BYTE1 = 0x1B,     // Escape byte
    SML_FRAME_END_BYTE2 = 0x1A,     // End marker
} sml_frame_marker_t;

/**
 * SML value structure - can hold different data types
 */
typedef struct {
    sml_value_type_t type;
    union {
        int64_t i64;         // Signed 64-bit integer
        uint64_t u64;        // Unsigned 64-bit integer
        float f32;           // 32-bit float
        uint8_t string[16];  // String (max 16 bytes)
    } value;
    uint8_t string_len;      // Length of string if type is STRING
} sml_value_t;

/**
 * Simplified OBIS code structure - 3 bytes identifying the value
 */
typedef struct {
    uint8_t code[3];         // Simplified OBIS code (C-D-E)
    sml_value_t *value;      // Pointer to extracted value
} sml_obis_t;

/**
 * SML parsing context
 */
typedef struct {
    const sml_obis_t *obis_list;   // Sorted list of OBIS codes to extract
    size_t obis_count;             // Number of entries in obis_list
} sml_ctx_t;

/**
 * Parse an SML frame from buffer
 * 
 * @param ctx        SML context with OBIS list
 * @param buffer     Input buffer with SML frame
 * @param buffer_len Length of data in buffer
 * @param length     Output: updated length after parsing (remaining unparsed data)
 * @return SML return code
 */
sml_rc sml_process_frame(__xdata const sml_ctx_t *ctx, __xdata uint8_t *buffer, size_t buffer_len, size_t *length);

/**
 * CRC-16 calculation for SML (X25 polynomial)
 */
uint16_t sml_crc16(const __xdata uint8_t *bytes, uint8_t len, const uint16_t start);
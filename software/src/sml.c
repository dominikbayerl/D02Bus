
// SPDX-License-Identifier: CC-BY-SA-4.0
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "sml.h"

/**
 * Binary search to find OBIS code in sorted list
 * 
 * @param list       Sorted OBIS list
 * @param count      Number of items
 * @param code       3-byte OBIS code to search for
 * @return Index if found, (size_t)-1 if not found
 */
static size_t sml_obis_binary_search(const sml_obis_t *list, size_t count, const uint8_t code[3]) {
    size_t left = 0, right = count;
    
    while (left < right) {
        size_t mid = left + (right - left) / 2;
        int cmp = 0;
        
        // Compare 3-byte OBIS codes
        for (int i = 0; i < 3; i++) {
            if (list[mid].code[i] != code[i]) {
                cmp = list[mid].code[i] - code[i];
                break;
            }
        }
        
        if (cmp == 0) {
            return mid;  // Found
        } else if (cmp < 0) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    
    return (size_t)-1;  // Not found
}

/**
 * Parse TL-encoded length and advance position
 * 
 * @param buffer     Buffer pointer
 * @param end        End of valid buffer
 * @param pos        In/out: current position
 * @param type       Out: TL type (upper 4 bits)
 * @return Length value, or 0 if error
 */
static unsigned int sml_parse_tl(const uint8_t *buffer, const uint8_t *end, 
                                  size_t *pos, uint8_t *type) {
    if (*pos >= (size_t)(end - buffer)) {
        return 0;
    }
    
    uint8_t tl = buffer[*pos];
    (*pos)++;
    
    *type = (tl >> 4) & 0x0F;
    unsigned int len = tl & 0x0F;
    
    // Extended length format
    if (len == 0 && tl != 0x00) {
        if (*pos >= (size_t)(end - buffer)) {
            return 0;
        }
        len = buffer[*pos];
        (*pos)++;
    }
    
    return len;
}

/**
 * Extract integer value from buffer (big-endian)
 */
static int64_t sml_extract_int(const uint8_t *data, unsigned int len, uint8_t is_signed) {
    int64_t result = 0;
    
    if (is_signed && len > 0 && (data[0] & 0x80)) {
        // Negative number - sign extend
        result = -1;
    }
    
    for (unsigned int i = 0; i < len; i++) {
        result = (result << 8) | data[i];
    }
    
    return result;
}

/**
 * Extract float value from buffer
 * Supports IEEE 754 32-bit float (4 bytes)
 */
static float sml_extract_float(const uint8_t *data, unsigned int len) {
    if (len == 4) {
        union {
            uint32_t u32;
            float f32;
        } conv;
        conv.u32 = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | 
                   ((uint32_t)data[2] << 8) | data[3];
        return conv.f32;
    }
    return 0.0f;
}

sml_rc sml_process_frame(__xdata const sml_ctx_t *ctx, __xdata uint8_t *buffer, size_t buffer_capacity, size_t *length) {
    if (!buffer || !ctx || !length || *length < 17) {
        return SML_ERROR_INVALID_FRAME;
    }

    if (buffer_capacity == 0 || *length > buffer_capacity) {
        return SML_ERROR_INVALID_FRAME;
    }

    size_t buf_len = *length;
    size_t pos;

    // Check exact SML start sequence: 1B 1B 1B 1B 01 01 01 01
    if (buffer[0] != SML_FRAME_START_BYTE1 || buffer[1] != SML_FRAME_START_BYTE1 ||
        buffer[2] != SML_FRAME_START_BYTE1 || buffer[3] != SML_FRAME_START_BYTE1 ||
        buffer[4] != SML_FRAME_START_BYTE2 || buffer[5] != SML_FRAME_START_BYTE2 ||
        buffer[6] != SML_FRAME_START_BYTE2 || buffer[7] != SML_FRAME_START_BYTE2) {
        return SML_ERROR_INVALID_FRAME;
    }

    // Payload starts after the 8-byte start marker
    pos = 8;

    // Find exact SML end sequence: 1B 1B 1B 1B 1A XX XX XX
    size_t frame_end = buf_len;
    for (size_t i = pos; i + 7 < buf_len; i++) {
        if (buffer[i] == SML_FRAME_END_BYTE1 && buffer[i + 1] == SML_FRAME_END_BYTE1 &&
            buffer[i + 2] == SML_FRAME_END_BYTE1 && buffer[i + 3] == SML_FRAME_END_BYTE1 &&
            buffer[i + 4] == SML_FRAME_END_BYTE2) {
            frame_end = i;
            break;
        }
    }

    if (frame_end == buf_len) {
        return SML_ERROR_INVALID_FRAME;
    }

    // Trailer: 1A <edl> <crc_lsb> <crc_msb>
    uint8_t edl = buffer[frame_end + 5];
    uint8_t fill_count = (uint8_t)(edl & 0x03u);
    if (fill_count > 3u || frame_end < pos + fill_count) {
        return SML_ERROR_INVALID_FRAME;
    }

    size_t payload_end = frame_end - fill_count;
    size_t crc_region_len = frame_end + 6;  // Include EDL, exclude CRC bytes
    if (crc_region_len > 255u) {
        return SML_ERROR_PARSE_ERROR;
    }

    uint16_t frame_crc = (uint16_t)buffer[frame_end + 6] | ((uint16_t)buffer[frame_end + 7] << 8);
    uint16_t calc_crc = sml_crc16(buffer, (uint8_t)crc_region_len, 0);
    if (calc_crc != frame_crc) {
        return SML_ERROR_CRC_MISMATCH;
    }

    // Parse payload starts right after start sequence
    pos = 8;
    
    // Parse message body - typically a list structure
    // Skip list TL byte and length if present
    if (pos < payload_end) {
        uint8_t type;
        unsigned int list_len = sml_parse_tl(buffer, buffer + payload_end, &pos, &type);
        
        // Type 0x09 = List, parse entries
        if (type == SML_TL_TYPE_LIST) {
            // Parse OBIS entries
            for (unsigned int entry = 0; entry < list_len && pos < payload_end; entry++) {
                // Each entry is a report with: timestamp, status, value list, period, values...
                // Skip timestamp (optional, TL-encoded)
                if (pos < payload_end && (buffer[pos] >> 4) == SML_TL_TYPE_UNSIGNED) {  // Unsigned type
                    sml_parse_tl(buffer, buffer + payload_end, &pos, &type);
                    unsigned int ts_len = sml_parse_tl(buffer, buffer + payload_end, &pos, &type);
                    pos += ts_len;
                } else if (buffer[pos] != 0x00 && pos < payload_end) {  // Not empty, is TL
                    unsigned int skip_len = sml_parse_tl(buffer, buffer + payload_end, &pos, &type);
                    if (skip_len == 0 && buffer[pos - 1] != 0x00) {
                        // Some value present, estimate skip
                        if (type == 0x06 || type == 0x05) pos += 4;  // Typical sizes
                        else pos += skip_len;
                    }
                }
                
                // Parse value list (list of OBIS values)
                if (pos < payload_end) {
                    uint8_t vtype;
                    unsigned int val_list_len = sml_parse_tl(buffer, buffer + payload_end, &pos, &vtype);
                    
                    if (vtype == SML_TL_TYPE_LIST) {  // It's a list
                        // Parse each OBIS value entry
                        for (unsigned int vi = 0; vi < val_list_len && pos < payload_end; vi++) {
                            // OBIS code is first: type 0x09 (list), 7 items typically
                            uint8_t obis_type;
                            unsigned int obis_items = sml_parse_tl(buffer, buffer + payload_end, &pos, &obis_type);
                            
                            if (obis_type == SML_TL_TYPE_LIST && obis_items >= 3) {
                                // Parse simplified OBIS code (3 bytes)
                                uint8_t obis_code[3];
                                int obis_parsed = 1;
                                
                                // First simplified OBIS byte
                                uint8_t code_type;
                                unsigned int code_len = sml_parse_tl(buffer, buffer + payload_end, &pos, &code_type);
                                if (code_type == SML_TL_TYPE_UNSIGNED && code_len == 1 && pos < payload_end) {
                                    obis_code[0] = buffer[pos];
                                    pos += code_len;
                                } else {
                                    obis_parsed = 0;
                                }
                                
                                // Second simplified OBIS byte
                                if (obis_parsed && pos < payload_end) {
                                    code_len = sml_parse_tl(buffer, buffer + payload_end, &pos, &code_type);
                                    if (code_type == SML_TL_TYPE_UNSIGNED && code_len == 1 && pos < payload_end) {
                                        obis_code[1] = buffer[pos];
                                        pos += code_len;
                                    } else {
                                        obis_parsed = 0;
                                    }
                                }
                                
                                // Third simplified OBIS byte
                                if (obis_parsed && pos < payload_end) {
                                    code_len = sml_parse_tl(buffer, buffer + payload_end, &pos, &code_type);
                                    if (code_type == SML_TL_TYPE_UNSIGNED && code_len == 1 && pos < payload_end) {
                                        obis_code[2] = buffer[pos];
                                        pos += code_len;
                                    } else {
                                        obis_parsed = 0;
                                    }
                                }
                                
                                if (obis_parsed) {
                                    // Search for this OBIS in the list
                                    size_t idx = sml_obis_binary_search(ctx->obis_list, ctx->obis_count, obis_code);
                                    
                                    if (idx != (size_t)-1) {
                                        // Found - parse the value
                                        // Skip status/timestamp/unit entries and get to value
                                        for (int skip = 0; skip < obis_items - 2 && pos < payload_end; skip++) {
                                            uint8_t skip_type;
                                            unsigned int skip_len = sml_parse_tl(buffer, buffer + payload_end, &pos, &skip_type);
                                            
                                            if (skip_len == 0 && buffer[pos - 1] == 0x00) {
                                                // Empty entry
                                                continue;
                                            }
                                            
                                            // Skip based on type
                                            if (skip_type == SML_TL_TYPE_UNSIGNED) {  // Unsigned: advance by skip_len
                                                pos += skip_len;
                                            } else if (skip_type == SML_TL_TYPE_SIGNED) {  // Signed
                                                pos += skip_len;
                                            } else if (skip_type == SML_TL_TYPE_FLOAT) {  // Float (only last before potential value)
                                                pos += skip_len;
                                            } else if (skip_type == SML_TL_TYPE_STRING) {  // String
                                                pos += skip_len;
                                            } else if (skip_type == SML_TL_TYPE_LIST) {  // List - more complex, skip for now
                                                // For safety, just skip what we know
                                                pos += 1;
                                            }
                                        }
                                        
                                        // Now parse the actual value
                                        if (pos < payload_end) {
                                            uint8_t val_type;
                                            unsigned int val_len = sml_parse_tl(buffer, buffer + payload_end, &pos, &val_type);
                                            
                                            if (val_len > 0 && pos + val_len <= payload_end) {
                                                // Store the value
                                                if (ctx->obis_list[idx].value != NULL) {
                                                    sml_value_t *pval = ctx->obis_list[idx].value;
                                                    
                                                    if (val_type == SML_TL_TYPE_UNSIGNED) {  // Unsigned
                                                        pval->type = SML_TYPE_UNSIGNED;
                                                        pval->value.u64 = sml_extract_int(buffer + pos, val_len, 0);
                                                    } else if (val_type == SML_TL_TYPE_SIGNED) {  // Signed
                                                        pval->type = SML_TYPE_SIGNED;
                                                        pval->value.i64 = sml_extract_int(buffer + pos, val_len, 1);
                                                    } else if (val_type == SML_TL_TYPE_FLOAT) {  // Float
                                                        pval->type = SML_TYPE_FLOAT;
                                                        pval->value.f32 = sml_extract_float(buffer + pos, val_len);
                                                    } else if (val_type == SML_TL_TYPE_STRING) {  // String
                                                        pval->type = SML_TYPE_STRING;
                                                        pval->string_len = (val_len < 16) ? val_len : 16;
                                                        for (int i = 0; i < pval->string_len; i++) {
                                                            pval->value.string[i] = buffer[pos + i];
                                                        }
                                                    }
                                                }
                                                pos += val_len;
                                            }
                                        }
                                    } else {
                                        // OBIS not in list, skip remaining items in this entry
                                        for (int skip = 0; skip < obis_items - 2 && pos < payload_end; skip++) {
                                            uint8_t skip_type;
                                            unsigned int skip_len = sml_parse_tl(buffer, buffer + payload_end, &pos, &skip_type);
                                            pos += skip_len;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                
                // Skip remaining fields in report (period, values)
                // For now, just continue to next entry
            }
        }
    }
    
    // Update length to point after full frame trailer (1B1B1B1B1A + EDL + CRC16)
    {
        size_t consumed = frame_end + 8;
        *length = (consumed < buf_len) ? (buf_len - consumed) : 0;
    }
    
    return SML_SUCCESS;
}

// CRC calculation for smartmeter (X25)
uint16_t sml_crc16(__xdata const uint8_t *bytes, uint8_t len, const uint16_t start) {
    // CRC table for SML (X25))
    static const uint16_t crc_table[] = {
        0x0000, 0x1189, 0x2312, 0x329B, 0x4624, 0x57AD, 0x6536, 0x74BF, 0x8C48, 0x9DC1, 0xAF5A, 0xBED3, 0xCA6C, 0xDBE5, 0xE97E, 0xF8F7,
        0x1081, 0x0108, 0x3393, 0x221A, 0x56A5, 0x472C, 0x75B7, 0x643E, 0x9CC9, 0x8D40, 0xBFDB, 0xAE52, 0xDAED, 0xCB64, 0xF9FF, 0xE876,
        0x2102, 0x308B, 0x0210, 0x1399, 0x6726, 0x76AF, 0x4434, 0x55BD, 0xAD4A, 0xBCC3, 0x8E58, 0x9FD1, 0xEB6E, 0xFAE7, 0xC87C, 0xD9F5,
        0x3183, 0x200A, 0x1291, 0x0318, 0x77A7, 0x662E, 0x54B5, 0x453C, 0xBDCB, 0xAC42, 0x9ED9, 0x8F50, 0xFBEF, 0xEA66, 0xD8FD, 0xC974,
        0x4204, 0x538D, 0x6116, 0x709F, 0x0420, 0x15A9, 0x2732, 0x36BB, 0xCE4C, 0xDFC5, 0xED5E, 0xFCD7, 0x8868, 0x99E1, 0xAB7A, 0xBAF3,
        0x5285, 0x430C, 0x7197, 0x601E, 0x14A1, 0x0528, 0x37B3, 0x263A, 0xDECD, 0xCF44, 0xFDDF, 0xEC56, 0x98E9, 0x8960, 0xBBFB, 0xAA72,
        0x6306, 0x728F, 0x4014, 0x519D, 0x2522, 0x34AB, 0x0630, 0x17B9, 0xEF4E, 0xFEC7, 0xCC5C, 0xDDD5, 0xA96A, 0xB8E3, 0x8A78, 0x9BF1,
        0x7387, 0x620E, 0x5095, 0x411C, 0x35A3, 0x242A, 0x16B1, 0x0738, 0xFFCF, 0xEE46, 0xDCDD, 0xCD54, 0xB9EB, 0xA862, 0x9AF9, 0x8B70,
        0x8408, 0x9581, 0xA71A, 0xB693, 0xC22C, 0xD3A5, 0xE13E, 0xF0B7, 0x0840, 0x19C9, 0x2B52, 0x3ADB, 0x4E64, 0x5FED, 0x6D76, 0x7CFF,
        0x9489, 0x8500, 0xB79B, 0xA612, 0xD2AD, 0xC324, 0xF1BF, 0xE036, 0x18C1, 0x0948, 0x3BD3, 0x2A5A, 0x5EE5, 0x4F6C, 0x7DF7, 0x6C7E,
        0xA50A, 0xB483, 0x8618, 0x9791, 0xE32E, 0xF2A7, 0xC03C, 0xD1B5, 0x2942, 0x38CB, 0x0A50, 0x1BD9, 0x6F66, 0x7EEF, 0x4C74, 0x5DFD,
        0xB58B, 0xA402, 0x9699, 0x8710, 0xF3AF, 0xE226, 0xD0BD, 0xC134, 0x39C3, 0x284A, 0x1AD1, 0x0B58, 0x7FE7, 0x6E6E, 0x5CF5, 0x4D7C,
        0xC60C, 0xD785, 0xE51E, 0xF497, 0x8028, 0x91A1, 0xA33A, 0xB2B3, 0x4A44, 0x5BCD, 0x6956, 0x78DF, 0x0C60, 0x1DE9, 0x2F72, 0x3EFB,
        0xD68D, 0xC704, 0xF59F, 0xE416, 0x90A9, 0x8120, 0xB3BB, 0xA232, 0x5AC5, 0x4B4C, 0x79D7, 0x685E, 0x1CE1, 0x0D68, 0x3FF3, 0x2E7A,
        0xE70E, 0xF687, 0xC41C, 0xD595, 0xA12A, 0xB0A3, 0x8238, 0x93B1, 0x6B46, 0x7ACF, 0x4854, 0x59DD, 0x2D62, 0x3CEB, 0x0E70, 0x1FF9,
        0xF78F, 0xE606, 0xD49D, 0xC514, 0xB1AB, 0xA022, 0x92B9, 0x8330, 0x7BC7, 0x6A4E, 0x58D5, 0x495C, 0x3DE3, 0x2C6A, 0x1EF1, 0x0F78,
    };
    uint8_t pos;            		// Calculated position in the CRC table
    uint16_t crc = start ^ 0xffff;	// Calculated CRC checksum
    
    // Calculate CRC checksum
    while(len--) {
        pos = (uint8_t) *bytes++ ^ crc;
        crc >>= 8;
        crc ^= crc_table[pos];
    }
    
    return crc^0xffff;  // Return CRC checksum with swapped bytes
}
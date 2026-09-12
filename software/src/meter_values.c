// SML measurement storage and derived SDM630 values, shared by MCU and host tests.
#include "modbus_rtu.h"
#include "config.h"
#include "systick.h"

#ifndef CFG_REACTIVE_SIGN
#define CFG_REACTIVE_SIGN 1
#endif
#if CFG_REACTIVE_SIGN != 1 && CFG_REACTIVE_SIGN != -1
#error CFG_REACTIVE_SIGN must be 1 or -1
#endif

// Array storing the immutable OBIS-to-SDM630 mapping in code flash.
__code const MODBUS_REGISTER_MAPPING modbus_registers[MODBUS_VALUE_COUNT] = {
    {{0x01, 0x00, 0x01, 0x08, 0x00, 0xFF}, 72}, // Active Energy + T0
    {{0x01, 0x00, 0x02, 0x08, 0x00, 0xFF}, 74}, // Active Energy - T0
    {{0x01, 0x00, 0x10, 0x07, 0x00, 0xFF}, 52}, // Active Power Total
    {{0x01, 0x00, 0x24, 0x07, 0x00, 0xFF}, 12}, // Active Power L1
    {{0x01, 0x00, 0x38, 0x07, 0x00, 0xFF}, 14}, // Active Power L2
    {{0x01, 0x00, 0x4C, 0x07, 0x00, 0xFF}, 16}, // Active Power L3
    {{0x01, 0x00, 0x20, 0x07, 0x00, 0xFF},  0}, // Voltage L1
    {{0x01, 0x00, 0x34, 0x07, 0x00, 0xFF},  2}, // Voltage L2
    {{0x01, 0x00, 0x48, 0x07, 0x00, 0xFF},  4}, // Voltage L3
    {{0x01, 0x00, 0x1F, 0x07, 0x00, 0xFF},  6}, // Current L1
    {{0x01, 0x00, 0x33, 0x07, 0x00, 0xFF},  8}, // Current L2
    {{0x01, 0x00, 0x47, 0x07, 0x00, 0xFF}, 10}, // Current L3
    {{0x01, 0x00, 0x0E, 0x07, 0x00, 0xFF}, 70}, // Frequency
    {{1, 0, 81, 7, 4, 255}, MODBUS_INTERNAL_ONLY},
    {{1, 0, 81, 7, 15, 255}, MODBUS_INTERNAL_ONLY},
    {{1, 0, 81, 7, 26, 255}, MODBUS_INTERNAL_ONLY},
    {{0}, 18}, {{0}, 20}, {{0}, 22}, // Apparent power L1..L3
    {{0}, 24}, {{0}, 26}, {{0}, 28}, // Reactive power L1..L3
    {{0}, 30}, {{0}, 32}, {{0}, 34}, // Signed power factor L1..L3
    {{0}, 56}, {{0}, 60}            // Total apparent/reactive power
};

__xdata float modbus_values[2][MODBUS_VALUE_COUNT];
volatile __xdata uint8_t modbus_active_buffer = 0;
static __xdata uint8_t modbus_staging_buffer = 1;
static __xdata uint16_t present;
static __xdata uint32_t published_ms;
static __xdata uint8_t values_valid;

uint8_t modbus_values_fresh(void) {
    if (values_valid && MS_ELAPSED(published_ms) > CFG_MODBUS_MAX_AGE_MS)
        values_valid = 0;
    return values_valid;
}

// round(32767 * sin(degrees)), 0..90 degrees. 182 bytes in flash, no libm.
// EFR reports whole degrees; quadrant folding covers the full circle.
static __code const uint16_t sine_q15[91] = {
    0, 572, 1144, 1715, 2286, 2856, 3425, 3993, 4560, 5126,
    5690, 6252, 6813, 7371, 7927, 8481, 9032, 9580, 10126, 10668,
    11207, 11743, 12275, 12803, 13328, 13848, 14364, 14876, 15383, 15886,
    16383, 16876, 17364, 17846, 18323, 18794, 19260, 19720, 20173, 20621,
    21062, 21497, 21925, 22347, 22762, 23170, 23571, 23964, 24351, 24730,
    25101, 25465, 25821, 26169, 26509, 26841, 27165, 27481, 27788, 28087,
    28377, 28659, 28932, 29196, 29451, 29697, 29934, 30162, 30381, 30591,
    30791, 30982, 31163, 31335, 31498, 31650, 31794, 31927, 32051, 32165,
    32269, 32364, 32448, 32523, 32587, 32642, 32687, 32722, 32747, 32762,
    32767
};

static int16_t sine_degrees(uint16_t angle) {
    uint8_t negative = angle >= 180;
    if (negative) angle -= 180;
    if (angle > 90) angle = 180 - angle;
    return negative ? -(int16_t)sine_q15[angle] : (int16_t)sine_q15[angle];
}

void modbus_staging_begin(void) {
    modbus_staging_buffer = modbus_active_buffer ^ 1;
    present = 0;
    for (uint8_t i = 0; i < MODBUS_VALUE_COUNT; i++)
        modbus_values[modbus_staging_buffer][i] = 0.0f;
}

void modbus_staging_write(uint8_t mapping_index, float value) {
    if (mapping_index < SUPPORTED_OBIS_CODES_NUMBER) {
        modbus_values[modbus_staging_buffer][mapping_index] = value;
        present |= (uint16_t)1 << mapping_index;
    }
}

// Called once per CRC-valid telegram, never during a Modbus response.
// Only derive from fields present in this telegram. Missing inputs remain zero.
void modbus_staging_commit(void) {
    float __xdata *v = modbus_values[modbus_staging_buffer];
    uint8_t apparent_complete = 1;
    uint8_t reactive_complete = 1;
    // Angles alone (or no supported fields) cannot renew measurement freshness.
    if (!(present & 0x1fff)) return;
    for (uint8_t phase = 0; phase < 3; phase++) {
        const uint16_t vi_mask = ((uint16_t)1 << (6 + phase)) |
                                 ((uint16_t)1 << (9 + phase));
        if ((present & vi_mask) == vi_mask &&
            v[6 + phase] >= 0.0f && v[9 + phase] >= 0.0f) {
            float s = v[6 + phase] * v[9 + phase];
            v[MODBUS_APPARENT_FIRST + phase] = s;
            v[MODBUS_APPARENT_TOTAL] += s;
            if (s > 0.0f && (present & ((uint16_t)1 << (3 + phase)))) {
                float pf = v[3 + phase] / s;
                // Rounded or asynchronously sampled meter values may exceed unity.
                if (pf > 1.0f) pf = 1.0f;
                if (pf < -1.0f) pf = -1.0f;
                v[MODBUS_PF_FIRST + phase] = pf;
            }
            if ((present & ((uint16_t)1 << (MODBUS_ANGLE_FIRST + phase))) &&
                v[MODBUS_ANGLE_FIRST + phase] >= 0.0f &&
                v[MODBUS_ANGLE_FIRST + phase] <= 360.0f) {
                uint16_t angle = (uint16_t)(v[MODBUS_ANGLE_FIRST + phase] + 0.5f);
                if (angle == 360) angle = 0;
                v[MODBUS_REACTIVE_FIRST + phase] =
                    s * ((float)sine_degrees(angle) * (CFG_REACTIVE_SIGN / 32767.0f));
                v[MODBUS_REACTIVE_TOTAL] += v[MODBUS_REACTIVE_FIRST + phase];
            } else {
                reactive_complete = 0;
            }
        } else {
            apparent_complete = 0;
            reactive_complete = 0;
        }
    }
    // Avoid presenting a partial sum as a three-phase total.
    if (!apparent_complete) v[MODBUS_APPARENT_TOTAL] = 0.0f;
    if (!reactive_complete) v[MODBUS_REACTIVE_TOTAL] = 0.0f;
    modbus_active_buffer = modbus_staging_buffer;
    published_ms = millis();
    values_valid = 1;
}

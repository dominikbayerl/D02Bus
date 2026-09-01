// ===================================================================================
// SML Smart-Meter Telegram Parser Port for CH552 (Simplified)
// ===================================================================================

#include "sml.h"
#include "config.h"
#include "system.h"
#include "systick.h"
#include "gpio.h"
#include "cdc_debug.h"

// SML RX buffer in XRAM
volatile __xdata uint8_t sml_rx_buffer[SML_RX_BUFFER_SIZE];
volatile __xdata uint8_t sml_rx_number = 0;
volatile __xdata uint8_t sml_rx_head = 0;
volatile __xdata uint32_t sml_rx_last_time = 0;

// Parser state
enum SML_STATE { SML_SCAN, SML_WAIT_TLV };
static enum SML_STATE sml_state = SML_SCAN;
static uint8_t obis_match_buffer[6];
static uint8_t current_obis_id = 0;
// A meter may report import/export energy as individual tariff registers
// (1-0:1.8.1, 1-0:1.8.2, ...) instead of the all-tariff register 1-0:1.8.0.
// Keep track of an explicitly supplied total so individual tariffs never
// overwrite it when both forms occur in the same telegram.
static uint16_t sml_energy_total_seen = 0;
static float sml_energy_tariff_sum[2];

enum SML_TRANSPORT_STATE { SML_T_SEARCH, SML_T_DATA, SML_T_FILL, SML_T_CRC_HI, SML_T_CRC_LO };
static enum SML_TRANSPORT_STATE sml_transport_state = SML_T_SEARCH;
static uint8_t sml_start_match = 0;
static uint8_t sml_escape_count = 0;
static uint8_t sml_crc_hi = 0;
static uint16_t sml_transport_crc = 0xffff;

#define SML_STATUS_BLINK_MS 50

static uint8_t sml_status_pulses = 0;
static uint8_t sml_status_led_on = 0;
static uint32_t sml_status_next_ms = 0;

static __code const uint8_t sml_start_sequence[8] = {
    0x1b, 0x1b, 0x1b, 0x1b, 0x01, 0x01, 0x01, 0x01
};

#ifdef HOST_TEST
uint16_t sml_test_crc_ok = 0;
uint16_t sml_test_crc_bad = 0;
#endif

static uint16_t sml_crc_update(uint16_t crc, uint8_t byte) {
    crc ^= byte;
    for (uint8_t i = 0; i < 8; i++) {
        crc = (crc & 1) ? ((crc >> 1) ^ 0x8408) : (crc >> 1);
    }
    return crc;
}

static uint16_t sml_crc_finish(uint16_t crc) {
    crc ^= 0xffff;
    return (uint16_t)((crc << 8) | (crc >> 8));
}

// D3 is the red status LED (PIN_LED0 / P3.4).  The pattern is non-blocking so
// SML reception continues while the result is being indicated.
static void sml_status_blink(uint8_t count) {
    sml_status_pulses = count;
    sml_status_led_on = 0;
    sml_status_next_ms = millis();
    PIN_low(PIN_LED0);
}

void sml_status_update(void) {
    const uint32_t now = millis();

    if (!sml_status_pulses || (uint32_t)(now - sml_status_next_ms) >= 0x80000000UL) {
        return;
    }

    if (sml_status_led_on) {
        PIN_low(PIN_LED0);
        sml_status_led_on = 0;
        sml_status_pulses--;
    } else {
        PIN_high(PIN_LED0);
        sml_status_led_on = 1;
    }
    sml_status_next_ms = now + SML_STATUS_BLINK_MS;
}

static void sml_parser_reset(void) {
    sml_state = SML_SCAN;
    sml_energy_total_seen = 0;
    sml_energy_tariff_sum[0] = 0.0f;
    sml_energy_tariff_sum[1] = 0.0f;
    for (uint8_t i = 0; i < 6; i++) obis_match_buffer[i] = 0;
}

static uint8_t sml_obis_matches(uint8_t mapping_index) {
    for (uint8_t j = 0; j < 6; j++) {
        if (modbus_registers[mapping_index].obis[j] != obis_match_buffer[j]) {
            // Energy mappings ending in tariff 0 also accept tariff 1..8.
            // Their values are accumulated below, unless tariff 0 (the
            // meter-provided all-tariff total) is available.
            if ((mapping_index == 0 || mapping_index == 1) && j == 4 &&
                modbus_registers[mapping_index].obis[4] == 0 &&
                obis_match_buffer[4] >= 1 && obis_match_buffer[4] <= 8) {
                continue;
            }
            // 1-0:1.7.0 is the import-power-only counterpart to the signed
            // total active power OBIS 1-0:16.7.0.  EMH eHZ meters commonly
            // provide the former.
            if (mapping_index == 2 && j == 2 &&
                modbus_registers[mapping_index].obis[2] == 0x10 &&
                obis_match_buffer[2] == 0x01) {
                continue;
            }
            return 0;
        }
    }
    return 1;
}

static void sml_transport_reset(void) {
    sml_transport_state = SML_T_SEARCH;
    sml_start_match = 0;
    sml_escape_count = 0;
    sml_parser_reset();
}

static void sml_transport_start(void) {
    sml_transport_crc = 0xffff;
    for (uint8_t i = 0; i < sizeof(sml_start_sequence); i++) {
        sml_transport_crc = sml_crc_update(sml_transport_crc, sml_start_sequence[i]);
    }
    sml_transport_state = SML_T_DATA;
    sml_escape_count = 0;
    sml_start_match = 0;
    sml_parser_reset();
    modbus_staging_begin();
}

static void sml_transport_search(uint8_t byte) {
    if (byte == sml_start_sequence[sml_start_match]) {
        sml_start_match++;
        if (sml_start_match == sizeof(sml_start_sequence)) sml_transport_start();
    } else {
        sml_start_match = (byte == 0x1b) ? 1 : 0;
    }
}

// Observe every raw transport byte. Values decoded while SML_T_DATA is active
// are written to the inactive Modbus bank. Only a valid transport CRC publishes
// that bank, so truncated and corrupt frames cannot replace live readings.
static void sml_transport_observe(uint8_t byte) {
    switch (sml_transport_state) {
        case SML_T_SEARCH:
            sml_transport_search(byte);
            break;

        case SML_T_DATA:
            sml_transport_crc = sml_crc_update(sml_transport_crc, byte);
            if (byte == 0x1b) {
                if (++sml_escape_count == 8) sml_escape_count = 0; // escaped 1b x4
            } else if (sml_escape_count == 4 && byte == 0x1a) {
                sml_escape_count = 0;
                sml_transport_state = SML_T_FILL;
            } else if (sml_escape_count >= 4) {
                // Invalid escape. Preserve a possible new start sequence.
                sml_transport_state = SML_T_SEARCH;
                sml_start_match = (byte == 0x01) ? 5 : 0;
                sml_escape_count = 0;
#ifdef HOST_TEST
                sml_test_crc_bad++;
#endif
            } else {
                sml_escape_count = 0;
            }
            break;

        case SML_T_FILL:
            if (byte > 3) {
                sml_transport_reset();
#ifdef HOST_TEST
                sml_test_crc_bad++;
#endif
            } else {
                sml_transport_crc = sml_crc_update(sml_transport_crc, byte);
                sml_transport_state = SML_T_CRC_HI;
            }
            break;

        case SML_T_CRC_HI:
            sml_crc_hi = byte;
            sml_transport_state = SML_T_CRC_LO;
            break;

        case SML_T_CRC_LO:
#ifdef HOST_TEST
            printf("Transport CRC received=%04X calculated=%04X\n",
                   ((uint16_t)sml_crc_hi << 8) | byte, sml_crc_finish(sml_transport_crc));
#endif
            if ((((uint16_t)sml_crc_hi << 8) | byte) == sml_crc_finish(sml_transport_crc)) {
                modbus_staging_commit();
                sml_status_blink(1);
#ifdef HOST_TEST
                sml_test_crc_ok++;
#endif
            } else {
                sml_status_blink(2);
#ifdef HOST_TEST
                sml_test_crc_bad++;
#endif
            }
            sml_transport_reset();
            break;
    }
}



// Configure UART1 for SML (9600 8N1 default or config.h value)
void sml_init(void) {
    // Select UART1's normal RXD1/TXD1 pins (P1.6/P1.7).  Do not depend on
    // the bootloader/reset state of the alternate-pin selection bit, because
    // its alternate RX pin is P3.4, which is also D3 on this board.
#ifndef HOST_TEST
    PIN_FUNC &= ~bUART1_PIN_X;
#endif
    // RXD1 is actively driven by U3 (SN74LVC2G14).  Use the true input-only
    // mode here; the quasi-bidirectional/pull-up mode can load or briefly drive
    // this net and is unnecessary for an actively driven UART signal.
    PIN_input(P16);
    PIN_output(P17);
    
    U1SM0  = 0;
    U1SMOD = 1;
    // Keep the receiver stopped until sml_rx_enable().  Otherwise it runs
    // throughout the blocking startup indication with RI unserviced, leaving
    // SBUF1/RB8 in an overrun/stale state before interrupts are enabled.
    U1REN  = 0;
    SBAUD1 = (uint8_t)(256 - (((F_CPU / 8 / CFG_SML_BAUD) + 1) / 2));
    // UART1 TX is unused.  Leaving U1TI set while IE_UART1 is enabled creates
    // a permanently pending shared UART1 interrupt and starves the main loop.
    U1TI   = 0;
}

// Enable SML reception & interrupts
void sml_rx_enable(void) {
    IE_UART1 = 0;
    U1REN = 0;
    U1RI = 0;
    sml_rx_number = 0;
    sml_rx_head = 0;
    sml_transport_reset();
    sml_rx_last_time = millis();
    // UART1 has only a single receive register; prioritize it over the timer
    // and Modbus UART so back-to-back SML bytes cannot be lost.
#ifndef HOST_TEST
    IP_EX |= bIP_UART1;
#endif
    U1REN = 1;
    IE_UART1 = 1;
}

// Disable SML reception & interrupts
void sml_rx_disable(void) {
    IE_UART1 = 0;
    U1REN = 0;
    U1RI = 0;
    sml_transport_reset();
}

void sml_rx_put_byte(uint8_t value) {
    if (sml_rx_number < SML_RX_BUFFER_SIZE) {
        uint8_t tail = (sml_rx_head + sml_rx_number) & (SML_RX_BUFFER_SIZE - 1);
        sml_rx_buffer[tail] = value;
        sml_rx_number++;
    }
}

// Get byte from SML buffer atomically
uint8_t sml_rx_get_byte(void) {
    uint8_t val = 0;
    IE_UART1 = 0;
    if (sml_rx_number > 0) {
        val = sml_rx_buffer[sml_rx_head];
        sml_rx_head = (sml_rx_head + 1) & (SML_RX_BUFFER_SIZE - 1);
        sml_rx_number--;
    }
    IE_UART1 = 1;
    sml_transport_observe(val);
    return val;
}

// Read byte from SML buffer without deleting it atomically
uint8_t sml_rx_read_byte(void) {
    uint8_t val = 0;
    IE_UART1 = 0;
    if (sml_rx_number > 0) {
        val = sml_rx_buffer[sml_rx_head];
    }
    IE_UART1 = 1;
    return val;
}

// Get bytes from SML buffer atomically
void sml_rx_get_bytes(uint8_t *storage, uint8_t number) {
    if (number > sml_rx_number) {
        number = sml_rx_number;
    }
    for (uint8_t i = 0; i < number; i++) {
        storage[i] = sml_rx_get_byte();
    }
}

// Read bytes from SML buffer without deleting atomically
void sml_rx_read_bytes(uint8_t *storage, uint8_t number) {
    IE_UART1 = 0;
    if (number > sml_rx_number) {
        number = sml_rx_number;
    }
    for (uint8_t i = 0; i < number; i++) {
        storage[i] = sml_rx_buffer[(sml_rx_head + i) & (SML_RX_BUFFER_SIZE - 1)];
    }
    IE_UART1 = 1;
}

// Return count of available bytes in SML buffer (checks for timeout first)
uint8_t sml_rx_avail(void) {
    IE_UART1 = 0;
    if (sml_rx_number > 0) {
        if (MS_ELAPSED(sml_rx_last_time) >= SML_RX_TIMEOUT_MS) {
            sml_rx_number = 0;
            sml_rx_head = 0;
            sml_transport_reset();
        }
    }
    uint8_t temp = sml_rx_number;
    IE_UART1 = 1;
    return temp;
}

// Clear SML buffer atomically
void sml_rx_delete(void) {
    IE_UART1 = 0;
    sml_rx_number = 0;
    sml_rx_head = 0;
    IE_UART1 = 1;
    sml_transport_reset();
}



// Stub for compile-time baud setup
void sml_baud_set(void) {
    SBAUD1 = (uint8_t)(256 - (((F_CPU / 8 / CFG_SML_BAUD) + 1) / 2));
}

// Wait until enough bytes are received or timeout
uint8_t sml_analyse_wait(const uint8_t count) {
    uint32_t start = millis();
    while (sml_rx_number < count) {
        if (MS_ELAPSED(start) >= SML_RX_TIMEOUT_MS) {
            sml_rx_number = 0;
            sml_rx_head = 0;
            sml_transport_reset();
            return 1;
        }
    }
    return 0;
}

uint8_t sml_skip_element(void) {
    if (sml_analyse_wait(1)) return 1;
    uint8_t header = sml_rx_get_byte();
    if (header == 0x00) return 0;
    
    uint8_t type = (header >> 4) & 0x07;
    uint16_t len = header & 0x0F;
    uint8_t header_len = 1;
    
    if (header & 0x80) {
        if (sml_analyse_wait(1)) return 1;
        uint8_t header2 = sml_rx_get_byte();
        len = (len << 4) | (header2 & 0x0F);
        header_len = 2;
    }
    
    if (type == 7) {
        for (uint16_t i = 0; i < len; i++) {
            if (sml_skip_element()) return 1;
        }
    } else {
        if (len >= header_len) {
            uint16_t payload_len = len - header_len;
            if (sml_analyse_wait(payload_len)) return 1;
            for (uint16_t i = 0; i < payload_len; i++) {
                sml_rx_get_byte();
            }
        }
    }
    return 0;
}

uint8_t sml_read_integer(int8_t *val) {
    if (sml_analyse_wait(1)) return 1;
    uint8_t header = sml_rx_get_byte();
    if (header == 0x01) {
        *val = 0;
        return 0;
    }
    
    uint8_t type = (header >> 4) & 0x07;
    uint8_t len = header & 0x0F;
    
    if (header & 0x80) return 1; // extended not expected
    
    uint8_t payload_len = len > 0 ? len - 1 : 0;
    if (payload_len == 0) {
        *val = 0;
        return 0;
    }
    
    if (sml_analyse_wait(payload_len)) return 1;

    if (type != 5 || payload_len != 1) {
        for (uint8_t i = 0; i < payload_len; i++) {
            (void)sml_rx_get_byte();
        }
        *val = 0;
        return 0;
    }

    *val = (int8_t)sml_rx_get_byte();
    return 0;
}

uint8_t sml_read_value(float *val_out, int8_t scaler) {
    if (sml_analyse_wait(1)) return 1;
    uint8_t header = sml_rx_get_byte();
    if (header == 0x01) {
        *val_out = 0.0f;
        return 0;
    }
    
    uint8_t type = (header >> 4) & 0x07;
    uint16_t len = header & 0x0F;
    uint8_t header_len = 1;
    
    if (header & 0x80) {
        if (sml_analyse_wait(1)) return 1;
        uint8_t header2 = sml_rx_get_byte();
        len = (len << 4) | (header2 & 0x0F);
        header_len = 2;
    }
    
    if (type == 7) return 1; // value cannot be a list
    
    uint16_t payload_len = len >= header_len ? len - header_len : 0;
    if (sml_analyse_wait(payload_len)) return 1;
    
    if (payload_len == 0 || payload_len > 8) {
        for (uint16_t i = 0; i < payload_len; i++) sml_rx_get_byte();
        return 1;
    }

    uint8_t payload[8];
    for (uint16_t i = 0; i < payload_len; i++) {
        payload[i] = sml_rx_get_byte();
    }
    
    if (type == 5 || type == 6) { // integer or unsigned
        uint64_t val64 = 0;
        uint8_t val_sign = (type == 5 && (payload[0] & 0x80)) ? 1 : 0;
        
        for (uint16_t i = 0; i < payload_len; i++) {
            val64 = (val64 << 8) | payload[i];
        }
        
        float f_val;
        if (val_sign) {
            uint8_t bits = payload_len * 8;
            if (bits < 64) val64 |= (~(uint64_t)0) << bits;
            f_val = (float)(int64_t)val64;
        } else {
            f_val = (float)val64;
        }
        
        if (f_val != 0.0f && scaler) {
            if (scaler > 0) {
                for (int8_t i = 0; i < scaler; i++) f_val *= 10.0f;
            } else {
                for (int8_t i = 0; i > scaler; i--) f_val /= 10.0f;
            }
        }
        
        *val_out = f_val;
        return 0;
    }
    
    return 1;
}

// SML Parser state machine (Simplified)
void sml_analyse(void) {
    while (sml_rx_avail() > 0) {
        uint8_t b = sml_rx_get_byte();
        
        // Shift buffer
        for(uint8_t i = 0; i < 5; i++) {
            obis_match_buffer[i] = obis_match_buffer[i+1];
        }
        obis_match_buffer[5] = b;
        
        for (uint8_t i = 0; i < SUPPORTED_OBIS_CODES_NUMBER; i++) {
            if (sml_obis_matches(i)) {
#ifdef HOST_TEST
                printf("OBIS Match! ID: %d, Sequence: %02X %02X %02X %02X %02X %02X\n", 
                    i, obis_match_buffer[0], obis_match_buffer[1], obis_match_buffer[2], 
                    obis_match_buffer[3], obis_match_buffer[4], obis_match_buffer[5]);
#endif
                current_obis_id = i;
                
                // Skip status
                if (sml_skip_element()) return;
                // Skip valTime
                if (sml_skip_element()) return;
                // Skip unit
                if (sml_skip_element()) return;
                
                // Read scaler
                int8_t scaler = 0;
                if (sml_read_integer(&scaler)) return;
                
#ifdef HOST_TEST
                printf("Scaler parsed: %d\n", scaler);
#endif

                // Read value
                float value = 0.0f;
                if (sml_read_value(&value, scaler)) return;

                // Convert Energy to kWh (SDM630 expectation for regs 72 and 74)
                if (modbus_registers[current_obis_id].modbus_register == 72 || 
                    modbus_registers[current_obis_id].modbus_register == 74) {
                    value /= 1000.0f;
                }
                
#ifdef HOST_TEST
                printf("Value parsed: %f\n", value);
#endif
                
                if (sml_transport_state == SML_T_DATA) {
                    if (current_obis_id == 0 || current_obis_id == 1) {
                        const uint16_t energy_mask = (uint16_t)1 << current_obis_id;
                        if (obis_match_buffer[4] == 0) {
                            sml_energy_total_seen |= energy_mask;
                            sml_energy_tariff_sum[current_obis_id] = value;
                        } else if (sml_energy_total_seen & energy_mask) {
                            // The explicit all-tariff reading is authoritative.
                            for(uint8_t k = 0; k < 6; k++) obis_match_buffer[k] = 0;
                            return;
                        } else {
                            sml_energy_tariff_sum[current_obis_id] += value;
                            value = sml_energy_tariff_sum[current_obis_id];
                        }
                    }
                    modbus_staging_write(current_obis_id, value);
                    cdc_debug_obis_value(obis_match_buffer, value);
                }
                
                // Reset match buffer to avoid double matching
                for(uint8_t k = 0; k < 6; k++) obis_match_buffer[k] = 0;
                return;
            }
        }
    }
}


// Removed uart1_isr, defined in main.c

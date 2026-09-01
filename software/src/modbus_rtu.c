// ===================================================================================
// Modbus RTU Slave Protocol Port for CH552
// ===================================================================================

#include "modbus_rtu.h"
#include "config.h"
#include "system.h"
#include "systick.h"
#include "gpio.h"

// Modbus RX buffer in XRAM
volatile __xdata uint8_t modbus_rx_buffer[MODBUS_RX_BUFFER_SIZE];
volatile __xdata uint8_t modbus_rx_number = 0;
volatile __xdata uint8_t modbus_frame_available = 0;
volatile __xdata uint32_t modbus_rx_last_time = 0;

// The legacy delay implementation waits on the touch-key peripheral, which
// this firmware does not configure.  Use the running Timer2 millisecond tick
// so UART1 interrupts continue filling the SML ring during Modbus guard times.
static void modbus_wait_ms(uint16_t delay_ms) {
    const uint32_t start = millis();
    while (MS_ELAPSED(start) < delay_ms) {}
}

// Array storing the immutable OBIS-to-SDM630 mapping in code flash.
__code const MODBUS_REGISTER_MAPPING modbus_registers[SUPPORTED_OBIS_CODES_NUMBER] = {
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
    {{0x01, 0x00, 0x0E, 0x07, 0x00, 0xFF}, 70}  // Frequency
};

__xdata float modbus_values[2][SUPPORTED_OBIS_CODES_NUMBER];
volatile __xdata uint8_t modbus_active_buffer = 0;
static __xdata uint8_t modbus_staging_buffer = 1;

void modbus_staging_begin(void) {
    modbus_staging_buffer = modbus_active_buffer ^ 1;
    for (uint8_t i = 0; i < SUPPORTED_OBIS_CODES_NUMBER; i++) {
        modbus_values[modbus_staging_buffer][i] = 0.0f;
    }
}

void modbus_staging_write(uint8_t mapping_index, float value) {
    if (mapping_index < SUPPORTED_OBIS_CODES_NUMBER) {
        modbus_values[modbus_staging_buffer][mapping_index] = value;
    }
}

void modbus_staging_commit(void) {
    modbus_active_buffer = modbus_staging_buffer;
}

// Modbus CRC-16 Lookup Table in Flash
static __code const uint16_t crc_table[] = {
    0X0000, 0XC0C1, 0XC181, 0X0140, 0XC301, 0X03C0, 0X0280, 0XC241,
    0XC601, 0X06C0, 0X0780, 0XC741, 0X0500, 0XC5C1, 0XC481, 0X0440,
    0XCC01, 0X0CC0, 0X0D80, 0XCD41, 0X0F00, 0XCFC1, 0XCE81, 0X0E40,
    0X0A00, 0XCAC1, 0XCB81, 0X0B40, 0XC901, 0X09C0, 0X0880, 0XC841,
    0XD801, 0X18C0, 0X1980, 0XD941, 0X1B00, 0XDBC1, 0XDA81, 0X1A40,
    0X1E00, 0XDEC1, 0XDF81, 0X1F40, 0XDD01, 0X1DC0, 0X1C80, 0XDC41,
    0X1400, 0XD4C1, 0XD581, 0X1540, 0XD701, 0X17C0, 0X1680, 0XD641,
    0XD201, 0X12C0, 0X1380, 0XD341, 0X1100, 0XD1C1, 0XD081, 0X1040,
    0XF001, 0X30C0, 0X3180, 0XF141, 0X3300, 0XF3C1, 0XF281, 0X3240,
    0X3600, 0XF6C1, 0XF781, 0X3740, 0XF501, 0X35C0, 0X3480, 0XF441,
    0X3C00, 0XFCC1, 0XFD81, 0X3D40, 0XFF01, 0X3FC0, 0X3E80, 0XFE41,
    0XFA01, 0X3AC0, 0X3B80, 0XFB41, 0X3900, 0XF9C1, 0XF881, 0X3840,
    0X2800, 0XE8C1, 0XE981, 0X2940, 0XEB01, 0X2BC0, 0X2A80, 0XEA41,
    0XEE01, 0X2EC0, 0X2F80, 0XEF41, 0X2D00, 0XEDC1, 0XEC81, 0X2C40,
    0XE401, 0X24C0, 0X2580, 0XE541, 0X2700, 0XE7C1, 0XE681, 0X2640,
    0X2200, 0XE2C1, 0XE381, 0X2340, 0XE101, 0X21C0, 0X2080, 0XE041,
    0XA001, 0X60C0, 0X6180, 0XA141, 0X6300, 0XA3C1, 0XA281, 0X6240,
    0X6600, 0XA6C1, 0XA781, 0X6740, 0XA501, 0X65C0, 0X6480, 0XA441,
    0X6C00, 0XACC1, 0XAD81, 0X6D40, 0XAF01, 0X6FC0, 0X6E80, 0XAE41,
    0XAA01, 0X6AC0, 0X6B80, 0XAB41, 0X6900, 0XA9C1, 0XA881, 0X6840,
    0X7800, 0XB8C1, 0XB981, 0X7940, 0XBB01, 0X7BC0, 0X7A80, 0XBA41,
    0XBE01, 0X7EC0, 0X7F80, 0XBF41, 0X7D00, 0XBDC1, 0XBC81, 0X7C40,
    0XB401, 0X74C0, 0X7580, 0XB541, 0X7700, 0XB7C1, 0XB681, 0X7640,
    0X7200, 0XB2C1, 0XB381, 0X7340, 0XB101, 0X71C0, 0X7080, 0XB041,
    0X5000, 0X90C1, 0X9181, 0X5140, 0X9301, 0X53C0, 0X5280, 0X9241,
    0X9601, 0X56C0, 0X5780, 0X9741, 0X5500, 0X95C1, 0X9481, 0X5440,
    0X9C01, 0X5CC0, 0X5D80, 0X9D41, 0X5F00, 0X9FC1, 0X9E81, 0X5E40,
    0X5A00, 0X9AC1, 0X9B81, 0X5B40, 0X9901, 0X59C0, 0X5880, 0X9841,
    0X8801, 0X48C0, 0X4980, 0X8941, 0X4B00, 0X8BC1, 0X8A81, 0X4A40,
    0X4E00, 0X8EC1, 0X8F81, 0X4F40, 0X8D01, 0X4DC0, 0X4C80, 0X8C41,
    0X4400, 0X84C1, 0X8581, 0X4540, 0X8701, 0X47C0, 0X4680, 0X8641,
    0X8201, 0X42C0, 0X4380, 0X8341, 0X4100, 0X81C1, 0X8081, 0X4040
};

// Initialize UART0 for Modbus RTU
void modbus_init(void) {
    // Configure RXD0 (P3.0) and TXD0 (P3.1)
    PIN_input_PU(P30);
    PIN_output(P31);
    
    SM0 = 0;                                  // UART0 8 data bits
    SM1 = 1;                                  // UART0 BAUD rate by timer
    SM2 = 0;                                  // UART0 no multi-device comm
    RCLK = 0;                                 // UART0 receive clock:  TIMER1
    TCLK = 0;                                 // UART0 transmit clock: TIMER1
    PCON |= SMOD;                             // UART0 fast BAUD rate
    TMOD &= ~(bT1_GATE | bT1_CT | MASK_T1_MOD);
    TMOD |= bT1_M1;                           // TIMER1 8-bit auto-reload
    T2MOD |= bTMR_CLK | bT1_CLK;              // TIMER1 fast clock selection
    TH1 = (uint8_t)(256 - (((F_CPU / 8 / CFG_MODBUS_BAUD) + 1) / 2));
    TR1 = 1;                                  // TIMER1 start
    TI  = 0;                                  // UART0 set transmit complete flag clear
    REN = 1;                                  // UART0 receive enable
    ES  = 1;                                  // Enable UART0 interrupt
}

// Enable Modbus RX
void modbus_rx_enable(void) {
    PIN_low(PIN_NRE);
    ES = 1;
    REN = 1;
}

// Disable Modbus RX
void modbus_rx_disable(void) {
    ES = 0;
    REN = 0;
    PIN_high(PIN_NRE);
}

// Get byte(s) from buffer atomically
void modbus_rx_get(uint8_t *storage, uint8_t number) {
    ES = 0; // Enter critical section
    if (number > modbus_rx_number) {
        number = modbus_rx_number;
    }
    for (uint8_t i = 0; i < number; i++) {
        storage[i] = modbus_rx_buffer[i];
    }
    modbus_rx_number -= number;
    for (uint8_t i = 0; i < modbus_rx_number; i++) {
        modbus_rx_buffer[i] = modbus_rx_buffer[i + number];
    }
    modbus_frame_available = 0;
    ES = 1; // Exit critical section
}

// Read byte(s) from buffer without deleting atomically
void modbus_rx_read(uint8_t *storage, uint8_t number) {
    ES = 0;
    if (number > modbus_rx_number) {
        number = modbus_rx_number;
    }
    for (uint8_t i = 0; i < number; i++) {
        storage[i] = modbus_rx_buffer[i];
    }
    ES = 1;
}

// Return count of available bytes in buffer
uint8_t modbus_rx_avail(void) {
    return modbus_rx_number;
}

// Clear Modbus RX buffer atomically
void modbus_rx_delete(void) {
    ES = 0;
    modbus_rx_number = 0;
    ES = 1;
}

// Transmit bytes synchronously over UART0
void modbus_tx_send(const uint8_t *bytes, uint8_t number) {
    PIN_high(PIN_LED0);
    uint8_t rx_stat = REN;
    modbus_rx_disable();
    
    // 3.5 character time delay before transmit
    modbus_wait_ms(MODBUS_T35_MS);
    
    // Enable RS-485 DE driver
    PIN_high(PIN_DE);
    
    while (number--) {
        SBUF = *bytes++;
        while (!TI);
        TI = 0;
    }
    
    // Disable RS-485 DE driver
    PIN_low(PIN_DE);
    
    // 3.5 character time delay after transmit
    modbus_wait_ms(MODBUS_T35_MS);
    
    if (rx_stat) {
        modbus_rx_enable();
    }
    PIN_low(PIN_LED0);
}

// Address configurations (compile-time in port)
void modbus_adr_set(void) {}

uint8_t modbus_adr_get(void) {
    return CFG_MODBUS_ADDRESS;
}

// Baud configuration (compile-time in port)
void modbus_baud_set(void) {}

// Calculate Modbus CRC-16
uint16_t modbus_crc_calc(const uint8_t *bytes, uint8_t number, const uint16_t start) {
    uint8_t pos;
    uint16_t crc = start;
    
    while (number--) {
        pos = (uint8_t)*bytes++ ^ (uint8_t)crc;
        crc >>= 8;
        crc ^= crc_table[pos];
    }
    return crc;
}

// Check if a complete Modbus frame is available (checks for character gap timeout first)
uint8_t modbus_frame_avail(void) {
    ES = 0; // Enter critical section
    if (modbus_rx_number > 0 && !modbus_frame_available) {
        if (MS_ELAPSED(modbus_rx_last_time) >= MODBUS_T35_MS) {
            if (modbus_rx_number < 8) {
                modbus_rx_number = 0; // Frame too short, discard
            } else {
                modbus_frame_available = 1;
            }
        }
    }
    uint8_t temp = modbus_frame_available;
    ES = 1; // Exit critical section
    return temp;
}

// Analyze Modbus frame and send response
uint8_t modbus_frame_send(const uint8_t *bytes, const uint8_t number) {
    uint8_t status = 0;
    
    if (number == 8 && bytes[0] == modbus_adr_get() &&
        !modbus_crc_calc(bytes, number, 0xffff)) {
        if (bytes[1] == 0x03 || bytes[1] == 0x04) { // FC03: Read Holding Regs, FC04: Read Input Regs
            const uint16_t address_first = ((uint16_t)bytes[2] << 8) | bytes[3];
            const uint16_t reg_count = ((uint16_t)bytes[4] << 8) | bytes[5];
            const uint16_t address_last = address_first + reg_count - 1;
            
            const uint16_t data_byte_number = 2 * reg_count;
            static __xdata uint8_t res[60]; // Safe array size in XRAM
            
            if (reg_count != 0 && data_byte_number <= 54 &&
                address_last >= address_first) {
                res[0] = bytes[0];
                res[1] = bytes[1]; // Echo requested Function Code
                res[2] = (uint8_t)data_byte_number;
                
                uint8_t byte_idx = 3;
                const uint8_t value_buffer = modbus_active_buffer;
                
                for (uint16_t reg = address_first; reg <= address_last; reg++) {
                    uint8_t found = 0;
                    for (uint8_t i = 0; i < SUPPORTED_OBIS_CODES_NUMBER; i++) {
                        if (modbus_registers[i].modbus_register == reg) {
                            uint16_t *val_ptr = (uint16_t*)&modbus_values[value_buffer][i];
                            res[byte_idx++] = (uint8_t)(val_ptr[1] >> 8);
                            res[byte_idx++] = (uint8_t)(val_ptr[1]);
                            found = 1;
                            break;
                        } else if (modbus_registers[i].modbus_register + 1 == reg) {
                            uint16_t *val_ptr = (uint16_t*)&modbus_values[value_buffer][i];
                            res[byte_idx++] = (uint8_t)(val_ptr[0] >> 8);
                            res[byte_idx++] = (uint8_t)(val_ptr[0]);
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        res[byte_idx++] = 0;
                        res[byte_idx++] = 0;
                    }
                }
                
                const uint16_t crc = modbus_crc_calc(res, 3 + data_byte_number, 0xffff);
                res[3 + data_byte_number] = (uint8_t)crc;
                res[4 + data_byte_number] = (uint8_t)(crc >> 8);
                
                modbus_tx_send(res, 5 + data_byte_number);
                status = bytes[1];
            } else {
                // Illegal Data Address
                uint8_t response[5] = {bytes[0], bytes[1] | 0x80, 0x02};
                uint16_t crc = modbus_crc_calc(response, 3, 0xffff);
                response[3] = (uint8_t)crc;
                response[4] = (uint8_t)(crc >> 8);
                modbus_tx_send(response, 5);
                status = bytes[1] | 0x80;
            }
        }
        else { // Illegal Function Code
            uint8_t response[5] = {bytes[0], bytes[1] | 0x80, 0x01};
            uint16_t crc = modbus_crc_calc(response, 3, 0xffff);
            response[3] = (uint8_t)crc;
            response[4] = (uint8_t)(crc >> 8);
            modbus_tx_send(response, 5);
            status = 0x01;
        }
    }
    return status;
}

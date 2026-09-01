// ===================================================================================
// Modbus RTU Slave Protocol Port for CH552
// ===================================================================================

#pragma once

#include <stdint.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

// Modbus holding register addresses
#define MODBUS_ADDRESS_MANUFACTURER 8192
#define MODBUS_ADDRESS_DEVICE       8193
#define MODBUS_ADDRESS_HARDWARE     8194
#define MODBUS_ADDRESS_FIRMWARE     8195
#define MODBUS_ADDRESS_INTERVAL     8244
#define MODBUS_ADDRESS_UNIX         8245
#define MODBUS_ADDRESS_INTERVAL_MOD 8249
#define MODBUS_ADDRESS_POWER        8250
#define MODBUS_ADDRESS_PIN          8251
#define MODBUS_ADDRESS_MICID        8252
#define MODBUS_ADDRESS_SERVERID     8257
#define MODBUS_ADDRESS_TEMPERATURE  8262
#define MODBUS_ADDRESS_UPDATE       8263

#define MODBUS_STORAGE          1
#define MODBUS_BUFFER           0

#define MODBUS_RX_BUFFER_SIZE   64
#define MODBUS_TABLE_COUNT      3

extern volatile __xdata uint8_t modbus_rx_buffer[MODBUS_RX_BUFFER_SIZE];
extern volatile __xdata uint8_t modbus_rx_number;
extern volatile __xdata uint8_t modbus_frame_available;
extern volatile __xdata uint32_t modbus_rx_last_time;

#define SUPPORTED_OBIS_CODES_NUMBER 13  // Number of supported OBIS codes

typedef struct {
    uint8_t obis[6];
    uint16_t modbus_register;
} MODBUS_REGISTER_MAPPING;

// Immutable register/OBIS metadata belongs in flash.  Only the two banks of
// live values consume XRAM.  The inactive bank is populated while an SML
// frame is received and becomes visible to Modbus only after its CRC passes.
extern __code const MODBUS_REGISTER_MAPPING modbus_registers[SUPPORTED_OBIS_CODES_NUMBER];
extern __xdata float modbus_values[2][SUPPORTED_OBIS_CODES_NUMBER];
extern volatile __xdata uint8_t modbus_active_buffer;

void modbus_staging_begin(void);
void modbus_staging_write(uint8_t mapping_index, float value);
void modbus_staging_commit(void);

void modbus_init(void);
void modbus_rx_enable(void);
void modbus_rx_disable(void);
void modbus_rx_get(uint8_t *storage, uint8_t number);
void modbus_rx_read(uint8_t *storage, uint8_t number);
uint8_t modbus_rx_avail(void);
void modbus_rx_delete(void);
void modbus_tx_send(const uint8_t *bytes, uint8_t number);
uint8_t modbus_adr_get(void);
void modbus_baud_set(void);
uint16_t modbus_crc_calc(const uint8_t *bytes, uint8_t number, const uint16_t start);
uint8_t modbus_frame_avail(void);
uint8_t modbus_frame_send(const uint8_t *bytes, const uint8_t number);

// ===================================================================================
// SML Smart-Meter Telegram Parser Port for CH552
// ===================================================================================

#pragma once

#include <stdint.h>
#include "modbus_rtu.h"

// Must remain a power of two for the ring mask.
#define SML_RX_BUFFER_SIZE          64

extern volatile __xdata uint8_t sml_rx_buffer[SML_RX_BUFFER_SIZE];
extern volatile __xdata uint8_t sml_rx_number;
extern volatile __xdata uint8_t sml_rx_head;
extern volatile __xdata uint32_t sml_rx_last_time;

// Function prototypes
void sml_init(void);
void sml_rx_enable(void);
void sml_rx_disable(void);
void sml_rx_put_byte(uint8_t value);
uint8_t sml_rx_get_byte(void);
uint8_t sml_rx_read_byte(void);
void sml_rx_get_bytes(uint8_t *storage, uint8_t number);
void sml_rx_read_bytes(uint8_t *storage, uint8_t number);
uint8_t sml_rx_avail(void);
void sml_rx_delete(void);

void sml_baud_set(void);

void sml_analyse(void);

// Advance the non-blocking D3 transport-status blink pattern.
void sml_status_update(void);

#ifdef HOST_TEST
extern uint16_t sml_test_crc_ok;
extern uint16_t sml_test_crc_bad;
#endif

// SPDX-License-Identifier: CC-BY-SA-4.0
#pragma once

#include <stdint.h>


void     uart_port_init(void);
uint8_t  uart_port_available(uint8_t port);
uint8_t  uart_port_read(uint8_t port, uint8_t* out_byte);
uint8_t  uart_port_write(uint8_t port, uint8_t data);
void     uart_port_flush(uint8_t port);
uint16_t uart_port_drop_count(uint8_t port);

void uart0_push_byte(uint8_t data);
void uart1_push_byte(uint8_t data);

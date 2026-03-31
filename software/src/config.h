// SPDX-License-Identifier: CC-BY-SA-4.0
#pragma once

// Only user-configurable UART parameters.
#define UART_D0_BAUD       9600UL
#define UART_RS485_BAUD    9600UL

#if (UART_D0_BAUD == 0UL)
#error "UART_D0_BAUD must be greater than 0"
#endif

#if (UART_RS485_BAUD == 0UL)
#error "UART_RS485_BAUD must be greater than 0"
#endif

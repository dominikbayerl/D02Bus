// SPDX-License-Identifier: CC-BY-SA-4.0
#include "uart_port.h"
#include "system.h"
#include "uart.h"
#include "gpio.h"

#define UART0_RX_BUFFER_SIZE  64u
#define UART1_RX_BUFFER_SIZE  64u

static volatile uint8_t __xdata g_uart0_rx_buf[UART0_RX_BUFFER_SIZE];
static volatile uint8_t __xdata g_uart1_rx_buf[UART1_RX_BUFFER_SIZE];

static volatile uint8_t g_uart0_rx_head;
static volatile uint8_t g_uart0_rx_tail;
static volatile uint8_t g_uart1_rx_head;
static volatile uint8_t g_uart1_rx_tail;
static volatile uint16_t g_uart0_drop_count;
static volatile uint16_t g_uart1_drop_count;

static uint8_t ring_count(uint8_t head, uint8_t tail, uint8_t size) {
  if (head >= tail) {
    return (uint8_t)(head - tail);
  }

  return (uint8_t)(size - tail + head);
}

void uart0_push_byte(uint8_t data) {
  uint8_t next;

  next = (uint8_t)(g_uart0_rx_head + 1u);
  if (next >= UART0_RX_BUFFER_SIZE) {
    next = 0u;
  }

  if (next == g_uart0_rx_tail) {
    ++g_uart0_drop_count;
    return;
  }

  g_uart0_rx_buf[g_uart0_rx_head] = data;
  g_uart0_rx_head = next;
}

void uart1_push_byte(uint8_t data) {
  uint8_t next;

  next = (uint8_t)(g_uart1_rx_head + 1u);
  if (next >= UART1_RX_BUFFER_SIZE) {
    next = 0u;
  }

  if (next == g_uart1_rx_tail) {
    ++g_uart1_drop_count;
    return;
  }

  g_uart1_rx_buf[g_uart1_rx_head] = data;
  g_uart1_rx_head = next;
}

void uart_port_init(void) {
  g_uart0_rx_head = 0u;
  g_uart0_rx_tail = 0u;
  g_uart1_rx_head = 0u;
  g_uart1_rx_tail = 0u;
  g_uart0_drop_count = 0u;
  g_uart1_drop_count = 0u;
}

uint8_t uart_port_available(uint8_t port) {
  uint8_t head = 0, tail = 0;

  if (port == 0) {
    INT_ATOMIC_BLOCK {
      head = g_uart0_rx_head;
      tail = g_uart0_rx_tail;
    }
    return ring_count(head, tail, UART0_RX_BUFFER_SIZE);
  }

  if (port == 1) {
    INT_ATOMIC_BLOCK {
      head = g_uart1_rx_head;
      tail = g_uart1_rx_tail;
    }
    return ring_count(head, tail, UART1_RX_BUFFER_SIZE);
  }

  return 0u;
}

uint8_t uart_port_read(uint8_t port, uint8_t* out_byte) {
  if (out_byte == 0) {
    return 0u;
  }

  if (port == 0) {
    if (g_uart0_rx_head == g_uart0_rx_tail) {
      return 0u;
    }

    INT_ATOMIC_BLOCK {
      *out_byte = g_uart0_rx_buf[g_uart0_rx_tail];
      g_uart0_rx_tail = (uint8_t)(g_uart0_rx_tail + 1u);
      if (g_uart0_rx_tail >= UART0_RX_BUFFER_SIZE) {
        g_uart0_rx_tail = 0u;
      }
    }
    return 1u;
  }

  if (port == 1) {
    if (g_uart1_rx_head == g_uart1_rx_tail) {
      return 0u;
    }

    INT_ATOMIC_BLOCK {
      *out_byte = g_uart1_rx_buf[g_uart1_rx_tail];
      g_uart1_rx_tail = (uint8_t)(g_uart1_rx_tail + 1u);
      if (g_uart1_rx_tail >= UART1_RX_BUFFER_SIZE) {
        g_uart1_rx_tail = 0u;
      }
    }
    return 1u;
  }

  return 0u;
}

uint8_t uart_port_write(uint8_t port, uint8_t data) {
  if (port == 0) {
    UART0_write(data);
    return 1u;
  }

  if (port == 1) {
    UART1_write(data);
    return 1u;
  }

  return 0u;
}

void uart_port_flush(uint8_t port) {
  if (port == 0) {
    INT_ATOMIC_BLOCK {
      g_uart0_rx_tail = g_uart0_rx_head;
    }
    return;
  }

  if (port == 1) {
    INT_ATOMIC_BLOCK {
      g_uart1_rx_tail = g_uart1_rx_head;
    }
  }
}

uint16_t uart_port_drop_count(uint8_t port) {
  uint16_t count;

  count = 0u;
  if (port == 0) {
    INT_ATOMIC_BLOCK {
      count = g_uart0_drop_count;
    }
    return count;
  }

  if (port == 1) {
    INT_ATOMIC_BLOCK {
      count = g_uart1_drop_count;
    }
    return count;
  }

  return 0u;
}


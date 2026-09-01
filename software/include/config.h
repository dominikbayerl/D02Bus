// ===================================================================================
// Project configuration - SML Reading Head CH552 port
// ===================================================================================
//
// All settings are compile-time. The original AVR firmware stored these in EEPROM;
// the CH552 has no general purpose writable EEPROM in this project, so configure
// the values here and rebuild.
//
// Pin assignments must be adapted to the actual PCB. Suggested mapping:
//   UART0 (Modbus/RS485)  : P3.0 = RXD0, P3.1 = TXD0
//   UART1 (SML smartmeter): P1.6 = RXD1, P1.7 = TXD1
//   RS485 /RE             : P1.4 (active low receiver enable)
//   RS485 DE              : P1.5 (active high driver enable)
//   LED Modbus            : P3.4
//   LED SML               : P1.1
// ===================================================================================

#pragma once
#include "ch554.h"
#include "gpio.h"

// ----- Modbus device address (1..247) -----
#ifndef CFG_MODBUS_ADDRESS
#define CFG_MODBUS_ADDRESS      1
#endif

// ----- Modbus serial parameters -----
#ifndef CFG_MODBUS_BAUD
#define CFG_MODBUS_BAUD         9600UL
#endif

// 1 or 2 stop bits (informational only; CH552 UART is 8N1/8N2 by hardware)
#ifndef CFG_MODBUS_STOPBITS
#define CFG_MODBUS_STOPBITS     2
#endif

// ----- SML smartmeter UART baud rate (typ. 9600 8N1) -----
#ifndef CFG_SML_BAUD
#define CFG_SML_BAUD            9600UL
#endif

// ----- Pin assignment -----
#define PIN_LED0                P34
#define PIN_LED1                P11
#define PIN_TXD0                P31
#define PIN_DE                  P15
#define PIN_NRE                 P14

// Identification (written to the modbus information registers at boot)
#define CFG_MANUFACTURER_ID     0x0000
#define CFG_DEVICE_ID           0x0001
#define CFG_HARDWARE_VERSION    0x0221
#define CFG_FIRMWARE_VERSION    0x0300      // 3.0.0 - CH552 port

// Selects which active power source feeds the totals (0 = SML message, 1 = local)
#define CFG_POWER_SOURCE        0

// Measurement interval mode (0 = SML auto, anything else = local override)
#define CFG_INTERVAL_MODE       0
#define CFG_INTERVAL_VALUE      0

// Device serial number (5 x uint16_t)
#define CFG_SERIAL_0            0xC552
#define CFG_SERIAL_1            0x0000
#define CFG_SERIAL_2            0x0000
#define CFG_SERIAL_3            0x0000
#define CFG_SERIAL_4            0x0001

// ----- Inter-frame timeout (3.5 character times rounded up to 1 ms ticks) -----
// At 9600 baud, 11 bit symbol = 1.146 ms and 3.5 symbols = 4.01 ms, which rounds
// up to 5 whole milliseconds. Use ceil(3.5 * 11000 / baud), with a 2 ms floor.
#define MODBUS_T35_MS \
    ((((38500UL + CFG_MODBUS_BAUD - 1UL) / CFG_MODBUS_BAUD) < 2UL) ? 2UL : \
     ((38500UL + CFG_MODBUS_BAUD - 1UL) / CFG_MODBUS_BAUD))

// SML telegram inter-byte timeout in ms. The smartmeter sends bursts; a gap
// longer than this signals end of telegram / desynchronisation.
#define SML_RX_TIMEOUT_MS       50

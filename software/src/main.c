// SPDX-License-Identifier: CC-BY-SA-4.0
#include <stdio.h>
#include <string.h>

#include "delay.h"
#include "gpio.h"
#include "system.h"
#include "uart.h"
#include "uart_port.h"
#include "usb/usb_cdc.h"

// Modbus
#include "modbus.h"
#define MODBUS_PAUSE 5 // ms to wait after last byte received before processing buffer

#define PIN_LED0 P34
#define PIN_LED1 P11
#define PIN_TXD0 P31
#define PIN_DE P15
#define PIN_NRE P14

// 32-bit counter to hold milliseconds since boot
volatile uint32_t millis_count = 0;
volatile char cdc_buf[64];

// modbus
static modbus_register registers[] = {
    { .register_address = 0x0000, .value.u16 = 12345 },
    { .register_address = 0x0001, .value.f32 = 3.14f },
    { .register_address = 0x0002, .value.i16 = -123 },
};

static const modbus_ctx ctx = {
    .device_address = 0x01,
    .registers = registers,
    .register_count = sizeof(registers) / sizeof(registers[0])
};

// Interrupts
void USB_ISR(void) __interrupt(INT_NO_USB) __using(1) {
    USB_interrupt();
}

// void UART0_ISR(void) __interrupt(INT_NO_UART0) __using(1) {
//     if (RI) {
//         RI = 0;
//         uart0_push_byte(SBUF);
//         PIN_toggle(PIN_LED1);
//     }
// }
// void UART1_ISR(void) __interrupt(INT_NO_UART1) __using(1) {
//     if (U1RI) {
//         U1RI = 0;
//         uart1_push_byte(SBUF1);
//         PIN_toggle(PIN_LED1);
//     }
// }

void TIMER2_ISR(void) __interrupt(INT_NO_TMR2) __using(1) {
    if (TF2) {
        TF2 = 0;        // Clear the overflow flag
        millis_count++; // Increment our millisecond counter
    }
}

const char HexLookUp[] = "0123456789abcdef";    
void bytes2hex (unsigned char *src, char *out, int len) {
    while(len--) {
        *out++ = HexLookUp[*src >> 4];
        *out++ = HexLookUp[*src & 0x0F];
        src++;
    }
    *out = 0;
}

unsigned long millis(void) {
    unsigned long m;
    // Critical Section: Disable interrupts while reading a 32-bit
    // value on an 8-bit MCU to prevent "torn reads."
    EA = 0;
    m = millis_count;
    EA = 1;
    return m;
}

void main(void) {
    CLK_config(); // configure system clock

    // configure timer2
    const uint16_t ticks_per_ms = F_CPU / 12000;
    const uint16_t reload_val = 65536 - ticks_per_ms;
    T2CON = 0x00;

    TL2 = RCAP2L = reload_val & 0xFF;
    TH2 = RCAP2H = (reload_val >> 8) & 0xFF;
    ET2 = TR2 = 1; // start timer2

    // modbus
    
    // default pin configuration
    PIN_output(PIN_LED0);
    PIN_output(PIN_LED1);
    PIN_output(PIN_TXD0);
    PIN_output(PIN_DE);
    PIN_output(PIN_NRE);

    uart_port_init();
    UART0_init();
    UART1_init();

    PIN_low(PIN_DE);
    PIN_low(PIN_NRE);

    USB_init();

    // Go!
    INT_enable(); // enable global interrupts

    // disable LEDs
    PIN_low(PIN_LED0);
    PIN_low(PIN_LED1);

    uint32_t last_led = millis();
    uint32_t last_uart0 = millis();

    while (1) {
        if (millis() - last_led >= 1000) { // every 1000ms
            PIN_toggle(PIN_LED0);          // heartbeat LED
            last_led = millis();
        }

        // Modbus
        if (UART0_available()) {
            last_uart0 = millis();
            PIN_toggle(PIN_LED1);

            uart0_push_byte(UART0_read());
        }
        
        if (millis() - last_uart0 >= MODBUS_PAUSE) {
            if (uart_port_available(0) > 0) {
                uint8_t buffer[64];
                size_t length = 0;
                while(uart_port_available(0) && length < sizeof(buffer)) {
                    uart_port_read(0, &buffer[length]);
                    length++;
                }
                CDC_write('<');
                bytes2hex(buffer, cdc_buf, length);
                CDC_print(cdc_buf);
                CDC_write('\n');

                modbus_rc ret = modbus_process_frame(&ctx, buffer, sizeof(buffer), &length);

                CDC_write('=');
                bytes2hex(&ret, cdc_buf, sizeof(ret));
                CDC_print(cdc_buf);
                CDC_write('\n');

                if (ret != MODBUS_SUCCESS) {
                    static const uint8_t exception_frame[] = { 0x01, 0x83, 0x01, 0x02, 0x70, 0x61 };
                    memcpy(buffer, exception_frame, sizeof(exception_frame));
                    length = sizeof(exception_frame);
                }

                CDC_write('>');
                bytes2hex(buffer, cdc_buf, length);
                CDC_print(cdc_buf);
                CDC_write('\n');

                INT_ATOMIC_BLOCK {
                    PIN_high(PIN_NRE);
                    PIN_high(PIN_DE);
                    for (size_t i = 0; i < length; i++) {
                        uart_port_write(0, buffer[i]);
                    }
                    PIN_low(PIN_DE);
                    PIN_low(PIN_NRE);
                }
                
                // if (ret == MODBUS_SUCCESS) {
                //     for (size_t i = 0; i < length; i++) {
                //         uart_port_write(0, buffer[i]);
                //     }
                // }
            }
            uart_port_flush(0);
            last_uart0 = millis();
        }

        // unsigned int avail = uart_port_available(0);
        // if ((avail > 0) && (millis() - last_uart0 >= 100)) {
        //     while (avail-- > 0) {
        //         uint8_t byte = 0;
        //         uart_port_read(0, &byte);
        //         CDC_write(byte);
        //     }
        //     uart_port_flush(0);
        // }

        // D0 Optical
        // if (UART1_available()) {
        //     U1RI = 0;
        //     char byte = UART1_read();
        //     PIN_toggle(PIN_LED1);

        //     uart1_push_byte(byte);
        // }

        // uint8_t buflen = CDC_available();
        // while (buflen--) {
        //     char byte = CDC_read();
        //     CDC_write(byte);
        //     PIN_toggle(PIN_LED1);
        // }
        CDC_flush();
    }
}

// SPDX-License-Identifier: CC-BY-SA-4.0
#include <stdio.h>
#include <string.h>

#include "delay.h"
#include "gpio.h"
#include "system.h"
#include "uart.h"
#include "usb/usb_cdc.h"

// Modbus
#include "modbus.h"
#define MODBUS_PAUSE 5 // ms to wait after last byte received before processing buffer

// SML
#include "sml.h"
#define SML_PAUSE 5 // ms to wait after last byte received before processing buffer

#define PIN_LED0 P34
#define PIN_LED1 P11
#define PIN_TXD0 P31
#define PIN_DE P15
#define PIN_NRE P14

// 32-bit counter to hold milliseconds since boot
volatile uint32_t millis_count = 0;
volatile uint8_t modbus_buf[64] = {0};
volatile __xdata uint8_t sml_buf[512] = {0};
volatile __xdata uint8_t cdc_buf[128] = {0};

// modbus
static modbus_register registers[] = {
    { .register_address =  0, .type = REG_TYPE_FLOAT, .value.f32 = 230.0f },
    { .register_address =  2, .type = REG_TYPE_FLOAT, .value.f32 = 231.0f },
    { .register_address =  4, .type = REG_TYPE_FLOAT, .value.f32 = 232.0f },
};

static const modbus_ctx ctx = {
    .device_address = 0x01,
    .registers = registers,
    .register_count = sizeof(registers) / sizeof(registers[0]),
};

// sml
static __xdata sml_obis_t obis_list[] = {
    { .code = {0x0e, 0x07, 0x00}, .value = NULL },
    { .code = {0x10, 0x07, 0x00}, .value = NULL },
    { .code = {0x1f, 0x07, 0x00}, .value = NULL },
};

static __xdata const sml_ctx_t sml_ctx = {
    .obis_list = obis_list,
    .obis_count = sizeof(obis_list) / sizeof(obis_list[0]),
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

    uint32_t last_led, last_uart0, last_uart1;
    last_led = last_uart0 = last_uart1 = millis();
    size_t uart0_cnt = 0;
    size_t uart1_cnt = 0;

    while (1) {
        if (millis() - last_led >= 1000) { // every 1000ms
            PIN_toggle(PIN_LED0);          // heartbeat LED
            last_led = millis();
        }

        // Modbus
        if (UART0_available()) {
            last_uart0 = millis();
            PIN_toggle(PIN_LED1);

            modbus_buf[uart0_cnt++] = UART0_read();
            if (uart0_cnt >= sizeof(modbus_buf)) {
                uart0_cnt = 0;
            }
        }
        // SML
        if (UART1_available()) {
            last_uart1 = millis();
            PIN_toggle(PIN_LED1);

            sml_buf[uart1_cnt++] = UART1_read();
            if (uart1_cnt >= sizeof(sml_buf)) {
                uart1_cnt = 0;
            }

            CDC_write(HexLookUp[SBUF1 >> 4]);
            CDC_write(HexLookUp[SBUF1 & 0x0F]);
        }
        
        if (uart0_cnt > 0 && (millis() - last_uart0 >= MODBUS_PAUSE)) {
            CDC_write('<');
            bytes2hex(modbus_buf, cdc_buf, uart0_cnt);
            CDC_print(cdc_buf);
            CDC_write('\n');

            modbus_rc ret = modbus_process_frame(&ctx, modbus_buf, 64, &uart0_cnt);

            CDC_write('=');
            bytes2hex(&ret, cdc_buf, sizeof(ret));
            CDC_print(cdc_buf);
            CDC_write('\n');

            if (ret != MODBUS_SUCCESS) {
                size_t ptr = 0;
                modbus_buf[ptr++] = ctx.device_address;
                modbus_buf[ptr++] = modbus_buf[1] | 0x80;
                modbus_buf[ptr++] = 1;
                modbus_buf[ptr++] = (uint8_t)ret; // Exception code
                uint16_t crc = modbus_crc16(modbus_buf, ptr);
                modbus_buf[ptr++] = (uint8_t)(crc & 0xFFu);
                modbus_buf[ptr++] = (uint8_t)((crc >> 8) & 0xFF);
                uart0_cnt = ptr;
            }

            CDC_write('>');
            bytes2hex(modbus_buf, cdc_buf, uart0_cnt);
            CDC_print(cdc_buf);
            CDC_write('\n');

            INT_ATOMIC_BLOCK {
                PIN_high(PIN_NRE);
                PIN_high(PIN_DE);
                for (size_t i = 0; i < uart0_cnt; i++) {
                    UART0_write(modbus_buf[i]);
                }
                PIN_low(PIN_DE);
                PIN_low(PIN_NRE);
            }
            
            // if (ret == MODBUS_SUCCESS) {
            //     for (size_t i = 0; i < length; i++) {
            //         uart_port_write(0, buffer[i]);
            //     }
            // }
            uart0_cnt = 0;
        }

        if (uart1_cnt > 0 && (millis() - last_uart1 >= SML_PAUSE)) {
            // Process SML frame
            sml_rc ret = sml_process_frame(&sml_ctx, sml_buf, sizeof(sml_buf), &uart1_cnt);
            CDC_write('\n');
            CDC_write('=');
            bytes2hex(&ret, cdc_buf, sizeof(ret));
            CDC_print(cdc_buf);
            CDC_write('\n');

            uart1_cnt = 0;
        }

        CDC_flush();
    }
}

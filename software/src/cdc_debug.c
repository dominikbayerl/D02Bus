#ifndef NDEBUG

#include "cdc_debug.h"
#include "usb_cdc.h"

static void cdc_write_u8(uint8_t value) {
    if (value >= 100) CDC_write((char)('0' + value / 100));
    if (value >= 10) CDC_write((char)('0' + (value / 10) % 10));
    CDC_write((char)('0' + value % 10));
}

static void cdc_write_u32(uint32_t value) {
    char digits[10];
    uint8_t count = 0;

    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);

    while (count) CDC_write(digits[--count]);
}

static void cdc_write_value(float value) {
    uint32_t whole;
    uint32_t fraction;

    if (value < 0.0f) {
        CDC_write('-');
        value = -value;
    }

    whole = (uint32_t)value;
    fraction = (uint32_t)((value - (float)whole) * 1000000.0f + 0.5f);
    if (fraction >= 1000000UL) {
        whole++;
        fraction -= 1000000UL;
    }

    cdc_write_u32(whole);
    CDC_write('.');
    for (uint32_t divisor = 100000UL; divisor; divisor /= 10) {
        CDC_write((char)('0' + (fraction / divisor) % 10));
    }
}

void cdc_debug_init(void) {
    CDC_init();
}

void cdc_debug_irq(void) {
    USB_interrupt();
}

void cdc_debug_obis_value(const uint8_t *obis, float value) {
    // Never delay parsing while no terminal is attached or the previous line
    // is still in flight. UART1 continues to run at high interrupt priority.
    if (!USB_ENUM_OK || !CDC_getDTR() || !CDC_ready()) return;

    CDC_print("OBIS ");
    cdc_write_u8(obis[0]);
    CDC_write('-');
    cdc_write_u8(obis[1]);
    CDC_write(':');
    cdc_write_u8(obis[2]);
    CDC_write('.');
    cdc_write_u8(obis[3]);
    CDC_write('.');
    cdc_write_u8(obis[4]);
    CDC_write('*');
    cdc_write_u8(obis[5]);
    CDC_print(" = ");
    cdc_write_value(value);
    CDC_write('\r');
    CDC_write('\n');
    CDC_flush();
}

#endif

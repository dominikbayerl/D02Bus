// ===================================================================================
// SMRH on CH552 — Smart-meter reading head, ported to WCH CH552
// Entry point and main coordinator loop
// ===================================================================================

#include "system.h"
#include "config.h"
#include "delay.h"
#include "gpio.h"
#include "systick.h"
#include "modbus_rtu.h"
#include "sml.h"
#include "cdc_debug.h"

// (PIN_SML_TX removed, replaced by PIN_DE)

// General initialization of pins
void general_init(void) {
    // Configure LEDs and control pins as output
    PIN_output(PIN_LED0);
    PIN_output(PIN_LED1);
    PIN_output(PIN_DE);
    PIN_output(PIN_NRE);
    
    // Set initial pin states
    PIN_low(PIN_LED0);
    PIN_low(PIN_LED1);
    PIN_low(PIN_DE);
    PIN_high(PIN_NRE); // NRE active low, set high to disable RX initially or just leave low? Usually RE is enabled when idle

}

// ===================================================================================
// Interrupt Service Routines
// Defined here to ensure SDCC links the interrupt vectors correctly.
// ===================================================================================

// Timer2 Overflow Interrupt Service Routine
void systick_isr(void) __interrupt (INT_NO_TMR2) {
    TF2 = 0; // Clear the overflow interrupt flag
    sys_millis_count++;
}

// UART0 Receive Interrupt Service Routine
void uart0_isr(void) __interrupt (INT_NO_UART0) {
    if (RI) {
        RI = 0; // Clear interrupt flag
        PIN_high(PIN_LED0);
        
        if (modbus_frame_available) {
            modbus_frame_available = 0;
            modbus_rx_number = 0;
        }
        
        if (modbus_rx_number < MODBUS_RX_BUFFER_SIZE) {
            modbus_rx_buffer[modbus_rx_number++] = SBUF;
        } else {
            modbus_rx_number = 0; // Buffer overflow, reset
            PIN_high(PIN_LED1); // Indicate error with LED1
            PIN_low(PIN_LED1);
        }
        
        modbus_rx_last_time = millis();
        PIN_low(PIN_LED0);
    }
}

// UART1 Receive Interrupt Service Routine
void uart1_isr(void) __interrupt (INT_NO_UART1) {
    // RX and TX share this vector.  TX is unused, but always clear a pending
    // transmit flag so it cannot turn into an interrupt storm.
    if (U1TI) {
        U1TI = 0;
    }

    if (U1RI) {
        // U1RB8 contains the received stop bit in 8-bit mode.
        const uint8_t stop_bit_valid = U1RB8;
        const uint8_t value = SBUF1;
        U1RI = 0; // Clear receive interrupt flag
        PIN_high(PIN_LED1);

        // Never pass a framing-error byte to the SML transport parser.  It can
        // occur when reception starts during a telegram or from a real UART
        // timing/electrical fault, and cannot be part of a valid SML frame.
        if (stop_bit_valid) {
            if (sml_rx_number < SML_RX_BUFFER_SIZE) {
                sml_rx_put_byte(value);
            } else {
                sml_rx_delete(); // Buffer overflow invalidates the current frame
                PIN_high(PIN_LED0); // Indicate error with LED0
                PIN_low(PIN_LED0);
            }
            sml_rx_last_time = millis();
        } else {
            // Reset UART1's receive state machine.  In particular, do not let
            // an invalid stop bit leave it repeatedly sampling the continuous
            // telegram at the same incorrect bit offset.
            U1REN = 0;
            U1REN = 1;
        }
        PIN_low(PIN_LED1);
    }
}

#ifndef NDEBUG
void usb_isr(void) __interrupt (INT_NO_USB) {
    cdc_debug_irq();
}
#endif

// Main program entrypoint
void main(void) {
    CLK_config();                                   // Configure system clock (F_CPU)
    general_init();                                 // Initialize GPIO and registers
    
    SYS_init();                                     // Start 1 ms system timer (Timer2)
    
    modbus_init();                                  // Set up UART0 Modbus
    modbus_rx_enable();                             // Enable Modbus reception
    
    sml_init();                                     // Set up UART1 SML
    
    // Keep global interrupts and SML buffering disabled during this blocking
    // startup indication.
    for (uint8_t i = 0; i < 6; i++) {
        PIN_toggle(PIN_LED0);
        PIN_toggle(PIN_LED1);
        DLY_ms(500);
    }

    cdc_debug_init();                               // No-op in NDEBUG builds
    sml_rx_enable();                                // Start SML reception cleanly
    
    // Enable interrupts globally
    INT_enable();

    while (1) {
        // A complete Modbus frame was received
        if (modbus_frame_avail()) {
            uint8_t frame_len = modbus_rx_avail();
            static __xdata uint8_t frame_bytes[64];
            modbus_rx_get(frame_bytes, frame_len);
            
            modbus_frame_send(frame_bytes, frame_len);
        }
        
        // Incoming SML bytes are available
        if (sml_rx_avail()) {
            sml_analyse();
        }

        // Keep the D3 SML transport-result indication running without ever
        // delaying UART reception.
        sml_status_update();
    }
}

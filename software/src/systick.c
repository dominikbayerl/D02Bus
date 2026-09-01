// ===================================================================================
// Systick - 1 ms millis() using Timer2 on CH552
// ===================================================================================

#include "systick.h"
#include "system.h"

volatile __xdata uint32_t sys_millis_count = 0;

// Initialize Timer2 for 1 ms tick
void SYS_init(void) {
    // Configure Timer2 clock: use fast clock (Fsys/1)
    T2MOD |= bT2_CLK;
    
    // Set Timer2 to 16-bit auto-reload mode, count up
    T2CON = 0;
    
    // Calculate reload value for 1 ms period
    // At 16 MHz, 1 ms is 16000 clock cycles.
    // Reload value = 65536 - (F_CPU / 1000)
    uint16_t reload = 65536 - (F_CPU / 1000);
    
    RCAP2H = reload >> 8;
    RCAP2L = reload & 0xFF;
    
    // Load initial counter value
    TH2 = RCAP2H;
    TL2 = RCAP2L;
    
    // Clear overflow flag
    TF2 = 0;
    
    // Enable Timer2 interrupt
    ET2 = 1;
    
    // Start Timer2
    TR2 = 1;
}

// Read the millisecond count atomically (disabling interrupts)
uint32_t millis(void) {
    uint32_t val = 0;
    INT_ATOMIC_BLOCK {
        val = sys_millis_count;
    }
    return val;
}

// Removed systick_isr, defined in main.c

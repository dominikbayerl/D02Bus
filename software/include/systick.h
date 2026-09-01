// ===================================================================================
// Systick - 1 ms millis() using Timer2 on CH552
// ===================================================================================
//
// Timer2 is configured as 16-bit auto-reload timer overflowing every 1 ms.
// The overflow ISR increments a 32-bit __xdata counter, providing an Arduino
// style millis() function.
//
// SYS_init()       configure Timer2 and start systick (call after CLK_config)
// millis()         return current ms count (atomic on 32 bit)
// ===================================================================================

#pragma once
#include <stdint.h>
#include "ch554.h"

extern volatile __xdata uint32_t sys_millis_count;

void SYS_init(void);
uint32_t millis(void);

// Compute interval (uint32_t-safe, wraps correctly)
#define MS_ELAPSED(start) ((uint32_t)(millis() - (start)))

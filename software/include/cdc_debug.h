#pragma once

#include <stdint.h>

#if !defined(NDEBUG) && !defined(HOST_TEST)
void cdc_debug_init(void);
void cdc_debug_irq(void);
void cdc_debug_obis_value(const uint8_t *obis, float value);
#else
#define cdc_debug_init()                       ((void)0)
#define cdc_debug_irq()                        ((void)0)
#define cdc_debug_obis_value(obis, value)      ((void)0)
#endif

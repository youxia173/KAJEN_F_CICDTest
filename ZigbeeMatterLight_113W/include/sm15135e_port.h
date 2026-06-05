#ifndef SM15135E_PORT_H
#define SM15135E_PORT_H

#include <stdint.h>

// Platform porting hooks (implement in your BSP or board layer).
// This is a single-wire RZ protocol, not a timer PWM output.
void sm15135e_port_init(void);
void sm15135e_din_high(void);
void sm15135e_din_low(void);
void sm15135e_delay_ns(uint32_t ns);
void sm15135e_delay_us(uint32_t us);

// Map driver macros to the platform implementations.
#define SM15135E_DIN_HIGH() sm15135e_din_high()
#define SM15135E_DIN_LOW()  sm15135e_din_low()

#endif // SM15135E_PORT_H

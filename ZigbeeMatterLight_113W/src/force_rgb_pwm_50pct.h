#ifndef FORCE_RGB_PWM_50PCT_H
#define FORCE_RGB_PWM_50PCT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sl_simple_rgb_pwm_led.h"
#include "sl_simple_rgb_pwm_led_instances.h"
#include "sl_pwm_instances.h"

static inline void force_rgb_pwm_50pct(void)
{
	/* 256 resolution, 50% duty = 128 */
	sl_led_set_rgb_color(&sl_simple_rgb_pwm_led_rgb_led0, 128, 128, 128);
}

static inline void force_red_pwm_50pct(void)
{
    /* 256 resolution, 50% duty = 128; set R=128, G=0, B=0 */
    sl_led_set_rgb_color(&sl_simple_rgb_pwm_led_rgb_led0, 128, 0, 0);
}

static inline void force_white_pwm_50pct(void)
{
	/* Set separate white PWM instance to 50% */
	sl_pwm_set_duty_cycle(&sl_pwm_rgb_white, 50);
	sl_pwm_start(&sl_pwm_rgb_white);
}
#ifdef __cplusplus
}
#endif

#endif // FORCE_RGB_PWM_50PCT_H

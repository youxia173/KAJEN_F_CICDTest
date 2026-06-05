#include "sl_simple_rgb_pwm_led.h"
#include "sl_simple_rgb_pwm_led_instances.h"
#include "force_rgb_pwm_50pct.h"

void force_rgb_pwm_50pct(void)
{
    // 256分辨率，50%占空比为128
    sl_led_set_rgb_color(&sl_simple_rgb_pwm_led_rgb_led0, 128, 128, 128);
}

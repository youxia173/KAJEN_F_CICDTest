/**
 * @file sm15135e_port.c
 * @brief Platform port for SM15135E data pin and delays.
 */
#include "sm15135e_port.h"

#include <stdbool.h>

#include "em_device.h"
#include "sl_spidrv_eusart_SPI_SM15135E_config.h"
#include "sl_clock_manager.h"
#include "sl_gpio.h"
#include "sl_udelay.h"

// Use the same loop primitive as sl_udelay for sub-microsecond timing.
void sli_delay_loop(unsigned n);

#if defined(__CORTEX_M) && ((__CORTEX_M == 33U) || (__CORTEX_M == 55U))
#define SM15135E_HW_LOOP_CYCLE  3u
#else
#define SM15135E_HW_LOOP_CYCLE  4u
#endif

static bool sm15135e_port_inited = false;

void sm15135e_port_init(void)
{
  if (sm15135e_port_inited) {
    return;
  }

  sl_clock_manager_enable_bus_clock(SL_BUS_CLOCK_GPIO);

  sl_gpio_t gpio;
  gpio.port = SL_SPIDRV_EUSART_SPI_SM15135E_TX_PORT;
  gpio.pin = SL_SPIDRV_EUSART_SPI_SM15135E_TX_PIN;
  sl_gpio_set_pin_mode(&gpio, SL_GPIO_MODE_PUSH_PULL, false);

  sm15135e_port_inited = true;
}

void sm15135e_din_high(void)
{
  sm15135e_port_init();

  sl_gpio_t gpio;
  gpio.port = SL_SPIDRV_EUSART_SPI_SM15135E_TX_PORT;
  gpio.pin = SL_SPIDRV_EUSART_SPI_SM15135E_TX_PIN;
  sl_gpio_set_pin(&gpio);
}

void sm15135e_din_low(void)
{
  sm15135e_port_init();

  sl_gpio_t gpio;
  gpio.port = SL_SPIDRV_EUSART_SPI_SM15135E_TX_PORT;
  gpio.pin = SL_SPIDRV_EUSART_SPI_SM15135E_TX_PIN;
  sl_gpio_clear_pin(&gpio);
}

void sm15135e_delay_ns(uint32_t ns)
{
  if (ns == 0u) {
    return;
  }

  uint32_t freq_hz = SystemCoreClockGet();
  if (freq_hz == 0u) {
    return;
  }

  uint64_t cycles = ((uint64_t)freq_hz * (uint64_t)ns + 999999999ull) / 1000000000ull;
  uint32_t loops = (uint32_t)(cycles / SM15135E_HW_LOOP_CYCLE);
  if (loops == 0u) {
    loops = 1u;
  }

  sli_delay_loop(loops);
}

void sm15135e_delay_us(uint32_t us)
{
  sl_udelay_wait((unsigned)us);
}

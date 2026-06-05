#include "sl_event_handler.h"

#include "sl_rail_mux.h"
#include "sl_clock_manager.h"
#include "sl_rail_util_load_devinfo.h"
#include "sl_rail_util_compatible_pa.h"
#include "sl_rail_util_power_manager_init.h"
#include "sl_rail_util_pti.h"
#include "sl_rail_util_rssi.h"
#include "sl_zigbee_system_common.h"
#include "gp-types.h"
#include "sl_event_system.h"
#include "btl_interface.h"
#include "platform-efr32.h"
#include "sl_bt_rtos_adaptation.h"
#include "sl_bluetooth.h"
#include "sl_gpio.h"
#include "sl_iostream_rtt.h"
#include "sl_iostream_stdio.h"
#include "sl_mbedtls.h"
#include "sl_ot_rtos_adaptation.h"
#include "sl_pwm_instances.h"
#include "sl_simple_button_instances.h"
#include "sl_simple_led_instances.h"
#include "sl_simple_rgb_pwm_led_instances.h"
#include "sl_spidrv_instances.h"
#include "sl_uartdrv_instances.h"
#include "sl_cli_instances.h"
#include "psa/crypto.h"
#include "sl_se_manager.h"
#include "sl_rail_util_mp_transition_time.h"
#include "sli_sxsymcrypt.h"
#include "sli_crypto.h"
#include "sli_crypto_ksu_manager.h"
#include "sl_iostream_init_instances.h"
#include "cmsis_os2.h"
#include "sl_token_manager_api.h"
#include "nvm3_default.h"
#include "sl_iostream_handles.h"

void sli_driver_permanent_allocation(void)
{
}

void sli_service_permanent_allocation(void)
{
}

void sli_stack_permanent_allocation(void)
{
  sli_zigbee_stack_rtos_perm_allocation();
  sli_legacy_buffer_manager_initialize_buffers();
  sli_zigbee_app_framework_rtos_perm_allocation();
  sli_bt_stack_permanent_allocation();
  sl_ot_rtos_perm_allocation();
}

void sli_internal_permanent_allocation(void)
{
}

void sl_platform_init(void)
{
  sli_rail_mux_local_init();
  sl_clock_manager_runtime_init();
  sl_event_system_init();
  bootloader_init();
  nvm3_initDefault();
}

void sli_internal_init_early(void)
{
  sl_rail_util_load_devinfo();
}

void sl_kernel_start(void)
{
  sli_bt_rtos_adaptation_kernel_start();
  osKernelStart();
}

void sl_driver_init(void)
{
  sl_gpio_init();
  sl_pwm_init_instances();
  sl_simple_button_init_instances();
  sl_simple_led_init_instances();
  sl_simple_rgb_pwm_led_init_instances();
  sl_spidrv_init_instances();
  sl_uartdrv_init_instances();
}

void sl_service_init(void)
{
  sl_mbedtls_init();
  psa_crypto_init();
  sl_se_init();
  sli_sxsymcrypt_init_locks();
  sli_crypto_init();
  sli_ksu_init();
  sl_iostream_init_instances_stage_1();
  sl_iostream_init_instances_stage_2();
  sl_cli_instances_init();
  sl_token_manager_init();
}

void sl_stack_init(void)
{
  sl_rail_util_pa_init();
  sl_rail_util_power_manager_init();
  sl_rail_util_pti_init();
  sl_rail_util_rssi_init();
  sli_zigbee_stack_rtos_task_init_cb();
  sli_zigbee_stack_sleep_init();
  sli_zigbee_app_framework_rtos_task_init_cb();
  sli_zigbee_app_framework_sleep_init();
  sli_zigbee_gp_init_tokens();
  sl_ot_sys_init();
  sli_bt_stack_functional_init();
  sl_rail_util_mp_transition_time_init();
}

void sl_internal_app_init(void)
{
  sl_ot_rtos_stack_init();
  sl_ot_rtos_app_init();
}

void sl_iostream_init_instances_stage_1(void)
{
  sl_iostream_rtt_init();
  sl_iostream_stdio_init();
}

void sl_iostream_init_instances_stage_2(void)
{
  sl_iostream_set_console_instance();
}


/***************************************************************************//**
 * @brief Zigbee Level Control Server Cluster component configuration header.
 *\n*******************************************************************************
 * # License
 * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

// <<< Use Configuration Wizard in Context Menu >>>

// 中文导读（用途）：本文件配置 Zigbee 调光（Level Control）能力，
// 修改建议：若非必要不要改最小/最大亮度，先验证与第三方开关的互通性。
// 影响开关端调光命令在灯端的行为范围与变化速率。

// <h>Zigbee Level Control Server Cluster configuration

// <o SL_ZIGBEE_AF_PLUGIN_LEVEL_CONTROL_MAXIMUM_LEVEL> Maximum level <0-255>
// <i> Default: 255
// <i> Used to specify device-specific maximum level. Not valid for ZLL devices.
#define SL_ZIGBEE_AF_PLUGIN_LEVEL_CONTROL_MAXIMUM_LEVEL   255

// <o SL_ZIGBEE_AF_PLUGIN_LEVEL_CONTROL_MINIMUM_LEVEL> Minimum level <0-255>
// <i> Default: 0
// <i> Used to specify device-specific minimum level. Not valid for ZLL devices.
#define SL_ZIGBEE_AF_PLUGIN_LEVEL_CONTROL_MINIMUM_LEVEL   0

// <o SL_ZIGBEE_AF_PLUGIN_LEVEL_CONTROL_RATE> Maximum transition rate (ticks / second) <0-255>
// <i> Default: 0
// <i> Used to specify device-specific maximum transition rate (ticks / second). A value of 0 relegates this task to the HAL.
#define SL_ZIGBEE_AF_PLUGIN_LEVEL_CONTROL_RATE   0

// </h>

// <<< end of configuration section >>>

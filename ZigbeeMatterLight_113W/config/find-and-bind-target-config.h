/***************************************************************************//**
 * @brief Zigbee Find and Bind Target component configuration header.
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

// <h>Zigbee Find and Bind Target configuration

// <o SL_ZIGBEE_AF_PLUGIN_FIND_AND_BIND_TARGET_COMMISSIONING_TIME> Target Commissioning Time <180-255>
// <i> Default: 180
// <i> The amount of time that the target will respond to identify querys.
// 中文导读（用途）：灯作为 Find-and-Bind 目标设备时，会在该时间窗口内响应识别/绑定流程。
// 修改建议：现场调试可短时加长窗口，量产时应控制时长以降低误触发概率。
// 开关若在窗口外发起匹配，通常无法自动完成绑定。
#define SL_ZIGBEE_AF_PLUGIN_FIND_AND_BIND_TARGET_COMMISSIONING_TIME   180

// </h>

// <<< end of configuration section >>>

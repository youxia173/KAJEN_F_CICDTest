/***************************************************************************//**
 * @brief Zigbee Application Framework common component configuration header.
 *\n*******************************************************************************
 * # License
 * <b>Copyright 2021 Silicon Laboratories Inc. www.silabs.com</b>
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

// 中文导读（用途）：本文件配置 Zigbee Core CLI 调试能力，
// 修改建议：量产版本建议关闭事件调试，开发联调阶段再按需打开。
// 主要用于开发阶段查看事件与网络状态。

// <h>Zigbee core CLI configuration
// <o SL_ZIGBEE_EVENT_DEBUG_ENABLED> Event Debug Info <0-1>
// <i> Default: False
// <i> Enable/Disable debug info for the "events" CLI command.
// <i> Debug info includes event name, nwk index, endpoint and remaining time.
#define SL_ZIGBEE_EVENT_DEBUG_ENABLED (0)

// </h>

// <<< end of configuration section >>>

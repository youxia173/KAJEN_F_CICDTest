/***************************************************************************//**
 * @file
 * @brief Default slot configuration header for internal storage bootloader
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc.  Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement.  This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

#ifndef BTL_INTERNAL_STORAGE_SLOT_CFG_DEFAULT_H
#define BTL_INTERNAL_STORAGE_SLOT_CFG_DEFAULT_H

// Disable Overlapping Slots
#define SLOT_OVERLAP_ENABLE 0

// Enable Slot 0
#define SLOT0_ENABLE 1
#define SLOT0_START  18251776
#define SLOT0_SIZE   1208320

// Disable all other slots
#define SLOT1_ENABLE 0
#define SLOT1_START  0
#define SLOT1_SIZE   0

#define SLOT2_ENABLE 0
#define SLOT2_START  0
#define SLOT2_SIZE   0

#endif // BTL_INTERNAL_STORAGE_SLOT_CFG_DEFAULT_H

/*
 * Project overrides for Silicon Labs Matter platform defaults.
 * This file shadows the SDK header via include/ on the compiler search path.
 */
#pragma once

#include_next <platform/silabs/CHIPDevicePlatformConfig.h>

#undef CHIP_DEVICE_CONFIG_TEST_VENDOR_NAME
#undef CHIP_DEVICE_CONFIG_TEST_PRODUCT_NAME

#define CHIP_DEVICE_CONFIG_TEST_VENDOR_NAME "ikea"
#define CHIP_DEVICE_CONFIG_TEST_PRODUCT_NAME "KAJEN_F"

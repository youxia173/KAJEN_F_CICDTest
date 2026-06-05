####################################################################
# Automatically-generated file. Do not edit!                       #
####################################################################

set(SDK_PATH "/home/hans/.silabs/slt/installs/conan/p/simpl1a11563c2e399/p")
set(COPIED_SDK_PATH "simplicity_sdk_2025.12.1")
set(PKG_PATH "/home/hans/.silabs/slt/installs")

add_library(slc OBJECT
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/core/btl_reset.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/debug/btl_debug.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/debug/btl_debug_swo.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/driver/btl_driver_util.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/parser/compression/lzma/LzmaDec.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3/core/btl_bootload.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3/core/btl_core.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3/core/btl_main.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3/core/btl_parse.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3/parser/compression/btl_decompress_lz4.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3/parser/compression/btl_decompress_lzma.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3/parser/gbl/btl_gbl_custom_tags.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3/parser/gbl/btl_gbl_parser.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3/security/btl_crc16.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3/security/btl_crc32.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3/security/btl_security_aes.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3/security/btl_security_ecdsa.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3/security/btl_security_sha256.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3/storage/bootloadinfo/btl_storage_bootloadinfo.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3/storage/btl_storage.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3/storage/btl_storage_library.c"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3/storage/ext_mem/btl_storage_ext_mem.c"
    "../${COPIED_SDK_PATH}/devices/platform/Device/SiliconLabs/SIMG301/Source/startup_simg301.c"
    "../${COPIED_SDK_PATH}/devices/platform/Device/SiliconLabs/SIMG301/Source/system_simg301.c"
    "../${COPIED_SDK_PATH}/platform_common/platform/common/src/sl_assert.c"
    "../${COPIED_SDK_PATH}/platform_common/platform/common/src/sl_syscalls.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/common/src/sl_core_cortexm.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/driver/gpio/src/sl_gpio.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_gpio.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_ldma.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_syscfg.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_system.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/peripheral/src/sl_hal_timer.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/clock_manager/src/sl_clock_manager.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/clock_manager/src/sl_clock_manager_hal_s3.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/clock_manager/src/sl_clock_manager_init.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/clock_manager/src/sl_clock_manager_init_hal_s3.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/device_manager/clocks/sl_device_clock_sixx301.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/device_manager/src/sl_device_clock.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/device_manager/src/sl_device_gpio.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/interrupt_manager/src/sl_interrupt_manager_cortexm.c"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/memory_manager/src/sl_memory_manager_region.c"
    "../${COPIED_SDK_PATH}/security_mbedtls/platform/security/sl_component/sli_crypto/src/sli_crypto_ksu_manager.c"
    "../${COPIED_SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager.c"
    "../${COPIED_SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_attestation.c"
    "../${COPIED_SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_cipher.c"
    "../${COPIED_SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_entropy.c"
    "../${COPIED_SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_extmem.c"
    "../${COPIED_SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_hash.c"
    "../${COPIED_SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_key_derivation.c"
    "../${COPIED_SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_key_handling.c"
    "../${COPIED_SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_signature.c"
    "../${COPIED_SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sl_se_manager_util.c"
    "../${COPIED_SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sli_se_manager_device_data.c"
    "../${COPIED_SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/src/sli_se_manager_mailbox.c"
    "../${COPIED_SDK_PATH}/security_sxsymcrypt/src/aead.c"
    "../${COPIED_SDK_PATH}/security_sxsymcrypt/src/blkcipher.c"
    "../${COPIED_SDK_PATH}/security_sxsymcrypt/src/channel.c"
    "../${COPIED_SDK_PATH}/security_sxsymcrypt/src/cmac.c"
    "../${COPIED_SDK_PATH}/security_sxsymcrypt/src/cmdma.c"
    "../${COPIED_SDK_PATH}/security_sxsymcrypt/src/cmmask.c"
    "../${COPIED_SDK_PATH}/security_sxsymcrypt/src/hash.c"
    "../${COPIED_SDK_PATH}/security_sxsymcrypt/src/hmac.c"
    "../${COPIED_SDK_PATH}/security_sxsymcrypt/src/interrupts.c"
    "../${COPIED_SDK_PATH}/security_sxsymcrypt/src/keyref.c"
    "../${COPIED_SDK_PATH}/security_sxsymcrypt/src/mac.c"
    "../${COPIED_SDK_PATH}/security_sxsymcrypt/src/platform/silicon_labs/sli_sxsymcrypt_engine_management.c"
    "../${COPIED_SDK_PATH}/security_sxsymcrypt/src/platform/silicon_labs/sli_sxsymcrypt_hardware_interaction.c"
    "../matter_2.8.0/slc/component/sdk-content/simplicity-sdk/util/third_party/segger/systemview/SEGGER/SEGGER_RTT.c"
)

target_include_directories(slc PUBLIC
   "../config"
   "../autogen"
   "../matter_2.8.0/slc/component/sdk-content/simplicity-sdk/util/third_party/segger/systemview/SEGGER"
    "../${COPIED_SDK_PATH}/devices/platform/Device/SiliconLabs/SIMG301/Include"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/parser/compression"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/debug"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3/parser"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3/api"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/api"
    "../${COPIED_SDK_PATH}/bootloader/platform/bootloader/series3/storage/ext_mem"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/clock_manager/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/clock_manager/src"
    "../${COPIED_SDK_PATH}/cmsis/Core/Include"
    "../${COPIED_SDK_PATH}/platform_common/platform/common/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/device_manager/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/driver/gpio/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/peripheral/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/interrupt_manager/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/interrupt_manager/src"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/interrupt_manager/inc/arm"
    "../${COPIED_SDK_PATH}/platform_core/platform/service/memory_manager/inc"
    "../${COPIED_SDK_PATH}/security_se_manager/platform/security/sl_component/se_manager/inc"
    "../${COPIED_SDK_PATH}/platform_core/platform/common/inc"
    "../${COPIED_SDK_PATH}/security_mbedtls/platform/security/sl_component/sli_crypto/inc"
    "../${COPIED_SDK_PATH}/security_se_manager/platform/security/sl_component/sli_psec_osal/inc"
    "../${COPIED_SDK_PATH}/security_sxsymcrypt/include"
    "../${COPIED_SDK_PATH}/security_sxsymcrypt/src"
    "../${COPIED_SDK_PATH}/security_sxsymcrypt/src/platform/silicon_labs"
)

target_compile_definitions(slc PUBLIC
    "SILABS_LOG_OUT_UART=0"
    "RTT_USE_ASM=0"
    "SIMG301M113WIH=1"
    "SL_CODE_COMPONENT_SYSTEM=system"
    "BTL_PARSER_SUPPORT_CUSTOM_TAGS=1"
    "BTL_PARSER_SUPPORT_LZMA=1"
    "_LZMA_SIZE_OPT=1"
    "BTL_PARSER_SUPPORT_LZ4=1"
    "BOOTLOADER_ENABLE=1"
    "BOOTLOADER_SECOND_STAGE=1"
    "SL_RAMFUNC_DISABLE=1"
    "SL_STACK_SIZE=16384"
    "__START=main"
    "__STARTUP_CLEAR_BSS=1"
    "BOOTLOADER_SUPPORT_STORAGE=1"
    "SL_CODE_COMPONENT_CLOCK_MANAGER=clock_manager"
    "SL_COMPONENT_CATALOG_PRESENT=1"
    "SL_CODE_COMPONENT_GPIO=gpio"
    "SL_CODE_COMPONENT_HAL_COMMON=hal_common"
    "SL_CODE_COMPONENT_HAL_GPIO=hal_gpio"
    "SL_CODE_COMPONENT_HAL_LDMA=hal_ldma"
    "SL_CODE_COMPONENT_INTERRUPT_MANAGER=interrupt_manager"
    "CMSIS_NVIC_VIRTUAL=1"
    "CMSIS_NVIC_VIRTUAL_HEADER_FILE=\"cmsis_nvic_virtual.h\""
    "SL_CODE_COMPONENT_SE_MANAGER=se_manager"
    "SL_CODE_COMPONENT_CORE=core"
    "SL_CODE_COMPONENT_PSEC_OSAL=psec_osal"
    "SL_CODE_COMPONENT_SXSYMCRYPT=sxsymcrypt"
    "SX_HASH_BLOCKSZ_MAX=0"
    "SX_HASH_DIGESTSZ_MAX=0"
    "SX_KEYREF_MAX_ID=32"
)

target_link_libraries(slc PUBLIC
    "-Wl,--start-group"
    "gcc"
    "c"
    "m"
    "nosys"
    "-Wl,--end-group"
)
target_compile_options(slc PUBLIC
    $<$<COMPILE_LANGUAGE:C>:-mcpu=cortex-m33>
    $<$<COMPILE_LANGUAGE:C>:-mthumb>
    $<$<COMPILE_LANGUAGE:C>:-mfpu=fpv5-sp-d16>
    $<$<COMPILE_LANGUAGE:C>:-mfloat-abi=hard>
    $<$<COMPILE_LANGUAGE:C>:-mcmse>
    $<$<COMPILE_LANGUAGE:C>:-Wall>
    $<$<COMPILE_LANGUAGE:C>:-Wextra>
    $<$<COMPILE_LANGUAGE:C>:-Os>
    $<$<COMPILE_LANGUAGE:C>:-fdata-sections>
    $<$<COMPILE_LANGUAGE:C>:-ffunction-sections>
    $<$<COMPILE_LANGUAGE:C>:-fomit-frame-pointer>
    $<$<COMPILE_LANGUAGE:C>:-g>
    $<$<COMPILE_LANGUAGE:C>:--specs=nano.specs>
    $<$<COMPILE_LANGUAGE:C>:-fno-lto>
    $<$<COMPILE_LANGUAGE:CXX>:-mcpu=cortex-m33>
    $<$<COMPILE_LANGUAGE:CXX>:-mthumb>
    $<$<COMPILE_LANGUAGE:CXX>:-mfpu=fpv5-sp-d16>
    $<$<COMPILE_LANGUAGE:CXX>:-mfloat-abi=hard>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-mcmse>
    $<$<COMPILE_LANGUAGE:CXX>:-Wall>
    $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
    $<$<COMPILE_LANGUAGE:CXX>:-Os>
    $<$<COMPILE_LANGUAGE:CXX>:-fdata-sections>
    $<$<COMPILE_LANGUAGE:CXX>:-ffunction-sections>
    $<$<COMPILE_LANGUAGE:CXX>:-fomit-frame-pointer>
    $<$<COMPILE_LANGUAGE:CXX>:-g>
    $<$<COMPILE_LANGUAGE:CXX>:--specs=nano.specs>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-lto>
    $<$<COMPILE_LANGUAGE:ASM>:-mcpu=cortex-m33>
    $<$<COMPILE_LANGUAGE:ASM>:-mthumb>
    $<$<COMPILE_LANGUAGE:ASM>:-mfpu=fpv5-sp-d16>
    $<$<COMPILE_LANGUAGE:ASM>:-mfloat-abi=hard>
    "$<$<COMPILE_LANGUAGE:ASM>:SHELL:-x assembler-with-cpp>"
)

set(post_build_command ${POST_BUILD_EXE} postbuild "./Matter-Bootloader_113W.slpb" --parameter build_dir:"$<TARGET_FILE_DIR:Matter-Bootloader_113W>")
set_property(TARGET slc PROPERTY C_STANDARD 17)
set_property(TARGET slc PROPERTY CXX_STANDARD 17)
set_property(TARGET slc PROPERTY CXX_EXTENSIONS OFF)

target_link_options(slc INTERFACE
    -mcpu=cortex-m33
    -mthumb
    -mfpu=fpv5-sp-d16
    -mfloat-abi=hard
    -T${CMAKE_CURRENT_LIST_DIR}/../autogen/linkerfile.ld
    --specs=nano.specs
    "SHELL:-Xlinker -Map=$<TARGET_FILE_DIR:Matter-Bootloader_113W>/Matter-Bootloader_113W.map"
    -fno-lto
    -Wl,--gc-sections
)

# BEGIN_SIMPLICITY_STUDIO_METADATA=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END_SIMPLICITY_STUDIO_METADATA
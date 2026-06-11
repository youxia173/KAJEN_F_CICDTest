/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    Copyright (c) 2019 Google LLC.
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#ifdef __cplusplus
extern "C" {
double fabs(double);
double fmod(double, double);
double floor(double);
float powf(float, float);
float logf(float);
float floorf(float);
}
#endif

#include "AppTask.h"
#include <lib/support/logging/CHIPLogging.h>
#include "AppConfig.h"
#include "AppEvent.h"

#include "LEDWidget.h"
#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
#include "RGBLEDWidget.h"
#endif //(defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)

#include <app/persistence/AttributePersistenceProviderInstance.h>
#include <app/persistence/DefaultAttributePersistenceProvider.h>


#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/clusters/on-off-server/on-off-server.h>
#include <app/reporting/reporting.h>
#include <app/server/Server.h>
#include <app/util/attribute-storage.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include <assert.h>
#include <inttypes.h>
// No <cmath> required; use simple piecewise HSV->RGB math.
#include <stdio.h>

#include <platform/silabs/platformAbstraction/SilabsPlatform.h>
extern "C" {
#include <micro.h>
#include <cortexm3/diagnostic.h>
#include <sl_hal_emu.h>
}

#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>

#include <lib/support/CodeUtils.h>

#include <lib/support/Span.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/CommissionableDataProvider.h>
#include <platform/PersistedStorage.h>
#include <platform/silabs/tracing/SilabsTracingMacros.h>
#include <system/SystemClock.h>
#include "sl_pwm.h"
#include "sl_pwm_instances.h"
#include "sl_simple_rgb_pwm_led.h"
#include "sl_simple_rgb_pwm_led_instances.h"

#ifdef SL_CATALOG_SIMPLE_LED_LED1_PRESENT
#define LIGHT_LED 1
#else
#define LIGHT_LED 0
#endif

#define APP_LIGHT_SWITCH 0
#define APP_FUNCTION_BUTTON 1

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace ::chip::DeviceLayer;
using namespace ::chip::DeviceLayer::Silabs;

extern "C" void MatterSetButtonPresetPwm(uint16_t wPermille, uint16_t rPermille, uint16_t gPermille, uint16_t bPermille,
                                           uint8_t level254);
extern "C" void MatterSetButtonPresetPwmWithFade(uint16_t wPermille, uint16_t rPermille, uint16_t gPermille,
                                                   uint16_t bPermille, uint8_t level254);
extern "C" void MatterSetButtonPresetTransaction(uint8_t active);
extern "C" void MatterSetButtonPresetSuppressColorCallbacks(uint8_t count);
extern "C" uint8_t MatterGetButtonPresetActive();
extern "C" void MatterSetButtonPresetActive(uint8_t active);
extern "C" uint8_t MatterGetColorSource();
extern "C" uint8_t MatterGetMemLevel();
extern "C" void MatterRestoreButtonPresetPermilles(uint16_t wPermille, uint16_t rPermille, uint16_t gPermille,
                                                     uint16_t bPermille);
extern "C" void MatterGetCurrentOutputRgbw(uint8_t * r, uint8_t * g, uint8_t * b, uint8_t * wDuty);
extern "C" void MatterRestoreOutputState(uint8_t r, uint8_t g, uint8_t b, uint8_t wDuty, uint8_t colorSource,
                                            uint8_t presetActive);
extern "C" void MatterApplyRgbwNow(uint8_t r, uint8_t g, uint8_t b, uint8_t wDuty);
extern "C" void MatterApplyWhiteBreathPermille(uint16_t permille);
extern "C" bool MatterRestorePowerOnMemoryIfAny();
extern "C" void MatterSavePowerOnMemorySnapshot();
extern "C" void MatterSetBootOutputSuppress(uint8_t suppress);
extern "C" void MatterReapplyPowerOnMemoryOutput();
extern "C" void MatterSyncPowerOnAttributesFromMemory();
extern "C" uint8_t MatterGetIsColorTempMode();
extern "C" uint16_t MatterGetRuntimeColorTempMireds();
extern "C" void MatterSetOffTransitionActive(uint8_t active);
extern "C" void MatterSetButtonDimmingActive(uint8_t active);
extern "C" void MatterApplyButtonDimmingQ16(int32_t levelQ16, uint8_t levelMin, uint8_t levelMax);
extern "C" void MatterFinalizeButtonDimming(int32_t levelQ16, uint8_t levelMin, uint8_t levelMax);
extern "C" uint8_t MatterGetLevelAtLastOn();
extern "C" void MatterSnapshotLevelForOff();
extern "C" void MatterSyncLevelBeforeOn();

extern "C" {
extern volatile uint32_t gResetDiagMagic;
extern volatile uint32_t gResetBootCount;
extern volatile uint32_t gResetLastFaultSignature;
extern volatile uint32_t gResetLastFaultValue;
extern volatile uint32_t gResetLastRebootCause;
extern volatile uint32_t gEarlyEmuResetCause;
extern volatile uint32_t gEarlyEmuResetCauseSeen;
extern volatile uint32_t gLastHalSysResetCause;
extern volatile uint32_t gLastHalSysResetSeen;

}

namespace {
constexpr uint32_t kResetDiagMagic = 0x52445331U; // RDS1
constexpr uint32_t kLongPressResetSignature = 0x4C505253U; // LPRS

constexpr uint8_t kLevelMax = APP_LEVEL_MAX;
constexpr uint8_t kLevelMin = APP_BUTTON_LEVEL_MIN;

// Button preset RGBW table (permille 0~1000 = 0~100%). Hardware output scales by CurrentLevel.
// ctMireds: Matter ColorControl sync for app (0 = use hue). Does not drive PWM.
struct ButtonPresetEntry
{
    uint16_t wPermille;
    uint16_t rPermille;
    uint16_t gPermille;
    uint16_t bPermille;
    uint16_t ctMireds;
    uint8_t hue;
};

constexpr ButtonPresetEntry kPresets[13] = {       //后面两个CT表用于上报给手机
    // order, name,   W%,    R%,    G%,    B%
    { 1000, 0,    0,    0,    370, 0   }, // 1  2700K
    { 400,  1000, 0,    0,    455, 0   }, // 2  2200K
    { 400,  0,    230,  550,  250, 0   }, // 3  4000K
    { 320,  0,    230,  1000, 154, 0   }, // 4  6500K
    { 0,    1000, 175,  0,    0,   28  }, // 5
    { 0,    1000, 100,  0,    0,   21  }, // 6
    { 0,    1000, 0,    0,    0,   11  }, // 7
    { 0,    1000, 100,  58,   0,   247 }, // 8
    { 0,    1000, 235,  395,  0,   212 }, // 9
    { 0,    235,  235,  1000, 0,   155 }, // 10
    { 0,    510,  825,  510,  0,   113 }, // 11
    { 0,    510,  1000, 155,  0,   56  }, // 12
    { 0,    1000, 315,  0,    0,   32  }, // 13
};

constexpr const char kFactoryResetBootKey[] = "FactoryResetBoot";
constexpr const char kButtonPresetMemoryKey[] = "BtnPresetMem";
constexpr uint8_t kPresetCount = 13;
constexpr uint32_t kBootReasonPowerOn = 0x01;
constexpr uint32_t kBootReasonSoftwareReset = 0x06;

struct ButtonPresetMemoryState
{
    uint8_t version;
    uint8_t latched;
    uint8_t presetIndex;
};

constexpr uint8_t kButtonPresetMemoryVersion = 2;

constexpr uintptr_t kTimerCtxClick    = 1;
constexpr uintptr_t kTimerCtxLong     = 2;
constexpr uintptr_t kTimerCtxEffect   = 3;
constexpr uintptr_t kTimerCtxCommissioning = 4;
constexpr uintptr_t kTimerCtxBootBreathEnd = 5;
constexpr uintptr_t kTimerCtxIdentifyEnd = 6;
constexpr uintptr_t kTimerCtxResetOff = 7;

bool sButtonPressed = false;
uint8_t sClickCount = 0;
bool sImmediateSinglePending = false;
uint32_t sLongPressMs = 0;
bool sResetWarnActive = false;
bool sResetTriggered = false;
bool sResetPendingAfterWarnEnd = false;
bool sResetEndSequenceActive = false;
bool sDimmingActive = false;
int8_t sNextDimmingDirection = -1;
int8_t sDimmingDirection = -1;
int32_t sDimmingLevelQ16 = 0;
int32_t sDimmingOriginLevelQ16 = 0;
uint32_t sDimmingStartTick = 0;
uint8_t sDimmingLastAppliedLevel = 0xFF;
size_t sPresetIndex = 0;
AppTask::EffectMode sEffectMode = AppTask::EffectMode::None;
uint32_t sEffectTickMs = 0;
bool sButtonPresetLatched = false;
bool sStartupSingleWhiteLock = true;
bool sCommissioningActive = false;
bool sBootBreathingActive = false;
bool sBootBreathExitPending = false;
bool sPairSuccessPending = false;
bool sIdentifyActive = false;

static uint32_t sPairSuccessSessionSeq = 0u;
static uint8_t sPairSuccessFlashCount = 0u;
static bool sPairSuccessLastOn = false;
static uint8_t sCommissioningCompleteCount = 0u;
static uint32_t sIdentifySessionSeq = 0u;
static bool sIdentifyLastOn = false;
static uint8_t sIdentifyFlashCount = 0u;

struct PreEffectState
{
    uint8_t onOff;
    uint8_t level;
    uint16_t ct;
    uint8_t hue;
    uint8_t sat;
    uint8_t rgbR;
    uint8_t rgbG;
    uint8_t rgbB;
    uint8_t whiteDuty;
    uint8_t colorSource;
    uint8_t presetActive;
    bool buttonPresetLatched;
    size_t presetIndex;
};

PreEffectState sPreEffectState = {};
PreEffectState sIdentifyEffectState = {};

static bool sDisableStartupEffects = false;

static uint8_t LevelToPercent(uint8_t level254)
{
    return static_cast<uint8_t>((static_cast<uint16_t>(level254) * 100u + 127u) / 254u);
}

static void ApplyRgbwEffect(uint8_t r, uint8_t g, uint8_t b, uint8_t wDuty)
{
    MatterApplyRgbwNow(r, g, b, wDuty);
}

static void ApplyWhitePwmEffect(uint8_t levelPct)
{
    if (levelPct > 100u)
    {
        levelPct = 100u;
    }

    // White-only effects (boot breath / identify / pair blink): force RGB off.
    // Power-on memory or SM15135E defaults may leave stale RGB lit otherwise.
    MatterApplyRgbwNow(0u, 0u, 0u, levelPct);
}

// 未配网呼吸专用：0~1000‰ 直接写 timer compare（见 MatterApplyWhiteBreathPermille）。
static void ApplyWhitePwmEffectPermille(uint16_t levelPermille)
{
    MatterApplyWhiteBreathPermille(levelPermille);
}

// 渐亮/渐灭正弦缓动查表：80 步 × 10ms = 800ms，值域 0~1000‰（离线预计算，运行时 O(1) 查表）。
static constexpr uint16_t kBootBreathRampPermilleLut[] = {
       0,    0,    1,    3,    5,    8,   12,   16,
      20,   26,   32,   39,   46,   54,   63,   72,
      82,   93,  104,  116,  128,  141,  155,  169,
     184,  200,  216,  233,  251,  269,  288,  307,
     327,  348,  370,  392,  414,  438,  462,  487,
     513,  538,  562,  586,  608,  630,  652,  673,
     693,  712,  731,  749,  766,  783,  799,  815,
     830,  844,  858,  870,  882,  893,  904,  914,
     924,  933,  941,  948,  955,  961,  967,  972,
     977,  981,  985,  988,  991,  994,  997, 1000,
};
static constexpr uint32_t kBootBreathRampSteps =
    static_cast<uint32_t>(sizeof(kBootBreathRampPermilleLut) / sizeof(kBootBreathRampPermilleLut[0]));

static_assert(APP_BOOT_BREATH_RAMP_MS % APP_EFFECT_TICK_MS == 0u,
              "APP_BOOT_BREATH_RAMP_MS must be divisible by APP_EFFECT_TICK_MS");
static_assert((APP_BOOT_BREATH_RAMP_MS / APP_EFFECT_TICK_MS) == kBootBreathRampSteps,
              "kBootBreathRampPermilleLut size must match ramp duration");

static uint16_t BootBreathRampPermille(uint32_t elapsedMs, bool rising)
{
    if (elapsedMs >= APP_BOOT_BREATH_RAMP_MS)
    {
        return rising ? 1000u : 0u;
    }

    const uint32_t step = elapsedMs / APP_EFFECT_TICK_MS;
    if (step >= kBootBreathRampSteps)
    {
        return rising ? 1000u : 0u;
    }

    return rising ? kBootBreathRampPermilleLut[step]
                  : kBootBreathRampPermilleLut[(kBootBreathRampSteps - 1u) - step];
}

static bool ShouldSkipEffect(AppTask::EffectMode mode)
{
    if (!sDisableStartupEffects)
    {
        return false;
    }

    return (mode == AppTask::EffectMode::BootBreathing)
        || (mode == AppTask::EffectMode::Identify)
        || (mode == AppTask::EffectMode::PairSuccess);
}

static void GetPresetBasePermilles(size_t presetIndex, uint16_t & w, uint16_t & r, uint16_t & g, uint16_t & b)
{
    if (presetIndex >= kPresetCount)
    {
        w = 0;
        r = 0;
        g = 0;
        b = 0;
        return;
    }

    const ButtonPresetEntry & p = kPresets[presetIndex];
    w = p.wPermille;
    r = p.rPermille;
    g = p.gPermille;
    b = p.bPermille;
}

static float SrgbChannelToLinear(uint8_t channel)
{
    const float v = static_cast<float>(channel) / 255.0f;
    return (v <= 0.04045f) ? (v / 12.92f) : powf((v + 0.055f) / 1.055f, 2.4f);
}

static void PermilleRgbToXy(uint16_t rPermille, uint16_t gPermille, uint16_t bPermille, uint16_t & xOut, uint16_t & yOut)
{
    const uint8_t r8 = static_cast<uint8_t>((static_cast<uint32_t>(rPermille) * 255u + 500u) / 1000u);
    const uint8_t g8 = static_cast<uint8_t>((static_cast<uint32_t>(gPermille) * 255u + 500u) / 1000u);
    const uint8_t b8 = static_cast<uint8_t>((static_cast<uint32_t>(bPermille) * 255u + 500u) / 1000u);

    const float rL = SrgbChannelToLinear(r8);
    const float gL = SrgbChannelToLinear(g8);
    const float bL = SrgbChannelToLinear(b8);

    const float X = 0.4124564f * rL + 0.3575761f * gL + 0.1804375f * bL;
    const float Y = 0.2126729f * rL + 0.7151522f * gL + 0.0721750f * bL;
    const float Z = 0.0193339f * rL + 0.1191920f * gL + 0.9503041f * bL;
    const float sum = X + Y + Z;
    if (sum <= 0.0001f)
    {
        xOut = 0x616B;
        yOut = 0x607D;
        return;
    }

    xOut = static_cast<uint16_t>(((X / sum) * 65535.0f) + 0.5f);
    yOut = static_cast<uint16_t>(((Y / sum) * 65535.0f) + 0.5f);
}

static ColorControl::ColorModeEnum SyncMatterAttributesForPreset(size_t presetIndex)
{
    if (presetIndex >= kPresetCount)
    {
        return ColorControl::ColorModeEnum::kCurrentXAndCurrentY;
    }

    const ButtonPresetEntry & p = kPresets[presetIndex];
    if (p.ctMireds != 0u)
    {
        ColorControl::Attributes::ColorMode::Set(LIGHT_ENDPOINT, ColorControl::ColorModeEnum::kColorTemperatureMireds);
        ColorControl::Attributes::ColorTemperatureMireds::Set(LIGHT_ENDPOINT, p.ctMireds);
        return ColorControl::ColorModeEnum::kColorTemperatureMireds;
    }

    uint16_t colorX = 0;
    uint16_t colorY = 0;
    PermilleRgbToXy(p.rPermille, p.gPermille, p.bPermille, colorX, colorY);
    ColorControl::Attributes::ColorMode::Set(LIGHT_ENDPOINT, ColorControl::ColorModeEnum::kCurrentXAndCurrentY);
    ColorControl::Attributes::CurrentX::Set(LIGHT_ENDPOINT, colorX);
    ColorControl::Attributes::CurrentY::Set(LIGHT_ENDPOINT, colorY);
    return ColorControl::ColorModeEnum::kCurrentXAndCurrentY;
}

static void ReportLightColorAttributes(ColorControl::ColorModeEnum colorMode)
{
    MatterReportingAttributeChangeCallback(LIGHT_ENDPOINT, ColorControl::Id, ColorControl::Attributes::ColorMode::Id);
    if (colorMode == ColorControl::ColorModeEnum::kColorTemperatureMireds)
    {
        MatterReportingAttributeChangeCallback(LIGHT_ENDPOINT, ColorControl::Id,
                                             ColorControl::Attributes::ColorTemperatureMireds::Id);
    }
    else if (colorMode == ColorControl::ColorModeEnum::kCurrentXAndCurrentY)
    {
        MatterReportingAttributeChangeCallback(LIGHT_ENDPOINT, ColorControl::Id, ColorControl::Attributes::CurrentX::Id);
        MatterReportingAttributeChangeCallback(LIGHT_ENDPOINT, ColorControl::Id, ColorControl::Attributes::CurrentY::Id);
    }
    else
    {
        MatterReportingAttributeChangeCallback(LIGHT_ENDPOINT, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id);
        MatterReportingAttributeChangeCallback(LIGHT_ENDPOINT, ColorControl::Id,
                                             ColorControl::Attributes::CurrentSaturation::Id);
    }
}

static void ReportLightStateAttributes(ColorControl::ColorModeEnum colorMode, bool reportOnOffLevel)
{
    if (reportOnOffLevel)
    {
        MatterReportingAttributeChangeCallback(LIGHT_ENDPOINT, OnOff::Id, OnOff::Attributes::OnOff::Id);
        MatterReportingAttributeChangeCallback(LIGHT_ENDPOINT, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    }
    ReportLightColorAttributes(colorMode);
}

static void SyncAndReportLightStateToApp(const char * reason)
{
    chip::DeviceLayer::PlatformMgr().LockChipStack();

    MatterSyncPowerOnAttributesFromMemory();

    ColorControl::ColorModeEnum reportColorMode = ColorControl::ColorModeEnum::kCurrentHueAndCurrentSaturation;
    if (sButtonPresetLatched && sPresetIndex < kPresetCount)
    {
        reportColorMode = SyncMatterAttributesForPreset(sPresetIndex);
    }
    else if (MatterGetIsColorTempMode() != 0u)
    {
        reportColorMode = ColorControl::ColorModeEnum::kColorTemperatureMireds;
        ColorControl::Attributes::ColorMode::Set(LIGHT_ENDPOINT, reportColorMode);
        ColorControl::Attributes::ColorTemperatureMireds::Set(LIGHT_ENDPOINT, MatterGetRuntimeColorTempMireds());
    }

    ReportLightStateAttributes(reportColorMode, true);

    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    ChipLogError(Zcl,
                 "[DIM] reported light state reason=%s preset_latched=%u preset=%u ct_mode=%u",
                 (reason != nullptr) ? reason : "?",
                 static_cast<unsigned>(sButtonPresetLatched ? 1u : 0u),
                 static_cast<unsigned>(sButtonPresetLatched ? (sPresetIndex + 1u) : 0u),
                 static_cast<unsigned>(MatterGetIsColorTempMode()));
}

static void CaptureState(PreEffectState & state)
{
    bool onoff = true;
    app::DataModel::Nullable<uint8_t> level;
    uint16_t ct = 370;
    uint8_t hue = 0;
    uint8_t sat = 0;

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    OnOff::Attributes::OnOff::Get(LIGHT_ENDPOINT, &onoff);
    if (LevelControl::Attributes::CurrentLevel::Get(LIGHT_ENDPOINT, level) == Protocols::InteractionModel::Status::Success &&
        !level.IsNull())
    {
        state.level = level.Value();
    }
    else
    {
        state.level = kLevelMax;
    }
    ColorControl::Attributes::ColorTemperatureMireds::Get(LIGHT_ENDPOINT, &ct);
    ColorControl::Attributes::CurrentHue::Get(LIGHT_ENDPOINT, &hue);
    ColorControl::Attributes::CurrentSaturation::Get(LIGHT_ENDPOINT, &sat);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    state.onOff = onoff ? 1u : 0u;
    state.ct = ct;
    state.hue = hue;
    state.sat = sat;
    state.colorSource = MatterGetColorSource();
    state.presetActive = MatterGetButtonPresetActive();
    MatterGetCurrentOutputRgbw(&state.rgbR, &state.rgbG, &state.rgbB, &state.whiteDuty);
    state.buttonPresetLatched = sButtonPresetLatched;
    state.presetIndex = sPresetIndex;
}

static void SaveButtonPresetMemoryState()
{
    ButtonPresetMemoryState state = {};
    state.version = kButtonPresetMemoryVersion;
    state.latched = sButtonPresetLatched ? 1u : 0u;
    state.presetIndex = static_cast<uint8_t>(sPresetIndex);

    CHIP_ERROR err = chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr().Put(kButtonPresetMemoryKey, &state, sizeof(state));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(Zcl, "[DIM] failed to save preset memory: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

static void RestoreButtonPresetMemoryState()
{
    ButtonPresetMemoryState state = {};
    CHIP_ERROR err = chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr().Get(kButtonPresetMemoryKey, &state, sizeof(state));
    if (err != CHIP_NO_ERROR || state.version != kButtonPresetMemoryVersion)
    {
        sButtonPresetLatched = false;
        MatterSetButtonPresetActive(0);
        return;
    }

    if (state.latched == 0 || state.presetIndex >= kPresetCount)
    {
        sButtonPresetLatched = false;
        MatterSetButtonPresetActive(0);
        return;
    }

    if (MatterGetColorSource() != 1u)
    {
        sButtonPresetLatched = false;
        MatterSetButtonPresetActive(0);
        return;
    }

    sButtonPresetLatched = true;
    sPresetIndex = state.presetIndex;
    MatterSetButtonPresetActive(1);

    uint16_t wBase = 0;
    uint16_t rBase = 0;
    uint16_t gBase = 0;
    uint16_t bBase = 0;
    GetPresetBasePermilles(sPresetIndex, wBase, rBase, gBase, bBase);
    MatterRestoreButtonPresetPermilles(wBase, rBase, gBase, bBase);
    ChipLogError(Zcl, "[DIM] restored preset memory: preset=%u base_permille=%u,%u,%u,%u",
                 static_cast<unsigned>(sPresetIndex + 1),
                 static_cast<unsigned>(wBase), static_cast<unsigned>(rBase),
                 static_cast<unsigned>(gBase), static_cast<unsigned>(bBase));
}

extern "C" void MatterClearButtonPresetLatch(void)
{
    if (!sButtonPresetLatched)
    {
        return;
    }

    sButtonPresetLatched = false;
    SaveButtonPresetMemoryState();
}

static void ApplyButtonPresetAtIndex(size_t presetIndex, uint8_t level254)
{
    if (presetIndex >= kPresetCount)
    {
        return;
    }

    sPresetIndex = presetIndex;
    const ButtonPresetEntry & p = kPresets[sPresetIndex];

    MatterSetOffTransitionActive(0);
    MatterSetButtonPresetSuppressColorCallbacks(4);
    MatterSetButtonPresetTransaction(1);
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    const ColorControl::ColorModeEnum colorMode = SyncMatterAttributesForPreset(sPresetIndex);
    OnOff::Attributes::OnOff::Set(LIGHT_ENDPOINT, true);
    ReportLightStateAttributes(colorMode, true);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    MatterSetButtonPresetTransaction(0);

    sButtonPresetLatched = true;
    MatterSetButtonPresetPwmWithFade(p.wPermille, p.rPermille, p.gPermille, p.bPermille, level254);
    ChipLogError(Zcl, "[DIM] button preset=%u base_permille=%u,%u,%u,%u level254=%u matter_mode=%u hue=%u",
                 static_cast<unsigned>(sPresetIndex + 1),
                 static_cast<unsigned>(p.wPermille), static_cast<unsigned>(p.rPermille),
                 static_cast<unsigned>(p.gPermille), static_cast<unsigned>(p.bPermille),
                 static_cast<unsigned>(level254),
                 static_cast<unsigned>(p.ctMireds != 0u ? 0u : 1u),
                 static_cast<unsigned>(p.hue));

    SaveButtonPresetMemoryState();
    MatterSavePowerOnMemorySnapshot();
}

static void PrintPairingQrUrlToRtt()
{
    const chip::RendezvousInformationFlags rendezvousFlags(chip::RendezvousInformationFlag::kBLE,
                                                           chip::RendezvousInformationFlag::kOnNetwork);
    chip::PayloadContents payload;
    CHIP_ERROR err = GetPayloadContents(payload, rendezvousFlags);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "[DBG] GetPayloadContents failed: %" CHIP_ERROR_FORMAT, err.Format());
        return;
    }

    ChipLogError(DeviceLayer, "[PAIR] SetupDiscriminator=%u SetupPasscode=%" PRIu32,
                 static_cast<unsigned>(payload.discriminator.GetLongValue()), static_cast<uint32_t>(payload.setUpPINCode));

    char qrBuffer[chip::QRCodeBasicSetupPayloadGenerator::kMaxQRCodeBase38RepresentationLength + 1] = { 0 };
    chip::MutableCharSpan qrCode(qrBuffer);
    err = GetQRCode(qrCode, payload);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "[DBG] GetQRCode failed: %" CHIP_ERROR_FORMAT, err.Format());
        return;
    }

    char qrUrl[1024] = { 0 };
    err = GetQRCodeUrl(qrUrl, sizeof(qrUrl), chip::CharSpan(qrCode.data(), qrCode.size()));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "[DBG] GetQRCodeUrl failed: %" CHIP_ERROR_FORMAT, err.Format());
        return;
    }

    ChipLogError(DeviceLayer, "[PAIR] QR URL: %s", qrUrl);
    printf("[RTT] Pairing QR URL: %s\n", qrUrl);
}

void AppTimerCallback(void * context)
{
    AppEvent event = {};
    event.Type = AppEvent::kEventType_Timer;
    event.TimerEvent.Context = context;
    event.Handler = AppTask::ButtonTimerEventHandler;
    AppTask::GetAppTask().PostEvent(&event);
}



#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
RGBLEDWidget sLightLED; // Use RGBLEDWidget if RGB LED functionality is enabled
#else
LEDWidget sLightLED; // Use LEDWidget for basic LED functionality
#endif

// Array of attributes that will have their non-volatile storage deferred/delayed.
// This is useful for attributes that change frequently over short periods of time, such as during transitions.
// In this example, we defer the storage of the Level Control's CurrentLevel attribute and the Color Control's
// CurrentHue and CurrentSaturation attributes for the LIGHT_ENDPOINT.
DeferredAttribute gDeferredAttributeTable[] = {
    DeferredAttribute(ConcreteAttributePath(LIGHT_ENDPOINT, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id)),
    DeferredAttribute(ConcreteAttributePath(LIGHT_ENDPOINT, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id)),
    DeferredAttribute(ConcreteAttributePath(LIGHT_ENDPOINT, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id))
};
} // namespace

using namespace chip::TLV;
using namespace ::chip::DeviceLayer;

AppTask AppTask::sAppTask;
osTimerId_t AppTask::sClickTimer = nullptr;
osTimerId_t AppTask::sLongPressTimer = nullptr;
osTimerId_t AppTask::sEffectTimer = nullptr;
osTimerId_t AppTask::sPostResetWindowTimer = nullptr;
osTimerId_t AppTask::sBootDefaultTimer = nullptr;
osTimerId_t AppTask::sIdentifyTimer = nullptr;
osTimerId_t AppTask::sResetOffTimer = nullptr;
CHIP_ERROR AppTask::AppInit()
{
    // 仅保留Error日志，最大化屏蔽协议栈通信细节日志
    chip::Logging::SetLogFilter(chip::Logging::kLogCategory_Error);
    CHIP_ERROR err = CHIP_NO_ERROR;
    uint32_t rebootCauseRaw = chip::DeviceLayer::Silabs::GetPlatform().GetRebootCause();
    uint32_t bootReason     = 0;

    if (gResetDiagMagic != kResetDiagMagic)
    {
        gResetDiagMagic         = kResetDiagMagic;
        gResetBootCount         = 0;
        gResetLastFaultSignature = 0;
        gResetLastFaultValue     = 0;
        gResetLastRebootCause    = 0;
        gEarlyEmuResetCause      = 0;
        gEarlyEmuResetCauseSeen  = 0;
    }

    const uint32_t prevFaultSignature = gResetLastFaultSignature;
    const uint32_t prevFaultValue     = gResetLastFaultValue;
    const uint32_t prevRebootCauseRaw = gResetLastRebootCause;
    const uint32_t earlyEmuResetCause = gEarlyEmuResetCause;
    // 已移除 cachedEmuResetCause 相关代码
    const uint32_t lastHalSysResetCause = gLastHalSysResetCause;
    const uint32_t lastHalSysResetSeen  = gLastHalSysResetSeen;
    gResetBootCount++;
    gResetLastRebootCause = rebootCauseRaw;


    ChipLogError(DeviceLayer,
                 "[RST] BC=%" PRIu32 " PF=0x%08" PRIx32 " PV=0x%08" PRIx32 " RR=0x%08" PRIx32
                 " EEMU=0x%08" PRIx32 " HSR=0x%08" PRIx32 " HSS=%" PRIu32,
                 static_cast<uint32_t>(gResetBootCount), prevFaultSignature, prevFaultValue, prevRebootCauseRaw,
                 earlyEmuResetCause, lastHalSysResetCause, lastHalSysResetSeen);





    gResetLastFaultSignature = 0;
    gResetLastFaultValue     = 0;

    chip::DeviceLayer::Silabs::GetPlatform().SetButtonsCb(AppTask::ButtonEventHandler);

    sClickTimer = osTimerNew(AppTimerCallback, osTimerOnce, reinterpret_cast<void *>(kTimerCtxClick), nullptr);
    sLongPressTimer = osTimerNew(AppTimerCallback, osTimerPeriodic, reinterpret_cast<void *>(kTimerCtxLong), nullptr);
    sEffectTimer = osTimerNew(AppTimerCallback, osTimerPeriodic, reinterpret_cast<void *>(kTimerCtxEffect), nullptr);
    sPostResetWindowTimer = osTimerNew(AppTimerCallback, osTimerOnce, reinterpret_cast<void *>(kTimerCtxCommissioning), nullptr);
    sBootDefaultTimer = osTimerNew(AppTimerCallback, osTimerOnce, reinterpret_cast<void *>(kTimerCtxBootBreathEnd), nullptr);
    sIdentifyTimer = osTimerNew(AppTimerCallback, osTimerOnce, reinterpret_cast<void *>(kTimerCtxIdentifyEnd), nullptr);
    sResetOffTimer = osTimerNew(AppTimerCallback, osTimerOnce, reinterpret_cast<void *>(kTimerCtxResetOff), nullptr);
    VerifyOrReturnError(sClickTimer != nullptr && sLongPressTimer != nullptr && sEffectTimer != nullptr &&
                            sPostResetWindowTimer != nullptr && sBootDefaultTimer != nullptr && sIdentifyTimer != nullptr &&
                            sResetOffTimer != nullptr,
                        APP_ERROR_CREATE_TIMER_FAILED);

    chip::DeviceLayer::PlatformMgr().AddEventHandler(OnPlatformEvent, 0);

    err = ConfigurationMgr().GetBootReason(bootReason);
    if (err == CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "[DBG] RebootCauseRaw=0x%08" PRIx32 ", BootReason=%" PRIu32, rebootCauseRaw, bootReason);
    }
    else
    {
        ChipLogError(DeviceLayer, "[DBG] GetBootReason failed: %" CHIP_ERROR_FORMAT ", RebootCauseRaw=0x%08" PRIx32, err.Format(),
                     rebootCauseRaw);
    }

    auto * commissionableDataProvider = DeviceLayer::GetCommissionableDataProvider();
    if (commissionableDataProvider != nullptr)
    {
        uint32_t setupPasscode = 0;
        uint16_t discriminator = 0;
        CHIP_ERROR passcodeErr = commissionableDataProvider->GetSetupPasscode(setupPasscode);
        CHIP_ERROR discErr     = commissionableDataProvider->GetSetupDiscriminator(discriminator);
        ChipLogProgress(DeviceLayer,
                        "[DBG] SetupPasscodeErr=%" CHIP_ERROR_FORMAT " SetupPasscode=%" PRIu32
                        " SetupDiscriminatorErr=%" CHIP_ERROR_FORMAT " SetupDiscriminator=%u",
                        passcodeErr.Format(), setupPasscode, discErr.Format(), discriminator);
    }
    else
    {
        ChipLogError(DeviceLayer, "[DBG] CommissionableDataProvider is null");
    }

    err = LightMgr().Init();
    if (err != CHIP_NO_ERROR)
    {
        SILABS_LOG("LightMgr::Init() failed");
        appError(err);
    }

    LightMgr().SetCallbacks(ActionInitiated, ActionCompleted);

    sLightLED.Init(LIGHT_LED);
    // Temporary: Send RED test color to verify 1-bits in waveform
    // Comment this out after waveform validation
    // MatterApplyRgbwNow(255u, 0u, 0u, 0u);
    MatterApplyRgbwNow(0u, 0u, 0u, 0u);
#if !(defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    sLightLED.Set(LightMgr().IsLightOn());
#endif
    SILABS_TRACE_NAMED_INSTANT("LightOn", "Reboot");

// Update the LCD with the Stored value. Show QR Code if not provisioned
#ifdef DISPLAY_ENABLED
    GetLCD().WriteDemoUI(LightMgr().IsLightOn());
#ifdef QR_CODE_ENABLED
#ifdef SL_WIFI
    if (!ConnectivityMgr().IsWiFiStationProvisioned())
#else
    if (!ConnectivityMgr().IsThreadProvisioned())
#endif /* !SL_WIFI */
    {
        GetLCD().ShowQRCode(true);
    }
#endif // QR_CODE_ENABLED
#endif

    BaseApplication::InitCompleteCallback(err);

    bool hasPowerOnMemory = MatterRestorePowerOnMemoryIfAny();
    RestoreButtonPresetMemoryState();

    uint8_t resetBootMark = 0;
    const CHIP_ERROR resetBootErr = chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr().Get(kFactoryResetBootKey, &resetBootMark,
                                                                                                sizeof(resetBootMark));
    const bool hasResetBootMark = (resetBootErr == CHIP_NO_ERROR && resetBootMark == 1);
    if (hasResetBootMark)
    {
        chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr().Delete(kFactoryResetBootKey);
    }

    if (!BaseApplication::sIsProvisioned)
    {
        const bool doBootBreath = !sDisableStartupEffects;
        sStartupSingleWhiteLock = doBootBreath;

        if (hasResetBootMark)
        {
            // 长按恢复出厂后的冷启动：先灭灯 2s，再由 sResetOffTimer 触发 StartBootBreathing()
            ApplyRgbwEffect(0, 0, 0, 0);
            osTimerStart(sResetOffTimer, pdMS_TO_TICKS(APP_RESET_BOOT_OFF_MS));
        }
        else
        {
            // 首次上电未配网：开配网窗口 + 启动白光呼吸
            StartCommissioningWindow();
            StartBootBreathing();
        }
    }
    else
    {
        sStartupSingleWhiteLock = false;
        if (!hasPowerOnMemory)
        {
            ChipLogError(Zcl, "[DIM] no power-on memory snapshot, keep current restored attributes");
        }
    }

    // Zigbee::RequestStart() runs after AppInit and may replay CT/OnOff attributes that
    // flash white PWM. Re-apply power-on output once init finishes, then allow attribute HW.
    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t context) {
            if (context != 0)
            {
                MatterReapplyPowerOnMemoryOutput();
            }
            MatterSetBootOutputSuppress(0);
        },
        BaseApplication::sIsProvisioned ? 1 : 0);

    return err;
}

CHIP_ERROR AppTask::StartAppTask()
{
    return BaseApplication::StartAppTask(AppTaskMain);
}

void AppTask::AppTaskMain(void * pvParameter)
{
    AppEvent event;
    osMessageQueueId_t sAppEventQueue = *(static_cast<osMessageQueueId_t *>(pvParameter));

    // Initialization that needs to happen before the BaseInit is called here as the BaseApplication::Init() will call
    // the AppInit() after BaseInit.

    // Retrieve the existing AttributePersistenceProvider, which should already be created and initialized.
    // This provider is typically set up by the CodegenDataModelProviderInstance constructor,
    // which is called in InitMatter within MatterConfig.cpp.
    // We use this as the base provider for deferred attribute persistence.
    AttributePersistenceProvider * attributePersistence = GetAttributePersistenceProvider();




    CHIP_ERROR err = sAppTask.Init();
    if (err != CHIP_NO_ERROR)
    {
        SILABS_LOG("AppTask.Init() failed");
        appError(err);
    }

#if !(defined(CHIP_CONFIG_ENABLE_ICD_SERVER) && CHIP_CONFIG_ENABLE_ICD_SERVER)
    sAppTask.StartStatusLEDTimer();
#endif

    SILABS_LOG("App Task started");
    PrintPairingQrUrlToRtt();

    while (true)
    {
        osStatus_t eventReceived = osMessageQueueGet(sAppEventQueue, &event, nullptr, osWaitForever);
        while (eventReceived == osOK)
        {
            sAppTask.DispatchEvent(&event);
            eventReceived = osMessageQueueGet(sAppEventQueue, &event, nullptr, 0);
        }
    }
}

void AppTask::LightActionEventHandler(AppEvent * aEvent)
{
    bool initiated = false;
    LightingManager::Action_t action;
    int32_t actor;
    uint8_t value  = aEvent->LightEvent.Value;
    CHIP_ERROR err = CHIP_NO_ERROR;

    if (aEvent->Type == AppEvent::kEventType_Light)
    {
        action = static_cast<LightingManager::Action_t>(aEvent->LightEvent.Action);
        actor  = aEvent->LightEvent.Actor;
    }
    else if (aEvent->Type == AppEvent::kEventType_Button)
    {
        action = (LightMgr().IsLightOn()) ? LightingManager::OFF_ACTION : LightingManager::ON_ACTION;
        actor  = AppEvent::kEventType_Button;
    }
    else
    {
        err = APP_ERROR_UNHANDLED_EVENT;
    }

    if (err == CHIP_NO_ERROR)
    {
        initiated = LightMgr().InitiateAction(actor, action, &value);

        if (!initiated)
        {
            SILABS_LOG("Action is already in progress or active.");
        }
    }
}

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
void AppTask::LightControlEventHandler(AppEvent * aEvent)
{
    uint8_t light_action                = aEvent->LightControlEvent.Action;
    RGBLEDWidget::ColorData_t colorData = aEvent->LightControlEvent.Value;

    if (sStartupSingleWhiteLock)
    {
        // During startup, keep RGB path fully off to avoid visible RGB white flash.
        MatterApplyRgbwNow(0u, 0u, 0u, 0u);
        ChipLogError(Zcl, "[DIM] startup single-white lock: ignore color action=%u", static_cast<unsigned>(light_action));
        return;
    }

    // Get currentLevel attribute
    PlatformMgr().LockChipStack();
    Protocols::InteractionModel::Status status;
    app::DataModel::Nullable<uint8_t> currentlevel;
    // Read currentlevel value
    status = LevelControl::Attributes::CurrentLevel::Get(LIGHT_ENDPOINT, currentlevel);
    PlatformMgr().UnlockChipStack();
    VerifyOrReturn(Protocols::InteractionModel::Status::Success == status,
                   ChipLogError(NotSpecified, "Failed to get CurrentLevel attribute"));
    if (status == Protocols::InteractionModel::Status::Success && !currentlevel.IsNull())
    {
        sLightLED.SetLevel(currentlevel.Value());
    }
    switch (light_action)
    {
    case LightingManager::COLOR_ACTION_XY: {
        sLightLED.SetColorFromXY(colorData.xy.x, colorData.xy.y);
    }
    break;
    case LightingManager::COLOR_ACTION_HSV: {
        sLightLED.SetColorFromHSV(colorData.hsv.h, colorData.hsv.s);
    }
    break;
    case LightingManager::COLOR_ACTION_CT: {
        sLightLED.SetColorFromCT(colorData.ct.ctMireds);
    }
    break;
    default:
        ChipLogProgress(NotSpecified, "LightMgr:Unknown");
        break;
    }
}
#endif // (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED)

void AppTask::ApplyWhiteEffectLevel(uint8_t level)
{
    ApplyWhitePwmEffect(level);
}


static void RestoreState(const PreEffectState & state)
{
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    OnOff::Attributes::OnOff::Set(LIGHT_ENDPOINT, state.onOff != 0u);
    LevelControl::Attributes::CurrentLevel::Set(LIGHT_ENDPOINT, state.level);
    ColorControl::Attributes::ColorTemperatureMireds::Set(LIGHT_ENDPOINT, state.ct);
    ColorControl::Attributes::CurrentHue::Set(LIGHT_ENDPOINT, state.hue);
    ColorControl::Attributes::CurrentSaturation::Set(LIGHT_ENDPOINT, state.sat);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    if (state.onOff == 0u)
    {
        MatterRestoreOutputState(0, 0, 0, 0, state.colorSource, 0);
        MatterSetButtonPresetActive(0);
        sButtonPresetLatched = false;
        SaveButtonPresetMemoryState();
        MatterSavePowerOnMemorySnapshot();
        return;
    }

    if (state.colorSource == 1u && state.buttonPresetLatched && state.presetIndex < kPresetCount)
    {
        sPresetIndex = state.presetIndex;
        sButtonPresetLatched = true;
        MatterSetButtonPresetActive(1);
        const ButtonPresetEntry & p = kPresets[sPresetIndex];
        MatterSetButtonPresetPwmWithFade(p.wPermille, p.rPermille, p.gPermille, p.bPermille, state.level);
    }
    else
    {
        sButtonPresetLatched = false;
        MatterSetButtonPresetActive(0);
        MatterRestoreOutputState(state.rgbR, state.rgbG, state.rgbB, state.whiteDuty, state.colorSource, state.presetActive);
    }

    SaveButtonPresetMemoryState();
    MatterSavePowerOnMemorySnapshot();
}

void AppTask::TriggerFactoryResetAfterLongPress()
{
    ApplyRgbwEffect(0, 0, 0, 0);

    gResetLastFaultSignature = kLongPressResetSignature;
    gResetLastFaultValue     = 10;

    uint8_t mark = 1;
    CHIP_ERROR markErr = chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr().Put(kFactoryResetBootKey, &mark, sizeof(mark));
    if (markErr != CHIP_NO_ERROR)
    {
        ChipLogError(Zcl, "[DIM] failed to store reset boot marker: %" CHIP_ERROR_FORMAT, markErr.Format());
    }
    ChipLogError(Zcl, "[DIM] long press 10s reset triggered");
    chip::Server::GetInstance().ScheduleFactoryReset();
}

void AppTask::StartResetWarnEndEffect(bool pendingFactoryReset)
{
    sResetPendingAfterWarnEnd = pendingFactoryReset;
    sResetEndSequenceActive = true;
    sDimmingActive = false;
    osTimerStop(sLongPressTimer);
    StartEffect(EffectMode::ResetWarnEnd);
    ChipLogError(Zcl, "[DIM] reset warn end slow blink (reset=%u)", pendingFactoryReset ? 1u : 0u);
}

void AppTask::CancelResetWarningSequence()
{
    sResetPendingAfterWarnEnd = false;
    sResetEndSequenceActive = false;
    sResetTriggered = false;
    sResetWarnActive = false;
    sDimmingActive = false;
    osTimerStop(sLongPressTimer);
    sLongPressMs = 0;

    if (sEffectMode == EffectMode::ResetWarn || sEffectMode == EffectMode::ResetWarnEnd)
    {
        StopEffect();
    }

    RestoreState(sPreEffectState);
    ChipLogError(Zcl, "[DIM] reset warning sequence cancelled");
}

void AppTask::FinishResetWarnEndEffect()
{
    const bool pendingReset = sResetPendingAfterWarnEnd;
    sResetPendingAfterWarnEnd = false;
    sResetEndSequenceActive = false;
    sResetWarnActive = false;
    StopEffect();
    ApplyRgbwEffect(0, 0, 0, 0);

    if (pendingReset)
    {
        TriggerFactoryResetAfterLongPress();
        return;
    }

    RestoreState(sPreEffectState);
    sLongPressMs = 0;
    ChipLogError(Zcl, "[DIM] long press interrupted between 5s and 10s");
}

void AppTask::BeginPairSuccessEffect(const char * reason)
{
    if (sEffectMode == EffectMode::PairSuccess)
    {
        ChipLogError(Zcl, "[BLINK2] PAIR duplicate ignored reason=%s active_seq=%u",
                     (reason != nullptr) ? reason : "?",
                     static_cast<unsigned>(sPairSuccessSessionSeq));
        return;
    }

    sPairSuccessSessionSeq++;
    sPairSuccessFlashCount = 0u;
    sPairSuccessLastOn = false;
    const bool wasBreathing = sBootBreathingActive;
    const bool wasPending = sPairSuccessPending;
    const EffectMode prevEffect = sEffectMode;
    sPairSuccessPending = false;
    sBootBreathingActive = false;
    osTimerStop(sBootDefaultTimer);
    MatterApplyRgbwNow(0u, 0u, 0u, 0u);
    StartEffect(EffectMode::PairSuccess);
    ChipLogError(Zcl,
                 "[BLINK2] PAIR start seq=%u reason=%s provisioned=%u breath=%u pending=%u prev_effect=%u",
                 static_cast<unsigned>(sPairSuccessSessionSeq),
                 (reason != nullptr) ? reason : "?",
                 static_cast<unsigned>(BaseApplication::sIsProvisioned ? 1u : 0u),
                 static_cast<unsigned>(wasBreathing ? 1u : 0u),
                 static_cast<unsigned>(wasPending ? 1u : 0u),
                 static_cast<unsigned>(prevEffect));
}

void AppTask::FinishPairSuccessEffect()
{
    const uint32_t doneTick = sEffectTickMs;
    const uint8_t flashCount = sPairSuccessFlashCount;
    const uint32_t seq = sPairSuccessSessionSeq;
    StopEffect();
    sStartupSingleWhiteLock = false;
    MatterApplyRgbwNow(0u, 0u, 0u, 0u);
    MatterSetOffTransitionActive(1);
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    OnOff::Attributes::OnOff::Set(LIGHT_ENDPOINT, false);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    MatterSetOffTransitionActive(0);
    ChipLogError(Zcl, "[BLINK2] PAIR done seq=%u flashes=%u tick=%u",
                 static_cast<unsigned>(seq),
                 static_cast<unsigned>(flashCount),
                 static_cast<unsigned>(doneTick));
    ChipLogError(Zcl, "[DIM] pairing success effect finished");
}

void AppTask::StartEffect(EffectMode mode)
{
    if (ShouldSkipEffect(mode))
    {
        ChipLogError(Zcl, "[EFFECT] skip mode=%u", static_cast<unsigned>(mode));
        sEffectMode = EffectMode::None;
        sEffectTickMs = 0;
        return;
    }
    sEffectMode = mode;
    sEffectTickMs = 0;
    ChipLogError(AppServer, "[IDENTIFY] StartEffect mode=%u", static_cast<unsigned>(mode));
    osTimerStart(sEffectTimer, pdMS_TO_TICKS(APP_EFFECT_TICK_MS));
}

void AppTask::StopEffect()
{
    ChipLogError(Zcl, "[EFFECT] stop mode=%u tick=%u", static_cast<unsigned>(sEffectMode),
                 static_cast<unsigned>(sEffectTickMs));
    sEffectMode = EffectMode::None;
    sEffectTickMs = 0;
    osTimerStop(sEffectTimer);
}

void AppTask::RunEffectStep()
{
    if (sEffectMode == EffectMode::None)
    {
        return;
    }

    // -------------------------------------------------------------------------
    // 未配网呼吸灯（EffectMode::BootBreathing）
    // 由 sEffectTimer 每 APP_EFFECT_TICK_MS(10ms) 调用一次 RunEffectStep()。
    // sEffectTickMs 递增，对 APP_BOOT_BREATH_CYCLE_MS 取模得到周期内位置 t。
    // 四段波形：渐亮(0→1000‰) → 保持最亮 → 渐灭(1000‰→0) → 保持熄灭，然后循环。
    // 每步查表得 whitePermille，再调用 MatterApplyWhiteBreathPermille() 写 timer compare。
    // -------------------------------------------------------------------------
    if (sEffectMode == EffectMode::BootBreathing)
    {
        const uint32_t cycleMs = APP_BOOT_BREATH_CYCLE_MS;
        const uint32_t rampMs = APP_BOOT_BREATH_RAMP_MS;
        const uint32_t holdMs = APP_BOOT_BREATH_HOLD_MS;
        const uint32_t t = sEffectTickMs % cycleMs; // 当前周期内时间位置 [0, cycleMs)
        uint16_t whitePermille = 0u;

        if (t < rampMs) {
            // 第 1 段：查表渐亮 0 → 1000‰
            whitePermille = BootBreathRampPermille(t, true);
        } else if (t < (rampMs + holdMs)) {
            // 第 2 段：保持最亮
            whitePermille = 1000u;
        } else if (t < (rampMs + holdMs + rampMs)) {
            // 第 3 段：查表渐灭 1000‰ → 0
            const uint32_t td = t - (rampMs + holdMs);
            whitePermille = BootBreathRampPermille(td, false);
        } else {
            // 第 4 段：保持熄灭
            whitePermille = 0u;
        }

        ApplyWhitePwmEffectPermille(whitePermille);

        // 配网成功且处于“渐灭完成后的熄灭段”时，切到配网成功快闪（PairSuccess）
        const uint32_t offHoldStart = rampMs + holdMs + rampMs;
        if (sPairSuccessPending && t >= offHoldStart && whitePermille == 0u)
        {
            BeginPairSuccessEffect("breath-off");
            return;
        }

        sEffectTickMs += APP_EFFECT_TICK_MS;
        return;
    }

    if (sEffectMode == EffectMode::ResetWarn)
    {
        const uint32_t period = APP_RESET_WARN_BLINK_MS;
        const bool on = ((sEffectTickMs / period) % 2u) == 0u;
        ApplyRgbwEffect(on ? 255u : 0u, 0u, 0u, 0u);
        sEffectTickMs += APP_EFFECT_TICK_MS;
        return;
    }

    if (sEffectMode == EffectMode::ResetWarnEnd)
    {
        const uint32_t offMs = APP_RESET_WARN_END_OFF_MS;
        const uint32_t onMs = APP_RESET_WARN_END_ON_MS;
        const uint32_t endMs = offMs + onMs;

        if (sEffectTickMs < offMs)
        {
            ApplyRgbwEffect(0u, 0u, 0u, 0u);
        }
        else if (sEffectTickMs < endMs)
        {
            ApplyRgbwEffect(255u, 0u, 0u, 0u);
        }
        else
        {
            FinishResetWarnEndEffect();
            return;
        }

        sEffectTickMs += APP_EFFECT_TICK_MS;
        return;
    }

    if (sEffectMode == EffectMode::PairSuccess)
    {
        const uint32_t blinkOn = APP_PAIR_SUCCESS_BLINK_ON_MS;
        const uint32_t blinkOff = APP_PAIR_SUCCESS_BLINK_OFF_MS;
        const uint32_t blinkUnit = blinkOn + blinkOff;
        const uint32_t totalBlinkMs = blinkUnit * 2u;

        if (sEffectTickMs < totalBlinkMs)
        {
            const uint32_t phase = sEffectTickMs % blinkUnit;
            const bool on = phase < blinkOn;
            if (on && !sPairSuccessLastOn)
            {
                sPairSuccessFlashCount++;
                ChipLogError(Zcl, "[BLINK2] PAIR flash=%u ON seq=%u tick=%u",
                             static_cast<unsigned>(sPairSuccessFlashCount),
                             static_cast<unsigned>(sPairSuccessSessionSeq),
                             static_cast<unsigned>(sEffectTickMs));
            }
            sPairSuccessLastOn = on;
            ApplyWhiteEffectLevel(on ? 100u : 0u);
            sEffectTickMs += APP_EFFECT_TICK_MS;
            return;
        }

        FinishPairSuccessEffect();
        return;
    }

    if (sEffectMode == EffectMode::Identify)
    {
        const uint32_t period = APP_IDENTIFY_BLINK_MS;
        const bool on = ((sEffectTickMs / period) % 2u) == 0u;
        if (on && !sIdentifyLastOn)
        {
            sIdentifyFlashCount++;
            ChipLogError(AppServer, "[BLINK2] IDENTIFY flash=%u ON seq=%u tick=%u",
                         static_cast<unsigned>(sIdentifyFlashCount),
                         static_cast<unsigned>(sIdentifySessionSeq),
                         static_cast<unsigned>(sEffectTickMs));
        }
        sIdentifyLastOn = on;
        ApplyWhiteEffectLevel(on ? 100u : 0u);
        sEffectTickMs += APP_EFFECT_TICK_MS;
        return;
    }
}

void AppTask::OnButtonPressed()
{
    if (sIdentifyActive || sEffectMode == EffectMode::Identify)
    {
        // Identify 优先级最高：按键不改变 Identify 输出
        return;
    }

    if (sEffectMode == EffectMode::ResetWarn || sEffectMode == EffectMode::ResetWarnEnd || sResetEndSequenceActive)
    {
        CancelResetWarningSequence();
        return;
    }

    if (sBootBreathingActive)
    {
        sBootBreathingActive = false;
        osTimerStop(sBootDefaultTimer);
        if (sEffectMode == EffectMode::BootBreathing)
        {
            StopEffect();
        }
        sStartupSingleWhiteLock = false;
        if (sPairSuccessPending)
        {
            BeginPairSuccessEffect("btn-breath-exit");
            return;
        }
        sBootBreathExitPending = true;
        ChipLogError(Zcl, "[DIM] boot breathing interrupted");
    }

    CaptureState(sPreEffectState);
    sButtonPressed = true;
    sLongPressMs = 0;
    sResetWarnActive = false;
    sResetTriggered = false;
    sResetPendingAfterWarnEnd = false;
    sResetEndSequenceActive = false;
    sDimmingActive = false;
    sDimmingLastAppliedLevel = 0xFF;
    osTimerStart(sLongPressTimer, pdMS_TO_TICKS(APP_DIMMING_STEP_MS));
}

void AppTask::OnButtonReleased()
{
    sButtonPressed = false;
    osTimerStop(sLongPressTimer);
    MatterSetButtonDimmingActive(0);

    if (sIdentifyActive)
    {
        // Identify 期间松手：清除可能残留的重置状态，避免 Identify 结束后误恢复红灯
        if (sResetWarnActive || sResetEndSequenceActive || sResetTriggered || sResetPendingAfterWarnEnd)
        {
            sResetPendingAfterWarnEnd = false;
            sResetEndSequenceActive = false;
            sResetTriggered = false;
            sResetWarnActive = false;
            sDimmingActive = false;
            sLongPressMs = 0;
        }
    }

    if (!sIdentifyActive)
    {
        if (sEffectMode == EffectMode::ResetWarnEnd || sResetEndSequenceActive)
        {
            // 慢闪收尾阶段且非即将恢复出厂：松手立即恢复，不播完慢闪序列
            if (!sResetPendingAfterWarnEnd && !sResetTriggered)
            {
                CancelResetWarningSequence();
            }
            return;
        }

        if (sResetTriggered)
        {
            return;
        }

        // 5~10s 快闪红灯警告期间松手：立即恢复先前状态，不触发慢闪收尾
        if (sEffectMode == EffectMode::ResetWarn || sResetWarnActive)
        {
            sBootBreathExitPending = false;
            CancelResetWarningSequence();
            sLongPressMs = 0;
            return;
        }

        if (sLongPressMs >= APP_LONG_PRESS_RESET_WARN_MS)
        {
            sBootBreathExitPending = false;
            CancelResetWarningSequence();
            sLongPressMs = 0;
            return;
        }

        if (sLongPressMs >= APP_LONG_PRESS_DIM_START_MS)
        {
            if (sDimmingActive)
            {
                MatterFinalizeButtonDimming(sDimmingLevelQ16, kLevelMin, kLevelMax);
            }
            sBootBreathExitPending = false;
            sDimmingActive = false;
            sLongPressMs = 0;
            return;
        }
    }
    else
    {
        sDimmingActive = false;
        sLongPressMs = 0;
    }

    // 未配网时任意短按：重置 15 分钟配网窗口（Matter 窗口 + 本地超时定时器）
    if (!BaseApplication::sIsProvisioned)
    {
        RestartCommissioningTimer();
    }

    if (sBootBreathExitPending)
    {
        sBootBreathExitPending = false;
        sImmediateSinglePending = false;
        sClickCount = 0;
        osTimerStop(sClickTimer);

        chip::DeviceLayer::PlatformMgr().LockChipStack();
        LevelControl::Attributes::CurrentLevel::Set(LIGHT_ENDPOINT, kLevelMax);
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        ApplyButtonPresetAtIndex(0, kLevelMax);
        return;
    }

    bool onoff = true;
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    OnOff::Attributes::OnOff::Get(LIGHT_ENDPOINT, &onoff);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    if (!sImmediateSinglePending && !onoff)
    {
        HandleSingleClick();
        sImmediateSinglePending = true;
        sClickCount = 0;
        osTimerStart(sClickTimer, pdMS_TO_TICKS(APP_DOUBLE_CLICK_WINDOW_MS));
        return;
    }

    if (sImmediateSinglePending)
    {
        sImmediateSinglePending = false;
        sClickCount = 0;
        osTimerStop(sClickTimer);
        HandleDoubleClick();
        return;
    }

    sClickCount++;
    osTimerStart(sClickTimer, pdMS_TO_TICKS(APP_DOUBLE_CLICK_WINDOW_MS));
}

void AppTask::HandleSingleClick()
{
    bool onoff = true;
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    OnOff::Attributes::OnOff::Get(LIGHT_ENDPOINT, &onoff);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    if (onoff)
    {
        MatterSnapshotLevelForOff();
        MatterSetOffTransitionActive(1);
        OnOff::Attributes::OnOff::Set(LIGHT_ENDPOINT, false);
        MatterReportingAttributeChangeCallback(LIGHT_ENDPOINT, OnOff::Id, OnOff::Attributes::OnOff::Id);
    }
    else
    {
        MatterSetOffTransitionActive(0);
        MatterSyncLevelBeforeOn();
        ColorControl::ColorModeEnum reportColorMode = ColorControl::ColorModeEnum::kCurrentXAndCurrentY;
        if (sButtonPresetLatched && MatterGetColorSource() == 1u)
        {
            const ButtonPresetEntry & p = kPresets[sPresetIndex];
            MatterRestoreButtonPresetPermilles(p.wPermille, p.rPermille, p.gPermille, p.bPermille);
            MatterSetButtonPresetActive(1);
            reportColorMode = SyncMatterAttributesForPreset(sPresetIndex);
        }
        else if (MatterGetIsColorTempMode() != 0u)
        {
            reportColorMode = ColorControl::ColorModeEnum::kColorTemperatureMireds;
            ColorControl::Attributes::ColorMode::Set(LIGHT_ENDPOINT, reportColorMode);
            ColorControl::Attributes::ColorTemperatureMireds::Set(LIGHT_ENDPOINT, MatterGetRuntimeColorTempMireds());
        }
        OnOff::Attributes::OnOff::Set(LIGHT_ENDPOINT, true);
        if (sButtonPresetLatched && MatterGetColorSource() == 1u)
        {
            ReportLightStateAttributes(reportColorMode, true);
        }
        else
        {
            MatterReportingAttributeChangeCallback(LIGHT_ENDPOINT, OnOff::Id, OnOff::Attributes::OnOff::Id);
            MatterReportingAttributeChangeCallback(LIGHT_ENDPOINT, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
            if (MatterGetIsColorTempMode() != 0u)
            {
                ReportLightColorAttributes(reportColorMode);
            }
        }
    }
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
}

void AppTask::HandleDoubleClick()
{
    bool onoff = true;
    app::DataModel::Nullable<uint8_t> level;
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    OnOff::Attributes::OnOff::Get(LIGHT_ENDPOINT, &onoff);
    if (LevelControl::Attributes::CurrentLevel::Get(LIGHT_ENDPOINT, level) != Protocols::InteractionModel::Status::Success ||
        level.IsNull())
    {
        level.SetNonNull(kLevelMax);
    }
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    if (!onoff)
    {
        return;
    }

    const uint8_t currentLevel = level.Value();

    sPresetIndex = (sPresetIndex + 1) % kPresetCount;
    ApplyButtonPresetAtIndex(sPresetIndex, currentLevel);
    ChipLogError(DeviceLayer,
                 "[DIM][BTN] double preset=%u level=%u%% rgbw from preset index %u",
                 static_cast<unsigned>(sPresetIndex + 1), static_cast<unsigned>(LevelToPercent(currentLevel)),
                 static_cast<unsigned>(sPresetIndex));
}

void AppTask::HandleLongPressTick()
{
    if (!sButtonPressed)
    {
        return;
    }

    if (sIdentifyActive || sEffectMode == EffectMode::Identify)
    {
        return;
    }

    if (sResetEndSequenceActive || sResetTriggered)
    {
        return;
    }

    sLongPressMs += APP_DIMMING_STEP_MS;

    if (!sResetWarnActive && sLongPressMs >= APP_LONG_PRESS_RESET_WARN_MS)
    {
        sResetWarnActive = true;
        sDimmingActive = false;
        MatterSetButtonDimmingActive(0);
        StartEffect(EffectMode::ResetWarn);
        ChipLogError(Zcl, "[DIM] long press 5s warning");
    }

    if (!sResetTriggered && sLongPressMs >= APP_LONG_PRESS_RESET_MS)
    {
        sResetTriggered = true;
        StartResetWarnEndEffect(true);
        return;
    }

    if (!sResetWarnActive && sLongPressMs >= APP_LONG_PRESS_DIM_START_MS)
    {
        if (!sDimmingActive)
        {
            bool onoff = true;
            app::DataModel::Nullable<uint8_t> level;
            chip::DeviceLayer::PlatformMgr().LockChipStack();
            OnOff::Attributes::OnOff::Get(LIGHT_ENDPOINT, &onoff);
            if (LevelControl::Attributes::CurrentLevel::Get(LIGHT_ENDPOINT, level) == Protocols::InteractionModel::Status::Success &&
                !level.IsNull())
            {
                sDimmingLevelQ16 = static_cast<int32_t>(level.Value()) << 16;
            }
            else
            {
                sDimmingLevelQ16 = static_cast<int32_t>(kLevelMax) << 16;
            }
            chip::DeviceLayer::PlatformMgr().UnlockChipStack();

            if (!onoff)
            {
                return;
            }

            const uint8_t curLevel = static_cast<uint8_t>(sDimmingLevelQ16 >> 16);
            if (curLevel <= kLevelMin)
            {
                sDimmingDirection = 1;
            }
            else
            {
                sDimmingDirection = sNextDimmingDirection;
            }
            sNextDimmingDirection = (sDimmingDirection > 0) ? -1 : 1;
            sDimmingOriginLevelQ16 = sDimmingLevelQ16;
            sDimmingStartTick = osKernelGetTickCount();
            sDimmingLastAppliedLevel = curLevel;
            sDimmingActive = true;
            MatterSetButtonDimmingActive(1);
        }

        if (sDimmingActive)
        {
            const int32_t range = static_cast<int32_t>(kLevelMax) - static_cast<int32_t>(kLevelMin);
            const uint32_t tickFreq = osKernelGetTickFreq();
            uint32_t elapsedMs = 0u;
            if (tickFreq != 0u)
            {
                elapsedMs = ((osKernelGetTickCount() - sDimmingStartTick) * 1000u) / tickFreq;
            }
            if (elapsedMs > APP_DIMMING_PERIOD_MS)
            {
                elapsedMs = APP_DIMMING_PERIOD_MS;
            }

            const int32_t deltaQ16 = static_cast<int32_t>((static_cast<int64_t>(range) << 16) * static_cast<int64_t>(elapsedMs) /
                static_cast<int64_t>(APP_DIMMING_PERIOD_MS));
            int32_t targetQ16 = sDimmingOriginLevelQ16 + ((sDimmingDirection > 0) ? deltaQ16 : -deltaQ16);

            const int32_t levelQ16Min = static_cast<int32_t>(kLevelMin) << 16;
            const int32_t levelQ16Max = static_cast<int32_t>(kLevelMax) << 16;
            if (targetQ16 < levelQ16Min)
            {
                targetQ16 = levelQ16Min;
            }
            if (targetQ16 > levelQ16Max)
            {
                targetQ16 = levelQ16Max;
            }

            sDimmingLevelQ16 = targetQ16;
            MatterApplyButtonDimmingQ16(targetQ16, kLevelMin, kLevelMax);

            const uint8_t newLevel = static_cast<uint8_t>(targetQ16 >> 16);
            if (newLevel != sDimmingLastAppliedLevel)
            {
                sDimmingLastAppliedLevel = newLevel;
                chip::DeviceLayer::PlatformMgr().LockChipStack();
                LevelControl::Attributes::CurrentLevel::Set(LIGHT_ENDPOINT, newLevel);
                MatterReportingAttributeChangeCallback(LIGHT_ENDPOINT, LevelControl::Id,
                                                       LevelControl::Attributes::CurrentLevel::Id);
                chip::DeviceLayer::PlatformMgr().UnlockChipStack();
            }
        }
    }
}

void AppTask::ButtonTimerEventHandler(AppEvent * aEvent)
{
    const uintptr_t ctx = reinterpret_cast<uintptr_t>(aEvent->TimerEvent.Context);
    if (ctx == kTimerCtxClick)
    {
        if (sImmediateSinglePending)
        {
            sImmediateSinglePending = false;
            sClickCount = 0;
            return;
        }

        uint8_t clicks = sClickCount;
        sClickCount = 0;
        if (clicks == 1)
        {
            HandleSingleClick();
        }
        else if (clicks >= 2)
        {
            HandleDoubleClick();
        }
        return;
    }

    if (ctx == kTimerCtxLong)
    {
        HandleLongPressTick();
        return;
    }

    if (ctx == kTimerCtxEffect)
    {
        RunEffectStep();
        return;
    }

    if (ctx == kTimerCtxCommissioning)
    {
        StopCommissioningWindow();
        ChipLogError(Zcl, "[DIM] commissioning window timeout, window closed");
        return;
    }

    if (ctx == kTimerCtxBootBreathEnd)
    {
        // APP_BOOT_BREATH_MS(60s) 到期且仍未配网：强制停止呼吸并灭灯
        sBootBreathingActive = false;
        StopEffect();
        sStartupSingleWhiteLock = false;
        // 呼吸期间绕过 Matter 属性直接驱动 PWM，结束前需先物理灭灯再同步 OnOff
        MatterApplyRgbwNow(0u, 0u, 0u, 0u);
        MatterSetOffTransitionActive(1);
        chip::DeviceLayer::PlatformMgr().LockChipStack();
        OnOff::Attributes::OnOff::Set(LIGHT_ENDPOINT, false);
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        MatterSetOffTransitionActive(0);
        ChipLogError(Zcl, "[DIM] boot breathing finished");
        return;
    }

    if (ctx == kTimerCtxIdentifyEnd)
    {
        StopIdentify();
        return;
    }

    if (ctx == kTimerCtxResetOff)
    {
        StartBootBreathing();
        return;
    }
}

void AppTask::OnPlatformEvent(const chip::DeviceLayer::ChipDeviceEvent * event, intptr_t arg)
{
    (void) arg;
    if (event == nullptr)
    {
        return;
    }

    if (event->Type == chip::DeviceLayer::DeviceEventType::kServiceProvisioningChange)
    {
        ChipLogError(Zcl, "[BLINK2] EVENT provisioning-change provisioned=%u",
                     static_cast<unsigned>(event->ServiceProvisioningChange.IsServiceProvisioned ? 1u : 0u));
    }

    if (event->Type == chip::DeviceLayer::DeviceEventType::kCommissioningComplete)
    {
        sCommissioningCompleteCount++;
        ChipLogError(Zcl,
                     "[BLINK2] EVENT commissioning-complete count=%u provisioned=%u breath=%u effect=%u pending=%u",
                     static_cast<unsigned>(sCommissioningCompleteCount),
                     static_cast<unsigned>(BaseApplication::sIsProvisioned ? 1u : 0u),
                     static_cast<unsigned>(sBootBreathingActive ? 1u : 0u),
                     static_cast<unsigned>(sEffectMode),
                     static_cast<unsigned>(sPairSuccessPending ? 1u : 0u));
        StopCommissioningWindow();
        osTimerStop(sBootDefaultTimer);

        if (sCommissioningCompleteCount < APP_PAIR_SUCCESS_COMPLETE_COUNT)
        {
            ChipLogError(Zcl, "[BLINK2] PAIR skipped reason=await-complete-%u count=%u",
                         static_cast<unsigned>(APP_PAIR_SUCCESS_COMPLETE_COUNT),
                         static_cast<unsigned>(sCommissioningCompleteCount));
            return;
        }

        chip::DeviceLayer::PlatformMgr().ScheduleWork(
            [](intptr_t) { SyncAndReportLightStateToApp("commissioning-complete"); }, 0);

        if (sBootBreathingActive && sEffectMode == EffectMode::BootBreathing)
        {
            sPairSuccessPending = true;
            ChipLogError(Zcl, "[BLINK2] PAIR deferred reason=breath-off-pending");
        }
        else
        {
            BeginPairSuccessEffect("commissioning-complete");
        }
    }
}

void AppTask::RestorePreEffectState()
{
    RestoreState(sPreEffectState);
}

void AppTask::StartCommissioningWindow()
{
    if (sCommissioningActive)
    {
        RestartCommissioningTimer();
        return;
    }
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    CHIP_ERROR err = chip::Server::GetInstance().GetCommissioningWindowManager().OpenBasicCommissioningWindow(
        chip::System::Clock::Seconds32(APP_COMMISSIONING_WINDOW_MS / 1000u));
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "[DIM] failed to open commissioning window");
        return;
    }

    sCommissioningActive = true;
    sCommissioningCompleteCount = 0u;
    osTimerStart(sPostResetWindowTimer, pdMS_TO_TICKS(APP_COMMISSIONING_WINDOW_MS));
    ChipLogError(Zcl, "[DIM] commissioning window started");
}

void AppTask::StopCommissioningWindow()
{
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    chip::Server::GetInstance().GetCommissioningWindowManager().CloseCommissioningWindow();
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    osTimerStop(sPostResetWindowTimer);
    sCommissioningActive = false;
}

void AppTask::RestartCommissioningTimer()
{
    if (BaseApplication::sIsProvisioned)
    {
        return;
    }

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    auto & commissioningMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    if (commissioningMgr.IsCommissioningWindowOpen())
    {
        commissioningMgr.CloseCommissioningWindow();
    }
    const CHIP_ERROR err = commissioningMgr.OpenBasicCommissioningWindow(
        chip::System::Clock::Seconds32(APP_COMMISSIONING_WINDOW_MS / 1000u));
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "[DIM] failed to restart commissioning window: %" CHIP_ERROR_FORMAT, err.Format());
        osTimerStop(sPostResetWindowTimer);
        sCommissioningActive = false;
        return;
    }

    sCommissioningActive = true;
    sCommissioningCompleteCount = 0u;
    osTimerStart(sPostResetWindowTimer, pdMS_TO_TICKS(APP_COMMISSIONING_WINDOW_MS));
    ChipLogError(Zcl, "[DIM] commissioning window restarted (%u min)", APP_COMMISSIONING_WINDOW_MS / 60000u);
}

void AppTask::StartBootBreathing()
{
    if (sBootBreathingActive)
    {
        return;
    }

    if (ShouldSkipEffect(EffectMode::BootBreathing))
    {
        sBootBreathingActive = false;
        sStartupSingleWhiteLock = false;
        return;
    }

    if (!sCommissioningActive && !BaseApplication::sIsProvisioned)
    {
        StartCommissioningWindow();
    }

    CaptureState(sPreEffectState);
    sBootBreathingActive = true;
    // 呼吸只亮白光：先清 RGB，避免掉电记忆或残留颜色干扰
    MatterApplyRgbwNow(0u, 0u, 0u, 0u);
    // 启动效果定时器（20ms 步进）并注册 60s 总超时
    StartEffect(EffectMode::BootBreathing);
    osTimerStart(sBootDefaultTimer, pdMS_TO_TICKS(APP_BOOT_BREATH_MS));
    ChipLogError(Zcl, "[DIM] boot breathing started");
}

void AppTask::StartIdentify(uint16_t identifyTimeSec, const char * source)
{
    const char * src = (source != nullptr) ? source : "unknown";
    ChipLogError(AppServer, "[BLINK2] IDENTIFY request src=%s sec=%u provisioned=%u active=%u effect=%u",
                 src,
                 static_cast<unsigned>(identifyTimeSec),
                 static_cast<unsigned>(BaseApplication::sIsProvisioned ? 1u : 0u),
                 static_cast<unsigned>(sIdentifyActive ? 1u : 0u),
                 static_cast<unsigned>(sEffectMode));

    if (ShouldSkipEffect(EffectMode::Identify))
    {
        ChipLogError(AppServer, "[BLINK2] IDENTIFY skipped src=%s reason=ShouldSkipEffect", src);
        return;
    }

    // Matter 配网过程中控制器会发 Identify/TriggerEffect 做发现提示。
    // 未配网完成前不闪灯，避免与配网成功快闪重复；配网完成后仍保留 App Identify。
    if (!BaseApplication::sIsProvisioned)
    {
        if (identifyTimeSec != 0u)
        {
            ChipLogError(AppServer, "[BLINK2] IDENTIFY suppressed src=%s reason=not-provisioned", src);
        }
        return;
    }

    if (identifyTimeSec == 0)
    {
        StopIdentify();
        return;
    }

    if (!sIdentifyActive)
    {
        const bool resetInProgress = sResetWarnActive || sResetEndSequenceActive || sResetTriggered || sResetPendingAfterWarnEnd
            || sEffectMode == EffectMode::ResetWarn || sEffectMode == EffectMode::ResetWarnEnd;

        if (resetInProgress)
        {
            // 长按重置进行中：Identify 结束后恢复到长按前的状态，而非恢复红灯闪烁
            sIdentifyEffectState = sPreEffectState;
            sResetPendingAfterWarnEnd = false;
            sResetEndSequenceActive = false;
            sResetTriggered = false;
            sResetWarnActive = false;
            sDimmingActive = false;
            osTimerStop(sLongPressTimer);
            sLongPressMs = 0;
        }

        if (sBootBreathingActive)
        {
            sBootBreathingActive = false;
            osTimerStop(sBootDefaultTimer);
            sStartupSingleWhiteLock = false;
        }

        if (sEffectMode == EffectMode::PairSuccess)
        {
            sPairSuccessPending = false;
        }

        if (!resetInProgress)
        {
            CaptureState(sIdentifyEffectState);
        }

        if (sEffectMode != EffectMode::None && sEffectMode != EffectMode::Identify)
        {
            StopEffect();
        }
    }

    sIdentifyActive = true;
    sIdentifySessionSeq++;
    sIdentifyFlashCount = 0u;
    sIdentifyLastOn = false;
    ChipLogError(AppServer, "[BLINK2] IDENTIFY start seq=%u src=%s sec=%u",
                 static_cast<unsigned>(sIdentifySessionSeq), src, static_cast<unsigned>(identifyTimeSec));
    StartEffect(EffectMode::Identify);
    osTimerStart(sIdentifyTimer, pdMS_TO_TICKS(static_cast<uint32_t>(identifyTimeSec) * 1000u));
}

void AppTask::StopIdentify()
{
    ChipLogError(AppServer, "[IDENTIFY] StopIdentify called");

    if (!sIdentifyActive)
    {
        return;
    }

    sIdentifyActive = false;
    osTimerStop(sIdentifyTimer);
    StopEffect();
    ChipLogError(AppServer, "[BLINK2] IDENTIFY done seq=%u flashes=%u",
                 static_cast<unsigned>(sIdentifySessionSeq),
                 static_cast<unsigned>(sIdentifyFlashCount));
    RestoreState(sIdentifyEffectState);
}

void AppTask::ButtonEventHandler(uint8_t button, uint8_t btnAction)
{
    AppEvent button_event           = {};
    button_event.Type               = AppEvent::kEventType_Button;
    button_event.ButtonEvent.Action = btnAction;

    if (button == APP_LIGHT_SWITCH)
    {
        button_event.Handler = LightSwitchButtonEventHandler;
        AppTask::GetAppTask().PostEvent(&button_event);
        return;
    }
    else if (button == APP_FUNCTION_BUTTON)
    {
        (void) btnAction;
        return;
    }
}

void AppTask::LightSwitchButtonEventHandler(AppEvent * aEvent)
{
    if (aEvent == nullptr)
    {
        return;
    }

    const uint8_t action = aEvent->ButtonEvent.Action;

    if (action == static_cast<uint8_t>(SilabsPlatform::ButtonAction::ButtonPressed))
    {
        OnButtonPressed();
    }
    else if (action == static_cast<uint8_t>(SilabsPlatform::ButtonAction::ButtonReleased))
    {
        OnButtonReleased();
    }
}

void AppTask::ActionInitiated(LightingManager::Action_t aAction, int32_t aActor, uint8_t * aValue)
{
    if (aAction == LightingManager::LEVEL_ACTION)
    {
        VerifyOrReturn(aValue != nullptr);
        sLightLED.SetLevel(*aValue);
    }
    else
    {
        // Action initiated, update the light led
        bool lightOn = aAction == LightingManager::ON_ACTION;
        SILABS_LOG("Turning light %s", (lightOn) ? "On" : "Off")

    #if !(defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
        sLightLED.Set(lightOn);
    #endif

#ifdef DISPLAY_ENABLED
        sAppTask.GetLCD().WriteDemoUI(lightOn);
#endif

        if (aActor == AppEvent::kEventType_Button)
        {
            sAppTask.mSyncClusterToButtonAction = true;
        }
    }
}

void AppTask::ActionCompleted(LightingManager::Action_t aAction)
{
    // action has been completed bon the light
    if (aAction == LightingManager::ON_ACTION)
    {
        SILABS_LOG("Light ON")
    }
    else if (aAction == LightingManager::OFF_ACTION)
    {
        SILABS_LOG("Light OFF")
    }

    if (sAppTask.mSyncClusterToButtonAction)
    {
        chip::DeviceLayer::PlatformMgr().ScheduleWork(UpdateClusterState, reinterpret_cast<intptr_t>(nullptr));
        sAppTask.mSyncClusterToButtonAction = false;
    }
}

void AppTask::PostLightActionRequest(int32_t aActor, LightingManager::Action_t aAction)
{
    AppEvent event;
    event.Type              = AppEvent::kEventType_Light;
    event.LightEvent.Actor  = aActor;
    event.LightEvent.Action = aAction;
    event.Handler           = LightActionEventHandler;
    PostEvent(&event);
}

#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
void AppTask::PostLightControlActionRequest(int32_t aActor, LightingManager::Action_t aAction,
                                           const RGBLEDWidget::ColorData_t & aValue)
{
    AppEvent light_event;
    light_event.Type                     = AppEvent::kEventType_Light;
    light_event.LightControlEvent.Actor  = aActor;
    light_event.LightControlEvent.Action = aAction;
    light_event.LightControlEvent.Value  = aValue;
    light_event.Handler                  = LightControlEventHandler;
    PostEvent(&light_event);
}
#endif // (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED)

void AppTask::UpdateClusterState(intptr_t context)
{
    uint8_t newValue = LightMgr().IsLightOn();

    // write the new on/off value
    Protocols::InteractionModel::Status status = OnOffServer::Instance().setOnOffValue(LIGHT_ENDPOINT, newValue, false);

    if (status != Protocols::InteractionModel::Status::Success)
    {
        SILABS_LOG("ERR: updating on/off %x", to_underlying(status));
    }
}

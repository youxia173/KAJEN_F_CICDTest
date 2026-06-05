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
extern "C" bool MatterRestorePowerOnMemoryIfAny();
extern "C" void MatterSavePowerOnMemorySnapshot();
extern "C" void MatterSetBootOutputSuppress(uint8_t suppress);
extern "C" void MatterReapplyPowerOnMemoryOutput();
extern "C" void MatterComputeCtRgbw(uint16_t mireds, uint8_t level254, uint8_t * rOut, uint8_t * gOut, uint8_t * bOut,
                                     uint8_t * wDutyOut);
extern "C" void MatterSetOffTransitionActive(uint8_t active);

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

constexpr uint8_t kLevelMax = 254;
constexpr uint8_t kLevelMin = 26; // 10%

struct LightPreset
{
    bool isCT;
    uint16_t param1;
    uint8_t level;
};

constexpr LightPreset kPresets[13] = {
    { true, 370, 254 },
    { true, 455, 254 },
    { true, 250, 254 },
    { true, 154, 254 },
    { false, 28, 127 },
    { false, 21, 127 },
    { false, 11, 127 },
    { false, 247, 229 },
    { false, 212, 229 },
    { false, 155, 216 },
    { false, 113, 229 },
    { false, 56, 216 },
    { false, 32, 216 },
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
size_t sPresetIndex = 0;
AppTask::EffectMode sEffectMode = AppTask::EffectMode::None;
uint32_t sEffectTickMs = 0;
bool sButtonPresetLatched = false;
bool sStartupSingleWhiteLock = true;
bool sCommissioningActive = false;
bool sBootBreathingActive = false;
bool sPairSuccessPending = false;
bool sIdentifyActive = false;
AppTask::EffectMode sIdentifyResumeMode = AppTask::EffectMode::None;

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
static uint8_t sBootBreathDutyRemainder = 0;

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

static void ApplyWhitePwmEffectPermille(uint16_t levelPermille)
{
    if (levelPermille > 1000u)
    {
        levelPermille = 1000u;
    }

    uint8_t levelPct = static_cast<uint8_t>(levelPermille / 10u);
    sBootBreathDutyRemainder = static_cast<uint8_t>(sBootBreathDutyRemainder + (levelPermille % 10u));
    if (sBootBreathDutyRemainder >= 10u)
    {
        ++levelPct;
        sBootBreathDutyRemainder = static_cast<uint8_t>(sBootBreathDutyRemainder - 10u);
    }

    if (levelPct > 100u)
    {
        levelPct = 100u;
    }

    MatterApplyRgbwNow(0u, 0u, 0u, levelPct);
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

static void HsvToRgb(uint8_t hue, uint8_t sat, uint8_t & r, uint8_t & g, uint8_t & b)
{
    const float H = (static_cast<float>(hue) * 360.0f) / 254.0f;
    const float S = static_cast<float>(sat) / 254.0f;
    const float V = 1.0f;

    const float Hd = H / 60.0f;
    int i = static_cast<int>(Hd) % 6;
    float f = Hd - static_cast<int>(Hd);
    float p = V * (1.0f - S);
    float q = V * (1.0f - S * f);
    float t = V * (1.0f - S * (1.0f - f));

    float rF = 0.0f, gF = 0.0f, bF = 0.0f;
    switch (i)
    {
        case 0: rF = V; gF = t; bF = p; break;
        case 1: rF = q; gF = V; bF = p; break;
        case 2: rF = p; gF = V; bF = t; break;
        case 3: rF = p; gF = q; bF = V; break;
        case 4: rF = t; gF = p; bF = V; break;
        case 5: default: rF = V; gF = p; bF = q; break;
    }

    r = static_cast<uint8_t>(rF * 255.0f);
    g = static_cast<uint8_t>(gF * 255.0f);
    b = static_cast<uint8_t>(bF * 255.0f);
}

static void ComputePresetRgbw(const LightPreset & p, uint8_t level254, uint16_t & w, uint16_t & r, uint16_t & g, uint16_t & b)
{
    if (p.isCT)
    {
        uint8_t r8 = 0;
        uint8_t g8 = 0;
        uint8_t b8 = 0;
        uint8_t w8 = 0;
        MatterComputeCtRgbw(p.param1, level254, &r8, &g8, &b8, &w8);
        r = static_cast<uint16_t>(r8) * 1000u / 255u;
        g = static_cast<uint16_t>(g8) * 1000u / 255u;
        b = static_cast<uint16_t>(b8) * 1000u / 255u;
        w = static_cast<uint16_t>(w8) * 10u;
        return;
    }

    uint8_t r8 = 0;
    uint8_t g8 = 0;
    uint8_t b8 = 0;
    HsvToRgb(static_cast<uint8_t>(p.param1), 254, r8, g8, b8);
    const uint8_t level255 = static_cast<uint8_t>((static_cast<uint16_t>(p.level) * 255u) / 254u);

    r = static_cast<uint16_t>((static_cast<uint32_t>(r8) * level255 + 127u) / 255u);
    g = static_cast<uint16_t>((static_cast<uint32_t>(g8) * level255 + 127u) / 255u);
    b = static_cast<uint16_t>((static_cast<uint32_t>(b8) * level255 + 127u) / 255u);
    w = 0;
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

    const LightPreset & p = kPresets[sPresetIndex];
    uint8_t level254 = MatterGetMemLevel();
    if (level254 == 0u)
    {
        level254 = kLevelMax;
    }
    uint16_t wOut = 0;
    uint16_t rOut = 0;
    uint16_t gOut = 0;
    uint16_t bOut = 0;
    ComputePresetRgbw(p, level254, wOut, rOut, gOut, bOut);
    MatterRestoreButtonPresetPermilles(wOut, rOut, gOut, bOut);
    ChipLogError(Zcl, "[DIM] restored preset memory: preset=%u level=%u permille=%u,%u,%u,%u",
                 static_cast<unsigned>(sPresetIndex + 1), static_cast<unsigned>(level254),
                 static_cast<unsigned>(wOut), static_cast<unsigned>(rOut),
                 static_cast<unsigned>(gOut), static_cast<unsigned>(bOut));
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
            ApplyRgbwEffect(0, 0, 0, 0);
            osTimerStart(sResetOffTimer, pdMS_TO_TICKS(APP_RESET_BOOT_OFF_MS));
        }
        else
        {
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
        const LightPreset & p = kPresets[sPresetIndex];
        uint16_t wOut = 0;
        uint16_t rOut = 0;
        uint16_t gOut = 0;
        uint16_t bOut = 0;
        ComputePresetRgbw(p, state.level, wOut, rOut, gOut, bOut);
        MatterSetButtonPresetPwmWithFade(wOut, rOut, gOut, bOut, state.level);
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
    StopEffect();
    RestoreState(sPreEffectState);
    sLongPressMs = 0;
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

void AppTask::BeginPairSuccessEffect()
{
    sPairSuccessPending = false;
    sBootBreathingActive = false;
    osTimerStop(sBootDefaultTimer);
    MatterApplyRgbwNow(0u, 0u, 0u, 0u);
    StartEffect(EffectMode::PairSuccess);
    ChipLogError(Zcl, "[DIM] pairing success white blink start");
}

void AppTask::FinishPairSuccessEffect()
{
    StopEffect();
    sStartupSingleWhiteLock = false;
    MatterApplyRgbwNow(0u, 0u, 0u, 0u);
    MatterSetOffTransitionActive(1);
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    OnOff::Attributes::OnOff::Set(LIGHT_ENDPOINT, false);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    MatterSetOffTransitionActive(0);
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

    if (sEffectMode == EffectMode::BootBreathing)
    {
        const uint32_t cycleMs = APP_BOOT_BREATH_CYCLE_MS;
        const uint32_t rampMs = APP_BOOT_BREATH_RAMP_MS;
        const uint32_t holdMs = APP_BOOT_BREATH_HOLD_MS;
        const uint32_t t = sEffectTickMs % cycleMs;
        uint16_t whitePermille = 0u;
        const bool logCheckpoint = ((t % 400u) == 0u) || (t == rampMs) || (t == (rampMs + holdMs))
            || (t == (rampMs + holdMs + rampMs));

        if (t < rampMs) {
            // ramp up 0 -> 1000
            whitePermille = static_cast<uint16_t>((t * 1000u) / rampMs);
        } else if (t < (rampMs + holdMs)) {
            // hold at 100%
            whitePermille = 1000u;
        } else if (t < (rampMs + holdMs + rampMs)) {
            // ramp down 1000 -> 0
            uint32_t td = t - (rampMs + holdMs);
            whitePermille = static_cast<uint16_t>(((rampMs - td) * 1000u) / rampMs);
        } else {
            // hold at 0%
            whitePermille = 0u;
        }

        if (logCheckpoint)
        {
            ChipLogError(Zcl, "[BOOT] tick=%u t=%u whitePermille=%u remainder=%u",
                         static_cast<unsigned>(sEffectTickMs), static_cast<unsigned>(t),
                         static_cast<unsigned>(whitePermille), static_cast<unsigned>(sBootBreathDutyRemainder));
        }
        ApplyWhitePwmEffectPermille(whitePermille);

        const uint32_t offHoldStart = rampMs + holdMs + rampMs;
        if (sPairSuccessPending && t >= offHoldStart && whitePermille == 0u)
        {
            BeginPairSuccessEffect();
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
        ChipLogError(AppServer, "[IDENTIFY] effect step tick=%u on=%u", static_cast<unsigned>(sEffectTickMs), on ? 1u : 0u);
        ApplyWhiteEffectLevel(on ? 100u : 0u);
        sEffectTickMs += APP_EFFECT_TICK_MS;
        return;
    }
}

void AppTask::OnButtonPressed()
{
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
            BeginPairSuccessEffect();
            return;
        }
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
    osTimerStart(sLongPressTimer, pdMS_TO_TICKS(APP_DIMMING_STEP_MS));
}

void AppTask::OnButtonReleased()
{
    sButtonPressed = false;
    osTimerStop(sLongPressTimer);

    if (sEffectMode == EffectMode::ResetWarnEnd || sResetEndSequenceActive)
    {
        return;
    }

    if (sResetTriggered)
    {
        return;
    }

    if (sLongPressMs >= APP_LONG_PRESS_RESET_WARN_MS)
    {
        StartResetWarnEndEffect(false);
        sLongPressMs = 0;
        return;
    }

    if (sLongPressMs >= APP_LONG_PRESS_DIM_START_MS)
    {
        sDimmingActive = false;
        sLongPressMs = 0;
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
    if (sCommissioningActive)
    {
        RestartCommissioningTimer();
    }

    bool onoff = true;
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    OnOff::Attributes::OnOff::Get(LIGHT_ENDPOINT, &onoff);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    if (onoff)
    {
        MatterSetOffTransitionActive(1);
        OnOff::Attributes::OnOff::Set(LIGHT_ENDPOINT, false);
    }
    else
    {
        MatterSetOffTransitionActive(0);
        if (sButtonPresetLatched)
        {
            app::DataModel::Nullable<uint8_t> level;
            uint8_t level254 = kLevelMax;
            if (LevelControl::Attributes::CurrentLevel::Get(LIGHT_ENDPOINT, level) == Protocols::InteractionModel::Status::Success &&
                !level.IsNull())
            {
                level254 = level.Value();
            }
            const LightPreset & p = kPresets[sPresetIndex];
            uint16_t wOut = 0;
            uint16_t rOut = 0;
            uint16_t gOut = 0;
            uint16_t bOut = 0;
            ComputePresetRgbw(p, level254, wOut, rOut, gOut, bOut);
            MatterRestoreButtonPresetPermilles(wOut, rOut, gOut, bOut);
            MatterSetButtonPresetActive(1);
        }
        OnOff::Attributes::OnOff::Set(LIGHT_ENDPOINT, true);
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
    const LightPreset & p = kPresets[sPresetIndex];

    MatterSetButtonPresetSuppressColorCallbacks(4);
    MatterSetButtonPresetTransaction(1);
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    if (p.isCT)
    {
        ColorControl::Attributes::ColorTemperatureMireds::Set(LIGHT_ENDPOINT, p.param1);
    }
    else
    {
        ColorControl::Attributes::CurrentHue::Set(LIGHT_ENDPOINT, static_cast<uint8_t>(p.param1));
        ColorControl::Attributes::CurrentSaturation::Set(LIGHT_ENDPOINT, 254);
    }
    OnOff::Attributes::OnOff::Set(LIGHT_ENDPOINT, true);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    MatterSetButtonPresetTransaction(0);

    sButtonPresetLatched = true;
    uint16_t wOut = 0;
    uint16_t rOut = 0;
    uint16_t gOut = 0;
    uint16_t bOut = 0;
    ComputePresetRgbw(p, currentLevel, wOut, rOut, gOut, bOut);
    MatterSetButtonPresetPwmWithFade(wOut, rOut, gOut, bOut, currentLevel);
    ChipLogError(DeviceLayer,
                 "[DIM][BTN] double preset=%u level=%u%% rgbw=%u,%u,%u,%u",
                 static_cast<unsigned>(sPresetIndex + 1), static_cast<unsigned>(LevelToPercent(currentLevel)),
                 static_cast<unsigned>(wOut), static_cast<unsigned>(rOut),
                 static_cast<unsigned>(gOut), static_cast<unsigned>(bOut));

    SaveButtonPresetMemoryState();
    MatterSavePowerOnMemorySnapshot();
}

void AppTask::HandleLongPressTick()
{
    if (!sButtonPressed)
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
            sDimmingActive = true;
        }

        if (sDimmingActive)
        {
            const int32_t range = static_cast<int32_t>(kLevelMax) - static_cast<int32_t>(kLevelMin);
            const int32_t stepQ16 = (range << 16) * static_cast<int32_t>(APP_DIMMING_STEP_MS) /
                static_cast<int32_t>(APP_DIMMING_PERIOD_MS);
            sDimmingLevelQ16 += (sDimmingDirection > 0) ? stepQ16 : -stepQ16;

            int32_t levelQ16Min = static_cast<int32_t>(kLevelMin) << 16;
            int32_t levelQ16Max = static_cast<int32_t>(kLevelMax) << 16;
            if (sDimmingLevelQ16 < levelQ16Min)
            {
                sDimmingLevelQ16 = levelQ16Min;
            }
            if (sDimmingLevelQ16 > levelQ16Max)
            {
                sDimmingLevelQ16 = levelQ16Max;
            }

            const uint8_t newLevel = static_cast<uint8_t>(sDimmingLevelQ16 >> 16);
            chip::DeviceLayer::PlatformMgr().LockChipStack();
            LevelControl::Attributes::CurrentLevel::Set(LIGHT_ENDPOINT, newLevel);
            chip::DeviceLayer::PlatformMgr().UnlockChipStack();
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
        sBootBreathingActive = false;
        StopEffect();
        sStartupSingleWhiteLock = false;
        // Force physical off before syncing Matter OnOff (breath used white PWM directly).
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

    if (event->Type == chip::DeviceLayer::DeviceEventType::kCommissioningComplete)
    {
        StopCommissioningWindow();
        osTimerStop(sBootDefaultTimer);

        if (sBootBreathingActive && sEffectMode == EffectMode::BootBreathing)
        {
            sPairSuccessPending = true;
            ChipLogError(Zcl, "[DIM] pairing success pending, wait for breath off");
        }
        else
        {
            BeginPairSuccessEffect();
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
    if (!sCommissioningActive)
    {
        return;
    }

    osTimerStart(sPostResetWindowTimer, pdMS_TO_TICKS(APP_COMMISSIONING_WINDOW_MS));
}

void AppTask::StartBootBreathing()
{
    if (sBootBreathingActive)
    {
        ChipLogError(Zcl, "[BOOT] start ignored, already active");
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
    sBootBreathDutyRemainder = 0;
    ChipLogError(Zcl, "[BOOT] start provisioning=%u commissioned=%u", BaseApplication::sIsProvisioned ? 1u : 0u,
                 sCommissioningActive ? 1u : 0u);
    // Clear RGB immediately (power-on memory may have restored color before breath starts).
    MatterApplyRgbwNow(0u, 0u, 0u, 0u);
    StartEffect(EffectMode::BootBreathing);
    sBootBreathDutyRemainder = 0;
    osTimerStart(sBootDefaultTimer, pdMS_TO_TICKS(APP_BOOT_BREATH_MS));
    ChipLogError(Zcl, "[DIM] boot breathing started");
}

void AppTask::StartIdentify(uint16_t identifyTimeSec)
{
    ChipLogError(AppServer, "[IDENTIFY] StartIdentify called (%u s)", identifyTimeSec);

    if (ShouldSkipEffect(EffectMode::Identify))
    {
        return;
    }

    if (identifyTimeSec == 0)
    {
        StopIdentify();
        return;
    }

    if (!sIdentifyActive)
    {
        CaptureState(sIdentifyEffectState);
        sIdentifyResumeMode = sEffectMode;
        StopEffect();
    }

    sIdentifyActive = true;
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

    if (sIdentifyResumeMode != EffectMode::None)
    {
        StartEffect(sIdentifyResumeMode);
    }
    else
    {
        RestoreState(sIdentifyEffectState);
    }

    sIdentifyResumeMode = EffectMode::None;
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

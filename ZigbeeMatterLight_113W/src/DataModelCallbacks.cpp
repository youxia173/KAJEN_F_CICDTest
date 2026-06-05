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

#include <cstdint>
#include <stdio.h>
#include "AppConfig.h"
#include "AppTask.h"
#include "cmsis_os2.h"
#include "sm15135e.h"
#include "sl_pwm.h"
#include "sl_pwm_instances.h"
// 静态变量缓存当前目标RGB值
static uint8_t g_rgbTargetR = 0;
static uint8_t g_rgbTargetG = 0;
static uint8_t g_rgbTargetB = 0;
static bool g_isColorTempMode = false;
static bool g_buttonPresetActive = false;
static bool g_buttonPresetTxn = false;
static uint8_t g_buttonPresetSuppressColorCb = 0;
static uint16_t g_buttonPresetWPermille = 1000;
static uint16_t g_buttonPresetRPermille = 0;
static uint16_t g_buttonPresetGPermille = 0;
static uint16_t g_buttonPresetBPermille = 0;
static uint8_t g_memOnOff = 0;
static uint8_t g_memLevel = 0;
static uint8_t g_colorSource = 0; // 0: app color, 1: button preset
static uint8_t g_lastNonZeroTargetR = 255;
static uint8_t g_lastNonZeroTargetG = 255;
static uint8_t g_lastNonZeroTargetB = 255;
static uint16_t g_xyPendingX = 0;
static uint16_t g_xyPendingY = 0;
static bool g_xyPendingXDirty = false;
static bool g_xyPendingYDirty = false;
static bool g_offTransitionActive = false;
// Suppress attribute-driven hardware output during boot until power-on memory is re-applied.
static bool g_suppressAttributeHwOutput = true;
static bool g_hasRestoredPowerOnState = false;

static void RememberNonZeroRgbTarget(uint8_t r, uint8_t g, uint8_t b)
{
    if ((r | g | b) == 0u)
    {
        return;
    }

    g_lastNonZeroTargetR = r;
    g_lastNonZeroTargetG = g;
    g_lastNonZeroTargetB = b;
}

struct RgbwFadeState
{
    bool active;
    uint16_t step;
    uint16_t totalSteps;
    uint16_t startR;
    uint16_t startG;
    uint16_t startB;
    uint8_t startW;
    uint16_t targetR;
    uint16_t targetG;
    uint16_t targetB;
    uint8_t targetW;
};

static RgbwFadeState g_rgbwFade = { false, 0, 50, 0, 0, 0, 0, 0, 0, 0, 0 };
static osTimerId_t g_rgbwFadeTimer = nullptr;
static constexpr uint32_t kRgbwFadeStepMs = 20;
static constexpr uint16_t kFadeStepsMin = 6;   // 120ms
static constexpr uint16_t kFadeStepsMax = 50;  // 1000ms
static constexpr uint32_t kFadeRetargetMinMs = 80;
static constexpr uint8_t kFadeRetargetMinDeltaRgb = 2;
static constexpr uint8_t kFadeRetargetMinDeltaW = 1;
static uint32_t g_lastFadeRetargetTick = 0;
static uint16_t g_lastAppliedR = 0;
static uint16_t g_lastAppliedG = 0;
static uint16_t g_lastAppliedB = 0;
static uint8_t g_lastAppliedW = 0;
static bool g_hasLastApplied = false;

static sm15135e_pixel_t g_sm15135e_pixel = { 0 };
static bool g_sm15135e_inited = false;

static uint16_t Scale8To16(uint8_t v)
{
    return static_cast<uint16_t>(v) * 257u;
}

// White light keeps using the dedicated white PWM channel (sl_pwm_rgb_white).
// pct is a duty-cycle percentage in [0, 100].
static void ApplyWhitePwm(uint8_t pct)
{
    if (pct > 100u)
    {
        pct = 100u;
    }
    sl_pwm_set_duty_cycle(&sl_pwm_rgb_white, pct);
    sl_pwm_start(&sl_pwm_rgb_white);
}

static void Sm15135eEnsureInit(void)
{
    if (g_sm15135e_inited)
    {
        return;
    }

    sm15135e_fill_default(&g_sm15135e_pixel);
    sm15135e_init();
    g_sm15135e_inited = true;
}

static void GetCurrentRgbw(uint16_t * r, uint16_t * g, uint16_t * b, uint8_t * wDuty)
{
    if (r != nullptr)
    {
        *r = g_hasLastApplied ? g_lastAppliedR : 0u;
    }
    if (g != nullptr)
    {
        *g = g_hasLastApplied ? g_lastAppliedG : 0u;
    }
    if (b != nullptr)
    {
        *b = g_hasLastApplied ? g_lastAppliedB : 0u;
    }
    if (wDuty != nullptr)
    {
        *wDuty = g_hasLastApplied ? g_lastAppliedW : 0u;
    }
}

static inline void MapLogicalToSm15135eRgb(uint8_t r, uint8_t g, uint8_t b,
                                           uint16_t * outR, uint16_t * outG, uint16_t * outB)
{
    // HW wiring: B->Green LED, G->Red LED, R->Blue LED.
    if (outR != nullptr)
    {
        *outR = Scale8To16(b);
    }
    if (outG != nullptr)
    {
        *outG = Scale8To16(r);
    }
    if (outB != nullptr)
    {
        *outB = Scale8To16(g);
    }
}

static inline void MapLogicalToSm15135eRgb16(uint16_t r, uint16_t g, uint16_t b,
                                             uint16_t * outR, uint16_t * outG, uint16_t * outB)
{
    // Same channel remap as MapLogicalToSm15135eRgb, at full 16-bit resolution.
    if (outR != nullptr)
    {
        *outR = b;
    }
    if (outG != nullptr)
    {
        *outG = r;
    }
    if (outB != nullptr)
    {
        *outB = g;
    }
}

static void CancelRgbwFade()
{
    if (!g_rgbwFade.active)
    {
        return;
    }
    g_rgbwFade.active = false;
    if (g_rgbwFadeTimer != nullptr)
    {
        osTimerStop(g_rgbwFadeTimer);
    }
}

// Level dimming (long-press): scale at 16-bit to avoid 8-bit stair-steps and shimmer.
static void ApplyRgbLevelNow(uint8_t level254)
{
    CancelRgbwFade();

    const uint16_t r16 = static_cast<uint16_t>((static_cast<uint32_t>(Scale8To16(g_rgbTargetR)) * level254 + 127u) / 254u);
    const uint16_t g16 = static_cast<uint16_t>((static_cast<uint32_t>(Scale8To16(g_rgbTargetG)) * level254 + 127u) / 254u);
    const uint16_t b16 = static_cast<uint16_t>((static_cast<uint32_t>(Scale8To16(g_rgbTargetB)) * level254 + 127u) / 254u);

    if (g_hasLastApplied && g_lastAppliedR == r16 && g_lastAppliedG == g16 && g_lastAppliedB == b16 && g_lastAppliedW == 0u)
    {
        return;
    }

    Sm15135eEnsureInit();

    uint16_t outR = 0, outG = 0, outB = 0;
    MapLogicalToSm15135eRgb16(r16, g16, b16, &outR, &outG, &outB);
    sm15135e_set_rgbwy(&g_sm15135e_pixel, outR, outG, outB, 0u, 0u);
    sm15135e_send_frame(&g_sm15135e_pixel);
    sm15135e_send_reset();

    g_lastAppliedR = r16;
    g_lastAppliedG = g16;
    g_lastAppliedB = b16;
    g_lastAppliedW = 0u;
    g_hasLastApplied = true;
}

// XY color calibration knobs for real-lamp tuning.
// Keep matrix identity and gains at 1.0f for pure standard conversion.
static constexpr float kXyCalM00 = 1.02f;
static constexpr float kXyCalM01 = 0.0f;
static constexpr float kXyCalM02 = 0.0f;
static constexpr float kXyCalM10 = 0.0f;
static constexpr float kXyCalM11 = 1.0f;
static constexpr float kXyCalM12 = 0.0f;
static constexpr float kXyCalM20 = -0.04f;
static constexpr float kXyCalM21 = -0.06f;
static constexpr float kXyCalM22 = 1.0f;
static constexpr float kXyGainR = 1.0f;
static constexpr float kXyGainG = 1.0f;
static constexpr float kXyGainB = 1.0f;
#include <stdint.h>
#include <math.h>

static void ApplyRgbwNow(uint16_t r, uint16_t g, uint16_t b, uint8_t wDuty)
{
    const uint8_t r8 = static_cast<uint8_t>(r > 255u ? 255u : r);
    const uint8_t g8 = static_cast<uint8_t>(g > 255u ? 255u : g);
    const uint8_t b8 = static_cast<uint8_t>(b > 255u ? 255u : b);
    const uint8_t w8 = static_cast<uint8_t>(wDuty > 100u ? 100u : wDuty);

    const bool rgbChanged =
        !g_hasLastApplied || g_lastAppliedR != r8 || g_lastAppliedG != g8 || g_lastAppliedB != b8;
    const bool wChanged = !g_hasLastApplied || g_lastAppliedW != w8;

    if (!rgbChanged && !wChanged)
    {
        return;
    }

    // RGB is driven by the SM15135E (SPI), replacing the original 3-channel RGB PWM.
    if (rgbChanged)
    {
        Sm15135eEnsureInit();

        uint16_t outR = 0, outG = 0, outB = 0;
        MapLogicalToSm15135eRgb(r8, g8, b8, &outR, &outG, &outB);
        // Keep the SM15135E white channel off; white stays on the dedicated white PWM.
        sm15135e_set_rgbwy(&g_sm15135e_pixel,
                           outR,
                           outG,
                           outB,
                           0u,
                           0u);
        sm15135e_send_frame(&g_sm15135e_pixel);
        sm15135e_send_reset();
    }

    // White light continues to use the original white PWM output.
    // Always drive hardware when turning off: effects may update PWM without refreshing g_lastAppliedW.
    if (wChanged || (w8 == 0u))
    {
        ApplyWhitePwm(w8);
    }

    g_lastAppliedR = r8;
    g_lastAppliedG = g8;
    g_lastAppliedB = b8;
    g_lastAppliedW = w8;
    g_hasLastApplied = true;
}

extern "C" void MatterApplyRgbwNow(uint8_t r, uint8_t g, uint8_t b, uint8_t wDuty)
{
    ApplyRgbwNow(r, g, b, wDuty);
}

static uint16_t ComputeFadeSteps(uint32_t durationMs)
{
    uint16_t steps = static_cast<uint16_t>((durationMs + (kRgbwFadeStepMs / 2u)) / kRgbwFadeStepMs);
    if (steps < kFadeStepsMin)
    {
        steps = kFadeStepsMin;
    }
    if (steps > kFadeStepsMax)
    {
        steps = kFadeStepsMax;
    }
    return steps;
}

static void RgbwFadeTimerCallback(void * context)
{
    (void) context;
    if (!g_rgbwFade.active)
    {
        osTimerStop(g_rgbwFadeTimer);
        return;
    }

    if (g_rgbwFade.step >= g_rgbwFade.totalSteps)
    {
        g_rgbwFade.active = false;
        ApplyRgbwNow(g_rgbwFade.targetR, g_rgbwFade.targetG, g_rgbwFade.targetB, g_rgbwFade.targetW);
        osTimerStop(g_rgbwFadeTimer);
        return;
    }

    g_rgbwFade.step++;
    const uint32_t s = g_rgbwFade.step;
    const uint32_t n = g_rgbwFade.totalSteps;

    const uint16_t r = static_cast<uint16_t>(g_rgbwFade.startR +
        ((static_cast<int32_t>(g_rgbwFade.targetR) - static_cast<int32_t>(g_rgbwFade.startR)) * static_cast<int32_t>(s)) /
            static_cast<int32_t>(n));
    const uint16_t g = static_cast<uint16_t>(g_rgbwFade.startG +
        ((static_cast<int32_t>(g_rgbwFade.targetG) - static_cast<int32_t>(g_rgbwFade.startG)) * static_cast<int32_t>(s)) /
            static_cast<int32_t>(n));
    const uint16_t b = static_cast<uint16_t>(g_rgbwFade.startB +
        ((static_cast<int32_t>(g_rgbwFade.targetB) - static_cast<int32_t>(g_rgbwFade.startB)) * static_cast<int32_t>(s)) /
            static_cast<int32_t>(n));
    const uint8_t w = static_cast<uint8_t>(g_rgbwFade.startW +
        ((static_cast<int32_t>(g_rgbwFade.targetW) - static_cast<int32_t>(g_rgbwFade.startW)) * static_cast<int32_t>(s)) /
            static_cast<int32_t>(n));

    ApplyRgbwNow(r, g, b, w);
}

static void StartRgbwFade(uint16_t targetR, uint16_t targetG, uint16_t targetB, uint8_t targetW, uint32_t durationMs)
{
    targetW = (targetW > 100u) ? 100u : targetW;

    if (g_rgbwFadeTimer == nullptr)
    {
        g_rgbwFadeTimer = osTimerNew(RgbwFadeTimerCallback, osTimerPeriodic, nullptr, nullptr);
        if (g_rgbwFadeTimer == nullptr)
        {
            ApplyRgbwNow(targetR, targetG, targetB, targetW);
            return;
        }
    }

    uint16_t curR = 0, curG = 0, curB = 0;
    uint8_t curW = 0;
    GetCurrentRgbw(&curR, &curG, &curB, &curW);

    const uint16_t dR = (curR > targetR) ? static_cast<uint16_t>(curR - targetR) : static_cast<uint16_t>(targetR - curR);
    const uint16_t dG = (curG > targetG) ? static_cast<uint16_t>(curG - targetG) : static_cast<uint16_t>(targetG - curG);
    const uint16_t dB = (curB > targetB) ? static_cast<uint16_t>(curB - targetB) : static_cast<uint16_t>(targetB - curB);
    const uint8_t dW = (curW > targetW) ? static_cast<uint8_t>(curW - targetW) : static_cast<uint8_t>(targetW - curW);

    // Tiny low-level adjustments cause visible shimmer when frequently retargeted.
    if (dR <= kFadeRetargetMinDeltaRgb && dG <= kFadeRetargetMinDeltaRgb && dB <= kFadeRetargetMinDeltaRgb &&
        dW <= kFadeRetargetMinDeltaW)
    {
        ApplyRgbwNow(targetR, targetG, targetB, targetW);
        g_rgbwFade.active = false;
        osTimerStop(g_rgbwFadeTimer);
        return;
    }

    const uint32_t nowTick = osKernelGetTickCount();
    if (g_rgbwFade.active)
    {
        const uint32_t elapsed = nowTick - g_lastFadeRetargetTick;
        if (elapsed < kFadeRetargetMinMs)
        {
            // Keep current trajectory to avoid high-frequency restart jitter.
            g_rgbwFade.targetR = targetR;
            g_rgbwFade.targetG = targetG;
            g_rgbwFade.targetB = targetB;
            g_rgbwFade.targetW = targetW;
            return;
        }
    }

    g_rgbwFade.active = true;
    g_rgbwFade.step = 0;
    g_rgbwFade.startR = curR;
    g_rgbwFade.startG = curG;
    g_rgbwFade.startB = curB;
    g_rgbwFade.startW = curW;
    g_rgbwFade.targetR = targetR;
    g_rgbwFade.targetG = targetG;
    g_rgbwFade.targetB = targetB;
    g_rgbwFade.targetW = targetW;
    if (durationMs == 0u)
    {
        durationMs = APP_COLOR_FADE_MS;
    }
    g_rgbwFade.totalSteps = ComputeFadeSteps(durationMs);
    g_lastFadeRetargetTick = nowTick;

    osTimerStart(g_rgbwFadeTimer, kRgbwFadeStepMs);
}

static uint8_t Level254To255(uint8_t level254)
{
    return static_cast<uint8_t>((static_cast<uint16_t>(level254) * 255u) / 254u);
}

static uint8_t Level254ToPercent(uint8_t level254)
{
    return static_cast<uint8_t>((static_cast<uint16_t>(level254) * 100u + 127u) / 254u);
}

static float Clamp01(float v)
{
    if (v < 0.0f)
    {
        return 0.0f;
    }
    if (v > 1.0f)
    {
        return 1.0f;
    }
    return v;
}

static float GammaCorrect(float v)
{
    v = Clamp01(v);
    return (v <= 0.00304f) ? (12.92f * v) : (1.055f * powf(v, 1.0f / 2.4f) - 0.055f);
}

static void XYToRgb(uint16_t currentX, uint16_t currentY, uint8_t level255, uint8_t * r, uint8_t * g, uint8_t * b)
{
    const float x = static_cast<float>(currentX) / 65535.0f;
    const float y = static_cast<float>(currentY) / 65535.0f;
    const float z = 1.0f - x - y;

    if (y <= 0.0001f)
    {
        *r = 0;
        *g = 0;
        *b = 0;
        return;
    }

    // Standard CIE xyY -> XYZ conversion.
    const float Y = static_cast<float>(level255) / 255.0f;
    const float X = (Y / y) * x;
    const float Z = (Y / y) * z;

    // Standard XYZ(D65) -> linear sRGB.
    float rLin = 3.2406f * X - 1.5372f * Y - 0.4986f * Z;
    float gLin = -0.9689f * X + 1.8758f * Y + 0.0415f * Z;
    float bLin = 0.0557f * X - 0.2040f * Y + 1.0570f * Z;

    // Clamp negatives before calibration/normalization.
    if (rLin < 0.0f)
    {
        rLin = 0.0f;
    }
    if (gLin < 0.0f)
    {
        gLin = 0.0f;
    }
    if (bLin < 0.0f)
    {
        bLin = 0.0f;
    }

    // Lamp-specific calibration matrix + channel gains for real-world tuning.
    float rCal = kXyCalM00 * rLin + kXyCalM01 * gLin + kXyCalM02 * bLin;
    float gCal = kXyCalM10 * rLin + kXyCalM11 * gLin + kXyCalM12 * bLin;
    float bCal = kXyCalM20 * rLin + kXyCalM21 * gLin + kXyCalM22 * bLin;
    rCal *= kXyGainR;
    gCal *= kXyGainG;
    bCal *= kXyGainB;

    if (rCal < 0.0f)
    {
        rCal = 0.0f;
    }
    if (gCal < 0.0f)
    {
        gCal = 0.0f;
    }
    if (bCal < 0.0f)
    {
        bCal = 0.0f;
    }

    // Keep chroma when out-of-gamut by scaling all channels together.
    float maxLin = rCal;
    if (gCal > maxLin)
    {
        maxLin = gCal;
    }
    if (bCal > maxLin)
    {
        maxLin = bCal;
    }
    if (maxLin > 1.0f)
    {
        rCal /= maxLin;
        gCal /= maxLin;
        bCal /= maxLin;
    }

    const float rGamma = Clamp01(GammaCorrect(rCal));
    const float gGamma = Clamp01(GammaCorrect(gCal));
    const float bGamma = Clamp01(GammaCorrect(bCal));

    uint8_t r8 = static_cast<uint8_t>(rGamma * 255.0f + 0.5f);
    uint8_t g8 = static_cast<uint8_t>(gGamma * 255.0f + 0.5f);
    uint8_t b8 = static_cast<uint8_t>(bGamma * 255.0f + 0.5f);

    *r = r8;
    *g = g8;
    *b = b8;

    printf("[RTT][XY] in x=%u y=%u (%.5f,%.5f) lvl=%u -> rgb=%u,%u,%u\n",
           static_cast<unsigned>(currentX), static_cast<unsigned>(currentY),
           static_cast<double>(x), static_cast<double>(y),
           static_cast<unsigned>(level255),
           static_cast<unsigned>(r8), static_cast<unsigned>(g8), static_cast<unsigned>(b8));
}

// 色温转RGB算法（常用经验公式，色温K范围1000~6500）
static void colorTempToRGB(uint16_t kelvin, uint8_t *r, uint8_t *g, uint8_t *b) {
    float temp = kelvin / 100.0f;
    float rF, gF, bF;
    // Red
    if (temp <= 66) {
        rF = 255;
    } else {
        rF = temp - 60;
        rF = 329.698727446f * powf(rF, -0.1332047592f);
        if (rF < 0) rF = 0;
        if (rF > 255) rF = 255;
    }
    // Green
    if (temp <= 66) {
        gF = temp;
        gF = 99.4708025861f * logf(gF) - 161.1195681661f;
        if (gF < 0) gF = 0;
        if (gF > 255) gF = 255;
    } else {
        gF = temp - 60;
        gF = 288.1221695283f * powf(gF, -0.0755148492f);
        if (gF < 0) gF = 0;
        if (gF > 255) gF = 255;
    }
    // Blue
    if (temp >= 66) {
        bF = 255;
    } else if (temp <= 19) {
        bF = 0;
    } else {
        bF = temp - 10;
        bF = 138.5177312231f * logf(bF) - 305.0447927307f;
        if (bF < 0) bF = 0;
        if (bF > 255) bF = 255;
    }
    *r = (uint8_t)rF;
    *g = (uint8_t)gF;
    *b = (uint8_t)bF;
}
/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
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

/**
 * @file
 *   This file implements the handler for data model messages.
 */

#include "LightingManager.h"
#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
#include "RGBLEDWidget.h"
#endif //(defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/ConcreteAttributePath.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/KeyValueStoreManager.h>
#include <platform/PersistedStorage.h>

#ifdef SL_MATTER_ENABLE_AWS
#include "MatterAws.h"
#endif // SL_MATTER_ENABLE_AWS

#include "sl_component_catalog.h"
#include "sm15135e.h"
#if defined(SL_CATALOG_ZIGBEE_ZCL_FRAMEWORK_CORE_PRESENT)
#include <MultiProtocolDataModelHelper.h>
#endif // SL_CATALOG_ZIGBEE_ZCL_FRAMEWORK_CORE_PRESENT

using namespace ::chip;
using namespace ::chip::app::Clusters;

namespace {
constexpr const char kPowerOnMemoryKey[] = "PwrRGBWMem";
constexpr uint8_t kPowerOnMemoryVersion = 2;

struct PowerOnMemoryState
{
    uint8_t version;
    uint8_t onOff;
    uint8_t level;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t wDuty;
    uint8_t colorSource;
};

static PowerOnMemoryState g_restoredPowerOnState = {};
}

extern "C" bool MatterRestorePowerOnMemoryIfAny()
{
    PowerOnMemoryState state = {};
    CHIP_ERROR err = chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr().Get(kPowerOnMemoryKey, &state, sizeof(state));
    if (err != CHIP_NO_ERROR || state.version != kPowerOnMemoryVersion)
    {
        return false;
    }

    ApplyRgbwNow(state.r, state.g, state.b, state.wDuty);
    g_restoredPowerOnState = state;
    g_hasRestoredPowerOnState = true;
    g_memOnOff = state.onOff;
    g_memLevel = state.level;
    g_colorSource = (state.colorSource == 1u) ? 1u : 0u;

    // Rebuild runtime color context from snapshot so later level changes
    // use the restored color instead of default white targets.
    g_buttonPresetActive = (g_colorSource == 1u);
    g_isColorTempMode = (state.wDuty > 0u && g_colorSource == 0u);

    const uint8_t level255 = Level254To255(state.level);
    if (level255 > 0u)
    {
        g_rgbTargetR = static_cast<uint8_t>((static_cast<uint16_t>(state.r) * 255u + level255 / 2u) / level255);
        g_rgbTargetG = static_cast<uint8_t>((static_cast<uint16_t>(state.g) * 255u + level255 / 2u) / level255);
        g_rgbTargetB = static_cast<uint8_t>((static_cast<uint16_t>(state.b) * 255u + level255 / 2u) / level255);
    }
    else
    {
        g_rgbTargetR = state.r;
        g_rgbTargetG = state.g;
        g_rgbTargetB = state.b;
    }

    RememberNonZeroRgbTarget(g_rgbTargetR, g_rgbTargetG, g_rgbTargetB);

    ChipLogError(Zcl, "[DIM] power-on restore applied: on=%u level=%u rgb=%u,%u,%u w=%u",
                 static_cast<unsigned>(state.onOff), static_cast<unsigned>(state.level),
                 static_cast<unsigned>(state.r), static_cast<unsigned>(state.g), static_cast<unsigned>(state.b),
                 static_cast<unsigned>(state.wDuty));
    return true;
}

extern "C" void MatterSavePowerOnMemorySnapshot()
{
    PowerOnMemoryState state = {};
    state.version = kPowerOnMemoryVersion;
    state.onOff = g_memOnOff;
    state.level = g_memLevel;

    if (state.onOff == 0u)
    {
        // Keep off-state deterministic across reboot.
        state.r = 0u;
        state.g = 0u;
        state.b = 0u;
        state.wDuty = 0u;
    }
    else if (g_rgbwFade.active)
    {
        // During fade, persist the intended end state, not an in-flight intermediate step.
        state.r = static_cast<uint8_t>(g_rgbwFade.targetR > 255u ? 255u : g_rgbwFade.targetR);
        state.g = static_cast<uint8_t>(g_rgbwFade.targetG > 255u ? 255u : g_rgbwFade.targetG);
        state.b = static_cast<uint8_t>(g_rgbwFade.targetB > 255u ? 255u : g_rgbwFade.targetB);
        state.wDuty = g_rgbwFade.targetW;
    }
    else
    {
        uint16_t r = 0, g = 0, b = 0;
        uint8_t w = 0;
        GetCurrentRgbw(&r, &g, &b, &w);
        state.r = static_cast<uint8_t>(r > 255u ? 255u : r);
        state.g = static_cast<uint8_t>(g > 255u ? 255u : g);
        state.b = static_cast<uint8_t>(b > 255u ? 255u : b);
        state.wDuty = w;
    }

    state.colorSource = g_colorSource;

    CHIP_ERROR err = chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr().Put(kPowerOnMemoryKey, &state, sizeof(state));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(Zcl, "[DIM] failed to save power-on memory: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

extern "C" void MatterSetBootOutputSuppress(uint8_t suppress)
{
    g_suppressAttributeHwOutput = (suppress != 0u);
}

extern "C" void MatterReapplyPowerOnMemoryOutput(void)
{
    if (!g_hasRestoredPowerOnState)
    {
        return;
    }

    ApplyRgbwNow(g_restoredPowerOnState.r, g_restoredPowerOnState.g, g_restoredPowerOnState.b,
                 g_restoredPowerOnState.wDuty);
    ChipLogError(Zcl, "[DIM] power-on output re-applied: rgb=%u,%u,%u w=%u",
                 static_cast<unsigned>(g_restoredPowerOnState.r), static_cast<unsigned>(g_restoredPowerOnState.g),
                 static_cast<unsigned>(g_restoredPowerOnState.b), static_cast<unsigned>(g_restoredPowerOnState.wDuty));
}

static void ApplyButtonPresetByLevel(uint8_t level254)
{
    const uint8_t level255 = Level254To255(level254);
    const uint8_t levelPct = Level254ToPercent(level254);

    const uint32_t r = (static_cast<uint32_t>(g_buttonPresetRPermille) * static_cast<uint32_t>(level255)) / 1000u;
    const uint32_t g = (static_cast<uint32_t>(g_buttonPresetGPermille) * static_cast<uint32_t>(level255)) / 1000u;
    const uint32_t b = (static_cast<uint32_t>(g_buttonPresetBPermille) * static_cast<uint32_t>(level255)) / 1000u;

    // sl_pwm_set_duty_cycle expects percent (0..100), not 0..255.
    const uint32_t wPct = (static_cast<uint32_t>(g_buttonPresetWPermille) * static_cast<uint32_t>(levelPct) + 500u) / 1000u;

    ApplyRgbwNow(static_cast<uint16_t>(r > 255u ? 255u : r),
                 static_cast<uint16_t>(g > 255u ? 255u : g),
                 static_cast<uint16_t>(b > 255u ? 255u : b),
                 static_cast<uint8_t>(wPct > 100u ? 100u : wPct));
}

static void ComputePresetRgbw(uint8_t level254, uint16_t & r, uint16_t & g, uint16_t & b, uint8_t & wDuty)
{
    const uint8_t level255 = Level254To255(level254);
    const uint8_t levelPct = Level254ToPercent(level254);

    r = static_cast<uint16_t>((static_cast<uint32_t>(g_buttonPresetRPermille) * static_cast<uint32_t>(level255)) / 1000u);
    g = static_cast<uint16_t>((static_cast<uint32_t>(g_buttonPresetGPermille) * static_cast<uint32_t>(level255)) / 1000u);
    b = static_cast<uint16_t>((static_cast<uint32_t>(g_buttonPresetBPermille) * static_cast<uint32_t>(level255)) / 1000u);

    const uint32_t wPct = (static_cast<uint32_t>(g_buttonPresetWPermille) * static_cast<uint32_t>(levelPct) + 500u) / 1000u;
    wDuty = static_cast<uint8_t>(wPct > 100u ? 100u : wPct);
}

static void ComputeCtRgbw(uint16_t mireds, uint8_t levelPct, uint8_t & rOut, uint8_t & gOut, uint8_t & bOut, uint8_t & wDuty)
{
    uint16_t targetK = 0;
    if (mireds > 0)
    {
        targetK = static_cast<uint16_t>(1000000UL / mireds);
    }
    else
    {
        targetK = 2700;
    }

    uint8_t rT = 0, gT = 0, bT = 0;
    uint8_t rW = 0, gW = 0, bW = 0;
    colorTempToRGB(targetK, &rT, &gT, &bT);
    colorTempToRGB(2700, &rW, &gW, &bW);

    const int16_t rC = static_cast<int16_t>(rT) - static_cast<int16_t>(rW);
    const int16_t gC = static_cast<int16_t>(gT) - static_cast<int16_t>(gW);
    const int16_t bC = static_cast<int16_t>(bT) - static_cast<int16_t>(bW);

    rOut = (rC > 0) ? static_cast<uint8_t>(rC) : 0u;
    gOut = (gC > 0) ? static_cast<uint8_t>(gC) : 0u;
    bOut = (bC > 0) ? static_cast<uint8_t>(bC) : 0u;

    wDuty = static_cast<uint8_t>(levelPct > 100u ? 100u : levelPct);
}

extern "C" void MatterComputeCtRgbw(uint16_t mireds, uint8_t level254, uint8_t * rOut, uint8_t * gOut, uint8_t * bOut,
                                     uint8_t * wDutyOut)
{
    if (rOut == nullptr || gOut == nullptr || bOut == nullptr || wDutyOut == nullptr)
    {
        return;
    }

    ComputeCtRgbw(mireds, Level254ToPercent(level254), *rOut, *gOut, *bOut, *wDutyOut);
}

extern "C" void MatterSetOffTransitionActive(uint8_t active)
{
    g_offTransitionActive = (active != 0u);
}

extern "C" void MatterSetButtonPresetPwmWithFade(uint16_t wPermille, uint16_t rPermille, uint16_t gPermille, uint16_t bPermille,
                                                   uint8_t level254)
{
    g_buttonPresetWPermille = wPermille;
    g_buttonPresetRPermille = rPermille;
    g_buttonPresetGPermille = gPermille;
    g_buttonPresetBPermille = bPermille;
    g_buttonPresetActive = true;
    g_colorSource = 1u;

    const uint8_t level255 = Level254To255(level254);
    const uint8_t levelPct = Level254ToPercent(level254);

    const uint16_t r = static_cast<uint16_t>((static_cast<uint32_t>(rPermille) * static_cast<uint32_t>(level255)) / 1000u);
    const uint16_t g = static_cast<uint16_t>((static_cast<uint32_t>(gPermille) * static_cast<uint32_t>(level255)) / 1000u);
    const uint16_t b = static_cast<uint16_t>((static_cast<uint32_t>(bPermille) * static_cast<uint32_t>(level255)) / 1000u);
    const uint8_t w = static_cast<uint8_t>(((static_cast<uint32_t>(wPermille) * static_cast<uint32_t>(levelPct) + 500u) / 1000u) > 100u
                                               ? 100u
                                               : ((static_cast<uint32_t>(wPermille) * static_cast<uint32_t>(levelPct) + 500u) / 1000u));

    StartRgbwFade(r, g, b, w, APP_COLOR_FADE_MS);
    ChipLogError(Zcl, "[DIM] button preset pwm fade: W=%u R=%u G=%u B=%u level=%u",
                 static_cast<unsigned>(wPermille), static_cast<unsigned>(rPermille),
                 static_cast<unsigned>(gPermille), static_cast<unsigned>(bPermille), static_cast<unsigned>(level254));
}

extern "C" void MatterSetButtonPresetPwm(uint16_t wPermille, uint16_t rPermille, uint16_t gPermille, uint16_t bPermille,
                                           uint8_t level254)
{
    g_buttonPresetWPermille = wPermille;
    g_buttonPresetRPermille = rPermille;
    g_buttonPresetGPermille = gPermille;
    g_buttonPresetBPermille = bPermille;
    g_buttonPresetActive = true;
    g_colorSource = 1u;
    ApplyButtonPresetByLevel(level254);
    ChipLogError(Zcl, "[DIM] button preset pwm: W=%u R=%u G=%u B=%u level=%u",
                 static_cast<unsigned>(wPermille), static_cast<unsigned>(rPermille),
                 static_cast<unsigned>(gPermille), static_cast<unsigned>(bPermille), static_cast<unsigned>(level254));
}

extern "C" void MatterSetButtonPresetTransaction(uint8_t active)
{
    g_buttonPresetTxn = (active != 0);
}

extern "C" void MatterSetButtonPresetSuppressColorCallbacks(uint8_t count)
{
    g_buttonPresetSuppressColorCb = count;
}

extern "C" uint8_t MatterGetButtonPresetActive()
{
    return g_buttonPresetActive ? 1u : 0u;
}

extern "C" void MatterSetButtonPresetActive(uint8_t active)
{
    g_buttonPresetActive = (active != 0);
    if (active != 0u)
    {
        g_colorSource = 1u;
    }
}

extern "C" void MatterRestoreButtonPresetPermilles(uint16_t wPermille, uint16_t rPermille, uint16_t gPermille,
                                                     uint16_t bPermille)
{
    g_buttonPresetWPermille = wPermille;
    g_buttonPresetRPermille = rPermille;
    g_buttonPresetGPermille = gPermille;
    g_buttonPresetBPermille = bPermille;
    g_buttonPresetActive = true;
    g_colorSource = 1u;
}

extern "C" uint8_t MatterGetMemLevel()
{
    return g_memLevel;
}

extern "C" uint8_t MatterGetColorSource()
{
    return g_colorSource;
}

extern "C" void MatterGetCurrentOutputRgbw(uint8_t * r, uint8_t * g, uint8_t * b, uint8_t * wDuty)
{
    uint16_t r16 = 0;
    uint16_t g16 = 0;
    uint16_t b16 = 0;
    uint8_t w = 0;
    GetCurrentRgbw(&r16, &g16, &b16, &w);

    if (r != nullptr)
    {
        *r = static_cast<uint8_t>(r16 <= 255u ? r16
                                               : (static_cast<uint32_t>(r16) * 255u + 32767u) / 65535u);
    }
    if (g != nullptr)
    {
        *g = static_cast<uint8_t>(g16 <= 255u ? g16
                                               : (static_cast<uint32_t>(g16) * 255u + 32767u) / 65535u);
    }
    if (b != nullptr)
    {
        *b = static_cast<uint8_t>(b16 <= 255u ? b16
                                               : (static_cast<uint32_t>(b16) * 255u + 32767u) / 65535u);
    }
    if (wDuty != nullptr)
    {
        *wDuty = w;
    }
}

extern "C" void MatterRestoreOutputState(uint8_t r, uint8_t g, uint8_t b, uint8_t wDuty, uint8_t colorSource,
                                           uint8_t presetActive)
{
    g_colorSource = (colorSource == 1u) ? 1u : 0u;
    g_buttonPresetActive = (presetActive != 0u) && (g_colorSource == 1u);
    g_isColorTempMode = (g_colorSource == 0u && wDuty > 0u);

    g_rgbTargetR = r;
    g_rgbTargetG = g;
    g_rgbTargetB = b;

    StartRgbwFade(r, g, b, wDuty > 100u ? 100u : wDuty, APP_COLOR_FADE_MS);
    ChipLogError(Zcl, "[DIM] restore output state src=%u preset=%u rgbw=%u,%u,%u,%u",
                 static_cast<unsigned>(g_colorSource), static_cast<unsigned>(g_buttonPresetActive ? 1u : 0u),
                 static_cast<unsigned>(r), static_cast<unsigned>(g), static_cast<unsigned>(b),
                 static_cast<unsigned>(wDuty));
}

void MatterPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                       uint8_t * value)
{
    // 中文导读（用途）：此回调处理的是“功能属性变化”（OnOff/Level/Color 等），
    // 修改建议：联调问题时优先打印 clusterId/attributeId/value，先确认命令语义再改渲染逻辑。
    // 不是开关原始按键事件（单击/双击/长按）。
    // 换言之，Zigbee 绑定成功后，开关命令最终会落到这些属性更新路径。
    [[maybe_unused]] EndpointId endpointId = attributePath.mEndpointId;
    ClusterId clusterId                    = attributePath.mClusterId;
    AttributeId attributeId                = attributePath.mAttributeId;

    if (clusterId == OnOff::Id && attributeId == OnOff::Attributes::OnOff::Id && value != nullptr)
    {
        g_memOnOff = (*value != 0) ? 1u : 0u;
    }
    if (clusterId == LevelControl::Id && attributeId == LevelControl::Attributes::CurrentLevel::Id && value != nullptr)
    {
        g_memLevel = *value;
    }

    // During boot, Matter/Zigbee may replay persisted attributes (often CT first) and
    // briefly drive white PWM before power-on memory is applied. Skip hardware here.
    if (g_suppressAttributeHwOutput &&
        (clusterId == OnOff::Id || clusterId == LevelControl::Id || clusterId == ColorControl::Id))
    {
        ChipLogError(Zcl, "[DIM] boot suppress hw for cluster " ChipLogFormatMEI, ChipLogValueMEI(clusterId));
        return;
    }

    ChipLogError(Zcl, "[DIM] Cluster callback: " ChipLogFormatMEI, ChipLogValueMEI(clusterId));
    // 新增：根据模式自动切换白光和RGB输出
    if (clusterId == ColorControl::Id) {
        if (g_offTransitionActive)
        {
            ChipLogError(Zcl, "[DIM] ignore ColorControl while off-fade active");
            return;
        }

        if (g_buttonPresetSuppressColorCb > 0)
        {
            g_buttonPresetSuppressColorCb--;
            ChipLogError(Zcl, "[DIM] suppress ColorControl callback after preset switch (remain=%u)",
                         static_cast<unsigned>(g_buttonPresetSuppressColorCb));
            return;
        }

        if (g_buttonPresetTxn)
        {
            ChipLogError(Zcl, "[DIM] defer ColorControl render during preset transaction");
            return;
        }

        // Ignore stale attribute replay (common on boot / OnOff toggle) before exiting
        // button preset mode, otherwise RGB presets are lost and white PWM takes over.
        if (attributeId == ColorControl::Attributes::ColorTemperatureMireds::Id)
        {
            ColorControl::ColorModeEnum colorMode = ColorControl::ColorModeEnum::kColorTemperatureMireds;
            if (ColorControl::Attributes::ColorMode::Get(1, &colorMode) == chip::Protocols::InteractionModel::Status::Success &&
                colorMode != ColorControl::ColorModeEnum::kColorTemperatureMireds)
            {
                ChipLogError(Zcl, "[DIM] ignore CT callback while ColorMode!=CT");
                return;
            }
        }
        else if (attributeId == ColorControl::Attributes::CurrentHue::Id ||
                 attributeId == ColorControl::Attributes::CurrentSaturation::Id ||
                 attributeId == ColorControl::Attributes::CurrentX::Id ||
                 attributeId == ColorControl::Attributes::CurrentY::Id)
        {
            ColorControl::ColorModeEnum colorMode = ColorControl::ColorModeEnum::kCurrentHueAndCurrentSaturation;
            if (ColorControl::Attributes::ColorMode::Get(1, &colorMode) == chip::Protocols::InteractionModel::Status::Success &&
                colorMode == ColorControl::ColorModeEnum::kColorTemperatureMireds)
            {
                ChipLogError(Zcl, "[DIM] ignore HSV/XY callback while ColorMode=CT");
                return;
            }
        }

        // External app color-control writes exit button preset mode.
        g_buttonPresetActive = false;
        g_colorSource = 0u;
        if (attributeId == ColorControl::Attributes::ColorTemperatureMireds::Id)
        {
            g_isColorTempMode = true;
        }
        else if (attributeId == ColorControl::Attributes::CurrentHue::Id ||
                 attributeId == ColorControl::Attributes::CurrentSaturation::Id ||
                 attributeId == ColorControl::Attributes::CurrentX::Id ||
                 attributeId == ColorControl::Attributes::CurrentY::Id)
        {
            g_isColorTempMode = false;
        }

        if (attributeId == ColorControl::Attributes::ColorTemperatureMireds::Id) {
            // 色温模式，白光始终输出2700K，亮度跟随level，RGB补偿混光
            g_isColorTempMode = true;
            // 获取目标色温（单位mireds，需转K）
            uint16_t mireds = 0;
            memcpy(&mireds, value, sizeof(uint16_t));
            uint16_t targetK = 0;
            if (mireds > 0) {
                targetK = (uint16_t)(1000000UL / mireds);
            } else {
                targetK = 2700;
            }
            // 计算目标色温RGB
            uint8_t rT, gT, bT;
            colorTempToRGB(targetK, &rT, &gT, &bT);
            // 2700K白光的RGB
            uint8_t rW, gW, bW;
            colorTempToRGB(2700, &rW, &gW, &bW);
            // 计算补偿分量，防止溢出
            int16_t rC = (int16_t)rT - (int16_t)rW;
            int16_t gC = (int16_t)gT - (int16_t)gW;
            int16_t bC = (int16_t)bT - (int16_t)bW;
            // 只允许正向补偿，负值归零
            uint8_t rOut = rC > 0 ? (uint8_t)rC : 0;
            uint8_t gOut = gC > 0 ? (uint8_t)gC : 0;
            uint8_t bOut = bC > 0 ? (uint8_t)bC : 0;
            ChipLogError(Zcl, "[DIM] set_rgb_color: R=%u G=%u B=%u", rOut, gOut, bOut);
            // 获取当前level，映射到0~100（sl_pwm_set_duty_cycle 入参单位为百分比）
            uint8_t levelPct = 100;
            app::DataModel::Nullable<uint8_t> brightness;
            if (chip::app::Clusters::LevelControl::Attributes::CurrentLevel::Get(1, brightness) == chip::Protocols::InteractionModel::Status::Success && !brightness.IsNull()) {
                levelPct = Level254ToPercent(brightness.Value());
            }
            const uint8_t whiteDuty = levelPct;
            StartRgbwFade(rOut, gOut, bOut, whiteDuty, APP_COLOR_FADE_MS);
            ChipLogError(Zcl, "[DIM] set_white_pwm(ct): duty=%u (0..100)", whiteDuty);
            ChipLogError(Zcl, "[DIM] mode=ColorTemp K=%u WhiteLevelPct=%u RGBComp=%u,%u,%u", (unsigned)targetK, (unsigned)levelPct,
                         rOut, gOut, bOut);
        } else if (
            attributeId == ColorControl::Attributes::CurrentHue::Id ||
            attributeId == ColorControl::Attributes::CurrentSaturation::Id ||
            attributeId == ColorControl::Attributes::CurrentX::Id ||
            attributeId == ColorControl::Attributes::CurrentY::Id) {
            // RGB模式，白光熄灭，RGB亮度联动Level
            g_isColorTempMode = false;
            // White channel is handled by SM15135E; fade to W=0 in RGB mode.
            // 解析色彩属性，更新目标RGB缓存
            if (attributeId == ColorControl::Attributes::CurrentHue::Id || attributeId == ColorControl::Attributes::CurrentSaturation::Id) {
                // HSV转RGB（假设色调0~254，饱和度0~254，值恒255）
                uint8_t hue = 0, sat = 0;
                (void) chip::app::Clusters::ColorControl::Attributes::CurrentHue::Get(1, &hue);
                (void) chip::app::Clusters::ColorControl::Attributes::CurrentSaturation::Get(1, &sat);
                // HSV转RGB算法（值恒255） - 使用分段算法避免依赖额外数学函数
                float H = (hue * 360.0f) / 254.0f;
                float S = sat / 254.0f;
                float V = 1.0f;
                float Hd = H / 60.0f;
                int i = static_cast<int>(Hd) % 6;
                float f = Hd - static_cast<int>(Hd);
                float p = V * (1.0f - S);
                float q = V * (1.0f - S * f);
                float t = V * (1.0f - S * (1.0f - f));
                float rF = 0, gF = 0, bF = 0;
                switch (i)
                {
                    case 0: rF = V; gF = t; bF = p; break;
                    case 1: rF = q; gF = V; bF = p; break;
                    case 2: rF = p; gF = V; bF = t; break;
                    case 3: rF = p; gF = q; bF = V; break;
                    case 4: rF = t; gF = p; bF = V; break;
                    case 5: default: rF = V; gF = p; bF = q; break;
                }
                g_rgbTargetR = static_cast<uint8_t>(rF * 255.0f);
                g_rgbTargetG = static_cast<uint8_t>(gF * 255.0f);
                g_rgbTargetB = static_cast<uint8_t>(bF * 255.0f);
                RememberNonZeroRgbTarget(g_rgbTargetR, g_rgbTargetG, g_rgbTargetB);
            } else if (attributeId == ColorControl::Attributes::CurrentX::Id || attributeId == ColorControl::Attributes::CurrentY::Id) {
                uint16_t x = 0, y = 0;
                (void) chip::app::Clusters::ColorControl::Attributes::CurrentX::Get(1, &x);
                (void) chip::app::Clusters::ColorControl::Attributes::CurrentY::Get(1, &y);

                // Many controllers send X and Y separately while dragging.
                // Coalesce partial updates and render only when both parts arrived.
                if (attributeId == ColorControl::Attributes::CurrentX::Id)
                {
                    g_xyPendingX = x;
                    g_xyPendingXDirty = true;
                }
                else
                {
                    g_xyPendingY = y;
                    g_xyPendingYDirty = true;
                }

                if (!(g_xyPendingXDirty && g_xyPendingYDirty))
                {
                    ChipLogError(Zcl, "[XY] hold partial update x=%u y=%u", static_cast<unsigned>(x), static_cast<unsigned>(y));
                    return;
                }

                x = g_xyPendingX;
                y = g_xyPendingY;
                g_xyPendingXDirty = false;
                g_xyPendingYDirty = false;

                XYToRgb(x, y, 255, &g_rgbTargetR, &g_rgbTargetG, &g_rgbTargetB);
                RememberNonZeroRgbTarget(g_rgbTargetR, g_rgbTargetG, g_rgbTargetB);
                ChipLogError(Zcl, "[XY] recv x=%u y=%u -> targetRgb=%u,%u,%u",
                             static_cast<unsigned>(x), static_cast<unsigned>(y),
                             static_cast<unsigned>(g_rgbTargetR), static_cast<unsigned>(g_rgbTargetG),
                             static_cast<unsigned>(g_rgbTargetB));
            }
            // 获取当前level，映射到0~255
            uint8_t level = 255;
            app::DataModel::Nullable<uint8_t> brightness;
            if (chip::app::Clusters::LevelControl::Attributes::CurrentLevel::Get(1, brightness) == chip::Protocols::InteractionModel::Status::Success && !brightness.IsNull()) {
                level = Level254To255(brightness.Value());
            }
            // 按level缩放目标RGB
            uint8_t r = (g_rgbTargetR * level) / 255;
            uint8_t g = (g_rgbTargetG * level) / 255;
            uint8_t b = (g_rgbTargetB * level) / 255;
            StartRgbwFade(r, g, b, 0, APP_COLOR_FADE_MS);
            ChipLogError(Zcl, "[DIM] mode=RGB level=%u rgb=%u,%u,%u", (unsigned)level, r, g, b);
        }
    }

    // Level属性变化时，按当前模式实时更新输出（颜色不变时也要生效）
    if (clusterId == LevelControl::Id && attributeId == LevelControl::Attributes::CurrentLevel::Id && value != nullptr)
    {
        if (g_offTransitionActive)
        {
            ChipLogError(Zcl, "[DIM] ignore Level while off-fade active");
            return;
        }

        bool isOn = true;
        (void) OnOff::Attributes::OnOff::Get(1, &isOn);
        if (!isOn)
        {
            // 设备处于关灯态时，忽略后续 Level 回调，避免被平台补发的亮度值重新点亮。
            ApplyRgbwNow(0, 0, 0, 0);
            ChipLogError(Zcl, "[DIM] level ignored because OnOff=0");
            return;
        }

        const uint8_t level = Level254To255(*value);
        if (g_buttonPresetActive)
        {
            ApplyButtonPresetByLevel(*value);
            ChipLogError(Zcl, "[DIM] level change in button preset mode: level=%u", static_cast<unsigned>(level));
        }
        else if (g_isColorTempMode)
        {
            const uint8_t levelPct = Level254ToPercent(*value);
            const uint8_t whiteDuty = levelPct;
            uint16_t curR = 0, curG = 0, curB = 0;
            uint8_t curW = 0;
            GetCurrentRgbw(&curR, &curG, &curB, &curW);
            ApplyRgbwNow(curR, curG, curB, whiteDuty);
            ChipLogError(Zcl, "[DIM] level change in CT mode: levelPct=%u white=%u", (unsigned) levelPct, (unsigned) whiteDuty);
        }
        else
        {
            ApplyRgbLevelNow(*value);
            ChipLogError(Zcl, "[DIM] level change in RGB mode: level254=%u", static_cast<unsigned>(*value));
        }
    }
    if (clusterId == OnOff::Id && attributeId == OnOff::Attributes::OnOff::Id)
    {
        const bool isOn = (value != nullptr) && (*value != 0);
        g_offTransitionActive = !isOn;
        app::DataModel::Nullable<uint8_t> brightness;
        uint8_t level254 = 254;
        if (chip::app::Clusters::LevelControl::Attributes::CurrentLevel::Get(1, brightness)
            == chip::Protocols::InteractionModel::Status::Success && !brightness.IsNull())
        {
            level254 = brightness.Value();
        }

        if (isOn && !g_buttonPresetActive && !g_isColorTempMode
            && g_rgbTargetR == 0u && g_rgbTargetG == 0u && g_rgbTargetB == 0u)
        {
            // App sometimes sends On/Level without color after Off; recover to the
            // last valid RGB target to avoid "logical on but visually off".
            g_rgbTargetR = g_lastNonZeroTargetR;
            g_rgbTargetG = g_lastNonZeroTargetG;
            g_rgbTargetB = g_lastNonZeroTargetB;

            const uint8_t level = Level254To255(level254);
            const uint8_t rOut = static_cast<uint8_t>((static_cast<uint16_t>(g_rgbTargetR) * level) / 255u);
            const uint8_t gOut = static_cast<uint8_t>((static_cast<uint16_t>(g_rgbTargetG) * level) / 255u);
            const uint8_t bOut = static_cast<uint8_t>((static_cast<uint16_t>(g_rgbTargetB) * level) / 255u);
            StartRgbwFade(rOut, gOut, bOut, 0, APP_ONOFF_FADE_MS);
            ChipLogError(Zcl, "[DIM] OnOff=1 fallback target restored: level=%u rgb=%u,%u,%u",
                         static_cast<unsigned>(level), static_cast<unsigned>(rOut),
                         static_cast<unsigned>(gOut), static_cast<unsigned>(bOut));
        }

        if (!isOn)
        {
            // 关灯时强制清零所有实际 PWM 输出，避免逻辑 OFF 但灯仍亮。
            // 注意：不要清除 g_buttonPresetActive / g_isColorTempMode，否则再次开灯
            // （尤其是通过 App 开灯）会丢失关灯前的颜色来源，错误地恢复成白光/默认色。
            StartRgbwFade(0, 0, 0, 0, APP_ONOFF_FADE_MS);
            ChipLogError(Zcl, "[DIM] OnOff=0 force pwm off");
        }
        else
        {
            if (g_buttonPresetActive)
            {
                uint16_t rOut = 0;
                uint16_t gOut = 0;
                uint16_t bOut = 0;
                uint8_t wDuty = 0;
                ComputePresetRgbw(level254, rOut, gOut, bOut, wDuty);
                StartRgbwFade(rOut, gOut, bOut, wDuty, APP_ONOFF_FADE_MS);
            }
            else if (g_isColorTempMode)
            {
                uint16_t mireds = 0;
                uint8_t rOut = 0, gOut = 0, bOut = 0, wDuty = 0;
                ColorControl::Attributes::ColorTemperatureMireds::Get(1, &mireds);
                ComputeCtRgbw(mireds, Level254ToPercent(level254), rOut, gOut, bOut, wDuty);
                StartRgbwFade(rOut, gOut, bOut, wDuty, APP_ONOFF_FADE_MS);
            }
            else
            {
                const uint8_t level = Level254To255(level254);
                const uint8_t rOut = static_cast<uint8_t>((static_cast<uint16_t>(g_rgbTargetR) * level) / 255u);
                const uint8_t gOut = static_cast<uint8_t>((static_cast<uint16_t>(g_rgbTargetG) * level) / 255u);
                const uint8_t bOut = static_cast<uint8_t>((static_cast<uint16_t>(g_rgbTargetB) * level) / 255u);
                StartRgbwFade(rOut, gOut, bOut, 0, APP_ONOFF_FADE_MS);
            }
        }

#ifdef SL_MATTER_ENABLE_AWS
        ChipLogProgress(Zcl, "sending light state update");
        MatterAwsSendMsg("light/state", (const char *) (value ? (*value ? "on" : "off") : "invalid"));
#endif // SL_MATTER_ENABLE_AWS
        LightMgr().InitiateAction(AppEvent::kEventType_Light, isOn ? LightingManager::ON_ACTION : LightingManager::OFF_ACTION,
                                  value);
    }
    // WIP Apply attribute change to Light
    else if (clusterId == LevelControl::Id)
    {
        ChipLogProgress(Zcl, "Level Control attribute ID: " ChipLogFormatMEI " Type: %u Value: %u, length %u",
                        ChipLogValueMEI(attributeId), type, (value != nullptr) ? *value : 0u, size);

        if (attributeId == LevelControl::Attributes::CurrentLevel::Id && value != nullptr)
        {
            LightMgr().InitiateAction(AppEvent::kEventType_Light, LightingManager::LEVEL_ACTION, value);
        }
    }
    // WIP Apply attribute change to Light
    if (clusterId == ColorControl::Id)
    {
        ChipLogProgress(Zcl, "Color Control attribute ID: " ChipLogFormatMEI " Type: %u Value: %u, length %u",
                        ChipLogValueMEI(attributeId), type, (value != nullptr) ? *value : 0u, size);
#if (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)

        if (clusterId == ColorControl::Id && attributeId == ColorControl::Attributes::CurrentX::Id)
        {
            ChipLogProgress(Zcl, "Color Control attribute ID: " ChipLogFormatMEI " Type: %u Value: %u, length %u",
                            ChipLogValueMEI(attributeId), type, (value != nullptr) ? *value : 0u, size);

            LightMgr().InitiateLightCtrlAction(AppEvent::kEventType_Light, LightingManager::COLOR_ACTION_XY, attributeId, value);
        }
        else if (clusterId == ColorControl::Id && attributeId == ColorControl::Attributes::CurrentY::Id)
        {
            ChipLogProgress(Zcl, "Color Control attribute ID: " ChipLogFormatMEI " Type: %u Value: %u, length %u",
                            ChipLogValueMEI(attributeId), type, (value != nullptr) ? *value : 0u, size);
            LightMgr().InitiateLightCtrlAction(AppEvent::kEventType_Light, LightingManager::COLOR_ACTION_XY, attributeId, value);
        }
        if (clusterId == ColorControl::Id && attributeId == ColorControl::Attributes::CurrentHue::Id)
        {
            ChipLogProgress(Zcl, "Color Control attribute ID: " ChipLogFormatMEI " Type: %u Value: %u, length %u",
                            ChipLogValueMEI(attributeId), type, (value != nullptr) ? *value : 0u, size);
            LightMgr().InitiateLightCtrlAction(AppEvent::kEventType_Light, LightingManager::COLOR_ACTION_HSV, attributeId, value);
        }
        else if (clusterId == ColorControl::Id && attributeId == ColorControl::Attributes::CurrentSaturation::Id)
        {
            ChipLogProgress(Zcl, "Color Control attribute ID: " ChipLogFormatMEI " Type: %u Value: %u, length %u",
                            ChipLogValueMEI(attributeId), type, (value != nullptr) ? *value : 0u, size);
            LightMgr().InitiateLightCtrlAction(AppEvent::kEventType_Light, LightingManager::COLOR_ACTION_HSV, attributeId, value);
        }
        else if (attributeId == ColorControl::Attributes::ColorTemperatureMireds::Id)
        {
            if (size != sizeof(uint16_t))
            {
                ChipLogError(Zcl, "Wrong length for ColorControl value: %d", size);
                return;
            }
            LightMgr().InitiateLightCtrlAction(AppEvent::kEventType_Light, LightingManager::COLOR_ACTION_CT, attributeId, value);
        }
#endif // (defined(SL_MATTER_RGB_LED_ENABLED) && SL_MATTER_RGB_LED_ENABLED == 1)
    }
    else if (clusterId == 0x0003u)
    {
        ChipLogProgress(Zcl, "Identify attribute ID: " ChipLogFormatMEI " Type: %u Value: %u, length %u",
                        ChipLogValueMEI(attributeId), type, (value != nullptr) ? *value : 0u, size);
        if (attributeId == 0x0000u && value != nullptr && size == sizeof(uint16_t))
        {
            uint16_t identifyTime = 0;
            memcpy(&identifyTime, value, sizeof(uint16_t));
            ChipLogError(Zcl, "[IDENTIFY] IdentifyTime attribute set: %u seconds", static_cast<unsigned>(identifyTime));
            AppTask::GetAppTask().StartIdentify(identifyTime);
        }
    }
#if defined(SL_CATALOG_ZIGBEE_ZCL_FRAMEWORK_CORE_PRESENT)
    MultiProtocolDataModel::WriteMatterAttributeValueToZigbee(endpointId, clusterId, attributeId, value, type);
#endif // SL_CATALOG_ZIGBEE_ZCL_FRAMEWORK_CORE_PRESENT

    if (clusterId == OnOff::Id || clusterId == LevelControl::Id || clusterId == ColorControl::Id)
    {
        MatterSavePowerOnMemorySnapshot();
    }
}

/** @brief OnOff Cluster Init
 *
 * This function is called when a specific cluster is initialized. It gives the
 * application an opportunity to take care of cluster initialization procedures.
 * It is called exactly once for each endpoint where cluster is present.
 *
 * @param endpoint   Ver.: always
 *
 * TODO Issue #3841
 * emberAfOnOffClusterInitCallback happens before the stack initialize the cluster
 * attributes to the default value.
 * The logic here expects something similar to the deprecated Plugins callback
 * emberAfPluginOnOffClusterServerPostInitCallback.
 *
 */
void emberAfOnOffClusterInitCallback(EndpointId endpoint)
{
    // TODO: implement any additional Cluster Server init actions
}

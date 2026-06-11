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
#if defined(_SILICON_LABS_32B_SERIES_2)
#include "em_timer.h"
#else
#include "sl_hal_timer.h"
#endif
// 静态变量缓存当前目标RGB值
static uint8_t g_rgbTargetR = 0;
static uint8_t g_rgbTargetG = 0;
static uint8_t g_rgbTargetB = 0;
static bool g_isColorTempMode = false;
static uint16_t g_colorTempMireds = 370;
static bool g_buttonPresetActive = false;
static bool g_buttonPresetTxn = false;
static uint8_t g_buttonPresetSuppressColorCb = 0;
static uint16_t g_buttonPresetWPermille = 1000;
static uint16_t g_buttonPresetRPermille = 0;
static uint16_t g_buttonPresetGPermille = 0;
static uint16_t g_buttonPresetBPermille = 0;
static uint8_t g_memOnOff = 0;
static uint8_t g_memLevel = 0;
static uint8_t g_levelAtLastOn = APP_LEVEL_MAX;
static uint8_t g_colorSource = 0; // 0: app color, 1: button preset
static uint8_t g_lastNonZeroTargetR = 255;
static uint8_t g_lastNonZeroTargetG = 255;
static uint8_t g_lastNonZeroTargetB = 255;
static uint16_t g_xyPendingX = 0;
static uint16_t g_xyPendingY = 0;
static bool g_xyPendingXDirty = false;
static bool g_xyPendingYDirty = false;
static bool g_offTransitionActive = false;
static bool g_buttonDimmingHwActive = false;
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
    uint16_t startWPermille;
    uint16_t targetR;
    uint16_t targetG;
    uint16_t targetB;
    uint16_t targetWPermille;
};

static RgbwFadeState g_rgbwFade = { false, 0, 50, 0, 0, 0, 0, 0, 0, 0, 0 };
static osTimerId_t g_rgbwFadeTimer = nullptr;
static osTimerId_t g_powMemSaveTimer = nullptr;
static osTimerId_t g_attrFadeCoalesceTimer = nullptr;
static bool g_attrFadePending = false;
static uint16_t g_attrFadeR = 0;
static uint16_t g_attrFadeG = 0;
static uint16_t g_attrFadeB = 0;
static uint16_t g_attrFadeWPermille = 0;
static bool g_powMemSavePending = false;
static constexpr uint32_t kPowMemSaveDebounceMs = 1000u;
static constexpr uint32_t kRgbwFadeStepMs = 20;
static constexpr uint16_t kFadeStepsMin = 6;   // 120ms
static constexpr uint16_t kFadeStepsMax = 50;  // 1000ms
static constexpr uint32_t kFadeRetargetMinMs = 80;
static constexpr uint8_t kFadeRetargetMinDeltaRgb = 2;
static constexpr uint32_t kAttrFadeCoalesceMs = 40u;
static uint32_t g_lastFadeRetargetTick = 0;
static uint16_t g_lastAppliedR = 0;
static uint16_t g_lastAppliedG = 0;
static uint16_t g_lastAppliedB = 0;
static uint16_t g_lastAppliedWPermille = 0;
static bool g_hasLastApplied = false;
static uint32_t g_lastAppliedWhiteCompare = UINT32_MAX;
static sm15135e_pixel_t g_sm15135e_pixel = { 0 };
static bool g_sm15135e_inited = false;

static uint16_t Scale8To16(uint8_t v)
{
    return static_cast<uint16_t>(v) * 257u;
}

static uint16_t NormalizeRgbInputTo16(uint16_t v)
{
    return (v <= 255u) ? Scale8To16(static_cast<uint8_t>(v)) : v;
}

static uint8_t Rgb16To8(uint16_t v16)
{
    return static_cast<uint8_t>((static_cast<uint32_t>(v16) * 255u + 32767u) / 65535u);
}

// 白光 PWM：硬件 timer compare 分辨率 ≈ top+1 档（15kHz 下 top≈1333，约 1334 级）。
// App/Matter 仍用 0~100%，内部统一 0~1000‰，在 ApplyRgbwNow 等边界转换。
static constexpr uint16_t kWhitePermilleMax = 1000u;

static uint16_t WhitePercentToPermille(uint8_t pct)
{
    if (pct > 100u)
    {
        pct = 100u;
    }
    return static_cast<uint16_t>(pct) * 10u;
}

static uint8_t WhitePermilleToPercent(uint16_t permille)
{
    if (permille > kWhitePermilleMax)
    {
        permille = kWhitePermilleMax;
    }
    return static_cast<uint8_t>((permille + 5u) / 10u);
}

static uint32_t GetWhitePwmTop(void)
{
#if defined(_SILICON_LABS_32B_SERIES_2)
    return TIMER_TopGet(sl_pwm_rgb_white.timer);
#else
    return sl_hal_timer_get_top(sl_pwm_rgb_white.timer);
#endif
}

static uint32_t WhitePermilleToCompare(uint16_t permille)
{
    if (permille > kWhitePermilleMax)
    {
        permille = kWhitePermilleMax;
    }

    const uint32_t top = GetWhitePwmTop();
    return (top * static_cast<uint32_t>(permille) + kWhitePermilleMax / 2u) / kWhitePermilleMax;
}

static uint16_t WhiteCompareToPermille(uint32_t compare)
{
    const uint32_t top = GetWhitePwmTop();
    if (top == 0u)
    {
        return 0u;
    }
    if (compare > top)
    {
        compare = top;
    }
    return static_cast<uint16_t>((compare * static_cast<uint32_t>(kWhitePermilleMax) + top / 2u) / top);
}

static void ApplyWhitePwmCompare(uint32_t compare)
{
    const uint32_t top = GetWhitePwmTop();
    if (compare > top)
    {
        compare = top;
    }

    if (g_lastAppliedWhiteCompare == compare)
    {
        return;
    }

    g_lastAppliedWhiteCompare = compare;

#if defined(_SILICON_LABS_32B_SERIES_2)
    TIMER_CompareBufSet(sl_pwm_rgb_white.timer, sl_pwm_rgb_white.channel, compare);
#else
    sl_hal_timer_channel_set_compare_buffer(sl_pwm_rgb_white.timer, sl_pwm_rgb_white.channel, compare);
#endif
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
        *r = g_hasLastApplied ? Rgb16To8(g_lastAppliedR) : 0u;
    }
    if (g != nullptr)
    {
        *g = g_hasLastApplied ? Rgb16To8(g_lastAppliedG) : 0u;
    }
    if (b != nullptr)
    {
        *b = g_hasLastApplied ? Rgb16To8(g_lastAppliedB) : 0u;
    }
    if (wDuty != nullptr)
    {
        *wDuty = g_hasLastApplied ? WhitePermilleToPercent(g_lastAppliedWPermille) : 0u;
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

    if (g_hasLastApplied && g_lastAppliedR == r16 && g_lastAppliedG == g16 && g_lastAppliedB == b16
        && g_lastAppliedWPermille == 0u)
    {
        return;
    }

    Sm15135eEnsureInit();

    uint16_t outR = 0, outG = 0, outB = 0;
    MapLogicalToSm15135eRgb16(r16, g16, b16, &outR, &outG, &outB);
    sm15135e_set_rgbwy(&g_sm15135e_pixel, outR, outG, outB, 0u, 0u);
    if (!sm15135e_transmit_pixel(&g_sm15135e_pixel))
    {
        return;
    }

    g_lastAppliedR = r16;
    g_lastAppliedG = g16;
    g_lastAppliedB = b16;
    g_lastAppliedWPermille = 0u;
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

static void ApplyRgbwOutput(uint16_t r, uint16_t g, uint16_t b, uint32_t wCompare)
{
    const uint16_t r16       = NormalizeRgbInputTo16(r);
    const uint16_t g16       = NormalizeRgbInputTo16(g);
    const uint16_t b16       = NormalizeRgbInputTo16(b);
    const uint16_t wPermille = WhiteCompareToPermille(wCompare);

    const bool rgbChanged =
        !g_hasLastApplied || g_lastAppliedR != r16 || g_lastAppliedG != g16 || g_lastAppliedB != b16;
    const bool wChanged = !g_hasLastApplied || g_lastAppliedWhiteCompare != wCompare;

    if (!rgbChanged && !wChanged)
    {
        return;
    }

    // [FLK] Discontinuity detector: at the single hardware output funnel, flag any
    // applied value that jumps more than a smooth fade step would. A clean fade moves
    // <~13/255 per RGB channel per 20ms step; a large jump here means a competing
    // writer/retarget or a corrupted frame (the visible flicker fingerprint).
    if (g_hasLastApplied)
    {
        const uint8_t prevR8 = Rgb16To8(g_lastAppliedR);
        const uint8_t prevG8 = Rgb16To8(g_lastAppliedG);
        const uint8_t prevB8 = Rgb16To8(g_lastAppliedB);
        const uint8_t newR8  = Rgb16To8(r16);
        const uint8_t newG8  = Rgb16To8(g16);
        const uint8_t newB8  = Rgb16To8(b16);
        const int dR = static_cast<int>(newR8) - static_cast<int>(prevR8);
        const int dG = static_cast<int>(newG8) - static_cast<int>(prevG8);
        const int dB = static_cast<int>(newB8) - static_cast<int>(prevB8);
        const int aR = dR < 0 ? -dR : dR;
        const int aG = dG < 0 ? -dG : dG;
        const int aB = dB < 0 ? -dB : dB;
        const int dWp = static_cast<int>(wPermille) - static_cast<int>(g_lastAppliedWPermille);
        const int aWp = dWp < 0 ? -dWp : dWp;
        if (aR > 40 || aG > 40 || aB > 40 || aWp > 150)
        {
            ChipLogError(Zcl, "[FLK] jump rgb %u,%u,%u->%u,%u,%u (d=%d,%d,%d) wpm %u->%u fade=%u step=%u/%u",
                         static_cast<unsigned>(prevR8), static_cast<unsigned>(prevG8), static_cast<unsigned>(prevB8),
                         static_cast<unsigned>(newR8), static_cast<unsigned>(newG8), static_cast<unsigned>(newB8),
                         dR, dG, dB, static_cast<unsigned>(g_lastAppliedWPermille), static_cast<unsigned>(wPermille),
                         static_cast<unsigned>(g_rgbwFade.active ? 1u : 0u),
                         static_cast<unsigned>(g_rgbwFade.step), static_cast<unsigned>(g_rgbwFade.totalSteps));
        }
    }

    if (wChanged)
    {
        ApplyWhitePwmCompare(wCompare);
    }

    if (rgbChanged)
    {
        Sm15135eEnsureInit();

        uint16_t outR = 0, outG = 0, outB = 0;
        MapLogicalToSm15135eRgb16(r16, g16, b16, &outR, &outG, &outB);
        sm15135e_set_rgbwy(&g_sm15135e_pixel, outR, outG, outB, 0u, 0u);
        if (!sm15135e_transmit_pixel(&g_sm15135e_pixel))
        {
            return;
        }
    }

    g_lastAppliedR = r16;
    g_lastAppliedG = g16;
    g_lastAppliedB = b16;
    g_lastAppliedWPermille = wPermille;
    g_hasLastApplied = true;
}

static void ApplyRgbwNowPermille(uint16_t r, uint16_t g, uint16_t b, uint16_t wPermille)
{
    CancelRgbwFade();

    if (wPermille > kWhitePermilleMax)
    {
        wPermille = kWhitePermilleMax;
    }

    ApplyRgbwOutput(r, g, b, WhitePermilleToCompare(wPermille));
}

// App/Matter 边界：wDuty 为 0~100%，内部转为 0~1000‰ 驱动 PWM。
static void ApplyRgbwNow(uint16_t r, uint16_t g, uint16_t b, uint8_t wDuty)
{
    ApplyRgbwNowPermille(r, g, b, WhitePercentToPermille(wDuty > 100u ? 100u : wDuty));
}

extern "C" void MatterApplyRgbwNow(uint8_t r, uint8_t g, uint8_t b, uint8_t wDuty)
{
    ApplyRgbwNow(r, g, b, wDuty);
}

extern "C" void MatterApplyWhiteBreathPermille(uint16_t permille)
{
    ApplyRgbwNowPermille(0u, 0u, 0u, permille > kWhitePermilleMax ? kWhitePermilleMax : permille);
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

static void InterpolateFadeRgb(uint16_t startR, uint16_t startG, uint16_t startB, uint16_t targetR, uint16_t targetG,
                               uint16_t targetB, float t, uint16_t & rOut, uint16_t & gOut, uint16_t & bOut);
static bool ShouldUseLinearRgbFade(uint8_t sr, uint8_t sg, uint8_t sb, uint8_t tr, uint8_t tg, uint8_t tb);
static void InterpolateFadeRgbLinear(uint8_t sr, uint8_t sg, uint8_t sb, uint8_t tr, uint8_t tg, uint8_t tb, float t,
                                     uint16_t & rOut, uint16_t & gOut, uint16_t & bOut);
static void GetFadePositionRgbw(uint16_t * r, uint16_t * g, uint16_t * b, uint16_t * wPermille);
static void StartRgbwFade(uint16_t targetR, uint16_t targetG, uint16_t targetB, uint16_t targetWPermille,
                          uint32_t durationMs);
static void ScheduleAttrRgbwFade(uint16_t targetR, uint16_t targetG, uint16_t targetB, uint16_t targetWPermille,
                                 uint32_t durationMs);

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
        ApplyRgbwOutput(g_rgbwFade.targetR, g_rgbwFade.targetG, g_rgbwFade.targetB,
                        WhitePermilleToCompare(g_rgbwFade.targetWPermille));
        osTimerStop(g_rgbwFadeTimer);
        return;
    }

    g_rgbwFade.step++;
    const uint32_t s = g_rgbwFade.step;
    const uint32_t n = g_rgbwFade.totalSteps;
    const float t    = static_cast<float>(s) / static_cast<float>(n);

    uint16_t r = 0;
    uint16_t g = 0;
    uint16_t b = 0;
    InterpolateFadeRgb(g_rgbwFade.startR, g_rgbwFade.startG, g_rgbwFade.startB, g_rgbwFade.targetR, g_rgbwFade.targetG,
                       g_rgbwFade.targetB, t, r, g, b);
    const uint32_t startCmp = WhitePermilleToCompare(g_rgbwFade.startWPermille);
    const uint32_t targetCmp = WhitePermilleToCompare(g_rgbwFade.targetWPermille);
    const uint32_t wCompare = static_cast<uint32_t>(static_cast<int32_t>(startCmp) +
        ((static_cast<int32_t>(targetCmp) - static_cast<int32_t>(startCmp)) * static_cast<int32_t>(s)) /
            static_cast<int32_t>(n));

    ApplyRgbwOutput(r, g, b, wCompare);
}

static void StartRgbwFade(uint16_t targetR, uint16_t targetG, uint16_t targetB, uint16_t targetWPermille,
                          uint32_t durationMs)
{
    if (targetWPermille > kWhitePermilleMax)
    {
        targetWPermille = kWhitePermilleMax;
    }

    if (g_rgbwFadeTimer == nullptr)
    {
        g_rgbwFadeTimer = osTimerNew(RgbwFadeTimerCallback, osTimerPeriodic, nullptr, nullptr);
        if (g_rgbwFadeTimer == nullptr)
        {
            ApplyRgbwOutput(targetR, targetG, targetB, WhitePermilleToCompare(targetWPermille));
            return;
        }
    }

    uint16_t curR = 0;
    uint16_t curG = 0;
    uint16_t curB = 0;
    uint16_t curWPermille = 0;
    GetFadePositionRgbw(&curR, &curG, &curB, &curWPermille);

    const bool fadeToOff =
        (targetR == 0u && targetG == 0u && targetB == 0u && targetWPermille == 0u);
    // RGB / preset-color off: any residual white PWM during the fade reads as a white flash.
    // CT and W-bearing presets need W to dim out together with RGB.
    if (fadeToOff && !g_isColorTempMode &&
        (!g_buttonPresetActive || g_buttonPresetWPermille == 0u))
    {
        ApplyWhitePwmCompare(0u);
        curWPermille             = 0u;
        g_lastAppliedWPermille   = 0u;
        g_lastAppliedWhiteCompare = 0u;
    }

    const uint16_t dR = (curR > targetR) ? static_cast<uint16_t>(curR - targetR) : static_cast<uint16_t>(targetR - curR);
    const uint16_t dG = (curG > targetG) ? static_cast<uint16_t>(curG - targetG) : static_cast<uint16_t>(targetG - curG);
    const uint16_t dB = (curB > targetB) ? static_cast<uint16_t>(curB - targetB) : static_cast<uint16_t>(targetB - curB);
    const uint32_t curCmp = WhitePermilleToCompare(curWPermille);
    const uint32_t targetCmp = WhitePermilleToCompare(targetWPermille);
    const uint32_t dWcmp = (curCmp > targetCmp) ? (curCmp - targetCmp) : (targetCmp - curCmp);

    if (dR <= kFadeRetargetMinDeltaRgb && dG <= kFadeRetargetMinDeltaRgb && dB <= kFadeRetargetMinDeltaRgb &&
        dWcmp <= 1u)
    {
        ApplyRgbwOutput(targetR, targetG, targetB, targetCmp);
        g_rgbwFade.active = false;
        osTimerStop(g_rgbwFadeTimer);
        return;
    }

    if (durationMs == 0u)
    {
        durationMs = APP_COLOR_FADE_MS;
    }

    const uint32_t nowTick = osKernelGetTickCount();
    if (g_rgbwFade.active && ((nowTick - g_lastFadeRetargetTick) < kFadeRetargetMinMs))
    {
        g_rgbwFade.targetR         = targetR;
        g_rgbwFade.targetG         = targetG;
        g_rgbwFade.targetB         = targetB;
        g_rgbwFade.targetWPermille = targetWPermille;
        return;
    }

    g_rgbwFade.active           = true;
    g_rgbwFade.step             = 0;
    g_rgbwFade.startR           = curR;
    g_rgbwFade.startG           = curG;
    g_rgbwFade.startB           = curB;
    g_rgbwFade.startWPermille   = curWPermille;
    g_rgbwFade.targetR          = targetR;
    g_rgbwFade.targetG          = targetG;
    g_rgbwFade.targetB          = targetB;
    g_rgbwFade.targetWPermille  = targetWPermille;
    g_rgbwFade.totalSteps       = ComputeFadeSteps(durationMs);
    g_lastFadeRetargetTick      = nowTick;

    osTimerStart(g_rgbwFadeTimer, kRgbwFadeStepMs);
}

static void AttrFadeCoalesceTimerCallback(void * context)
{
    (void) context;
    if (!g_attrFadePending)
    {
        return;
    }

    g_attrFadePending = false;
    StartRgbwFade(g_attrFadeR, g_attrFadeG, g_attrFadeB, g_attrFadeWPermille, APP_COLOR_FADE_MS);
}

static void ScheduleAttrRgbwFade(uint16_t targetR, uint16_t targetG, uint16_t targetB, uint16_t targetWPermille,
                                 uint32_t durationMs)
{
    if (targetWPermille > kWhitePermilleMax)
    {
        targetWPermille = kWhitePermilleMax;
    }

    g_attrFadeR         = targetR;
    g_attrFadeG         = targetG;
    g_attrFadeB         = targetB;
    g_attrFadeWPermille = targetWPermille;
    g_attrFadePending   = true;

    if (g_attrFadeCoalesceTimer == nullptr)
    {
        g_attrFadeCoalesceTimer = osTimerNew(AttrFadeCoalesceTimerCallback, osTimerOnce, nullptr, nullptr);
        if (g_attrFadeCoalesceTimer == nullptr)
        {
            g_attrFadePending = false;
            StartRgbwFade(targetR, targetG, targetB, targetWPermille, durationMs);
            return;
        }
    }

    (void) osTimerStop(g_attrFadeCoalesceTimer);
    (void) osTimerStart(g_attrFadeCoalesceTimer, kAttrFadeCoalesceMs);
}

static uint8_t Level254To255(uint8_t level254)
{
    return static_cast<uint8_t>((static_cast<uint16_t>(level254) * 255u) / 254u);
}

static void colorTempToRGB(uint16_t kelvin, uint8_t * r, uint8_t * g, uint8_t * b);
static uint8_t CtMiredToWhiteDutyPercent(uint16_t mireds);
static void ComputeCtRgbw(uint16_t mireds, uint8_t level254, uint8_t & rOut, uint8_t & gOut, uint8_t & bOut,
                          uint16_t & wPermilleOut);

static uint16_t Level254ToPermille(uint8_t level254)
{
    return static_cast<uint16_t>((static_cast<uint32_t>(level254) * kWhitePermilleMax + 127u) / 254u);
}

static uint16_t LevelQ16ToPermille(int32_t levelQ16)
{
    if (levelQ16 < 0)
    {
        levelQ16 = 0;
    }
    const int32_t levelQ16Max = static_cast<int32_t>(254) << 16;
    if (levelQ16 > levelQ16Max)
    {
        levelQ16 = levelQ16Max;
    }
    return static_cast<uint16_t>(((static_cast<int64_t>(levelQ16) * kWhitePermilleMax) + (254 << 15)) / (254 << 16));
}

static uint8_t LevelQ16To255(int32_t levelQ16)
{
    if (levelQ16 < 0)
    {
        levelQ16 = 0;
    }
    const int32_t levelQ16Max = static_cast<int32_t>(254) << 16;
    if (levelQ16 > levelQ16Max)
    {
        levelQ16 = levelQ16Max;
    }
    return static_cast<uint8_t>(((static_cast<int64_t>(levelQ16) * 255) + (254 << 15)) / (254 << 16));
}

static void ApplyOutputFromLevelQ16(int32_t levelQ16)
{
    // Long-press dimming drives the hardware directly at full 16-bit precision (no 8-bit
    // stair-steps). It must run at a high refresh rate: at ~33fps (30ms ticks) the per-frame
    // refresh is visible as flicker; the dimming tick period is therefore kept at 20ms (50fps)
    // in AppConfig.h, matching the color fade timer which is already flicker-free.
    CancelRgbwFade();

    if (g_buttonPresetActive)
    {
        const uint16_t levelPermille = LevelQ16ToPermille(levelQ16);
        const uint8_t level255 = LevelQ16To255(levelQ16);

        const uint32_t r = (static_cast<uint32_t>(g_buttonPresetRPermille) * static_cast<uint32_t>(level255)) / 1000u;
        const uint32_t g = (static_cast<uint32_t>(g_buttonPresetGPermille) * static_cast<uint32_t>(level255)) / 1000u;
        const uint32_t b = (static_cast<uint32_t>(g_buttonPresetBPermille) * static_cast<uint32_t>(level255)) / 1000u;
        const uint32_t wPermille =
            (static_cast<uint32_t>(g_buttonPresetWPermille) * static_cast<uint32_t>(levelPermille) + 500u) / 1000u;

        ApplyRgbwNowPermille(static_cast<uint16_t>(r > 255u ? 255u : r),
                             static_cast<uint16_t>(g > 255u ? 255u : g),
                             static_cast<uint16_t>(b > 255u ? 255u : b),
                             static_cast<uint16_t>(wPermille > kWhitePermilleMax ? kWhitePermilleMax : wPermille));
        return;
    }

    if (g_isColorTempMode)
    {
        uint16_t mireds = g_colorTempMireds;
        uint8_t rOut = 0;
        uint8_t gOut = 0;
        uint8_t bOut = 0;
        uint16_t wPermille = 0;
        const uint8_t level254 = static_cast<uint8_t>(levelQ16 >> 16);
        ComputeCtRgbw(mireds, level254, rOut, gOut, bOut, wPermille);
        ApplyRgbwNowPermille(rOut, gOut, bOut, wPermille);
        return;
    }

    const uint16_t levelPermille = LevelQ16ToPermille(levelQ16);
    const uint32_t scale = (static_cast<uint32_t>(levelPermille) << 16) / kWhitePermilleMax;
    const uint16_t r16 = static_cast<uint16_t>((static_cast<uint32_t>(Scale8To16(g_rgbTargetR)) * scale + 32768u) >> 16);
    const uint16_t g16 = static_cast<uint16_t>((static_cast<uint32_t>(Scale8To16(g_rgbTargetG)) * scale + 32768u) >> 16);
    const uint16_t b16 = static_cast<uint16_t>((static_cast<uint32_t>(Scale8To16(g_rgbTargetB)) * scale + 32768u) >> 16);
    ApplyRgbwOutput(r16, g16, b16, 0u);
}

extern "C" void MatterSetButtonDimmingActive(uint8_t active)
{
    g_buttonDimmingHwActive = (active != 0u);
}

extern "C" void MatterApplyButtonDimmingQ16(int32_t levelQ16, uint8_t levelMin, uint8_t levelMax)
{
    const int32_t levelQ16Min = static_cast<int32_t>(levelMin) << 16;
    const int32_t levelQ16Max = static_cast<int32_t>(levelMax) << 16;

    if (levelQ16 < levelQ16Min)
    {
        levelQ16 = levelQ16Min;
    }
    if (levelQ16 > levelQ16Max)
    {
        levelQ16 = levelQ16Max;
    }

    ApplyOutputFromLevelQ16(levelQ16);
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

static uint8_t ClampU8FromU16(uint16_t v)
{
    return static_cast<uint8_t>(v > 255u ? 255u : v);
}

static void RgbToHsv(uint8_t r, uint8_t g, uint8_t b, float & h, float & s, float & v)
{
    const float rf   = static_cast<float>(r) / 255.0f;
    const float gf   = static_cast<float>(g) / 255.0f;
    const float bf   = static_cast<float>(b) / 255.0f;
    const float maxc = fmaxf(rf, fmaxf(gf, bf));
    const float minc = fminf(rf, fminf(gf, bf));
    const float delta = maxc - minc;

    v = maxc;
    if (maxc <= 0.0001f)
    {
        h = 0.0f;
        s = 0.0f;
        return;
    }

    s = delta / maxc;
    if (delta <= 0.0001f)
    {
        h = 0.0f;
        return;
    }

    if (maxc == rf)
    {
        h = 60.0f * fmodf((gf - bf) / delta + 6.0f, 6.0f);
    }
    else if (maxc == gf)
    {
        h = 60.0f * (((bf - rf) / delta) + 2.0f);
    }
    else
    {
        h = 60.0f * (((rf - gf) / delta) + 4.0f);
    }
}

static void HsvToRgb(float h, float s, float v, uint8_t & r, uint8_t & g, uint8_t & b)
{
    if (s <= 0.0001f)
    {
        const uint8_t c = static_cast<uint8_t>(Clamp01(v) * 255.0f + 0.5f);
        r               = c;
        g               = c;
        b               = c;
        return;
    }

    h = fmodf(h, 360.0f);
    if (h < 0.0f)
    {
        h += 360.0f;
    }

    const float hf = h / 60.0f;
    const int i    = static_cast<int>(hf);
    const float f  = hf - static_cast<float>(i);
    const float p  = v * (1.0f - s);
    const float q  = v * (1.0f - s * f);
    const float t  = v * (1.0f - s * (1.0f - f));

    float rf = 0.0f;
    float gf = 0.0f;
    float bf = 0.0f;
    switch (i % 6)
    {
    case 0:
        rf = v;
        gf = t;
        bf = p;
        break;
    case 1:
        rf = q;
        gf = v;
        bf = p;
        break;
    case 2:
        rf = p;
        gf = v;
        bf = t;
        break;
    case 3:
        rf = p;
        gf = q;
        bf = v;
        break;
    case 4:
        rf = t;
        gf = p;
        bf = v;
        break;
    default:
        rf = v;
        gf = p;
        bf = q;
        break;
    }

    r = static_cast<uint8_t>(Clamp01(rf) * 255.0f + 0.5f);
    g = static_cast<uint8_t>(Clamp01(gf) * 255.0f + 0.5f);
    b = static_cast<uint8_t>(Clamp01(bf) * 255.0f + 0.5f);
}

static float LerpHueShortest(float h0, float h1, float t)
{
    float d = h1 - h0;
    if (d > 180.0f)
    {
        d -= 360.0f;
    }
    if (d < -180.0f)
    {
        d += 360.0f;
    }

    float h = h0 + d * t;
    if (h < 0.0f)
    {
        h += 360.0f;
    }
    if (h >= 360.0f)
    {
        h -= 360.0f;
    }
    return h;
}

static bool ShouldUseLinearRgbFade(uint8_t sr, uint8_t sg, uint8_t sb, uint8_t tr, uint8_t tg, uint8_t tb)
{
    // Fade to off: keep hue by scaling RGB toward zero (HSV path desaturates toward white/gray).
    if (tr == 0u && tg == 0u && tb == 0u)
    {
        return true;
    }

    if ((sr | sg | sb) == 0u)
    {
        return true;
    }

    // Level-only change: target is a scalar multiple of start (same chromaticity).
    const uint16_t maxS = static_cast<uint16_t>(sr > sg ? (sr > sb ? sr : sb) : (sg > sb ? sg : sb));
    const uint16_t maxT = static_cast<uint16_t>(tr > tg ? (tr > tb ? tr : tb) : (tg > tb ? tg : tb));
    if (maxS == 0u || maxT == 0u)
    {
        return maxS == maxT;
    }

    const uint32_t tol = static_cast<uint32_t>(maxS) + 1u;
    return (static_cast<uint32_t>(sr) * maxT <= static_cast<uint32_t>(tr) * maxS + tol) &&
           (static_cast<uint32_t>(sr) * maxT + tol >= static_cast<uint32_t>(tr) * maxS) &&
           (static_cast<uint32_t>(sg) * maxT <= static_cast<uint32_t>(tg) * maxS + tol) &&
           (static_cast<uint32_t>(sg) * maxT + tol >= static_cast<uint32_t>(tg) * maxS) &&
           (static_cast<uint32_t>(sb) * maxT <= static_cast<uint32_t>(tb) * maxS + tol) &&
           (static_cast<uint32_t>(sb) * maxT + tol >= static_cast<uint32_t>(tb) * maxS);
}

static void InterpolateFadeRgbLinear(uint8_t sr, uint8_t sg, uint8_t sb, uint8_t tr, uint8_t tg, uint8_t tb, float t,
                                     uint16_t & rOut, uint16_t & gOut, uint16_t & bOut)
{
    const float k0 = 1.0f - t;
    const float k1 = t;
    rOut           = static_cast<uint16_t>(static_cast<float>(sr) * k0 + static_cast<float>(tr) * k1 + 0.5f);
    gOut           = static_cast<uint16_t>(static_cast<float>(sg) * k0 + static_cast<float>(tg) * k1 + 0.5f);
    bOut           = static_cast<uint16_t>(static_cast<float>(sb) * k0 + static_cast<float>(tb) * k1 + 0.5f);
}

static void InterpolateFadeRgb(uint16_t startR, uint16_t startG, uint16_t startB, uint16_t targetR, uint16_t targetG,
                               uint16_t targetB, float t, uint16_t & rOut, uint16_t & gOut, uint16_t & bOut)
{
    const uint8_t sr = ClampU8FromU16(startR);
    const uint8_t sg = ClampU8FromU16(startG);
    const uint8_t sb = ClampU8FromU16(startB);
    const uint8_t tr = ClampU8FromU16(targetR);
    const uint8_t tg = ClampU8FromU16(targetG);
    const uint8_t tb = ClampU8FromU16(targetB);

    if (ShouldUseLinearRgbFade(sr, sg, sb, tr, tg, tb))
    {
        InterpolateFadeRgbLinear(sr, sg, sb, tr, tg, tb, t, rOut, gOut, bOut);
        return;
    }

    float h0 = 0.0f;
    float s0 = 0.0f;
    float v0 = 0.0f;
    float h1 = 0.0f;
    float s1 = 0.0f;
    float v1 = 0.0f;
    RgbToHsv(sr, sg, sb, h0, s0, v0);
    RgbToHsv(tr, tg, tb, h1, s1, v1);

    if (s0 <= 0.0001f && s1 > 0.0001f)
    {
        h0 = h1;
    }
    else if (s1 <= 0.0001f && s0 > 0.0001f)
    {
        h1 = h0;
    }

    const float h   = LerpHueShortest(h0, h1, t);
    const float sat = s0 + (s1 - s0) * t;
    const float val = v0 + (v1 - v0) * t;

    uint8_t r8 = 0;
    uint8_t g8 = 0;
    uint8_t b8 = 0;
    HsvToRgb(h, sat, val, r8, g8, b8);
    rOut = r8;
    gOut = g8;
    bOut = b8;
}

static void GetFadePositionRgbw(uint16_t * r, uint16_t * g, uint16_t * b, uint16_t * wPermille)
{
    if (g_rgbwFade.active && g_rgbwFade.totalSteps > 0u)
    {
        const float t = static_cast<float>(g_rgbwFade.step) / static_cast<float>(g_rgbwFade.totalSteps);
        uint16_t rOut = 0;
        uint16_t gOut = 0;
        uint16_t bOut = 0;
        InterpolateFadeRgb(g_rgbwFade.startR, g_rgbwFade.startG, g_rgbwFade.startB, g_rgbwFade.targetR,
                           g_rgbwFade.targetG, g_rgbwFade.targetB, t, rOut, gOut, bOut);
        if (r != nullptr)
        {
            *r = rOut;
        }
        if (g != nullptr)
        {
            *g = gOut;
        }
        if (b != nullptr)
        {
            *b = bOut;
        }
        if (wPermille != nullptr)
        {
            const uint32_t startCmp  = WhitePermilleToCompare(g_rgbwFade.startWPermille);
            const uint32_t targetCmp = WhitePermilleToCompare(g_rgbwFade.targetWPermille);
            const uint32_t cmp       = static_cast<uint32_t>(static_cast<int32_t>(startCmp) +
                ((static_cast<int32_t>(targetCmp) - static_cast<int32_t>(startCmp)) *
                 static_cast<int32_t>(g_rgbwFade.step)) /
                    static_cast<int32_t>(g_rgbwFade.totalSteps));
            *wPermille = WhiteCompareToPermille(cmp);
        }
        return;
    }

    GetCurrentRgbw(r, g, b, nullptr);
    if (wPermille != nullptr)
    {
        *wPermille = g_hasLastApplied ? g_lastAppliedWPermille : 0u;
    }
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

static uint8_t CtMiredToWhiteDutyPercent(uint16_t mireds)
{
    constexpr uint8_t kCtWarmWhitePct             = 70u;
    constexpr uint8_t kCtExtraWarmWhitePct        = 40u;
    constexpr uint16_t kCtWarmReferenceMired      = 370u;
    constexpr uint16_t kCtExtraWarmReferenceMired = 455u;
    constexpr uint16_t kCtCoolReferenceMired      = 153u;

    if (mireds >= kCtExtraWarmReferenceMired)
    {
        return kCtExtraWarmWhitePct;
    }
    if (mireds >= kCtWarmReferenceMired)
    {
        const uint16_t span  = kCtExtraWarmReferenceMired - kCtWarmReferenceMired;
        const uint16_t offset = mireds - kCtWarmReferenceMired;
        const uint16_t drop  = kCtWarmWhitePct - kCtExtraWarmWhitePct;
        return static_cast<uint8_t>(kCtWarmWhitePct -
                                    (static_cast<uint32_t>(offset) * drop + span / 2u) / span);
    }
    if (mireds <= kCtCoolReferenceMired)
    {
        return 0u;
    }

    const uint16_t span  = kCtWarmReferenceMired - kCtCoolReferenceMired;
    const uint16_t offset = mireds - kCtCoolReferenceMired;
    return static_cast<uint8_t>((static_cast<uint32_t>(offset) * kCtWarmWhitePct + span / 2u) / span);
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

#include <access/SubjectDescriptor.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/ConcreteAttributePath.h>
#include <app/reporting/reporting.h>
#include <app/util/MatterCallbacks.h>
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
extern "C" {
#include "app/framework/include/af.h"
}
#endif // SL_CATALOG_ZIGBEE_ZCL_FRAMEWORK_CORE_PRESENT

using namespace ::chip;
using namespace ::chip::app::Clusters;

#if defined(SL_CATALOG_ZIGBEE_ZCL_FRAMEWORK_CORE_PRESENT)
static void SyncLevel254ToZigbee(uint8_t level254)
{
    (void) sl_zigbee_af_write_server_attribute_without_sync(1, ZCL_LEVEL_CONTROL_CLUSTER_ID,
                                                            ZCL_CURRENT_LEVEL_ATTRIBUTE_ID, &level254,
                                                            ZCL_INT8U_ATTRIBUTE_TYPE);
}
#endif

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

static void MatterSyncPowerOnMemoryAttributes()
{
    if (!g_hasRestoredPowerOnState)
    {
        return;
    }

    const bool prevSuppress = g_suppressAttributeHwOutput;
    g_suppressAttributeHwOutput = true;

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    (void) LevelControl::Attributes::CurrentLevel::Set(1, g_restoredPowerOnState.level);
    (void) OnOff::Attributes::OnOff::Set(1, g_restoredPowerOnState.onOff != 0);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    g_memLevel = g_restoredPowerOnState.level;
    g_memOnOff = g_restoredPowerOnState.onOff;
    g_levelAtLastOn = g_restoredPowerOnState.level;
    g_suppressAttributeHwOutput = prevSuppress;

    ChipLogError(Zcl, "[DIM] power-on attributes synced: on=%u level=%u",
                 static_cast<unsigned>(g_restoredPowerOnState.onOff),
                 static_cast<unsigned>(g_restoredPowerOnState.level));
}

extern "C" void MatterSyncPowerOnAttributesFromMemory(void)
{
    MatterSyncPowerOnMemoryAttributes();
}

extern "C" uint8_t MatterGetIsColorTempMode(void)
{
    return g_isColorTempMode ? 1u : 0u;
}

extern "C" uint16_t MatterGetRuntimeColorTempMireds(void)
{
    return g_colorTempMireds;
}
}

extern "C" void MatterSavePowerOnMemorySnapshot();

extern "C" void MatterFinalizeButtonDimming(int32_t levelQ16, uint8_t levelMin, uint8_t levelMax)
{
    const int32_t levelQ16Min = static_cast<int32_t>(levelMin) << 16;
    const int32_t levelQ16Max = static_cast<int32_t>(levelMax) << 16;

    if (levelQ16 < levelQ16Min)
    {
        levelQ16 = levelQ16Min;
    }
    if (levelQ16 > levelQ16Max)
    {
        levelQ16 = levelQ16Max;
    }

    const uint8_t finalLevel = static_cast<uint8_t>(levelQ16 >> 16);
    MatterApplyButtonDimmingQ16(levelQ16, levelMin, levelMax);

    if (finalLevel >= APP_BUTTON_LEVEL_MIN)
    {
        g_levelAtLastOn = finalLevel;
    }
    g_memLevel = finalLevel;

    const bool prevSuppress = g_suppressAttributeHwOutput;
    g_suppressAttributeHwOutput = true;
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    app::DataModel::Nullable<uint8_t> level;
    if (LevelControl::Attributes::CurrentLevel::Get(1, level) == chip::Protocols::InteractionModel::Status::Success &&
        (level.IsNull() || level.Value() != finalLevel))
    {
        LevelControl::Attributes::CurrentLevel::Set(1, finalLevel);
        MatterReportingAttributeChangeCallback(1, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    }
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    g_suppressAttributeHwOutput = prevSuppress;
#if defined(SL_CATALOG_ZIGBEE_ZCL_FRAMEWORK_CORE_PRESENT)
    SyncLevel254ToZigbee(finalLevel);
#endif
    MatterSavePowerOnMemorySnapshot();
}

extern "C" uint16_t MatterLevelQ16ToPermille(int32_t levelQ16)
{
    return LevelQ16ToPermille(levelQ16);
}

extern "C" uint8_t MatterGetLevelAtLastOn()
{
    return g_levelAtLastOn;
}

extern "C" void MatterSnapshotLevelForOff()
{
    uint8_t level254 = g_memLevel;
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    app::DataModel::Nullable<uint8_t> level;
    if (LevelControl::Attributes::CurrentLevel::Get(1, level) == chip::Protocols::InteractionModel::Status::Success &&
        !level.IsNull())
    {
        level254 = level.Value();
    }
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    if (level254 >= APP_BUTTON_LEVEL_MIN)
    {
        g_levelAtLastOn = level254;
        g_memLevel = level254;
    }
    MatterSavePowerOnMemorySnapshot();
}

extern "C" void MatterSyncLevelBeforeOn()
{
    if (g_levelAtLastOn < APP_BUTTON_LEVEL_MIN)
    {
        return;
    }

    const bool prevSuppress = g_suppressAttributeHwOutput;
    g_suppressAttributeHwOutput = true;
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    LevelControl::Attributes::CurrentLevel::Set(1, g_levelAtLastOn);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    g_suppressAttributeHwOutput = prevSuppress;
    g_memLevel = g_levelAtLastOn;
#if defined(SL_CATALOG_ZIGBEE_ZCL_FRAMEWORK_CORE_PRESENT)
    SyncLevel254ToZigbee(g_levelAtLastOn);
#endif
}

extern "C" bool MatterRestorePowerOnMemoryIfAny()
{
    PowerOnMemoryState state = {};
    CHIP_ERROR err = chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr().Get(kPowerOnMemoryKey, &state, sizeof(state));
    if (err != CHIP_NO_ERROR || state.version != kPowerOnMemoryVersion)
    {
        return false;
    }

    g_restoredPowerOnState = state;
    g_hasRestoredPowerOnState = true;
    g_memOnOff = state.onOff;
    g_memLevel = state.level;
    g_levelAtLastOn = state.level;
    g_colorSource = (state.colorSource == 1u) ? 1u : 0u;

    // 先保持灭灯；配网完成后由 MatterReapplyPowerOnMemoryOutput() 做 400ms 渐亮恢复。
    ApplyRgbwNow(0, 0, 0, 0);

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

    MatterSyncPowerOnMemoryAttributes();

    ChipLogError(Zcl, "[DIM] power-on restore applied: on=%u level=%u rgb=%u,%u,%u w=%u",
                 static_cast<unsigned>(state.onOff), static_cast<unsigned>(state.level),
                 static_cast<unsigned>(state.r), static_cast<unsigned>(state.g), static_cast<unsigned>(state.b),
                 static_cast<unsigned>(state.wDuty));
    return true;
}

static void ComputePresetRgbw(uint8_t level254, uint16_t & r, uint16_t & g, uint16_t & b, uint16_t & wPermilleOut);
static void ComputeCtRgbw(uint16_t mireds, uint8_t level254, uint8_t & rOut, uint8_t & gOut, uint8_t & bOut,
                          uint16_t & wPermilleOut);

static uint8_t ResolvePersistLevel254(uint8_t attrLevel, uint8_t onOff)
{
    uint8_t level254 = attrLevel;

    if (g_levelAtLastOn >= APP_BUTTON_LEVEL_MIN)
    {
        if (onOff == 0u)
        {
            level254 = g_levelAtLastOn;
        }
        else if (level254 < APP_BUTTON_LEVEL_MIN)
        {
            // OnOff 联动可能把 Matter 属性写成 MinLevel(1)，持久化时用真实用户亮度。
            level254 = g_levelAtLastOn;
        }
    }
    else if (level254 < APP_BUTTON_LEVEL_MIN && g_memLevel >= APP_BUTTON_LEVEL_MIN)
    {
        level254 = g_memLevel;
    }

    return level254;
}

static void FillPowerOnRgbwFromLevel254(uint8_t level254, uint8_t & rOut, uint8_t & gOut, uint8_t & bOut, uint8_t & wDutyOut)
{
    if (g_buttonPresetActive)
    {
        uint16_t r16 = 0;
        uint16_t g16 = 0;
        uint16_t b16 = 0;
        uint16_t wPermille = 0;
        ComputePresetRgbw(level254, r16, g16, b16, wPermille);
        rOut = static_cast<uint8_t>(r16 > 255u ? 255u : r16);
        gOut = static_cast<uint8_t>(g16 > 255u ? 255u : g16);
        bOut = static_cast<uint8_t>(b16 > 255u ? 255u : b16);
        wDutyOut = WhitePermilleToPercent(wPermille);
    }
    else if (g_isColorTempMode)
    {
        uint16_t mireds    = g_colorTempMireds;
        uint8_t r            = 0;
        uint8_t g            = 0;
        uint8_t b            = 0;
        uint16_t wPermille   = 0;
        ComputeCtRgbw(mireds, level254, r, g, b, wPermille);
        rOut = r;
        gOut = g;
        bOut = b;
        wDutyOut = WhitePermilleToPercent(wPermille);
    }
    else
    {
        const uint8_t level255 = Level254To255(level254);
        rOut = static_cast<uint8_t>((static_cast<uint16_t>(g_rgbTargetR) * level255) / 255u);
        gOut = static_cast<uint8_t>((static_cast<uint16_t>(g_rgbTargetG) * level255) / 255u);
        bOut = static_cast<uint8_t>((static_cast<uint16_t>(g_rgbTargetB) * level255) / 255u);
        wDutyOut = 0u;
    }
}

static void WritePowerOnMemorySnapshot(void)
{
    PowerOnMemoryState state = {};
    state.version            = kPowerOnMemoryVersion;

    chip::DeviceLayer::PlatformMgr().LockChipStack();
    app::DataModel::Nullable<uint8_t> level;
    if (LevelControl::Attributes::CurrentLevel::Get(1, level) == chip::Protocols::InteractionModel::Status::Success &&
        !level.IsNull())
    {
        state.level = level.Value();
        g_memLevel  = level.Value();
    }
    else
    {
        state.level = g_memLevel;
    }

    bool onOff = false;
    if (OnOff::Attributes::OnOff::Get(1, &onOff) == chip::Protocols::InteractionModel::Status::Success)
    {
        state.onOff = onOff ? 1u : 0u;
        g_memOnOff  = state.onOff;
    }
    else
    {
        state.onOff = g_memOnOff;
    }

    state.level = ResolvePersistLevel254(state.level, state.onOff);
    g_memLevel  = state.level;

    if (state.onOff == 0u)
    {
        state.r         = 0u;
        state.g         = 0u;
        state.b         = 0u;
        state.wDuty     = 0u;
        state.colorSource = g_colorSource;
    }
    else
    {
        FillPowerOnRgbwFromLevel254(state.level, state.r, state.g, state.b, state.wDuty);
        state.colorSource = g_colorSource;
    }

    chip::DeviceLayer::PlatformMgr().UnlockChipStack();

    CHIP_ERROR err = chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr().Put(kPowerOnMemoryKey, &state, sizeof(state));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(Zcl, "[DIM] failed to save power-on memory: on=%u level=%u err=%" CHIP_ERROR_FORMAT,
                     static_cast<unsigned>(state.onOff), static_cast<unsigned>(state.level), err.Format());
    }
    else
    {
        ChipLogError(Zcl, "[DIM] power-on memory saved: on=%u level=%u w=%u preset=%u ct=%u",
                     static_cast<unsigned>(state.onOff), static_cast<unsigned>(state.level),
                     static_cast<unsigned>(state.wDuty), static_cast<unsigned>(g_buttonPresetActive ? 1u : 0u),
                     static_cast<unsigned>(g_isColorTempMode ? 1u : 0u));
    }
}

static void PowMemSaveWorkHandler(intptr_t arg)
{
    (void) arg;
    WritePowerOnMemorySnapshot();
}

static void PowMemSaveTimerCallback(void * context)
{
    (void) context;
    if (!g_powMemSavePending)
    {
        return;
    }

    g_powMemSavePending = false;
    (void) chip::DeviceLayer::PlatformMgr().ScheduleWork(PowMemSaveWorkHandler, 0);
}

extern "C" void MatterSavePowerOnMemorySnapshot()
{
    g_powMemSavePending = true;

    if (g_powMemSaveTimer == nullptr)
    {
        g_powMemSaveTimer = osTimerNew(PowMemSaveTimerCallback, osTimerOnce, nullptr, nullptr);
    }

    if (g_powMemSaveTimer != nullptr)
    {
        (void) osTimerStop(g_powMemSaveTimer);
        (void) osTimerStart(g_powMemSaveTimer, kPowMemSaveDebounceMs);
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

    MatterSyncPowerOnMemoryAttributes();

    const uint8_t level254 = ResolvePersistLevel254(g_restoredPowerOnState.level, g_restoredPowerOnState.onOff);
    g_restoredPowerOnState.level = level254;
    g_levelAtLastOn = level254;
    g_memLevel = level254;

    if (g_restoredPowerOnState.onOff == 0u)
    {
        ApplyRgbwNow(0, 0, 0, 0);
    }
    else
    {
        uint8_t rOut = 0;
        uint8_t gOut = 0;
        uint8_t bOut = 0;
        uint8_t wDuty = 0;
        FillPowerOnRgbwFromLevel254(level254, rOut, gOut, bOut, wDuty);
        g_restoredPowerOnState.r = rOut;
        g_restoredPowerOnState.g = gOut;
        g_restoredPowerOnState.b = bOut;
        g_restoredPowerOnState.wDuty = wDuty;
        ApplyRgbwNow(0, 0, 0, 0);
        StartRgbwFade(rOut, gOut, bOut, WhitePercentToPermille(wDuty), APP_ONOFF_FADE_MS);
    }
    ChipLogError(Zcl, "[DIM] power-on output re-applied: on=%u level=%u rgb=%u,%u,%u w=%u fade=%u",
                 static_cast<unsigned>(g_restoredPowerOnState.onOff), static_cast<unsigned>(level254),
                 static_cast<unsigned>(g_restoredPowerOnState.r),
                 static_cast<unsigned>(g_restoredPowerOnState.g),
                 static_cast<unsigned>(g_restoredPowerOnState.b),
                 static_cast<unsigned>(g_restoredPowerOnState.wDuty),
                 static_cast<unsigned>(g_restoredPowerOnState.onOff != 0u ? APP_ONOFF_FADE_MS : 0u));
}

static void ApplyButtonPresetByLevel(uint8_t level254)
{
    const uint8_t level255 = Level254To255(level254);
    const uint16_t levelPermille = Level254ToPermille(level254);

    const uint32_t r = (static_cast<uint32_t>(g_buttonPresetRPermille) * static_cast<uint32_t>(level255)) / 1000u;
    const uint32_t g = (static_cast<uint32_t>(g_buttonPresetGPermille) * static_cast<uint32_t>(level255)) / 1000u;
    const uint32_t b = (static_cast<uint32_t>(g_buttonPresetBPermille) * static_cast<uint32_t>(level255)) / 1000u;
    const uint32_t wPermille =
        (static_cast<uint32_t>(g_buttonPresetWPermille) * static_cast<uint32_t>(levelPermille) + 500u) / 1000u;

    ApplyRgbwNowPermille(static_cast<uint16_t>(r > 255u ? 255u : r),
                         static_cast<uint16_t>(g > 255u ? 255u : g),
                         static_cast<uint16_t>(b > 255u ? 255u : b),
                         static_cast<uint16_t>(wPermille > kWhitePermilleMax ? kWhitePermilleMax : wPermille));
}

static void ComputePresetRgbw(uint8_t level254, uint16_t & r, uint16_t & g, uint16_t & b, uint16_t & wPermilleOut)
{
    const uint8_t level255 = Level254To255(level254);
    const uint16_t levelPermille = Level254ToPermille(level254);

    r = static_cast<uint16_t>((static_cast<uint32_t>(g_buttonPresetRPermille) * static_cast<uint32_t>(level255)) / 1000u);
    g = static_cast<uint16_t>((static_cast<uint32_t>(g_buttonPresetGPermille) * static_cast<uint32_t>(level255)) / 1000u);
    b = static_cast<uint16_t>((static_cast<uint32_t>(g_buttonPresetBPermille) * static_cast<uint32_t>(level255)) / 1000u);

    wPermilleOut = static_cast<uint16_t>(
        ((static_cast<uint32_t>(g_buttonPresetWPermille) * static_cast<uint32_t>(levelPermille) + 500u) / 1000u) >
                kWhitePermilleMax
            ? kWhitePermilleMax
            : ((static_cast<uint32_t>(g_buttonPresetWPermille) * static_cast<uint32_t>(levelPermille) + 500u) / 1000u));
}

static void ComputeCtRgbw(uint16_t mireds, uint8_t level254, uint8_t & rOut, uint8_t & gOut, uint8_t & bOut,
                          uint16_t & wPermilleOut)
{
    uint16_t kelvin = 2700u;
    if (mireds > 0u)
    {
        kelvin = static_cast<uint16_t>(1000000UL / mireds);
    }

    uint8_t rFull = 0;
    uint8_t gFull = 0;
    uint8_t bFull = 0;
    colorTempToRGB(kelvin, &rFull, &gFull, &bFull);

    const uint8_t level255        = Level254To255(level254);
    const uint16_t levelPermille  = Level254ToPermille(level254);
    const uint8_t whitePct        = CtMiredToWhiteDutyPercent(mireds);

    rOut = static_cast<uint8_t>((static_cast<uint16_t>(rFull) * level255) / 255u);
    gOut = static_cast<uint8_t>((static_cast<uint16_t>(gFull) * level255) / 255u);
    bOut = static_cast<uint8_t>((static_cast<uint16_t>(bFull) * level255) / 255u);
    wPermilleOut = static_cast<uint16_t>((static_cast<uint32_t>(whitePct) * levelPermille + 50u) / 100u);
}

extern "C" void MatterComputeCtRgbw(uint16_t mireds, uint8_t level254, uint8_t * rOut, uint8_t * gOut, uint8_t * bOut,
                                     uint8_t * wDutyOut)
{
    if (rOut == nullptr || gOut == nullptr || bOut == nullptr || wDutyOut == nullptr)
    {
        return;
    }

    uint16_t wPermille = 0;
    ComputeCtRgbw(mireds, level254, *rOut, *gOut, *bOut, wPermille);
    *wDutyOut = WhitePermilleToPercent(wPermille);
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

    const uint8_t level255       = Level254To255(level254);
    const uint16_t levelPermille = Level254ToPermille(level254);

    const uint16_t r = static_cast<uint16_t>((static_cast<uint32_t>(rPermille) * static_cast<uint32_t>(level255)) / 1000u);
    const uint16_t g = static_cast<uint16_t>((static_cast<uint32_t>(gPermille) * static_cast<uint32_t>(level255)) / 1000u);
    const uint16_t b = static_cast<uint16_t>((static_cast<uint32_t>(bPermille) * static_cast<uint32_t>(level255)) / 1000u);
    const uint16_t wOutPermille = static_cast<uint16_t>(
        ((static_cast<uint32_t>(wPermille) * static_cast<uint32_t>(levelPermille) + 500u) / 1000u) > kWhitePermilleMax
            ? kWhitePermilleMax
            : ((static_cast<uint32_t>(wPermille) * static_cast<uint32_t>(levelPermille) + 500u) / 1000u));

    g_attrFadePending = false;
    if (g_attrFadeCoalesceTimer != nullptr)
    {
        (void) osTimerStop(g_attrFadeCoalesceTimer);
    }
    StartRgbwFade(r, g, b, wOutPermille, APP_COLOR_FADE_MS);
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

    StartRgbwFade(r, g, b, WhitePercentToPermille(wDuty > 100u ? 100u : wDuty), APP_COLOR_FADE_MS);
    ChipLogError(Zcl, "[DIM] restore output state src=%u preset=%u rgbw=%u,%u,%u,%u",
                 static_cast<unsigned>(g_colorSource), static_cast<unsigned>(g_buttonPresetActive ? 1u : 0u),
                 static_cast<unsigned>(r), static_cast<unsigned>(g), static_cast<unsigned>(b),
                 static_cast<unsigned>(wDuty));
}

static void RestoreMatterLevel254(uint8_t level254)
{
    const bool prevSuppress = g_suppressAttributeHwOutput;
    g_suppressAttributeHwOutput = true;
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    LevelControl::Attributes::CurrentLevel::Set(1, level254);
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    g_suppressAttributeHwOutput = prevSuppress;
    g_memLevel = level254;
#if defined(SL_CATALOG_ZIGBEE_ZCL_FRAMEWORK_CORE_PRESENT)
    SyncLevel254ToZigbee(level254);
#endif
}

static void ApplyLevel254Hardware(uint8_t level254)
{
    if (g_buttonPresetActive)
    {
        uint16_t r = 0;
        uint16_t g = 0;
        uint16_t b = 0;
        uint16_t wPermille = 0;
        ComputePresetRgbw(level254, r, g, b, wPermille);
        StartRgbwFade(r, g, b, wPermille, APP_COLOR_FADE_MS);
    }
    else if (g_isColorTempMode)
    {
        uint16_t mireds = g_colorTempMireds;
        uint8_t rOut = 0;
        uint8_t gOut = 0;
        uint8_t bOut = 0;
        uint16_t wPermille = 0;
        ComputeCtRgbw(mireds, level254, rOut, gOut, bOut, wPermille);
        ScheduleAttrRgbwFade(rOut, gOut, bOut, wPermille, APP_COLOR_FADE_MS);
    }
    else
    {
        const uint8_t level255 = Level254To255(level254);
        const uint8_t r        = static_cast<uint8_t>((static_cast<uint16_t>(g_rgbTargetR) * level255) / 255u);
        const uint8_t g        = static_cast<uint8_t>((static_cast<uint16_t>(g_rgbTargetG) * level255) / 255u);
        const uint8_t b        = static_cast<uint8_t>((static_cast<uint16_t>(g_rgbTargetB) * level255) / 255u);
        ScheduleAttrRgbwFade(r, g, b, 0, APP_COLOR_FADE_MS);
    }
}

static void StartTurnOnFadeForLevel254(uint8_t level254)
{
    uint8_t rOut = 0;
    uint8_t gOut = 0;
    uint8_t bOut = 0;
    uint8_t wDuty = 0;
    FillPowerOnRgbwFromLevel254(level254, rOut, gOut, bOut, wDuty);
    StartRgbwFade(rOut, gOut, bOut, WhitePercentToPermille(wDuty), APP_ONOFF_FADE_MS);
    ChipLogError(Zcl, "[DIM] OnOff=1 fade: level254=%u permille=%u rgb=%u,%u,%u preset=%u ct=%u",
                 static_cast<unsigned>(level254), static_cast<unsigned>(Level254ToPermille(level254)),
                 static_cast<unsigned>(rOut), static_cast<unsigned>(gOut), static_cast<unsigned>(bOut),
                 static_cast<unsigned>(g_buttonPresetActive ? 1u : 0u),
                 static_cast<unsigned>(g_isColorTempMode ? 1u : 0u));
}

extern "C" void MatterClearButtonPresetLatch(void);

namespace {

class AppDataModelCallbacks : public chip::DataModelCallbacks
{
public:
    CHIP_ERROR PreCommandReceived(const chip::app::ConcreteCommandPath & commandPath,
                                  const chip::Access::SubjectDescriptor & subjectDescriptor) override
    {
        (void) subjectDescriptor;

        if (commandPath.mEndpointId != LIGHT_ENDPOINT || commandPath.mClusterId != ColorControl::Id)
        {
            return CHIP_NO_ERROR;
        }

        g_buttonPresetSuppressColorCb = 0;
        g_buttonPresetTxn               = false;
        g_buttonPresetActive            = false;
        g_colorSource                   = 0u;
        MatterClearButtonPresetLatch();

        ChipLogProgress(Zcl, "[DIM] app ColorControl cmd=" ChipLogFormatMEI, ChipLogValueMEI(commandPath.mCommandId));
        return CHIP_NO_ERROR;
    }
};

AppDataModelCallbacks sAppDataModelCallbacks;

struct AppDataModelCallbacksRegistrar
{
    AppDataModelCallbacksRegistrar() { chip::DataModelCallbacks::SetInstance(&sAppDataModelCallbacks); }
};

AppDataModelCallbacksRegistrar sAppDataModelCallbacksRegistrar;

} // namespace

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
        bool isOn = true;
        (void) OnOff::Attributes::OnOff::Get(1, &isOn);
        if (isOn)
        {
            g_memLevel = *value;
            if (*value >= APP_BUTTON_LEVEL_MIN)
            {
                g_levelAtLastOn = *value;
            }
            else if (*value > 1u || g_levelAtLastOn < APP_BUTTON_LEVEL_MIN)
            {
                // 2..25 为 App 有意设置；level==1 且已有更高记忆时视为 stack 联动，不覆盖。
                g_levelAtLastOn = *value;
            }
        }
        else if (*value >= APP_BUTTON_LEVEL_MIN || g_levelAtLastOn < APP_BUTTON_LEVEL_MIN)
        {
            // Ignore OnOff cluster level effect dropping to device MinLevel(1) on turn-off.
            g_memLevel = *value;
        }
    }

    // During boot, Matter/Zigbee may replay persisted attributes (often CT first) and
    // briefly drive white PWM before power-on memory is applied. Skip hardware here.
    if (g_suppressAttributeHwOutput &&
        (clusterId == OnOff::Id || clusterId == LevelControl::Id || clusterId == ColorControl::Id))
    {
        ChipLogError(Zcl, "[DIM] boot suppress hw for cluster " ChipLogFormatMEI, ChipLogValueMEI(clusterId));
        return;
    }

    // Level Control 在长按调光期间由 AppTask 直接驱动，不在此刷屏
    if (clusterId == LevelControl::Id && g_buttonDimmingHwActive)
    {
        return;
    }

    if (clusterId == OnOff::Id || clusterId == ColorControl::Id)
    {
        ChipLogError(Zcl, "[DIM] Cluster callback: " ChipLogFormatMEI, ChipLogValueMEI(clusterId));
    }
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

        if (attributeId == ColorControl::Attributes::ColorTemperatureMireds::Id ||
            attributeId == ColorControl::Attributes::CurrentHue::Id ||
            attributeId == ColorControl::Attributes::CurrentSaturation::Id ||
            attributeId == ColorControl::Attributes::CurrentX::Id ||
            attributeId == ColorControl::Attributes::CurrentY::Id)
        {
            g_buttonPresetActive = false;
            g_colorSource        = 0u;
            MatterClearButtonPresetLatch();
            if (attributeId == ColorControl::Attributes::ColorTemperatureMireds::Id)
            {
                g_isColorTempMode = true;
            }
            else
            {
                g_isColorTempMode = false;
            }
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

        if (attributeId == ColorControl::Attributes::ColorTemperatureMireds::Id) {
            g_isColorTempMode = true;
            uint16_t mireds = 0;
            memcpy(&mireds, value, sizeof(uint16_t));
            g_colorTempMireds = mireds;
            uint8_t level254 = 254;
            app::DataModel::Nullable<uint8_t> brightness;
            if (chip::app::Clusters::LevelControl::Attributes::CurrentLevel::Get(1, brightness)
                == chip::Protocols::InteractionModel::Status::Success && !brightness.IsNull())
            {
                level254 = brightness.Value();
            }
            uint8_t rOut = 0;
            uint8_t gOut = 0;
            uint8_t bOut = 0;
            uint16_t wPermille = 0;
            ComputeCtRgbw(mireds, level254, rOut, gOut, bOut, wPermille);
            ScheduleAttrRgbwFade(rOut, gOut, bOut, wPermille, APP_COLOR_FADE_MS);
            ChipLogError(Zcl, "[DIM] mode=ColorTemp mireds=%u level254=%u rgbw=%u,%u,%u,%u",
                         static_cast<unsigned>(mireds), static_cast<unsigned>(level254),
                         static_cast<unsigned>(rOut), static_cast<unsigned>(gOut),
                         static_cast<unsigned>(bOut), static_cast<unsigned>(wPermille));
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
            ScheduleAttrRgbwFade(r, g, b, 0, APP_COLOR_FADE_MS);
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
            return;
        }

        if (*value < APP_BUTTON_LEVEL_MIN && g_levelAtLastOn >= APP_BUTTON_LEVEL_MIN && !g_buttonDimmingHwActive)
        {
            // Zigbee/Matter OnOff 联动效应会把 CurrentLevel 压到 MinLevel(1)，在此恢复。
            if (*value != g_levelAtLastOn)
            {
                RestoreMatterLevel254(g_levelAtLastOn);
                ChipLogError(Zcl, "[DIM] reject stack level254=%u, restore=%u",
                             static_cast<unsigned>(*value), static_cast<unsigned>(g_levelAtLastOn));
            }
            ApplyLevel254Hardware(g_levelAtLastOn);
            return;
        }

        if (g_buttonDimmingHwActive)
        {
            // 长按调光期间硬件由 AppTask 以 Q16 精度直接驱动，避免整数 level 台阶
            return;
        }

        ApplyLevel254Hardware(*value);
    }
    if (clusterId == OnOff::Id && attributeId == OnOff::Attributes::OnOff::Id)
    {
        const bool isOn = (value != nullptr) && (*value != 0);
        g_offTransitionActive = !isOn;
        app::DataModel::Nullable<uint8_t> brightness;
        uint8_t level254 = APP_LEVEL_MAX;
        if (chip::app::Clusters::LevelControl::Attributes::CurrentLevel::Get(1, brightness)
            == chip::Protocols::InteractionModel::Status::Success && !brightness.IsNull())
        {
            level254 = brightness.Value();
        }

        if (!isOn)
        {
            if (level254 >= APP_BUTTON_LEVEL_MIN)
            {
                g_levelAtLastOn = level254;
            }
            else if (g_memLevel >= APP_BUTTON_LEVEL_MIN)
            {
                g_levelAtLastOn = g_memLevel;
            }
        }
        else if (level254 < APP_BUTTON_LEVEL_MIN && g_levelAtLastOn >= APP_BUTTON_LEVEL_MIN)
        {
            level254 = g_levelAtLastOn;
            RestoreMatterLevel254(level254);
            ChipLogError(Zcl, "[DIM] OnOff=1 restored level254=%u (was device min)",
                         static_cast<unsigned>(level254));
        }

        if (isOn && !g_buttonPresetActive && !g_isColorTempMode
            && g_rgbTargetR == 0u && g_rgbTargetG == 0u && g_rgbTargetB == 0u)
        {
            g_rgbTargetR = g_lastNonZeroTargetR;
            g_rgbTargetG = g_lastNonZeroTargetG;
            g_rgbTargetB = g_lastNonZeroTargetB;
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
            StartTurnOnFadeForLevel254(level254);
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
    // NOTE: Color output is fully owned by the SM15135E fade path above
    // (ScheduleAttrRgbwFade / StartRgbwFade). The stock RGBLEDWidget path via
    // LightMgr().InitiateLightCtrlAction() drives the LED a second time with the
    // final color immediately (no fade), fighting the fade and causing flicker.
    // It is intentionally disabled here to keep a single output owner.
    if (clusterId == 0x0003u)
    {
        ChipLogProgress(Zcl, "Identify attribute ID: " ChipLogFormatMEI " Type: %u Value: %u, length %u",
                        ChipLogValueMEI(attributeId), type, (value != nullptr) ? *value : 0u, size);
        if (attributeId == 0x0000u && value != nullptr && size == sizeof(uint16_t))
        {
            uint16_t identifyTime = 0;
            memcpy(&identifyTime, value, sizeof(uint16_t));
            ChipLogError(Zcl, "[IDENTIFY] IdentifyTime attribute set: %u seconds", static_cast<unsigned>(identifyTime));
            AppTask::GetAppTask().StartIdentify(identifyTime, "zigbee-identify-attr");
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

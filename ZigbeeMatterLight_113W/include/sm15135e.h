#pragma once

#include <stddef.h>
#include <stdint.h>

// Standby modes (2-bit field)
#define SM15135E_STANDBY_NORMAL 0x0u
#define SM15135E_STANDBY_SLEEP  0x2u

// Current gain codes (D5..D1) -> typical current
#define SM15135E_GAIN_10_2MA   0x00u
#define SM15135E_GAIN_20_3MA   0x01u
#define SM15135E_GAIN_30_4MA   0x02u
#define SM15135E_GAIN_40_5MA   0x03u
#define SM15135E_GAIN_50_6MA   0x04u
#define SM15135E_GAIN_60_7MA   0x05u
#define SM15135E_GAIN_70_8MA   0x06u
#define SM15135E_GAIN_80_9MA   0x07u
#define SM15135E_GAIN_91_0MA   0x08u
#define SM15135E_GAIN_101_1MA  0x09u
#define SM15135E_GAIN_111_2MA  0x0Au
#define SM15135E_GAIN_121_3MA  0x0Bu
#define SM15135E_GAIN_130_7MA  0x0Cu
#define SM15135E_GAIN_140_6MA  0x0Du
#define SM15135E_GAIN_150_5MA  0x0Eu
#define SM15135E_GAIN_160_2MA  0x0Fu
#define SM15135E_GAIN_170_0MA  0x10u
#define SM15135E_GAIN_179_0MA  0x11u
#define SM15135E_GAIN_188_5MA  0x12u
#define SM15135E_GAIN_198_0MA  0x13u

// 保留你原本的 STANDBY 和 GAIN 宏定义...

typedef struct {
  uint16_t r;
  uint16_t g;
  uint16_t b;
  uint16_t w;
  uint16_t y; // 💡 确保有第 5 路黄光/琥珀光灰度

  uint8_t gain_r;
  uint8_t gain_g;
  uint8_t gain_b;
  uint8_t gain_w;
  uint8_t gain_y; // 💡 确保有第 5 路电流增益

  uint8_t standby;
  uint8_t reserve;
} sm15135e_pixel_t;

// 调整导出函数，适配 5 路渐变参数
void sm15135e_init(void);
void sm15135e_send_reset(void);
void sm15135e_send_frame(const sm15135e_pixel_t *p);
void sm15135e_set_rgbwy(sm15135e_pixel_t *p, uint16_t r, uint16_t g, uint16_t b, uint16_t w, uint16_t y);
void sm15135e_set_all_gain(sm15135e_pixel_t *p, uint8_t gain);
void sm15135e_fill_default(sm15135e_pixel_t *p);


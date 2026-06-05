#include "sm15135e.h"
#include "cmsis_os2.h"
#include "sl_spidrv_instances.h"
#include "spidrv.h"
#include <string.h>

// #define SM15135E_SPI_SYMBOL_BITS 4u

// 💡 核心修改：升级为手册指定的 112 Bits
// #define SM15135E_FRAME_BITS   112u
// #define SM15135E_ENCODED_BITS (SM15135E_FRAME_BITS * SM15135E_SPI_SYMBOL_BITS)
// 🎯 (112 * 4 + 7) / 8 = 56 字节静态安全 DMA 缓冲区

//(SM15135E_FRAME_BITS >> 1)
//((SM15135E_ENCODED_BITS + 7u) / 8u) //+7 是为了向上取整到完整字节

#define SM15135E_RESET_BUF_BYTES 128u // 复位发送长度
#define SM15135E_ENCODED_BYTES   56   // 数据发送长度

#define sm15135e_mask5(v) (uint8_t)(v & 0x1Fu)

// ⚡⚡⚡ 无分支无跳转宏：通过纯算术合并图案 ⚡⚡⚡
// bh 和 bl 必须是严格的 0 或 1。
// 映射单个逻辑位：0x08u | (bit << 2u) | (bit << 1u)
// bh（高位）转换后整体左移 4 位，再与 bl（低位）相或
#define PACK_SPI_BYTE_PURE(bh, bl)                                                                                     \
    ((uint8_t)(((0x08u | ((uint8_t)(bh) << 2u) | ((uint8_t)(bh) << 1u)) << 4u)                                         \
               | (0x08u | ((uint8_t)(bl) << 2u) | ((uint8_t)(bl) << 1u))))

typedef union
{
    uint32_t val32;
    struct
    {
        unsigned int b0 : 1;
        unsigned int b1 : 1;
        unsigned int b2 : 1;
        unsigned int b3 : 1;
        unsigned int b4 : 1;
        unsigned int b5 : 1;
        unsigned int b6 : 1;
        unsigned int b7 : 1;
        unsigned int b8 : 1;
        unsigned int b9 : 1;
        unsigned int b10 : 1;
        unsigned int b11 : 1;
        unsigned int b12 : 1;
        unsigned int b13 : 1;
        unsigned int b14 : 1;
        unsigned int b15 : 1;
        unsigned int b16 : 1;
        unsigned int b17 : 1;
        unsigned int b18 : 1;
        unsigned int b19 : 1;
        unsigned int b20 : 1;
        unsigned int b21 : 1;
        unsigned int b22 : 1;
        unsigned int b23 : 1;
        unsigned int b24 : 1;
        unsigned int b25 : 1;
        unsigned int b26 : 1;
        unsigned int b27 : 1;
        unsigned int b28 : 1;
        unsigned int b29 : 1;
        unsigned int b30 : 1;
        unsigned int b31 : 1;
    } bits;
} sm15135e_bit_extractor_t;

// 静态安全缓冲区，规避栈释放导致的 DMA 硬件报错
static uint8_t sm15135e_reset_buf[SM15135E_RESET_BUF_BYTES] = {0};
static uint8_t sm15135e_frame_buf[SM15135E_ENCODED_BYTES];

// 控制器全局单例标志，确保 SPI/EUSART 只初始化一次
static bool sm15135e_spi_inited = false;

static void sm15135e_spi_init_once(void)
{
    if (sm15135e_spi_inited)
    {
        return;
    }
    sl_spidrv_init_instances();
    osDelay(10); // 等待 EUSART/SPI 硬件总线时钟稳定
    sm15135e_spi_inited = true;
}

void sm15135e_send_reset(void)
{
    sm15135e_spi_init_once();
    // memset(sm15135e_reset_buf, 0, sizeof(sm15135e_reset_buf));
    //  🚀 通过 Silicon Labs LDMA 引擎异步发送 Reset 信号
    (void)SPIDRV_MTransmitB(sl_spidrv_eusart_SPI_SM15135E_handle, sm15135e_reset_buf, sizeof(sm15135e_reset_buf));
}

void sm15135e_init(void)
{
    sm15135e_spi_init_once();
    // 额外挂起确保引脚处于稳定的 Idle 状态
    osDelay(2);
    sm15135e_send_reset();
}

void sm15135e_send_frame(const sm15135e_pixel_t *p)
{
    if (p == NULL)
    {
        return;
    }
    sm15135e_spi_init_once();

    size_t                   byte_idx = 0;
    sm15135e_bit_extractor_t ext;

    // 1. 灰度 5 通道打包
    uint16_t gray_channels[5] = {p->r, p->g, p->b, p->w, p->y};
    for (uint8_t i = 0; i < 5; ++i)
    {
        ext.val32 = (uint32_t)gray_channels[i];

        // 纯算术级并行压入，没有任何汇编跳转指令（无 BNE/BEQ），流水线跑满
        sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b15, ext.bits.b14);
        sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b13, ext.bits.b12);
        sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b11, ext.bits.b10);
        sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b9, ext.bits.b8);
        sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b7, ext.bits.b6);
        sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b5, ext.bits.b4);
        sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b3, ext.bits.b2);
        sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b1, ext.bits.b0);
    }

    // 2. 增益+模式复合 32位 打包
    uint32_t pack32 = 0;
    pack32 |= (uint32_t)(p->gain_r & 0x1Fu) << 27u;
    pack32 |= (uint32_t)(p->gain_g & 0x1Fu) << 22u;
    pack32 |= (uint32_t)(p->gain_b & 0x1Fu) << 17u;
    pack32 |= (uint32_t)(p->gain_w & 0x1Fu) << 12u;
    pack32 |= (uint32_t)(p->gain_y & 0x1Fu) << 7u;
    pack32 |= (uint32_t)(p->standby & 0x03u) << 5u;
    pack32 |= (uint32_t)(p->reserve & 0x1Fu);

    ext.val32 = pack32;

    sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b31, ext.bits.b30);
    sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b29, ext.bits.b28);
    sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b27, ext.bits.b26);
    sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b25, ext.bits.b24);
    sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b23, ext.bits.b22);
    sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b21, ext.bits.b20);
    sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b19, ext.bits.b18);
    sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b17, ext.bits.b16);
    sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b15, ext.bits.b14);
    sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b13, ext.bits.b12);
    sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b11, ext.bits.b10);
    sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b9, ext.bits.b8);
    sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b7, ext.bits.b6);
    sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b5, ext.bits.b4);
    sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b3, ext.bits.b2);
    sm15135e_frame_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b1, ext.bits.b0);

    // 🚀 纯内存到寄存器的算术映射，完工！
    (void)SPIDRV_MTransmitB(sl_spidrv_eusart_SPI_SM15135E_handle, sm15135e_frame_buf, sizeof(sm15135e_frame_buf));
}

void sm15135e_send_chain(const sm15135e_pixel_t *pixels, size_t count)
{
    if ((pixels == NULL) || (count == 0u))
    {
        return;
    }
    for (size_t i = count; i > 0u; --i)
    {
        sm15135e_send_frame(&pixels[i - 1u]);
    }
}

void sm15135e_set_rgbwy(sm15135e_pixel_t *p, uint16_t r, uint16_t g, uint16_t b, uint16_t w, uint16_t y)
{
    if (p == NULL)
    {
        return;
    }
    p->r = r;
    p->g = g;
    p->b = b;
    p->w = w;
    p->y = y;
}

void sm15135e_set_all_gain(sm15135e_pixel_t *p, uint8_t gain)
{
    if (p == NULL)
    {
        return;
    }
    uint8_t g5 = sm15135e_mask5(gain);
    p->gain_r = g5;
    p->gain_g = g5;
    p->gain_b = g5;
    p->gain_w = g5;
    p->gain_y = g5;
}

void sm15135e_fill_default(sm15135e_pixel_t *p)
{
    if (p == NULL)
    {
        return;
    }
    p->r = 0u;
    p->g = 0u;
    p->b = 0u;
    p->w = 0u;
    p->y = 0u;
    sm15135e_set_all_gain(p, SM15135E_GAIN_91_0MA); // 默认 60.7mA 电流
    p->standby = SM15135E_STANDBY_NORMAL;
    p->reserve = 0x1Fu; // 手册强烈建议全填 1
}

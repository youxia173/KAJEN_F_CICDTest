#include "sm15135e.h"
#include "cmsis_os2.h"
#include "sl_spidrv_instances.h"
#include "spidrv.h"
#include <string.h>

#define SM15135E_RESET_BUF_BYTES 128u // 复位发送长度
#define SM15135E_ENCODED_BYTES   56   // 数据发送长度

#define sm15135e_mask5(v) (uint8_t)(v & 0x1Fu)

// spi 查表函数值
static const uint8_t SPI_PACK_LUT[2][2] = {
    // bl = 0, bl = 1
    {0x88, 0x8E}, // bh = 0
    {0xE8, 0xEE}, // bh = 1
};

// 改为查表
#define PACK_SPI_BYTE_PURE(bh, bl) (SPI_PACK_LUT[bh][bl])

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

static uint8_t sm15135e_reset_buf[SM15135E_RESET_BUF_BYTES] = { 0 };
static uint8_t sm15135e_frame_buf[SM15135E_ENCODED_BYTES];

static bool sm15135e_spi_inited = false;
static osMutexId_t sm15135e_bus_mutex = nullptr;

static void sm15135e_bus_mutex_init(void)
{
    if (sm15135e_bus_mutex == nullptr)
    {
        sm15135e_bus_mutex = osMutexNew(nullptr);
    }
}

static void sm15135e_bus_lock(void)
{
    sm15135e_bus_mutex_init();
    if (sm15135e_bus_mutex != nullptr)
    {
        (void) osMutexAcquire(sm15135e_bus_mutex, osWaitForever);
    }
}

static void sm15135e_bus_unlock(void)
{
    if (sm15135e_bus_mutex != nullptr)
    {
        (void) osMutexRelease(sm15135e_bus_mutex);
    }
}

static void sm15135e_spi_init_once(void)
{
    if (sm15135e_spi_inited)
    {
        return;
    }
    sl_spidrv_init_instances();
    osDelay(10);
    sm15135e_spi_inited = true;
}

static bool sm15135e_spi_transmit(const void * buffer, size_t count)
{
    sm15135e_spi_init_once();

    for (uint8_t attempt = 0; attempt < 8u; ++attempt)
    {
        const Ecode_t status =
            SPIDRV_MTransmitB(sl_spidrv_eusart_SPI_SM15135E_handle, buffer, static_cast<int>(count));
        if (status == ECODE_EMDRV_SPIDRV_OK)
        {
            return true;
        }
        if (status != ECODE_EMDRV_SPIDRV_BUSY)
        {
            return false;
        }
        osDelay(1);
    }

    return false;
}

static void sm15135e_encode_frame(const sm15135e_pixel_t * p, uint8_t * out_buf)
{
    size_t                   byte_idx = 0;
    sm15135e_bit_extractor_t ext;

    const uint16_t gray_channels[5] = { p->r, p->g, p->b, p->w, p->y };
    for (uint8_t i = 0; i < 5; ++i)
    {
        ext.val32 = (uint32_t) gray_channels[i];

        out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b15, ext.bits.b14);
        out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b13, ext.bits.b12);
        out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b11, ext.bits.b10);
        out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b9, ext.bits.b8);
        out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b7, ext.bits.b6);
        out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b5, ext.bits.b4);
        out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b3, ext.bits.b2);
        out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b1, ext.bits.b0);
    }

    uint32_t pack32 = 0;
    pack32 |= (uint32_t) (p->gain_r & 0x1Fu) << 27u;
    pack32 |= (uint32_t) (p->gain_g & 0x1Fu) << 22u;
    pack32 |= (uint32_t) (p->gain_b & 0x1Fu) << 17u;
    pack32 |= (uint32_t) (p->gain_w & 0x1Fu) << 12u;
    pack32 |= (uint32_t) (p->gain_y & 0x1Fu) << 7u;
    pack32 |= (uint32_t) (p->standby & 0x03u) << 5u;
    pack32 |= (uint32_t) (p->reserve & 0x1Fu);

    ext.val32 = pack32;

    out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b31, ext.bits.b30);
    out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b29, ext.bits.b28);
    out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b27, ext.bits.b26);
    out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b25, ext.bits.b24);
    out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b23, ext.bits.b22);
    out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b21, ext.bits.b20);
    out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b19, ext.bits.b18);
    out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b17, ext.bits.b16);
    out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b15, ext.bits.b14);
    out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b13, ext.bits.b12);
    out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b11, ext.bits.b10);
    out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b9, ext.bits.b8);
    out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b7, ext.bits.b6);
    out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b5, ext.bits.b4);
    out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b3, ext.bits.b2);
    out_buf[byte_idx++] = PACK_SPI_BYTE_PURE(ext.bits.b1, ext.bits.b0);
}

void sm15135e_send_reset(void)
{
    sm15135e_bus_lock();
    (void) sm15135e_spi_transmit(sm15135e_reset_buf, sizeof(sm15135e_reset_buf));
    sm15135e_bus_unlock();
}

void sm15135e_init(void)
{
    sm15135e_spi_init_once();
    osDelay(2);
    sm15135e_send_reset();
}

void sm15135e_send_frame(const sm15135e_pixel_t * p)
{
    if (p == NULL)
    {
        return;
    }

    sm15135e_bus_lock();
    sm15135e_encode_frame(p, sm15135e_frame_buf);
    (void) sm15135e_spi_transmit(sm15135e_frame_buf, sizeof(sm15135e_frame_buf));
    sm15135e_bus_unlock();
}

bool sm15135e_transmit_pixel(const sm15135e_pixel_t * p)
{
    if (p == NULL)
    {
        return false;
    }

    sm15135e_bus_lock();
    sm15135e_encode_frame(p, sm15135e_frame_buf);
    const bool frameOk = sm15135e_spi_transmit(sm15135e_frame_buf, sizeof(sm15135e_frame_buf));
    bool ok            = frameOk;
    if (frameOk)
    {
        ok = sm15135e_spi_transmit(sm15135e_reset_buf, sizeof(sm15135e_reset_buf));
    }
    sm15135e_bus_unlock();
    return ok;
}

void sm15135e_send_chain(const sm15135e_pixel_t * pixels, size_t count)
{
    if ((pixels == NULL) || (count == 0u))
    {
        return;
    }
    for (size_t i = count; i > 0u; --i)
    {
        (void) sm15135e_transmit_pixel(&pixels[i - 1u]);
    }
}

void sm15135e_set_rgbwy(sm15135e_pixel_t * p, uint16_t r, uint16_t g, uint16_t b, uint16_t w, uint16_t y)
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

void sm15135e_set_all_gain(sm15135e_pixel_t * p, uint8_t gain)
{
    if (p == NULL)
    {
        return;
    }
    uint8_t g5 = sm15135e_mask5(gain);
    p->gain_r  = g5;
    p->gain_g  = g5;
    p->gain_b  = g5;
    p->gain_w  = g5;
    p->gain_y  = g5;
}

void sm15135e_fill_default(sm15135e_pixel_t * p)
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
    sm15135e_set_all_gain(p, SM15135E_GAIN_91_0MA);
    p->standby = SM15135E_STANDBY_NORMAL;
    p->reserve = 0x1Fu;
}

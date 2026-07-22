#include "bsp/lcd.h"
#include "bsp/fonts.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "tjpgd.h"    /* lib/tjpgd/tjpgd.[ch] — 从 elm-chan.org 下载 */

/* ================================================================== *
 *  基础初始化 / 清屏 / 文本 / 矩形
 * ================================================================== */

void LCD_Init(void)
{
    ILI9341_Init();
    ILI9341_GramScan(6);                   /* 横屏，320×240 */
    ILI9341_BackLed_Control(ENABLE);
    LCD_SetFont(&Font8x16);
    LCD_Clear(BLACK);
}

void LCD_Clear(uint16_t bg)
{
    LCD_SetColors(WHITE, bg);
    ILI9341_Clear(0, 0, LCD_X_LENGTH, LCD_Y_LENGTH);
}

void LCD_DrawText(uint16_t x, uint16_t y, const char *s)
{
    ILI9341_DispString_EN(x, y, (char *)s);
}

void LCD_DrawTextC(uint16_t x, uint16_t y, uint16_t fg, uint16_t bg, const char *s)
{
    LCD_SetColors(fg, bg);
    ILI9341_DispString_EN(x, y, (char *)s);
}

void LCD_DrawTextf(uint16_t x, uint16_t y, uint16_t fg, uint16_t bg,
                   const char *fmt, ...)
{
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    LCD_SetColors(fg, bg);
    ILI9341_DispString_EN(x, y, buf);
}

void LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    LCD_SetColors(color, color);
    ILI9341_DrawRectangle(x, y, w, h, 1);
}

/* ================================================================== *
 *  像素块推送
 *  直接走 FSMC, 不经过 ILI9341_Write_Data 的函数调用. 内联到位.
 * ================================================================== */

#define LCD_REG16(a)     (*(volatile uint16_t *)(a))
#define LCD_CMD          LCD_REG16(FSMC_Addr_ILI9341_CMD)
#define LCD_DAT          LCD_REG16(FSMC_Addr_ILI9341_DATA)

void LCD_PushRGB565Block(uint16_t x, uint16_t y,
                         uint16_t w, uint16_t h,
                         const uint16_t *pix)
{
    if (w == 0 || h == 0) return;

    /* 屏内裁剪, 避免越界访问 */
    if (x >= LCD_X_LENGTH || y >= LCD_Y_LENGTH) return;
    uint16_t vw = w, vh = h;
    if (x + vw > LCD_X_LENGTH) vw = LCD_X_LENGTH - x;
    if (y + vh > LCD_Y_LENGTH) vh = LCD_Y_LENGTH - y;

    if (vw == w && vh == h) {
        /* 无裁剪快速路径: 一次性开窗, 连续写 */
        ILI9341_OpenWindow(x, y, w, h);
        LCD_CMD = CMD_SetPixel;
        uint32_t n = (uint32_t)w * h;
        while (n--) LCD_DAT = *pix++;
    } else {
        /* 有裁剪: 逐行开窗, pix 仍按 w 步长前进 */
        for (uint16_t row = 0; row < vh; row++) {
            ILI9341_OpenWindow(x, y + row, vw, 1);
            LCD_CMD = CMD_SetPixel;
            const uint16_t *p = pix + (uint32_t)row * w;
            for (uint16_t c = 0; c < vw; c++) LCD_DAT = *p++;
        }
    }
}

/* 直方图量化位数. 用于抑制 JPG 解码时 IDCT 引入的 ±1~2 灰度扰动.
 *   0: 不量化, 256 级 (bin=g[i])           — 精度高, 但低熵图熵值虚高
 *   2: 4 级/桶, 有效 64 级 (bin=g[i]&0xFC) — 吸收小抖动, 低熵图更接近源图熵
 *   3: 8 级/桶, 有效 32 级                 — 更粗, 只在噪声很大时用
 * 注: 桶粗了以后 H 上限从 8 变成 log2(256/2^N), 但相对熵仍除以 8, 数值会缩小
 *     一点; 报告里注明即可. */
#define HIST_QUANT_BITS   0

/* 灰度 → RGB565: 高 5 位红, 高 6 位绿, 高 5 位蓝 */
static inline uint16_t gray_to_rgb565(uint8_t g)
{
    return (uint16_t)(((g & 0xF8) << 8) | ((g & 0xFC) << 3) | (g >> 3));
}

void LCD_PushGray8Block(uint16_t x, uint16_t y,
                        uint16_t w, uint16_t h,
                        uint16_t scale,
                        const uint8_t *gray)
{
    if (w == 0 || h == 0 || scale == 0) return;
    if (x >= LCD_X_LENGTH || y >= LCD_Y_LENGTH) return;

    /* 显示区尺寸 (放大后) */
    uint32_t dw = (uint32_t)w * scale;
    uint32_t dh = (uint32_t)h * scale;

    /* 屏内裁剪 */
    uint16_t vdw = (dw > (uint32_t)(LCD_X_LENGTH - x)) ? (LCD_X_LENGTH - x) : (uint16_t)dw;
    uint16_t vdh = (dh > (uint32_t)(LCD_Y_LENGTH - y)) ? (LCD_Y_LENGTH - y) : (uint16_t)dh;

    if (scale == 1) {
        /* scale=1 快速路径: 无重复, 单次开窗连续写 */
        if (vdw == w && vdh == h) {
            ILI9341_OpenWindow(x, y, w, h);
            LCD_CMD = CMD_SetPixel;
            uint32_t n = (uint32_t)w * h;
            while (n--) LCD_DAT = gray_to_rgb565(*gray++);
        } else {
            for (uint16_t row = 0; row < vdh; row++) {
                ILI9341_OpenWindow(x, y + row, vdw, 1);
                LCD_CMD = CMD_SetPixel;
                const uint8_t *p = gray + (uint32_t)row * w;
                for (uint16_t c = 0; c < vdw; c++) LCD_DAT = gray_to_rgb565(*p++);
            }
        }
        return;
    }

    /* scale > 1: 每源像素在 LCD 上画 scale 宽; 每源行在 LCD 上重复 scale 遍.
     * 逐显示行开窗, 通过 (drow / scale) 找源行. */
    for (uint16_t drow = 0; drow < vdh; drow++) {
        uint16_t srow = drow / scale;
        ILI9341_OpenWindow(x, y + drow, vdw, 1);
        LCD_CMD = CMD_SetPixel;
        const uint8_t *p = gray + (uint32_t)srow * w;
        uint16_t written = 0;
        for (uint16_t scol = 0; scol < w && written < vdw; scol++) {
            uint16_t rgb = gray_to_rgb565(p[scol]);
            uint16_t reps = scale;
            if (written + reps > vdw) reps = vdw - written;
            for (uint16_t k = 0; k < reps; k++) LCD_DAT = rgb;
            written += reps;
        }
    }
}

/* ================================================================== *
 *  JPG 解码显示 (TJpgDec)
 *  策略: 输出回调按 MCU 块 (通常 8x8 或 16x16) 触发,
 *        每收到一块直接推到 LCD, 不缓存整帧 → 省 RAM.
 *        同时可选累计 8-bit 灰度直方图给上层算熵.
 *        整数倍最近邻放大在 LCD_PushGray8Block 里做.
 * ================================================================== */

/* TJpgDec 工作区.
 *   JD_FASTDECODE=0 最小 3092B, =1 需 ~3412B; 给到 4608B 留足余量,
 *   避免个别 JPG 在紧边界下触发零散解码错误 (表现为图像上散布小黑点). */
static uint8_t s_tjd_work[4608];

typedef struct {
    const uint8_t *buf;
    uint32_t total;
    uint32_t pos;
    uint16_t ox, oy;              /* 屏幕上的左上角 */
    uint16_t scale;               /* 最近邻整数倍放大 (>=1) */
    lcd_jpeg_info_t *info;        /* 可选: 尺寸 + 直方图 */
} jpg_ctx_t;

/* 输入回调: 从我们持有的内存里搬字节给 tjpgd. dst==NULL 表示 seek 前进. */
static size_t tjd_in_cb(JDEC *jd, uint8_t *dst, size_t nread)
{
    jpg_ctx_t *c = (jpg_ctx_t *)jd->device;
    uint32_t left = (c->total > c->pos) ? (c->total - c->pos) : 0;
    if (nread > left) nread = left;
    if (dst) memcpy(dst, c->buf + c->pos, nread);
    c->pos += nread;
    return nread;
}

/* 输出回调: tjpgd 把一小块解码好的像素喂给我们. JD_FORMAT=2 → 8-bit 灰度. */
static int tjd_out_cb(JDEC *jd, void *bitmap, JRECT *rect)
{
    jpg_ctx_t *c = (jpg_ctx_t *)jd->device;

    uint16_t bw = rect->right  - rect->left + 1;
    uint16_t bh = rect->bottom - rect->top  + 1;

    /* 屏幕位置: 源坐标 × scale + 左上角偏移 */
    uint16_t sx = c->ox + (uint16_t)(rect->left * c->scale);
    uint16_t sy = c->oy + (uint16_t)(rect->top  * c->scale);

    const uint8_t *g = (const uint8_t *)bitmap;

    /* 攒直方图: 基于源像素 (未放大), 保证熵值不受 scale 影响.
     * 若 HIST_QUANT_BITS > 0, 低若干位截零, 吸收 JPG 解码 ±1~2 的扰动. */
    if (c->info) {
#if HIST_QUANT_BITS > 0
        const uint8_t mask = (uint8_t)(0xFFU << HIST_QUANT_BITS);
#endif
        uint32_t n = (uint32_t)bw * bh;
        for (uint32_t i = 0; i < n; i++) {
#if HIST_QUANT_BITS > 0
            c->info->hist[g[i] & mask]++;
#else
            c->info->hist[g[i]]++;
#endif
        }
    }

    LCD_PushGray8Block(sx, sy, bw, bh, c->scale, g);
    return 1;   /* 非 0: 继续解码 */
}

int LCD_DrawJpegEx(uint16_t x, uint16_t y,
                   const uint8_t *jpg_buf, uint32_t jpg_len,
                   lcd_jpeg_info_t *info,
                   uint16_t scale)
{
    if (scale == 0) scale = 1;

    JDEC jd;
    jpg_ctx_t ctx = { jpg_buf, jpg_len, 0, x, y, scale, info };

    if (info) {
        info->width = info->height = 0;
        memset(info->hist, 0, sizeof(info->hist));
    }

    JRESULT rc = jd_prepare(&jd, tjd_in_cb, s_tjd_work, sizeof(s_tjd_work), &ctx);
    if (rc != JDR_OK) return (int)rc;

    if (info) {
        info->width  = jd.width;
        info->height = jd.height;
    }

    /* jd_decomp 的 scale 参数是 JPG 内部 IDCT 缩小 (1/2, 1/4, 1/8), 与我们的
     * 最近邻放大无关. 传 0 (=1:1) 让 tjpgd 输出源分辨率, 我们在 tjd_out_cb
     * 里手动放大. */
    rc = jd_decomp(&jd, tjd_out_cb, 0);
    return (int)rc;
}

int LCD_DrawJpeg(uint16_t x, uint16_t y,
                 const uint8_t *jpg_buf, uint32_t jpg_len)
{
    return LCD_DrawJpegEx(x, y, jpg_buf, jpg_len, NULL, 1);
}

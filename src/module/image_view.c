/*
 * image_view.c —— 串口图像检测系统 · 显示端主模块
 *
 * 流程:
 *   ┌───────────────┐  jpg_rx_get_frame  ┌────────────────┐   FSMC
 *   │ drv/jpg_rx    │ ─────────────────► │ LCD_DrawJpegEx │ ────►  ILI9341
 *   │  (DMA + SM)   │                    │  (TJpgDec)     │
 *   └───────────────┘                    └────────────────┘
 *                                               │ info.hist[256]
 *                                               ▼
 *                                        entropy_from_hist()
 *                                               │
 *                                               ▼
 *                                    LCD 文本行 + UART 打印
 *
 * 熵值公式 (题目附录):
 *   绝对熵 H       = -Σ p(i) * log2 p(i)      p(i) = hist[i] / N
 *   相对熵 Hr (%)  =  H / 8 * 100
 */

#include "module/image_view.h"
#include "drv/jpg_rx.h"
#include "bsp/lcd.h"
#include "bsp/uart.h"
#include "stm32f1xx_hal.h"
#include <math.h>
#include <string.h>

/* ==================================================================
 *  显示布局 (硬编码, 简单直接)
 * ================================================================== */
#define INFO_Y0        192U                     /* 信息区起始 y */
#define INFO_LINE_H     16U                     /* 8x16 字体行高 */
#define INFO_L0_Y      (INFO_Y0 + 0 * INFO_LINE_H)
#define INFO_L1_Y      (INFO_Y0 + 1 * INFO_LINE_H)
#define INFO_L2_Y      (INFO_Y0 + 2 * INFO_LINE_H)

/* ==================================================================
 *  状态
 * ================================================================== */
static uint32_t s_fps_frames    = 0;    /* 上一个统计窗内解码成功帧数 */
static uint32_t s_fps_win_start = 0;    /* 上一次统计窗起始 tick */
static float    s_last_fps      = 0.0f;

/* ==================================================================
 *  熵值计算
 * ================================================================== */
static float entropy_from_hist(const uint32_t hist[256], uint32_t total)
{
    if (total == 0) return 0.0f;

    const float inv_total = 1.0f / (float)total;
    float H = 0.0f;

    for (int i = 0; i < 256; i++) {
        if (hist[i]) {
            float p = (float)hist[i] * inv_total;
            H -= p * log2f(p);
        }
    }
    return H;
}

/* ==================================================================
 *  API
 * ================================================================== */
void image_view_init(void)
{
    LCD_Init();
    LCD_Clear(BLACK);
    LCD_DrawTextC(4, 4,   WHITE, BLACK, "College Contest 2026");
    LCD_DrawTextC(4, 24,  CYAN,  BLACK, "JPG stream over USART1");
    LCD_DrawTextC(4, 44,  GREY,  BLACK, "waiting for frame...");

    jpg_rx_init();

    s_fps_frames    = 0;
    s_fps_win_start = HAL_GetTick();
    s_last_fps      = 0.0f;
}

void image_view_task(void)
{
    uint32_t     jpg_len = 0;
    const uint8_t *jpg   = jpg_rx_get_frame(&jpg_len);
    if (!jpg) return;                            /* 没新帧, 直接返回 */

    /* 首次收到有效帧时把提示行擦掉 */
    static bool s_first_frame = true;
    if (s_first_frame) {
        LCD_FillRect(0, 0, LCD_X_LENGTH, INFO_Y0, BLACK);
        s_first_frame = false;
    }

    /* -------- 解码 + 显示 (核心一步) -------- */
    lcd_jpeg_info_t info;
    int rc = LCD_DrawJpegEx(0, 0, jpg, jpg_len, &info);
    jpg_rx_release();                            /* 尽早释放, 让 rx 组下一帧 */

    if (rc != 0) {
        LCD_DrawTextf(4, INFO_L0_Y, RED, BLACK,
                      "decode err %d  len %lu   ", rc, jpg_len);
        return;
    }

    /* -------- 熵值 -------- */
    uint32_t npix = (uint32_t)info.width * (uint32_t)info.height;
    float    H    = entropy_from_hist(info.hist, npix);
    float    Hr   = H * 100.0f / 8.0f;

    /* -------- 帧速 (每 1s 更新一次) -------- */
    s_fps_frames++;
    uint32_t now = HAL_GetTick();
    uint32_t dt  = now - s_fps_win_start;
    if (dt >= 1000) {
        s_last_fps      = (float)s_fps_frames * 1000.0f / (float)dt;
        s_fps_frames    = 0;
        s_fps_win_start = now;

        UART_Printf("[img] %ux%u  %lu B  H=%.3f  Hr=%.2f%%  fps=%.1f\r\n",
                    info.width, info.height, jpg_len,
                    (double)H, (double)Hr, (double)s_last_fps);
    }

    /* -------- LCD 信息区 -------- */
    LCD_DrawTextf(4, INFO_L0_Y, WHITE,  BLACK,
                  "size %ux%u  len %luB   ",
                  info.width, info.height, jpg_len);
    LCD_DrawTextf(4, INFO_L1_Y, YELLOW, BLACK,
                  "H=%.3f  Hr=%.1f%%   ",
                  (double)H, (double)Hr);
    LCD_DrawTextf(4, INFO_L2_Y, GREEN,  BLACK,
                  "fps %.1f  frm %lu    ",
                  (double)s_last_fps, jpg_rx_frame_count());
}

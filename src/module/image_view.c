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
#include "module/eye_detect.h"
#include "drv/jpg_rx.h"
#include "bsp/lcd.h"
#include "bsp/uart.h"
#include "bsp/led.h"
#include "stm32f1xx_hal.h"
#include <math.h>
#include <string.h>

/* ==================================================================
 *  显示布局 (硬编码, 简单直接)
 *  信息区 4 行, 每行 16 px 高, 占屏底部 64 px.
 * ================================================================== */
#define INFO_Y0        176U                     /* 信息区起始 y (上移 16 给 L3) */
#define INFO_LINE_H     16U                     /* 8x16 字体行高 */
#define INFO_L0_Y      (INFO_Y0 + 0 * INFO_LINE_H)   /* size / len   */
#define INFO_L1_Y      (INFO_Y0 + 1 * INFO_LINE_H)   /* H / Hr       */
#define INFO_L2_Y      (INFO_Y0 + 2 * INFO_LINE_H)   /* fps / frames */
#define INFO_L3_Y      (INFO_Y0 + 3 * INFO_LINE_H)   /* eye 状态提示 */

/* 图片显示区: 抠图固定 48x36, 4x 最近邻放大 → 192x144, 水平居中. */
#define IMG_SRC_W       48U
#define IMG_SRC_H       36U
#define IMG_SCALE        4U
#define IMG_X          ((LCD_X_LENGTH - IMG_SRC_W * IMG_SCALE) / 2U)   /* = 64 */
#define IMG_Y            0U

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
    LCD_DrawTextC(4, 24,  CYAN,  BLACK, "JPG stream over USART2");
    LCD_DrawTextC(4, 44,  GREY,  BLACK, "waiting for frame...");

    jpg_rx_init();
    LED_Init();
    eye_detect_init();

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

    /* -------- 解码 + 显示 (核心一步) --------
     * 源图 48x36 固定, 4x 最近邻放大 → 192x144.
     * 居中放在 x=64, y=0. 上部 176px 全用于图片, 下 64px 留给 info. */
    lcd_jpeg_info_t info;
    int rc = LCD_DrawJpegEx(IMG_X, IMG_Y, jpg, jpg_len, &info, IMG_SCALE);
    jpg_rx_release();                            /* 尽早释放, 让 rx 组下一帧 */

    if (rc != 0) {
        LCD_DrawTextf(4, INFO_L0_Y, RED, BLACK,
                      "decode err %d  len %lu   ", rc, jpg_len);
        return;
    }

    /* -------- 熵值 -------- */
    uint32_t npix = (uint32_t)info.width * (uint32_t)info.height;
    float    H    = entropy_from_hist(info.hist, npix);
    float    Hr   = H / 8.0f;                    /* 与 PC 端一致: 0~1 的比例 */

    /* -------- 帧速 (每 1s 更新一次) -------- */
    s_fps_frames++;
    uint32_t now = HAL_GetTick();
    uint32_t dt  = now - s_fps_win_start;
    if (dt >= 1000) {
        s_last_fps      = (float)s_fps_frames * 1000.0f / (float)dt;
        s_fps_frames    = 0;
        s_fps_win_start = now;
    }

    /* -------- LCD 信息区 -------- */
    LCD_DrawTextf(4, INFO_L0_Y, WHITE,  BLACK,
                  "size %ux%u  len %luB   ",
                  info.width, info.height, jpg_len);
    LCD_DrawTextf(4, INFO_L1_Y, YELLOW, BLACK,
                  "H=%.3f  Hr=%.3f   ",
                  (double)H, (double)Hr);
    LCD_DrawTextf(4, INFO_L2_Y, GREEN,  BLACK,
                  "fps %.1f  frm %lu    ",
                  (double)s_last_fps, jpg_rx_frame_count());
    LCD_DrawTextf(4, INFO_L3_Y, CYAN, BLACK,
                  "eye: %s   ",
                  eye_detect_is_closed() ? "CLOSED" : "OPEN");

    /* -------- 闭眼检测 (拓展题 2) --------
     * 喂 H 给状态机, 输出稳定的 OPEN/CLOSED. 只在跨状态时刷 LCD/LED,
     * 减少 FSMC 无谓写和 LED IO 抖动. */
    eye_detect_feed(H, now);
    bool closed = eye_detect_is_closed();

    static bool s_prev_closed = false;
    if (closed != s_prev_closed) {
        LED_Set(LED_RED, closed);
        if (closed) {
            LCD_DrawTextf(4, INFO_L3_Y, GREEN, BLACK,
                          "*** EYE CLOSED ***  ");
        } else {
            /* 睁眼: 用黑底空串把整行擦掉 */
            LCD_FillRect(0, INFO_L3_Y, LCD_X_LENGTH, INFO_LINE_H, BLACK);
        }
        s_prev_closed = closed;
    }
}

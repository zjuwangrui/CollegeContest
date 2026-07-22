#ifndef __MODULE_IMAGE_VIEW_H
#define __MODULE_IMAGE_VIEW_H

/*
 * ===========================================================================
 *  image_view —— 串口图像检测系统 · 单片机显示端主模块
 * ===========================================================================
 *
 *  职责:
 *    1) 从 drv/jpg_rx 拿到一整帧 JPG
 *    2) 用 bsp/lcd 的 LCD_DrawJpegEx 解码显示 (RGB565, 1:1 不缩小)
 *    3) 用返回的直方图算绝对熵 H 和相对熵 Hr
 *    4) 在 LCD 底部显示: 分辨率, 字节数, H, Hr, 显示帧速 (FPS)
 *    5) 通过 UART_Printf 把帧速打印到 USART1 调试口 (评测参考)
 *
 *  与其它任务的关系:
 *    jpg_rx_task     (20ms) —— 拉 DMA, 组帧
 *    image_view_task ( 0ms) —— 每轮都跑, 有帧就解, 没帧就返回
 *
 *  LCD 布局 (320x240 横屏):
 *    [0,0)          .. [W, H)             ← 灰度图 1:1 显示 (W,H 由 jpg 决定)
 *    y = 192 .. 239                       ← 3 行 8x16 字体信息区
 *      L0: size 40x36    len  415B
 *      L1: H=3.134  Hr=39.2%
 *      L2: fps 5.2      frames 128
 * ===========================================================================
 */

void image_view_init(void);
void image_view_task(void);

#endif /* __MODULE_IMAGE_VIEW_H */

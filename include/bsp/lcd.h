#ifndef __BSP_LCD_H
#define __BSP_LCD_H

#include <stdbool.h>
#include <stdint.h>
#include "bsp/bsp_ili9341_lcd.h"

/*
 * LCD 通用封装。底层芯片驱动在 bsp_ili9341_lcd。
 * 只暴露"上电初始化 + 常用绘制原语"，不假设应用页面结构。
 * 应用页面（首页/频谱页/参数页...）请在 module/ui 或用户自己的
 * module/xxx 里写。
 */

void LCD_Init(void);                                 /* 上电初始化 + 清屏 + 背光 */
void LCD_Clear(uint16_t bg);

/* 文字：fg/bg 颜色需先自己设置好，或直接用便捷版本 */
void LCD_DrawText  (uint16_t x, uint16_t y, const char *s);
void LCD_DrawTextC (uint16_t x, uint16_t y, uint16_t fg, uint16_t bg,
                    const char *s);
void LCD_DrawTextf (uint16_t x, uint16_t y, uint16_t fg, uint16_t bg,
                    const char *fmt, ...);

/* 矩形填充（用于状态灯、条形谱等） */
void LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                  uint16_t color);

/* ------------------------------------------------------------------ *
 *  像素块推送 + JPG 解码显示
 *  用于串口图像检测系统:  UART 收 jpg 流 →  LCD_DrawJpeg() 一步到屏.
 *  1:1 显示, 不缩放; 超出屏幕的部分自动裁剪.
 * ------------------------------------------------------------------ */

/* 把 w*h 个 RGB565 像素直接推到 LCD 指定矩形. 无缩放. */
void LCD_PushRGB565Block(uint16_t x, uint16_t y,
                         uint16_t w, uint16_t h,
                         const uint16_t *pix);

/* 解码 (jpg_buf, jpg_len) 里的 jpeg, 左上角放在 (x, y) 显示.
 *   返回值:  0        成功
 *            非 0     TJpgDec 的 JDR_* 错误码 (见 tjpgd.h)
 * 若解出的图片比 LCD 大, 超出部分不显示 (评测抠图不会超 320x240). */
int  LCD_DrawJpeg(uint16_t x, uint16_t y,
                  const uint8_t *jpg_buf, uint32_t jpg_len);

/* 解码后回填的图片元信息 (可选; 传 NULL 就不填).
 *   width/height : 解码得到的像素宽高
 *   hist         : 8-bit 灰度直方图, 供上层算熵值用. */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t hist[256];
} lcd_jpeg_info_t;

int  LCD_DrawJpegEx(uint16_t x, uint16_t y,
                    const uint8_t *jpg_buf, uint32_t jpg_len,
                    lcd_jpeg_info_t *info);

#endif /* __BSP_LCD_H */

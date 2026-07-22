#ifndef __DRV_JPG_RX_H
#define __DRV_JPG_RX_H

#include <stdint.h>
#include <stdbool.h>

/*
 * ===========================================================================
 *  JPG 图像接收驱动 (USART2 + DMA1_Channel6)
 * ===========================================================================
 *
 *  串口: USART2  PA2(TX) / PA3(RX)  115200 8N1
 *        (院赛用外接 USB-TTL 模块接 PA2/PA3, USART1 留给调试打印)
 *  DMA : DMA1 Channel6, 环形模式, 常驻收数.
 *
 *  数据流:
 *    PC (JPG2COM.exe) --USB-TTL--> USART2
 *      → DMA1_Ch6 循环写入 s_ring[]
 *      → jpg_rx_task() 按字节扫描, 找 SOI(FF D8) 和 EOI(FF D9)
 *      → 组好一整帧 jpg 后, 通过 jpg_rx_get_frame() 交给上层显示模块
 *
 *  为什么用 DMA + 轮询 而不是 RXNE 中断:
 *    115200 baud 满负荷时约 11.5 KB/s, 逐字节中断代价太高.
 *    DMA 常驻收数, 任务 20ms 拉一次 NDTR 就够了.
 *
 *  为什么用 marker 扫描 而不是 IDLE 线中断:
 *    题目说负载 100% 时几乎没有 idle 间隔, IDLE 判帧不可靠;
 *    FF D8 / FF D9 是 JPG 规范, 100% 可用.
 *
 *  帧丢弃策略:
 *    上层没消费完 → 新到的完整帧直接丢弃 (只保留最近一帧待处理).
 *    保证实时性, 不做队列.
 *
 *  与 drv/serial_screen 的关系:
 *    两者都用 USART2, 互斥. 本工程 (院赛图像检测) 用 jpg_rx,
 *    另一工程 (信号发生器 + 大彩屏) 用 serial_screen. 不同时链接.
 * ===========================================================================
 */

/* 单帧最大字节. 评测 jpg 通常 <1500B, 4KB 有充分冗余. */
#define JPG_RX_FRAME_MAX   (4 * 1024)

/* DMA 环形缓冲大小. 20ms 任务周期下, 8KB 足够. */
#define JPG_RX_RING_SZ     (8 * 1024)

/* ---------- 生命周期 ---------- */

/* 初始化 USART2 DMA1_Ch6, 启动常驻接收. 必须先调过 MX_USART2_UART_Init.
 * 内部会关掉 USART2 的 RXNE 中断, 避免和 drv/serial_screen 冲突. */
void jpg_rx_init(void);

/* 任务函数: 扫描 DMA 环形区新字节, 组帧. 建议 20ms 周期. */
void jpg_rx_task(void);

/* ---------- 消费接口 ---------- */

/* 取出最近一帧完整 jpg. 若没有新帧, 返回 NULL.
 *   *len_out  被填成帧长度 (含 FF D8 头和 FF D9 尾).
 *   返回的指针内容在 jpg_rx_release() 之前保持稳定. */
const uint8_t *jpg_rx_get_frame(uint32_t *len_out);

/* 释放当前帧, 允许接收下一帧. 上层显示完必须调用. */
void jpg_rx_release(void);

/* ---------- 状态查询 (可选, 用于调试) ---------- */

/* 累计成功组帧数 */
uint32_t jpg_rx_frame_count(void);

/* 累计丢弃帧数 (上层来不及消费) */
uint32_t jpg_rx_drop_count(void);

/* 累计溢出次数 (单帧超过 JPG_RX_FRAME_MAX) */
uint32_t jpg_rx_overflow_count(void);

/* DMA 已写入的字节数游标 (环形位置, 0..JPG_RX_RING_SZ-1).
 * 反复调用观察值是否在变: 变 → DMA 在收数; 不变 → 硬件层没通. */
uint16_t jpg_rx_dma_head(void);

/* 读一个环形缓冲字节, offset 从 head 往回数.
 * 用来 hex dump 最近收到的字节, 看是不是真 JPG 数据. */
uint8_t  jpg_rx_ring_peek(uint16_t offset_from_head);

/* 读 USART2 错误标志 (ORE/NE/FE/PE).
 * 返回位掩码: bit0=ORE, bit1=NE, bit2=FE, bit3=PE.
 * 长期为 0 说明字节层完全 OK; 非 0 说明至少出过一次相应错误. */
uint8_t  jpg_rx_usart_errors(void);

#endif /* __DRV_JPG_RX_H */

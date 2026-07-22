/*
 * jpg_rx.c —— JPG 图像接收驱动 (USART2 + DMA1_Ch6 + marker 扫描)
 *
 * DMA 常驻循环写入 s_ring[]. jpg_rx_task 拉 NDTR 计算写入位置,
 * 逐字节喂给状态机, 遇到 FF D8 开新帧, 遇到 FF D9 结帧.
 *
 * 与 drv/serial_screen 的区别:
 *   - serial_screen 用 USART2 RXNE 中断, 逐字节收指令帧, 适合低速屏协议;
 *   - jpg_rx        用 USART2 DMA + 任务轮询, 适合高速 (11.5KB/s) 图像流.
 *   两者同占 USART2, 只能二选一. 本文件 init 时会主动关掉 RXNE 中断.
 */

#include "drv/jpg_rx.h"
#include "bsp/uart.h"
#include <string.h>

/* ==================================================================
 *  DMA 环形接收区 + 单帧组装区
 * ================================================================== */
static uint8_t         s_ring[JPG_RX_RING_SZ];
static uint16_t        s_ring_tail = 0;              /* 已扫描到的位置 */

static DMA_HandleTypeDef s_hdma_usart2_rx;

/* 单帧组装. 被状态机写入, 完成后置 s_frame_ready. */
static uint8_t          s_frame[JPG_RX_FRAME_MAX];
static uint32_t         s_frame_len   = 0;
static volatile bool    s_frame_ready = false;

/* 状态机 */
static bool             s_in_frame    = false;
static uint8_t          s_last_byte   = 0x00;

/* 统计 */
static uint32_t         s_frames    = 0;
static uint32_t         s_drops     = 0;
static uint32_t         s_overflows = 0;

/* ==================================================================
 *  DMA 硬件初始化 (DMA1_Ch6 = USART2_RX)
 * ================================================================== */
static void jpg_rx_dma_init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();

    s_hdma_usart2_rx.Instance                 = DMA1_Channel6;
    s_hdma_usart2_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    s_hdma_usart2_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    s_hdma_usart2_rx.Init.MemInc              = DMA_MINC_ENABLE;
    s_hdma_usart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    s_hdma_usart2_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    s_hdma_usart2_rx.Init.Mode                = DMA_CIRCULAR;
    s_hdma_usart2_rx.Init.Priority            = DMA_PRIORITY_VERY_HIGH;

    if (HAL_DMA_Init(&s_hdma_usart2_rx) != HAL_OK) while (1);

    __HAL_LINKDMA(&huart2, hdmarx, s_hdma_usart2_rx);

    /* 关掉 RXNE 中断: 避免与 drv/serial_screen 的字节回调抢总线 */
    __HAL_UART_DISABLE_IT(&huart2, UART_IT_RXNE);
    HAL_NVIC_DisableIRQ(USART2_IRQn);

    /* USART2 允许 DMA 接收. HAL_DMA_Start 只启动 DMA, 不动 UART 状态机. */
    SET_BIT(huart2.Instance->CR3, USART_CR3_DMAR);

    HAL_DMA_Start(&s_hdma_usart2_rx,
                  (uint32_t)&huart2.Instance->DR,
                  (uint32_t)s_ring,
                  JPG_RX_RING_SZ);
}

/* ==================================================================
 *  API
 * ================================================================== */
void jpg_rx_init(void)
{
    s_ring_tail   = 0;
    s_frame_len   = 0;
    s_frame_ready = false;
    s_in_frame    = false;
    s_last_byte   = 0;
    s_frames      = 0;
    s_drops       = 0;
    s_overflows   = 0;

    jpg_rx_dma_init();
}

/* 状态机: 处理一个新字节.
 *   规则:
 *     - 见到 FF D8 (SOI): 打开新帧, 写入 FF D8
 *     - 帧内: 累积字节
 *     - 见到 FF D9 (EOI): 帧结束, 置 ready 标志
 *     - 溢出: 放弃当前帧
 *   注: JPG 熵编码段里 0xFF 后总跟 0x00 (字节填充), 所以只有
 *       FF D8 / FF D9 这两个组合会作为真正的段标记出现. */
static void feed_byte(uint8_t b)
{
    if (!s_in_frame) {
        if (s_last_byte == 0xFF && b == 0xD8) {
            /* 若上一帧还未消费, 直接丢弃新一帧的开头, 保留旧帧 */
            if (s_frame_ready) {
                s_drops++;
                s_last_byte = b;
                return;
            }
            s_in_frame       = true;
            s_frame_len      = 0;
            s_frame[s_frame_len++] = 0xFF;
            s_frame[s_frame_len++] = 0xD8;
        }
    } else {
        if (s_frame_len >= JPG_RX_FRAME_MAX) {
            s_overflows++;
            s_in_frame  = false;
            s_frame_len = 0;
        } else {
            s_frame[s_frame_len++] = b;
            if (s_last_byte == 0xFF && b == 0xD9) {
                s_frame_ready = true;
                s_in_frame    = false;
                s_frames++;
            }
        }
    }

    s_last_byte = b;
}

void jpg_rx_task(void)
{
    /* DMA 剩余传输数 → 已写入字节数 (head 位置).
     * 循环模式下, CNDTR 从 SZ 递减到 0 再重装为 SZ. */
    uint16_t ndtr = (uint16_t)__HAL_DMA_GET_COUNTER(&s_hdma_usart2_rx);
    uint16_t head = (uint16_t)(JPG_RX_RING_SZ - ndtr);
    if (head >= JPG_RX_RING_SZ) head = 0;           /* 边界防御 */

    /* 从 tail 处理到 head. head 可能已跨过 tail (环形回卷), 分两段. */
    while (s_ring_tail != head) {
        feed_byte(s_ring[s_ring_tail]);
        s_ring_tail++;
        if (s_ring_tail >= JPG_RX_RING_SZ) s_ring_tail = 0;
    }
}

const uint8_t *jpg_rx_get_frame(uint32_t *len_out)
{
    if (!s_frame_ready) return NULL;
    if (len_out) *len_out = s_frame_len;
    return s_frame;
}

void jpg_rx_release(void)
{
    s_frame_ready = false;
    /* s_frame_len 不清零, 保留供调试打印, 下一帧写入时会覆盖 */
}

uint32_t jpg_rx_frame_count   (void) { return s_frames;    }
uint32_t jpg_rx_drop_count    (void) { return s_drops;     }
uint32_t jpg_rx_overflow_count(void) { return s_overflows; }

/* 读 USART2 错误标志 (读 SR 后再读 DR 会清标志; 只读 SR 保留状态用).
 * 返回值:
 *   bit0 = ORE (overrun, DMA 没跟上)
 *   bit1 = NE  (noise, 采样点有毛刺)
 *   bit2 = FE  (framing, 停止位错)
 *   bit3 = PE  (parity, 校验错)
 * 位一直是 1 就说明这类错误至少出过一次. */
uint8_t jpg_rx_usart_errors(void)
{
    uint32_t sr = huart2.Instance->SR;
    uint8_t r = 0;
    if (sr & USART_SR_ORE) r |= 0x01;
    if (sr & USART_SR_NE ) r |= 0x02;
    if (sr & USART_SR_FE ) r |= 0x04;
    if (sr & USART_SR_PE ) r |= 0x08;
    /* 清: 读 SR 后读 DR 就能清 ORE/NE/FE/PE.
     * 但 DR 被 DMA 占用了, 读 DR 相当于抢一个字节 → 会掉数据.
     * 所以这里只读, 不清; 让状态一直保留供观察. */
    return r;
}

uint16_t jpg_rx_dma_head(void)
{
    uint16_t ndtr = (uint16_t)__HAL_DMA_GET_COUNTER(&s_hdma_usart2_rx);
    uint16_t head = (uint16_t)(JPG_RX_RING_SZ - ndtr);
    if (head >= JPG_RX_RING_SZ) head = 0;
    return head;
}

uint8_t jpg_rx_ring_peek(uint16_t offset_from_head)
{
    /* 从 head 往回数 offset 位, 回卷 */
    uint16_t head = jpg_rx_dma_head();
    uint32_t idx  = ((uint32_t)head + JPG_RX_RING_SZ - offset_from_head - 1)
                    % JPG_RX_RING_SZ;
    return s_ring[idx];
}

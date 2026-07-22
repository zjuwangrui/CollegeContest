/**
 * main.c —— STM32 App Framework 骨架
 *
 * 只做四件事：
 *   1) 内核: HAL_Init + 系统时钟 (HSE 8M × 9 = 72MHz)
 *   2) BSP : GPIO / USART1 (调试) / ADC
 *   3) 通用模块 & 驱动的 init
 *   4) 注册调度任务 → sched_run_forever
 *
 * 增加业务模块的步骤:
 *   - include/module/xxx.h + src/module/xxx.c: void xxx_init/void xxx_task
 *   - 若需要新驱动: include/drv/yyy.h + src/drv/yyy.c
 *   - 在下方 "任务表" 里加一行 sched_task_t，并在 main() 里 sched_register
 *
 * 任务耗时约束 (详见 core/scheduler.h):
 *   - 禁用 HAL_Delay / 阻塞轮询
 *   - 单次执行时间 < 5ms
 *   - 长操作用状态机分帧
 */

#include "stm32f1xx_hal.h"

#include "bsp/gpio.h"
#include "bsp/uart.h"
#include "bsp/led.h"

#include "core/scheduler.h"
#include "module/image_view.h"
#include "drv/jpg_rx.h"
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL     = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        osc.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
        osc.HSIState            = RCC_HSI_ON;
        osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
        osc.PLL.PLLSource       = RCC_PLLSOURCE_HSI_DIV2;
        osc.PLL.PLLMUL          = RCC_PLL_MUL16;
        if (HAL_RCC_OscConfig(&osc) != HAL_OK) while (1);
    }

    clk.ClockType      = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                         RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) while (1);
}




/* ===== 任务表 (在这里增删) ===== */


/* jpg_rx: 20ms 从 DMA1_Ch6 (USART2_RX) 环形区拉字节, 按 FF D8 / FF D9 组帧 */
static sched_task_t t_jpg_rx     = { .run = jpg_rx_task,      .period_ms = 20,   .name = "jpgrx" };
/* image_view: 每轮都跑, 有帧就解码显示 + 算熵 + 更新 FPS */
static sched_task_t t_image_view = { .run = image_view_task,  .period_ms = 0,    .name = "imgv"  };

int main(void)
{
    /* ---- 内核 ---- */
    HAL_Init();
    SystemClock_Config();

    /* ---- BSP ---- */
    MX_GPIO_Init();
    MX_USART2_UART_Init();          /* JPG 图像接收 (PA2/PA3), 由 jpg_rx 用 DMA 抢占 */

    /* ---- 业务模块 ---- */
    image_view_init();              /* 内部会 LCD_Init 和 jpg_rx_init */

    /* ---- 调度器 ---- */
    sched_init();
    sched_register(&t_jpg_rx);
    sched_register(&t_image_view);
    sched_run_forever();
}

/*
 * eye_detect.c —— 熵值 + JPG 文件长度联合的闭眼检测状态机
 *
 * 判决逻辑 (OR):
 *   闭眼候选 = (H < H_ENTER)  OR  (len < LEN_THR)
 *   睁眼恢复 = (H >= H_EXIT)  AND (len >= LEN_THR)      ← OR 的反是 AND
 *
 * 迟滞 + 时间过滤: 短暂 H 或 len 下跌 (眨眼) 不触发; 持续 T_CLOSE_MS 才判 CLOSED.
 * 反过来睁眼也需要 T_OPEN_MS 稳定才回 OPEN.
 *
 * 之所以引入 len: 熵是全局直方图统计, 对眼睛在抠图区里的位置敏感; JPG 文件
 * 长度反映的是压缩后信息量, 与位置耦合度更低. 两者 OR 起来可以互补——只要
 * 任一指标掉下去就可以怀疑闭眼, 提高检出率.
 */

#include "module/eye_detect.h"

/* ==================================================================
 *  可调参数 (根据实测调整)
 * ================================================================== */

/* 进入 MAYBE_CLOSED 的熵值下阈. H 低于此值可能已闭眼. */
#define EYE_H_ENTER          6.35f

/* 离开 CLOSED / MAYBE_CLOSED 的熵值上阈. H 高于此值判定睁眼.
 * 与 EYE_H_ENTER 之间形成迟滞带, 抗抖动. */
#define EYE_H_EXIT           6.4f

/* JPG 文件字节数阈值. len 低于此值视为压缩后信息量低 → 闭眼候选.
 * 由于 len 是整数, 不加迟滞. */
#define EYE_LEN_THR          700U

/* 从 MAYBE_CLOSED 升级到 CLOSED 所需的最短持续时间.
 * 生理眨眼约 100~400ms, 取 800ms 完全过滤. */
#define EYE_T_CLOSE_MS       800U

/* CLOSED → OPEN 所需的最短睁眼稳定时间.
 * 防止 H 抖到 EXIT 阈值上方一下又掉下去时误判恢复. */
#define EYE_T_OPEN_MS        300U

/* ==================================================================
 *  状态
 * ================================================================== */
typedef enum {
    ST_OPEN         = 0,
    ST_MAYBE_CLOSED = 1,   /* 至少一个指标低了, 但没到确认时间 */
    ST_CLOSED       = 2,   /* 确认闭眼, 提示 */
    ST_MAYBE_OPEN   = 3,   /* CLOSED 中两个指标都回升, 观察是否稳定 */
} eye_state_t;

static eye_state_t s_state       = ST_OPEN;
static uint32_t    s_state_start = 0;      /* 进入当前状态的 tick */

/* ==================================================================
 *  判据 (OR / AND 内联)
 * ================================================================== */
static inline bool is_closed_candidate(float H, uint32_t len)
{
    /* OR: 熵低 或 len 小, 任一成立 */
    return (H < EYE_H_ENTER) || (len < EYE_LEN_THR);
}

static inline bool is_open_confirmed(float H, uint32_t len)
{
    /* AND: 熵高 且 len 大, 两者都成立才算真睁眼
     * (数学上是 is_closed_candidate 用 EXIT 阈值代入的取反) */
    return (H >= EYE_H_EXIT) && (len >= EYE_LEN_THR);
}

/* ==================================================================
 *  API
 * ================================================================== */

void eye_detect_init(void)
{
    s_state       = ST_OPEN;
    s_state_start = 0;
}

void eye_detect_feed(float H, uint32_t len, uint32_t tick_ms)
{
    switch (s_state) {

    case ST_OPEN:
        if (is_closed_candidate(H, len)) {
            s_state       = ST_MAYBE_CLOSED;
            s_state_start = tick_ms;
        }
        break;

    case ST_MAYBE_CLOSED:
        if (is_open_confirmed(H, len)) {
            /* H 和 len 都回来了, 判定为眨眼, 直接返回 OPEN */
            s_state       = ST_OPEN;
            s_state_start = tick_ms;
        } else if ((tick_ms - s_state_start) >= EYE_T_CLOSE_MS) {
            /* 持续够久, 确认闭眼 */
            s_state       = ST_CLOSED;
            s_state_start = tick_ms;
        }
        /* 否则继续等 */
        break;

    case ST_CLOSED:
        if (is_open_confirmed(H, len)) {
            /* 两指标都抬头, 进入观察态, 但暂不撤销提示 */
            s_state       = ST_MAYBE_OPEN;
            s_state_start = tick_ms;
        }
        break;

    case ST_MAYBE_OPEN:
        if (!is_open_confirmed(H, len)) {
            /* 又有指标掉回去了, 保持 CLOSED */
            s_state       = ST_CLOSED;
            s_state_start = tick_ms;
        } else if ((tick_ms - s_state_start) >= EYE_T_OPEN_MS) {
            /* 睁眼稳定, 回 OPEN, 清提示 */
            s_state       = ST_OPEN;
            s_state_start = tick_ms;
        }
        break;
    }
}

bool eye_detect_is_closed(void)
{
    /* CLOSED 和 MAYBE_OPEN 都仍然显示提示 (提示 UI 上未清除).
     * 严格意义上的"确认闭眼"就是 ST_CLOSED, MAYBE_OPEN 保留提示是为了
     * 让用户看到时序上的完整闭眼窗, 而不是一 H 抬头就消失. */
    return (s_state == ST_CLOSED) || (s_state == ST_MAYBE_OPEN);
}

/* include ------------------------------------------------------------------ */
#include "eos_led.h"
#include "eventos.h"
#include "event_def.h"
#include <stdio.h>

#if (EOS_USE_SM_MODE != 0)
/* data structure ----------------------------------------------------------- */
/* LED 状态机示例。
 * 这个 Actor 使用 Event_Time_500ms 在两个稳定状态之间往返迁移。 */
typedef struct eos_sm_led_tag {
    eos_sm_t super;

    /* 逻辑亮灭标志:
     * 0 表示灭，1 表示亮。当前示例未直接操作硬件 GPIO。 */
    eos_u8_t status;
} eos_sm_led_t;

eos_sm_led_t sm_led;

/* static state function ---------------------------------------------------- */
/* 初始伪状态:
 * 只负责订阅事件、启动周期定时器，然后无条件迁移到真正的初始状态。 */
static eos_ret_t state_init(eos_sm_led_t * const me, eos_event_t const * const e);
/* 亮灯状态 */
static eos_ret_t state_on(eos_sm_led_t * const me, eos_event_t const * const e);
/* 灭灯状态 */
static eos_ret_t state_off(eos_sm_led_t * const me, eos_event_t const * const e);

/* api ---------------------------------------------------- */
void eos_sm_led_init(void)
{
    /* 优先级 1，高于 reactor 示例的优先级 2。 */
    eos_sm_init(&sm_led.super, 1, EOS_NULL);
    /* 启动后，框架会先调用 state_init(Event_Null)，
     * 要求其返回 EOS_Ret_Tran，从而进入真正的初始稳定状态。 */
    eos_sm_start(&sm_led.super, EOS_STATE_CAST(state_init));

    /* 默认值仅用于上电初始化，真正稳定值以后续 Enter 动作为准。 */
    sm_led.status = 0;
}

/* static state function ---------------------------------------------------- */
static eos_ret_t state_init(eos_sm_led_t * const me, eos_event_t const * const e)
{
    (void)e;

#if (EOS_USE_PUB_SUB != 0)
    /* 只订阅 500ms 周期事件。 */
    EOS_EVENT_SUB(Event_Time_500ms);
#endif
    /* 发布 500ms 周期事件。
     * 之后框架会周期性地向本状态机投递 Event_Time_500ms。 */
    eos_event_pub_period(Event_Time_500ms, 500);

    /* 手工走一遍初始迁移:
     * state_init --(Event_Null)--> state_off */
    return EOS_TRAN(state_off);
}

static eos_ret_t state_on(eos_sm_led_t * const me, eos_event_t const * const e)
{
    switch (e->topic) {
        case Event_Enter:
            /* 进入亮灯状态时设置逻辑输出。 */
            me->status = 1;
            return EOS_Ret_Handled;

        case Event_Exit:
            /* 当前示例离开亮灯状态时没有额外资源要释放。 */
            return EOS_Ret_Handled;

        case Event_Time_500ms:
            /* 状态迁移:
             * state_on --(Event_Time_500ms)--> state_off */
            return EOS_TRAN(state_off);

        default:
            /* 本示例没有父状态层级，未处理事件直接上交顶层伪状态。 */
            return EOS_SUPER(eos_state_top);
    }
}

static eos_ret_t state_off(eos_sm_led_t * const me, eos_event_t const * const e)
{
    switch (e->topic) {
        case Event_Enter:
            /* 进入灭灯状态时设置逻辑输出。 */
            me->status = 0;
            return EOS_Ret_Handled;

        case Event_Exit:
            /* 当前示例离开灭灯状态时没有额外资源要释放。 */
            return EOS_Ret_Handled;

        case Event_Time_500ms:
            /* 状态迁移:
             * state_off --(Event_Time_500ms)--> state_on */
            return EOS_TRAN(state_on);

        default:
            /* 本示例没有父状态层级，未处理事件直接上交顶层伪状态。 */
            return EOS_SUPER(eos_state_top);
    }
}
#endif

/* include ------------------------------------------------------------------ */
#include "eos_led.h"
#include "eventos.h"
#include "event_def.h"
#include <stdio.h>

/* data structure ----------------------------------------------------------- */
/* Reactor 示例。
 * 与状态机 Actor 不同，Reactor 没有显式状态，只在回调里直接处理事件。 */
typedef struct eos_reactor_led_tag {
    eos_reactor_t super;

    /* 逻辑亮灭标志:
     * 0 表示灭，1 表示亮。 */
    eos_u8_t status;
} eos_reactor_led_t;

eos_reactor_led_t actor_led;

/* static event handler ----------------------------------------------------- */
/* Reactor 模式下所有事件都进入这个统一回调。 */
static void led_e_handler(eos_reactor_led_t * const me, eos_event_t const * const e);

/* api ---------------------------------------------------- */
void eos_reactor_led_init(void)
{
    /* 优先级 2，低于状态机示例的优先级 1。 */
    eos_reactor_init(&actor_led.super, 2, EOS_NULL);
    eos_reactor_start(&actor_led.super, EOS_HANDLER_CAST(led_e_handler));

    actor_led.status = 0;

#if (EOS_USE_PUB_SUB != 0)
    /* 只订阅 1000ms 周期事件。 */
    eos_event_sub((eos_actor_t *)(&actor_led), Event_Time_1000ms);
#endif
#if (EOS_USE_TIME_EVENT != 0)
    /* 发布 1000ms 周期事件。 */
    eos_event_pub_period(Event_Time_1000ms, 1000);
#endif
}

/* static event handler ----------------------------------------------------- */
static void led_e_handler(eos_reactor_led_t * const me, eos_event_t const * const e)
{
    if (e->topic == Event_Time_1000ms) {
        /* Reactor 不做状态迁移，只在收到事件后直接翻转业务变量。 */
        me->status = (me->status == 0) ? 1 : 0;
    }
}

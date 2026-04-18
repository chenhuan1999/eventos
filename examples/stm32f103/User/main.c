/* include ------------------------------------------------------------------ */
#include "stm32f10x.h"
#include "eventos.h"                                // EventOS Nano 头文件
#include "event_def.h"                              // 用户事件主题定义
#include "eos_led.h"                                // LED 示例 Actor 声明

/* define ------------------------------------------------------------------- */
#if (EOS_USE_PUB_SUB != 0)
/* 订阅表由应用层提供。
 * 下标是 topic，表项内容是“哪些 Actor 订阅了这个 topic”的位图。 */
static eos_u32_t eos_sub_table[Event_Max];
#endif

/* main function ------------------------------------------------------------ */
int main(void)
{
    /* 配置 1ms SysTick 中断。
     * 需要和 eventos_config.h 中的 EOS_TICK_MS=1 保持一致。 */
    if (SysTick_Config(SystemCoreClock / 1000) != 0)
        while (1);

    /* 初始化 EventOS 框架内核。 */
    eos_init();
#if (EOS_USE_PUB_SUB != 0)
    /* 初始化订阅表，后续 topic 分发都依赖它。 */
    eos_sub_init(eos_sub_table, Event_Max);
#endif

#if (EOS_USE_SM_MODE != 0)
    /* 初始化 LED 状态机示例。
     * 它会订阅 500ms 事件，并在 on/off 两个状态之间迁移。 */
    eos_sm_led_init();
#endif
    /* 初始化 LED Reactor 示例。
     * 它会订阅 1000ms 事件，并在回调中直接翻转 status。 */
    eos_reactor_led_init();

    /* 启动 EventOS 主循环，之后不会再返回。 */
    eos_run();

    return 0;
}

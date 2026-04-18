#include "eventos.h"
#include "rtt/SEGGER_RTT.h"

/* 进入临界区:
 * EventOS 在修改 heap、事件队列、定时器表时会调用此接口。 */
void eos_port_critical_enter(void)
{
    __disable_irq();
}

/* 退出临界区 */
void eos_port_critical_exit(void)
{
    __enable_irq();
}

/* 保存最近一次断言的错误行号，便于调试时读取。 */
eos_u32_t eos_error_id = 0;
void eos_port_assert(eos_u32_t error_id)
{
    /* 当前移植层使用 SEGGER RTT 输出断言信息。 */
    SEGGER_RTT_printf(0, "------------------------------------\n");
    SEGGER_RTT_printf(0, "ASSERT >>> Module: EventOS Nano, ErrorId: %d.\n", error_id);
    SEGGER_RTT_printf(0, "------------------------------------\n");

    eos_error_id = error_id;

    /* 断言失败后停机，等待调试器介入。 */
    while (1) {
    }
}

void eos_hook_idle(void)
{
    /* 空闲钩子:
     * 当前示例未做低功耗或后台处理。 */
}

void eos_hook_start(void)
{
    /* 框架启动钩子:
     * 当前示例未使用，可扩展为打印日志或初始化外设。 */
}

void eos_hook_stop(void)
{
    /* 框架停止钩子:
     * 当前示例未使用，可扩展为关闭外设或记录日志。 */
}

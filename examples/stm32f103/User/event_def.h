
#ifndef EVENT_DEF_H__
#define EVENT_DEF_H__

#include "eventos.h"

/* 用户事件主题定义。
 * Event_User 之前的主题编号由 EventOS 框架保留。 */
enum {
    /* 预留测试事件，当前示例未使用。 */
    Event_Test = Event_User,
    /* 500ms 周期事件，供 LED 状态机示例使用。 */
    Event_Time_500ms,
    /* 1000ms 周期事件，供 LED Reactor 示例使用。 */
    Event_Time_1000ms,

    /* 用户事件结束标记，当前示例未直接使用。 */
    Event_ActEnd,

    /* 事件总数，用于初始化订阅表。 */
    Event_Max
};

#endif

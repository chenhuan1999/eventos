/*
 * EventOS Nano
 * Copyright (c) 2021, EventOS Team, <event-os@outlook.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the 'Software'), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
 * OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
 * IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.event-os.cn
 * https://github.com/event-os/eventos-nano
 * https://gitee.com/event-os/eventos-nano
 *
 * Change Logs:
 * Date           Author        Notes
 * 2021-11-23     DogMing       V0.0.2
 * 2021-11-23     DogMing       V0.1.4 Fix heap bug.
 */

// include ---------------------------------------------------------------------
#include "eventos.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 断言实现 --------------------------------------------------------------
 *
 * 断言是开发阶段的重要调试工具，用于检测不可能发生的条件。
 * 当断言失败时，系统会停止并报告错误位置。
 *
 * 设计考虑：
 * - 即使关闭断言，宏仍然存在（只是变成空操作），便于代码保留断言点
 * - 失败时调用eos_hook_stop()停止一切中断和调度，防止错误扩大
 * - 进入临界区确保错误报告的原子性，不被其他事件干扰
 */
#if (EOS_USE_ASSERT != 0)
#define EOS_ASSERT(test_) do { if (!(test_)) {                                 \
        eos_hook_stop();                                                       \
        eos_port_critical_enter();                                             \
        eos_port_assert(__LINE__);                                             \
    } } while (0)
#else
/** @brief 断言关闭时为空操作，不产生任何代码 */
#define EOS_ASSERT(test_)               ((void)0)
#endif

// 框架内部定义 ------------------------------------------------------------------

/**
 * @brief Actor运行模式枚举
 *
 * 区分Reactor模式和状态机模式：
 * - Reactor: 简单的事件处理回调，没有状态概念
 * - StateMachine: 基于状态的事件处理，支持状态转换
 *
 * 设计原因：同一个Actor基类可以支持两种不同的处理模式，
 * 通过mode字段区分，框架根据mode选择不同的处理逻辑。
 */
enum eos_actor_mode {
    EOS_Mode_Reactor = 0,        /**< 反应式模式，只处理事件 */
    EOS_Mode_StateMachine = !EOS_Mode_Reactor  /**< 状态机模式，支持状态转换 */
};

// 框架内部错误码定义 ----------------------------------------------------------
/**
 * @brief 框架运行返回值
 *
 * 这些返回值用于诊断框架运行状态。
 * 正值表示正常流程返回，负值表示错误。
 *
 * 设计思路：区分不同类型的"无事件可处理"情况，
 * 便于上层代码判断是真的没有事件，还是出了错误。
 */
enum {
    EosRun_OK                               = 0,    /**< 正常运行 */
    EosRun_NotEnabled,                      /**< 框架未启用 */
    EosRun_NoEvent,                         /**< 没有事件 */
    EosRun_NoActor,                         /**< 没有注册的Actor */
    EosRun_NoActorSub,                      /**< 没有Actor订阅当前事件 */

    // 定时器相关返回值
    EosTimer_Empty,                         /**< 定时器队列为空 */
    EosTimer_NotTimeout,                   /**< 定时器未到期 */
    EosTimer_ChangeToEmpty,                 /**< 定时器队列变为空 */

    // 错误码（负值）
    EosRunErr_NotInitEnd                    = -1,   /**< 初始化未完成 */
    EosRunErr_ActorNotSub                   = -2,   /**< Actor未订阅此事件 */
    EosRunErr_MallocFail                    = -3,   /**< 内存分配失败 */
    EosRunErr_SubTableNull                  = -4,   /**< 订阅表未初始化 */
    EosRunErr_InvalidEventData              = -5,   /**< 无效的事件数据 */
    EosRunErr_HeapMemoryNotEnough           = -6,   /**< 堆内存不足 */
    EosRunErr_TimerRepeated                 = -7,   /**< 定时器重复 */
};

/**
 * @brief 魔术数字
 *
 * 用于内存覆盖检测的标记值。
 * 当写入关键数据结构时写入此值，运行时会检查是否被破坏。
 *
 * 选择0xDEADBEEF的原因：
 * - 易于识别（在十六进制 dump 中显眼）
 * - 不太可能在正常数据中巧合出现
 */
#define EOS_MAGIC_NUMBER                    0xDEADBEEF

/* 时间事件相关定义 ----------------------------------------------------------
 *
 * 时间事件（延时/周期事件）的实现使用软件定时器模拟。
 * 系统时间由eos_tick()驱动，定时器检查是否到期。
 *
 * 设计考虑：
 * - 不依赖硬件定时器中断，可在没有定时器外设的MCU上使用
 * - 使用链表管理多个定时器，定时器数量可配置
 * - 支持不同精度的时间单位，自动选择合适的计时粒度
 */

#if (EOS_USE_TIME_EVENT != 0)
/** @brief 30天的毫秒数，用于时间溢出计算
 *
 * 系统时间使用32位无符号整数，最大值为0xFFFFFFFF。
 * 约等于49.7天。定义30天的毫秒数用于时间溢出时的处理。
 */
#define EOS_MS_NUM_30DAY                    (2592000000)

/**
 * @brief 定时器精度等级
 *
 * 根据延时时长自动选择合适的计时精度：
 * - EosTimerUnit_Ms: 毫秒级精度，最精细，但最大时长有限（60秒）
 * - EosTimerUnit_100Ms: 百毫秒级，适合几分钟的延时
 * - EosTimerUnit_Sec: 秒级，适合几十分钟的延时
 * - EosTimerUnit_Minute: 分钟级，适合几小时甚至几天
 *
 * 设计原因：避免为长延时事件频繁检查计时器，提高效率。
 */
enum {
    EosTimerUnit_Ms                         = 0,    /**< 毫秒级：最大60秒 */
    EosTimerUnit_100Ms,                             /**< 百毫秒级：最大约100分钟 */
    EosTimerUnit_Sec,                               /**< 秒级：最大约16小时 */
    EosTimerUnit_Minute,                            /**< 分钟级：最大约15天 */
    EosTimerUnit_Max
};

/**
 * @brief 各精度等级的时间阈值
 *
 * 当延时超过阈值时，需要使用更粗的精度等级。
 * 例如：延时70秒应使用秒级而非毫秒级。
 */
static const eos_u32_t timer_threshold[EosTimerUnit_Max] = {
    60000,                                          // 60 S
    6000000,                                        // 100 Minutes
    57600000,                                       // 16 hours
    1296000000,                                     // 15 days
};

/**
 * @brief 各精度等级的最小时间单位
 *
 * 用于计算周期事件的实际触发间隔。
 * 例如：周期事件period=5，unit=Sec时，实际周期为5秒。
 */
static const eos_u32_t timer_unit[EosTimerUnit_Max] = {
    1, 100, 1000, 60000
};

/**
 * @brief 时间事件结构体
 *
 * 存储单个定时器事件的所有信息。
 * 使用位域压缩存储，节省内存。
 *
 * 设计亮点：
 * - topic: 13位，最大支持8192个不同主题
 * - oneshoot: 1位，标记是否是一次性事件
 * - unit: 2位，选择时间精度
 * - period: 16位，周期值
 */
typedef struct eos_event_timer {
    eos_u32_t topic                         : 13;   /**< 事件主题 */
    eos_u32_t oneshoot                      : 1;    /**< 是否一次性（True=一次性，False=周期） */
    eos_u32_t unit                          : 2;    /**< 时间单位 */
    eos_u32_t period                        : 16;   /**< 周期值（实际时间=period*timer_unit） */
    eos_u32_t timeout_ms;                          /**< 到期时间点（绝对时间） */
} eos_event_timer_t;
#endif

/**
 * @brief 堆内存块结构体
 *
 * 管理堆内存分配的基本单位。
 * 每个块包含：前向/后向链表指针、队列链表指针、大小、状态等。
 *
 * 设计考虑：
 * - next/last: 用于空闲块链表管理（内存合并）
 * - q_next/q_last: 用于已分配块队列管理（事件队列）
 * - free: 标记块是否空闲
 * - offset: 处理对齐问题，记录填充字节数
 */
typedef struct eos_block {
    // word[0]
    eos_u32_t next                          : 15;   /**< 下一个空闲块偏移（链表） */
    eos_u32_t q_next                        : 15;   /**< 队列中下一个块偏移 */
    // word[1]
    eos_u32_t last                          : 15;   /**< 上一个空闲块偏移（链表） */
    eos_u32_t q_last                        : 15;   /**< 队列中上一个块偏移 */
    eos_u32_t free                          : 1;    /**< 是否空闲：1=空闲，0=已分配 */
    // word[2]
    eos_u16_t size                          : 15;   /**< 数据区大小（不含块头） */
    eos_u32_t offset                        : 8;    /**< 对齐填充字节数 */
} eos_block_t;

/**
 * @brief 事件内部结构体
 *
 * 存储在堆中的事件数据结构。
 * 包含事件订阅信息和主题，紧跟在块头之后。
 *
 * sub字段用于快速判断哪些Actor订阅了该事件，
 * 无需遍历订阅表即可确定事件的目标。
 */
typedef struct eos_event_inner {
    eos_sub_t sub;                           /**< 订阅此事件的Actor位掩码 */
    eos_topic_t topic;                      /**< 事件主题 */
} eos_event_inner_t;

/**
 * @brief 堆内存管理结构体
 *
 * 管理整个事件堆的元数据。
 *
 * 设计亮点：
 * - data: 堆数据区，实际的事件存储在这里
 * - queue: 指向事件队列头（按时间/优先级排序）
 * - current: 当前处理的块位置
 * - sub_general: 所有待处理事件的订阅位掩码"或"结果
 *
 * 使用位掩码追踪有哪些Actor有待处理事件，
 * 事件分发时快速跳过没有事件的Actor。
 */
typedef struct eos_heap {
#if (EOS_USE_MAGIC != 0)
    eos_u32_t magic;                         /**< 魔术数字，检测覆盖 */
#endif
    eos_u8_t data[EOS_SIZE_HEAP];           /**< 堆数据区 */
    // word[0]
    eos_u32_t size                          : 15;       /**< 堆总大小 */
    eos_u32_t queue                         : 15;       /**< 事件队列头索引 */
    eos_u32_t error_id                      : 2;        /**< 错误状态 */
    // word[2]
    eos_u32_t current                       : 15;       /**< 当前块位置 */
    eos_u32_t empty                         : 1;        /**< 堆是否为空 */
    // word[3]
    eos_sub_t sub_general;                          /**< 全局订阅标志（所有事件订阅的或） */
    eos_sub_t count;                                /**< 块计数器 */
} eos_heap_t;

/**
 * @brief EventOS框架主结构体
 *
 * 这是框架的核心数据结构，保存所有运行时状态。
 * 设计为单例模式，整个系统只有一个实例。
 *
 * 组成：
 * - magic: 魔术数字（可选）
 * - sub_table: 订阅表指针
 * - actors: Actor注册表（数组）
 * - heap: 事件堆（如果启用）
 * - etimer: 定时器数组（如果启用）
 * - time: 系统时间
 * - enabled/running/init_end: 状态标志
 */
typedef struct eos_tag {
#if (EOS_USE_MAGIC != 0)
    eos_u32_t magic;                         /**< 框架魔术数字 */
#endif
#if (EOS_USE_PUB_SUB != 0)
    eos_mcu_t *sub_table;                   /**< 事件订阅表（用户分配） */
#endif

    eos_mcu_t actor_exist;                   /**< 已注册的Actor位掩码 */
    eos_mcu_t actor_enabled;                 /**< 已启用的Actor位掩码 */
    eos_actor_t * actor[EOS_MAX_ACTORS];    /**< Actor指针数组 */

#if (EOS_USE_EVENT_DATA != 0)
    eos_heap_t heap;                        /**< 事件堆 */
#endif

#if (EOS_USE_TIME_EVENT != 0)
    eos_event_timer_t etimer[EOS_MAX_TIME_EVENT];  /**< 时间事件数组 */
    eos_u32_t time;                         /**< 系统时间（毫秒） */
    eos_u32_t timeout_min;                  /**< 最近的定时器到期时间 */
    eos_u8_t timer_count;                   /**< 当前定时器数量 */
#endif

    eos_u8_t enabled                        : 1;   /**< 框架是否启用 */
    eos_u8_t running                        : 1;   /**< 框架是否正在运行 */
    eos_u8_t init_end                       : 1;   /**< 初始化是否完成 */
} eos_t;

/* 框架测试API --------------------------------------------------------------
 *
 * 这些API主要用于单元测试，不属于公共API。
 * 允许测试代码访问框架内部状态。
 */
eos_s8_t eos_once(void);
eos_s8_t eos_event_pub_ret(eos_topic_t topic, void *data, eos_u32_t size);
void * eos_get_framework(void);
void eos_event_pub_time(eos_topic_t topic, eos_u32_t time_ms, eos_bool_t oneshoot);
void eos_set_time(eos_u32_t time_ms);
// **eos end** -----------------------------------------------------------------

/** @brief 框架全局实例（单例） */
static eos_t eos;

/* 事件表定义 ----------------------------------------------------------
 *
 * 预定义框架保留事件的表。
 * 这些事件用于状态机的状态转换触发。
 */
#if (EOS_USE_SM_MODE != 0)
static const eos_event_t eos_event_table[Event_User] = {
    {Event_Null, 0},        /**< 空事件：查询当前状态 */
    {Event_Enter, 0},       /**< 进入事件：状态入口 */
    {Event_Exit, 0},       /**< 退出事件：状态出口 */
#if (EOS_USE_HSM_MODE != 0)
    {Event_Init, 0},        /**< 初始化事件：HSM特有 */
#endif
};
#endif

/* 宏定义 --------------------------------------------------------------
 *
 * 层次状态机(HSM)的触发宏。
 * 用于在HSM中调用状态处理函数。
 *
 * 设计原因：HSM的状态转换需要调用状态函数并传递特殊的事件。
 * 这个宏简化了调用语法，使得状态转换逻辑更清晰。
 */
#if (EOS_USE_SM_MODE != 0)
#define HSM_TRIG_(state_, topic_)                                              \
    ((*(state_))(me, &eos_event_table[topic_]))
#endif

// 静态函数声明 -------------------------------------------------------------
#if (EOS_USE_SM_MODE != 0)
static void eos_sm_dispath(eos_sm_t * const me, eos_event_t const * const e);
#if (EOS_USE_HSM_MODE != 0)
static eos_s32_t eos_sm_tran(eos_sm_t * const me, eos_state_handler path[EOS_MAX_HSM_NEST_DEPTH]);
#endif
#endif
#if (EOS_USE_EVENT_DATA != 0)
void eos_heap_init(eos_heap_t * const me);
void * eos_heap_malloc(eos_heap_t * const me, eos_u32_t size);
void eos_heap_free(eos_heap_t * const me, void * data);
void *eos_heap_get_block(eos_heap_t * const me, eos_u8_t priority);
void eos_heap_gc(eos_heap_t * const me, void *data);
#endif

// 框架核心实现 ==============================================================

/**
 * @brief 清除框架运行时状态
 *
 * 在停止或重新初始化时调用，重置动态状态。
 */
static void eos_clear(void)
{
#if (EOS_USE_TIME_EVENT != 0)
    eos.timer_count = 0;
#endif
}

/**
 * @brief 框架初始化
 *
 * 必须是最先调用的函数，在任何Actor初始化之前。
 *
 * 初始化内容：
 * 1. 清除运行时数据
 * 2. 设置魔术数字（如果启用）
 * 3. 设置初始状态为启用但未运行
 * 4. 清空Actor注册表
 * 5. 初始化堆内存
 * 6. 设置init_end标志
 *
 * 注意：初始化后框架并未运行，需要调用eos_run()才进入事件循环。
 */
void eos_init(void)
{
    /* 先清掉运行期状态，例如计时器数量这类“上电后会变化”的字段。 */
    eos_clear();

#if (EOS_USE_MAGIC != 0)
    /* 如果启用了 magic number，就先给框架单例打上完整性标记。 */
    eos.magic = EOS_MAGIC_NUMBER;
#endif
    /* 框架初始化完成后默认处于 enabled，但此时还没有进入调度循环。 */
    eos.enabled = EOS_True;
    eos.running = EOS_False;
    /* 此时还没有任何 actor 被注册，也没有 actor 被启动。 */
    eos.actor_exist = 0;
    eos.actor_enabled = 0;
#if (EOS_USE_PUB_SUB != 0)
    /* 订阅表由用户后续通过 eos_sub_init() 提供，这里先置空。 */
    eos.sub_table = EOS_NULL;
#endif

#if (EOS_USE_EVENT_DATA != 0)
    /* 初始化事件堆，后面所有事件实例都会进入这个内部队列。 */
    eos_heap_init(&eos.heap);
#endif

    /* 标记初始化结束，后面 eos_once()/发布事件 时都会检查这个标志。 */
    eos.init_end = 1;
#if (EOS_USE_TIME_EVENT != 0)
    /* 软件时基从 0 开始累计。 */
    eos.time = 0;
#endif
}

/**
 * @brief 订阅表初始化
 *
 * 初始化发布-订阅机制的订阅标志表。
 *
 * @param flag_sub 指向订阅表的指针（用户分配）
 * @param topic_max 最大的主题数量
 *
 * 设计理由：订阅表由用户分配内存，框架只负责使用。
 * 这样用户可以选择：
 * - 静态分配：放在全局变量区
 * - 栈分配：临时使用
 * - 动态分配：按需创建和释放
 */
#if (EOS_USE_PUB_SUB != 0)
void eos_sub_init(eos_mcu_t *flag_sub, eos_topic_t topic_max)
{
    /* 保存用户提供的订阅位图表地址。 */
    eos.sub_table = flag_sub;
    /* 初始化时每个 topic 默认都没有订阅者。 */
    for (int i = 0; i < topic_max; i ++) {
        eos.sub_table[i] = 0;
    }
}
#endif

/* 时间事件处理 ================================================================
 *
 * 时间事件的处理流程：
 * 1. 应用程序发布延时/周期事件（eos_event_pub_delay/period）
 * 2. 事件被加入定时器数组，等待到期
 * 3. 每次eos_tick()更新系统时间
 * 4. eos_once()中调用eos_evttimer()检查到期事件
 * 5. 到期事件被发布到事件队列，转换为普通事件处理
 *
 * 设计亮点：
 * - 使用绝对时间（timeout_ms）而非相对时间，便于处理溢出
 * - 自动选择时间精度，减少不必要的检查开销
 */

/**
 * @brief 检查并处理到期的时间事件
 *
 * 在eos_once()的每次调用中检查定时器队列。
 * 将到期的定时器事件转换为普通事件发布。
 *
 * @return 处理结果码
 *
 * 处理逻辑：
 * 1. 如果定时器队列为空，返回EosTimer_Empty
 * 2. 如果当前时间未到最近到期时间，返回EosTimer_NotTimeout
 * 3. 遍历所有定时器，到期的：
 *    - 一次性事件：从队列移除
 *    - 周期事件：更新timeout_ms继续等待
 * 4. 重新计算timeout_min
 */
#if (EOS_USE_TIME_EVENT != 0)
eos_s32_t eos_evttimer(void)
{
    // 获取当前时间，检查延时事件队列
    eos_u32_t system_time = eos.time;

    if (eos.etimer[0].topic == Event_Null)
        return EosTimer_Empty;

    // 时间未到达
    if (system_time < eos.timeout_min)
        return EosTimer_NotTimeout;

    // 若时间到达，将此事件推入事件队列，同时在etimer里删除。
    for (eos_u32_t i = 0; i < eos.timer_count; i ++) {
        if (eos.etimer[i].timeout_ms > system_time)
            continue;
        eos_event_pub_topic(eos.etimer[i].topic);

        // 清零标志位
        if (eos.etimer[i].oneshoot == EOS_True) {
            if (i == (eos.timer_count - 1)) {
                eos.timer_count -= 1;
                break;
            }
            eos.etimer[i] = eos.etimer[eos.timer_count - 1];
            eos.timer_count -= 1;
            i --;
        }
        else {
            eos_u32_t period = eos.etimer[i].period * timer_unit[eos.etimer[i].unit];
            eos.etimer[i].timeout_ms += period;
        }
    }
    if (eos.timer_count == 0) {
        eos.timeout_min = EOS_U32_MAX;
        return EosTimer_ChangeToEmpty;
    }

    // 寻找到最小的时间定时器
    eos_u32_t min_time_out_ms = EOS_U32_MAX;
    for (eos_u32_t i = 0; i < eos.timer_count; i ++) {
        if (min_time_out_ms <= eos.etimer[i].timeout_ms)
            continue;
        min_time_out_ms = eos.etimer[i].timeout_ms;
    }
    eos.timeout_min = min_time_out_ms;

    return EosRun_OK;
}
#endif

/**
 * @brief 处理一个事件（单步执行）
 *
 * 框架的主处理函数，每次调用处理一个事件。
 * 被eos_run()中的循环反复调用。
 *
 * @return 处理结果，>=0表示正常，<0表示错误
 *
 * 处理流程：
 * 1. 检查初始化状态
 * 2. 检查订阅表是否初始化
 * 3. 检查框架是否启用
 * 4. 检查是否有注册的Actor
 * 5. 检查时间事件（如果有启用）
 * 6. 检查是否有待处理事件
 * 7. 找到优先级最高的待处理事件
 * 8. 提取事件并分发到对应的Actor
 * 9. 事件处理完毕后释放事件内存
 *
 * 设计要点：
 * - 返回值区分不同情况，上层可以根据返回值决定行为
 * - 事件按优先级处理，高优先级Actor的事件先处理
 * - 使用临界区保护堆操作，防止数据竞争
 */
eos_s8_t eos_once(void)
{
    /* 没做 eos_init() 就不能进入调度。 */
    if (eos.init_end == 0) {
        return (eos_s8_t)EosRunErr_NotInitEnd;
    }

#if (EOS_USE_PUB_SUB != 0)
    /* 发布订阅模式下，订阅表必须已经准备好。 */
    if (eos.sub_table == EOS_NULL) {
        return (eos_s8_t)EosRunErr_SubTableNull;
    }
#endif

    /* 如果外部调用了 eos_stop()，这里会感知到并退出主循环。 */
    if (eos.enabled == EOS_False) {
        eos_clear();
        return (eos_s8_t)EosRun_NotEnabled;
    }

    // 检查是否有状态机的注册
    /* 至少要有一个已注册 actor，且至少一个 actor 已经 start。 */
    if (eos.actor_exist == 0 || eos.actor_enabled == 0) {
        return (eos_s8_t)EosRun_NoActor;
    }

#if (EOS_USE_TIME_EVENT != 0)
    /* 先把已经到期的时间事件转成普通事件。 */
    eos_evttimer();
#endif

    /* 事件堆为空，说明这一轮没有事情可做。 */
    if (eos.heap.empty == EOS_True) {
        return (eos_s8_t)EosRun_NoEvent;
    }

    // 寻找到优先级最高，且有事件需要处理的Actor
    /* 从高优先级往低优先级找：谁当前既存在，又确实有待处理事件。 */
    eos_actor_t *actor = eos.actor[0];
    eos_u8_t priority = EOS_MAX_ACTORS;
    for (eos_s8_t i = (eos_s8_t)(EOS_MAX_ACTORS - 1); i >= 0; i --) {
        if ((eos.actor_exist & (1 << i)) == 0)
            continue;
        if ((eos.heap.sub_general & (1 << i)) == 0)
            continue;
        actor = eos.actor[i];
        priority = i;
        break;
    }
    // 如果没有找到，返回
    /* 订阅位图显示有活，但最终没定位到 actor，返回异常状态。 */
    if (priority == EOS_MAX_ACTORS) {
        return (eos_s8_t)EosRun_NoActorSub;
    }

    // 寻找当前Actor的最老的事件
    /* 取出“属于当前 actor 的最老事件”，同时清掉它的订阅位。 */
    eos_port_critical_enter();
    eos_event_inner_t * e = eos_heap_get_block(&eos.heap, priority);
    EOS_ASSERT(e != EOS_NULL);

    eos_port_critical_exit();
    /* 对外暴露的是 eos_event_t，内部事件头要在这里做一次转换。 */
    eos_event_t event;
    event.topic = e->topic;
    event.data = (void *)((eos_pointer_t)e + sizeof(eos_event_inner_t));
    eos_block_t *block = (eos_block_t *)((eos_pointer_t)e - sizeof(eos_block_t));
    event.size = block->size - block->offset - sizeof(eos_event_inner_t);

    // 对事件进行执行
#if (EOS_USE_PUB_SUB != 0)
    if ((eos.sub_table[e->topic] & (1 << actor->priority)) != 0)
#endif
    {
#if (EOS_USE_SM_MODE != 0)
        if (actor->mode == EOS_Mode_StateMachine) {
            /* 状态机 actor：可能在处理过程中发生状态切换。 */
            // 执行状态的转换
            eos_sm_t *sm = (eos_sm_t *)actor;
            eos_sm_dispath(sm, &event);
        }
        else
#endif
        {
            /* Reactor actor：直接执行它的事件处理函数。 */
            eos_reactor_t *reactor = (eos_reactor_t *)actor;
            reactor->event_handler(reactor, &event);
        }
    }
#if (EOS_USE_PUB_SUB != 0)
    else {
        return (eos_s8_t)EosRunErr_ActorNotSub;
    }
#endif
#if (EOS_USE_EVENT_DATA != 0)
    // 销毁过期事件与其携带的参数
    /* 事件处理结束后尝试回收。
     * 如果还有别的订阅者没消费完，这个事件块会继续保留。 */
    eos_port_critical_enter();
    eos_heap_gc(&eos.heap, e);
    eos_port_critical_exit();
#endif

    return (eos_s8_t)EosRun_OK;
}

/**
 * @brief 启动框架（进入事件循环）
 *
 * 框架的入口函数，调用后永远不会返回。
 *
 * 处理流程：
 * 1. 调用eos_hook_start()钩子
 * 2. 断言各种前置条件
 * 3. 设置running标志
 * 4. 进入主循环，调用eos_once()处理事件
 * 5. 当enabled变为false时退出循环
 * 6. 进入空闲循环，调用eos_hook_idle()
 *
 * 设计要点：
 * - 断言检查前置条件，确保系统状态正确
 * - 退出循环后进入空闲处理，不再返回
 * - 使用while(1)确保不会意外退出
 */
void eos_run(void)
{
    /* 应用层启动钩子：在正式进入调度循环前执行一次。 */
    eos_hook_start();

    /* 进入主循环前，把最基本的前置条件先断言检查一遍。 */
    EOS_ASSERT(eos.enabled == EOS_True);
#if (EOS_USE_PUB_SUB != 0)
    EOS_ASSERT(eos.sub_table != 0);
#endif
#if (EOS_USE_EVENT_DATA != 0 && EOS_USE_HEAP != 0)
    EOS_ASSERT(eos.heap.size != 0);
#endif

    /* 从这里开始，框架已经真正进入运行态。 */
    eos.running = EOS_True;

    /* 主循环：每次循环最多处理一个事件。 */
    while (eos.enabled) {
        eos_s8_t ret = eos_once();
        /* ret < 0 表示框架内部错误。 */
        EOS_ASSERT(ret >= 0);

        /* eos_once() 发现框架被停止，请求离开主循环。 */
        if (ret == EosRun_NotEnabled) {
            break;
        }

        /* 没有 actor 或没有事件时，不算错误，而是进入 idle 钩子。 */
        if (ret == EosRun_NoActor || ret == EosRun_NoEvent) {
#if (EOS_USE_MAGIC != 0)
            EOS_ASSERT(eos.heap.magic == EOS_MAGIC_NUMBER);
            EOS_ASSERT(eos.magic == EOS_MAGIC_NUMBER);
            for (eos_u8_t i = 0; i < EOS_MAX_ACTORS; i ++) {
                if ((eos.actor_exist & (1 << i)) != 0) {
                    EOS_ASSERT(eos.actor[i]->magic == EOS_MAGIC_NUMBER);
                }
            }
#endif
            eos_hook_idle();
        }
    }

    /* EventOS Nano 不返回到调用者；停止后永久停在 idle。 */
    while (1) {
        eos_hook_idle();
    }
}

/**
 * @brief 停止框架
 *
 * 请求框架停止运行。
 * 设置enabled为false，下一次eos_once()循环时会检测到并退出。
 *
 * 注意：这只是请求，不是立即停止。
 * 框架会处理完当前事件后才退出循环。
 */
void eos_stop(void)
{
    /* 先发出“停止请求”，让 eos_run() 在下一轮感知到。 */
    eos.enabled = EOS_False;
    /* 再立即回调 stop 钩子，给应用层机会关硬件、拉告警。 */
    eos_hook_stop();
}

/* 时间管理 ================================================================
 *
 * 系统时间由eos_tick()驱动，每次调用增加EOS_TICK_MS毫秒。
 * 时间事件基于这个时间进行计算。
 */

/**
 * @brief 获取系统当前时间
 *
 * @return 系统启动以来的毫秒数
 */
#if (EOS_USE_TIME_EVENT != 0)
eos_u32_t eos_time(void)
{
    /* 只读返回当前的软件系统时间。 */
    return eos.time;
}

/**
 * @brief 系统Tick更新
 *
 * 由定时器中断调用，推动系统时间前进。
 *
 * 处理逻辑：
 * 1. 时间正常增加
 * 2. 检测时间溢出回绕（接近最大值时）
 * 3. 溢出时调整所有定时器的到期时间
 *
 * 设计亮点：
 * - 使用模运算处理时间溢出
 * - 溢出时调整定时器使用户看不到时间"回绕"
 */
void eos_tick(void)
{
    /* 同时保留旧时间和新时间，用于检测软件时钟是否回绕。 */
    eos_u32_t system_time = eos.time, system_time_bkp = eos.time;
    eos_u32_t offset = EOS_MS_NUM_30DAY - 1 + EOS_TICK_MS;
    /* 每调用一次，就按配置的 tick 步长推进软件时间。 */
    system_time = ((system_time + EOS_TICK_MS) % EOS_MS_NUM_30DAY);
    if (system_time_bkp >= (EOS_MS_NUM_30DAY - EOS_TICK_MS) && system_time < EOS_TICK_MS) {
        /* 时间回绕时，要把所有绝对超时时间一起平移，保证定时器逻辑不乱。 */
        eos_port_critical_enter();
        EOS_ASSERT(eos.timeout_min >= offset);
        eos.timeout_min -= offset;
        for (eos_u32_t i = 0; i < eos.timer_count; i ++) {
            EOS_ASSERT(eos.etimer[i].timeout_ms >= offset);
            eos.etimer[i].timeout_ms -= offset;
        }
        eos_port_critical_exit();
    }
    /* 最后再提交新的系统时间。 */
    eos.time = system_time;
}
#endif

// Actor相关实现 ================================================================

/**
 * @brief Actor基类初始化
 *
 * 所有Actor（Reactor/StateMachine）的公共初始化逻辑。
 *
 * @param me       Actor指针
 * @param priority 优先级
 * @param parameter 初始化参数（未使用）
 *
 * 检查项：
 * - 框架已启用
 * - 框架未运行（防止运行时注册）
 * - 订阅表已初始化
 * - Actor指针有效
 * - 优先级未冲突
 *
 * 设计要点：
 * - 防止二次启动（re-entry guard）
 * - 使用位掩码检测优先级冲突
 * - 注册后actor_exist对应位被置1
 */
static void eos_actor_init( eos_actor_t * const me,
                            eos_u8_t priority,
                            void const * const parameter)
{
    /* 当前版本没有使用 parameter，但保留这个入口方便后续扩展。 */
    (void)parameter;

    // 框架需要先启动起来
    /* actor 只能在 eos_init() 之后、eos_run() 之前注册。 */
    EOS_ASSERT(eos.enabled == EOS_True);
    EOS_ASSERT(eos.running == EOS_False);
#if (EOS_USE_PUB_SUB != 0)
    /* 发布订阅模式下，没有订阅表就无法安全注册 actor。 */
    EOS_ASSERT(eos.sub_table != EOS_NULL);
#endif
    // 参数检查
    /* 基本参数合法性检查。 */
    EOS_ASSERT(me != (eos_actor_t *)0);
    EOS_ASSERT(priority < EOS_MAX_ACTORS);

    // 防止二次启动
    /* 防止同一个 actor 被重复初始化/重复注册。 */
    if (me->enabled == EOS_True)
        return;

    // 检查优先级的重复注册
    /* 每个优先级位只能挂一个 actor。 */
    EOS_ASSERT((eos.actor_exist & (1 << priority)) == 0);

    // 注册到框架里
    /* 把 actor 写入框架注册表。 */
    eos.actor_exist |= (1 << priority);
    eos.actor[priority] = me;
    // 状态机
    /* 把优先级保存到 actor 自身。 */
    me->priority = priority;
#if (EOS_USE_MAGIC != 0)
    me->magic = EOS_MAGIC_NUMBER;
#endif
}

/**
 * @brief Reactor初始化
 *
 * 初始化Reactor类型的Actor。
 * 调用基类初始化后设置运行模式为Reactor。
 */
void eos_reactor_init(  eos_reactor_t * const me,
                        eos_u8_t priority,
                        void const * const parameter)
{
    /* 先走公共 actor 初始化。 */
    eos_actor_init(&me->super, priority, parameter);
    /* 再标记它是 reactor 模式。 */
    me->super.mode = EOS_Mode_Reactor;
}

/**
 * @brief 启动Reactor
 *
 * 设置事件处理函数并启用Reactor。
 * 启用后，Reactor开始接收和处理事件。
 *
 * @param me            Reactor指针
 * @param event_handler 事件处理函数
 */
void eos_reactor_start(eos_reactor_t * const me, eos_event_handler event_handler)
{
    /* 保存 reactor 的事件处理入口。 */
    me->event_handler = event_handler;
    /* 启用这个 actor。 */
    me->super.enabled = EOS_True;
    /* 同步更新“已启动 actor”位图。 */
    eos.actor_enabled |= (1 << me->super.priority);
}

/* 状态机实现 ================================================================
 *
 * 状态机是EventOS的核心功能之一。
 * 支持两种模式：
 * 1. FSM（有限状态机）：简单的状态转换
 * 2. HSM（层次状态机）：支持状态嵌套和继承
 *
 * 设计亮点：
 * - 状态即函数：通过函数指针实现状态
 * - 返回值驱动：状态转换由返回值决定，而非硬编码
 * - 层次化设计（HSM）：事件可以向上传递给父状态处理
 */

/**
 * @brief 状态机初始化
 *
 * @see eos_actor_init()
 */
#if (EOS_USE_SM_MODE != 0)
void eos_sm_init(   eos_sm_t * const me,
                    eos_u8_t priority,
                    void const * const parameter)
{
    /* 先复用公共 actor 初始化逻辑。 */
    eos_actor_init(&me->super, priority, parameter);
    /* 标记该 actor 运行在状态机模式。 */
    me->super.mode = EOS_Mode_StateMachine;
    /* 启动前默认指向最顶层伪状态。 */
    me->state = eos_state_top;
}

/**
 * @brief 启动状态机
 *
 * 状态机启动流程：
 * 1. 设置初始状态
 * 2. 启用Actor
 * 3. 执行初始转换（Initial Transition）
 *
 * 对于FSM：
 * - 执行一次空事件获取目标状态
 * - 执行Exit事件（从原状态）
 * - 执行Enter事件（到目标状态）
 *
 * 对于HSM：
 * - 执行空事件获取完整状态路径
 * - 从顶层开始逐层进入子状态
 * - 递归执行Init事件直到没有转换
 *
 * 设计要点：
 * - 初始转换是自动的，无需用户手动触发
 * - HSM的层次初始化支持嵌套状态的自动进入
 */
void eos_sm_start(eos_sm_t * const me, eos_state_handler state_init)
{
#if (EOS_USE_HSM_MODE != 0)
    /* path[] 用来记录初始化进入路径。 */
    eos_state_handler path[EOS_MAX_HSM_NEST_DEPTH];
#endif
    eos_state_handler t;

    /* 先安装“初始状态处理函数”。 */
    me->state = state_init;
    /* 状态机从这里开始允许被框架调度。 */
    me->super.enabled = EOS_True;
    eos.actor_enabled |= (1 << me->super.priority);

    // 进入初始状态，执行TRAN动作。这也意味着，进入初始状态，必须无条件执行Tran动作。
    /* 初始状态并不是最终业务状态，它必须在 Event_Null 上返回一次
     * EOS_Ret_Tran，把状态机带到真正的初始叶子状态。 */
    t = me->state;
    eos_ret_t ret = t(me, &eos_event_table[Event_Null]);
    EOS_ASSERT(ret == EOS_Ret_Tran);
#if (EOS_USE_HSM_MODE == 0)
    /* 普通 FSM：做完初始跳转后，直接补一次 Event_Enter。 */
    ret = me->state(me, &eos_event_table[Event_Enter]);
    EOS_ASSERT(ret != EOS_Ret_Tran);
#else
    t = eos_state_top;
    // 由初始状态转移，引发的各层状态的进入
    // 每一个循环，都代表着一个Event_Init的执行
    /* HSM：要把父状态链一层一层补齐进入动作。 */
    eos_s32_t ip = 0;
    ret = EOS_Ret_Null;
    do {
        // 由当前层，探测需要进入的各层父状态
        /* path[0] 永远是当前目标叶子状态。 */
        path[0] = me->state;
        // 一层一层的探测，一直探测到原状态
        /* Event_Null 在 HSM 里承担“查询父状态”的作用。 */
        HSM_TRIG_(me->state, Event_Null);
        while (me->state != t) {
            ++ ip;
            EOS_ASSERT(ip < EOS_MAX_HSM_NEST_DEPTH);
            /* 逐层记录父状态路径。 */
            path[ip] = me->state;
            HSM_TRIG_(me->state, Event_Null);
        }
        /* 恢复真正的目标叶子状态。 */
        me->state = path[0];

        // 各层状态的进入
        /* 按“从父到子”的顺序补做 Event_Enter。 */
        do {
            HSM_TRIG_(path[ip --], Event_Enter);
        } while (ip >= 0);

        /* 当前最深叶子状态保存到 t。 */
        t = path[0];

        /* 如果当前状态还有默认子状态，会在 Event_Init 上继续下钻。 */
        ret = HSM_TRIG_(t, Event_Init);
    } while (ret == EOS_Ret_Tran);

    /* 初始化全部完成后，状态机稳定在最终叶子状态。 */
    me->state = t;
#endif
}
#endif

// 事件发布-订阅实现 ==========================================================

/**
 * @brief 发布事件（带返回值）
 *
 * 事件发布的核心函数，处理事件创建、内存分配、数据复制。
 *
 * @param topic 事件主题
 * @param data  数据指针
 * @param size  数据大小
 * @return 成功/失败状态
 *
 * 处理流程：
 * 1. 检查框架状态
 * 2. 检查是否有订阅者
 * 3. 进入临界区
 * 4. 分配堆内存
 * 5. 填充事件数据
 * 6. 更新全局订阅标志
 * 7. 复制用户数据
 * 8. 退出临界区
 *
 * 设计要点：
 * - 事件数据被复制到堆中，发布函数返回后数据可安全复用
 * - 使用临界区保护堆操作
 * - 失败时返回错误码而非断言，让调用者决定如何处理
 */
eos_s8_t eos_event_pub_ret(eos_topic_t topic, void *data, eos_u32_t size)
{
    /* 只有在框架完成初始化后，事件发布才是合法的。 */
    if (eos.init_end == 0) {
        return (eos_s8_t)EosRunErr_NotInitEnd;
    }

#if (EOS_USE_PUB_SUB != 0)
    /* 发布订阅模式下，没有订阅表就没法建立订阅快照。 */
    if (eos.sub_table == EOS_NULL) {
        return (eos_s8_t)EosRunErr_SubTableNull;
    }
#endif

    // 保证框架已经运行
    /* 框架已经停止时，拒绝继续入队新事件。 */
    if (eos.enabled == 0) {
        return (eos_s8_t)EosRun_NotEnabled;
    }

    /* 连 actor 都没有，说明当前没人能消费这个事件。 */
    if (eos.actor_exist == 0) {
        return (eos_s8_t)EosRun_NoActor;
    }

    // 没有状态机使能，返回
    /* 有 actor 但没有任何 actor start，也不能接收事件。 */
    if (eos.actor_enabled == 0) {
        return (eos_s8_t)EosRun_NotEnabled;
    }
    // 没有状态机订阅，返回
#if (EOS_USE_PUB_SUB != 0)
    /* 发布订阅模式下，没人订阅就没必要创建事件实例。 */
    if (eos.sub_table[topic] == 0) {
        return (eos_s8_t)EosRun_NoActorSub;
    }
#endif

    /* 分配事件块并修改队列元数据时要进入临界区。 */
    eos_port_critical_enter();
    // 申请事件空间
    /* 申请 [内部事件头 + 数据负载] 这一整块空间。 */
    eos_event_inner_t *e = eos_heap_malloc(&eos.heap, (size + sizeof(eos_event_inner_t)));
    if (e == (eos_event_inner_t *)0) {
        eos_port_critical_exit();
        return (eos_s8_t)EosRunErr_MallocFail;
    }
    /* 先写入 topic。 */
    e->topic = topic;
#if (EOS_USE_PUB_SUB != 0)
    /* 把当前 topic 的订阅位图快照到事件内部。 */
    e->sub = eos.sub_table[e->topic];
#else
    /* 非发布订阅模式下，默认广播给所有 actor。 */
    e->sub = eos.actor_exist;
#endif
    /* 更新全局汇总位图，便于 eos_once() 快速判断谁有活。 */
    eos.heap.sub_general |= e->sub;
    /* 数据区紧跟在内部事件头后面。 */
    eos_u8_t *e_data = (eos_u8_t *)e + sizeof(eos_event_inner_t);
    /* 把用户 payload 复制进框架私有堆内存。 */
    for (eos_u32_t i = 0; i < size; i ++) {
        e_data[i] = ((eos_u8_t *)data)[i];
    }
    /* 事件现在已经完整入队。 */
    eos_port_critical_exit();

    return (eos_s8_t)EosRun_OK;
}

/**
 * @brief 发布事件（仅主题）
 *
 * 发布一个不带数据的事件。
 */
void eos_event_pub_topic(eos_topic_t topic)
{
    /* 纯 topic 事件：没有 payload，只靠 topic 驱动订阅者。 */
    eos_s8_t ret = eos_event_pub_ret(topic, EOS_NULL, 0);
    EOS_ASSERT(ret >= 0);
    (void)ret;
}

/**
 * @brief 发布事件（携带数据）
 *
 * 发布一个带数据负载的事件。
 */
#if (EOS_USE_EVENT_DATA != 0)
void eos_event_pub(eos_topic_t topic, void *data, eos_u32_t size)
{
    /* 带 payload 的事件：底层仍然统一走 eos_event_pub_ret()。 */
    eos_s8_t ret = eos_event_pub_ret(topic, data, size);
    EOS_ASSERT(ret >= 0);
    (void)ret;
}
#endif

/**
 * @brief 订阅事件
 *
 * 设置Actor对指定主题的订阅。
 */
#if (EOS_USE_PUB_SUB != 0)
void eos_event_sub(eos_actor_t * const me, eos_topic_t topic)
{
    /* 把该 actor 的优先级位加入这个 topic 的订阅位图。 */
    eos.sub_table[topic] |= (1 << me->priority);
}

/**
 * @brief 取消订阅
 *
 * 清除Actor对指定主题的订阅。
 */
void eos_event_unsub(eos_actor_t * const me, eos_topic_t topic)
{
    /* 从这个 topic 的订阅位图里清掉该 actor 的优先级位。 */
    eos.sub_table[topic] &= ~(1 << me->priority);
}
#endif

/* 时间事件实现 ================================================================
 *
 * 时间事件的发布接口。
 * 时间事件不是立即投递，而是加入定时器队列，等待到期后自动投递。
 */

/**
 * @brief 发布延时/周期事件
 *
 * @param topic     事件主题
 * @param time_ms   时间（延时或周期）
 * @param oneshoot  True=一次性，False=周期重复
 *
 * 实现逻辑：
 * 1. 检查定时器数量未超限
 * 2. 检查没有重复的定时器（同一主题不能有多个）
 * 3. 根据时间选择精度单位
 * 4. 计算到期时间点（绝对时间）
 * 5. 加入定时器数组
 * 6. 更新timeout_min（最近的到期时间）
 */
#if (EOS_USE_TIME_EVENT != 0)
void eos_event_pub_time(eos_topic_t topic, eos_u32_t time_ms, eos_bool_t oneshoot)
{
    /* 时间事件必须有正延时，且不能超过框架允许的最大范围。 */
    EOS_ASSERT(time_ms != 0);
    EOS_ASSERT(time_ms <= timer_threshold[EosTimerUnit_Minute]);
    EOS_ASSERT(eos.timer_count < EOS_MAX_TIME_EVENT);

    // 检查重复，不允许重复发送。
    /* 同一个 topic 当前实现只允许挂一个时间事件。 */
    for (eos_u32_t i = 0; i < eos.timer_count; i ++) {
        EOS_ASSERT(topic != eos.etimer[i].topic);
    }

    /* 用当前软件时间作为绝对超时时刻的基准。 */
    eos_u32_t system_ms = eos.time;
    /* 默认先按毫秒精度考虑。 */
    eos_u8_t unit = EosTimerUnit_Ms;
    eos_u16_t period;
    /* 根据延时时长自动选择合适的时间粒度。 */
    for (eos_u8_t i = 0; i < EosTimerUnit_Max; i ++) {
        if (time_ms > timer_threshold[i])
            continue;
        unit = i;

        if (i == EosTimerUnit_Ms) {
            /* 毫秒级不需要换算，直接保存原值。 */
            period = time_ms;
            break;
        }
        /* 更粗粒度下，period 保存“多少个时间单位”。 */
        period = (time_ms + (timer_unit[i] >> 1)) / timer_unit[i];
        break;
    }
    /* timeout 一律保存绝对到期时刻。 */
    eos_u32_t timeout = (system_ms + time_ms);
    /* 把这个时间事件挂进活动定时器表。 */
    eos.etimer[eos.timer_count ++] = (eos_event_timer_t) {
        topic, oneshoot, unit, period, timeout
    };

    /* 更新“最近到期时间”缓存，加速后续判断。 */
    if (eos.timeout_min > timeout) {
        eos.timeout_min = timeout;
    }
}

/**
 * @brief 发布延时一次性事件
 */
void eos_event_pub_delay(eos_topic_t topic, eos_u32_t time_ms)
{
    /* 一次性延时事件。 */
    eos_event_pub_time(topic, time_ms, EOS_True);
}

/**
 * @brief 发布周期事件
 */
void eos_event_pub_period(eos_topic_t topic, eos_u32_t peroid_ms)
{
    /* 周期事件。 */
    eos_event_pub_time(topic, peroid_ms, EOS_False);
}

/**
 * @brief 取消时间事件
 *
 * 从定时器数组中移除指定主题的事件。
 */
void eos_event_time_cancel(eos_topic_t topic)
{
    /* 删除指定 topic 的时间事件，并重算最近到期时间。 */
    eos_u32_t timeout_min = EOS_U32_MAX;
    for (eos_u32_t i = 0; i < eos.timer_count; i ++) {
        /* 不是目标 topic，就顺手拿它参与 timeout_min 重算。 */
        if (topic != eos.etimer[i].topic) {
            timeout_min =   timeout_min > eos.etimer[i].timeout_ms ?
                            eos.etimer[i].timeout_ms :
                            timeout_min;
            continue;
        }
        /* 删除最后一个元素时，只需要减少计数。 */
        if (i == (eos.timer_count - 1)) {
            eos.timer_count --;
            break;
        }
        else {
            /* 中间元素删除时，用最后一个元素覆盖当前位置。 */
            eos.etimer[i] = eos.etimer[eos.timer_count - 1];
            eos.timer_count -= 1;
            /* 覆盖进来的这个新元素还要重新检查一遍。 */
            i --;
        }
    }

    /* 发布新的最近到期时间缓存。 */
    eos.timeout_min = timeout_min;
}
#endif

// 状态转换API ================================================================

/**
 * @brief 执行状态转换
 *
 * 在状态处理函数中调用，触发状态转换。
 */
#if (EOS_USE_SM_MODE != 0)
eos_ret_t eos_tran(eos_sm_t * const me, eos_state_handler state)
{
    /* 这里只做一件事：把目标状态函数写进 me->state。 */
    me->state = state;

    /* 返回 EOS_Ret_Tran，让外层状态机分发器补做 Exit/Enter。 */
    return EOS_Ret_Tran;
}

/**
 * @brief 转换到父状态
 *
 * HSM模式专用，表示事件需要在父状态处理。
 */
eos_ret_t eos_super(eos_sm_t * const me, eos_state_handler state)
{
    /* HSM 中，把父状态暂时写进 me->state，供分发器继续向上查找。 */
    me->state = state;

    /* 返回 EOS_Ret_Super，表示“当前层不处理，请父状态接手”。 */
    return EOS_Ret_Super;
}

/**
 * @brief 顶层状态处理函数
 *
 * 所有状态层次的最顶层。
 * 接收到未处理的事件时返回EOS_Ret_Null。
 */
eos_ret_t eos_state_top(eos_sm_t * const me, eos_event_t const * const e)
{
    /* 顶层伪状态本身不处理业务事件，它只是层级状态机的锚点。 */
    (void)me;
    (void)e;

    return EOS_Ret_Null;
}
#endif

/* 状态机分发 ================================================================
 *
 * 状态机事件处理的核心算法。
 * 根据模式（FSM/HSM）执行不同的事件分发策略。
 */

/**
 * @brief 状态机事件分发
 *
 * 将事件分发到状态机的当前状态处理。
 *
 * FSM模式：
 * 1. 调用当前状态处理函数
 * 2. 如果返回Tran，执行状态转换
 * 3. 先执行Exit，再执行Enter
 *
 * HSM模式：
 * 1. 循环调用当前状态，直到不返回Super
 * 2. 如果Tran，追溯状态转换路径
 * 3. 执行LCA（最低公共祖先）检测
 * 4. 从上到下执行Exit
 * 5. 从上到下执行Enter
 * 6. 处理Init事件（可能触发新的转换）
 */
#if (EOS_USE_SM_MODE != 0)
static void eos_sm_dispath(eos_sm_t * const me, eos_event_t const * const e)
{
#if (EOS_USE_HSM_MODE != 0)
    /* path[] 用来保存层级跳转的进入路径。 */
    eos_state_handler path[EOS_MAX_HSM_NEST_DEPTH];
#endif
    eos_ret_t r;

    /* 分发器只接受有效事件对象。 */
    EOS_ASSERT(e != (eos_event_t *)0);

#if (EOS_USE_HSM_MODE == 0)
    /* s 保存当前状态，t 保存跳转目标。 */
    eos_state_handler s = me->state;
    eos_state_handler t;

    /* 先把外部事件交给当前状态处理。 */
    r = s(me, e);
    if (r == EOS_Ret_Tran) {
        /* 当前状态函数已经把目标状态写进 me->state。 */
        t = me->state;
        /* 先执行源状态 Exit。 */
        r = s(me, &eos_event_table[Event_Exit]);
        EOS_ASSERT(r == EOS_Ret_Handled || r == EOS_Ret_Super);
        /* 再执行目标状态 Enter。 */
        r = t(me, &eos_event_table[Event_Enter]);
        EOS_ASSERT(r == EOS_Ret_Handled || r == EOS_Ret_Super);
        /* 最后提交当前状态。 */
        me->state = t;
    }
    else {
        /* 没有跳转时，把当前状态恢复成原来的 s。 */
        me->state = s;
    }
#else
    /* t 是外部事件进入前的当前状态；s 是逐层上探时的处理状态。 */
    eos_state_handler t = me->state;
    eos_state_handler s;

    // 层次化的处理事件
    // 注：分为两种情况：
    // (1) 当该状态存在数据时，处理此事件。
    // (2) 当该状态不存在该事件时，到StateTop状态下处理此事件。
    do {
        s = me->state;
        r = (*s)(me, e);                              // 执行状态S下的事件处理
    } while (r == EOS_Ret_Super);

    // 如果不存在状态转移
    if (r != EOS_Ret_Tran) {
        me->state = t;                                  // 更新当前状态
        return;
    }

    // 如果存在状态转移
    path[0] = me->state;    // 保存目标状态
    path[1] = t;
    path[2] = s;

    // exit current state to transition source s...
    while (t != s) {
        // exit handled?
        if (HSM_TRIG_(t, Event_Exit) == EOS_Ret_Handled) {
            (void)HSM_TRIG_(t, Event_Null); // find superstate of t
        }
        t = me->state; // stateTgt_ holds the superstate
    }

    eos_s32_t ip = eos_sm_tran(me, path); // take the HSM transition

    // retrace the entry path in reverse (desired) order...
    for (; ip >= 0; --ip) {
        HSM_TRIG_(path[ip], Event_Enter); // enter path[ip]
    }
    t = path[0];    // stick the target into register
    me->state = t; // update the next state

    // 一级一级的钻入各层
    while (HSM_TRIG_(t, Event_Init) == EOS_Ret_Tran) {
        ip = 0;
        path[0] = me->state;
        (void)HSM_TRIG_(me->state, Event_Null);       // 获取其父状态
        while (me->state != t) {
            ip ++;
            path[ip] = me->state;
            (void)HSM_TRIG_(me->state, Event_Null);   // 获取其父状态
        }
        me->state = path[0];

        // 层数不能大于MAX_NEST_DEPTH_
        EOS_ASSERT(ip < EOS_MAX_HSM_NEST_DEPTH);

        // retrace the entry path in reverse (correct) order...
        do {
            HSM_TRIG_(path[ip], Event_Enter);       // 进入path[ip]
        } while (ip >= 0);

        t = path[0];
    }

    me->state = t;                                  // 更新当前状态
#endif
}

/* HSM状态转换算法 ============================================================
 *
 * 层次状态机的转换算法是EventOS的核心。
 * 它处理状态转换时的进入/退出路径，确保：
 * 1. 所有必要的父状态都被进入
 * 2. 适当的父状态不被重复进入
 * 3. 退出路径上的状态都执行Exit
 *
 * 核心概念：
 * - LCA（Lowest Common Ancestor）：源状态和目标状态的最低公共祖先
 * - 转换路径：从目标状态到LCA的路径
 * - 进入路径：从LCA下一级到目标状态的路径
 *
 * 转换规则：
 * (a) s == t: 转换到自己，直接返回
 * (b) source == target->super: 目标状态的父状态就是源状态
 * (c) source->super == target->super: 源和目标有共同父状态
 * (d) source->super == target: 源状态是目标状态的父状态
 * (e) 其他情况：需要计算完整的LCA
 */

/**
 * @brief HSM状态转换算法
 *
 * 计算层次状态机转换时的进入路径索引。
 *
 * @param me   状态机指针
 * @param path 转换路径数组 [目标, 源->super的super, 源]
 * @return 进入路径索引
 */
#if (EOS_USE_HSM_MODE != 0)
static eos_s32_t eos_sm_tran(eos_sm_t * const me, eos_state_handler path[EOS_MAX_HSM_NEST_DEPTH])
{
    // transition entry path index
    eos_s32_t ip = -1;
    eos_s32_t iq; // helper transition entry path index
    eos_state_handler t = path[0];
    eos_state_handler s = path[2];
    eos_ret_t r;

    // (a) 跳转到自身 s == t
    if (s == t) {
        HSM_TRIG_(s, Event_Exit);  // exit the source
        return 0; // cause entering the target
    }

    (void)HSM_TRIG_(t, Event_Null); // superstate of target
    t = me->state;

    // (b) check source == target->super
    if (s == t)
        return 0; // cause entering the target

    (void)HSM_TRIG_(s, Event_Null); // superstate of src

    // (c) check source->super == target->super
    if (me->state == t) {
        HSM_TRIG_(s, Event_Exit);  // exit the source
        return 0; // cause entering the target
    }

    // (d) check source->super == target
    if (me->state == path[0]) {
        HSM_TRIG_(s, Event_Exit); // exit the source
        return -1;
    }

    // (e) check rest of source == target->super->super..
    // and store the entry path along the way

    // indicate that the LCA was not found
    iq = 0;

    // enter target and its superstate
    ip = 1;
    path[1] = t; // save the superstate of target
    t = me->state; // save source->super

    // find target->super->super
    r = HSM_TRIG_(path[1], Event_Null);
    while (r == EOS_Ret_Super) {
        ++ ip;
        path[ip] = me->state; // store the entry path
        if (me->state == s) { // is it the source?
            // indicate that the LCA was found
            iq = 1;

            // entry path must not overflow
            EOS_ASSERT(ip < EOS_MAX_HSM_NEST_DEPTH);
            --ip;  // do not enter the source
            r = EOS_Ret_Handled; // terminate the loop
        }
        // it is not the source, keep going up
        else
            r = HSM_TRIG_(me->state, Event_Null);
    }

    // LCA found yet?
    if (iq == 0) {
        // entry path must not overflow
        EOS_ASSERT(ip < EOS_MAX_HSM_NEST_DEPTH);

        HSM_TRIG_(s, Event_Exit); // exit the source

        // (f) check the rest of source->super
        //                  == target->super->super...
        iq = ip;
        r = EOS_Ret_Null; // indicate LCA NOT found
        do {
            // is this the LCA?
            if (t == path[iq]) {
                r = EOS_Ret_Handled; // indicate LCA found
                // do not enter LCA
                ip = iq - 1;
                // cause termination of the loop
                iq = -1;
            }
            else
                -- iq; // try lower superstate of target
        } while (iq >= 0);

        // LCA not found yet?
        if (r != EOS_Ret_Handled) {
            // (g) check each source->super->...
            // for each target->super...
            r = EOS_Ret_Null; // keep looping
            do {
                // exit t unhandled?
                if (HSM_TRIG_(t, Event_Exit) == EOS_Ret_Handled) {
                    (void)HSM_TRIG_(t, Event_Null);
                }
                t = me->state; //  set to super of t
                iq = ip;
                do {
                    // is this LCA?
                    if (t == path[iq]) {
                        // do not enter LCA
                        ip = iq - 1;
                        // break out of inner loop
                        iq = -1;
                        r = EOS_Ret_Handled; // break outer loop
                    }
                    else
                        --iq;
                } while (iq >= 0);
            } while (r != EOS_Ret_Handled);
        }
    }

    return ip;
}
#endif
#endif

/* 堆内存管理 ================================================================
 *
 * EventOS使用动态内存管理来存储事件数据负载。
 * 堆内存分配算法：First-Fit（首次适配）
 *
 * 设计特点：
 * - 固定大小堆：编译时确定大小，无外部依赖
 * - 块式管理：每个分配块包含元头和数据区
 * - 双向链表：空闲块和已分配块都用链表管理
 * - 连续合并：释放时合并相邻的空闲块
 *
 * 内存布局：
 * +--------+--------+------------------+
 * | block  | block  |      block       |
 * | header | header |     header        |
 * +--------+--------+------------------+
 * |  data  |  data  |      data        |
 * +--------+--------+------------------+
 *  ^        ^
 *  |        |
 *  空闲块    已分配块（队列管理）
 */

/**
 * @brief 初始化堆
 *
 * 设置堆的初始状态，创建第一个空闲块。
 *
 * 初始状态：
 * - 整个堆是一个大的空闲块
 * - queue指向空（无已分配块）
 * - empty标志设为true
 *
 * 设计要点：
 * - 使用memset清零数据区
 * - 第一个空闲块的last设为EOS_HEAP_MAX（无前一块）
 * - 空闲块大小 = 堆大小 - 块头大小
 */
void eos_heap_init(eos_heap_t * const me)
{
    eos_block_t * block_1st;

#if (EOS_USE_MAGIC != 0)
    /* 给 heap 自身打上完整性标记。 */
    me->magic = EOS_MAGIC_NUMBER;
#endif

    /* 初始化事件队列与堆管理元数据。 */
    me->queue = EOS_HEAP_MAX;
    me->error_id = 0;
    me->size = EOS_SIZE_HEAP;
    me->empty = 1;
    me->sub_general = 0;
    me->current = EOS_HEAP_MAX;

    /* 底层缓冲区全部清零。 */
    memset(me->data, 0, EOS_SIZE_HEAP);

    // the 1st free block
    /* 整个 heap 初始被视为一个完整的空闲块。 */
    block_1st = (eos_block_t *)(me->data);

    block_1st->last = EOS_HEAP_MAX;
    block_1st->size = EOS_SIZE_HEAP - (eos_u16_t)sizeof(eos_block_t);
    block_1st->free = 1;
    block_1st->next = EOS_HEAP_MAX;
}

/**
 * @brief 分配内存
 *
 * 从堆中分配指定大小的内存块。
 *
 * 算法：First-Fit（首次适配）
 * 1. 遍历空闲块链表，找到第一个足够大的块
 * 2. 如果剩余空间足够大，拆分块
 * 3. 将新块加入已分配队列
 *
 * 对齐处理：
 * - ARM Cortex-M0不支持非对齐访问
 * - 自动填充到4字节对齐
 * - offset字段记录填充字节数
 *
 * @param size 要分配的大小（字节）
 * @return 分配到的内存指针，失败返回NULL
 */
void * eos_heap_malloc(eos_heap_t * const me, eos_u32_t size)
{
    eos_block_t * block;
    eos_s16_t remaining;

    /* 不允许申请 0 字节。 */
    if (size == 0) {
        me->error_id = 1;
        return EOS_NULL;
    }

    /* Find the first free block in the block-list. */
    eos_u16_t next = 0;
    /* First-Fit：找到第一个足够大的空闲块。 */
    do {
        block = (eos_block_t *)(me->data + next);
        remaining = (block->size - size - sizeof(eos_block_t));
        if (block->free == 1 && remaining >= 0) {
            break;
        }
        next = block->next;
    } while (next != EOS_HEAP_MAX);

    /* 走到末尾还没找到空间，说明堆不够。 */
    if (next == EOS_HEAP_MAX) {
        me->error_id = 2;
        return EOS_NULL;
    }

    /* Divide the block into two blocks. */
    /* ARM Cortex-M0不支持非对齐访问 */
    /* 做 4 字节对齐，兼容不支持非对齐访问的 MCU。 */
    eos_u8_t offset = (size % 4);
    size = (offset == 0) ? size : (size + 4 - offset);
    eos_pointer_t address = (eos_pointer_t)block + size + sizeof(eos_block_t);
    /* new_block 表示切分后剩下的那部分空闲块。 */
    eos_block_t * new_block = (eos_block_t *)address;
    eos_u32_t _size = block->size - size - sizeof(eos_block_t);

    /* Update the list. */
    /* 先补齐剩余空闲块的元数据。 */
    new_block->size = _size;
    new_block->free = EOS_True;
    new_block->next = block->next;
    new_block->last = (eos_u16_t)((eos_pointer_t)block - (eos_pointer_t)me->data);

    /* 再把当前块改造成“已分配块”。 */
    block->next = (eos_u16_t)((eos_pointer_t)new_block - (eos_pointer_t)me->data);
    block->size = size;
    block->free = EOS_False;
    block->offset = (offset == 0) ? 0 : (4 - offset);

    /* 如果后面还有空闲块，顺手修正它的 last 指针。 */
    if (new_block->next != EOS_HEAP_MAX) {
        eos_block_t * block_next2 = (eos_block_t *)((eos_pointer_t)me->data + new_block->next);
        block_next2->last = (eos_u16_t)((eos_pointer_t)new_block - (eos_pointer_t)me->data);
    }

    /* 挂在Queue的最后端 */
    /* 已分配出来的事件块还要挂到事件队列尾部。 */
    next = me->queue;
    eos_block_t * block_queue;
    if (me->queue == EOS_HEAP_MAX) {
        /* 队列原来为空，这个块就是第一个事件块。 */
        me->queue = (eos_u16_t)((eos_pointer_t)block - (eos_pointer_t)me->data);
        block->q_next = EOS_HEAP_MAX;
        block->q_last = EOS_HEAP_MAX;
        me->current = me->queue;
    }
    else {
        /* 队列非空时，先走到当前队尾。 */
        do {
            block_queue = (eos_block_t *)(me->data + next);
            next = block_queue->q_next;
        } while (next != EOS_HEAP_MAX);

        /* 把新块追加到队尾。 */
        block_queue->q_next = (eos_u16_t)((eos_pointer_t)block - (eos_pointer_t)me->data);
        block->q_next = EOS_HEAP_MAX;
        block->q_last = (eos_u16_t)((eos_pointer_t)block_queue - (eos_pointer_t)me->data);
    }

    /* 更新堆状态。 */
    me->error_id = 0;
    me->empty = 0;
    /* 返回给上层的是“块头后面的数据区”指针。 */
    void *p = (void *)((eos_pointer_t)block + (eos_u32_t)sizeof(eos_block_t));
    me->count ++;

    return p;
}

/**
 * @brief 垃圾回收（释放事件）
 *
 * 当事件被处理完毕后调用，释放事件占用的内存。
 *
 * 处理流程：
 * 1. 检查事件是否还有未处理的订阅者（sub为0表示已全部处理）
 * 2. 从事件队列中移除
 * 3. 调用eos_heap_free释放内存
 * 4. 重新计算sub_general（所有待处理事件的订阅或）
 *
 * @param data 事件数据指针
 */
void eos_heap_gc(eos_heap_t * const me, void *data)
{
    /* data 指向内部事件头，不是用户 payload。 */
    eos_event_inner_t *e = (eos_event_inner_t *)data;

    /* 只有当所有订阅位都清空，这个事件块才真正可以释放。 */
    if (e->sub == 0) {
        eos_block_t *block = (eos_block_t *)((eos_pointer_t)data - sizeof(eos_block_t));
        eos_u16_t index = (eos_u16_t)((eos_pointer_t)block - (eos_pointer_t)me->data);
        eos_block_t *block_last = (eos_block_t *)(me->data + block->q_last);
        eos_block_t *block_next = (eos_block_t *)(me->data + block->q_next);

        /* 从Queue中删除 */
        // 如果当前只有这一个block
        if (block->q_next == EOS_HEAP_MAX && block->q_last == EOS_HEAP_MAX) {
            /* 队列里只剩这一个块。 */
            me->empty = 1;
            me->current = EOS_HEAP_MAX;
            me->queue = EOS_HEAP_MAX;
        }
        // 如果这个block在Queue的第一个
        else if (me->queue == index) {
            /* 当前块在队头。 */
            block_next->q_last = EOS_HEAP_MAX;
            me->queue = block->q_next;
            me->current = block->q_next;
        }
        // 如果这个block在Queue的最后一个
        else if (block->q_next == EOS_HEAP_MAX) {
            /* 当前块在队尾。 */
            block_last->q_next = EOS_HEAP_MAX;
            me->current = me->queue;
        }
        else {
            /* 当前块在队列中间。 */
            block_last->q_next = block->q_next;
            block_next->q_last = block->q_last;
            me->current = block->q_next;
        }

        /* 释放这块内存 */
        /* 真正把这块内存归还给堆。 */
        eos_heap_free(me, data);
    }

    /* 根据所有的sub重新生成sub_general */
    /* 重新汇总所有未处理事件的订阅位图。 */
    me->sub_general = 0;
    eos_u16_t next = me->queue;
    eos_u16_t loop_count = 0;
    eos_block_t *block;
    while (next != EOS_HEAP_MAX && loop_count < me->count) {
        eos_event_inner_t *evt;
        block = (eos_block_t *)((eos_pointer_t)me->data + next);
        evt = (eos_event_inner_t *)((eos_pointer_t)block + sizeof(eos_block_t));
        /* 逐个 OR 起来，形成“哪些 actor 还有活”的摘要。 */
        me->sub_general |= evt->sub;
        next = block->q_next;

        loop_count ++;
    }
}

/**
 * @brief 获取指定优先级的事件
 *
 * 从事件队列中找到属于指定优先级Actor的最老事件。
 *
 * @param priority Actor优先级
 * @return 事件指针，如果队列为空或没有该优先级的事件返回NULL
 *
 * 设计要点：
 * - 只返回当前Actor尚未处理的事件（evt->sub中对应位为1）
 * - 找到后将对应位清除（evt->sub &= ~(1<<priority)）
 * - 使用循环遍历，loop_count防止无限循环
 */
void *eos_heap_get_block(eos_heap_t * const me, eos_u8_t priority)
{
    eos_block_t * block = EOS_NULL;
    eos_event_inner_t *e = EOS_NULL;

    /* 优先级必须落在合法位宽内。 */
    EOS_ASSERT(priority < EOS_MAX_ACTORS);

    eos_u16_t next = me->queue;
    eos_u16_t loop_count = 0;
    /* 从队头开始，找“当前 actor 还没消费过的最老事件”。 */
    while (next != EOS_HEAP_MAX && loop_count < me->count) {
        eos_event_inner_t *evt;
        block = (eos_block_t *)((eos_pointer_t)me->data + next);
        EOS_ASSERT(block->free == 0);
        evt = (eos_event_inner_t *)((eos_pointer_t)block + sizeof(eos_block_t));
        if ((evt->sub & (1 << priority)) == 0) {
            /* 当前 actor 对这条事件已经消费过了，继续往后找。 */
            next = block->q_next;
            loop_count ++;
        }
        else {
            /* 找到了目标事件，同时清掉当前 actor 的订阅位。 */
            e = evt;
            evt->sub &=~ (1 << priority);
            break;
        }
    }

    return (void *)e;
}

/**
 * @brief 释放内存块
 *
 * 将内存块标记为空闲，并尝试与相邻的空闲块合并。
 *
 * 合并策略：
 * 1. 检查前一块（last），如果空闲则合并
 * 2. 检查后一块（next），如果空闲则合并
 * 3. 合并后的块成为更大的空闲块
 *
 * @param data 要释放的内存指针
 */
void eos_heap_free(eos_heap_t * const me, void * data)
{
    /* 先从数据区指针回退到块头。 */
    eos_block_t * block = (eos_block_t *)((eos_pointer_t)data - sizeof(eos_block_t));
    eos_block_t * block_next;
    me->error_id = 0;
    /* 先尝试和前一个物理相邻块合并。 */
    if (block->last != EOS_HEAP_MAX) {
        eos_block_t * block_last = (eos_block_t *)(me->data + block->last);
        /* Check the block can be combined with the front one. */
        if (block_last->free == 1) {
            /* 前一个块如果空闲，就把当前块并进去。 */
            block_last->next = block->next;
            if (block->next != EOS_HEAP_MAX) {
                block_next = (eos_block_t *)(me->data + block_last->next);
                block_next->last = (eos_u16_t)((eos_pointer_t)block_last - (eos_pointer_t)me->data);
            }
            block_last->size += (block->size + sizeof(eos_block_t));
            block = block_last;
        }
    }

    /* Check the block can be combined with the later one. */
    /* 再尝试和后一个物理相邻块合并。 */
    if (block->next != EOS_HEAP_MAX) {
        eos_block_t * block_next = (eos_block_t *)(me->data + block->next);
        eos_block_t * block_next2;
        if (block_next->free == 1) {
            /* 后一个块也是空闲时，继续扩展当前空闲块。 */
            block->size += (block_next->size + (eos_u32_t)sizeof(eos_block_t));
            block->next = block_next->next;
            if (block->next != EOS_HEAP_MAX) {
                block_next2 = (eos_block_t *)(me->data + block_next->next);
                block_next2->last = (eos_u16_t)((eos_pointer_t)block - (eos_pointer_t)me->data);
            }
        }
    }

    /* 最终把合并后的块标记为空闲。 */
    block->free = 1;
    me->count --;
}

/* 测试辅助接口 ================================================================
 *
 * 这些函数主要用于单元测试访问框架内部状态。
 */

/**
 * @brief 获取框架实例指针
 *
 * 用于测试代码验证框架状态。
 */
void * eos_get_framework(void)
{
    return (void *)&eos;
}

/**
 * @brief 设置系统时间
 *
 * 用于测试时人工控制时间流逝。
 */
#if (EOS_USE_TIME_EVENT != 0)
void eos_set_time(eos_u32_t time_ms)
{
    eos.time = time_ms;
}
#endif

#ifdef __cplusplus
}
#endif

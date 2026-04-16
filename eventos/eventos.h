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
 */

#ifndef EVENTOS_H_
#define EVENTOS_H_

/* include ------------------------------------------------------------------ */
#include "eventos_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 默认配置 ----------------------------------------------------------
 *
 * 这些配置项提供安全的默认值，确保即使不修改eventos_config.h，
 * 框架也能在基本模式下编译和运行。
 */

/** @brief 默认MCU类型：32位
 *
 * 大多数现代嵌入式MCU是32位的，设为默认值便于大多数用户开箱即用。
 */
#ifndef EOS_MCU_TYPE
#define EOS_MCU_TYPE                            32
#endif

/** @brief 默认最大Actor数量：8
 *
 * 设为8是因为：8 > 4（典型使用场景所需），且8可以用1字节的位掩码表示。
 * 用户可以根据实际需求减少到4以节省内存。
 */
#ifndef EOS_MAX_ACTORS
#define EOS_MAX_ACTORS                          8
#endif

/** @brief 默认启用断言
 *
 * 开发阶段启用断言有助于快速发现编程错误。
 * 生产环境可通过定义为0关闭。
 */
#ifndef EOS_USE_ASSERT
#define EOS_USE_ASSERT                          1
#endif

/** @brief 默认关闭状态机
 *
 * 简单应用可能只需要Reactor模式（回调函数处理事件）。
 * 状态机模式会引入额外的状态转换开销和代码复杂度。
 */
#ifndef EOS_USE_SM_MODE
#define EOS_USE_SM_MODE                         0
#endif

/** @brief 默认关闭发布-订阅
 *
 * 简单应用中，所有事件广播给所有Actor即可。
 * 发布-订阅需要额外维护订阅表，增加内存和CPU开销。
 */
#ifndef EOS_USE_PUB_SUB
#define EOS_USE_PUB_SUB                         0
#endif

/** @brief 默认关闭时间事件
 *
 * 时间事件需要额外的定时器管理逻辑。
 * 若应用不需要延时或周期事件，可关闭以节省资源。
 */
#ifndef EOS_USE_TIME_EVENT
#define EOS_USE_TIME_EVENT                      0
#endif

/** @brief 默认关闭事件数据携带
 *
 * 若事件只需传递信号（主题）而不需要数据负载，关闭此选项可节省堆内存。
 */
#ifndef EOS_USE_EVENT_DATA
#define EOS_USE_EVENT_DATA                      0
#endif

/** @brief 默认关闭事件桥
 *
 * 多芯片/多实例协同功能，普通单芯片应用不需要。
 */
#ifndef EOS_USE_EVENT_BRIDGE
#define EOS_USE_EVENT_BRIDGE                    0
#endif

#include "eventos_def.h"

/* 事件主题(Topic)定义 ----------------------------------------------------------
 *
 * 事件主题是事件的类型标识，用于发布-订阅机制中的主题过滤。
 * Event_User是用户自定义事件的起始值，用户的事件主题应大于此值。
 */

enum eos_event_topic {
#if (EOS_USE_SM_MODE != 0)
    Event_Null = 0,    /**< 空事件，用于触发状态查询（获取当前状态的父状态） */
    Event_Enter,       /**< 进入状态事件，状态转换时触发 */
    Event_Exit,        /**< 退出状态事件，状态转换时触发 */
#if (EOS_USE_HSM_MODE != 0)
    Event_Init,        /**< 初始化事件，用于层次状态机的初始转换 */
#endif
    Event_User,        /**< 用户事件起始索引，在此之前的是框架保留事件 */
#else
    Event_Null = 0,    /**< 空事件（简单模式下仅作为占位符） */
    Event_User,        /**< 用户事件起始索引 */
#endif
};

/** @brief 事件主题类型
 *
 * 根据MCU类型选择：
 * - 8位MCU: eos_u8_t，最大256个主题
 * - 16/32位MCU: eos_u16_t，最大65536个主题
 *
 * 设计理由：嵌入式系统通常不需要大量主题，节省内存。
 */
#if (EOS_MCU_TYPE == 8)
typedef eos_u8_t                        eos_topic_t;
#else
typedef eos_u16_t                       eos_topic_t;
#endif

/* 状态机返回值定义 ----------------------------------------------------------
 *
 * 状态处理函数的返回值，决定框架如何处理当前状态对事件的响应。
 * 这是状态机模式的核心设计，影响状态转换的决策逻辑。
 */

#if (EOS_USE_SM_MODE != 0)
/**
 * @brief 状态处理返回值
 *
 * - EOS_Ret_Null: 事件未处理，不进行状态转换
 * - EOS_Ret_Handled: 事件已处理，不进行状态转换（但表示事件被消费了）
 * - EOS_Ret_Super: 事件未在当前状态处理，传递给父状态（HSM模式）
 * - EOS_Ret_Tran: 触发状态转换，当前状态将转换到目标状态
 *
 * 设计思路：返回值决定了状态转换的行为，
 * 使得状态转换逻辑可以完全由数据（返回值）驱动，而非硬编码。
 */
typedef enum eos_ret {
    EOS_Ret_Null = 0,      /**< 事件未处理，状态保持不变 */
    EOS_Ret_Handled,       /**< 事件已处理，状态保持不变 */
    EOS_Ret_Super,         /**< 事件向上传递给父状态处理（层次状态机） */
    EOS_Ret_Tran,          /**< 触发状态转换到目标状态 */
} eos_ret_t;
#endif

/**
 * @brief 事件结构体
 *
 * 事件由主题和数据负载组成。
 * 事件在被处理后自动释放，无需手动管理。
 */
typedef struct eos_event {
    eos_topic_t topic;      /**< 事件主题/类型 */
    void *data;             /**< 事件数据指针，携带额外信息 */
    eos_u16_t size;         /**< 数据大小（字节） */
} eos_event_t;

/* 事件处理相关类型定义 ----------------------------------------------------------
 *
 * 这里定义了事件处理函数和状态处理函数的函数指针类型。
 * 使用函数指针而非其他机制（如消息队列）的原因：
 * 1. 零抽象开销：函数指针调用直接、高效
 * 2. 灵活性和解耦：用户可以自由定义处理逻辑
 * 3. 嵌入式友好：不需要动态内存分配
 */

/** @brief 前向声明，让eos_event_handler可以引用eos_reactor */
struct eos_reactor;

/**
 * @brief 事件处理函数类型
 *
 * Reactor模式下，Actor的事件处理函数类型。
 *
 * @param me 指向所属Reactor实例的指针
 * @param e  指向事件结构体的指针，包含事件主题和数据
 *
 * 使用函数指针而非接口/虚函数的原因：
 * - 嵌入式环境需要零运行时开销
 * - 不需要虚函数表，节省内存
 * - 简单直接，易于理解和使用
 */
typedef void (* eos_event_handler)(struct eos_reactor *const me, eos_event_t const * const e);

#if (EOS_USE_SM_MODE != 0)
/** @brief 前向声明，让eos_state_handler可以引用eos_sm */
struct eos_sm;

/**
 * @brief 状态处理函数类型（状态机模式）
 *
 * 状态机中每个状态的处理函数类型。
 *
 * @param me 指向所属状态机实例的指针
 * @param e  指向事件结构体的指针
 * @return   状态处理结果，决定框架的后续行为
 *
 * 设计思路：每个状态就是一个函数，状态转换通过返回EOS_Ret_Tran实现。
 * 这种"状态即函数"的设计简化了状态机的实现，避免了复杂的查表方式。
 */
typedef eos_ret_t (* eos_state_handler)(struct eos_sm *const me, eos_event_t const * const e);
#endif

/** @brief 事件引用类型（保留接口） */
typedef eos_event_t *                       eos_event_quote_t;

/* Actor基类 ----------------------------------------------------------
 *
 * Actor是框架的核心执行单元，代表一个独立的事件处理实体。
 * Reactor和StateMachine都继承自Actor，拥有共同的优先级和使能控制。
 *
 * 设计理由：通过基类共享公共字段，避免代码重复。
 * 使用结构体嵌入（Composition）而非继承，实现零成本抽象。
 */

/**
 * @brief Actor基类结构体
 *
 * 定义所有Actor共有的属性：
 * - magic: 魔术数字（可选，用于内存覆盖检测）
 * - priority: 优先级（0为最高），用于事件分发时的优先级排序
 * - mode: 运行模式（Reactor或StateMachine）
 * - enabled: 使能状态
 *
 * 使用位域(bitfield)压缩存储：
 * - 32位MCU上使用1个uint32_t存储这三个字段
 * - 节省内存但有轻微的位操作开销
 */
typedef struct eos_actor {
#if (EOS_USE_MAGIC != 0)
    eos_u32_t magic;   /**< 魔术数字，运行时检测是否被破坏 */
#endif
#if (EOS_MCU_TYPE == 32 || EOS_MCU_TYPE == 16)
    eos_u32_t priority              : 5;  /**< 优先级：0~31，值越小优先级越高 */
    eos_u32_t mode                  : 1;  /**< 模式：0=Reactor，1=StateMachine */
    eos_u32_t enabled               : 1;  /**< 使能状态：0=禁用，1=启用 */
    eos_u32_t reserve               : 1;  /**< 保留位，用于对齐 */
#else
    eos_u8_t priority               : 5;  /**< 8位MCU上的优先级字段 */
    eos_u8_t mode                   : 1;
    eos_u8_t enabled                : 1;
    eos_u8_t reserve                : 1;
#endif
} eos_actor_t;

/* Reactor类 ----------------------------------------------------------
 *
 * Reactor是一种简单的事件处理模式：注册一个事件处理函数，
 * 当有事件到达时，框架调用这个函数进行处理。
 *
 * 适用于：简单的事件响应逻辑，不需要复杂的状态管理。
 */

/**
 * @brief Reactor类
 *
 * 包含父类(Actor)和一个事件处理函数指针。
 * 通过eos_reactor_init()初始化，eos_reactor_start()启动。
 *
 * 使用场景示例：按键检测、LED控制、简单状态报告等。
 */
typedef struct eos_reactor {
    eos_actor_t super;                      /**< 父类，包含优先级等公共属性 */
    eos_event_handler event_handler;        /**< 事件处理函数指针 */
} eos_reactor_t;

/* 状态机类 ----------------------------------------------------------
 *
 * 状态机(HSM/FSM)是一种更结构化的事件处理模式。
 * 事件被发送到当前状态，状态根据事件类型和当前状态决定：
 * - 处理事件并保持当前状态
 * - 转换到另一个状态
 *
 * HSM（层次状态机）支持状态的嵌套和继承。
 */

/**
 * @brief 状态机类
 *
 * 包含父类(Actor)和一个当前状态处理器。
 * 状态机通过状态转换表或状态处理函数实现状态逻辑。
 *
 * 设计思路：
 * - 使用 volatile 修饰state，因为状态转换可能在中断中发生
 * - 状态处理器函数返回EOS_Ret_Tran时，state会被更新为目标状态
 */
#if (EOS_USE_SM_MODE != 0)
typedef struct eos_sm {
    eos_actor_t super;                          /**< 父类 */
    volatile eos_state_handler state;           /**< 当前状态处理函数 */
} eos_sm_t;
#endif

/* 框架级API ----------------------------------------------------------
 *
 * 这些是框架的核心API，控制框架的生命周期。
 * 调用顺序：eos_init() -> (各Actor初始化) -> eos_run()
 */

/**
 * @brief 初始化框架
 *
 * 在所有Actor初始化之前调用，进行框架内部数据结构的初始化。
 *
 * 初始化内容：
 * - 清除运行时数据
 * - 初始化堆内存（如果启用）
 * - 重置Actor注册表
 * - 设置初始状态为就绪
 *
 * 注意：此函数只初始化框架本身，不启动框架运行。
 */
void eos_init(void);

#if (EOS_USE_PUB_SUB != 0)
/**
 * @brief 初始化订阅表
 *
 * 必须在e os_run()之前调用，为发布-订阅机制分配和初始化订阅标志表。
 *
 * @param flag_sub 指向订阅标志数组的指针，数组大小应为topic_max
 * @param topic_max 最大主题数量（数组长度）
 *
 * 设计理由：订阅表由用户分配，允许灵活的内存管理策略。
 * 用户可以选择放在栈上、静态区或堆上。
 */
void eos_sub_init(eos_mcu_t *flag_sub, eos_topic_t topic_max);
#endif

/**
 * @brief 启动框架（进入事件循环）
 *
 * 必须放在main函数的末尾，调用后永远不会返回（除非调用eos_stop()）。
 *
 * 函数内部会：
 * 1. 调用 eos_hook_start() 钩子
 * 2. 进入主事件循环，持续调用 eos_once() 处理事件
 * 3. 当 eos.enabled 变为 false 时，调用 eos_hook_idle() 进入空闲循环
 *
 * 警告：此函数不会返回，不应在它之后编写任何代码。
 */
void eos_run(void);

/**
 * @brief 停止框架运行
 *
 * 请求框架停止运行。调用后，主循环会在处理完当前事件后退出。
 *
 * 使用场景：系统关机、切换到bootloader、深度睡眠等。
 *
 * 注意：这只是请求停止，框架可能不会立即响应，会执行完当前事件。
 */
void eos_stop(void);

/**
 * @brief 延时函数（可响应事件）
 *
 * 延时期间不阻塞事件接收，新事件仍会被处理。
 *
 * @param time_ms 延时时长（毫秒）
 *
 * 实现方式：通过发布一个延时事件，等延时到期后自动触发。
 * 延时期间CPU可以处理其他事件，实现准并发。
 */
void eos_delay(eos_u32_t time_ms);

/**
 * @brief 延时函数（屏蔽事件接收）
 *
 * 延时期间不处理任何事件，阻塞式等待延时结束。
 *
 * @param time_ms 延时时长（毫秒）
 *
 * 使用场景：需要严格时序保护的初始化序列、
 * 等待硬件稳定等不允许被打断的场景。
 */
void eos_delay_unsub_event(eos_u32_t time_ms);

#if (EOS_USE_TIME_EVENT != 0)
/**
 * @brief 获取系统当前时间
 *
 * @return 系统从启动至今经过的毫秒数（不含休眠时间）
 *
 * 注意：这是一个32位无符号整数，最大值约49.7天。
 * 使用时需考虑时间溢出回绕的情况。
 */
eos_u32_t eos_time(void);

/**
 * @brief 系统Tick更新
 *
 * 由定时器中断或主循环定期调用，推动系统时间前进。
 * 每次调用，时间增加 EOS_TICK_MS 毫秒。
 *
 * 调用频率应与 EOS_TICK_MS 匹配：
 * - EOS_TICK_MS=1时，每1ms调用一次
 * - EOS_TICK_MS=10时，每10ms调用一次
 *
 * 典型实现是在定时器中断中调用。
 */
void eos_tick(void);
#endif

/* Reactor相关API ----------------------------------------------------------
 *
 * Reactor是一种简单的事件处理模式，直接注册事件处理函数。
 */

/**
 * @brief 初始化Reactor
 *
 * @param me       指向Reactor实例的指针
 * @param priority 优先级（0最高），用于事件分发顺序
 * @param parameter 初始化参数（目前未使用，传NULL即可）
 *
 * 调用时机：必须在 eos_run() 之前调用。
 * 会将Reactor注册到框架，但此时还不处理事件（需要调用start）。
 */
void eos_reactor_init(  eos_reactor_t * const me,
                        eos_u8_t priority,
                        void const * const parameter);

/**
 * @brief 启动Reactor
 *
 * @param me            指向Reactor实例的指针
 * @param event_handler 事件处理函数
 *
 * 启动后，Reactor开始接收事件。处理函数会被调用来处理每个事件。
 *
 * 事件处理函数类型转换宏，用于兼容不同风格的函数签名。
 */
void eos_reactor_start(eos_reactor_t * const me, eos_event_handler event_handler);

/** @brief 事件处理函数类型转换宏
 *
 * 用于将成员函数转换为标准事件处理函数类型。
 * C语言不支持成员函数指针直接转换，需要通过此宏。
 */
#define EOS_HANDLER_CAST(handler)       ((eos_event_handler)(handler))

/* 状态机相关API ----------------------------------------------------------
 *
 * 状态机模式支持更复杂的状态转换逻辑。
 * 区分FSM（有限状态机）和HSM（层次状态机）。
 */

#if (EOS_USE_SM_MODE != 0)
/**
 * @brief 初始化状态机
 *
 * @param me        指向状态机实例的指针
 * @param priority 优先级
 * @param parameter 初始化参数
 *
 * 与Reactor类似，但设置mode为StateMachine模式。
 */
void eos_sm_init(   eos_sm_t * const me,
                    eos_u8_t priority,
                    void const * const parameter);

/**
 * @brief 启动状态机
 *
 * @param me         指向状态机实例的指针
 * @param state_init 初始状态处理函数
 *
 * 启动后：
 * 1. 设置初始状态
 * 2. 调用初始状态的空事件处理，获取目标状态（用于初始化转换）
 * 3. 进入初始状态，触发Event_Enter
 * 4. 对于HSM，执行Event_Init进行层次初始化
 *
 * 重要：进入初始状态时会无条件执行一次TRAN转换，
 * 所以初始状态处理函数通常返回EOS_Ret_Tran到真正的初始状态。
 */
void eos_sm_start(eos_sm_t * const me, eos_state_handler state_init);

/**
 * @brief 状态转换宏
 *
 * 在状态处理函数中使用，将控制权转移到目标状态。
 *
 * @param target 目标状态处理函数
 *
 * 使用示例：return EOS_TRAN(some_state);
 *
 * 原理：调用eos_tran()设置状态机的新状态为target，并返回EOS_Ret_Tran。
 */
#define EOS_TRAN(target)            eos_tran((eos_sm_t * )me, (eos_state_handler)target)

/**
 * @brief 转换到父状态宏
 *
 * 在状态处理函数中使用，表示事件需要在父状态中处理。
 *
 * @param super 父状态处理函数
 *
 * HSM模式专用，FSM模式下等效于EOS_TRAN。
 */
#define EOS_SUPER(super)            eos_super((eos_sm_t * )me, (eos_state_handler)super)

/** @brief 状态函数类型转换宏 */
#define EOS_STATE_CAST(state)       ((eos_state_handler)(state))

/**
 * @brief 执行状态转换
 *
 * 内部函数，由EOS_TRAN宏调用，不应直接使用。
 */
eos_ret_t eos_tran(eos_sm_t * const me, eos_state_handler state);

/**
 * @brief 转换到超状态（父状态）
 *
 * 内部函数，由EOS_SUPER宏调用，不应直接使用。
 * 主要用于HSM模式，表示事件需要向上传递给父状态处理。
 */
eos_ret_t eos_super(eos_sm_t * const me, eos_state_handler state);

/**
 * @brief 状态机顶层状态（HSM根状态）
 *
 * 所有状态层次的最顶层，所有状态的最终父状态。
 * 接收到未被处理的事件时，返回EOS_Ret_Null。
 *
 * 设计原因：提供一个统一的最顶层状态，避免状态转换到无效状态。
 */
eos_ret_t eos_state_top(eos_sm_t * const me, eos_event_t const * const e);
#endif

/* 事件发布-订阅API ----------------------------------------------------------
 *
 * 发布-订阅是一种松耦合的事件分发机制。
 * 发布者只管发布事件到主题，不关心谁会接收。
 * 订阅者只管订阅感兴趣的主题，不关心事件从哪里来。
 */

/**
 * @brief 设置不可阻塞事件
 *
 * 某些事件在延时状态下也应该被立即处理，不受eos_delay_unsub_event()影响。
 *
 * @param topic 不可阻塞的事件主题
 *
 * 使用场景：紧急停止信号、错误恢复事件等。
 */
void eos_event_set_unblocked(eos_topic_t topic);

#if (EOS_USE_PUB_SUB != 0)
/**
 * @brief 订阅事件
 *
 * Actor订阅某主题后，可以接收该主题的事件。
 *
 * @param me   指向Actor的指针
 * @param topic 要订阅的主题
 *
 * 实现：将Actor优先级对应的位掩码设置到订阅表的topic位置。
 */
void eos_event_sub(eos_actor_t * const me, eos_topic_t topic);

/**
 * @brief 取消订阅
 *
 * 取消对某主题的订阅，不再接收该主题的事件。
 *
 * @param me   指向Actor的指针
 * @param topic 要取消的主题
 */
void eos_event_unsub(eos_actor_t * const me, eos_topic_t topic);

/** @brief 订阅事件宏（用于状态机内部）
 *
 * 简化为直接使用me指针，省去取地址操作。
 */
#define EOS_EVENT_SUB(_evt)               eos_event_sub(&(me->super.super), _evt)

/** @brief 取消订阅宏 */
#define EOS_EVENT_UNSUB(_evt)             eos_event_unsub(&(me->super.super), _evt)
#endif

/* 事件发布API ----------------------------------------------------------
 *
 * 事件发布有多种方式：仅主题、携带数据、延时、周期等。
 * 不同发布方式适用于不同场景。
 */

/**
 * @brief 发布事件（仅主题）
 *
 * 发布一个只包含主题没有数据的事件。
 * 可在中断服务程序(ISR)中调用。
 *
 * @param topic 事件主题
 *
 * 注意：这是唯一可以在ISR中安全调用的发布函数。
 */
void eos_event_pub_topic(eos_topic_t topic);

#if (EOS_USE_EVENT_DATA != 0)
/**
 * @brief 发布事件（携带数据）
 *
 * 发布一个带数据负载的事件。
 * 数据被复制到堆内存中，接收者获得数据的独立副本。
 *
 * @param topic 事件主题
 * @param data  数据指针，要复制的数据
 * @param size  数据大小（字节）
 *
 * 注意：如果堆内存不足，事件发布失败，调用者应处理这种情况。
 */
void eos_event_pub(eos_topic_t topic, void *data, eos_u32_t size);
#endif

#if (EOS_USE_TIME_EVENT != 0)
/**
 * @brief 发布延时一次性事件
 *
 * 事件在指定的延时后才会被投递到事件队列。
 *
 * @param topic     事件主题
 * @param time_ms   延时时长（毫秒）
 *
 * 特点：
 * - 是一次性的，到期后自动从定时器列表移除
 * - 可用于超时检测、延迟执行等场景
 */
void eos_event_pub_delay(eos_topic_t topic, eos_u32_t time_ms);

/**
 * @brief 发布周期事件
 *
 * 事件会周期性重复投递。
 *
 * @param topic          事件主题
 * @param peroid_ms      周期时间（毫秒）
 *
 * 特点：
 * - 无限重复，直到调用eos_event_time_cancel()取消
 * - 适用于定时任务、周期性检测等
 */
void eos_event_pub_period(eos_topic_t topic, eos_u32_t peroid_ms);

/**
 * @brief 取消延时/周期事件
 *
 * 取消指定主题的延时或周期事件。
 *
 * @param topic 要取消的事件主题
 *
 * 注意：只能取消自己发布的事件，防止误取消其他模块的事件。
 */
void eos_event_time_cancel(eos_topic_t topic);
#endif

/* 硬件端口接口 ----------------------------------------------------------
 *
 * 这些函数需要由用户针对具体硬件平台实现。
 * 是框架与硬件/操作系统之间的抽象层。
 */

/**
 * @brief 进入临界区
 *
 * 禁止中断，保护共享数据的原子访问。
 * 必须与 eos_port_critical_exit() 配对使用。
 *
 * 实现建议：
 * - 简单实现：直接保存中断使能状态，禁用所有中断
 * - 嵌套安全：使用计数器，允许嵌套调用
 *
 * 注意：临界区应尽量短，只保护关键的数据操作。
 */
void eos_port_critical_enter(void);

/**
 * @brief 退出临界区
 *
 * 恢复之前的_interrupt使能状态。
 * 必须与 eos_port_critical_enter() 配对使用。
 */
void eos_port_critical_exit(void);

/**
 * @brief 断言失败处理
 *
 * 当 EOS_ASSERT 失败时调用。
 *
 * @param error_id 错误ID（通常是源代码行号）
 *
 * 实现建议：进入死循环、输出错误信息、复位等。
 */
void eos_port_assert(eos_u32_t error_id);

/* 钩子函数 ----------------------------------------------------------
 *
 * 框架在特定时机调用这些钩子，允许用户插入自定义代码。
 * 主要用于外设初始化、资源清理、性能监控等。
 */

/**
 * @brief 空闲钩子
 *
 * 当没有事件需要处理时调用。
 * 可用于进入低功耗模式、清理资源等。
 *
 * 默认实现：通常是一个死循环。
 */
void eos_hook_idle(void);

/**
 * @brief 停止钩子
 *
 * 当框架被eos_stop()请求停止时调用。
 * 可用于资源清理、关闭外设等。
 */
void eos_hook_stop(void);

/**
 * @brief 启动钩子
 *
 * 在e os_run()主循环开始前调用。
 * 可用于外设初始化、系统自检等。
 */
void eos_hook_start(void);

#ifdef __cplusplus
}
#endif

#endif

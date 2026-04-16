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
 * 2022-02-20     DogMing       V0.0.2
 */

#ifndef EVENTOS_CONFIG_H__
#define EVENTOS_CONFIG_H__

/* EventOS Nano 通用配置 --------------------------------------------------- */

/**
 * @brief MCU类型选择
 *
 * 定义目标MCU的位数，用于选择最紧凑的数据结构。
 * - 8:  使用 eos_u8_t 作为优先级和订阅标志的数据结构，节省内存但限制最大Actor数
 * - 16: 使用 eos_u16_t，适合16位嵌入式处理器
 * - 32: 使用 eos_u32_t，适合32位处理器（如Cortex-M系列）
 *
 * 这个选择直接影响 eos_mcu_t 和 eos_sub_t 的定义，进而影响内存占用和性能。
 */
#define EOS_MCU_TYPE                            32

/**
 * @brief 最大Actor数量
 *
 * Actor是事件驱动框架中的执行单元，可以是Reactor（反应式）或StateMachine（状态机）。
 * 最大值为 MCU_TYPE，即优先级位图的位数。
 *
 * 设计理由：使用位掩码(bitmask)来标记哪些优先级的Actor存在和启用，
 * 所以Actor数量不能超过位图能表示的范围。
 */
#define EOS_MAX_ACTORS                          4

/**
 * @brief 测试平台位数
 *
 * 用于选择指针类型 eos_pointer_t 的宽度：
 * - 32: 使用 eos_u32_t，32位测试环境
 * - 64: 使用 uint64_t，64位测试环境（如PC上的单元测试）
 *
 * 注意：这个选项是针对测试环境而非目标MCU，因为嵌入式MCU通常是32位的。
 */
#define EOS_TEST_PLATFORM                       32

/**
 * @brief 系统Tick周期（毫秒）
 *
 * 每次调用 eos_tick() 时增加的时间增量。
 * 设置为1表示1ms tick，适合实时性要求较高的嵌入式系统。
 *
 * 使用建议：根据MCU的实际定时器中断频率调整此值。
 */
#define EOS_TICK_MS                             1

/**
 * @brief Magic Number检测开关
 *
 * 设为1时启用内存覆盖检测功能，在关键数据结构中写入魔术数字 0xDEADBEEF。
 * 运行时检查魔术数字是否被破坏，可检测栈溢出或野指针写入等内存错误。
 *
 * 生产环境可关闭以减少内存占用和检查开销。
 */
#define EOS_USE_MAGIC                           0

/* 断言配置 ------------------------------------------------------------ */

/**
 * @brief 断言功能开关
 *
 * 启用后，未通过的断言会调用 eos_hook_stop() 停止系统，
 * 并通过 eos_port_assert() 报告错误位置（行号）。
 *
 * 关闭后，断言表达式不会被计算，适合对性能敏感的生产环境。
 * 但即使关闭，EOS_ASSERT 宏仍会保留断言点，便于调试。
 */
#define EOS_USE_ASSERT                          1

/* 状态机功能配置 -------------------------------------------------------- */

/**
 * @brief 状态机模式开关
 *
 * 启用后，框架支持普通状态机(FSM)功能。
 * Actor可以以状态机模式运行，实现状态转换逻辑。
 *
 * HSM（层次状态机）依赖此选项，必须同时启用。
 */
#define EOS_USE_SM_MODE                         1

/**
 * @brief 层次状态机(HSM)开关
 *
 * 启用后，状态机支持层次化设计：状态可以嵌套，有父状态的概念。
 * 事件先在当前状态处理，若未处理则传递给父状态（Super状态）。
 *
 * 此选项依赖 EOS_USE_SM_MODE 同时开启。
 */
#define EOS_USE_HSM_MODE                        1

#if (EOS_USE_SM_MODE != 0 && EOS_USE_HSM_MODE != 0)
/**
 * @brief HSM最大嵌套深度
 *
 * 层次状态机的最大嵌套层数。
 * 取值范围 2~4，4层嵌套足以满足大多数层次化状态设计需求。
 *
 * 设计理由：深度嵌套会增加状态转换时的路径追踪开销，
 * 限制深度可控制内存占用和转换算法的复杂度。
 */
#define EOS_MAX_HSM_NEST_DEPTH                  4
#endif

/* 发布-订阅功能配置 ---------------------------------------------------- */

/**
 * @brief 发布-订阅机制开关
 *
 * 启用后，事件可以发布到特定主题(Topic)，Actor通过订阅主题来接收事件。
 * 事件发布时只关心主题，不关心谁会接收，实现松耦合。
 *
 * 关闭后，所有事件广播给所有启用的Actor。
 */
#define EOS_USE_PUB_SUB                         1

/* 时间事件配置 -------------------------------------------------------- */

/**
 * @brief 时间事件开关
 *
 * 启用后，支持延时事件和周期事件：
 * - eos_event_pub_delay(): 延时一次性事件
 * - eos_event_pub_period(): 周期重复事件
 *
 * 时间事件使用系统时间(eos_time())进行计时，不依赖硬件定时器中断。
 * 需定期调用 eos_tick() 更新系统时间。
 */
#define EOS_USE_TIME_EVENT                      1
#if (EOS_USE_TIME_EVENT != 0)
    /** @brief 最大时间事件数量
     *
     * 同时存在的延时/周期事件的最大数量。
     * 限制为小于256，因为使用 eos_u8_t 存储计数器。
     *
     * 实际应用中应根据同时需要的定时器数量设置。
     */
    #define EOS_MAX_TIME_EVENT                  4
#endif

/* 事件数据配置 -------------------------------------------------------- */

/**
 * @brief 事件数据携带开关
 *
 * 启用后，事件可以携带数据负载(Data Payload)。
 * 发布事件时指定数据指针和大小，接收时可获取完整数据。
 *
 * 关闭后，事件仅为信号通知，不携带额外数据，可节省内存。
 */
#define EOS_USE_EVENT_DATA                      1

/**
 * @brief 堆内存大小（字节）
 *
 * 事件数据负载使用的堆内存大小上限。
 * 仅当 EOS_USE_EVENT_DATA 启用时有效。
 *
 * 限制范围 128~32767 字节（32KB）。
 * 设计理由：使用15位存储大小，EOS_HEAP_MAX 定义为 0x7fff。
 */
#define EOS_SIZE_HEAP                           32767

/* 事件桥接配置 -------------------------------------------------------- */

/**
 * @brief 事件桥接功能开关
 *
 * 启用后，支持多个EventOS实例之间的事件转发。
 * 用于多芯片协同或分布式嵌入式系统。
 *
 * 当前默认关闭。
 */
#define EOS_USE_EVENT_BRIDGE                    0

/* 错误检查 -------------------------------------------------------- */

#if ((EOS_MCU_TYPE != 8) && (EOS_MCU_TYPE != 16) && (EOS_MCU_TYPE != 32))
#error The MCU type must be 8-bit, 16-bit or 32-bit !
#endif

#if ((EOS_TEST_PLATFORM != 32) && (EOS_TEST_PLATFORM != 64))
#error The test paltform must be 32-bit or 64-bit !
#endif

/**
 * @brief Actor数量合法性检查
 *
 * 检查最大Actor数量是否在有效范围内：1 ~ MCU_TYPE。
 * 使用位掩码表示法，Actor优先级从0开始编号。
 */
#if (EOS_MAX_ACTOR > EOS_MCU_TYPE || EOS_MAX_ACTORS <= 0)
#error The maximum number of actors must be 1 ~ EOS_MCU_TYPE !
#endif

/* 状态机嵌套深度检查
 *
 * HSM嵌套深度必须在2~4之间：
 * - 至少2层：保证有实际的分层结构
 * - 最多4层：防止过深的递归调用和内存消耗
 */
#if (EOS_USE_SM_MODE != 0)
    #if (EOS_USE_HSM_MODE != 0)
        #if (EOS_MAX_HSM_NEST_DEPTH > 4 || EOS_MAX_HSM_NEST_DEPTH < 2)
            #error The maximum nested depth of hsm must be 2 ~ 4 !
        #endif
    #endif
#endif

/* 时间事件数量检查
 *
 * 时间事件计数器使用 eos_u8_t，最大255个。
 * 实际限制为 <256，所以这里检查是否 >= 256。
 */
#if (EOS_USE_TIME_EVENT != 0 && EOS_MAX_TIME_EVENT >= 256)
    #error The number of time events must be less than 256 !
#endif

/* 堆大小检查
 *
 * 堆大小必须在有效范围内：
 * - 最小128字节：保证至少能分配一个基本事件结构
 * - 最大32767字节(0x7fff)：使用15位无符号数存储，大小不能超过这个限制
 */
#if (EOS_USE_EVENT_DATA != 0)
    #if (EOS_USE_HEAP != 0 && (EOS_SIZE_HEAP < 128 || EOS_SIZE_HEAP > EOS_HEAP_MAX))
        #error The heap size must be 128 ~ 32767 (32KB) if the function is enabled !
    #endif
#endif

#endif

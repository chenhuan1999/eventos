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

#ifndef EVENTOS_DEF_H__
#define EVENTOS_DEF_H__

#include "eventos_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 基础数据类型定义 ----------------------------------------------------------
 *
 * 使用自定义类型名称而非标准uint8_t等类型的原因：
 * 1. 跨平台兼容性：不依赖stdint.h，裸嵌入式环境可能没有此头文件
 * 2. 名称一致性：eventos内部的命名风格统一
 * 3. 可移植性：便于移植到不支持标准类型的8位/16位MCU
 */

/** @brief 无符号32位整数 */
typedef unsigned int                    eos_u32_t;

/** @brief 有符号32位整数 */
typedef signed int                      eos_s32_t;

/** @brief 无符号16位整数 */
typedef unsigned short                  eos_u16_t;

/** @brief 有符号16位整数 */
typedef signed short                    eos_s16_t;

/** @brief 无符号8位整数（字节） */
typedef unsigned char                   eos_u8_t;

/** @brief 有符号8位整数 */
typedef signed char                     eos_s8_t;

/* 布尔类型定义 ----------------------------------------------------------
 *
 * 使用enum定义而非#define的原因：
 * 1. 类型安全：可以作为函数参数类型，接受类型检查
 * 2. 调试友好：IDE可以显示枚举值名称
 * 3. 避免预处理问题：宏替换可能在某些上下文中产生意外结果
 */
typedef enum eos_bool {
    EOS_False = 0,     /**< 假/否 */
    EOS_True = !EOS_False,  /**< 真/是（取反确保定义为1） */
} eos_bool_t;

/** @brief 空指针常量
 *
 * C标准中NULL的定义不统一，有的定义为((void*)0)，有的定义为0。
 * 这里明确定义为((void*)0)，确保指针类型正确。
 */
#define EOS_NULL                        ((void *)0)

/* 32位无符号整数极值 ----------------------------------------------- */

/** @brief 32位无符号整数最大值 */
#define EOS_U32_MAX                     0xffffffff

/** @brief 32位无符号整数最小值 */
#define EOS_U32_MIN                     0

/* 16位无符号整数极值 ----------------------------------------------- */

/** @brief 16位无符号整数最大值 */
#define EOS_U16_MAX                     0xffff

/** @brief 16位无符号整数最小值 */
#define EOS_U16_MIN                     0

/** @brief 堆内存最大偏移量
 *
 * 定义为0x7fff（32767），这是15位无符号数能表示的最大值。
 * 用于表示堆中"无有效索引"的特殊值，类似链表中的NULL。
 *
 * 设计理由：
 * - 使用15位是因为堆索引需要用15位存储（最大32767字节）
 * - 留出1位作为其他标志位使用
 * - 之所以不用0xFFFF，是因为有些地方用全1表示无效值
 */
#define EOS_HEAP_MAX                    0x7fff

/* MCU相关类型选择 ----------------------------------------------------------
 *
 * 根据配置的MCU类型，选择最合适的整数类型作为：
 * - eos_mcu_t: MCU数据类型，用于Actor优先级等
 * - eos_sub_t: 订阅标志类型，用于位掩码表示哪些Actor订阅了某事件
 *
 * 这样可以根据MCU字长选择最紧凑的类型，节省内存。
 */

/** @brief 根据MCU类型选择的数据类型
 *
 * - 8位MCU: 使用 eos_u8_t（1字节）
 * - 16位MCU: 使用 eos_u16_t（2字节）
 * - 32位MCU: 使用 eos_u32_t（4字节）
 *
 * 注意：这里说的是MCU的"位数"而非"字节数"。
 * 8位MCU使用1字节存储，16位MCU使用2字节，以此类推。
 */
#if (EOS_MCU_TYPE == 8)
typedef eos_u8_t                        eos_mcu_t;
#elif (EOS_MCU_TYPE == 16)
typedef eos_u16_t                       eos_mcu_t;
#else
typedef eos_u32_t                       eos_mcu_t;
#endif

/** @brief 订阅标志类型
 *
 * 用于表示事件订阅关系的位掩码。
 * 当Actor订阅某主题时，对应位被置1。
 *
 * 例如：0b00001011 表示优先级0、1、3的Actor订阅了该事件。
 *
 * 与 eos_mcu_t 使用相同的类型选择逻辑，保证位运算兼容性。
 */
#if (EOS_MCU_TYPE == 8)
typedef eos_u8_t                        eos_sub_t;
#elif (EOS_MCU_TYPE == 16)
typedef eos_u16_t                       eos_sub_t;
#else
typedef eos_u32_t                       eos_sub_t;
#endif

/* 测试平台类型选择 ----------------------------------------------------------
 *
 * eos_pointer_t 用于指针运算和地址计算。
 *
 * 在嵌入式开发中，目标MCU可能是32位的（使用32位指针），
 * 但测试在PC上进行，PC可能是64位的（使用64位指针）。
 *
 * 通过 TEST_PLATFORM 选择不同的类型，可以：
 * 1. 在32位MCU上使用4字节指针运算
 * 2. 在64位PC上使用8字节指针运算进行单元测试
 */

/** @brief 指针运算类型
 *
 * 用于指针算术运算（如计算结构体成员地址偏移）。
 *
 * - 32位测试平台: eos_u32_t，模拟嵌入式环境的32位地址空间
 * - 64位测试平台: uint64_t，真实反映PC上的指针宽度
 *
 * 注意：stdint.h只在64位平台需要包含，因为32位平台可能没有此头文件。
 */
#if (EOS_TEST_PLATFORM == 32)
typedef eos_u32_t                       eos_pointer_t;
#else
#include <stdint.h>
typedef uint64_t                        eos_pointer_t;
#endif

#ifdef __cplusplus
}
#endif

#endif

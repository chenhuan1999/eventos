# EventOS Nano 实例流程图完整教学

> 基于 STM32F103 LED 示例，将整个 EventOS 框架从启动到事件消费的完整知识串联起来。
>
> 预设你已经读过 [EventOS-Nano-Deep-Reading-Notes.md](EventOS-Nano-Deep-Reading-Notes.md)，这份文档更像"图解复习版"。

---

## 目录

- [1. 示例全景](#1-示例全景)
- [2. 框架启动全流程](#2-框架启动全流程)
- [3. eos_once() 一步调度详解](#3-eos_once-一步调度详解)
  - [3.1 为什么两个 Actor 不能共享同一个优先级？](#31-为什么两个-actor-不能共享同一个优先级)
  - [3.2 "属于它的最老事件" 图解](#32-属于它的最老事件-图解)
  - [3.3 eos_once 是先遍历 Actor 还是先遍历事件？](#33-eos_once-是先遍历-actor-还是先遍历事件)
- [4. 时间事件完整流程](#4-时间事件完整流程)
  - [4.1 三层模型](#41-三层模型)
  - [4.2 时序图：一个 500ms 周期事件的生命](#42-时序图一个-500ms-周期事件的生命)
  - [4.3 分层粒度设计](#43-分层粒度设计)
  - [4.4 时间回绕处理](#44-时间回绕处理)
- [5. 状态机分发详解 (eos_sm_dispath)](#5-状态机分发详解-eos_sm_dispath)
  - [5.1 FSM 模式流程图](#51-fsm-模式流程图)
  - [5.2 HSM 模式完整流程图](#52-hsm-模式完整流程图)
  - [5.3 冒泡机制图解](#53-冒泡机制图解)
  - [5.4 eos_sm_tran()：LCA 计算](#54-eos_sm_tranlca-计算)
  - [5.5 实例推演：sm_led 从创建到事件处理](#55-实例推演sm_led-从创建到事件处理)
    - [5.5.1 状态机相关的类型体系](#551-状态机相关的类型体系)
    - [5.5.2 创建 sm_led 的完整过程](#552-创建-sm_led-的完整过程)
    - [5.5.3 事件到来后状态机如何操作](#553-事件到来后状态机如何操作)
    - [5.5.4 四个返回值把控制流分成了四条路](#554-四个返回值把控制流分成了四条路)
    - [5.5.5 三个关键宏的本质](#555-三个关键宏的本质)
- [6. Heap 与事件内存模型](#6-heap-与事件内存模型)
  - [6.1 两套链关系](#61-两套链关系)
  - [6.2 一条事件的内存布局](#62-一条事件的内存布局)
  - [6.3 分配流程](#63-分配流程)
  - [6.4 取事件流程](#64-取事件流程)
  - [6.5 GC 回收流程](#65-gc-回收流程)
- [7. 一条事件的完整生命周期（合起来看）](#7-一条事件的完整生命周期合起来看)
- [8. 对比表与配置上限](#8-对比表与配置上限)
  - [8.1 Reactor vs 状态机](#81-reactor-vs-状态机)
  - [8.2 各宏控制面](#82-各宏控制面)
  - [8.3 EOS_MAX_ACTORS 最大能设多大？依据是什么？](#83-eos_max_actors-最大能设多大依据是什么)
  - [8.4 每条事件最多能被多少 Actor 订阅？](#84-每条事件最多能被多少-actor-订阅)
- [9. 设计亮点（记住这三点就够了）](#9-设计亮点记住这三点就够了)
- [10. 如果你想继续深入](#10-如果你想继续深入)

---

## 1. 示例全景

本示例运行两个 Actor：

| Actor | 类型 | 优先级 | 订阅事件 | 行为 |
|-------|------|--------|----------|------|
| `sm_led` | 状态机 | 1（高） | `Event_Time_500ms` | on/off 两状态往返迁移 |
| `actor_led` | Reactor | 2（低） | `Event_Time_1000ms` | 收到事件直接翻转 status |

硬件节拍：`SysTick` 每 1ms 中断一次，中断里调 `eos_tick()`。

---

## 2. 框架启动全流程

```
main()
 │
 ├─(1)─ SysTick_Config(1000Hz)     // 硬件 1ms 中断
 │
 ├─(2)─ eos_init()                 // 初始化框架内核单例
 │       └─ eos_clear()            //   清 timer_count
 │       └─ enabled = 1, running = 0
 │       └─ actor_exist = 0
 │       └─ sub_table = NULL (等用户提供)
 │       └─ eos_heap_init()        //   整块 heap → 一个大 free block
 │       └─ time = 0
 │       └─ init_end = 1           //   标记"可启动"
 │
 ├─(3)─ eos_sub_init(table, Max)   // 用户提供订阅表，清零
 │
 ├─(4)─ eos_sm_led_init()          // 注册状态机 Actor
 │       └─ eos_sm_init(priority=1)
 │       └─ eos_sm_start(state_init)
 │             └─ 立刻执行 state_init(Event_Null)
 │             └─ 内部: eos_event_sub(Event_Time_500ms)
 │             └─ 内部: eos_event_pub_period(500ms)  → 登记到 etimer[0]
 │             └─ 返回 EOS_TRAN(state_off)
 │                   └─ HSM 自动处理: Exit(null) → Enter(state_off)
 │
 ├─(5)─ eos_reactor_led_init()     // 注册 Reactor Actor
 │       └─ eos_reactor_init(priority=2)
 │       └─ eos_reactor_start(led_e_handler)
 │       └─ eos_event_sub(Event_Time_1000ms)
 │       └─ eos_event_pub_period(1000ms)  → 登记到 etimer[1]
 │
 └─(6)─ eos_run()                  // 进入主循环，永不返回
         └─ eos_hook_start()
         └─ running = 1
         └─ while (enabled):
               └─ ret = eos_once()        ← 核心一步
                     ├─ 有空闲 → eos_hook_idle()
                     └─ 有错误 → EOS_ASSERT
```

**关键理解**：`eos_init()` 只是把框架带到"可运行"状态，真正让事件流动起来的是 `eos_run()`。

---

## 3. eos_once() 一步调度详解

这是整份代码最重要的函数。每次调用只处理 **一个事件**。

```
eos_once()
 │
 ├─(0)─ 检查 init_end 是否为 1        ← 必须已经 eos_init()
 ├─(1)─ 检查 sub_table != NULL        ← 必须已经 eos_sub_init()
 ├─(2)─ 检查 eos.enabled == 1         ← 如果被 eos_stop() 关了，返回
 ├─(3)─ 检查 actor_exist != 0         ← 至少要注册了 Actor
 │     检查 actor_enabled != 0        ← 至少要启动了 Actor
 │
 ├─(4)─ eos_evttimer()                ← 一看：有没有定时器到期？
 │       └─ 见 §4 时间事件流程
 │
 ├─(5)─ 检查 heap 是否为空            ← 二看：事件队列有没有活？
 │
 ├─(6)─ 扫描优先级找 Actor            ← 三挑：谁有最高优先级且有待处理事件？
 │       对 i = (MAX-1) → 0:
 │         检查 actor_exist & (1<<i)   ← 这个 Actor 注册了吗？
 │         检查 sub_general & (1<<i)   ← 这个 Actor 有事件要处理吗？
 │         → 找到最高优先级的有活 Actor
 │
 │       注意：Actor 的 priority 编号就是它在框架里的唯一身份。
 │       两个 Actor 不能共享同一个 priority——注册时框架会断言
 │       actor_exist 对应位必须为 0（见 §3.1 详解）。
 │
 ├─(7)─ eos_heap_get_block(priority)  ← 四取：从全局队列找"属于它的最老事件"
 │       └─ 见 §6 heap 流程 以及下方 §3.2 图解说明
 │
 ├─(8)─ 内部格式 → 外部格式          ← 五转：拼成 eos_event_t
 │       event.topic = e->topic
 │       event.data  = (e + 1) 的地址
 │       event.size  = block->size - 头开销
 │
 ├─(9)─ 分发                          ← 六发：Reactor 还是状态机？
 │       ├─ StateMachine → eos_sm_dispath(sm, &event)
 │       │                   └─ 见 §5 状态机分发
 │       └─ Reactor      → reactor->event_handler(reactor, &event)
 │                           └─ 直接回调用户函数
 │
 └─(10)─ eos_heap_gc(e)              ← 七收：所有订阅者都消费完了吗？
          └─ sub == 0 → 释放块 + 重算 sub_general
          └─ sub != 0 → 先留着，还有 Actor 没处理
```

**核心位图优化**：
```
sub_general = 全体待处理事件的所有未消费订阅者 OR 起来

例如队列里有事件 A(sub=0b010) 和事件 B(sub=0b100)
→ sub_general = 0b110
→ 扫描到 priority=1 时: (0b110 & 0b010) != 0 → 有活!
→ 扫描到 priority=2 时: (0b110 & 0b100) != 0 → 有活!
```

这比"每次扫描整个事件队列"快得多。

### 3.1 为什么两个 Actor 不能共享同一个优先级？

**结论：优先级编号就是 Actor 的唯一身份证。框架在注册时用断言直接拦死。**

原因要从 Actor 注册代码说起。`eos_actor_init()` 在 [eventos.c:867](eventos/eventos.c#L867) 做了关键检查：

```c
// 每个优先级位只能挂一个 actor
EOS_ASSERT((eos.actor_exist & (1 << priority)) == 0);

// 把 actor 写入框架注册表
eos.actor_exist |= (1 << priority);
eos.actor[priority] = me;           // ← priority 直接当数组下标
me->priority = priority;
```

一个 priority 编号同时承担三种角色：

```
priority = 1 →
  ├─ 位图索引: eos.actor_exist 的 bit 1   → "这个 Actor 是否存在"
  ├─ 数组下标: eos.actor[1]               → "这个 Actor 的指针在哪"
  └─ 调度序号: 扫描时 (1 << 1) 参与 & 运算 → "当前谁有活"
```

如果把两个 Actor 都注册到 priority=1：

```
Actor A 先注册:
  actor_exist |= (1<<1)  →  bit 1 = 1  ✓
  actor[1] = &A

Actor B 后注册:
  (actor_exist & (1<<1)) != 0  →  断言失败！停机！
```

即使去掉断言，actor[1] 也会被 B 覆盖，A 永远丢失——逻辑上说不通。

**那想让两个 Actor "同优先级轮转"怎么办？** EventOS 的方案是给它们分配相邻优先级（如 1 和 2），靠 `eos_once()` 每次只处理一个事件 + `eos_run()` 主循环反复调用的机制，在高优先级无活时自动落到低优先级。

### 3.2 "属于它的最老事件" 图解

`eos_heap_get_block(priority)` 做的事就一句：**从全局事件队列队头开始，找第一条 `sub` 位图里还留着当前 Actor 那一位的事件。** 关键代码在 [eventos.c:1914-1928](eventos/eventos.c#L1914-L1928)。

**单订阅者场景（STM32 LED 示例）**：

```
全局事件队列:
  队头                              队尾
  ┌──────────────────┐   ┌──────────────────┐
  │ Event_Time_500ms │ → │ Event_Time_1000ms│ → END
  │ sub = 0b010      │   │ sub = 0b100       │
  │ (只有 Actor1)     │   │ (只有 Actor2)     │
  └──────────────────┘   └──────────────────┘

sm_led (priority=1) 调 eos_heap_get_block(1):
  → 扫到 Event_Time_500ms: sub & (1<<1) = 0b010 ≠ 0 → 命中！
  → evt->sub &= ~0b010  →  sub 变 0b000
  → 返回这条事件

actor_led (priority=2) 调 eos_heap_get_block(2):
  → 扫到 Event_Time_500ms: sub & (1<<2) = 0b000 = 0 → 跳过
  → 扫到 Event_Time_1000ms: sub & (1<<2) = 0b100 ≠ 0 → 命中！
```

**多订阅者共享同一条事件（这才是 sub 位图真正的威力）**：

假设两个 Actor 都订阅了 `Event_Time_500ms`：

```
队列只有一条事件:
  ┌──────────────────┐
  │ Event_Time_500ms │
  │ sub = 0b110      │  ← bit1=1 (sm_led), bit2=1 (actor_led)
  └──────────────────┘

sm_led (priority=1) 先调度（优先级更高）:
  sub & (1<<1) ≠ 0 → 命中!
  sub &= ~0b010 → sub 变为 0b100
  GC: sub ≠ 0 → 还有 actor_led 没消费 → 保留不释放！

actor_led (priority=2) 后调度:
  sub & (1<<2) ≠ 0 → 命中!
  sub &= ~0b100 → sub 变为 0b000
  GC: sub == 0 → 全消费完了 → 释放！
```

**一句话**："属于它的" = sub 位图里它的那一位还是 1；"最老" = 按入队时间最早。

### 3.3 eos_once 是先遍历 Actor 还是先遍历事件？

**先遍历 Actor（按优先级选人），选定后再为它找事件。不是反过来。**

代码在 [eventos.c:608-631](eventos/eventos.c#L608-L631) 分两步走，中间不交叉：

```c
// 第一步：遍历 Actor（按优先级从高到低）——只碰位图，不碰事件队列
for (eos_s8_t i = (EOS_MAX_ACTORS - 1); i >= 0; i--) {
    if ((eos.actor_exist & (1 << i)) == 0)   continue;  // 没注册
    if ((eos.heap.sub_general & (1 << i)) == 0) continue; // 没事件
    actor = eos.actor[i];   // ← 选定了
    priority = i;
    break;
}

// 第二步：为选定的 Actor 找它最老的未消费事件——才碰事件队列
eos_event_inner_t *e = eos_heap_get_block(&eos.heap, priority);
```

为什么敢这样设计？因为 `sub_general` 是提前算好的全局摘要——它把"每个 Actor 当前有没有活"浓缩成一个位图，选 Actor 时完全不用扫事件队列：

```text
第一步 O(n)：遍历 n 个 Actor（n ≤ EOS_MCU_TYPE，最多 32）
  → 只看 actor_exist + sub_general 两个位图
  → 不碰 q_next 链，不读事件内存

第二步 O(m)：为选中的 Actor 扫事件队列（m = 当前队列长度）
  → 只有被选中的 Actor 走这一步
```

如果反过来"先遍历事件，为每条事件找最适合的 Actor"：

```text
每一步 O(m)：遍历所有事件 → 记录每条属于谁 → 比优先级 → 选最高的
每轮 eos_once 都要扫全队列，没有 sub_general 这种捷径
```

EventOS 的做法把"**选谁干活**"和"**挑哪件活**"拆成了两个独立步骤，`sub_general` 就是让第一步变 O(1) 位运算的关键缓存。

---

## 4. 时间事件完整流程

### 4.1 三层模型

```
Layer 1: 硬件中断           SysTick_Handler() → eos_tick()
Layer 2: 定时器转译         eos_evttimer() → eos_event_pub_topic()
Layer 3: 普通事件消费       eos_once() → Reactor/HSM
```

### 4.2 时序图：一个 500ms 周期事件的生命

```
时间轴 (ms)     事件

  0            eos_event_pub_period(Event_Time_500ms, 500)
               → etimer[0] = {topic, oneshoot=0, period=500, timeout_ms=500}
               → timeout_min = 500

  1            eos_tick()  → time = 1
               eos_once() → eos_evttimer() → 500 > 1 → 没到期

  2~499        同上，每次 tick 推进 time，每次 once 检查不到期

  500          eos_tick()  → time = 500
               eos_once()
                 └─ eos_evttimer()
                      └─ time >= timeout_min (500 >= 500)！
                      └─ eos_event_pub_topic(Event_Time_500ms)
                           └─ eos_event_pub_ret()
                                └─ 创建事件块 → 入队
                      └─ oneshoot=0 → timeout_ms += 500 → 下次 1000ms
                      └─ 重算 timeout_min = 1000

  501          eos_once()
                 └─ sub_general 已有 sm_led 的位
                 └─ 选 priority=1 的 sm_led
                 └─ 取事件 → eos_sm_dispath() 处理

  1000         再次到期，发布，消费...
```

### 4.3 分层粒度设计

**根本动机：`period` 字段只有 16 位，不加粒度压缩最长只能表示 65 秒。**

`eos_event_timer_t` 是一个位域压缩结构体 [eventos.c:197-203](eventos/eventos.c#L197-L203)：

```c
typedef struct eos_event_timer {
    eos_u32_t topic    : 13;   // 主题号
    eos_u32_t oneshoot : 1;    // 一次性 / 周期
    eos_u32_t unit     : 2;    // 时间单位（0~3）
    eos_u32_t period   : 16;   // 周期值 ← 只有 16 位！最大 65535
    eos_u32_t timeout_ms;      // 绝对到期时刻（完整 32 位，不参与压缩）
} eos_event_timer_t;
```

`period` 是 `eos_u16_t`，如果直接存毫秒，最长的周期事件只能到 65535 ms ≈ **65 秒**。加一个 `unit` 字段做"单位换算"，同一段 16 位就能覆盖从毫秒到月：

| unit | timer_unit[unit] | period 能表示的范围 |
| --- | --- | --- |
| 0 (Ms) | 1 ms | 1 ms ~ 65535 ms (≈ 65 s) |
| 1 (100Ms) | 100 ms | 100 ms ~ 6553500 ms (≈ 109 min) |
| 2 (Sec) | 1000 ms | 1 s ~ 65535 s (≈ 18 h) |
| 3 (Minute) | 60000 ms | 1 min ~ 65535 min (≈ 45 day) |

源码中的实际定义 [eventos.c:168-183](eventos/eventos.c#L168-L183)：

```c
static const eos_u32_t timer_threshold[EosTimerUnit_Max] = {
    60000,          // 60 s  —— 超过这个换 100ms 粒度
    6000000,        // 100 min
    57600000,       // 16 h
    1296000000,     // 15 day
};
static const eos_u32_t timer_unit[EosTimerUnit_Max] = {
    1, 100, 1000, 60000
};
```

**自动选择机制**在 `eos_event_pub_time()` [eventos.c:1224-1238](eventos/eventos.c#L1224-L1238)：

```c
// 从最细粒度开始，找第一个"装得下"的粒度
for (eos_u8_t i = 0; i < EosTimerUnit_Max; i++) {
    if (time_ms > timer_threshold[i])
        continue;       // 当前粒度装不下，换下一档
    unit = i;
    if (i == EosTimerUnit_Ms)
        period = time_ms;                            // 毫秒级直接存原值
    else
        period = (time_ms + timer_unit[i]/2) / timer_unit[i];  // 换算并四舍五入
    break;
}
```

两个具体例子：

```text
eos_event_pub_period(Event_X, 500):   time_ms = 500
  500 > 60000(threshold[0])?  否 → unit=Ms, period=500
  → 每 500 ms 触发一次

eos_event_pub_period(Event_Y, 120000):  time_ms = 120000
  120000 > 60000(threshold[0])?   是 → 换下一档
  120000 > 6000000(threshold[1])? 否 → unit=100Ms, period=(120000+50)/100=1200
  → 每 1200×100ms = 120s 触发一次
```

**周期事件续期**在 `eos_evttimer()` [eventos.c:526-527](eventos/eventos.c#L526-L527)，只有一行乘法和加法：

```c
eos_u32_t period = eos.etimer[i].period * timer_unit[eos.etimer[i].unit];
eos.etimer[i].timeout_ms += period;   // 绝对时间 + 实际毫秒周期
```

**整个设计的核心逻辑**：

```text
period:16 bit 是硬约束（位域压缩节省内存）
    ↓
不加 unit → period 只能表示 0~65s
加了 unit → 同一段 16 bit 覆盖 1ms~45 day
    ↓
选粒度只在注册时算一次乘除法
续期只在到期时算一次乘除法
    ↓
运行期只比对 timeout_ms（32 位），不涉及粒度，零换算开销
```

本质是用 2 bit 的 `unit` 做索引，给 16 bit 的 `period` 换了一把刻度可变的尺子。牺牲少量精度（粗粒度下有几 ms~几十 ms 的四舍五入），换来了 16 bit 字段覆盖全量程的能力。

> 原先文档写"减少检查频率"，实际运行期所有定时器每次 `eos_once()` 都会被 `timeout_ms > system_time` 扫描一遍。真正的快速跳过靠的是 `timeout_min`（[eventos.c:506](eventos/eventos.c#L506)），与粒度无关。

### 4.4 时间回绕处理

软件时间在 30 天窗口内循环 (`EOS_MS_NUM_30DAY`)。当旧时间接近上限、新时间跳回小值时：

```
eos_tick():
  old_time 接近 28天
  new_time 跳回 1ms
  → 检测到回绕
  → 所有 etimer[i].timeout_ms -= offset
  → timeout_min -= offset
  → 相对关系全保持，对外无感
```

---

## 5. 状态机分发详解 (eos_sm_dispath)

### 5.1 FSM 模式流程图

```
eos_sm_dispath(sm, event)
 │
 ├─ s = sm->state                 // 保存当前状态
 │
 ├─ r = s(sm, event)             // 让当前状态处理事件
 │
 ├─ r == EOS_Ret_Tran?
 │    │
 │    ├─ YES ──────────────────────────────────┐
 │    │   t = sm->state          // 取出目标状态 │
 │    │   s(sm, Event_Exit)      // 源状态退出  │
 │    │   t(sm, Event_Enter)     // 目标状态进入 │
 │    │   sm->state = t          // 正式提交    │
 │    │                                         │
 │    └─ NO ─────────────────────────────────── │
 │        sm->state = s          // 恢复原状态  │
 │                                              │
 └──────────────────────────────────────────────┘
```

### 5.2 HSM 模式完整流程图

这是框架里最复杂的算法，分 4 个阶段：

```
eos_sm_dispath(sm, event)                          阶段
 │
 ├─ t = sm->state                   // 记住进入前的叶子状态     ╮
 │                                                             │
 ├─ do {                          // 事件冒泡                   │ 阶段1
 │      s = sm->state                                            │ 冒泡找
 │      r = s(sm, event)              // 逐层向上找处理者        │ 处理者
 │   } while (r == EOS_Ret_Super)                                │
 │                                                             ╯
 ├─ r != EOS_Ret_Tran?                                         ╮
 │    YES → sm->state = t; return     // 没迁移，恢复叶子态    │ 阶段2
 │                                                             │ 分支
 │    NO  → 继续下面                                               │
 │                                                                  ╯
 │
 ├─ path[0] = sm->state             // 目标状态                ╮
 │  path[1] = t                     // 旧叶子态                  │
 │  path[2] = s                     // 真正处理事件的源状态     │
 │                                                                │ 阶段3
 ├─ while (t != s)                  // 叶子态逐层 Exit          │ 退出到
 │      t(Event_Exit)                     // 退到源状态层               │ 源层级
 │                                                                   │
 ├─ ip = eos_sm_tran(sm, path)      // 计算 LCA + 进入路径     ╯
 │
 ├─ for (; ip >= 0; --ip)           // 按路径进入               ╮
 │      path[ip](Event_Enter)                                       │
 │                                                                   │ 阶段4
 ├─ t = path[0]; sm->state = t      // 定位在目标状态               │ 进入路径
 │                                                                   │
 ├─ while (t(Event_Init) == Tran)   // Init 链式下钻                │ +下钻
 │      补充子状态路径                                              │
 │      依次 Event_Enter                                             │
 │      t = 最新叶子态                                           ╯
 │
 └─ sm->state = t                   // 最终稳定叶子状态
```

### 5.3 冒泡机制图解

```
         eos_state_top            ← 根：返回 EOS_Ret_Null
              ↑
         state_on                 ← 返回 EOS_SUPER(top)
              ↑
         当前叶子                  ← 这里收到事件
```

当叶子状态说 `EOS_SUPER(parent)` 时，`eos_sm_dispath()` 自动把 `sm->state` 临时改成父状态再调一次。循环直到有人说 `EOS_Ret_Handled` 或 `EOS_Ret_Tran`。

### 5.4 eos_sm_tran()：LCA 计算

```
输入：从 s（源状态）迁移到 t（目标状态）
输出：进入路径 + 起始索引 ip

四种特殊情况（快速路径）：
┌──────────────────────────────────────────────┐
│ s == t                  → 自迁移，Exit 再 Enter 自己    │
│ s == t->super           → 直接进入子状态 t             │
│ s->super == t->super    → 兄弟迁移，Exit s 再 Enter t   │
│ s->super == t           → 向父迁移，只需 Exit s         │
└──────────────────────────────────────────────┘

一般情况（慢路径）：
  1. 沿着 t 往上爬到 eos_state_top，记录目标祖先链
  2. 沿着 s 往上爬，在目标祖先链里找第一个匹配 → 就是 LCA
  3. 返回从 LCA 下一层到 t 的路径
```

### 5.5 实例推演：sm_led 从创建到事件处理

下面以 STM32 LED 示例的 `sm_led` 为实例，把"类型是什么 → 怎么创建 → 事件来了怎么走"整条链路完整串一遍。

---

#### 5.5.1 状态机相关的类型体系

EventOS 的状态机由三层类型嵌套而成，自底向上：

```text
eos_actor_t                    ← 最底层：所有 Actor 的公共基类
  ├─ priority:5  优先级（也是 Actor 的唯一 ID）
  ├─ mode:1      0=Reactor, 1=StateMachine
  └─ enabled:1   是否已启动

eos_sm_t                       ← 中间层：继承 eos_actor_t，追加状态机字段
  ├─ super       eos_actor_t（嵌入继承）
  └─ state       当前状态函数指针  ← 这是状态机的核心

用户自定义结构体                 ← 最外层：业务字段
  ├─ super       eos_sm_t（嵌入继承）
  └─ status      业务变量（如 LED 亮灭标志）
```

**三层嵌套的 C 语言本质：首成员地址等价。**

`super` 始终是每个结构体的第一个成员。这意味着在内存里 `&outer == &outer.super`——外层的地址和内嵌基类的地址完全相同。所以 `(eos_actor_t *)&sm`、`(eos_sm_t *)&sm_led` 这些跨层强转都是安全的，没有偏移，没有虚表，零运行时开销。

三层各自的职责边界：

```text
eos_actor_t   → 框架调度器看得懂的最小接口
                 优先级、类型、启停。影响 eos_once() 选 Actor

eos_sm_t      → 状态机分发器看得懂的接口
                 当前状态指针。影响 eos_sm_dispath()、eos_sm_start()

用户结构体     → 只有用户自己的状态函数看得懂
                 LED 状态、传感器数据等。框架完全不知道这层的存在
                 在状态函数里通过 (eos_sm_led_t *)me 安全取回
```

对应源码定义：

```c
// eventos.h:264 — Actor 基类
typedef struct eos_actor {
    eos_u32_t priority  : 5;   // 0~31，越小优先级越低
    eos_u32_t mode      : 1;   // 0=Reactor, 1=StateMachine
    eos_u32_t enabled   : 1;
} eos_actor_t;

// eventos.h:323 — 状态机类
typedef struct eos_sm {
    eos_actor_t super;                    // 嵌入基类
    volatile eos_state_handler state;     // 当前状态函数
} eos_sm_t;

// 示例文件 eos_led_sm.c:11 — 用户自定义
typedef struct eos_sm_led_tag {
    eos_sm_t super;       // 嵌入状态机
    eos_u8_t status;      // 业务字段
} eos_sm_led_t;
```

状态处理函数的签名 [eventos.h:236](eventos/eventos.h#L236)：

```c
typedef eos_ret_t (* eos_state_handler)(struct eos_sm *const me, eos_event_t const * const e);
//                                        ↑ 参数是 eos_sm_t *           ↑ 事件只读
//                      ↑ 返回 eos_ret_t，告诉分发器怎么做
```

返回值 `eos_ret_t` [eventos.h:173-178](eventos/eventos.h#L173-L178)：

| 返回值 | 含义 | 分发器的反应 |
| --- | --- | --- |
| `EOS_Ret_Null` | 完全未处理 | 通常冒泡给父状态 |
| `EOS_Ret_Handled` | 已处理，不迁移 | 当前状态保持不变 |
| `EOS_Ret_Super` | 交给父状态处理 | 分发器临时切换到父状态再问一次（HSM） |
| `EOS_Ret_Tran` | 请求状态迁移 | 分发器执行 Exit → LCA 计算 → Enter 路径 |

---

#### 5.5.2 创建 sm_led 的完整过程

用户代码在 [eos_led_sm.c:31-41](examples/stm32f103/User/eos_led_sm.c#L31-L41)：

```c
void eos_sm_led_init(void)
{
    eos_sm_init(&sm_led.super, 1, EOS_NULL);   // Step A: 注册
    eos_sm_start(&sm_led.super, EOS_STATE_CAST(state_init)); // Step B: 启动
    sm_led.status = 0;
}
```

**Step A — `eos_sm_init()`** [eventos.c:935-945](eventos/eventos.c#L935-L945)：

```c
void eos_sm_init(eos_sm_t * const me, eos_u8_t priority, void const * const parameter)
{
    eos_actor_init(&me->super, priority, parameter);  // 注册到框架
    me->super.mode = EOS_Mode_StateMachine;            // 标记为状态机
    me->state = eos_state_top;                         // 默认指向顶层伪状态
}
```

`eos_actor_init()` 内部 [eventos.c:849-878](eventos/eventos.c#L849-L878)：

```c
// 断言：框架已初始化、订阅表已就绪、参数合法
EOS_ASSERT(eos.init_end == 1);
EOS_ASSERT(eos.sub_table != NULL);
EOS_ASSERT(priority < EOS_MAX_ACTORS);

// 断言：这个优先级还没被占用 —— priority 就是唯一 ID
EOS_ASSERT((eos.actor_exist & (1 << priority)) == 0);

// 注册到框架
eos.actor_exist |= (1 << priority);    // 位图标记：这个 priority 已占用
eos.actor[priority] = me;              // 数组按 priority 索引存放指针
me->priority = priority;               // Actor 自身记住自己的编号
```

此时框架内部状态：

```text
eos.actor_exist    = 0b010          (bit 1 = priority=1 已注册)
eos.actor[1]       = &sm_led.super.super  (指向 eos_actor_t 层级)
eos.actor_enabled  = 0b000          ← 还没启动！调度器不会选它
me->super.priority = 1
me->super.mode     = StateMachine
me->super.enabled  = False
me->state          = eos_state_top  ← 默认指向顶层伪状态
sm_led.status      = 0              ← 业务字段还是初始值
```

**Step B — `eos_sm_start()`** [eventos.c:969-1032](eventos/eventos.c#L969-L1032)：

```c
void eos_sm_start(eos_sm_t * const me, eos_state_handler state_init)
{
    me->state = state_init;                              // 1. 安装初始状态函数
    me->super.enabled = EOS_True;                        // 2. 标记为已启动
    eos.actor_enabled |= (1 << me->super.priority);      // 3. 登记全局启用位图

    t = me->state;
    eos_ret_t ret = t(me, &eos_event_table[Event_Null]); // 4. 触发初始迁移
    EOS_ASSERT(ret == EOS_Ret_Tran);                     //    必须无条件迁移！
    // ... HSM 初始化进入路径 ...
}
```

第 4 步框架以 `Event_Null` 调用 `state_init()` [eos_led_sm.c:44-58](examples/stm32f103/User/eos_led_sm.c#L44-L58)：

```c
static eos_ret_t state_init(eos_sm_led_t * const me, eos_event_t const * const e)
{
    (void)e;

    // 订阅：在订阅表里把 sm_led 的 bit 写上
    EOS_EVENT_SUB(Event_Time_500ms);
    // → eos.sub_table[Event_Time_500ms] |= (1 << 1)

    // 发布 500ms 周期事件，登记到 etimer[] —— 见下方"事件创建"
    eos_event_pub_period(Event_Time_500ms, 500);

    // 无条件迁移到 state_off
    return EOS_TRAN(state_off);
}
```

`EOS_TRAN(state_off)` 展开为 `eos_tran(me, state_off)` [eventos.h:527](eventos/eventos.h#L527)：

```c
// eos_tran 只做两件事：
me->state = state_off;       // ← 关键：在函数返回前就把目标状态写入了 me->state！
return EOS_Ret_Tran;         // ← 返回值告诉分发器"我需要迁移"
```

**注意此时 `me->state` 已经被改成了 `state_off`，但还没有执行任何 Exit/Enter。** `me->state` 在这里承担双重角色——对外是"目标状态的暂存器"，对内是"告诉分发器目的地是什么"。

`eos_sm_start()` 收到 `EOS_Ret_Tran` 后，HSM 模式自动完成初始进入链：

```text
1. 从 eos_state_top 向下探路径:
   path[0] = state_off
   对 state_off 发 Event_Null → state_off 说自己父状态是 eos_state_top
   me->state == eos_state_top → 停止上探

2. 对 path 中的状态依次发 Event_Enter（从父到子）:
   state_off(Event_Enter) → me->status = 0; return EOS_Ret_Handled

3. 对 state_off 发 Event_Init:
   state_off 没有默认子状态 → default: return EOS_SUPER(eos_state_top)
   → eos_state_top(Event_Init) → return EOS_Ret_Null (不再下钻)

4. me->state = state_off  ← 稳定在初始叶子状态
```

**创建完成后的完整快照**：

```text
// 框架层面
eos.actor_exist    = 0b010
eos.actor_enabled  = 0b010
eos.actor[1]       → sm_led.super.super

// Actor 层面
sm_led.super.super.priority = 1
sm_led.super.super.mode     = StateMachine
sm_led.super.super.enabled  = True

// 状态机层面
sm_led.super.state          = state_off   ← 稳定状态
sm_led.status               = 0           ← 灯灭

// 订阅和定时器层面
eos.sub_table[Event_Time_500ms] = 0b010   ← sm_led 的 bit 已写入
eos.etimer[0] = {
    .topic      = Event_Time_500ms,
    .oneshoot   = False,
    .unit       = Ms,
    .period     = 500,
    .timeout_ms = 500          // T=0 + 500ms
}
eos.timeout_min = 500
eos.timer_count = 1
```

---

#### 5.5.3 事件到来后状态机如何操作

假设 T=500ms 已到。`eos_evttimer()` 扫描 `etimer[]`，发现 `500 >= timeout_min(500)`，遍历到 `etimer[0].timeout_ms(500) <= system_time(500)`，调用 `eos_event_pub_topic(Event_Time_500ms)`。

事件创建过程 [eventos.c:1070-1130](eventos/eventos.c#L1070-L1130)：

```c
// 1. 进入临界区，向 heap 申请一块内存
eos_port_critical_enter();
eos_event_inner_t *e = eos_heap_malloc(&eos.heap, sizeof(eos_event_inner_t));

// 2. 写入事件头
e->topic = Event_Time_500ms;                      // 主题
e->sub   = eos.sub_table[Event_Time_500ms];       // 订阅快照 = 0b010

// 3. 更新全局摘要位图
eos.heap.sub_general |= e->sub;                   // 0 | 0b010 = 0b010

eos_port_critical_exit();
```

此时内核状态：

```text
全局事件队列:  [Event_Time_500ms: sub=0b010] → END
sub_general = 0b010   (Actor1 有活)
```

下一次 `eos_once()` 选 Actor [eventos.c:612-619](eventos/eventos.c#L612-L619)：

```c
for (eos_s8_t i = 3; i >= 0; i--) {
    i=3: actor_exist & 0b1000 = 0 → 跳过
    i=2: actor_exist & 0b0100 ≠ 0 (actor_led 存在)
         sub_general & 0b0100 = 0b010 & 0b0100 = 0 → 没活，跳过
    i=1: actor_exist & 0b0010 ≠ 0 (sm_led 存在)
         sub_general & 0b0010 = 0b010 & 0b0010 ≠ 0 → 有活！
    → actor = eos.actor[1], priority = 1, break
}
```

取事件 `eos_heap_get_block(priority=1)` [eventos.c:1914-1928](eventos/eventos.c#L1914-L1928)：

```c
// 从队头扫: Event_Time_500ms
// evt->sub & (1<<1) = 0b010 & 0b010 ≠ 0 → 命中！
evt->sub &= ~(1<<1);    // 0b010 → 0b000，标记已消费
```

转换为对外 `eos_event_t`，然后分支到状态机：

```c
eos_sm_t *sm = (eos_sm_t *)actor;     // 安全强转，首成员地址等价
eos_sm_dispath(sm, &event);
```

**进入 `eos_sm_dispath()` FSM 分支逐行执行** [eventos.c:1389-1411](eventos/eventos.c#L1389-L1411)：

```c
// 第 1 行：保存当前状态
eos_state_handler s = me->state;       // s = state_off

// 第 2 行：让当前状态处理外部事件
r = s(me, e);                          // → state_off(sm, &{Event_Time_500ms})
```

进入 `state_off` [eos_led_sm.c:84-99](examples/stm32f103/User/eos_led_sm.c#L84-L99)：

```c
switch (e->topic) {
    case Event_Time_500ms:             // ← 命中！
        return EOS_TRAN(state_on);
        // 展开为:
        //   me->state = state_on;     // ← 目标状态已写入！但 Exit/Enter 还没做
        //   return EOS_Ret_Tran;
```

回到分发器继续：

```c
// 第 3 行：判断是否迁移
if (r == EOS_Ret_Tran) {               // ← 是！

    // 第 4 行：取出目标状态
    t = me->state;                     // t = state_on

    // 第 5 行：对源状态发 Event_Exit
    r = s(me, &eos_event_table[Event_Exit]);   // state_off(Event_Exit)
    // → 用户代码 default: return EOS_SUPER(eos_state_top)
    // → eos_state_top(Event_Exit) → return EOS_Ret_Null
    //    (Null 在这里被宽容，因为顶层伪状态不处理 Exit)

    // 第 6 行：对目标状态发 Event_Enter
    r = t(me, &eos_event_table[Event_Enter]);  // state_on(Event_Enter)
    // → me->status = 1; return EOS_Ret_Handled

    // 第 7 行：正式提交
    me->state = t;                     // state = state_on
}
```

状态迁移全过程状态图：

```text
sm->state = state_off ─────────────────────────────────────────────
    │
    ├─ state_off(Event_Time_500ms)
    │    → eos_tran(state_on)
    │    → me->state 暂变为 state_on  (但分发器还没做动作)
    │    → 返回 EOS_Ret_Tran
    │
    ├─ 分发器检测到 EOS_Ret_Tran
    │
    ├─ state_off(Event_Exit)          ← 旧状态收尾
    │
    ├─ state_on(Event_Enter)          ← 新状态初始化
    │    → me->status = 1
    │
    └─ me->state = state_on           ← 正式切换

sm->state = state_on ──────────────────────────────────────────────
```

**处理完回到 eos_once()**，调用 `eos_heap_gc()` 回收：`evt->sub == 0b000` → 从队列摘除 → `eos_heap_free()` 归还内存 → 重算 `sub_general`（队列已空，变为 0）。

下一次 500ms 事件到来时，`state_on` 收到 `Event_Time_500ms` → `EOS_TRAN(state_off)` → Exit(state_on) → Enter(state_off, status=0)，如此循环。

---

#### 5.5.4 四个返回值把控制流分成了四条路

```text
EOS_Ret_Handled:
  → 事件已消费，不迁移
  → 分发器: me->state = s (恢复原状态)
  → 状态机继续停在当前状态

EOS_Ret_Null:
  → 完全没处理
  → 分发器: me->state = s (恢复)
  → HSM 下由上层再处理，FSM 下等于被忽略

EOS_Ret_Super:  (仅 HSM)
  → 不处理，交给父状态
  → 分发器: me->state = super, 再循环调一次
  → 形成冒泡链

EOS_Ret_Tran:
  → 请求迁移
  → eos_tran 内部已把 me->state 改为目标状态
  → 分发器: 执行 Exit(s) → Enter(t) → me->state = t
  → 状态机切换到目标状态
```

#### 5.5.5 三个关键宏的本质

```c
// eventos.h:527 — EOS_TRAN: 声明迁移目标
#define EOS_TRAN(target)  eos_tran((eos_sm_t *)me, (eos_state_handler)target)
// 内部: me->state = target; return EOS_Ret_Tran;
// 状态函数不负责做 Exit/Enter，只负责"告诉分发器要跳到谁"

// eventos.h:538 — EOS_SUPER: 声明交给父状态
#define EOS_SUPER(super)  eos_super((eos_sm_t *)me, (eos_state_handler)super)
// 内部: me->state = super; return EOS_Ret_Super;
// 分发器会继续用新 state 再调一次，形成冒泡循环

// eventos.h:541 — EOS_STATE_CAST: 类型转换辅助
#define EOS_STATE_CAST(state)  ((eos_state_handler)(state))
```

**核心设计思想**：状态函数只管"声明意图"（通过返回值 + 临时写 `me->state`），真正的 **Exit / Enter / LCA 计算 / Init 下钻** 全由 `eos_sm_dispath()` 统一完成。状态函数写起来像 switch-case，但背后是一套完整的 HSM 生命周期引擎。

---

## 6. Heap 与事件内存模型

### 6.1 两套链关系

```
物理布局 (next/last)           事件队列 (q_next/q_last)
─────────────────────          ────────────────────────
                                 queue_head
[block A: used] ←→ [B: free]     ↓
   ↕ q_next                      A (最早) → C (次早) → D (最新)
   ↓                             
[block C: used] ←→ [D: used]
   ↕ q_next
   ↓
[block D: used] ←→ [E: free]
```

- `next/last` 管理**物理内存相邻关系**，用于分配时切块、释放时合并
- `q_next/q_last` 管理**事件到达顺序**，用于 eos_heap_get_block() 找最老事件

### 6.2 一条事件的内存布局

```
┌──────────────┬────────────────────┬──────────────┐
│  eos_block_t │ eos_event_inner_t  │   payload    │
│   (块头)     │    (事件头)         │   (数据区)    │
├──────────────┼────────────────────┼──────────────┤
│ next, last   │ sub (订阅位图)      │  用户数据     │
│ q_next,q_last│ topic              │               │
│ offset, size │                    │               │
│ free 标志    │                    │               │
└──────────────┴────────────────────┴──────────────┘
                 ↑
                 这就是 eos_event_t.data 指向的位置
```

### 6.3 分配流程

```
eos_event_pub_ret(topic, data, size)
 │
 ├─ eos_heap_malloc( &heap, sizeof(inner) + data_size )
 │    │
 │    ├─ 从 next=0 开始 First-Fit 扫描 free block
 │    ├─ size 对齐到 4 字节
 │    ├─ 如果剩余空间够一个 block 头 → 切块
 │    │   前半段 = 已分配
 │    │   后半段 = 新 free block
 │    └─ 挂到事件队列尾部 (通过 q_next/q_last)
 │
 ├─ 写入 eos_event_inner_t
 │    e->topic = topic
 │    e->sub   = sub_table[topic]  // 快照：此刻谁订阅了我
 │
 ├─ 复制 payload 到数据区
 │
 └─ sub_general |= e->sub  // 全局摘要位图更新
```

### 6.4 取事件流程

```
eos_heap_get_block(&heap, priority)
 │
 ├─ 从队列头开始扫描 (按 q_next 链)
 ├─ 找第一条 evt->sub & (1 << priority) != 0 的事件
 │    → 这条事件"属于"当前 Actor
 │
 └─ evt->sub &= ~(1 << priority)   // 清掉这一位："已消费"
```

### 6.5 GC 回收流程

```
eos_heap_gc(&heap, evt)
 │
 ├─ evt->sub != 0?
 │    YES → 还有 Actor 没消费 → return (不释放!)
 │
 ├─ evt->sub == 0?
 │    YES → 从事件队列摘除
 │         ├─ 只有这一块 → queue = MAX, empty = 1
 │         ├─ 在队头     → queue = q_next
 │         ├─ 在队尾     → 前一块的 q_next = MAX
 │         └─ 在中间     → 前一块 q_next = 当前块 q_next
 │
 ├─ eos_heap_free(block)
 │    ├─ 标记 free
 │    ├─ 前一块 free? → 合并
 │    └─ 后一块 free? → 合并
 │
 └─ 重新计算 sub_general
      遍历队列所有事件，OR 起所有 sub
```

---

## 7. 一条事件的完整生命周期（合起来看）

以 STM32 LED 示例的 `Event_Time_500ms` 为例：

```
时刻 T=500ms:

  1. eos_once() 调 eos_evttimer()
  2. eos_evttimer() 发现 etimer[0] 到期 (500 >= 500)
  3. 调 eos_event_pub_topic(Event_Time_500ms)
       └─ eos_event_pub_ret(topic, NULL, 0)
            └─ eos_heap_malloc()      → 分配块,入队
            └─ 写 e->topic = Event_Time_500ms
            └─ 写 e->sub = sub_table[topic]  → 只有 Actor1 的位
            └─ sub_general |= e->sub  → 标记 Actor1 有活

时刻 T=501ms (下一次 eos_once):

  4. eos_once() 扫描 priority:
       检查 priority=1: sub_general & (1<<1) != 0 → 找到 sm_led!
  5. eos_heap_get_block(priority=1)
        └─ 从队头扫: e->sub & (1<<1) ? 找到!
        └─ e->sub &= ~(1<<1)   → 清掉 Actor1 的位
  6. 转成 eos_event_t: topic=Event_Time_500ms, data=NULL, size=0
  7. actor->mode == StateMachine → eos_sm_dispath(sm, &event)
        └─ 当前 state=state_off
        └─ state_off 收到 Event_Time_500ms
        └─ 返回 EOS_TRAN(state_on)
        └─ 分发器执行: Exit(state_off) → Enter(state_on)
        └─ state_on 收到 Event_Enter → me->status = 1
  8. eos_heap_gc(e)
        └─ e->sub == 0?  (只有 Actor1 订阅,已经清了)
        └─ YES → 从队列摘除 + eos_heap_free + 重算 sub_general
        └─ sub_general 变为空 (队列已空)

事件消失。下一次 500ms 事件会在 T=1000ms 再次诞生。
```

---

## 8. 两张对比表

### 8.1 Reactor vs 状态机

| | Reactor | StateMachine |
|---|---|---|
| 结构体 | `eos_reactor_t` + 业务字段 | `eos_sm_t` + 业务字段 |
| 初始化 | `eos_reactor_init()` + `_start()` | `eos_sm_init()` + `eos_sm_start()` |
| 处理方式 | 一个回调处理所有事件 | 每个状态一个处理函数 |
| 事件分发 | `reactor->event_handler()` | `eos_sm_dispath()` |
| 状态记忆 | 靠业务变量 (如 status) | 靠状态函数指针 + HSM 层级 |
| 适用场景 | 简单事件响应 | 复杂行为协议 |

### 8.2 各宏控制面

| 宏 | 控制面 |
|---|---|
| `EOS_MCU_TYPE` | 位图底层类型宽度 |
| `EOS_MAX_ACTORS` | Actor 数量上限、优先级范围 |
| `EOS_USE_SM_MODE` | 是否有状态机 (关闭则只有 Reactor) |
| `EOS_USE_HSM_MODE` | 状态机是否有层级冒泡能力 |
| `EOS_USE_PUB_SUB` | 按订阅分发 vs 全广播 |
| `EOS_USE_TIME_EVENT` | 是否有延时/周期事件 |
| `EOS_USE_EVENT_DATA` | 事件能否携带 payload |
| `EOS_TICK_MS` | 时间事件精度 |
| `EOS_USE_ASSERT` | 运行时自检 |
| `EOS_USE_MAGIC` | 内存踩踏检测 |

### 8.3 EOS_MAX_ACTORS 最大能设多大？依据是什么？

**硬上限不是作者拍脑袋定的，是 `eos_mcu_t` 位宽从数学上卡死的。**

核心因果链只有三步：

**第一步：Actor 的一切操作都是 `1 << priority`。**

`1 << priority` 在框架里出现了 7 处，涵盖 Actor 的整个生命周期：

```
注册去重:  eos.actor_exist  &  (1 << priority)    // eventos.c:867
注册写入:  eos.actor_exist  |= (1 << priority)     // eventos.c:871
调度存在:  eos.actor_exist  &  (1 << i)            // eventos.c:613
调度有活:  sub_general      &  (1 << i)            // eventos.c:615
事件订阅:  evt->sub         &  (1 << priority)     // eventos.c:1919
事件消费:  evt->sub        &= ~(1 << priority)     // eventos.c:1927
```

**第二步：这些位图变量的类型是 `eos_mcu_t`。**

[eventos_def.h:133-139](eventos/eventos_def.h#L133-L139)：

```c
EOS_MCU_TYPE = 8   →  eos_mcu_t = eos_u8_t   →  8 位
EOS_MCU_TYPE = 16  →  eos_mcu_t = eos_u16_t  →  16 位
EOS_MCU_TYPE = 32  →  eos_mcu_t = eos_u32_t  →  32 位
```

用到 `eos_mcu_t` 的字段：

| 字段 | 位置 |
| --- | --- |
| `eos.actor_exist` | eventos.c:299 |
| `eos.actor_enabled` | eventos.c:300 |
| `eos.heap.sub_general` | eventos.c:272 |
| `evt->sub` | eventos.c:241 |

**第三步：如果 priority ≥ 位宽，`1 << priority` 就溢出。**

```text
假设 EOS_MCU_TYPE = 8，eos_mcu_t = u8：

priority = 7 → 1 << 7 = 0x80  → u8 装得下 ✓
priority = 8 → 1 << 8 = 0x100 → u8 只有 8 bit，溢出 ✗
```

所以完整的约束链是：

```text
EOS_MCU_TYPE
    │
    ▼
eos_mcu_t 的位宽 (= 8 / 16 / 32)
    │
    ▼
所有位图字段最大只能表示 bit 0 ~ bit (位宽-1)
    │
    ▼
priority 只能是 0 ~ (位宽-1)
    │
    ▼
EOS_MAX_ACTORS ≤ 位宽 = EOS_MCU_TYPE
```

**一张表定论**：

| EOS_MCU_TYPE | eos_mcu_t | 最大 Actor 数 | 根本原因 |
| --- | --- | --- | --- |
| 8 | u8 | 8 | `1 << 7` 是最后有效位 |
| 16 | u16 | 16 | `1 << 15` 是最后有效位 |
| 32 | u32 | 32 | `1 << 31` 是最后有效位 |

**为什么不用 u32 固定下来？** 在 8 位 MCU 上，32 位位图每次位操作要拆成 4 条指令，既慢又占空间。把位图宽度和 MCU 字长绑定，是在性能和内存之间取平衡。

### 8.4 每条事件最多能被多少 Actor 订阅？

**跟 EOS_MAX_ACTORS 是同一个上限——取决于 `eos_sub_t` 的位宽。**

事件内部的 `sub` 位图和订阅表的每一项都是 `eos_sub_t` 类型，定义在 [eventos_def.h:150-156](eventos/eventos_def.h#L150-L156)：

```c
EOS_MCU_TYPE = 8   →  eos_sub_t = eos_u8_t   →  一条事件最多 8 个订阅者
EOS_MCU_TYPE = 16  →  eos_sub_t = eos_u16_t  →  一条事件最多 16 个订阅者
EOS_MCU_TYPE = 32  →  eos_sub_t = eos_u32_t  →  一条事件最多 32 个订阅者
```

代码里用到 `eos_sub_t` 的位置：

| 位置 | 代码 | 含义 |
| --- | --- | --- |
| eventos.c:241 | `eos_sub_t sub` | 事件内部："还有哪些 Actor 没消费" |
| eventos.c:272 | `eos_sub_t sub_general` | 全局摘要："哪些 Actor 有活" |
| 用户提供 | `eos_sub_t sub_table[topic]` | 订阅表："哪些 Actor 订阅了此 topic" |

逻辑上这也是自恰的：系统里最多只有 EOS_MAX_ACTORS 个 Actor，即使它们全部订阅同一条事件，需要的位数也不会超过 EOS_MAX_ACTORS。所以**订阅者上限 = Actor 总数上限 = EOS_MCU_TYPE 的位宽**。

| EOS_MCU_TYPE | 最大 Actor 数 | 每条事件最大订阅者 |
| --- | --- | --- |
| 8 | 8 | 8 |
| 16 | 16 | 16 |
| 32 | 32 | 32 |

当前工程 `EOS_MCU_TYPE = 32`，所以每条事件最多 **32** 个订阅者，不是固定 8。

---

## 9. 设计亮点（记住这三点就够了）

### 9.1 全局一条队列 + 内部订阅位图

事件不在 Actor 各自队列里，而在一条全局队列里。每条事件内部有一个 `sub` 位图记录"还有谁没消费"。多个 Actor 共享同一条事件而无需复制。

### 9.2 时间事件 = 延迟发布普通事件

定时器到期后自动调用 `eos_event_pub_topic()`，之后和普通事件完全一致。没有两条分发通路。

### 9.3 状态函数只管"声明"，不管"执行"

状态函数通过返回值 (`Handled/Super/Tran`) 声明意图，真正的 Exit/Enter/LCA 计算全由分发器统一完成。状态函数写起来像 switch-case，但背后是完整的 HSM 语义。

---

## 10. 如果你想继续深入

1. **代码审查** — 对着 `eventos.c` 的 `eos_sm_dispath()` HSM 分支和 heap GC 做边界测试，验证笔记标记的几个可疑点
2. **性能分析** — `sub_general` 全量重算 vs 增量维护的取舍；First-Fit 的碎片化程度
3. **功能裁剪** — 把 `EOS_USE_EVENT_DATA` 和"事件队列存储"拆开，做一版真正的无 heap 模式
4. **移植实践** — 把当前 STM32F103 示例移植到另一个 MCU，感受 `eos_port_*` 层的工作量

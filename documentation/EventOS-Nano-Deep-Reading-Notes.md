# EventOS Nano 框架精读笔记

## 1. 说明

这份笔记基于以下文件的逐段精读整理而成，目标是给后续阅读、维护、移植、代码审查提供一份结构化参考。

- `eventos/eventos_config.h`
- `eventos/eventos_def.h`
- `eventos/eventos.h`
- `eventos/eventos.c`

这份笔记不追求逐字翻译源码注释，而是重点总结：

- 每组配置项是做什么的
- 为什么这么设计
- 会影响哪些结构、函数和运行路径
- 事件在框架内部是如何流动的
- 当前实现里有哪些值得留意的疑点或潜在风险

## 2. 整体架构一图看懂

EventOS Nano 可以理解为一个“事件驱动内核”，核心由五部分组成：

1. Actor 模型  
   Actor 是最基本执行单元，分为 `Reactor` 和 `StateMachine` 两种模式。

2. 事件模型  
   事件由 `topic + payload` 组成，内部入队时还带有“未消费订阅者位图”。

3. 调度模型  
   全局只有一条事件队列，调度器按优先级选择 Actor，再从全局队列里找出“属于它的最老事件”。

4. 时间模型  
   软件时间由 `eos_tick()` 推进，定时事件最终会被转换成普通事件。

5. 内存模型  
   事件 payload 使用内部固定大小 heap 存储，支持块切分、队列挂接、消费后 GC、相邻块合并。

主调用链可以概括为：

`eos_init()`  
-> `eos_sub_init()`  
-> Actor 初始化/启动  
-> `eos_run()`  
-> 循环调用 `eos_once()`  
-> `eos_once()` 内部先处理定时器，再取事件，再分发给 Reactor/HSM  
-> 处理完成后做 GC 和回收

## 3. `eventos_config.h` 精读

### 3.1 通用配置

#### `EOS_MCU_TYPE`

- 作用：定义目标 MCU 位宽，只允许 `8/16/32`
- 设计意图：让框架内部位图、优先级掩码、订阅掩码尽量贴合目标平台字长
- 直接影响：
  - `eventos_def.h` 中 `eos_mcu_t`
  - `eventos_def.h` 中 `eos_sub_t`
  - `eventos.h` 中 `eos_topic_t`
  - `eventos.h` 中 `eos_actor_t` 位段底层类型
- 当前配置：`32`

#### `EOS_MAX_ACTORS`

- 作用：定义系统中最大 Actor 数量
- 设计意图：Actor 存在状态、启用状态、订阅关系都是用位图表示，所以最大 Actor 数不能超过位图宽度
- 直接影响：
  - `eventos.c` 中 `eos.actor[EOS_MAX_ACTORS]`
  - 调度扫描逻辑
  - 优先级合法性判断
- 当前配置：`4`

#### `EOS_TEST_PLATFORM`

- 作用：决定测试平台上的 `eos_pointer_t` 位宽，只允许 `32/64`
- 设计意图：目标 MCU 和宿主测试平台位宽可能不同，内部地址运算必须与宿主平台一致
- 直接影响：
  - `eventos_def.h` 中 `eos_pointer_t`
  - `eventos.c` 中 heap 地址计算、事件头偏移计算
- 当前配置：`32`

#### `EOS_TICK_MS`

- 作用：每调用一次 `eos_tick()`，软件时间推进多少毫秒
- 设计意图：显式绑定“硬件中断节拍”和“框架逻辑时间基准”
- 直接影响：
  - `eos_tick()`
  - 时间事件精度
  - 定时器回绕处理
- 当前配置：`1`

#### `EOS_USE_MAGIC`

- 作用：开启后在关键结构体中写入 magic number，用于检测内存踩踏
- 设计意图：开发期用空间换诊断能力
- 直接影响：
  - `eos_actor_t`
  - `eos_heap_t`
  - `eos_t`
  - `eos_init()` 和 `eos_run()` 中的完整性检查
- 当前配置：`0`

### 3.2 断言与状态机配置

#### `EOS_USE_ASSERT`

- 作用：控制 `EOS_ASSERT` 宏是否真正生效
- 打开后行为：
  - 调用 `eos_hook_stop()`
  - 进入临界区
  - 调用 `eos_port_assert(__LINE__)`
- 设计意图：开发期快速停机定位错误，而不是带着坏状态继续跑
- 当前配置：`1`

#### `EOS_USE_SM_MODE`

- 作用：开启状态机模式
- 设计意图：把简单 Reactor 模式和状态机模式在编译期分档
- 直接影响：
  - 保留事件主题 `Event_Enter/Event_Exit`
  - `eos_ret_t`
  - `eos_state_handler`
  - `eos_sm_t`
  - `eos_once()` 中 Reactor / StateMachine 分支
- 当前配置：`1`

#### `EOS_USE_HSM_MODE`

- 作用：开启层次状态机 HSM
- 设计意图：允许子状态向父状态冒泡，减少平面 FSM 的逻辑复制
- 直接影响：
  - `Event_Init`
  - `EOS_Ret_Super`
  - `eos_sm_start()`
  - `eos_sm_dispath()`
  - `eos_sm_tran()`
- 当前配置：`1`

#### `EOS_MAX_HSM_NEST_DEPTH`

- 作用：限制 HSM 最大嵌套深度
- 设计意图：限制状态迁移路径数组大小和转换算法复杂度
- 直接影响：
  - `eos_sm_start()` 中 `path[]`
  - `eos_sm_dispath()` 中 `path[]`
  - `eos_sm_tran()` 中路径计算
- 当前配置：`4`

### 3.3 事件分发、时间事件、事件数据

#### `EOS_USE_PUB_SUB`

- 作用：开启发布/订阅模式
- 打开后：事件按 topic 分发给订阅者
- 关闭后：事件广播给所有已存在 Actor
- 设计意图：把发布者与处理者解耦
- 直接影响：
  - `eos.sub_table`
  - `eos_sub_init()`
  - `eos_event_sub()`
  - `eos_event_unsub()`
  - `eos_event_pub_ret()` 中事件订阅快照写入
- 当前配置：`1`

#### `EOS_USE_TIME_EVENT`

- 作用：开启延时事件和周期事件
- 设计意图：让“时间触发”最终仍归并到普通事件模型中
- 直接影响：
  - `eos_event_pub_delay()`
  - `eos_event_pub_period()`
  - `eos_event_pub_time()`
  - `eos_evttimer()`
  - `eos_time()`
  - `eos_tick()`
- 当前配置：`1`

#### `EOS_MAX_TIME_EVENT`

- 作用：同时存在的时间事件上限
- 设计意图：固定容量，避免动态扩容
- 直接影响：
  - `eos.etimer[EOS_MAX_TIME_EVENT]`
  - `eos.timer_count` 上限
- 当前配置：`4`

#### `EOS_USE_EVENT_DATA`

- 作用：允许事件携带 payload
- 设计意图：兼容“纯信号事件”和“带消息体事件”两种场景
- 直接影响：
  - `eos_heap_t`
  - `eos_heap_malloc()`
  - `eos_heap_gc()`
  - `eos_heap_free()`
  - `eos_event_pub()`
  - `eos_once()` 中事件恢复为 `eos_event_t`
- 当前配置：`1`

#### `EOS_SIZE_HEAP`

- 作用：事件内部 heap 大小上限
- 设计意图：heap 元数据中许多偏移/大小字段是 15 位，因此最大受 `0x7fff` 限制
- 直接影响：
  - `eos_heap_t.data[EOS_SIZE_HEAP]`
  - heap 初始化和分配/回收逻辑
- 当前配置：`32767`

#### `EOS_USE_EVENT_BRIDGE`

- 作用：预留多实例事件桥接能力
- 当前观察：在这版代码里没有看到实际结构、API 或实现分支使用它
- 当前配置：`0`

### 3.4 编译期配置检查的意义

`eventos_config.h` 末尾的 `#error` 检查，本质上是在保护三类边界：

1. 类型系统边界  
   `EOS_MCU_TYPE`、`EOS_TEST_PLATFORM` 必须落在已实现分支内

2. 容量边界  
   `EOS_MAX_ACTORS`、`EOS_MAX_TIME_EVENT` 不能超过位图和计数器承载能力

3. 算法边界  
   `EOS_MAX_HSM_NEST_DEPTH`、`EOS_SIZE_HEAP` 必须在当前路径算法和 heap 编码限制内

## 4. `eventos.c` 文件级骨架

### 4.1 断言底座

`EOS_ASSERT` 是整份 `eventos.c` 的第一层护栏。

- 打开时：
  - 停止框架
  - 进入临界区
  - 报出行号
- 关闭时：
  - 为空操作

它贯穿 Actor 注册、主循环、状态机迁移、时间回绕、heap 访问等关键路径。

### 4.2 运行语义与返回码

文件前部定义了：

- `enum eos_actor_mode`
  - `EOS_Mode_Reactor`
  - `EOS_Mode_StateMachine`

- 内部返回码枚举
  - 正常但没事做：`EosRun_NoEvent`、`EosRun_NoActor`
  - 真正错误：`EosRunErr_MallocFail`、`EosRunErr_SubTableNull` 等

这说明 EventOS 的主循环不是“非 0 都算失败”，而是明确区分：

- 运行正常但当前空闲
- 内部出现错误

### 4.3 时间事件内部表示

时间事件相关核心对象：

- `timer_threshold[]`
  - 根据延时长度自动选择合适时间粒度
- `timer_unit[]`
  - 记录每个粒度对应的实际毫秒数
- `eos_event_timer_t`
  - `topic`
  - `oneshoot`
  - `unit`
  - `period`
  - `timeout_ms`

设计思想：

- 定时器最终仍发布普通事件
- 长延时不用始终以毫秒粒度维护，减轻运行期开销
- 用绝对时间 `timeout_ms` 管理，而不是剩余时间

### 4.4 heap 与事件内部格式

核心结构：

- `eos_block_t`
  - heap 块头
  - 同时承担 free list 与事件队列关系维护
- `eos_event_inner_t`
  - 内部事件头
  - 包含 `sub` 和 `topic`
- `eos_heap_t`
  - 管理整个事件 heap
- `eos_t`
  - 框架运行时总控单例

尤其要记住：

- 事件内部是“块头 + `eos_event_inner_t` + payload”
- `eos_event_t` 只是对外暴露的逻辑视图
- 真正入队的是带订阅快照的内部事件对象

### 4.5 单例模型

`static eos_t eos;`

说明这版 EventOS Nano 采用单实例架构。  
整个系统只有一个运行时框架对象。

## 5. 框架主流程

### 5.1 `eos_init()`

职责：

- 清运行态
- 设置 `enabled/running/init_end`
- 清空 Actor 注册状态
- 置空订阅表指针
- 初始化 heap
- 初始化软件时间

它的语义不是“开始运行”，而是“让框架进入可启动状态”。

### 5.2 `eos_sub_init()`

职责：

- 接收用户提供的订阅表地址
- 将所有 topic 对应的订阅位图清零

设计意图：

- 订阅表由用户决定放哪里
- 框架只负责使用，不负责替用户分配

### 5.3 `eos_evttimer()`

职责：

- 扫描定时器数组
- 将到期 timer 转成普通 topic 事件
- 一次性 timer 删除
- 周期 timer 更新时间点
- 重算 `timeout_min`

核心思想：

- 时间事件不是独立分发模型
- 到期后直接复用普通事件发布路径

### 5.4 `eos_once()`

这是整个框架的一步调度核心。

执行顺序：

1. 检查是否已 `eos_init()`
2. 检查订阅表是否就绪
3. 检查框架是否被停止
4. 检查是否存在已注册且已启动的 Actor
5. 处理到期定时器
6. 如果 heap 为空，则返回 `NoEvent`
7. 根据 `sub_general` 找当前最高优先级且有活的 Actor
8. 从 heap 中取出“属于该 Actor 的最老事件”
9. 恢复为对外 `eos_event_t`
10. 分发给 Reactor 或状态机
11. 尝试 GC

注意：

- 每次只处理一个事件
- 优先级决策不是按队列头，而是先按 Actor 优先级找“当前谁有活”

### 5.5 `eos_run()`

职责：

- 调 `eos_hook_start()`
- 断言关键前提成立
- 设置 `running = True`
- 循环调用 `eos_once()`
- 空闲时调用 `eos_hook_idle()`
- 停止后不返回，而是永久停在 idle

这说明 EventOS 是一个标准嵌入式事件循环框架，而不是函数调用式任务库。

### 5.6 `eos_stop()`

职责：

- 设置 `enabled = False`
- 调用 `eos_hook_stop()`

它是“请求式停止”，不是强制打断当前事件处理。

## 6. 软件时间与时间回绕

### 6.1 `eos_time()`

- 只读返回当前软件时间 `eos.time`

### 6.2 `eos_tick()`

职责：

- 每次调用推进软件时间 `EOS_TICK_MS`
- 检测 30 天逻辑时间窗口内的回绕
- 回绕时同步平移所有 timer 的绝对超时点

设计意图：

- EventOS 的时间事件是按“绝对超时刻”管理的
- 使用绝对时间更简单，但必须解决回绕
- 回绕后整体平移 timer，可保持相对超时关系不变

## 7. Actor 与状态机

### 7.1 Actor 初始化与启动

#### `eos_actor_init()`

职责：

- 检查框架是否已初始化且尚未运行
- 检查优先级合法性与唯一性
- 注册 Actor 到 `eos.actor[]`
- 设置优先级

#### `eos_reactor_init()` / `eos_reactor_start()`

- `init` 负责注册并标记模式为 Reactor
- `start` 保存事件回调并把 Actor 标记为 enabled

#### `eos_sm_init()` / `eos_sm_start()`

- `init` 负责注册并标记模式为 StateMachine
- `start` 会触发初始状态迁移
- HSM 模式下还会处理层次化初始化和默认子状态进入

### 7.2 状态迁移基础 API

#### `eos_tran()`

- 把 `me->state` 改成目标状态
- 返回 `EOS_Ret_Tran`

含义：状态函数只声明“我要迁移到谁”，真正的 Exit/Enter 由分发器完成。

#### `eos_super()`

- 把 `me->state` 暂时改成父状态
- 返回 `EOS_Ret_Super`

含义：当前层不处理，请父状态接手。

#### `eos_state_top()`

- 顶层伪状态
- 不处理业务事件
- 返回 `EOS_Ret_Null`

作用：为 HSM 提供统一根状态。

### 7.3 `eos_sm_dispath()`

#### FSM 模式

流程：

1. 调当前状态处理函数
2. 如果返回 `EOS_Ret_Tran`
3. 先发 `Event_Exit`
4. 再发 `Event_Enter`
5. 提交新状态

#### HSM 模式

流程：

1. 从当前叶子状态开始处理外部事件
2. 若返回 `EOS_Ret_Super`，持续向父状态冒泡
3. 如果最终没有迁移，恢复稳定叶子状态
4. 如果发生迁移，先退出到真正迁移源状态层
5. 调 `eos_sm_tran()` 计算进入路径
6. 按路径发 `Event_Enter`
7. 再通过 `Event_Init` 自动下钻到最终叶子状态

这套设计把“状态函数”和“状态机生命周期动作”彻底解耦。

### 7.4 `eos_sm_tran()`

职责：

- 计算 HSM 转移路径
- 找最低公共祖先 LCA
- 告诉外层从哪里开始补 `Event_Enter`

它先处理若干简单场景：

- 自迁移
- 迁移到直接子状态
- 兄弟状态迁移
- 迁移到父状态

更复杂情况则沿祖先链向上搜索 LCA。

## 8. heap 与事件内存模型

### 8.1 设计核心

这套 heap 不是通用 malloc，而是“为事件分发定制的消息堆”。

它同时维护两种关系：

- 物理块关系：`next/last`
- 事件队列关系：`q_next/q_last`

这意味着：

- 内存布局和事件排队是两套正交关系
- 事件按到达顺序排队
- 内存按物理相邻关系管理和合并

### 8.2 `eos_heap_init()`

职责：

- 初始化 heap 元数据
- 把整块 heap 视为一个大的 free block

### 8.3 `eos_heap_malloc()`

职责：

- First-Fit 找第一个够大的 free block
- 按 4 字节对齐
- 把块切成“已分配块 + 剩余 free block”
- 把已分配块挂到事件队列尾部

这说明“分配事件内存”和“事件入队”在这里是一次完成的。

### 8.4 `eos_heap_get_block()`

职责：

- 从全局事件队列头开始扫描
- 找出“属于某个 Actor 且还未被它消费”的最老事件
- 清掉该 Actor 对应的 `sub` 位

核心思想：

- EventOS 不是每个 Actor 独立队列
- 而是全局一条事件队列
- 每条事件内部带“还有哪些订阅者没消费完”的位图

### 8.5 `eos_heap_gc()`

职责：

- 如果 `evt->sub != 0`，说明还有别的 Actor 未处理，不能释放
- 只有 `evt->sub == 0`，才从队列摘除并真正回收
- 最后重算 `sub_general`

这就是 EventOS 支持“一条事件共享给多个订阅者”的关键。

### 8.6 `eos_heap_free()`

职责：

- 将块标记为 free
- 尝试与前一个物理块合并
- 再尝试与后一个物理块合并

它提供了基本的碎片控制能力。

## 9. 一条事件的完整生命周期

### 9.1 普通事件

1. 用户调用 `eos_event_pub_topic()` 或 `eos_event_pub()`
2. 落到 `eos_event_pub_ret()`
3. `eos_heap_malloc()` 分配一块事件内存
4. 写入：
   - `topic`
   - `sub`
   - payload
5. 更新 `sub_general`
6. `eos_run()` 反复调 `eos_once()`
7. `eos_once()` 选择当前最高优先级且有活的 Actor
8. `eos_heap_get_block()` 找到它应处理的最老事件，并清掉该 Actor 位
9. 事件被还原为 `eos_event_t`
10. 分发给 Reactor 或状态机
11. `eos_heap_gc()` 检查：
   - 若还有订阅者未处理，保留
   - 若 `sub == 0`，从队列摘掉并 `eos_heap_free()`

### 9.2 时间事件

1. 用户调用 `eos_event_pub_delay()` 或 `eos_event_pub_period()`
2. `eos_event_pub_time()` 把 timer 填入 `etimer[]`
3. 外部周期性调用 `eos_tick()`
4. `eos_once()` 内部调用 `eos_evttimer()`
5. 到期后用 `eos_event_pub_topic()` 转成普通事件
6. 后续流程与普通事件完全一致

## 10. 设计亮点总结

### 10.1 全局事件队列 + 内部订阅位图

这是整套 EventOS 最核心的设计点。

优点：

- 同一事件无需复制给多个 Actor
- 多订阅者共享一份事件存储
- 回收时机清晰

### 10.2 时间事件与普通事件统一

时间事件不是另一套消息系统，而只是“未来时刻自动发布一条普通事件”。

优点：

- 主调度器无需理解多套事件类型
- Actor 层不区分事件来源

### 10.3 状态机生命周期统一由分发器维护

状态函数只负责：

- 处理事件
- 返回 `Handled/Super/Tran`
- 必要时通过 `eos_tran()` 或 `eos_super()` 写入目标状态

真正的 `Exit/Enter/Init/LCA` 由分发器完成。

优点：

- 状态函数简单
- 生命周期动作一致
- HSM 算法集中管理

## 11. 当前代码里值得留意的点

以下几项不一定都是确定 bug，但都值得后续审查。

### 11.1 `EOS_MAX_ACTOR` 疑似拼写错误

`eventos_config.h` 中编译期检查写的是：

- `EOS_MAX_ACTOR > EOS_MCU_TYPE`

但实际配置宏名是 `EOS_MAX_ACTORS`。  
这会导致“Actor 数量上限检查”没有完整生效。

### 11.2 `EOS_USE_HEAP` 在当前代码里未找到定义

相关位置：

- `eventos_config.h` 末尾 heap 检查
- `eventos.c` 中 `eos_run()` 的 heap 断言条件

预处理器下未定义宏会按 `0` 处理，因此相关检查实际上可能被静默绕过。

### 11.3 `eos_evttimer()` 的空表判断风格值得复核

当前代码中存在：

- 用 `eos.etimer[0].topic == Event_Null` 判断空定时器表

从表达清晰度和稳健性上看，直接使用 `timer_count == 0` 更直观。  
当前写法更依赖初始化约定。

### 11.4 `eos_sm_dispath()` 中一处循环值得重点复核

在 HSM 分支处理 `Event_Init` 的进入路径时，有一段代码表面上看像少了 `ip--`。  
这部分需要结合实际源码和编译结果进一步核对，建议后续做一次专门代码审查。

### 11.5 `eos_heap_malloc()` 的 `remaining` 在对齐前计算

当前实现先估算剩余空间，再做 4 字节对齐。  
逻辑未必一定错，但容易让“是否能切块”和“实际切块尺寸”之间出现理解偏差，值得进一步确认边界场景。

## 12. 最适合记住的三条主线

如果以后只想快速回忆 EventOS 的核心，记住下面三条就够了。

### 12.1 时间线

`eos_tick()`  
-> 更新时间  
-> `eos_evttimer()`  
-> 到期 timer 转成普通事件

### 12.2 调度线

`eos_run()`  
-> `eos_once()`  
-> 按优先级找有活的 Actor  
-> 从全局事件队列中取它该处理的最老事件  
-> Reactor / 状态机处理

### 12.3 内存线

`eos_event_pub_ret()`  
-> `eos_heap_malloc()`  
-> 入全局事件队列  
-> `eos_heap_get_block()`  
-> 某 Actor 消费  
-> `eos_heap_gc()`  
-> `eos_heap_free()`

---

如果后续继续深入，最值得做的两件事是：

1. 结合一个真实例子手工走一遍 HSM 状态迁移  
2. 针对 heap 和 `eos_sm_dispath()` 做一次代码审查式验证

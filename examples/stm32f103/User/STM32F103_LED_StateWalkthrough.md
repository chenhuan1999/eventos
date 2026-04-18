# STM32F103 LED 示例状态迁移手工推演

## 1. 示例里有哪些参与者

这个例子里有两个 Actor：

1. `sm_led`
   - 类型：`eos_sm_t`
   - 文件：`eos_led_sm.c`
   - 优先级：`1`
   - 订阅事件：`Event_Time_500ms`
   - 作用：在 `state_off` 和 `state_on` 之间来回迁移

2. `actor_led`
   - 类型：`eos_reactor_t`
   - 文件：`eos_led_reactor.c`
   - 优先级：`2`
   - 订阅事件：`Event_Time_1000ms`
   - 作用：每秒翻转一次 `status`

注意：

- 数值越小，优先级越高。
- 所以 `sm_led` 会先于 `actor_led` 被调度。

## 2. 系统启动顺序

`main.c` 中的启动顺序如下：

1. 配置 `SysTick` 为 1ms 中断一次
2. 调 `eos_init()`
3. 调 `eos_sub_init(eos_sub_table, Event_Max)`
4. 调 `eos_sm_led_init()`
5. 调 `eos_reactor_led_init()`
6. 调 `eos_run()`

这意味着：

- 框架内核先起来
- 订阅表先准备好
- 状态机 Actor 和 Reactor Actor 再依次注册并启动
- 最后进入主循环

## 3. `sm_led` 的初始迁移是怎么发生的

### 3.1 初始化阶段

`eos_sm_led_init()` 里做了两件关键事：

1. `eos_sm_init(&sm_led.super, 1, EOS_NULL);`
2. `eos_sm_start(&sm_led.super, EOS_STATE_CAST(state_init));`

其中真正触发状态迁移的是 `eos_sm_start()`。

### 3.2 `eos_sm_start()` 内部会发生什么

按照 EventOS 的实现，`eos_sm_start()` 会：

1. 把当前状态临时设为 `state_init`
2. 用 `Event_Null` 调一次 `state_init`
3. 要求 `state_init` 返回 `EOS_Ret_Tran`
4. 把目标状态设为真正的初始稳定状态
5. 再补做 `Event_Enter`

### 3.3 本例里的实际结果

`state_init()` 做了三件事：

1. 订阅 `Event_Time_500ms`
2. 发布一个 500ms 周期事件
3. 返回 `EOS_TRAN(state_off)`

所以初始迁移链就是：

`state_init --(Event_Null)--> state_off`

随后框架会补发：

- `state_off(Event_Enter)`

于是：

- `state_off` 中把 `me->status = 0`
- 状态机最终稳定在 `state_off`

## 4. 500ms 周期运行时，状态是怎么迁移的

### 4.1 定时事件是怎么来的

`state_init()` 中调用了：

`eos_event_pub_period(Event_Time_500ms, 500);`

这并不是立刻把事件投给状态机，而是先注册了一个周期 timer。

之后系统会走这条链：

1. `SysTick` 中断周期性触发
2. 移植层里应调用 `eos_tick()`
3. `eos.time` 按 1ms 递增
4. `eos_once()` 内部调用 `eos_evttimer()`
5. 到了 500ms 时，`eos_evttimer()` 把 `Event_Time_500ms` 发布成普通事件
6. 这个事件进入 EventOS 全局事件队列

### 4.2 状态机第一次收到 500ms 事件

当前稳定状态是：

- `state_off`

当 `Event_Time_500ms` 到来时：

1. `eos_once()` 先选中优先级更高的 `sm_led`
2. `eos_sm_dispath()` 把事件交给当前状态 `state_off`
3. `state_off(Event_Time_500ms)` 返回 `EOS_TRAN(state_on)`

于是框架自动补做生命周期动作：

1. `state_off(Event_Exit)`
2. `state_on(Event_Enter)`

进入 `state_on` 后：

- `me->status = 1`

因此第一次 500ms 事件后的状态变化是：

`state_off --(Event_Time_500ms)--> state_on`

## 5. 第二次 500ms 事件

此时稳定状态已经是：

- `state_on`

再收到一次 `Event_Time_500ms` 时：

1. `state_on(Event_Time_500ms)` 返回 `EOS_TRAN(state_off)`
2. 框架补做：
   - `state_on(Event_Exit)`
   - `state_off(Event_Enter)`
3. `state_off(Event_Enter)` 中把 `status = 0`

因此第二次 500ms 事件后的状态变化是：

`state_on --(Event_Time_500ms)--> state_off`

## 6. 后续周期行为

之后每来一次 `Event_Time_500ms`，状态机会在两个状态之间交替切换：

1. `state_off -> state_on`
2. `state_on -> state_off`
3. `state_off -> state_on`
4. `state_on -> state_off`

所以从行为上看，这就是一个“500ms 翻转一次”的 LED 状态机。

## 7. 为什么这个例子虽然开启了 HSM，却看起来像普通 FSM

原因是：

1. 配置上开启了 `EOS_USE_HSM_MODE`
2. 但这个具体示例没有定义父状态/子状态层级
3. `state_on` 和 `state_off` 都把未处理事件直接 `EOS_SUPER(eos_state_top)`

所以从例子本身看，它实际上是：

- 运行在 HSM 框架上的一个扁平 FSM

也就是说：

- 框架支持层次状态机
- 但这个例子只用了最简单的平面迁移能力

## 8. `actor_led` 这个 Reactor 在做什么

Reactor 这部分没有“状态迁移”，只有“收到事件就直接处理”。

它在初始化时：

1. 订阅 `Event_Time_1000ms`
2. 发布一个 1000ms 周期事件

之后每收到一次 `Event_Time_1000ms`：

- `led_e_handler()` 直接把 `status` 翻转一次

所以它和状态机 Actor 的区别非常明显：

- 状态机：通过 `EOS_TRAN()` 迁移状态
- Reactor：没有状态迁移，只执行回调

## 9. 调度时谁先处理

当前两个 Actor 的优先级是：

- `sm_led = 1`
- `actor_led = 2`

所以：

- 当 500ms 和 1000ms 事件同时都在队列里时
- `sm_led` 会先处理
- `actor_led` 后处理

这是因为 EventOS 在 `eos_once()` 里是按优先级从高到低扫描 Actor 的。

## 10. 最适合记住的一条完整链路

如果以后只记一条链，记这条就够了：

1. `main()` 调 `eos_sm_led_init()`
2. `eos_sm_start()` 调 `state_init(Event_Null)`
3. `state_init()` 返回 `EOS_TRAN(state_off)`
4. 框架补做 `state_off(Event_Enter)`
5. 状态机稳定在 `state_off`
6. 每隔 500ms，timer 发布 `Event_Time_500ms`
7. `state_off(Event_Time_500ms)` 迁移到 `state_on`
8. 框架补做 `Exit/Enter`
9. 再过 500ms，`state_on(Event_Time_500ms)` 迁移回 `state_off`

最终效果就是：

- `status` 每 500ms 在 0 和 1 之间切换一次

## 11. 这个例子最值得学习的点

1. 初始状态不是直接赋值进去的，而是通过 `state_init -> EOS_TRAN(state_off)` 进入
2. 状态机中的业务输出放在 `Event_Enter` 里，而不是放在迁移语句旁边
3. 定时事件最终会被转换成普通事件，再交给状态机处理
4. Reactor 和状态机可以同时存在于同一个 EventOS 系统中

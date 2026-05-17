# MCU 固件上下文文档

当需要创建或更新项目文档，让后续 Codex 会话能够持续、一致地修改 MCU 固件时，使用这份参考。

## 推荐的 docs 目录

```text
docs/
  requirements.md
  project_context.md
  architecture.md
  hardware_map.md
  module_contracts.md
  modules/
    power_service.md
    communication_service.md
  task_model.md
  interrupt_dma_model.md
  state_machines.md
  coding_rules.md
  test_plan.md
  traceability_matrix.md
  change_log.md
  decisions/
    ADR-0001-layering.md
    ADR-0002-event-model.md
```

## 每个文档的作用

`docs/requirements.md`

- 从 PRD 提取出的固件需求。
- 每条需求有稳定 ID，例如 `REQ-001`。
- 包含功能需求、非功能需求、硬件输入输出、通信协议、状态机、异常场景、实时性、功耗、存储、安全和验收标准。
- 当 PRD 有歧义时，记录假设和待确认问题，不要直接把不明确需求写死到代码里。

`docs/project_context.md`

- MCU 型号、开发板、SDK/HAL 版本、工具链、RTOS 或裸机模型。
- 目录结构和关键入口文件。
- 产品功能概述。
- 编译、烧录和调试命令。
- 不应随意修改的文件或区域。

`docs/architecture.md`

- 分层规则：`Hardware/SDK -> HAL/BSP -> Driver -> Service -> Application`。
- 各层允许和禁止的依赖关系。
- 初始化顺序和组合根。
- 依赖注入、回调、事件、队列使用规则。
- 全局状态管理规则。

`docs/hardware_map.md`

- GPIO、UART、SPI、I2C、CAN、ADC、PWM、TIM、DMA、EXTI、NVIC 使用情况。
- 引脚归属和外设归属。
- 中断优先级。
- 已保留或共享的硬件资源。

`docs/module_contracts.md`

- 每个模块的职责和公开 API。
- 输入、输出、错误码和初始化要求。
- 是否可重入，是否线程安全。
- 是否允许在 ISR 中调用。
- 阻塞行为和超时规则。
- 对下层模块的依赖。

`docs/modules/<module>.md`

- 只给复杂模块或可独立验收的能力写详细模块文档。
- 不要为每个 `.c` 文件机械生成对应 `.md`。
- 推荐内容：模块职责、涉及文件、公开 API、依赖关系、状态机、错误码、ISR 可调用性、阻塞和超时行为、测试方法。
- 判断规则：如果别的模块会依赖它，就写模块文档；如果只是内部实现细节，不单独写。

`docs/task_model.md`

- RTOS 任务、优先级、栈大小、周期和阻塞点。
- 队列、信号量、互斥锁、事件组、软件定时器。
- 生产者-消费者关系和数据所有权。

`docs/interrupt_dma_model.md`

- ISR 和回调函数的职责。
- DMA 完成和错误处理流程。
- ISR 上下文允许调用哪些函数。
- 如何把处理延后到任务、队列、标志位或事件循环。

`docs/state_machines.md`

- 系统、通信、设备、OTA、配网、采集或控制状态机。
- 推荐使用下面的表格形式：

```text
当前状态 | 事件 | 动作 | 下一状态
```

`docs/coding_rules.md`

- 命名风格、文件组织、头文件规则、错误码规则、日志规则。
- 是否允许动态内存。
- 阻塞和超时策略。
- ISR、RTOS 和驱动层编码规则。
- 测试桩和模拟依赖规则。

`docs/test_plan.md`

- 编译、格式化、静态分析、单元测试和仿真命令。
- 硬件验证步骤。
- 需要检查的串口日志、示波器波形、逻辑分析仪结果或故障注入用例。

`docs/traceability_matrix.md`

- 需求到设计、代码、测试和验收的追踪表。
- 推荐格式：

```text
需求ID | PRD条目 | 固件模块 | 接口/API | 测试用例 | 状态
REQ-001 | 开机自检 | app_selftest.c | selftest_start() | TC-001 | 已实现
REQ-002 | 低电量报警 | service_power.c | power_get_state() | TC-002 | 待实现
```

- 状态可以是：`待分析`、`待实现`、`实现中`、`已实现`、`已测试`、`已硬件验证`、`阻塞`。

`docs/change_log.md`

- 每次任务后追加一条记录：

```text
## YYYY-MM-DD - 简短任务标题

修改文件：
- path/to/file.c

接口变化：
- 新增 xxx_init()

行为变化：
- 状态 A 收到事件 B 后进入状态 C。

验证：
- 已通过 ... 编译
- 未硬件验证：...

后续注意：
- 保持 xxx_isr_callback() 不包含服务层业务逻辑。
```

`docs/decisions/ADR-xxxx.md`

- 用来记录长期有效的架构决策。
- 包含背景、决策、影响和被放弃的替代方案。

## 任务和上下文映射

```text
从 PRD 建立固件需求：
  PRD, requirements.md, project_context.md, architecture.md

实现 PRD 功能：
  PRD, requirements.md, traceability_matrix.md, architecture.md, module_contracts.md, state_machines.md, test_plan.md

新增器件驱动：
  project_context.md, architecture.md, hardware_map.md, module_contracts.md

修改应用行为：
  project_context.md, architecture.md, state_machines.md, module_contracts.md

调整 RTOS 调度：
  task_model.md, interrupt_dma_model.md, architecture.md

修复 ISR 或 DMA 问题：
  interrupt_dma_model.md, hardware_map.md, module_contracts.md

重构模块：
  architecture.md, module_contracts.md, modules/<module>.md, change_log.md, decisions/*.md

测试或验证：
  test_plan.md, module_contracts.md, coding_rules.md

定位 bug：
  project_context.md, change_log.md, 相关模块契约, 相关状态机
```

## PRD 到闭环流程

当用户已经有产品 PRD 时，先把 PRD 变成固件侧可执行、可验证、可追踪的文档，再实现代码：

```text
PRD
  -> requirements.md
  -> architecture.md / module_contracts.md / state_machines.md
  -> 代码实现
  -> test_plan.md / 测试代码 / 硬件验证记录
  -> traceability_matrix.md
  -> change_log.md
```

闭环判断标准：

- PRD 有原始描述。
- `requirements.md` 有需求 ID 和验收标准。
- 设计文档有模块边界、接口和状态变化。
- 代码中有实现。
- `test_plan.md` 有测试用例或硬件验收步骤。
- `traceability_matrix.md` 能追踪需求、模块、接口、测试和状态。
- `change_log.md` 记录变更、验证和未验证项。

## 模块文档粒度规则

不推荐每个 `.c` 文件都生成一个 `.md`。文档粒度按模块和能力划分。

示例：

```text
driver/
  sensor_a.c
  sensor_a.h
  sensor_a_reg.c
  sensor_a_bus.c

docs/modules/
  sensor_a.md
```

`sensor_a.md` 覆盖整个传感器驱动模块，不需要生成 `sensor_a.c.md`、`sensor_a_reg.c.md`、`sensor_a_bus.c.md`。

需要单独模块文档的情况：

- 有公开接口。
- 被其他模块依赖。
- 使用硬件资源。
- 有状态机。
- 有复杂时序或阻塞/超时行为。
- 有独立测试或验收要求。

不需要单独模块文档的情况：

- 纯内部工具函数。
- 小的私有 `.c` 文件。
- 只服务于一个模块内部的辅助实现。
- 代码注释足够说明的局部细节。

## 可复用提示词

```text
你是这个 MCU 固件项目的持续协作工程师。

修改前请先读取：
- 产品 PRD，或 docs/requirements.md
- docs/project_context.md
- docs/architecture.md
- docs/module_contracts.md
- docs/traceability_matrix.md
- docs/change_log.md

本次任务：<描述任务>

规则：
1. 先确认该功能对应的需求 ID；没有需求 ID 时先更新 requirements.md。
2. 保持现有分层边界。
3. 不要让 Application 直接访问寄存器、MCU SDK 句柄或外设。
4. 不要在 ISR 或 DMA 回调中加入业务逻辑。
5. 公开 API 变化时，更新 module_contracts.md。
6. 复杂模块变化时，更新 docs/modules/<module>.md；不要为每个 .c 文件机械生成文档。
7. 硬件资源、任务模型、中断 DMA 流程或状态机变化时，更新对应文档。
8. 更新 traceability_matrix.md，保持需求、模块、接口和测试状态可追踪。
9. 代码修改后追加 change_log.md。
10. 明确说明已验证内容，以及仍需硬件验证的内容。
```

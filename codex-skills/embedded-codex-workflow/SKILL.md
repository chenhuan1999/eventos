---
name: embedded-codex-workflow
description: 指导 Codex 在 MCU 单片机嵌入式固件项目中稳定地获取上下文、从 PRD 提取固件需求、规划任务、修改 C/C++ 代码、维护分层架构、更新项目文档并记录验证结果。适用于用户编写、修改、审查或重构单片机固件，尤其涉及产品 PRD、需求追踪闭环、HAL/BSP、驱动、服务、应用逻辑、RTOS 任务、中断、DMA、状态机、硬件资源表、模块接口契约、模块文档、以及为了后续 Codex 任务保持一致性的项目文档。
---

# 嵌入式 Codex 工作流

## 核心原则

把 Codex 当成持续协作的固件工程师，而不是一次性的代码生成器。每次任务都要先建立必要上下文，再做小范围修改，最后同步更新项目文档，让后续任务可以沿着同一套事实和架构继续推进。

如果项目已有产品 PRD，要把 PRD 当成上游事实源。不要直接从 PRD 跳到代码实现，先把 PRD 转换成可追踪的固件需求、模块设计、测试用例和验收标准。

## 工作流程

1. 修改代码前，先检查最小必要上下文：
   - 入口：`main.c`、`main.cpp`、启动流程、系统初始化。
   - 调度：RTOS 任务创建、软件定时器、事件循环、裸机主循环。
   - 硬件路径：BSP、HAL、引脚表、外设初始化、DMA、中断回调。
   - 业务路径：驱动、服务、应用模块、状态机。
   - 构建路径：构建脚本、编译参数、SDK 配置、板级配置。

2. 识别当前任务类型：
   - 从 PRD 提取固件需求。
   - 建立需求到代码和测试的追踪闭环。
   - 新增外设或器件驱动。
   - 修改应用层行为。
   - 调整 RTOS 任务或调度。
   - 修复中断、DMA 或时序问题。
   - 重构模块或清理依赖。
   - 增加测试、仿真桩、日志或项目文档。

3. 如果项目里已有文档，优先读取这些文件：
   - 产品 PRD 或 `docs/requirements.md`
   - `docs/project_context.md`
   - `docs/architecture.md`
   - `docs/module_contracts.md`
   - `docs/traceability_matrix.md`
   - `docs/change_log.md`

   需要按任务类型选择更多上下文时，读取 `references/context-documents.md` 中的文档映射表。

4. 保持固件分层边界：

   ```text
   Hardware / MCU SDK
       -> HAL / BSP
       -> Driver
       -> Service
       -> Application
   ```

   不要把硬件细节上移到服务层或应用层。不要让驱动层依赖应用层策略。

5. 做小而清晰的修改：
   - 优先沿用当前代码库已有风格和模式。
   - 如果任务来自 PRD，先确认需求 ID；没有需求 ID 时先更新 `docs/requirements.md`。
   - 只有在模块契约清晰时，才新增或修改公开 API。
   - 中断里只做最少工作：清标志、取数据、置标志、释放信号量、投递事件。
   - 产品策略放在应用层，可复用能力放在服务层，器件协议放在驱动层，MCU 相关操作放在 HAL/BSP。

6. 在当前环境里尽量验证：
   - 运行最接近的编译、单元测试、静态检查或格式化命令。
   - 如果必须依赖硬件验证但当前无法执行，要明确说明哪些项目尚未验证。
   - 涉及时序、中断、DMA 或 RTOS 的修改，要说明建议的硬件验证步骤。

7. 代码修改后同步更新文档：
   - PRD 需求变化或新增需求：更新 `docs/requirements.md`。
   - 需求、模块、接口、测试状态变化：更新 `docs/traceability_matrix.md`。
   - 公开 API 变化：更新 `docs/module_contracts.md`。
   - 复杂模块的职责、依赖、状态机或验收方式变化：更新 `docs/modules/<module>.md`。
   - 引脚、外设、DMA、定时器、中断变化：更新 `docs/hardware_map.md`。
   - RTOS 任务、队列、信号量、优先级、栈大小变化：更新 `docs/task_model.md`。
   - ISR 或 DMA 流程变化：更新 `docs/interrupt_dma_model.md`。
   - 行为或状态迁移变化：更新 `docs/state_machines.md`。
   - 每次任务完成后：追加 `docs/change_log.md`。

## 架构检查点

遇到下面情况时，要在实现前或实现过程中标记风险：

- 应用层代码直接访问寄存器、SDK 句柄、GPIO、UART、SPI、I2C、ADC、PWM、DMA 或 NVIC。
- 驱动层调用应用层函数，或写入产品业务策略。
- 服务层在模块内部偷偷创建底层依赖，而不是通过初始化参数或配置注入。
- ISR 中执行解析、业务决策、阻塞等待、动态内存分配、大量日志或长循环。
- 公共头文件越来越大，变成包含大量无关能力的 god header。
- 全局变量被当作隐藏的跨模块契约。
- 高优先级任务或实时路径中出现无边界阻塞调用。
- 需求没有 ID，代码实现无法追溯到 PRD 条目。
- 每个 `.c` 文件都机械生成一个 `.md`，导致文档爆炸和维护不一致。

## PRD 到固件闭环

当用户提供 PRD 或要求“按产品需求完善固件功能”时，按下面链路推进：

```text
PRD
  -> docs/requirements.md
  -> docs/architecture.md / docs/module_contracts.md / docs/state_machines.md
  -> 代码实现
  -> docs/test_plan.md / 测试代码 / 硬件验证记录
  -> docs/traceability_matrix.md
  -> docs/change_log.md
```

一个功能只有同时满足下面条件，才算闭环：

- PRD 或 `docs/requirements.md` 中有明确需求描述和需求 ID。
- 设计文档中说明模块边界、接口、状态机或硬件资源影响。
- 代码中有实现，且分层边界没有被破坏。
- `docs/test_plan.md` 中有测试或硬件验收方法。
- `docs/traceability_matrix.md` 能追踪需求、模块、接口、测试和状态。
- `docs/change_log.md` 记录了本次变更和未验证项。
- 硬件验证结果明确；如果未验证，要写清楚原因和后续验证步骤。

## 模块文档粒度

不要为每个 `.c` 文件机械生成一个对应的 `.md`。按“模块、能力、接口边界”写文档，而不是按源文件数量写文档。

推荐规则：

- 有公开接口、跨模块依赖、状态机、硬件资源、复杂时序或测试验收要求的模块，写 `docs/modules/<module>.md`。
- 纯内部工具函数、小的私有 `.c`、只被一个模块内部使用的辅助文件，不单独写模块文档。
- 如果别的模块会依赖它，就写模块文档；如果只是内部实现细节，就用代码注释和模块主文档覆盖。

文档层级建议：

```text
docs/module_contracts.md     # 所有模块接口总表
docs/modules/<module>.md     # 复杂模块详细说明
源代码注释                   # 文件内部实现细节
```

## 项目文档初始化

如果项目还没有用于 Codex 的 `docs/` 上下文文档，在大改代码前先生成最小文档集：

```text
docs/project_context.md
docs/requirements.md
docs/architecture.md
docs/hardware_map.md
docs/module_contracts.md
docs/traceability_matrix.md
docs/change_log.md
```

这些文档的推荐内容和任务提示词见 `references/context-documents.md`。

## 回复方式

执行实现类任务时，按下面顺序向用户说明：

1. 已读取的上下文和当前理解。
2. 准备修改的范围。
3. 已完成的代码和文档变化。
4. 已运行的验证，以及仍需硬件验证的项目。
5. 修改过的文件和后续风险。

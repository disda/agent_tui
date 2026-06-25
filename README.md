# Agent TUI

这是「L2命题任务-从零实现一个 TUI 终端编码 Agent（发布版）」的项目工作区。

当前实现方向：**纯 C++ TUI Coding Agent**，并采用 **kwoa-cli 风格 Skills Runtime** 组织 Agent 能力。

## 文档入口

- [TODO：下一步计划](./TODO.md)
- [C++ + Skills 实施计划](./docs/00-implementation-plan.md)
- [Skills 标准设计](./docs/01-skill-standard.md)
- [Agent Loop 与运行时设计](./docs/02-agent-loop-and-runtime.md)
- [交付路线与验收清单](./docs/03-delivery-roadmap.md)
- [Agent Learning Hub 启发总结](./docs/04-agent-learning-hub-insights.md)
- [Agent Harness 基础知识与实现思路](./docs/05-agent-harness-basics.md)
- [构建验证记录](./docs/06-build-verification.md)
- [题目 Markdown 原文](./output/l2-agent-tui-task.md)

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## 目标

从零实现一个最小可用的本地 TUI 编码 Agent，覆盖：

- C++ TUI 交互界面
- 自研 Agent Loop
- 结构化工具调用
- kwoa-cli 风格 Skills Runtime
- 权限确认与拒绝处理
- WPS CodingPlan Provider 适配
- 会话上下文与审计日志
- 用户级 / 项目级配置管理
- 测试与可运行交付物

## 设计原则

- 核心 Agent 能力自行实现，不依赖第三方 Agent SDK 或 Agent Framework。
- Skills 只负责描述能力、触发条件和行为规范；Tools 负责实际执行。
- 只读工具可自动执行；写文件、编辑文件和 Shell 命令必须经过用户确认。
- 所有用户输入、模型回复、工具调用、工具结果、权限拒绝和错误信息都必须进入会话历史。
- 关键 AI 协作记录沉淀到 `.ai_history/logs/`。
- 可运行验证产物放到 `deliverables/`。

## 第一阶段交付形态

第一阶段先实现最小可用闭环：

```text
用户输入任务
  ↓
TUI 展示状态
  ↓
Agent Loop 请求模型
  ↓
模型返回文本或 tool_call
  ↓
Tool Registry 执行工具
  ↓
Permission Gate 拦截危险操作
  ↓
工具结果回传模型
  ↓
模型继续推理直到完成
```

第一批内置工具：

- `list_dir`
- `read_file`
- `glob_files`
- `search_text`
- `write_file`
- `edit_file`
- `run_shell`

第一批内置 Skills：

- `repo_reader`
- `code_editor`
- `shell_runner`
- `cpp_project`
- `tui_agent`

# Agent TUI

这是「L2命题任务-从零实现一个 TUI 终端编码 Agent（发布版）」的项目工作区。

当前实现方向：**纯 C++ TUI Coding Agent**。主线是模型驱动的 Agent Loop：接入 API 后，Agent 应能通过 tool_calls 读取仓库、写代码、运行命令，并完成一个简单代码 demo。

## 文档入口

- [TODO：下一步计划](./TODO.md)
- [C++ Coding Agent 实施计划](./docs/00-implementation-plan.md)
- [Skills 标准设计](./docs/01-skill-standard.md)
- [Agent Loop 与运行时设计](./docs/02-agent-loop-and-runtime.md)
- [交付路线与验收清单](./docs/03-delivery-roadmap.md)
- [Agent Learning Hub 启发总结](./docs/04-agent-learning-hub-insights.md)
- [Agent Harness 基础知识与实现思路](./docs/05-agent-harness-basics.md)
- [构建验证记录](./docs/06-build-verification.md)
- [Hello-Agents 启发总结](./docs/07-hello-agents-lessons.md)
- [FileTools + WorkspaceGuard 验证记录](./docs/08-file-tools-workspace-guard.md)
- [PermissionGate 技术设计](./docs/09-permission-gate-design.md)
- [PermissionGate 验证记录](./docs/10-permission-gate-verification.md)
- [Controlled Shell Tool 技术设计](./docs/11-controlled-shell-tool-design.md)
- [Controlled Shell Tool 验证记录](./docs/12-controlled-shell-tool-verification.md)
- [Write / Edit Tools 技术设计](./docs/13-write-edit-tools-design.md)
- [Write / Edit Tools 验证记录](./docs/14-write-edit-tools-verification.md)
- [SessionHistory / AuditLog 技术设计](./docs/15-session-history-audit-log-design.md)
- [SessionHistory / AuditLog 验证说明](./docs/16-session-history-audit-log-verification.md)
- [Minimal TUI + Interrupt + API Config 技术设计](./docs/17-minimal-tui-config-interrupt-design.md)
- [Minimal TUI + Interrupt + API Config 验证说明](./docs/18-minimal-tui-config-interrupt-verification.md)
- [Config + Provider + Terminal Chat 技术设计](./docs/19-config-provider-terminal-chat-design.md)
- [Config + Provider + Terminal Chat 验证说明](./docs/20-config-provider-terminal-chat-verification.md)
- [OpenAI-compatible Provider 技术设计](./docs/21-openai-compatible-provider-design.md)
- [OpenAI-compatible Provider 验证说明](./docs/22-openai-compatible-provider-verification.md)
- [Local Intent Router 技术设计](./docs/23-local-intent-router-design.md)
- [Local Intent Router 验证说明](./docs/24-local-intent-router-verification.md)
- [File Manager Tools 技术设计](./docs/25-file-manager-tools-design.md)
- [File Manager Tools 验证说明](./docs/26-file-manager-tools-verification.md)
- [Pi / Codex CLI 风格工具协议演进路线](./docs/27-pi-codex-tooling-roadmap.md)
- [Deliverables 验证产物](./deliverables/README.md)
- [题目 Markdown 原文](./output/l2-agent-tui-task.md)

## Build

```bash
./scripts/build.sh
```

On Windows PowerShell:

```powershell
./scripts/build.ps1
```

Useful options:

```bash
./scripts/build.sh --release --clean
./scripts/build.sh --build-dir build-release --config Release --no-tests
```

```powershell
./scripts/build.ps1 -Release -Clean
./scripts/build.ps1 -BuildDir build-release -Config Release -NoTests
```

## Run

```bash
./build/agent_tui
```

Windows Debug 构建示例：

```powershell
.\build\Debug\agent_tui.exe
```

用户级配置目录类似 `.codex`：

```text
~/.agent_tui/config.toml
```

项目级配置：

```text
./.agent_tui/config.toml
```

配置示例：

```toml
provider = "openai-compatible"
model = "gpt-4.1"
api_base = "https://api.openai.com/v1"
api_key_env = "OPENAI_API_KEY"
timeout_seconds = 60
max_loops = 25
```

PowerShell 验证 OpenAI-compatible：

```powershell
$env:OPENAI_API_KEY="你的 key"
.\build\Debug\agent_tui.exe
```

启动后输入：

```text
/config reload
/api provider openai-compatible
/api base https://api.openai.com/v1
/api key-env OPENAI_API_KEY
/model gpt-4.1
你好，回复一句话
/exit
```

当前 `mock` provider 会返回：

```text
assistant: mock assistant: hello
```

`openai-compatible` provider 会调用 `api_base + /chat/completions`。

## Agent Loop 主路径

当前主路径：

```text
用户自然语言任务
  -> Provider 返回文本或 tool_calls
  -> AgentRunner 执行工具调用
  -> PermissionGate 拦截 write_file / edit_file / run_shell
  -> tool_result 回传 Provider
  -> Provider 继续推理直到最终回答
```

本地已提供一个不需要真实 API 的闭环验证 Provider：

```text
/api provider mock-agent-demo
实现一个简单代码 demo
```

它会通过 AgentRunner 触发 `write_file`，在用户确认后生成 `demo.py`。

自然语言任务不再走 Local Intent Router。本地能力必须通过模型可见的 tool_calls 执行；显式 slash command 只用于 `/status`、`/config`、`/exit` 这类 TUI 控制命令。

## 验证目标

当前验证目标是：**接入 API 后完成一个简单代码 demo 产出**。

重点验证：

- 模型能返回 `write_file` / `edit_file` / `run_shell` 等 tool_calls。
- TUI 能展示工具调用、工具结果、权限确认和当前状态。
- 写文件、编辑文件、Shell 命令必须经过权限确认。
- 将工具结果回传模型继续推理。
- 最终在 `deliverables/` 中沉淀一次真实代码 demo 的运行记录和关键截图。

## 目标

从零实现一个最小可用的本地 TUI 编码 Agent，覆盖：

- C++ TUI 交互界面
- 自研 Agent Loop
- 结构化工具调用
- 权限确认与拒绝处理
- WPS CodingPlan Provider 适配
- 会话上下文与审计日志
- 用户级 / 项目级配置管理
- 测试与可运行交付物

## 设计原则

- 核心 Agent 能力自行实现，不依赖第三方 Agent SDK 或 Agent Framework。
- Tools 是自然语言任务的唯一执行入口；TUI 不做本地意图规则抢答，显式 slash command 只处理界面和配置控制。
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

后续扩展：

- WPS CodingPlan Provider
- 更完整的 TUI tool log / permission panel
- Skills Runtime

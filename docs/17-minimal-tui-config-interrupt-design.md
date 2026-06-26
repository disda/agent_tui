# Minimal TUI + Interrupt + API Config 技术设计

## 1. 背景

当前 `agent_tui` 的核心 Harness 已经完成较多能力：

```text
AgentRunner
ToolRegistry
MockProvider
FileTools + WorkspaceGuard
PermissionGate
Controlled Shell Tool
Write / Edit Tools
SessionHistory / AuditLog
```

但是当前 `src/main.cpp` 仍然只是最早期 skeleton，运行 `agent_tui.exe` 只会输出：

```text
agent_tui minimal runner skeleton
```

这不满足题目中“程序必须以 TUI 形式呈现”的要求。因此本轮先接一个最小可运行 TUI 入口，同时接入中断和 API / 模型配置命令。

## 2. 设计目标

第一版目标：

- 程序启动后进入交互式终端界面，而不是只打印 skeleton。
- 展示结构化区域：Chat、Status、Config、Commands。
- 支持基础输入循环。
- 支持内置命令：`/help`、`/status`、`/clear`、`/model`、`/api`、`/interrupt`、`/exit`。
- 支持 Ctrl+C 中断标记。
- 支持 API / Provider / Model 配置的运行时查看和设置。
- 不暴露 API Key，只保存 API Key 的环境变量名。
- 记录用户输入和系统提示到 SessionHistory。

## 3. 非目标

第一版不做：

- FTXUI 复杂布局。
- 鼠标交互。
- 真正异步 Agent 执行中断。
- 完整 ConfigLoader。
- 真实 Provider 调用。
- 多窗口滚动。
- 配置落盘。

这些后续在 ConfigLoader、AgentLoop、Provider 和正式 TUI 阶段继续补齐。

## 4. TuiConfig

第一版先做运行时配置结构：

```cpp
struct TuiConfig {
    std::string provider = "mock";
    std::string model = "mock-model";
    std::string api_base = "";
    std::string api_key_env = "";
    int timeout_seconds = 60;
    int max_loops = 8;
};
```

敏感信息策略：

```text
/api key-env OPENAI_API_KEY
```

只记录环境变量名，不读取、不打印、不保存真实 key。

## 5. 内置命令

### /help

展示命令帮助。

### /status

展示当前状态：

```text
status
provider
model
api_base
api_key_env
max_loops
timeout_seconds
interrupted
history size
```

### /clear

清空 SessionHistory 和屏幕输出。

### /model

```text
/model
/model gpt-4.1
```

不带参数显示当前模型，带参数切换模型。

### /api

```text
/api
/api provider openai
/api base https://api.openai.com/v1
/api key-env OPENAI_API_KEY
/api timeout 60
/api max-loops 8
```

### /interrupt

设置 interrupted 标记。

第一版只做状态标记，后续 AgentLoop 执行工具或模型流时检查该标记。

### /exit

退出程序。

## 6. Ctrl+C 中断

POSIX / Windows 通用第一版使用 C signal：

```cpp
std::signal(SIGINT, handler)
```

处理逻辑：

```text
第一次 Ctrl+C：设置 interrupted 标记，并提示用户输入 /exit 退出或继续输入。
```

第一版不直接强杀进程，避免破坏后续审计日志写入。

## 7. 后续演进

本轮只是 TUI-lite。后续完整 TUI 应替换或扩展为：

```text
Chat History
Status Bar
Tool Call Log
Permission Panel
Input Box
```

并接入：

```text
AgentLoop
ApprovalService
ConfigLoader
Provider
SkillRuntime
```

## 8. 测试计划

新增：

```text
tests/test_tui_app.cpp
```

覆盖：

- `/model` 能显示和设置模型。
- `/api provider` 能设置 Provider。
- `/api key-env` 不暴露真实 key，只保存 env 名。
- `/interrupt` 能设置 interrupted。
- `/clear` 能清空 history。

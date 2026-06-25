# C++ TUI Agent 中文实施计划（Skills Runtime 版）

> 本计划将原 Python/Textual 实施方案调整为纯 C++ 方案，并采用 kwoa-cli 风格 Skills Runtime 组织 Agent 能力。实施时建议按任务逐项推进：先写测试，再实现，再运行验证。

## 1. 目标

从零实现一个最小可用的本地 TUI 编码 Agent，使其能理解用户开发任务、读取仓库上下文、调用结构化工具、执行权限确认、把工具结果回传给模型继续推理，并产出 L2 认证要求的测试与交付物。

最终程序应具备：

- C++ TUI 交互界面
- 自研 Agent Loop
- 自研 Tool System
- kwoa-cli 风格 Skill Runtime
- 权限确认与拒绝处理
- WPS CodingPlan Provider 适配
- Session History 与 Audit Log
- 用户级 / 项目级配置管理
- Mock Provider 测试闭环
- 可运行验证产物

## 2. 总体架构

```text
TUI Layer
  ↓
App Controller
  ↓
Agent Loop
  ↓
Skill Selector
  ↓
Provider Adapter
  ↓
Tool Call Parser
  ↓
Permission Gate
  ↓
Tool Registry
  ↓
Session + Audit Log
```

核心原则：

- Agent Loop、工具系统、权限控制、会话管理必须自行实现。
- 第三方库只能用于 TUI 渲染、HTTP 请求、JSON/YAML 解析、日志和测试等非核心能力。
- Skill 只描述能力，不直接执行。
- Tool 是唯一执行入口。
- Provider 只负责模型协议，不掌控 Agent 流程。

## 3. 技术栈建议

- C++20
- CMake
- FTXUI：终端 TUI
- nlohmann/json：JSON 与工具 schema
- yaml-cpp：配置和 skill.yaml
- cpp-httplib 或 cpr/libcurl：HTTP Provider
- doctest 或 Catch2：测试
- std::filesystem：仓库文件操作

## 4. 目录规划

```text
.
├── CMakeLists.txt
├── README.md
├── config.example.yaml
├── docs/
│   ├── 00-implementation-plan.md
│   ├── 01-skill-standard.md
│   ├── 02-agent-loop-and-runtime.md
│   └── 03-delivery-roadmap.md
├── output/
│   └── l2-agent-tui-task.md
├── skills/
│   ├── repo_reader/
│   │   ├── skill.yaml
│   │   └── SKILL.md
│   ├── code_editor/
│   │   ├── skill.yaml
│   │   └── SKILL.md
│   ├── shell_runner/
│   │   ├── skill.yaml
│   │   └── SKILL.md
│   ├── cpp_project/
│   │   ├── skill.yaml
│   │   └── SKILL.md
│   └── tui_agent/
│       ├── skill.yaml
│       └── SKILL.md
├── include/
│   └── agent_tui/
│       ├── app/
│       │   └── App.hpp
│       ├── tui/
│       │   ├── TuiApp.hpp
│       │   └── TuiEvent.hpp
│       ├── agent/
│       │   ├── AgentLoop.hpp
│       │   ├── AgentEvent.hpp
│       │   ├── Message.hpp
│       │   └── ToolCall.hpp
│       ├── session/
│       │   ├── Session.hpp
│       │   └── AuditLog.hpp
│       ├── skills/
│       │   ├── Skill.hpp
│       │   ├── SkillRegistry.hpp
│       │   ├── SkillSelector.hpp
│       │   └── SkillLoader.hpp
│       ├── tools/
│       │   ├── Tool.hpp
│       │   ├── ToolRegistry.hpp
│       │   ├── FileTools.hpp
│       │   └── ShellTool.hpp
│       ├── permissions/
│       │   ├── Permission.hpp
│       │   └── ApprovalService.hpp
│       ├── llm/
│       │   ├── Provider.hpp
│       │   ├── MockProvider.hpp
│       │   ├── OpenAICompatibleProvider.hpp
│       │   └── CodingPlanProvider.hpp
│       └── config/
│           └── Config.hpp
├── src/
│   ├── main.cpp
│   ├── app/
│   ├── tui/
│   ├── agent/
│   ├── session/
│   ├── skills/
│   ├── tools/
│   ├── permissions/
│   ├── llm/
│   └── config/
├── tests/
│   ├── test_agent_loop.cpp
│   ├── test_tool_registry.cpp
│   ├── test_skills.cpp
│   ├── test_permissions.cpp
│   ├── test_config.cpp
│   └── test_mock_provider.cpp
├── .ai_history/
│   └── logs/
│       └── .gitkeep
└── deliverables/
    ├── README.md
    └── demo-task.md
```

## 5. 核心数据结构

### Message

```cpp
enum class Role {
    System,
    User,
    Assistant,
    Tool
};

struct Message {
    Role role;
    std::string content;
    std::string tool_call_id;
};
```

### ToolCall

```cpp
struct ToolCall {
    std::string id;
    std::string name;
    nlohmann::json arguments;
};
```

### ProviderResponse

```cpp
enum class ProviderResponseType {
    Text,
    ToolCalls,
    Error
};

struct ProviderResponse {
    ProviderResponseType type;
    std::string text;
    std::vector<ToolCall> tool_calls;
    std::string error;
};
```

### Tool

```cpp
enum class PermissionMode {
    Auto,
    Confirm
};

struct ToolResult {
    bool ok;
    std::string output;
    std::string error;
};

class Tool {
public:
    virtual ~Tool() = default;

    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual nlohmann::json parameters_schema() const = 0;
    virtual PermissionMode permission_mode() const = 0;

    virtual ToolResult run(const nlohmann::json& args) = 0;
};
```

### Skill

```cpp
struct Skill {
    std::string id;
    std::string name;
    std::string description;
    std::vector<std::string> triggers;
    std::vector<std::string> allowed_tools;
    std::string instruction_md;
    int priority = 0;
};
```

## 6. 实施任务

### 任务 1：初始化 C++ 工程骨架

目标：建立可编译、可测试、可运行的 C++ 项目。

创建：

- `CMakeLists.txt`
- `src/main.cpp`
- `include/agent_tui/...`
- `tests/`

验收：

- `cmake -S . -B build` 通过
- `cmake --build build` 通过
- `ctest --test-dir build` 可运行

### 任务 2：实现 Session 与 Audit Log

目标：所有关键交互都能进入会话历史，并可写入 `.ai_history/logs/session.jsonl`。

事件类型：

- user
- assistant
- tool_call
- tool_result
- permission_request
- permission_denial
- error
- status

验收：

- 能按顺序记录事件
- 能 clear 当前会话
- 权限拒绝和错误信息有明确事件类型

### 任务 3：实现 Tool System

目标：提供结构化工具注册、schema 暴露、工具执行和错误返回。

第一批工具：

- `list_dir`
- `read_file`
- `glob_files`
- `search_text`
- `write_file`
- `edit_file`
- `run_shell`

权限：

- 只读工具：Auto
- 写入、编辑、Shell：Confirm

文件工具必须限制在 workspace 根目录内，禁止通过 `../` 逃逸。

### 任务 4：实现 Permission Gate

目标：写文件、编辑文件和 Shell 命令必须经过用户授权。

拒绝时：

- 不执行工具
- 记录 permission_denial
- 将拒绝结果作为 tool_result 回传模型

### 任务 5：实现 MockProvider

目标：不用真实模型也能测试 Agent Loop。

MockProvider 应支持：

- 直接返回文本
- 返回单个 tool_call
- 返回多个 tool_call
- 在收到 tool_result 后返回最终答案
- 返回错误

### 任务 6：实现 Agent Loop

目标：跑通完整的「模型决策 -> 工具调用 -> 结果回传 -> 继续推理」流程。

能力：

- max_loops 防无限循环
- 处理 tool not found
- 处理 permission denied
- 处理 provider error
- 支持多轮 tool_call

### 任务 7：实现 Skill Runtime

目标：加载 `skills/*/skill.yaml` 与 `SKILL.md`，并根据用户输入选择相关 skill。

第一版 SkillSelector 使用关键词和 trigger 匹配，不引入 embedding。

### 任务 8：实现 TUI

目标：终端中展示用户输入、模型回复、工具调用、工具结果、权限确认和当前状态。

状态流：

```text
Idle -> Thinking -> CallingTool -> WaitingApproval -> RunningTool -> Done
```

内置命令：

- `/help`
- `/clear`
- `/status`
- `/model`
- `/model <name>`
- `/skills`
- `/exit`

### 任务 9：实现 Provider Adapter

Provider 接口：

- `MockProvider`
- `OpenAICompatibleProvider`
- `CodingPlanProvider`

CodingPlanProvider 需要支持：

- 工具调用解析
- 流式输出
- 超时控制
- 基础重试

### 任务 10：交付物与测试

测试至少覆盖：

- Agent 主循环
- 工具调用与结果回传
- 权限确认与拒绝
- 配置优先级
- Mock Provider 场景
- Skill 加载与选择

`deliverables/` 放置可运行验证任务和截图说明。

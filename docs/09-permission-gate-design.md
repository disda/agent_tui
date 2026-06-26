# PermissionGate 技术设计

## 1. 背景

`agent_tui` 已完成 Minimal AgentRunner、ToolRegistry、FileTools 和 WorkspaceGuard。下一步必须实现 PermissionGate。

原因：后续要实现的 `run_shell`、`write_file`、`edit_file`、`run_kwoa_cli` 都属于高风险能力。如果没有权限确认机制，Agent 可能在模型误判或 prompt injection 的影响下直接执行危险操作。

PermissionGate 是真实 Coding Agent 与玩具 Agent 的关键分界。

## 2. 设计目标

PermissionGate 需要满足：

- 只读工具自动执行。
- 写文件、编辑文件、Shell 命令必须确认。
- 用户拒绝时不得执行工具。
- 用户拒绝结果必须作为 tool_result 回传模型。
- 用户可以编辑工具参数后再执行。
- 用户可以给出反馈，让模型重新思考，而不是执行工具。
- 所有权限请求、拒绝、编辑、反馈后续都要能进入 SessionHistory / AuditLog。

## 3. 核心概念

### 3.1 PermissionMode

已有工具权限模式：

```cpp
enum class PermissionMode {
    Auto,
    Confirm,
};
```

当前规则：

| PermissionMode | 行为 |
| --- | --- |
| Auto | 自动执行，不弹权限确认 |
| Confirm | 必须经过 ApprovalService |

### 3.2 ApprovalType

PermissionGate 不应只支持 yes/no。第一版设计四种结果：

```cpp
enum class ApprovalType {
    Approve,
    Deny,
    Edit,
    Feedback,
};
```

| ApprovalType | 行为 |
| --- | --- |
| Approve | 按模型原始参数执行工具 |
| Deny | 不执行工具，回传 `User denied permission.` |
| Edit | 使用用户编辑后的参数执行工具 |
| Feedback | 不执行工具，把用户反馈作为 tool_result 回传模型 |

### 3.3 ApprovalDecision

```cpp
struct ApprovalDecision {
    ApprovalType type;
    JsonLike edited_arguments;
    std::string feedback;
};
```

静态构造函数：

```cpp
ApprovalDecision::approve();
ApprovalDecision::deny(reason);
ApprovalDecision::edit(arguments);
ApprovalDecision::user_feedback(text);
```

## 4. ApprovalService

PermissionGate 的交互由 `ApprovalService` 抽象承载：

```cpp
class ApprovalService {
public:
    virtual ~ApprovalService() = default;
    virtual ApprovalDecision request(const ToolCall& call, const Tool& tool) = 0;
};
```

第一版提供：

```text
MockApprovalService
```

用于单元测试。

后续 TUI 会实现：

```text
TuiApprovalService
```

它负责在 TUI 中展示：

- tool name
- arguments
- risk reason
- cwd / path / command
- approve / deny / edit / feedback 操作

## 5. AgentRunner 集成点

`AgentRunner` 在执行工具前检查：

```cpp
if (tool->permission_mode() == PermissionMode::Confirm) {
    auto decision = approval_service.request(call, *tool);
    ...
}
```

执行规则：

```text
Auto:
  直接执行工具

Confirm + Approve:
  执行原始参数

Confirm + Deny:
  不执行工具
  messages.push_back(tool_result("User denied permission."))

Confirm + Edit:
  使用 edited_arguments 执行工具

Confirm + Feedback:
  不执行工具
  messages.push_back(tool_result("User feedback: ..."))
```

如果工具是 Confirm，但没有配置 ApprovalService：

```text
不执行工具
回传：Permission required but no approval service configured.
```

## 6. 测试计划

新增：

```text
tests/test_permission_gate.cpp
```

覆盖：

- Confirm 工具被 Deny 后不执行。
- Deny 结果作为 tool_result 回传模型。
- Confirm 工具 Approve 后执行。
- Confirm 工具 Edit 后使用 edited arguments 执行。
- Confirm 工具 Feedback 后不执行，并把反馈回传模型。

## 7. 当前限制

第一版 PermissionGate 只实现运行时机制，不做真实 TUI 弹窗。

暂不实现：

- 本次会话永久允许。
- 按工具类型记住选择。
- 复杂风险分级。
- 命令 diff 展示。
- 对写文件内容做结构化摘要。

这些后续在 TUI 和 ShellTool 阶段补齐。

## 8. 下一步

完成 PermissionGate 后，才能继续实现：

```text
feat: add controlled shell tool
```

因为 `run_shell` 必须走 PermissionGate。

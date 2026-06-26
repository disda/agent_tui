# AI 协作记录：PermissionGate

日期：2026-06-26

## 背景

用户要求后续每一步开发都先写技术文档，再进行开发实现。本轮在 FileTools + WorkspaceGuard 之后，进入 PermissionGate。

## 本轮技术文档

新增：

```text
docs/09-permission-gate-design.md
```

文档明确：

- PermissionGate 是真实 Coding Agent 与玩具 Agent 的关键分界。
- `run_shell`、`write_file`、`edit_file`、`run_kwoa_cli` 必须依赖 PermissionGate。
- PermissionGate 不应只有 yes/no，而应支持 Approve / Deny / Edit / Feedback。

## 本轮实现

新增：

```text
include/agent_tui/permissions/Approval.hpp
include/agent_tui/permissions/ApprovalService.hpp
include/agent_tui/permissions/MockApprovalService.hpp
tests/test_permission_gate.cpp
```

更新：

```text
include/agent_tui/agent/AgentRunner.hpp
CMakeLists.txt
README.md
TODO.md
docs/10-permission-gate-verification.md
```

## 当前新增能力

- Confirm 工具执行前请求 ApprovalService。
- Approve：执行原始参数。
- Deny：不执行工具，回传 `User denied permission.`。
- Edit：使用编辑后的参数执行工具。
- Feedback：不执行工具，回传 `User feedback: ...`。
- Confirm 工具在没有 ApprovalService 时不会执行。

## 本地隔离验证

执行：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

结果：

```text
100% tests passed, 0 tests failed out of 3
```

## 下一步建议

下一步进入：

```text
feat: add controlled shell tool
```

原因：PermissionGate 已完成，`run_shell` 可以作为 Confirm 工具进入受控执行阶段。

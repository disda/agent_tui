# 构建验证记录：Controlled Shell Tool

日期：2026-06-26

## 1. 验证目标

本次验证目标是实现受控 shell 命令执行工具：

```text
run_shell
```

该工具用于让 Agent 能在真实项目中执行构建、测试和本地 CLI 命令。它是后续完成真实 Coding Agent 验证的必要能力。

## 2. 新增内容

新增：

```text
include/agent_tui/tools/ShellTool.hpp
tests/test_shell_tool.cpp
```

更新：

```text
CMakeLists.txt
```

## 3. 当前能力

`ShellTool` 当前实现：

- 工具名：`run_shell`
- 权限：`PermissionMode::Confirm`
- cwd 使用 `Workspace.resolve()` 限制在 workspace 内
- POSIX 平台使用 `/bin/sh -c` 执行命令
- 捕获 stdout
- 捕获 stderr
- 捕获 exit_code
- 支持 timeout
- 支持 max_output_bytes 输出限制

返回格式：

```text
exit_code: 0
timeout: false
stdout:
...
stderr:
...
```

## 4. 安全边界

`run_shell` 是高风险工具，因此：

```text
ShellTool::permission_mode() == PermissionMode::Confirm
```

这意味着它必须经过 AgentRunner 的 PermissionGate。

如果用户拒绝：

```text
不执行命令
tool_result = User denied permission.
```

测试中已经验证 `touch marker` 在 Deny 后不会执行。

## 5. 测试覆盖

`tests/test_shell_tool.cpp` 覆盖：

- `test_run_shell_echo`
- `test_run_shell_nonzero_exit`
- `test_run_shell_timeout`
- `test_run_shell_denied_not_executed`
- `test_run_shell_rejects_cwd_escape`

其中：

- `nonzero_exit` 验证命令失败码会作为工具输出返回模型。
- `timeout` 验证超时后返回 `timeout: true`。
- `denied_not_executed` 验证 PermissionGate 拒绝后命令不会执行。
- `rejects_cwd_escape` 验证 cwd 不能逃逸 workspace。

## 6. 本地隔离构建环境

验证命令：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## 7. 验证结果

本地沙箱验证通过：

```text
[100%] Built target agent_tui_shell_tool_tests
Test project /mnt/data/agent_tui_local/build
    Start 1: agent_tui_tests
1/4 Test #1: agent_tui_tests ...................   Passed    0.02 sec
    Start 2: agent_tui_file_tools_tests
2/4 Test #2: agent_tui_file_tools_tests ........   Passed    0.02 sec
    Start 3: agent_tui_permission_gate_tests
3/4 Test #3: agent_tui_permission_gate_tests ...   Passed    0.02 sec
    Start 4: agent_tui_shell_tool_tests
4/4 Test #4: agent_tui_shell_tool_tests ........   Passed    2.08 sec

100% tests passed, 0 tests failed out of 4
```

## 8. 当前限制

第一版限制：

- Windows 原生进程控制暂未实现。
- 暂不支持 stdin 交互。
- 暂不支持后台长任务。
- 暂不支持复杂命令风险分级。
- 暂不支持 per-command policy。
- 暂不做 Docker / 云沙箱。

## 9. 下一步

下一步建议实现：

```text
feat: add write and edit file tools
```

原因：PermissionGate、WorkspaceGuard 和 ShellTool 已完成，下一步可以补齐真实 coding agent 需要的写入与编辑能力。

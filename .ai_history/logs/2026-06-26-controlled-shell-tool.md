# AI 协作记录：Controlled Shell Tool

日期：2026-06-26

## 背景

用户要求继续按“每一步开发先技术文档再开发”的流程推进。PermissionGate 已完成，因此本轮进入受控 Shell 执行能力。

## 本轮技术文档

新增：

```text
docs/11-controlled-shell-tool-design.md
```

文档明确：

- `run_shell` 是真实 Coding Agent 的必要能力。
- `run_shell` 必须是 `PermissionMode::Confirm`。
- cwd 必须受 WorkspaceGuard 限制。
- 第一版只做 POSIX 实现，不做 Docker / 云沙箱 / 内置解释器。

## 本轮实现

新增：

```text
include/agent_tui/tools/ShellTool.hpp
tests/test_shell_tool.cpp
```

更新：

```text
CMakeLists.txt
README.md
TODO.md
docs/12-controlled-shell-tool-verification.md
```

## 当前新增能力

`ShellTool`：

- 工具名：`run_shell`
- 权限：`PermissionMode::Confirm`
- 参数：`command`、`cwd`、`timeout_seconds`、`max_output_bytes`
- 返回：`exit_code`、`timeout`、`stdout`、`stderr`
- POSIX 实现使用 `pipe` / `fork` / `dup2` / `chdir` / `/bin/sh -c` / `waitpid` / `kill`
- cwd 通过 `Workspace.resolve()` 限制在 workspace 内

测试覆盖：

- echo 命令执行
- 非零 exit_code 返回
- timeout
- PermissionGate 拒绝后不执行
- cwd 逃逸拒绝

## 本地隔离验证

执行：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

结果：

```text
100% tests passed, 0 tests failed out of 4
```

## 下一步建议

下一步进入：

```text
feat: add write and edit file tools
```

原因：PermissionGate、WorkspaceGuard 和 ShellTool 已经完成，下一步应补齐真实 coding agent 的文件修改能力。

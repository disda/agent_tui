# Controlled Shell Tool 技术设计

## 1. 背景

`agent_tui` 已完成：

```text
Minimal AgentRunner
FileTools + WorkspaceGuard
PermissionGate
```

下一步需要实现受控命令执行能力：

```text
run_shell
```

这是交付要求中的关键能力。没有 `run_shell`，Agent 只能读写文件，无法构建项目、执行测试命令，也无法用工具结果继续调试。

但 Shell 是高风险能力，因此必须建立在 PermissionGate 和 WorkspaceGuard 之上。

## 2. 设计目标

`run_shell` 第一版目标：

- 能执行本地 shell 命令。
- 必须是 `PermissionMode::Confirm`。
- 必须经过 AgentRunner 的 ApprovalService。
- cwd 必须限制在 workspace 内。
- 捕获 stdout。
- 捕获 stderr。
- 捕获 exit_code。
- 支持 timeout。
- 支持输出长度限制。
- 用户拒绝时不执行。

## 3. 非目标

第一版不做：

- Docker 沙箱。
- 云沙箱。
- 内置 Python / JS 解释器。
- 复杂命令策略引擎。
- 自动安装依赖。
- 后台长任务管理。
- 交互式 stdin。
- Windows 原生 Shell 进程控制完整实现。

Windows 可先返回 unsupported，后续再补 `CreateProcess` 版本。

## 4. Tool 定义

工具名：

```text
run_shell
```

权限：

```cpp
PermissionMode::Confirm
```

参数：

```text
command: string
cwd: string, default = "."
timeout_seconds: integer, default = 30
max_output_bytes: integer, default = 65536
```

返回格式使用文本结构化输出：

```text
exit_code: 0
timeout: false
stdout:
...
stderr:
...
```

`exit_code != 0` 不代表工具自身失败，而是命令运行完成但命令返回失败码。因此第一版会把非零退出码作为正常 ToolResult 输出交给模型分析。

工具自身失败只用于：

- 缺少 command。
- cwd 非法。
- cwd 逃逸 workspace。
- fork/pipe 等系统调用失败。

## 5. Workspace 约束

`run_shell` 不直接接受任意 cwd。

流程：

```text
cwd argument
  ↓
Workspace.resolve(cwd)
  ↓
确保 canonical path 在 workspace root 内
  ↓
chdir(cwd)
  ↓
执行命令
```

这能阻止：

```text
cwd = ../
cwd = /etc
cwd = ../../other-project
```

## 6. PermissionGate 集成

`ShellTool::permission_mode()` 返回：

```cpp
PermissionMode::Confirm
```

因此 AgentRunner 会先请求 ApprovalService：

```text
Approve -> 执行命令
Deny -> 不执行，回传 User denied permission.
Edit -> 使用编辑后的 command/cwd/timeout 执行
Feedback -> 不执行，回传 User feedback: ...
```

这保证模型不能绕过用户确认直接运行命令。

## 7. POSIX 实现方案

第一版在 POSIX 平台使用：

```text
pipe
fork
dup2
chdir
execl("/bin/sh", "sh", "-c", command)
waitpid(WNOHANG)
kill(SIGKILL)
```

stdout / stderr 分别用 pipe 捕获。

timeout 逻辑：

```text
启动子进程
循环读取 stdout/stderr
waitpid WNOHANG 检查退出
超过 timeout_seconds 时 kill 子进程
继续 drain pipes
返回 timeout: true
```

## 8. 测试计划

新增：

```text
tests/test_shell_tool.cpp
```

覆盖：

- `test_run_shell_echo`
- `test_run_shell_nonzero_exit`
- `test_run_shell_timeout`
- `test_run_shell_denied_not_executed`

其中 `test_run_shell_denied_not_executed` 必须通过 AgentRunner + MockApprovalService 验证：

```text
模型请求 run_shell: touch marker
ApprovalService Deny
marker 文件不得出现
tool_result 必须是 User denied permission.
```

## 9. 下一步

完成 `run_shell` 后，下一步进入：

```text
feat: add write and edit file tools
```

因为写文件和编辑文件也要基于 PermissionGate + WorkspaceGuard。

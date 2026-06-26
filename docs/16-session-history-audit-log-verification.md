# 验证说明：SessionHistory / AuditLog

日期：2026-06-26

## 1. 验证目标

本次验证目标是确认运行时会话历史和审计日志能力已接入 AgentRunner。

新增内容：

```text
include/agent_tui/session/SessionEvent.hpp
include/agent_tui/session/SessionHistory.hpp
include/agent_tui/session/AuditLog.hpp
tests/test_session_history.cpp
```

更新：

```text
include/agent_tui/agent/AgentRunner.hpp
CMakeLists.txt
```

## 2. 当前能力

### SessionEvent

支持事件类型：

```text
user_input
assistant_message
tool_call
tool_result
permission_denied
user_feedback
error
```

每条事件包含：

```text
timestamp
type
content
tool_call_id
tool_name
arguments
```

### SessionHistory

内存事件容器，支持：

```text
add
events
clear
empty
size
```

### AuditLog

支持将事件写入 JSONL：

```text
.agent-tui/sessions/<session-id>.jsonl
```

当前第一版提供指定路径写入能力。

### AgentRunner 集成

AgentRunner 当前会记录：

- 输入 messages 中的 user message。
- Provider 普通文本回复。
- Provider error。
- 每个 tool_call。
- 每个 tool_result。
- permission_denied。
- user_feedback。
- tool not found。
- max_loops exceeded。

## 3. 测试覆盖

`tests/test_session_history.cpp` 覆盖：

- `test_session_history_records_tool_flow`
- `test_session_history_records_permission_denial`
- `test_audit_log_writes_jsonl`
- `test_json_escape_in_audit_log`

## 4. 验证命令

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## 5. 当前限制

第一版限制：

- 暂无 session id 生成。
- 暂无多会话索引。
- 暂无日志轮转。
- 暂无上下文压缩。
- 暂无 TUI 展示。
- 暂未接入 AgentLoop。

## 6. 下一步

下一步建议实现：

```text
feat: add config loader with user project priority
```

原因：作业测试要求明确需要覆盖配置优先级。

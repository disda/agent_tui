# 构建验证记录：Minimal AgentRunner Skeleton

日期：2026-06-26

## 1. 验证目标

本次验证目标是确认第一版 C++ 项目地基可编译、可运行、可测试。

新增内容包括：

```text
CMakeLists.txt
src/main.cpp
include/agent_tui/agent/Message.hpp
include/agent_tui/agent/ToolCall.hpp
include/agent_tui/agent/AgentResult.hpp
include/agent_tui/agent/AgentRunner.hpp
include/agent_tui/llm/Provider.hpp
include/agent_tui/llm/MockProvider.hpp
include/agent_tui/tools/Tool.hpp
include/agent_tui/tools/ToolRegistry.hpp
tests/test_agent_runner.cpp
```

## 2. 本地隔离构建环境

C++ 项目不使用 Python venv。本次使用独立 `build/` 目录作为隔离构建环境。

验证命令：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## 3. 验证结果

本地沙箱验证通过：

```text
-- Configuring done
-- Generating done
-- Build files have been written to: /mnt/data/agent_tui_skel/build
[100%] Built target agent_tui_tests
Test project /mnt/data/agent_tui_skel/build
    Start 1: agent_tui_tests
1/1 Test #1: agent_tui_tests ..................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 1
```

## 4. 当前测试覆盖

`tests/test_agent_runner.cpp` 当前覆盖：

- MockProvider 第一次返回 tool_call，ToolRegistry 执行工具，第二次返回 Done。
- Tool 不存在时，错误作为 tool_result 回传模型。
- 超过 max_loops 时返回失败。

## 5. 当前限制

当前只是 AgentRunner 最小骨架，还没有实现：

- 真实文件工具
- Shell 工具
- PermissionGate
- SkillRuntime
- TUI
- CodingPlanProvider

下一步建议：

```text
feat: add permission gate and file tools
```

或者先继续补：

```text
feat: add real file read/list/search tools
```

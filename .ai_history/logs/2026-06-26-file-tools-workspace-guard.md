# AI 协作记录：FileTools + WorkspaceGuard

日期：2026-06-26

## 背景

用户要求先将 Hello-Agents / 《从零开始构建智能体》的启发总结进文档并提交，然后开始实现：

```text
feat: add file tools and workspace guard
```

## 本轮文档沉淀

新增：

```text
docs/07-hello-agents-lessons.md
```

核心结论：

- `agent_tui` 是通用 C++ Agent Harness + 可插拔 Skills。
- `kwoa-cli` 是第一个真实 Skill 验证场景，不是唯一目标。
- 第一阶段应保持轻量，采用 ReAct-like loop。
- 万物皆工具是第一阶段最好的简化。
- 下一步应先做 FileTools + WorkspaceGuard。

## 本轮代码实现

新增：

```text
include/agent_tui/workspace/Workspace.hpp
include/agent_tui/tools/FileTools.hpp
tests/test_file_tools.cpp
```

更新：

```text
CMakeLists.txt
README.md
TODO.md
docs/08-file-tools-workspace-guard.md
```

## 当前新增能力

`Workspace`：

- 保存 workspace root。
- canonical path 校验。
- 阻止 `../` path traversal。
- 将绝对路径转为 workspace 相对展示路径。

`FileTools`：

- `list_dir`
- `read_file`
- `glob_files`
- `search_text`

安全规则：

- 只读工具为 `PermissionMode::Auto`。
- 所有路径必须经过 WorkspaceGuard。
- `search_text` 跳过 `.git`、`build`、`cmake-build-*`。
- `read_file` 支持 `max_bytes` 输出限制。

## 本地隔离验证

执行：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

结果：

```text
100% tests passed, 0 tests failed out of 2
```

## 下一步建议

下一步进入：

```text
feat: add permission gate
```

原因：`run_shell` 是高风险工具，必须先实现 PermissionGate，再进入真实 shell 执行。

# TODO：C++ TUI Agent 下一步计划

> 当前项目方向：纯 C++ 实现一个本地 TUI Coding Agent，并采用 kwoa-cli 风格 Skills Runtime 组织能力。

## 当前状态

- [x] 明确项目从 Python/Textual 方案调整为纯 C++ 方案。
- [x] 明确采用 kwoa-cli 风格 Skills Runtime。
- [x] 完成设计文档：`docs/00` 到 `docs/06`。
- [x] 按项目要求补充 `.ai_history/logs/` AI 协作记录。
- [x] 新增 C++ 工程骨架。
- [x] 新增 Minimal AgentRunner。
- [x] 新增 Tool / ToolRegistry 抽象。
- [x] 新增 Provider / MockProvider 抽象。
- [x] 新增 Done 虚拟工具处理。
- [x] 新增 `tests/test_agent_runner.cpp`。
- [x] 已在本地隔离 `build/` 目录中验证 `cmake` / `build` / `ctest` 通过。
- [x] 创建 `deliverables/` 可运行验证产物目录。
- [x] 明确最终验收 Demo：加载 kwoa-cli Skill，实现 IM / KDocs 文档操作能力验证。

## 当前已验证命令

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

验证记录见：

```text
docs/06-build-verification.md
```

## 交付清单状态

| 交付要求 | 状态 | 位置 |
| --- | --- | --- |
| 项目源码仓库 | 进行中 | `CMakeLists.txt`、`include/`、`src/`、`tests/` |
| AI 协作过程记录 | 进行中，已提交 | `.ai_history/logs/` |
| 可运行验证产物 | 已创建目录，持续补充 | `deliverables/` |

当前 deliverables：

```text
deliverables/README.md
deliverables/demo-kwoa-cli-skill.md
deliverables/screenshots/.gitkeep
```

## 项目最终验收目标

本项目最终不是做普通玩具 Demo，而是验证一个能加载真实 Skill 的本地 Coding Agent Harness：

```text
加载 kwoa-cli Skill，
理解 IM / KDocs 文档操作规则，
执行只读 IM / 文档查询，
对消息发送、撤回、转发、reaction、KDocs 写入进行权限确认，
把工具结果回传模型继续推理。
```

## Agent-Learning-Hub 启发

- [x] 确认主线不是老式 role-play 多 Agent，而是 Codex / Claude Code 风格 coding agent harness。
- [x] 确认 Agent 能力主要来自 harness：工具协议、权限、状态、反馈、回放、CI、评测。
- [x] 确认 Skills 是能力包，不是普通 Prompt。
- [x] 确认 Evaluation 和 Safety 需要前置，不应等到后期补。
- [x] 确认第一阶段不做 Browser / Computer Use Agent。
- [x] 确认 kwoa-cli Skill 作为真实 smoke test。

当前实现主线：

```text
用 Agent-Learning-Hub 的学习/工程节奏，
实现 FastClaw / Nanobot 风格的小型 C++ Agent Harness，
再用 kwoa-cli Skill 做真实验收。
```

## 下一步优先级

### 1. File Tools

实现真实只读文件工具：

- [ ] `list_dir`
- [ ] `read_file`
- [ ] `glob_files`
- [ ] `search_text`
- [ ] workspace root 限制
- [ ] path traversal 防护
- [ ] 工具结果长度限制

### 2. PermissionGate

实现危险操作确认：

- [ ] `ApprovalType::Approve`
- [ ] `ApprovalType::Deny`
- [ ] `ApprovalType::Edit`
- [ ] `ApprovalType::Feedback`
- [ ] 写文件必须确认
- [ ] Shell 命令必须确认
- [ ] 拒绝结果作为 tool_result 回传模型

### 3. Write / Edit / Shell Tools

实现需要确认的工具：

- [ ] `write_file`
- [ ] `edit_file`
- [ ] `run_shell`
- [ ] stdout / stderr / exit code 捕获
- [ ] timeout
- [ ] cwd 限制

### 4. kwoa-cli Skill Runtime 验证

实现加载 kwoa-cli Skill 并验证 IM / 文档操作：

- [ ] 兼容加载 `skills/kwoa-cli/SKILL.md` 风格 Skill。
- [ ] 新增 `skills/kwoa_cli/skill.yaml`。
- [ ] 新增 `skills/kwoa_cli/SKILL.md`。
- [ ] 新增 `run_kwoa_cli` 工具或通过 `run_shell` 安全封装。
- [ ] `auth status` 作为只读初始化检查。
- [ ] IM 只读命令限制 count/page-limit。
- [ ] KDocs 读取命令支持 docs +info / docs +read。
- [ ] IM / KDocs 写操作必须走 PermissionGate。
- [ ] 增加 `test_kwoa_cli_send_message_requires_confirm`。

### 5. SessionHistory / AuditLog

实现运行时会话和审计日志：

- [ ] `SessionEvent`
- [ ] `SessionHistory`
- [ ] `AuditLog`
- [ ] 记录 user / assistant / tool_call / tool_result / permission_denial / error
- [ ] 运行时 session 写入 `.agent-tui/sessions/`
- [ ] AI 协作记录继续写入 `.ai_history/logs/`

### 6. SkillRuntime

实现 Skills 加载和选择：

- [ ] 加载 `skills/*/skill.yaml`
- [ ] 加载 `skills/*/SKILL.md`
- [ ] SkillSelector 关键词匹配
- [ ] 按 Skill 限制可用工具集合
- [ ] 新增 `kwoa_cli` Skill smoke test

### 7. TUI

第一版 TUI 不做复杂布局，先保证可用：

- [ ] Chat History
- [ ] Status Bar
- [ ] Tool Call Log
- [ ] Permission Panel
- [ ] Input Box
- [ ] `/help`
- [ ] `/clear`
- [ ] `/status`
- [ ] `/model`
- [ ] `/skills`
- [ ] `/exit`

## 下一次最建议做的提交

```text
feat: add file tools and workspace guard
```

建议包含：

- `include/agent_tui/tools/FileTools.hpp`
- `include/agent_tui/workspace/Workspace.hpp`
- `tests/test_file_tools.cpp`

目标：让 AgentRunner 不再只执行 fake echo tool，而是能真实读取当前 workspace 下的文件。

## 当前不要做

第一阶段先不要做：

- 向量数据库
- 多 Agent
- MCP
- 插件市场
- 自动 Git 提交
- Browser / Computer Use Agent
- 复杂长期记忆
- 复杂上下文压缩
- 完整真实 Provider

先把 C++ 工程骨架、MockProvider、AgentRunner、ToolSystem、PermissionGate、SkillRuntime 跑通。

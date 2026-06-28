# TODO：回到 L2 Coding Agent 主线

> 当前唯一 P0：让 TUI 走模型驱动的 Agent Loop。接入 API 后，用户输入一个简单开发任务，Agent 能通过 tool_calls 创建/修改代码、执行必要命令，并给出最终结果。

## 0. 目标校准

L2 题目要求的是一个最小可用的 **TUI 终端编码 Agent**，不是规则意图机器人，也不是 kwoa-cli 专用助手。

核心验收闭环：

```text
用户自然语言开发任务
  -> TUI 展示输入和运行状态
  -> Provider 返回文本或 tool_calls
  -> AgentRunner 执行工具调用
  -> write_file / edit_file / run_shell 经过权限确认
  -> tool_result 回传 Provider
  -> Provider 继续推理
  -> 最终完成任务或明确失败
```

当前策略：

- 模型 tool_calls 是主路径。
- 自然语言任务不走 Local Intent Router；TUI 不做本地意图规则抢答。
- 显式 slash command 只用于 `/status`、`/config`、`/exit` 等界面和配置控制。
- 工具能力参考 Pi / Codex CLI 风格：模型决定工具调用，TUI 负责执行、权限和可观察性。
- Skills Runtime、kwoa-cli、桌面文件整理全部降级为后续扩展，不进入当前 P0。

## 1. 当前状态

- [x] C++ 工程骨架。
- [x] TUI-lite 输入、输出、状态和基础命令。
- [x] Provider / MockProvider / OpenAI-compatible Provider 基础接口。
- [x] AgentRunner 可执行 tool_calls 并把 tool_result 回传 Provider。
- [x] ToolRegistry 与基础仓库工具。
- [x] `list_dir` / `read_file` / `glob_files` / `search_text`。
- [x] `write_file` / `edit_file`。
- [x] `run_shell`。
- [x] PermissionGate / ApprovalService。
- [x] SessionHistory / AuditLog。
- [x] 用户级 / 项目级配置。
- [x] TUI 已接入 AgentRunner 第一版。
- [x] `mock-agent-demo` 可通过 TUI 触发 `write_file`，确认后生成 `demo.py`。
- [x] 删除 Local Intent Router 自然语言规则路径。
- [x] 新增 `workspace_info`，`list_dir` 输出实际 path。

## 2. 当前最大问题

真实 API 路径还没有稳定完成“代码 demo 产出”验证。当前最大风险不是缺本地规则，而是 **工具协议还没有成为单一、完整、可观察的主路径**。

已修正的问题：

- 以前 TUI 先走规则 Intent，识别不到就只做普通聊天。
- 以前 Provider 返回 `tool_calls` 时，TUI 没有执行 AgentRunner。
- 现在普通用户输入已经可以进入 AgentRunner；`mock-agent-demo` 已验证最小工具闭环。
- “当前目录路径”这类问题不再靠本地规则；已通过 `workspace_info` / workspace path 支持模型回答。

剩余关键缺口：

- [ ] OpenAI-compatible 请求体需要稳定携带完整 tools schema。
- [x] `run_shell` 已注册到 TUI，但仍需暴露到 OpenAI-compatible tools schema。
- [x] system prompt 需要包含当前 workspace 路径，参考 Pi 的 `Current working directory`。
- [x] ToolRegistry 需要导出 tools schema，避免 Provider 内部硬编码工具列表。
- [ ] OpenAI-compatible response 的 tool_calls 解析需要覆盖真实返回格式。
- [ ] TUI 需要在消息区展示 tool_call 和 tool_result，而不是只展示最终答案。
- [ ] `run_shell` 参与代码 demo 验证：创建代码后运行测试或脚本。
- [ ] deliverables 需要记录一次真实 demo：创建代码、确认写入、运行命令、最终回答。
- [ ] WPS CodingPlan Provider 仍未实现。

## 3. P0：真实 Coding Agent Demo 闭环

### P0-A. 固化 TUI -> AgentRunner 主路径

- [x] 新增测试：TUI 使用 `mock-agent-demo` 通过 AgentRunner 写出 `demo.py`。
- [x] TUI 普通输入接入 AgentRunner。
- [x] TUI ApprovalService 支持 `write_file` 确认。
- [x] TUI 注册基础工具集合。
- [ ] TUI 输出 tool_call / tool_result 可见日志。
- [x] 移除 README 中把 Local Intent 当主能力的描述。
- [x] 删除本地规则 IntentClassifier / Intent 模块。
- [x] 新增回归测试：自然语言目录问题不走 `local intent`。

### P0-A2. 对齐 Pi / Codex CLI 工具协议

目标：所有自然语言任务都由模型通过工具协议完成，TUI 不做本地规则决策。

- [x] 新增技术文档：`docs/27-pi-codex-tooling-roadmap.md`。
- [x] `workspace_info` 工具返回 workspace 根路径。
- [x] 把当前 workspace 路径加入 system prompt。
- [x] OpenAI-compatible tools schema 暴露 `workspace_info`、`list_dir`、`read_file`、`glob_files`、`search_text`、`write_file`、`edit_file`、`run_shell`。
- [x] 新增测试：Provider request body 中包含所有 TUI 注册工具。
- [x] 新增测试：`run_shell` 出现在 tools schema。
- [ ] 用真实模型验证“当前目录路径”不 max loop，且不会出现本地规则日志。

### P0-B. 让 demo 覆盖写代码 + 执行命令

目标输入：

```text
实现一个 hello world Python demo，并运行它
```

期望过程：

```text
tool_call write_file demo.py
permission approve
tool_result wrote file
tool_call run_shell python3 demo.py
permission approve
tool_result stdout: hello from agent_tui
assistant final answer
```

待办：

- [ ] 扩展 `mock-agent-demo`：第二步调用 `run_shell`。
- [ ] 新增测试：TUI 两次确认后生成并运行 demo。
- [ ] 输出中能看到 `hello from agent_tui`。

### P0-C. 真实 API tool_calls 验证

- [x] ToolRegistry 导出 tools schema，避免 Provider 内部硬编码工具列表。
- [x] OpenAI-compatible request 使用 ToolRegistry schema。
- [ ] OpenAI-compatible Provider 解析真实 `tool_calls`。
- [ ] AgentRunner 用真实 Provider 完成至少一次代码 demo。
- [ ] 把真实运行记录写入 `deliverables/run-log.md`。

### P0-D. Pi 风格核心工具能力差距

- [ ] `read_file` 支持 offset / limit / truncation，避免大文件一次性塞回模型。
- [ ] `edit_file` 支持一次多个精确替换，并返回 diff 摘要。
- [ ] `search_text` 对齐 `grep`：支持 glob、ignoreCase、literal、limit，优先使用 `rg`。
- [ ] `glob_files` 对齐 `find`：支持 path、limit，尊重 `.gitignore`。
- [ ] `list_dir` 支持 limit，并稳定包含 dotfiles / 目录后缀。
- [ ] `run_shell` 支持输出截断、完整输出落盘、超时和中断清理。

## 4. P1：TUI 可观察性

题目要求用户能看到 Agent 当前阶段，所以 TUI 需要明确显示：

- [ ] 用户输入。
- [ ] 模型回复。
- [ ] tool_call 名称和参数摘要。
- [ ] tool_result 成功/失败。
- [ ] 权限确认请求。
- [ ] 当前状态：IDLE / THINKING / WAITING_APPROVAL / RUNNING_TOOL / DONE / ERROR。

## 5. P2：WPS CodingPlan Provider

题目明确要求支持 WPS CodingPlan 协议。等 OpenAI-compatible 主闭环稳定后实现：

- [ ] `CodingPlanProvider`。
- [ ] 工具调用解析。
- [ ] 流式输出。
- [ ] 超时控制。
- [ ] 基础重试。
- [ ] 错误回传 SessionHistory。

## 6. P3：扩展能力，暂不抢主线

以下内容暂缓，等 P0/P1/P2 完成后再做：

- Skills Runtime。
- 自定义工具注册。
- 工具 allowlist / denylist / active tools。
- tool_call hook：权限 gate、参数改写、危险命令确认。
- prompt snippets / prompt guidelines 注入 system prompt。
- kwoa-cli Skill。
- 桌面/下载目录文件整理。
- FTXUI 复杂布局。
- 上下文压缩。
- 多 Agent。
- 向量数据库。

## 7. 下一次提交建议

```text
feat: align agent tools with model-driven tool protocol
```

包含：

- 删除本地规则路由后的文档校准。
- `run_shell` 暴露到 OpenAI-compatible tools schema。
- system prompt 注入当前 workspace 路径。
- ToolRegistry schema 单一来源设计。
- 测试验证自然语言任务不走本地规则，模型可见工具和实际注册工具一致。

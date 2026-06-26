# TODO：C++ TUI Agent 能力完善路线图

> 当前项目方向：纯 C++ 实现一个本地 TUI Coding Agent，并采用 kwoa-cli 风格 Skills Runtime 组织能力。
>
> 当前最新判断：项目已经有 AgentRunner、文件工具、权限系统、SessionHistory、TUI-lite、OpenAI-compatible 文本对话、Local Intent Router、安全本地文件管理功能类和基础测试。接下来要把桌面文件意图接到这些功能类，并继续补真实 tool_calls 闭环。

## 0. 当前状态快照

- [x] 纯 C++ 工程骨架。
- [x] Minimal AgentRunner。
- [x] Tool / ToolRegistry 抽象。
- [x] Provider / MockProvider 抽象。
- [x] OpenAI-compatible Provider 文本对话。
- [x] Done 虚拟工具处理。
- [x] FileTools + WorkspaceGuard。
- [x] PermissionGate。
- [x] Controlled Shell Tool。
- [x] Windows 简版 `run_shell`。
- [x] Write / Edit Tools。
- [x] SessionHistory / AuditLog。
- [x] TUI-lite 入口。
- [x] 类似 `.codex` 的 TOML 配置目录。
- [x] 用户级 / 项目级配置，项目级覆盖用户级。
- [x] Local Intent Router。
- [x] Safe File Manager 功能类：KnownPaths / AllowedRoots / FileManagerTools。
- [x] `tests/test_agent_runner.cpp`。
- [x] `tests/test_file_tools.cpp`。
- [x] `tests/test_permission_gate.cpp`。
- [x] `tests/test_shell_tool.cpp`。
- [x] `tests/test_write_edit_tools.cpp`。
- [x] `tests/test_session_history.cpp`。
- [x] `tests/test_tui_app.cpp`。
- [x] `tests/test_config_loader.cpp`。
- [x] `tests/test_openai_compatible_provider.cpp`。
- [x] `tests/test_intent_classifier.cpp`。
- [x] `tests/test_file_manager_tools.cpp`。
- [x] `deliverables/` 目录。

## 1. 当前最大问题

用户现在已经开始提出更接近真实 Agent 的任务：

```text
列出桌面文件
移动图片到新文件夹
整理下载目录
把桌面截图归档
找出最近修改的文档
```

底层功能类已经有第一版：

```text
KnownPaths
AllowedRoots
list_path
make_dir
move_file
move_files_by_extension
```

但这些功能还没有完整接到 TUI / Local Intent Router，所以用户输入自然语言时仍不能直接完成桌面整理任务。

当前剩余缺口：

- [ ] TUI 中 `列出桌面文件` 还没路由到 `list_path desktop`。
- [ ] TUI 中 `移动图片到新文件夹` 还没生成 dry-run 计划。
- [ ] `allowed_roots` 还没有配置化。
- [ ] 批量移动计划还没有 PermissionPanel 展示。
- [ ] 移动执行还没有审计日志增强。

## 2. 作业要求对齐状态

| 作业要求 | 当前状态 | 还缺什么 |
| --- | --- | --- |
| 可编译运行的 TUI Agent 完整源码 | 部分完成 | 还缺正式 AgentLoop 应用层、完整真实 Provider tool_calls |
| TUI 展示用户输入、模型回复、工具调用、工具结果、权限确认、当前状态 | 部分完成 | 已有 TUI-lite、输入、状态、配置命令、Provider 对话、本地工具路由；还缺正式 Tool Log、Permission Panel、AgentLoop 状态 |
| 模型决策 -> 工具调用 -> 结果回传 -> 继续推理 | 部分完成 | `AgentRunner` 已具备核心闭环；Local Intent Router 可直接执行常见任务；还需正式 AgentLoop 和 Provider tool_calls 闭环 |
| 仓库工具：目录浏览、文件读取、文件匹配、内容搜索 | 已完成 | 后续优化编码检测、大文件处理 |
| 文件写入或编辑 | 已完成 | 后续可增强 diff/patch、AST 级编辑 |
| Shell 命令执行 | 已完成基础版 | POSIX 支持 timeout；Windows 支持 `_popen` 简版，后续补 timeout 强杀 |
| 安全本地文件管理 | 已完成功能类基础版 | 已有 KnownPaths / AllowedRoots / list_path / make_dir / move_file / move_files_by_extension；还缺 TUI 路由和 allowed_roots 配置化 |
| 权限控制：写文件、编辑、Shell 必须确认 | 已完成核心机制 | 文件管理工具声明 Confirm；后续 TUI 中需要可视化 Permission Panel |
| 用户拒绝授权不得执行，拒绝结果进入上下文 | 已完成核心机制 | PermissionGate / Local Intent Router 均记录拒绝结果；后续补文件移动审计详情 |
| WPS CodingPlan 协议 | 未完成 | 需要 `CodingPlanProvider`、工具调用解析、流式输出、超时、重试 |
| 多轮会话上下文管理 | 部分完成 | 已有 SessionHistory / AuditLog；还缺 AgentLoop 多轮会话组织和 TUI 展示 |
| 用户级 / 项目级配置，项目级优先 | 已完成基础版 | 后续可增强配置保存命令、allowed_roots 配置 |
| TUI 内置命令 | 部分完成 | 已有 `/help`、`/clear`、`/model`、`/status`、`/exit`、`/skills`、`/api`、`/config`、`/interrupt`；还缺接 AgentLoop 后的真实状态 |
| 不使用第三方 Agent SDK / Framework | 符合 | 继续保持核心 AgentLoop、工具、权限、会话自行实现 |
| `.ai_history/logs/` 必须提交 | 已完成并持续补充 | 每轮关键设计继续补日志 |
| `deliverables/` 可运行验证产物和截图 | 部分完成 | 目录和计划已创建，还缺真实运行日志和关键截图 |
| 测试至少覆盖 Agent 主循环、工具调用结果回传、权限确认与拒绝、配置优先级、Mock LLM Provider | 基础覆盖完成 | 已覆盖 AgentRunner、工具、权限、MockProvider、会话记录、TUI 命令、配置优先级、IntentClassifier、FileManagerTools；后续补真实 Provider tool_calls、桌面意图测试 |

## 3. 下一步优先级

### P0-A. 将桌面文件意图接到 FileManagerTools

底层功能类已完成，下一步要让 TUI 能直接执行这些任务。

- [ ] `docs/27-desktop-file-intents-design.md`。
- [ ] 扩展 `IntentType`：`ListDesktop`、`ListDownloads`、`MoveImagesToFolder`。
- [ ] `列出桌面文件` -> `list_path desktop`。
- [ ] `列出下载目录` -> `list_path downloads`。
- [ ] `移动图片到新文件夹` -> `move_files_by_extension` dry-run。
- [ ] `把桌面 png 移到 xxx` -> source=desktop, ext=png, target=xxx。
- [ ] TUI 显示 dry-run 计划。
- [ ] 用户确认后 execute=true。
- [ ] `tests/test_local_desktop_intents.cpp`。

### P0-B. FileManagerTools 增强

第一版已完成：

- [x] `docs/25-file-manager-tools-design.md`。
- [x] `include/agent_tui/filesystem/KnownPaths.hpp`。
- [x] `include/agent_tui/filesystem/AllowedRoots.hpp`。
- [x] `include/agent_tui/tools/FileManagerTools.hpp`。
- [x] `tests/test_file_manager_tools.cpp`。
- [x] `list_path`。
- [x] `make_dir`。
- [x] `move_file`。
- [x] `move_files_by_extension`。
- [x] dry-run 默认预览。
- [x] 不覆盖目标文件。
- [x] 越权路径拒绝。

后续增强：

- [ ] `copy_file`：复制文件，需要确认。
- [ ] `delete_file_safe`：移动到 `.trash` 或回收站，第一版不要永久删除。
- [ ] `list_recent_files`：按修改时间列出文件。
- [ ] `group_files_by_extension`：按扩展名生成整理计划。
- [ ] `allowed_roots` 配置字段。
- [ ] Unicode 中文路径测试。

### P1. OpenAI-compatible tool_calls 完整闭环

Local Intent Router 解决了常见任务直接执行的问题；下一步仍需要正式 Provider tool_calls 闭环。

- [ ] ToolSchema 结构。
- [ ] ToolRegistry 导出 ToolSchema。
- [ ] OpenAI-compatible tools JSON 由 ToolRegistry 生成。
- [ ] 解析 `tool_calls`。
- [ ] 将 tool_calls 转为内部 `ToolCall`。
- [ ] AgentRunner + OpenAI-compatible Provider 真实工具调用闭环。
- [ ] `tests/test_openai_compatible_tool_calls.cpp`。

### P2. AgentLoop 应用层

`AgentRunner` 已有核心 tool loop，TUI-lite 已有输入入口，Local Intent Router 已能跑常见本地任务，但还缺正式 AgentLoop。

- [ ] `include/agent_tui/app/AgentLoop.hpp`。
- [ ] 接收用户输入。
- [ ] 处理内置命令。
- [ ] 调用 SkillSelector。
- [ ] 构造 messages。
- [ ] 调用 AgentRunner。
- [ ] 把事件写入 SessionHistory / AuditLog。
- [ ] 将运行状态推给 TUI。
- [ ] 支持 cancel / interrupt。
- [ ] 支持本地 Intent Router 作为 fallback。

### P3. TUI 增强 / FTXUI 后端

当前 TUI-lite 能跑，但不够像成熟 Agent。

- [ ] `docs/28-ftxui-tui-backend-design.md`。
- [ ] 增加 `TuiState`。
- [ ] 分离 UI 状态和 Agent 执行逻辑。
- [ ] 可选接入 FTXUI。
- [ ] Chat panel。
- [ ] Tool Log panel。
- [ ] Permission Panel。
- [ ] Status Bar。
- [ ] Input Box。
- [ ] Streaming 输出区域。
- [ ] 支持上下滚动。

### P4. 权限与审计增强

现在已有基础 PermissionGate，但文件管理任务需要更强确认。

- [ ] `ApprovalPreview`：展示即将修改的文件清单。
- [ ] `FileOperationPlan`：移动/复制/删除前先生成计划。
- [ ] `dry_run=true` 默认预览。
- [ ] TUI 中支持 approve / deny / edit。
- [ ] 审计日志记录 source、target、operation、count、timestamp。
- [ ] 拒绝后把拒绝结果进入会话上下文。

### P5. SkillRuntime + kwoa-cli Skill 验证

实现通用 SkillRuntime，再用 `kwoa_cli` 作为第一个真实 Skill 验证场景。

- [ ] 加载 `skills/*/skill.yaml`。
- [ ] 加载 `skills/*/SKILL.md`。
- [ ] SkillSelector 关键词匹配。
- [ ] 按 Skill 限制可用工具集合。
- [ ] 新增 `skills/kwoa_cli/skill.yaml`。
- [ ] 新增 `skills/kwoa_cli/SKILL.md`。
- [ ] 新增 `run_kwoa_cli` 工具或通过 `run_shell` 安全封装。
- [ ] `auth status` 作为只读初始化检查。
- [ ] IM / KDocs 写操作必须走 PermissionGate。

### P6. CodingPlan Provider Skeleton

作业明确要求 WPS CodingPlan 协议，需单独 Provider。

- [ ] `CodingPlanProvider`。
- [ ] 工具调用解析。
- [ ] 流式输出。
- [ ] 超时控制。
- [ ] 基础重试。
- [ ] Provider 错误回传。

### P7. 测试补齐

现有测试还偏框架层，需要补真实使用场景。

- [x] `tests/test_file_manager_tools.cpp`。
- [ ] `tests/test_allowed_roots.cpp`。
- [ ] `tests/test_local_desktop_intents.cpp`。
- [ ] `tests/test_openai_compatible_tool_calls.cpp`。
- [ ] `tests/test_agent_loop.cpp`。
- [ ] `tests/test_permission_preview.cpp`。
- [ ] `tests/test_tui_state.cpp`。
- [ ] Windows shell 行为测试。
- [ ] 大文件 / 二进制文件跳过测试。
- [ ] Unicode 中文路径测试。

### P8. Deliverables 真实验证产物

最后阶段补可运行验证产物和截图。

- [ ] `deliverables/run-log.md`。
- [ ] `deliverables/demo-task.md`。
- [ ] `deliverables/demo-desktop-file-management.md`。
- [ ] `deliverables/screenshots/01-tui-start.png`。
- [ ] `deliverables/screenshots/02-tool-call.png`。
- [ ] `deliverables/screenshots/03-permission-confirm.png`。
- [ ] `deliverables/screenshots/04-test-run.png`。
- [ ] `deliverables/screenshots/05-final-answer.png`。
- [ ] `deliverables/screenshots/06-desktop-file-plan.png`。

## 4. 推荐下一次提交

```text
feat: route desktop file intents to file manager tools
```

建议包含：

- `docs/27-desktop-file-intents-design.md`。
- 扩展 `IntentClassifier`。
- TUI 接入 `ListPathTool` 和 `MoveFilesByExtensionTool`。
- `tests/test_local_desktop_intents.cpp`。

目标：让用户可以输入：

```text
列出桌面文件
移动图片到新文件夹
```

然后先看到 dry-run 计划，再确认执行。

## 5. 当前不要急着做

- 不要先做复杂多 Agent。
- 不要先做向量数据库。
- 不要先做 MCP。
- 不要直接开放全盘文件权限。
- 不要做永久删除。
- 不要跳过 dry-run 预览。
- 不要把桌面/下载目录操作混进 WorkspaceGuard，要单独做 AllowedRoots。

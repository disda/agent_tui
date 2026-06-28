# Pi / Codex CLI 风格工具协议演进路线

## 背景

我们对照了 `earendil-works/pi` 的 coding agent 实现，确认当前方向应该继续收敛到 **模型驱动的工具协议**，而不是恢复本地自然语言规则路由。

正确主路径：

```text
用户自然语言任务
  -> Provider 流式或非流式返回文本 / tool_calls
  -> AgentRunner 按 ToolRegistry 执行工具
  -> 权限层拦截写入、编辑、Shell 等高风险操作
  -> tool_result 回传 Provider
  -> 模型继续推理直到最终回答
```

不再走的路径：

```text
用户自然语言
  -> TUI 本地 if/else 判断意图
  -> 直接执行 list_dir / write_file / run_shell
  -> 跳过模型 tool_call
```

本地命令仍然可以存在，例如 `/status`、`/config`、`/exit`，但它们只能是显式 slash command，不能替代模型对自然语言任务的决策。

## Pi 的能力参考

Pi 的内置工具面不是规则机器人，而是一组可被模型调用的工具：

| Pi 工具 | 能力 | agent_tui 当前对应 |
| --- | --- | --- |
| `read` | 读取文本和图片，支持 offset / limit，输出截断提示 | `read_file` |
| `write` | 创建或覆盖文件，自动创建父目录 | `write_file` |
| `edit` | 精确文本替换，支持多个 edit，生成 diff / patch | `edit_file` |
| `bash` | 执行 shell，支持 timeout、流式输出、中断、截断 | `run_shell` |
| `grep` | 基于 `rg` 搜索内容，尊重 `.gitignore` | `search_text` |
| `find` | 基于 `fd` 按 glob 找文件，尊重 `.gitignore` | `glob_files` |
| `ls` | 列目录，包含 dotfiles，限制输出数量 | `list_dir` |

Pi 还把 `Current working directory` 写入 system prompt。这样模型可以直接回答“当前目录路径”，不需要靠本地规则猜测，也不一定需要先调用 `ls`。

## agent_tui 的目标工具面

短期不追求完整复制 Pi，而是先对齐最小 coding agent 工具体系：

| 目标工具 | 权限 | 近期要求 |
| --- | --- | --- |
| `workspace_info` | Auto | 返回当前 workspace 根路径；也应把路径写入 system prompt |
| `list_dir` | Auto | 输出实际 path 和目录项；后续加 limit |
| `read_file` | Auto | 后续支持 offset / limit 和截断提示 |
| `glob_files` | Auto | 后续尊重 `.gitignore`，语义对齐 `find` |
| `search_text` | Auto | 后续优先使用 `rg`，支持 glob / ignoreCase / limit |
| `write_file` | Confirm | 写入前确认；结果回传模型 |
| `edit_file` | Confirm | 后续改为多段精确 edit，返回 diff |
| `run_shell` | Confirm | 必须暴露给 Provider tools schema；支持 timeout 和输出截断 |

## 必须坚持的边界

- 自然语言任务不得由 TUI 本地规则直接执行。
- ToolRegistry 是工具能力的唯一来源。
- Provider tools schema 必须由 ToolRegistry 或同源定义生成，避免“注册了工具但模型看不到”。
- 权限拒绝、用户反馈、工具失败都必须作为 tool_result 回传模型。
- 只读工具可以自动执行；写文件、编辑文件、Shell 命令默认需要确认。
- TUI 展示层只负责可观察性，不负责替模型决定任务意图。

## 当前差距

1. OpenAI-compatible tools schema 已改为由 ToolRegistry 导出，但 schema 元数据还分散在各个 Tool 类里，后续需要补 prompt snippets / prompt guidelines。
2. `workspace_info` 已作为工具存在，system prompt 也已包含当前工作目录；后续应验证更多模型不会误用 `list_dir` 回答路径问题。
3. TUI 仍主要展示最终回答，tool_call / tool_result 可观察性不足。
4. `read_file` / `edit_file` / `search_text` / `glob_files` 的参数能力还弱于 Pi。
5. Agent loop 目前是非流式结果为主，后续需要把模型输出和工具输出做成流式事件。
6. 工具 allowlist / denylist / active tools 还未实现，后续扩展工具时需要防止所有工具默认暴露。

## 演进顺序

### P0：工具协议闭环正确

- [x] 补齐 `run_shell` tools schema。
- [x] 将当前 workspace 路径加入 system prompt。
- [x] 新增测试覆盖：自然语言“当前目录路径”不走本地规则，可由模型直接回答或通过 `workspace_info` 回答。
- [x] 新增测试覆盖：OpenAI-compatible request body 包含 ToolRegistry 注册工具。
- 用真实 Provider 完成一次写文件 + 运行命令 + 最终回答的 demo。

### P1：ToolRegistry 成为 schema 单一来源

- [x] 给 `Tool` 增加 schema 元数据。
- [x] Provider 从 ToolRegistry 生成 OpenAI-compatible tools JSON。
- [x] 删除 Provider 内部硬编码工具清单。
- [ ] 给 `Tool` 增加 prompt_snippet / prompt_guidelines 元数据。
- [ ] 对每个工具做 schema 快照测试。

### P2：工具能力对齐 Pi

- `read_file` 增加 offset / limit / truncation。
- `edit_file` 支持多个精确替换，输出 diff。
- `search_text` 迁移到 `rg` 风格参数。
- `glob_files` 迁移到 `find` 风格参数，尊重 `.gitignore`。
- `run_shell` 支持流式输出、完整输出保存和中断清理。

### P3：扩展系统

- 支持自定义工具注册。
- 支持工具 allowlist / denylist / active tools。
- 支持 tool_call hook 做权限 gate 或参数改写。
- 支持 prompt snippets / prompt guidelines 注入 system prompt。

## 验收标准

- 对自然语言输入，没有任何本地规则抢答。
- 模型能看到和 TUI 实际注册一致的工具列表。
- 真实模型能通过 tool_call 完成代码读取、写入、编辑和命令执行。
- 用户能在 TUI 中看到每个 tool_call、参数摘要、权限确认、tool_result 和最终回答。
- 所有关键路径都有自动化测试和一次真实运行记录。

# AI 协作记录：Agent Harness 基础知识总结

日期：2026-06-26

## 背景

用户询问开发一个 Agent 应该具备什么基础知识，尤其是 harness 的具体实现思路。

## 本轮结论

开发本项目不是做普通聊天窗口，而是实现一个本地 Agent Harness。

Harness 的定义：

```text
把模型、工具、权限、上下文、状态、日志、测试、安全边界串起来的运行时外壳。
```

核心循环：

```text
用户输入 -> 构造 messages -> 调用模型 -> 模型决定回答或调用工具 -> 执行工具 -> 工具结果回传模型 -> 继续推理 -> 完成
```

## 新增文档

新增：

```text
docs/05-agent-harness-basics.md
```

文档内容包括：

- Agent Harness 是什么
- LLM 消息协议
- Tool Calling
- AgentRunner
- ToolRegistry
- PermissionGate
- SessionHistory
- ContextBuilder
- Skill Runtime
- 推荐模块划分
- 第一阶段不要做的内容
- 推荐学习与实现顺序
- 最小知识地图

## README 更新

已将新文档加入 README 文档入口：

```text
Agent Harness 基础知识与实现思路 -> docs/05-agent-harness-basics.md
```

## 当前建议

下一步进入代码实现：

```text
feat: add minimal agent runner skeleton
```

优先实现：

- `Message`
- `ToolCall`
- `ToolResult`
- `ProviderResponse`
- `AgentResult`
- `AgentRunner`
- `Tool`
- `ToolRegistry`
- `Provider`
- `MockProvider`
- `tests/test_agent_runner.cpp`

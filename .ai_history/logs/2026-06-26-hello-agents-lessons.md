# AI 协作记录：Hello-Agents 启发总结

日期：2026-06-26

## 背景

用户要求讲解 Datawhale `hello-agents` / 《从零开始构建智能体》对当前 `agent_tui` 项目的启发，并要求总结到文档中提交。

## 参考结论

Hello-Agents 的价值不是提供可照搬的 Python 代码，而是提供从零构建 Agent 框架的工程路线。

核心启发：

- 不要只调用成熟框架 API，要亲手实现 Agent 核心闭环。
- 自建框架能理解 Agent 思考过程、工具调用机制和设计模式差异。
- Agent 框架要分层：core / agents / tools / config / messages。
- 第一阶段应轻量、可读、可运行，不引入重型依赖。
- 基于标准 API，Provider 层保持简单。
- 渐进式开发，每一步都可测试可运行。
- 第一阶段采用“万物皆工具”的简化抽象。
- 先实现 ReAct-like loop，再考虑 Plan-and-Solve / Reflection。

## 本轮新增文档

新增：

```text
docs/07-hello-agents-lessons.md
```

更新：

```text
README.md
```

## 对本项目的直接决策

`agent_tui` 定位为：

```text
通用 C++ Agent Harness + 可插拔 Skills
```

`kwoa-cli` 定位为：

```text
第一个真实 Skill 验证场景
```

下一步继续推进通用地基：

```text
feat: add file tools and workspace guard
```

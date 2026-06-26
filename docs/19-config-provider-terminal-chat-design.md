# Config + Provider + Terminal Chat 技术设计

## 1. 背景

当前 `agent_tui` 已经有 TUI-lite 入口和运行时 `/api` 配置命令，但还缺类似 `.codex` 的用户配置目录，也还没有把终端普通输入真正接到 Provider。

用户希望支持：

```text
类似 .codex 的目录
用户配置 API Key
配置 Provider
终端对话跑起来
```

本轮目标是把配置、Provider 工厂和终端对话链路先接上。

## 2. 配置目录

采用类似 `.codex` 的本地配置目录，但使用项目名：

```text
~/.agent_tui/config.toml
```

项目级配置：

```text
./.agent_tui/config.toml
```

加载优先级：

```text
默认值 < 用户级配置 < 项目级配置 < TUI 运行时命令
```

第一版不引入 TOML/YAML 第三方库，只解析简单 `key = value` 格式。

## 3. 配置字段

```toml
provider = "mock"
model = "mock-model"
api_base = ""
api_key_env = "OPENAI_API_KEY"
timeout_seconds = 60
max_loops = 8
```

敏感信息规则：

```text
配置文件只保存 API Key 的环境变量名，不保存真实 API Key。
TUI、日志和 status 只展示 api_key_env，不展示真实 key。
```

## 4. ConfigLoader

新增：

```text
include/agent_tui/config/Config.hpp
include/agent_tui/config/ConfigLoader.hpp
```

能力：

- 读取用户级配置。
- 读取项目级配置。
- 项目级覆盖用户级。
- 生成默认配置模板。
- 初始化用户配置目录。
- 输出脱敏 summary。

## 5. ProviderFactory

新增：

```text
include/agent_tui/llm/EchoProvider.hpp
include/agent_tui/llm/ProviderFactory.hpp
```

第一版 Provider 策略：

```text
mock / echo -> EchoProvider
openai-compatible / codingplan -> Placeholder error provider
```

原因：本轮重点是把配置和终端对话链路跑通。真实 HTTP Provider 后续单独实现。

## 6. Terminal Chat 链路

TUI 普通输入从：

```text
只记录用户输入
```

改为：

```text
用户输入
  ↓
SessionHistory 记录 user_input
  ↓
ProviderFactory 根据 config 创建 provider
  ↓
Provider.chat(messages)
  ↓
展示 assistant 回复或 provider error
  ↓
SessionHistory 记录 assistant_message / error
```

`mock` provider 会返回：

```text
mock assistant: <用户输入>
```

这让终端对话先跑起来，后续替换为 OpenAI-compatible / CodingPlan Provider 时，TUI 层不用改。

## 7. 新增命令

在现有 `/api` 基础上增加 `/config`：

```text
/config show
/config paths
/config init user
/config reload
```

含义：

- `/config show`：显示当前脱敏配置。
- `/config paths`：显示用户级和项目级配置路径。
- `/config init user`：创建 `~/.agent_tui/config.toml` 示例配置。
- `/config reload`：重新加载默认 + 用户级 + 项目级配置。

## 8. 测试计划

新增：

```text
tests/test_config_loader.cpp
```

覆盖：

- 项目级配置覆盖用户级配置。
- API Key 不暴露，只显示 api_key_env。
- 示例配置创建。
- TUI 普通输入能通过 mock provider 得到 assistant 回复。

## 9. 后续工作

本轮完成后，下一步 Provider 接入应继续做：

```text
feat: add openai compatible provider
feat: add codingplan provider skeleton
```

真实 Provider 实现要补：

- HTTP 请求。
- 工具 schema 转换。
- tool_calls 解析。
- 流式输出。
- 超时和重试。

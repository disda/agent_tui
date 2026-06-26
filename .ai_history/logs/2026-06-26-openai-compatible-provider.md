# AI 协作记录：OpenAI-compatible Provider

日期：2026-06-26

## 背景

用户要求直接接入 OpenAI-compatible Provider，并进行真实验证。

上一轮已完成配置目录和 mock terminal chat，本轮继续实现真实 OpenAI-compatible Provider。

## 本轮技术文档

新增：

```text
docs/21-openai-compatible-provider-design.md
docs/22-openai-compatible-provider-verification.md
```

## 本轮实现

新增：

```text
include/agent_tui/llm/OpenAICompatibleProvider.hpp
tests/test_openai_compatible_provider.cpp
```

更新：

```text
include/agent_tui/llm/ProviderFactory.hpp
CMakeLists.txt
README.md
```

## 当前能力

Provider 名称：

```text
openai-compatible
openai
```

支持：

- 从 `api_key_env` 读取 API Key。
- 使用 `api_base + /chat/completions` 发送请求。
- 构造 OpenAI-compatible chat request body。
- 解析普通文本 response。
- 解析 provider error response。

## 验证方式

PowerShell：

```powershell
$env:OPENAI_API_KEY="你的 key"
.\build\Debug\agent_tui.exe
```

TUI：

```text
/api provider openai-compatible
/api base https://api.openai.com/v1
/api key-env OPENAI_API_KEY
/model gpt-4.1
你好，回复一句话
```

## 当前限制

第一版 OpenAI-compatible Provider 使用系统 `curl` 作为传输层，暂不支持 streaming 和 tool_calls 解析。后续需要补 tool_calls、tools schema 和正式 HTTP 客户端。

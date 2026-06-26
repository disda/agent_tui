# OpenAI-compatible Provider 技术设计

## 1. 背景

当前 `agent_tui` 已具备配置目录、ProviderFactory 和 mock terminal chat，但用户需要直接接入 OpenAI-compatible Provider 进行真实验证。

本轮目标是实现第一版真实 HTTP Provider：

```text
provider = "openai-compatible"
```

## 2. 目标

第一版目标：

- 使用 `api_base` + `/chat/completions` 调用 OpenAI-compatible API。
- 使用 `api_key_env` 从环境变量读取 API Key。
- 不在 TUI、日志和错误输出中打印真实 API Key。
- 支持普通文本对话验证。
- 支持 request body 构造测试。
- 支持 response text 解析测试。

## 3. 非目标

第一版不做：

- 工具 schema 转换。
- tool_calls 解析执行。
- streaming。
- 多模态。
- HTTP 库依赖。
- 复杂重试。

这些后续继续增强。

## 4. 传输层选择

第一版使用系统 `curl` 命令作为 HTTP 传输层。

原因：

- 避免立即引入 libcurl / cpr / cpp-httplib 依赖。
- Windows 10+ 通常自带 `curl.exe`。
- 可以快速完成真实 provider 验证。

后续可替换为正式 HTTP 客户端库。

## 5. 安全策略

- 配置文件只保存 `api_key_env`。
- Provider 从环境变量读取真实 key。
- curl 命令不在 TUI 中展示。
- 错误信息不包含真实 key。

## 6. 配置示例

用户级：

```text
~/.agent_tui/config.toml
```

内容：

```toml
provider = "openai-compatible"
model = "gpt-4.1"
api_base = "https://api.openai.com/v1"
api_key_env = "OPENAI_API_KEY"
timeout_seconds = 60
max_loops = 8
```

PowerShell 设置环境变量：

```powershell
$env:OPENAI_API_KEY="你的 key"
```

## 7. TUI 验证命令

```text
/config reload
/api provider openai-compatible
/api base https://api.openai.com/v1
/api key-env OPENAI_API_KEY
/model gpt-4.1
你好，回复一句话
```

## 8. 下一步

本轮完成文本对话验证后，下一步继续增强：

```text
OpenAI-compatible tool_calls 解析
OpenAI-compatible tools schema 转换
CodingPlanProvider
```

# 验证说明：Local Intent Router

日期：2026-06-26

## 1. 验证目标

本次验证目标是解决“很多任务无法执行”的问题。

在真实 Provider 的 tool_calls 完整接入前，先加入本地规则型 Intent Router，对常见开发任务直接调用本地工具。

## 2. 新增内容

新增：

```text
include/agent_tui/intent/Intent.hpp
include/agent_tui/intent/IntentClassifier.hpp
tests/test_intent_classifier.cpp
```

更新：

```text
include/agent_tui/tui/TuiApp.hpp
include/agent_tui/tools/ShellTool.hpp
CMakeLists.txt
```

## 3. 当前支持的本地意图

### 目录浏览

```text
ls
dir
list
list docs
列出目录
看看目录
```

调用：

```text
list_dir
```

### 文件读取

```text
read README.md
cat README.md
读取 README.md
打开 README.md
```

调用：

```text
read_file
```

### 内容搜索

```text
search AgentRunner
grep AgentRunner
搜索 AgentRunner
查找 AgentRunner
```

调用：

```text
search_text
```

### 项目配置 / 构建 / 测试

```text
configure
配置 cmake
build
编译
构建
test
ctest
运行测试
```

调用：

```text
run_shell
```

其中 shell 类命令会先询问：

```text
Approve run_shell: <command> ? [y/N]
```

## 4. Windows Shell 支持

此前 `ShellTool` 在 Windows 下返回 unsupported。本轮增加了基于 `_popen` 的 Windows 简版实现：

```text
cmd /C "cd /d <cwd> && <command> 2>&1"
```

当前 Windows 版本支持：

- cwd 限制在 workspace 内。
- stdout/stderr 合并捕获。
- exit_code 返回。

当前 Windows 限制：

- 暂不支持 timeout 强杀。
- stderr 与 stdout 合并。

## 5. 验证示例

启动：

```powershell
.\build\Debug\agent_tui.exe
```

输入：

```text
ls
read README.md
search AgentRunner
build
```

`build` 会提示确认，输入：

```text
y
```

## 6. 下一步

Local Intent Router 是临时增强，不替代正式 AgentLoop。

后续仍需继续：

```text
OpenAI-compatible tool_calls 解析
AgentLoop 应用层
TUI PermissionPanel
SkillRuntime
```

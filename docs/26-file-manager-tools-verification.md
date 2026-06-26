# 验证说明：Safe File Manager Tools

日期：2026-06-26

## 1. 验证目标

本次验证目标是完成 P0 功能类：安全本地文件管理能力。

目标任务类型：

```text
列出桌面文件
移动图片到新文件夹
整理下载目录
```

本轮先实现底层功能类和测试，不直接开放全盘权限，也不直接把 TUI 意图路由接到真实用户 Desktop。

## 2. 新增内容

新增：

```text
include/agent_tui/filesystem/KnownPaths.hpp
include/agent_tui/filesystem/AllowedRoots.hpp
include/agent_tui/tools/FileManagerTools.hpp
tests/test_file_manager_tools.cpp
```

更新：

```text
CMakeLists.txt
```

## 3. 当前能力

### KnownPaths

支持：

```text
home
desktop
downloads
documents
pictures
```

### AllowedRoots

支持：

```text
workspace_only(workspace)
with_known_user_dirs(workspace)
resolve(alias_or_path)
is_allowed(path)
```

安全策略：

- 默认不全盘开放。
- 所有路径必须在 allowed roots 下。
- 越权路径会失败。

### FileManagerTools

新增工具：

```text
list_path
make_dir
move_file
move_files_by_extension
```

权限策略：

```text
list_path -> Auto
make_dir -> Confirm
move_file -> Confirm
move_files_by_extension -> Confirm
```

### move_files_by_extension

支持 dry-run：

```text
execute=false
```

默认不移动，只输出计划。

执行移动：

```text
execute=true
```

不覆盖目标已有文件。

## 4. 测试覆盖

新增：

```text
tests/test_file_manager_tools.cpp
```

覆盖：

- `KnownPaths::resolve_alias`。
- `AllowedRoots` 允许 workspace 内路径。
- `AllowedRoots` 拒绝越权路径。
- `list_path` 能列出文件。
- `make_dir` 能创建目录。
- `move_file` 能移动单个文件。
- `move_file` 拒绝覆盖目标。
- `move_files_by_extension` dry-run 不移动文件。
- `move_files_by_extension` execute 只移动匹配扩展名文件。
- `move_files_by_extension` 拒绝越权 target。

## 5. 验证命令

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows：

```powershell
.\scripts\build.ps1
```

## 6. 当前限制

当前还没有接到 TUI 意图：

```text
列出桌面文件
移动图片到新文件夹
```

下一步需要：

- 将 Local Intent Router 扩展到 Desktop / Downloads / Pictures。
- TUI 展示 dry-run 文件移动计划。
- 用户确认后执行 `move_files_by_extension execute=true`。
- 增加 `allowed_roots` 配置项。

## 7. 下一步建议

继续：

```text
feat: route desktop file intents to file manager tools
```

目标：让用户可以在 TUI 中输入：

```text
列出桌面文件
把桌面图片移动到 Pictures/桌面图片
```

并先看到 dry-run 计划，再确认执行。

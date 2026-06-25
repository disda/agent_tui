# AI 协作记录：受控 Shell 执行能力决策

日期：2026-06-26

## 背景

用户询问是否需要代码脚本执行能力。

结合 L2 交付要求和本项目最终 kwoa-cli Skill 验证目标，结论是：需要执行能力，但第一版只做受控 Shell 执行，不做完整代码解释器、Docker 沙箱或浏览器自动化。

## 决策

第一版执行能力定义为：

```text
run_shell
```

它用于执行：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/kwoa-cli auth status --format pretty
./build/kwoa-cli im +recent-list --count 20 --compact --format json
./build/kwoa-cli docs +read ...
```

## 安全规则

`run_shell` 必须满足：

- 走 PermissionGate。
- cwd 必须在 workspace 内。
- 支持 timeout。
- 捕获 stdout / stderr / exit_code。
- 输出长度限制。
- 用户拒绝时不执行。
- 用户拒绝结果必须作为 tool_result 回传模型。

## kwoa-cli 特殊规则

kwoa-cli 只读命令可以作为验证任务的一部分执行，例如：

```text
auth status
im +recent-list
docs +info
docs +read
```

高风险写操作必须强确认：

```text
im +messages-send --yes
im +messages-recall --yes
im +messages-forward --yes
im +reaction-send --yes
docs +markdown-insert --yes
```

## TODO 更新

已更新 `TODO.md`：

- 增加“受控 Shell 执行能力”章节。
- 明确 `run_shell` 的参数和返回。
- 明确必须先实现 PermissionGate，再进入真实 shell 执行。
- 明确下一步仍然是 `FileTools + WorkspaceGuard`，之后依次实现 PermissionGate 和 run_shell。

## 下一步

推荐提交顺序：

```text
feat: add file tools and workspace guard
feat: add permission gate
feat: add controlled shell tool
```

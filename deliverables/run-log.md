# Agent TUI Demo Run

## Build

Command:

```powershell
cmake --build build --config Debug --target agent_tui agent_tui_app_tests
```

Result: pass.

## Targeted Test

Command:

```powershell
.\build\Debug\agent_tui_app_tests.exe
```

Result: pass.

## Mock Agent Demo

Command:

```powershell
cmd /c "H:\24.agent\agent_tui\build\Debug\agent_tui.exe < input.txt > clean-demo-output.txt 2> clean-demo-error.txt"
```

Prompt:

```text
Create a hello world Python demo and run it
```

Approvals:

```text
y
y
```

Observed transcript:

```text
tool call > write_file (content=print('hello from agent_tui')
approval required > write_file (content=print('hello from agent_tui')
tool result > write_file wrote file: demo.py
tool call > run_shell (command=python demo.py, cwd=., max_output_bytes=2000, timeout_seconds=10)
approval required > run_shell (command=python demo.py, cwd=., max_output_bytes=2000, timeout_seconds=10)
tool result > run_shell exit_code: 0; stdout: hello from agent_tui
tool call > Done (final_answer=demo.py is ready and was executed)
assistant done > demo.py is ready and was executed
```

Result: pass.

## Notes

- Natural-language tasks use provider/model tool calls.
- Slash commands remain TUI and config controls.
- Assistant responses show both streaming and done boundaries.
- Tool execution is visible through tool call, approval required, tool result, and final answer transcript cells.

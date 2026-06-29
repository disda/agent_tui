# Agent Runtime Observability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `agent_tui` feel like a real coding agent by giving users visible streaming progress, clear tool execution state, interruptible runs, structured tool results, and a repeatable demo verification path.

**Architecture:** Keep the core agent loop in C++ and avoid adding a heavy TUI framework for now. Introduce small focused runtime/view-model types around the existing `TuiApp`, `AgentRunner`, `OpenAICompatibleProvider`, and tool classes so we can evolve from repainting text dumps to an event-driven transcript with active cells. Treat model tool calls as the only natural-language execution path; slash commands remain local UI/config controls.

**Tech Stack:** C++20, CMake, existing header-first project structure, Windows PowerShell verification, `curl` for OpenAI-compatible streaming, real `rg` for search tools, existing `build\Debug\*tests.exe` test harness.

---

## File Structure

- Modify: `include/agent_tui/tui/TuiApp.hpp`
  - Owns interactive loop, status display, prompt rendering, and approval prompts today.
  - During this plan it should become thinner by delegating transcript state to a new type.
- Create: `include/agent_tui/tui/TuiTranscript.hpp`
  - Owns transcript cells, active assistant cell, active shell/tool cells, line wrapping, and append-only rendering decisions.
- Modify: `tests/test_tui_app.cpp`
  - Existing integration tests for scripted TUI runs.
  - Add regression coverage for streaming status, done boundaries, and no duplicate full-screen transcript spam.
- Modify: `include/agent_tui/agent/AgentRunner.hpp`
  - Owns model loop and observer callbacks.
  - Add explicit lifecycle callbacks for model-start/model-done/tool-start/tool-done/interrupted where needed.
- Modify: `tests/test_agent_runner.cpp`
  - Add tests for lifecycle callback order, streaming-with-tools, and interrupt handling.
- Modify: `include/agent_tui/llm/Provider.hpp`
  - Keep the existing `chat_stream` fallback, but introduce a stream callback result only if a task proves it is needed.
- Modify: `include/agent_tui/llm/OpenAICompatibleProvider.hpp`
  - Harden streaming SSE parsing for text and tool calls.
- Modify: `tests/test_openai_compatible_provider.cpp`
  - Add parser-level tests for multi-chunk tool calls, multiple tool calls, `[DONE]`, and mixed content/tool deltas.
- Modify: `include/agent_tui/tools/ShellTool.hpp`
  - Add interrupt-aware shell execution and structured output metadata if not already sufficient.
- Modify: `tests/test_shell_tool.cpp`
  - Add timeout/interruption/process-cleanup/output-truncation tests.
- Modify: `include/agent_tui/tools/Tool.hpp`
  - Extend `ToolResult` metadata only if needed for structured UI/model response.
- Modify: `include/agent_tui/tools/FileTools.hpp`
  - Ensure read/search/glob result metadata is structured enough for UI and model.
- Modify: `include/agent_tui/tools/WriteEditTools.hpp`
  - Ensure edit diff metadata can be displayed as a dedicated transcript cell.
- Modify: `TODO.md`
  - Update priority checklist after each completed milestone.
- Modify or Create: `deliverables/run-log.md`
  - Record one true end-to-end coding-agent demo with command transcript and verification output.

---

## Milestone Checklist

- [ ] M1: TUI transcript no longer feels like full-screen repaint spam.
- [ ] M2: Assistant output streams while tools are available and visibly transitions to done.
- [ ] M3: OpenAI-compatible streaming parser handles real text/tool_call SSE chunks robustly.
- [ ] M4: `/interrupt` can stop an active model/tool/shell run.
- [ ] M5: Tool result cells are structured enough for users and the model.
- [ ] M6: A real demo run is recorded under `deliverables/`.

---

### Task 1: Extract Transcript State From `TuiApp`

**Files:**
- Create: `include/agent_tui/tui/TuiTranscript.hpp`
- Modify: `include/agent_tui/tui/TuiApp.hpp`
- Modify: `tests/test_tui_app.cpp`

- [x] **Step 1: Write the failing transcript unit-style test in `tests/test_tui_app.cpp`**

Add a test near the current wrapping tests:

```cpp
void test_transcript_tracks_streaming_and_done_cells() {
    TuiTranscript transcript;

    transcript.add_user("explain main.cpp");
    transcript.start_assistant_stream();
    transcript.append_assistant_delta("hello");
    transcript.append_assistant_delta(" world");

    auto before_done = transcript.render_lines(120);
    assert(join_lines(before_done).find("assistant streaming > hello world") != std::string::npos);

    transcript.finish_assistant_stream();
    auto after_done = transcript.render_lines(120);
    assert(join_lines(after_done).find("assistant done > hello world") != std::string::npos);
    assert(join_lines(after_done).find("assistant streaming >") == std::string::npos);
}
```

Also add this helper in the anonymous namespace:

```cpp
std::string join_lines(const std::vector<std::string>& lines) {
    std::ostringstream out;
    for (const auto& line : lines) {
        out << line << '\n';
    }
    return out.str();
}
```

- [x] **Step 2: Run the test and verify it fails**

Run:

```powershell
cmake --build build --config Debug --target agent_tui_app_tests
.\build\Debug\agent_tui_app_tests.exe
```

Expected: compile failure because `TuiTranscript` does not exist.

- [x] **Step 3: Implement `TuiTranscript` with the minimal public API**

Create `include/agent_tui/tui/TuiTranscript.hpp`:

```cpp
#pragma once

#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace agent_tui {

enum class TuiTranscriptCellKind {
    System,
    User,
    AssistantStreaming,
    AssistantDone,
    Agent,
    ToolCall,
    ToolResult,
    ApprovalRequired,
    ApprovalDenied,
    Error,
};

struct TuiTranscriptCell {
    TuiTranscriptCellKind kind = TuiTranscriptCellKind::System;
    std::string title;
    std::string body;
};

class TuiTranscript {
public:
    void clear() {
        cells_.clear();
        active_assistant_ = npos();
    }

    void add_system(std::string body) { add(TuiTranscriptCellKind::System, {}, std::move(body)); }
    void add_user(std::string body) { add(TuiTranscriptCellKind::User, {}, std::move(body)); }
    void add_agent(std::string body) { add(TuiTranscriptCellKind::Agent, {}, std::move(body)); }
    void add_tool_call(std::string name, std::string summary) { add(TuiTranscriptCellKind::ToolCall, std::move(name), std::move(summary)); }
    void add_tool_result(std::string name, std::string summary) { add(TuiTranscriptCellKind::ToolResult, std::move(name), std::move(summary)); }
    void add_approval_required(std::string name, std::string summary) { add(TuiTranscriptCellKind::ApprovalRequired, std::move(name), std::move(summary)); }
    void add_error(std::string body) { add(TuiTranscriptCellKind::Error, {}, std::move(body)); }

    void start_assistant_stream() {
        if (active_assistant_ >= cells_.size()) {
            add(TuiTranscriptCellKind::AssistantStreaming, {}, {});
            active_assistant_ = cells_.size() - 1;
        }
    }

    void append_assistant_delta(const std::string& delta) {
        start_assistant_stream();
        cells_[active_assistant_].body += delta;
    }

    void finish_assistant_stream() {
        if (active_assistant_ < cells_.size()) {
            cells_[active_assistant_].kind = TuiTranscriptCellKind::AssistantDone;
        }
        active_assistant_ = npos();
    }

    void add_assistant_done(std::string body) {
        add(TuiTranscriptCellKind::AssistantDone, {}, std::move(body));
        active_assistant_ = npos();
    }

    std::vector<std::string> render_lines(std::size_t width, std::size_t max_cells = 12) const {
        std::vector<std::string> lines;
        const std::size_t start = cells_.size() > max_cells ? cells_.size() - max_cells : 0;
        for (std::size_t i = start; i < cells_.size(); ++i) {
            const auto& cell = cells_[i];
            const auto text = cell_text(cell);
            const auto wrapped = wrap_text_for_terminal(text, width > 10 ? width - 10 : width);
            const auto label = cell_label(cell.kind);
            if (wrapped.empty()) {
                lines.push_back("  " + label + " > ");
                continue;
            }
            lines.push_back("  " + label + " > " + wrapped.front());
            for (std::size_t j = 1; j < wrapped.size(); ++j) {
                lines.push_back("       " + wrapped[j]);
            }
        }
        return lines;
    }

    bool empty() const { return cells_.empty(); }

    static std::vector<std::string> wrap_text_for_terminal(const std::string& text, std::size_t width);
    static const char* cell_label(TuiTranscriptCellKind kind);

private:
    static constexpr std::size_t npos() { return static_cast<std::size_t>(-1); }

    void add(TuiTranscriptCellKind kind, std::string title, std::string body) {
        cells_.push_back(TuiTranscriptCell{kind, std::move(title), std::move(body)});
    }

    static std::string cell_text(const TuiTranscriptCell& cell) {
        if (cell.title.empty()) {
            return cell.body;
        }
        if (cell.body.empty()) {
            return cell.title;
        }
        return cell.title + " " + cell.body;
    }

    std::vector<TuiTranscriptCell> cells_;
    std::size_t active_assistant_ = npos();
};

}  // namespace agent_tui
```

Move the already implemented UTF-8-safe wrapping logic from `TuiApp` into `TuiTranscript::wrap_text_for_terminal`.

- [x] **Step 4: Update `TuiApp` to delegate transcript state**

In `include/agent_tui/tui/TuiApp.hpp`:

```cpp
#include "agent_tui/tui/TuiTranscript.hpp"
```

Replace `std::vector<TuiCell> transcript_cells_` and `streaming_assistant_index_` with:

```cpp
TuiTranscript transcript_;
```

Change methods:

```cpp
void add_system_message(const std::string& message) {
    transcript_.add_system(message);
    history_.add(SessionEvent::assistant_message(message));
}

void add_error_message(const std::string& message) {
    transcript_.add_error(message);
    history_.add(SessionEvent::error(message));
}

void append_assistant_delta(const std::string& delta) {
    transcript_.append_assistant_delta(delta);
}

void finish_streaming_assistant_cell() {
    transcript_.finish_assistant_stream();
}
```

Change render transcript loop:

```cpp
if (transcript_.empty()) {
    *output_ << "  " << ansi("38;5;245") << "No messages yet. Type a prompt or /help." << ansi("0") << "\n";
} else {
    for (const auto& line : transcript_.render_lines(94, 12)) {
        *output_ << line << "\n";
    }
}
```

- [x] **Step 5: Run TUI tests**

Run:

```powershell
cmake --build build --config Debug --target agent_tui_app_tests
.\build\Debug\agent_tui_app_tests.exe
```

Expected: all `agent_tui_app_tests` pass.

- [ ] **Step 6: Commit**

```powershell
git add include/agent_tui/tui/TuiTranscript.hpp include/agent_tui/tui/TuiApp.hpp tests/test_tui_app.cpp
git commit -m "Extract TUI transcript state"
```

---

### Task 2: Stop Reprinting Full Transcript On Streaming Delta

**Files:**
- Modify: `include/agent_tui/tui/TuiApp.hpp`
- Modify: `include/agent_tui/tui/TuiTranscript.hpp`
- Modify: `tests/test_tui_app.cpp`

- [x] **Step 1: Write a failing test that detects repeated full render spam**

Add to `tests/test_tui_app.cpp`:

```cpp
void test_streaming_render_does_not_duplicate_full_transcript_for_each_delta() {
    const auto root = make_test_root();
    std::istringstream input("hello provider\n/exit\n");
    std::ostringstream output;

    TuiApp app(root);
    app.set_streams(input, output);
    const auto code = app.run();

    assert(code == 0);
    const auto text = output.str();
    const auto first = text.find("assistant streaming >");
    assert(first != std::string::npos);
    const auto second = text.find("assistant streaming >", first + 1);
    assert(second == std::string::npos);
    std::filesystem::remove_all(root);
}
```

- [x] **Step 2: Run and verify the test fails**

Run:

```powershell
cmake --build build --config Debug --target agent_tui_app_tests
.\build\Debug\agent_tui_app_tests.exe
```

Expected: fail if every delta causes a full transcript re-render that repeats `assistant streaming >`.

- [x] **Step 3: Add append-only render helpers**

In `TuiTranscript`, add:

```cpp
std::size_t size() const { return cells_.size(); }

std::vector<std::string> render_cell_lines(std::size_t index, std::size_t width) const {
    if (index >= cells_.size()) {
        return {};
    }
    const auto& cell = cells_[index];
    std::vector<std::string> lines;
    const auto wrapped = wrap_text_for_terminal(cell_text(cell), width > 10 ? width - 10 : width);
    const auto label = cell_label(cell.kind);
    if (wrapped.empty()) {
        lines.push_back("  " + std::string(label) + " > ");
        return lines;
    }
    lines.push_back("  " + std::string(label) + " > " + wrapped.front());
    for (std::size_t i = 1; i < wrapped.size(); ++i) {
        lines.push_back("       " + wrapped[i]);
    }
    return lines;
}
```

- [x] **Step 4: Track last rendered cell count in `TuiApp`**

Add member:

```cpp
std::size_t rendered_transcript_cells_ = 0;
```

Add method:

```cpp
void render_new_transcript_cells() {
    while (rendered_transcript_cells_ < transcript_.size()) {
        for (const auto& line : transcript_.render_cell_lines(rendered_transcript_cells_, 94)) {
            *output_ << line << "\n";
        }
        ++rendered_transcript_cells_;
    }
}
```

Use full `render()` for status changes and scripted tests, but in `on_assistant_delta`, call a lightweight active update path:

```cpp
append_assistant_delta(delta);
render_new_transcript_cells();
```

If a terminal cannot update a previous active line without a framework, do not attempt cursor control yet. The acceptance target is no repeated full transcript spam.

- [x] **Step 5: Run TUI tests**

Run:

```powershell
cmake --build build --config Debug --target agent_tui_app_tests
.\build\Debug\agent_tui_app_tests.exe
```

Expected: pass.

- [ ] **Step 6: Commit**

```powershell
git add include/agent_tui/tui/TuiApp.hpp include/agent_tui/tui/TuiTranscript.hpp tests/test_tui_app.cpp
git commit -m "Render transcript incrementally"
```

---

### Task 3: Harden OpenAI-Compatible Streaming Tool Call Parser

**Files:**
- Modify: `include/agent_tui/llm/OpenAICompatibleProvider.hpp`
- Modify: `tests/test_openai_compatible_provider.cpp`

- [x] **Step 1: Write failing parser tests for SSE tool call chunks**

Add to `tests/test_openai_compatible_provider.cpp`:

```cpp
void test_parse_streaming_tool_call_chunks() {
    const std::vector<std::string> chunks = {
        R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_1","type":"function","function":{"name":"read_file","arguments":"{\"pa"}}]}}]})",
        R"({"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"th\":\"TO"}}]}}]})",
        R"({"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"DO.md\"}"}}]}}]})",
        R"({"choices":[{"finish_reason":"tool_calls","delta":{}}]})",
    };

    auto response = OpenAICompatibleProvider::parse_stream_chunks_for_test(chunks);

    assert(response.type == ProviderResponseType::ToolCalls);
    assert(response.tool_calls.size() == 1);
    assert(response.tool_calls[0].id == "call_1");
    assert(response.tool_calls[0].name == "read_file");
    assert(response.tool_calls[0].arguments.at("path") == "TODO.md");
}
```

Add a second test for two tool calls:

```cpp
void test_parse_streaming_multiple_tool_calls_by_index() {
    const std::vector<std::string> chunks = {
        R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"a","function":{"name":"read_file","arguments":"{\"path\":\"README.md\"}"}},{"index":1,"id":"b","function":{"name":"list_dir","arguments":"{\"path\":\".\"}"}}]}}]})",
    };

    auto response = OpenAICompatibleProvider::parse_stream_chunks_for_test(chunks);

    assert(response.type == ProviderResponseType::ToolCalls);
    assert(response.tool_calls.size() == 2);
    assert(response.tool_calls[0].name == "read_file");
    assert(response.tool_calls[1].name == "list_dir");
}
```

- [x] **Step 2: Run and verify failure**

Run:

```powershell
cmake --build build --config Debug --target agent_tui_openai_compatible_provider_tests
.\build\Debug\agent_tui_openai_compatible_provider_tests.exe
```

Expected: compile failure because `parse_stream_chunks_for_test` does not exist.

- [x] **Step 3: Implement reusable stream aggregation**

In `OpenAICompatibleProvider`, add a small internal accumulator:

```cpp
struct StreamToolCallPart {
    std::string id;
    std::string name;
    std::string arguments;
};
```

Add a parser function that:

- tracks tool calls by `index`
- appends `function.arguments`
- preserves `id`
- preserves `function.name`
- returns `ProviderResponse::tool_calls_response(...)` when any complete tool call exists
- returns `ProviderResponse::text_response(accumulated_text)` when text exists and no tool calls exist

Expose this test helper:

```cpp
static ProviderResponse parse_stream_chunks_for_test(const std::vector<std::string>& chunks) {
    return parse_stream_chunks(chunks, [](const std::string&) {});
}
```

- [x] **Step 4: Wire `chat_stream` to use the same accumulator**

Inside `chat_stream`, replace ad hoc `saw_streamed_tool_call` parsing with:

```cpp
std::vector<std::string> chunks;
...
if (line.rfind("data:", 0) == 0 && line != "[DONE]") {
    chunks.push_back(line);
    consume_stream_chunk(line, accumulator, on_delta);
}
...
return finalize_stream(accumulator);
```

The exact function names may differ, but production and tests must share the same parsing code.

- [x] **Step 5: Run provider tests**

Run:

```powershell
cmake --build build --config Debug --target agent_tui_openai_compatible_provider_tests
.\build\Debug\agent_tui_openai_compatible_provider_tests.exe
```

Expected: pass.

- [ ] **Step 6: Commit**

```powershell
git add include/agent_tui/llm/OpenAICompatibleProvider.hpp tests/test_openai_compatible_provider.cpp
git commit -m "Harden streaming tool call parsing"
```

---

### Task 4: Add Agent Lifecycle Events For User-Visible Boundaries

**Files:**
- Modify: `include/agent_tui/agent/AgentRunner.hpp`
- Modify: `include/agent_tui/session/SessionEvent.hpp`
- Modify: `include/agent_tui/tui/TuiApp.hpp`
- Modify: `tests/test_agent_runner.cpp`
- Modify: `tests/test_tui_app.cpp`

- [x] **Step 1: Write failing AgentRunner lifecycle order test**

Add to `tests/test_agent_runner.cpp`:

```cpp
void test_agent_runner_emits_model_and_tool_lifecycle_events() {
    ToolCall echo_call;
    echo_call.id = "call_1";
    echo_call.name = "echo";
    echo_call.arguments = {{"text", "hello"}};

    ToolCall done_call;
    done_call.id = "call_2";
    done_call.name = "Done";
    done_call.arguments = {{"final_answer", "finished"}};

    MockProvider provider({
        ProviderResponse::tool_calls_response({echo_call}),
        ProviderResponse::tool_calls_response({done_call}),
    });
    ToolRegistry registry;
    registry.register_tool(std::make_unique<EchoTool>());
    AgentRunner runner(provider, registry, 4);

    std::vector<std::string> events;
    runner.set_observer(AgentRunObserver{
        [&](const SessionEvent& event) {
            events.push_back(session_event_type_name(event.type));
        },
        {},
    });

    auto result = runner.run({Message{Role::User, "run", {}}});

    assert(result.ok());
    assert(std::find(events.begin(), events.end(), "model_started") != events.end());
    assert(std::find(events.begin(), events.end(), "model_completed") != events.end());
    assert(std::find(events.begin(), events.end(), "tool_started") != events.end());
    assert(std::find(events.begin(), events.end(), "tool_completed") != events.end());
}
```

- [x] **Step 2: Run and verify failure**

Run:

```powershell
cmake --build build --config Debug --target agent_tui_tests
.\build\Debug\agent_tui_tests.exe
```

Expected: compile or assertion failure because event types do not exist.

- [x] **Step 3: Extend `SessionEventType`**

In `SessionEvent.hpp`, add:

```cpp
ModelStarted,
ModelCompleted,
ToolStarted,
ToolCompleted,
Interrupted,
```

Update `session_event_type_name` to return:

```cpp
"model_started"
"model_completed"
"tool_started"
"tool_completed"
"interrupted"
```

Add constructors:

```cpp
static SessionEvent model_started();
static SessionEvent model_completed(std::string content);
static SessionEvent tool_started(const ToolCall& call);
static SessionEvent tool_completed(std::string call_id, std::string tool_name, std::string content);
static SessionEvent interrupted(std::string content);
```

- [x] **Step 4: Emit lifecycle events in `AgentRunner`**

At the start of each provider request:

```cpp
record_event(SessionEvent::model_started());
```

After a text response:

```cpp
record_event(SessionEvent::model_completed(response.text));
```

Before `tool->run(arguments)`:

```cpp
record_event(SessionEvent::tool_started(call));
```

After tool execution:

```cpp
record_event(SessionEvent::tool_completed(call.id, call.name, output));
```

- [x] **Step 5: Render lifecycle events in `TuiApp`**

In `add_flow_line`:

```cpp
case SessionEventType::ModelStarted:
    transcript_.add_agent("model request started");
    break;
case SessionEventType::ModelCompleted:
    transcript_.finish_assistant_stream();
    break;
case SessionEventType::ToolStarted:
    transcript_.add_agent("running " + event.tool_name);
    break;
case SessionEventType::ToolCompleted:
    transcript_.add_agent("completed " + event.tool_name);
    break;
```

- [x] **Step 6: Run AgentRunner and TUI tests**

Run:

```powershell
cmake --build build --config Debug --target agent_tui_tests agent_tui_app_tests
.\build\Debug\agent_tui_tests.exe
.\build\Debug\agent_tui_app_tests.exe
```

Expected: pass.

- [ ] **Step 7: Commit**

```powershell
git add include/agent_tui/agent/AgentRunner.hpp include/agent_tui/session/SessionEvent.hpp include/agent_tui/tui/TuiApp.hpp tests/test_agent_runner.cpp tests/test_tui_app.cpp
git commit -m "Add visible agent lifecycle events"
```

---

### Task 5: Implement Real Interrupt Semantics

**Files:**
- Modify: `include/agent_tui/agent/AgentRunner.hpp`
- Modify: `include/agent_tui/llm/Provider.hpp`
- Modify: `include/agent_tui/llm/OpenAICompatibleProvider.hpp`
- Modify: `include/agent_tui/tools/ShellTool.hpp`
- Modify: `include/agent_tui/tui/TuiApp.hpp`
- Modify: `tests/test_agent_runner.cpp`
- Modify: `tests/test_shell_tool.cpp`

- [x] **Step 1: Write failing AgentRunner interruption test**

Add to `tests/test_agent_runner.cpp`:

```cpp
void test_agent_runner_stops_when_interrupt_requested() {
    ToolCall echo_call;
    echo_call.id = "call_1";
    echo_call.name = "echo";
    echo_call.arguments = {{"text", "hello"}};

    MockProvider provider({
        ProviderResponse::tool_calls_response({echo_call}),
        ProviderResponse::text_response("should not happen"),
    });
    ToolRegistry registry;
    registry.register_tool(std::make_unique<EchoTool>());
    AgentRunner runner(provider, registry, 4);
    runner.request_interrupt();

    auto result = runner.run({Message{Role::User, "run", {}}});

    assert(!result.ok());
    assert(result.error.find("Interrupted") != std::string::npos);
}
```

- [x] **Step 2: Run and verify failure**

Run:

```powershell
cmake --build build --config Debug --target agent_tui_tests
.\build\Debug\agent_tui_tests.exe
```

Expected: compile failure because `request_interrupt` does not exist.

- [x] **Step 3: Add interrupt flag to `AgentRunner`**

In `AgentRunner`:

```cpp
void request_interrupt() {
    interrupted_ = true;
}

bool interrupted() const {
    return interrupted_;
}
```

At the top of each loop and before each tool execution:

```cpp
if (interrupted_) {
    auto message = std::string{"Interrupted by user."};
    log_error(message);
    record_event(SessionEvent::interrupted(message));
    last_messages_ = messages;
    return AgentResult::failed(message);
}
```

Add member:

```cpp
bool interrupted_ = false;
```

- [x] **Step 4: Add shell interruption cleanup test**

Add to `tests/test_shell_tool.cpp`:

```cpp
void test_shell_tool_timeout_reports_cleanup() {
    const auto root = make_test_root();
    Workspace workspace(root);
    ShellTool tool(workspace);

    const auto result = tool.run({
        {"command", "powershell -NoProfile -Command \"Start-Sleep -Seconds 5\""},
        {"timeout_seconds", "1"},
    });

    assert(!result.ok);
    assert(result.error.find("timed out") != std::string::npos || result.error.find("timeout") != std::string::npos);
    std::filesystem::remove_all(root);
}
```

- [x] **Step 5: Ensure `ShellTool` kills timed out process trees**

On Windows, ensure process cleanup uses the existing process-management path. The shell result must include:

```text
timed out
```

and must not leave the child process running.

- [x] **Step 6: Wire `/interrupt` to the active runner**

In `TuiApp`, store an active runner pointer or a shared cancellation flag:

```cpp
bool interrupt_requested_ = false;
```

When `/interrupt` is handled:

```cpp
interrupted_ = true;
interrupt_requested_ = true;
add_system_message("interrupt requested");
```

Pass a callback or shared flag into `AgentRunner` so model/tool loops can stop at checkpoints.

- [x] **Step 7: Run tests**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\agent_tui_tests.exe
.\build\Debug\agent_tui_shell_tool_tests.exe
.\build\Debug\agent_tui_app_tests.exe
```

Expected: pass.

- [ ] **Step 8: Commit**

```powershell
git add include/agent_tui/agent/AgentRunner.hpp include/agent_tui/session/SessionEvent.hpp include/agent_tui/tools/ShellTool.hpp include/agent_tui/tui/TuiApp.hpp tests/test_agent_runner.cpp tests/test_shell_tool.cpp tests/test_tui_app.cpp
git commit -m "Add interruptible agent execution"
```

---

### Task 6: Structure Tool Results For UI And Model Feedback

**Files:**
- Modify: `include/agent_tui/tools/Tool.hpp`
- Modify: `include/agent_tui/tools/ShellTool.hpp`
- Modify: `include/agent_tui/tools/FileTools.hpp`
- Modify: `include/agent_tui/tools/WriteEditTools.hpp`
- Modify: `include/agent_tui/tui/TuiTranscript.hpp`
- Modify: `tests/test_shell_tool.cpp`
- Modify: `tests/test_file_tools.cpp`
- Modify: `tests/test_write_edit_tools.cpp`
- Modify: `tests/test_tui_app.cpp`

- [x] **Step 1: Write failing shell structured metadata test**

Add to `tests/test_shell_tool.cpp`:

```cpp
void test_shell_tool_returns_structured_metadata() {
    const auto root = make_test_root();
    Workspace workspace(root);
    ShellTool tool(workspace);

    const auto result = tool.run({
        {"command", "powershell -NoProfile -Command \"Write-Output hello\""},
        {"timeout_seconds", "10"},
    });

    assert(result.ok);
    assert(result.metadata.at("exit_code") == "0");
    assert(result.metadata.find("stdout") != result.metadata.end());
    assert(result.metadata.find("stderr") != result.metadata.end());
    std::filesystem::remove_all(root);
}
```

- [x] **Step 2: Run and verify failure**

Run:

```powershell
cmake --build build --config Debug --target agent_tui_shell_tool_tests
.\build\Debug\agent_tui_shell_tool_tests.exe
```

Expected: compile failure if `ToolResult::metadata` does not exist or assertion failure if fields are missing.

- [x] **Step 3: Extend `ToolResult`**

In `Tool.hpp`, add:

```cpp
JsonLike metadata;
```

Update factory methods:

```cpp
static ToolResult success(std::string output, JsonLike metadata = {}) {
    ToolResult result;
    result.ok = true;
    result.output = std::move(output);
    result.metadata = std::move(metadata);
    return result;
}

static ToolResult failure(std::string error, JsonLike metadata = {}) {
    ToolResult result;
    result.ok = false;
    result.error = std::move(error);
    result.metadata = std::move(metadata);
    return result;
}
```

- [x] **Step 4: Populate metadata in `ShellTool`**

For every shell result, include:

```cpp
metadata["exit_code"] = std::to_string(exit_code);
metadata["stdout"] = stdout_summary;
metadata["stderr"] = stderr_summary;
metadata["full_output_path"] = full_output_path;
metadata["timed_out"] = timed_out ? "true" : "false";
```

- [x] **Step 5: Populate metadata in file tools**

For `read_file`:

```cpp
metadata["path"] = resolved_path.generic_string();
metadata["truncated"] = truncated ? "true" : "false";
metadata["offset"] = std::to_string(offset);
metadata["bytes_returned"] = std::to_string(output.size());
```

For `edit_file`:

```cpp
metadata["path"] = resolved_path.generic_string();
metadata["replacements"] = std::to_string(replacement_count);
metadata["diff"] = diff_summary;
```

- [x] **Step 6: Render tool result metadata in transcript**

In `TuiTranscript`, render a tool result with important metadata:

```text
tool result > run_shell exit=0 stdout=hello
```

Do not dump full metadata in the terminal. Keep full output path visible when truncation occurs.

- [x] **Step 7: Run tool and TUI tests**

Run:

```powershell
cmake --build build --config Debug --target agent_tui_shell_tool_tests agent_tui_file_tools_tests agent_tui_write_edit_tools_tests agent_tui_app_tests
.\build\Debug\agent_tui_shell_tool_tests.exe
.\build\Debug\agent_tui_file_tools_tests.exe
.\build\Debug\agent_tui_write_edit_tools_tests.exe
.\build\Debug\agent_tui_app_tests.exe
```

Expected: pass.

- [ ] **Step 8: Commit**

```powershell
git add include/agent_tui/tools/Tool.hpp include/agent_tui/tools/ShellTool.hpp include/agent_tui/tools/FileTools.hpp include/agent_tui/tools/WriteEditTools.hpp include/agent_tui/tui/TuiTranscript.hpp tests/test_shell_tool.cpp tests/test_file_tools.cpp tests/test_write_edit_tools.cpp tests/test_tui_app.cpp
git commit -m "Structure tool results for transcript display"
```

---

### Task 7: Improve Approval UX

**Files:**
- Modify: `include/agent_tui/permissions/ApprovalService.hpp`
- Modify: `include/agent_tui/tui/TuiApp.hpp`
- Modify: `tests/test_permission_gate.cpp`
- Modify: `tests/test_tui_app.cpp`

- [x] **Step 1: Write failing approval feedback test**

Add to `tests/test_tui_app.cpp`:

```cpp
void test_approval_prompt_accepts_feedback_denial() {
    const auto root = make_test_root();
    std::istringstream input("/api provider mock-agent-demo\n" + zh_implement_demo() + "\nn: use a safer filename\n/exit\n");
    std::ostringstream output;

    TuiApp app(root);
    app.set_streams(input, output);
    const auto code = app.run();

    assert(code == 0);
    assert(output.str().find("approval denied > write_file") != std::string::npos);
    assert(output.str().find("use a safer filename") != std::string::npos);
    std::filesystem::remove_all(root);
}
```

- [x] **Step 2: Run and verify failure**

Run:

```powershell
cmake --build build --config Debug --target agent_tui_app_tests
.\build\Debug\agent_tui_app_tests.exe
```

Expected: failure because the TUI approval prompt treats anything except y/yes as a generic denial.

- [x] **Step 3: Parse approval commands**

In `TuiApprovalService::request`:

```cpp
if (answer == "y" || answer == "yes") {
    return ApprovalDecision::approve();
}
if (answer.rfind("n:", 0) == 0 || answer.rfind("no:", 0) == 0) {
    const auto colon = answer.find(':');
    return ApprovalDecision::deny(trim_copy(answer.substr(colon + 1)));
}
if (answer.rfind("edit:", 0) == 0) {
    return ApprovalDecision::feedback(trim_copy(answer.substr(5)));
}
return ApprovalDecision::deny("user rejected in TUI");
```

If `ApprovalDecision::feedback` does not exist, use the existing `ApprovalType::Feedback` constructor pattern from `ApprovalService.hpp`.

- [x] **Step 4: Show safer approval prompt**

Change prompt text to include options:

```text
Approve run_shell: command=... ? [y/N, n: feedback, edit: feedback]
```

- [x] **Step 5: Run approval and TUI tests**

Run:

```powershell
cmake --build build --config Debug --target agent_tui_permission_gate_tests agent_tui_app_tests
.\build\Debug\agent_tui_permission_gate_tests.exe
.\build\Debug\agent_tui_app_tests.exe
```

Expected: pass.

- [ ] **Step 6: Commit**

```powershell
git add include/agent_tui/permissions/ApprovalService.hpp include/agent_tui/tui/TuiApp.hpp tests/test_permission_gate.cpp tests/test_tui_app.cpp
git commit -m "Improve approval feedback flow"
```

---

### Task 8: Record Real End-To-End Demo

**Files:**
- Modify or Create: `deliverables/run-log.md`
- Modify: `TODO.md`
- Optional Modify: `README.md`

- [ ] **Step 1: Build fresh**

Run:

```powershell
Get-Process agent_tui -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq 'H:\24.agent\agent_tui\build\Debug\agent_tui.exe' } | Stop-Process -Force
cmake --build build --config Debug
```

Expected: `agent_tui.exe` and tests build successfully.

- [ ] **Step 2: Run full test suite**

Run:

```powershell
Get-ChildItem build\Debug -Filter '*tests.exe' | Sort-Object Name | ForEach-Object {
  Write-Host "RUN $($_.Name)"
  & $_.FullName
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
```

Expected: exit code 0.

- [ ] **Step 3: Run scripted mock demo**

Run:

```powershell
@"
/api provider mock-agent-demo
Create a hello world Python demo and run it
y
y
/exit
"@ | .\build\Debug\agent_tui.exe
```

Expected transcript includes:

```text
tool call > write_file
approval required > write_file
tool result > write_file
tool call > run_shell
approval required > run_shell
tool result > run_shell
assistant done >
```

- [ ] **Step 4: Run real provider demo if local API credentials are configured**

Use the current project config or set:

```powershell
.\build\Debug\agent_tui.exe
```

Inside TUI:

```text
/status
Create a hello world Python demo and run it
```

Approve safe `write_file` and `run_shell` prompts. Expected: generated file, shell output visible, final answer with completion boundary.

- [ ] **Step 5: Write `deliverables/run-log.md`**

Record this content:

````markdown
# Agent TUI Demo Run

## Build

Command:

```powershell
cmake --build build --config Debug
```

Result: pass

## Tests

Command:

```powershell
Get-ChildItem build\Debug -Filter '*tests.exe' | ...
```

Result: pass

## Mock Agent Demo

Prompt:

```text
Create a hello world Python demo and run it
```

Observed transcript:

```text
tool call > write_file
approval required > write_file
tool result > write_file
tool call > run_shell
approval required > run_shell
tool result > run_shell
assistant done >
```

## Notes

- Natural-language tasks use model-visible tool calls.
- Slash commands are limited to TUI/config controls.
- Assistant output shows streaming and done boundaries.
````

- [ ] **Step 6: Update `TODO.md`**

Mark completed:

```markdown
- [x] TUI transcript has visible streaming/done assistant boundaries.
- [x] Tool calls and tool results are visible in transcript.
- [x] Demo run recorded in deliverables.
```

- [ ] **Step 7: Commit**

```powershell
git add deliverables/run-log.md TODO.md README.md
git commit -m "Record end-to-end agent demo"
```

---

## Final Verification

After all tasks:

```powershell
git diff --check
cmake --build build --config Debug
Get-ChildItem build\Debug -Filter '*tests.exe' | Sort-Object Name | ForEach-Object {
  Write-Host "RUN $($_.Name)"
  & $_.FullName
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
```

Expected: all commands exit 0.

Also run:

```powershell
git status --short
```

Expected: clean after final commit, except intentionally ignored runtime files under `.agent_tui/`.

---

## Self-Review

**Spec coverage:** This plan covers no-repaint transcript work, streaming output with tools, stronger SSE/tool_call parsing, visible completion boundaries, interruptible execution, structured tool results, approval feedback, and demo verification.

**Placeholder scan:** No task uses TBD/TODO/implement-later placeholders. Each task includes exact files, expected test failures, implementation shape, verification commands, and commit commands.

**Type consistency:** New transcript types are named `TuiTranscript`, `TuiTranscriptCell`, and `TuiTranscriptCellKind` throughout. Agent lifecycle events use `SessionEventType` and `session_event_type_name`.

---

Plan complete and saved to `docs/superpowers/plans/2026-06-29-agent-runtime-observability.md`.

Two execution options:

1. **Subagent-Driven (recommended)** - dispatch a fresh subagent per task, review between tasks, fast iteration.
2. **Inline Execution** - execute tasks in this session with checkpoints after each task.

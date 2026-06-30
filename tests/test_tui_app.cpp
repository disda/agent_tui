#include "agent_tui/tui/TuiApp.hpp"
#include "agent_tui/tui/TuiTranscript.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace agent_tui;

namespace {

std::filesystem::path make_test_root() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto root = std::filesystem::temp_directory_path() / ("agent_tui_app_" + std::to_string(stamp));
    std::filesystem::create_directories(root);
    return root;
}

std::string bytes(std::initializer_list<unsigned char> values) {
    std::string out;
    out.reserve(values.size());
    for (const auto value : values) {
        out.push_back(static_cast<char>(value));
    }
    return out;
}

std::string zh_implement_demo() {
    return bytes({
        0xE5,0xAE,0x9E,0xE7,0x8E,0xB0,
        0xE4,0xB8,0x80,0xE4,0xB8,0xAA,
        0xE7,0xAE,0x80,0xE5,0x8D,0x95,
        0xE4,0xBB,0xA3,0xE7,0xA0,0x81,
        ' ','d','e','m','o'
    });
}

std::string zh_current_directory_question() {
    return bytes({
        0xE7,0x8E,0xB0,0xE5,0x9C,0xA8,
        0xE4,0xBB,0x80,0xE4,0xB9,0x88,
        0xE7,0x9B,0xAE,0xE5,0xBD,0x95
    });
}

std::string join_lines(const std::vector<std::string>& lines) {
    std::ostringstream out;
    for (const auto& line : lines) {
        out << line << '\n';
    }
    return out.str();
}

std::size_t count_occurrences(const std::string& text, const std::string& pattern) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(pattern, pos)) != std::string::npos) {
        ++count;
        pos += pattern.size();
    }
    return count;
}

}  // namespace

void test_model_command_sets_model() {
    TuiApp app;
    assert(app.handle_command("/model gpt-test"));
    assert(app.config().model == "gpt-test");
}

void test_api_commands_set_runtime_config_without_key_value() {
    TuiApp app;
    assert(app.handle_command("/api provider openai"));
    assert(app.handle_command("/api base https://example.com/v1"));
    assert(app.handle_command("/api key-env OPENAI_API_KEY"));
    assert(app.handle_command("/api timeout 45"));
    assert(app.handle_command("/api max-loops 9"));

    assert(app.config().provider == "openai");
    assert(app.config().api_base == "https://example.com/v1");
    assert(app.config().api_key_env == "OPENAI_API_KEY");
    assert(app.config().timeout_seconds == 45);
    assert(app.config().max_loops == 9);
}

void test_config_command_paths_and_show() {
    TuiApp app;
    assert(app.handle_command("/config show"));
    assert(app.handle_command("/config paths"));
}

void test_config_init_project_creates_project_config() {
    const auto root = make_test_root();
    TuiApp app(root);
    assert(app.handle_command("/config init project"));
    assert(std::filesystem::exists(root / ".agent_tui" / "config.toml"));
    std::filesystem::remove_all(root);
}

void test_run_initializes_project_state_directory() {
    const auto root = make_test_root();
    std::istringstream input("/exit\n");
    std::ostringstream output;

    TuiApp app(root);
    app.set_streams(input, output);
    const auto code = app.run();

    assert(code == 0);
    assert(std::filesystem::exists(root / ".agent_tui"));
    assert(std::filesystem::exists(root / ".agent_tui" / "sessions"));
    std::filesystem::remove_all(root);
}

void test_interrupt_command_sets_flag() {
    TuiApp app;
    assert(!app.interrupted());
    assert(app.handle_command("/interrupt"));
    assert(app.interrupted());
}

void test_clear_command_resets_history_and_interrupt() {
    TuiApp app;
    app.handle_command("/interrupt");
    app.handle_command("/status");
    assert(app.interrupted());
    assert(!app.history().empty());

    assert(app.handle_command("/clear"));
    assert(!app.interrupted());
    assert(app.history().size() == 1);  // /clear records "session cleared"
}

void test_exit_command_stops_app() {
    TuiApp app;
    assert(app.running());
    assert(app.handle_command("/exit"));
    assert(!app.running());
}

void test_run_accepts_scripted_input_and_uses_mock_provider() {
    const auto root = make_test_root();
    std::istringstream input("hello provider\n/model scripted\n/api provider mock\n/exit\n");
    std::ostringstream output;

    TuiApp app(root);
    app.set_streams(input, output);
    const auto code = app.run();

    assert(code == 0);
    assert(app.config().model == "scripted");
    assert(app.config().provider == "mock");
    assert(output.str().find("Agent TUI") != std::string::npos);
    assert(output.str().find("Provider") != std::string::npos);
    assert(output.str().find("assistant streaming > mock assistant: hello provider") != std::string::npos);
    assert(count_occurrences(output.str(), "assistant streaming >") == 1);
    assert(output.str().find("assistant done > mock assistant: hello provider") != std::string::npos);
    std::filesystem::remove_all(root);
}

void test_render_uses_transcript_cell_layout() {
    const auto root = make_test_root();
    std::istringstream input("/status\n/exit\n");
    std::ostringstream output;

    TuiApp app(root);
    app.set_streams(input, output);
    const auto code = app.run();

    assert(code == 0);
    assert(output.str().find("Agent TUI") != std::string::npos);
    assert(output.str().find("[IDLE]") != std::string::npos);
    assert(output.str().find("Transcript") != std::string::npos);
    assert(output.str().find("Commands") != std::string::npos);
    std::filesystem::remove_all(root);
}

void test_windows_code_page_input_can_be_normalized_to_utf8() {
#ifdef _WIN32
    const std::string gbk_hello = "\xC4\xE3\xBA\xC3";
    const auto utf8_hello = tui_detail::decode_code_page(gbk_hello, 936);
    assert(utf8_hello == "\xE4\xBD\xA0\xE5\xA5\xBD");
#endif
}

void test_terminal_wraps_long_unspaced_text() {
    const auto ascii_lines = TuiApp::wrap_text_for_terminal("abcdefghijklmnopqrstuvwxyz", 8);
    assert(ascii_lines.size() == 4);
    assert(ascii_lines[0] == "abcdefgh");
    assert(ascii_lines[1] == "ijklmnop");
    assert(ascii_lines[2] == "qrstuvwx");
    assert(ascii_lines[3] == "yz");

    const auto chinese = bytes({
        0xE4,0xBD,0xA0, 0xE5,0xA5,0xBD,
        0xE4,0xBD,0xA0, 0xE5,0xA5,0xBD,
        0xE4,0xBD,0xA0, 0xE5,0xA5,0xBD
    });
    const auto chinese_lines = TuiApp::wrap_text_for_terminal(chinese, 4);
    assert(chinese_lines.size() == 3);
    assert(chinese_lines[0] == chinese.substr(0, 6));
    assert(chinese_lines[1] == chinese.substr(6, 6));
    assert(chinese_lines[2] == chinese.substr(12, 6));
}

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

void test_system_prompt_includes_workspace_path() {
    const auto root = make_test_root();
    Workspace workspace(root);
    const auto prompt = TuiApp::coding_agent_system_prompt(workspace);

    assert(prompt.find("Current working directory: " + workspace.root().generic_string()) != std::string::npos);
    std::filesystem::remove_all(root);
}

void test_tui_runs_agent_tool_loop_for_code_demo() {
    const auto root = make_test_root();
    std::istringstream input("/api provider mock-agent-demo\n" + zh_implement_demo() + "\ny\ny\n/exit\n");
    std::ostringstream output;

    TuiApp app(root);
    app.set_streams(input, output);
    const auto code = app.run();

    assert(code == 0);
    assert(std::filesystem::exists(root / "demo.py"));
    {
        std::ifstream file(root / "demo.py", std::ios::binary);
        std::ostringstream buffer;
        buffer << file.rdbuf();
        assert(buffer.str().find("hello from agent_tui") != std::string::npos);
    }
    assert(output.str().find("Approve write_file") != std::string::npos);
    assert(output.str().find("[THINKING]") != std::string::npos);
    assert(output.str().find("[WAITING_APPROVAL]") != std::string::npos);
    assert(output.str().find("[RUNNING_TOOL]") != std::string::npos);
    assert(output.str().find("agent > thinking") != std::string::npos);
    assert(output.str().find("tool call > write_file") != std::string::npos);
    assert(output.str().find("approval required > write_file") != std::string::npos);
    assert(output.str().find("tool result > write_file") != std::string::npos);
    assert(output.str().find("tool call > run_shell") != std::string::npos);
    assert(output.str().find("approval required > run_shell") != std::string::npos);
    assert(output.str().find("tool result > run_shell") != std::string::npos);
    assert(output.str().find("hello from agent_tui") != std::string::npos);
    assert(output.str().find("tool result > run_shell exit_code: 0; stdout: hello from agent_tui") != std::string::npos);
    assert(output.str().find("tool_call >") == std::string::npos);
    assert(output.str().find("tool_result >") == std::string::npos);
    assert(output.str().find("assistant done > demo.py is ready and was executed") != std::string::npos);
    assert(output.str().find("demo.py is ready and was executed") != std::string::npos);
    std::filesystem::remove_all(root);
}

void test_tui_emits_progress_heartbeat_during_agent_run() {
    const auto root = make_test_root();
    std::istringstream input("/api provider mock-agent-demo\n" + zh_implement_demo() + "\ny\ny\n/exit\n");
    std::ostringstream output;

    TuiApp app(root);
    app.set_streams(input, output);
    app.set_progress_heartbeat_interval_for_test(std::chrono::milliseconds(0));
    const auto code = app.run();

    assert(code == 0);
    assert(output.str().find("agent > waiting for model response") != std::string::npos);
    std::filesystem::remove_all(root);
}

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

void test_directory_question_is_not_handled_by_local_intent_router() {
    const auto root = make_test_root();
    std::istringstream input("/api provider mock\n" + zh_current_directory_question() + "\n/exit\n");
    std::ostringstream output;

    TuiApp app(root);
    app.set_streams(input, output);
    const auto code = app.run();

    assert(code == 0);
    assert(output.str().find("mock assistant:") != std::string::npos);
    assert(output.str().find("local intent") == std::string::npos);
    assert(output.str().find("tool > list_dir") == std::string::npos);
    std::filesystem::remove_all(root);
}

int main() {
    test_model_command_sets_model();
    test_api_commands_set_runtime_config_without_key_value();
    test_config_command_paths_and_show();
    test_config_init_project_creates_project_config();
    test_run_initializes_project_state_directory();
    test_interrupt_command_sets_flag();
    test_clear_command_resets_history_and_interrupt();
    test_exit_command_stops_app();
    test_run_accepts_scripted_input_and_uses_mock_provider();
    test_render_uses_transcript_cell_layout();
    test_windows_code_page_input_can_be_normalized_to_utf8();
    test_terminal_wraps_long_unspaced_text();
    test_transcript_tracks_streaming_and_done_cells();
    test_system_prompt_includes_workspace_path();
    test_tui_runs_agent_tool_loop_for_code_demo();
    test_tui_emits_progress_heartbeat_during_agent_run();
    test_approval_prompt_accepts_feedback_denial();
    test_directory_question_is_not_handled_by_local_intent_router();
    return 0;
}

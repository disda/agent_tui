#include "agent_tui/tui/TuiApp.hpp"

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

std::string utf8_hello() {
    return bytes({0xE4, 0xBD, 0xA0, 0xE5, 0xA5, 0xBD});
}

std::string utf8_chinese_create_request() {
    return bytes({
        0xE5, 0x88, 0x9B, 0xE5, 0xBB, 0xBA, 0x20,
        'h', 'e', 'l', 'l', 'o', '.', 't', 'x', 't', 0x20,
        0xE6, 0x96, 0x87, 0xE4, 0xBB, 0xB6,
        0xEF, 0xBC, 0x8C,
        0xE5, 0x86, 0x85, 0xE5, 0xAE, 0xB9,
        0xE5, 0x8F, 0xAA, 0xE5, 0x86, 0x99,
        0xEF, 0xBC, 0x9A,
        0xE4, 0xBD, 0xA0, 0xE5, 0xA5, 0xBD,
        '\n', '/', 'e', 'x', 'i', 't', '\n'
    });
}

std::string utf8_write_file_without_name_request() {
    return bytes({
        0xE5, 0x86, 0x99,
        0xE4, 0xB8, 0xAA,
        0xE6, 0x96, 0x87, 0xE4, 0xBB, 0xB6,
        0xE5, 0x86, 0x99, 0xE5, 0x85, 0xA5,
        0xE4, 0xBD, 0xA0, 0xE5, 0xA5, 0xBD,
        '\n', '/', 'e', 'x', 'i', 't', '\n'
    });
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
    assert(output.str().find("mock assistant: hello provider") != std::string::npos);
    std::filesystem::remove_all(root);
}

void test_render_uses_compact_chat_layout() {
    const auto root = make_test_root();
    std::istringstream input("/status\n/exit\n");
    std::ostringstream output;

    TuiApp app(root);
    app.set_streams(input, output);
    const auto code = app.run();

    assert(code == 0);
    assert(output.str().find("Agent TUI") != std::string::npos);
    assert(output.str().find("[IDLE]") != std::string::npos);
    assert(output.str().find("Recent messages") != std::string::npos);
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

void test_chinese_create_file_request_executes_locally() {
    const auto root = make_test_root();
    std::istringstream input(utf8_chinese_create_request());
    std::ostringstream output;

    TuiApp app(root);
    app.set_streams(input, output);
    const auto code = app.run();

    assert(code == 0);
    assert(std::filesystem::exists(root / "hello.txt"));
    {
        std::ifstream file(root / "hello.txt", std::ios::binary);
        std::ostringstream buffer;
        buffer << file.rdbuf();
        assert(buffer.str() == utf8_hello());
    }
    std::filesystem::remove_all(root);
}

void test_chinese_write_file_without_name_defaults_to_hello_txt() {
    const auto root = make_test_root();
    std::istringstream input(utf8_write_file_without_name_request());
    std::ostringstream output;

    TuiApp app(root);
    app.set_streams(input, output);
    const auto code = app.run();

    assert(code == 0);
    assert(std::filesystem::exists(root / "hello.txt"));
    {
        std::ifstream file(root / "hello.txt", std::ios::binary);
        std::ostringstream buffer;
        buffer << file.rdbuf();
        assert(buffer.str() == utf8_hello());
    }
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
    test_render_uses_compact_chat_layout();
    test_windows_code_page_input_can_be_normalized_to_utf8();
    test_chinese_create_file_request_executes_locally();
    test_chinese_write_file_without_name_defaults_to_hello_txt();
    return 0;
}

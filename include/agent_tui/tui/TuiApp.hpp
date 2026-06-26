#pragma once

#include <csignal>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

#include "agent_tui/agent/AgentRunner.hpp"
#include "agent_tui/config/ConfigLoader.hpp"
#include "agent_tui/llm/OpenAICompatibleProvider.hpp"
#include "agent_tui/llm/ProviderFactory.hpp"
#include "agent_tui/permissions/ApprovalService.hpp"
#include "agent_tui/session/SessionHistory.hpp"
#include "agent_tui/tools/FileTools.hpp"
#include "agent_tui/tools/WriteEditTools.hpp"
#include "agent_tui/tui/TuiConfig.hpp"
#include "agent_tui/workspace/Workspace.hpp"

namespace agent_tui {

namespace tui_detail {

#ifdef _WIN32
inline std::string decode_code_page(const std::string& input, unsigned int code_page) {
    if (input.empty()) {
        return input;
    }

    const int wide_size = MultiByteToWideChar(code_page, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (wide_size <= 0) {
        return input;
    }

    std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
    const int converted_wide = MultiByteToWideChar(code_page, 0, input.data(), static_cast<int>(input.size()), wide.data(), wide_size);
    if (converted_wide <= 0) {
        return input;
    }

    const int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), converted_wide, nullptr, 0, nullptr, nullptr);
    if (utf8_size <= 0) {
        return input;
    }

    std::string utf8(static_cast<std::size_t>(utf8_size), '\0');
    const int converted_utf8 = WideCharToMultiByte(CP_UTF8, 0, wide.data(), converted_wide, utf8.data(), utf8_size, nullptr, nullptr);
    if (converted_utf8 <= 0) {
        return input;
    }
    return utf8;
}

inline bool is_interactive_stdin(const std::istream* input) {
    return input == &std::cin && _isatty(_fileno(stdin)) != 0;
}

inline void configure_console_output() {
    SetConsoleOutputCP(CP_UTF8);
}
#else
inline std::string decode_code_page(const std::string& input, unsigned int) {
    return input;
}

inline bool is_interactive_stdin(const std::istream*) {
    return false;
}

inline void configure_console_output() {}
#endif

}  // namespace tui_detail

class AutoApprovalService final : public ApprovalService {
public:
    ApprovalDecision request(const ToolCall&, const Tool&) override {
        return ApprovalDecision::approve();
    }
};

class TuiApp {
public:
    explicit TuiApp(std::filesystem::path workspace = std::filesystem::current_path())
        : workspace_(std::filesystem::absolute(std::move(workspace))), config_(ConfigLoader::load(workspace_)) {}

    int run() {
        tui_detail::configure_console_output();
        install_signal_handler();
        initialize_project_state();
        add_system_message("Welcome to agent_tui. Type /help for commands.");
        render();

        std::string line;
        while (running_ && std::getline(*input_, line)) {
            line = normalize_input_line(line);
            sync_global_interrupt();
            if (interrupted_) {
                add_system_message("Interrupt flag is set. Use /status to inspect, /clear to reset history, or continue typing.");
            }

            if (line.empty()) {
                render_prompt_only();
                continue;
            }

            if (line[0] == '/') {
                handle_command(line);
            } else {
                handle_user_input(line);
            }

            if (running_) {
                render();
            }
        }
        return 0;
    }

    bool handle_command(const std::string& line) {
        std::istringstream input(line);
        std::string command;
        input >> command;

        if (command == "/help") {
            show_help();
            return true;
        }
        if (command == "/exit" || command == "/quit") {
            add_system_message("bye");
            running_ = false;
            return true;
        }
        if (command == "/status") {
            show_status();
            return true;
        }
        if (command == "/clear") {
            chat_lines_.clear();
            history_.clear();
            interrupted_ = false;
            add_system_message("session cleared");
            return true;
        }
        if (command == "/model") {
            std::string model;
            input >> model;
            if (model.empty()) {
                add_system_message("current model: " + config_.model);
            } else {
                config_.model = model;
                add_system_message("model set to: " + config_.model);
            }
            return true;
        }
        if (command == "/api") {
            handle_api_command(input);
            return true;
        }
        if (command == "/config") {
            handle_config_command(input);
            return true;
        }
        if (command == "/interrupt") {
            interrupted_ = true;
            add_system_message("interrupt requested");
            return true;
        }
        if (command == "/skills") {
            add_system_message("SkillRuntime is not wired yet. Planned default skills: repo_reader, code_editor, shell_runner, cpp_project, tui_agent, kwoa_cli.");
            return true;
        }

        add_system_message("unknown command: " + command + " (try /help)");
        return false;
    }

    const TuiConfig& config() const { return config_; }
    const SessionHistory& history() const { return history_; }
    bool interrupted() const { return interrupted_; }
    bool running() const { return running_; }

    void request_interrupt() {
        interrupted_ = true;
    }

    static void request_global_interrupt() {
        global_interrupted_ = true;
    }

    void sync_global_interrupt() {
        if (global_interrupted_) {
            interrupted_ = true;
            global_interrupted_ = false;
        }
    }

    void set_streams(std::istream& input, std::ostream& output) {
        input_ = &input;
        output_ = &output;
    }

private:
    static void signal_handler(int) {
        global_interrupted_ = true;
    }

    void install_signal_handler() {
        std::signal(SIGINT, signal_handler);
    }

    void handle_user_input(const std::string& line) {
        add_chat_line("user", line);

        if (try_handle_local_file_request(line)) {
            return;
        }

        if (should_stream_plain_chat(line)) {
            OpenAICompatibleProvider provider(config_);
            *output_ << "\n" << ansi("38;5;114") << "assistant" << ansi("0") << " > " << std::flush;
            const auto response = provider.chat_stream({
                Message{Role::System, "You are agent_tui, a concise local coding assistant. Answer in the user's language.", {}},
                Message{Role::User, line, {}},
            }, [this](const std::string& delta) {
                *output_ << delta << std::flush;
            });
            *output_ << "\n";
            if (response.type == ProviderResponseType::Text) {
                add_chat_line("assistant", response.text);
                history_.add(SessionEvent::assistant_message(response.text));
                return;
            }
            if (response.type == ProviderResponseType::Error) {
                add_error_message(response.error);
                return;
            }
        }

        auto provider = ProviderFactory::create(config_);
        Workspace workspace(workspace_);
        ToolRegistry registry;
        registry.register_tool(std::make_unique<ListDirTool>(workspace));
        registry.register_tool(std::make_unique<ReadFileTool>(workspace));
        registry.register_tool(std::make_unique<GlobFilesTool>(workspace));
        registry.register_tool(std::make_unique<SearchTextTool>(workspace));
        registry.register_tool(std::make_unique<WriteFileTool>(workspace));
        registry.register_tool(std::make_unique<EditFileTool>(workspace));

        AutoApprovalService approval;
        AgentRunner runner(*provider, registry, approval, history_, config_.max_loops);
        const auto result = runner.run({
            Message{Role::System,
                    "You are agent_tui, a local coding agent. Use tools to inspect and modify files when the user asks for workspace actions. "
                    "For requests like creating or editing files, call write_file or edit_file instead of only explaining commands. "
                    "Keep final answers concise and mention changed files.",
                    {}},
            Message{Role::User, line, {}},
        });

        append_tool_events(runner.last_messages());
        if (result.ok()) {
            add_chat_line("assistant", result.output);
            return;
        }
        add_error_message(result.error);
    }

    bool try_handle_local_file_request(const std::string& line) {
        if (!looks_like_local_file_write_request(line)) {
            return false;
        }

        const std::regex path_pattern(R"(([A-Za-z0-9_.\-/\\]+\.[A-Za-z0-9]+))");
        std::smatch match;
        const auto path = std::regex_search(line, match, path_pattern) ? match[1].str() : std::string{"hello.txt"};

        auto content = extract_requested_file_content(line);
        if (content.empty()) {
            return false;
        }

        history_.add(SessionEvent::user_input(line));
        Workspace workspace(workspace_);
        WriteFileTool tool(workspace);
        const auto result = tool.run({
            {"path", path},
            {"content", content},
            {"create_parent_dirs", "true"},
        });

        if (result.ok) {
            const auto answer = "created " + path + " and wrote the requested content.";
            add_chat_line("tool", result.output);
            add_chat_line("assistant", answer);
            history_.add(SessionEvent::tool_result("local_write_file", "write_file", result.output));
            history_.add(SessionEvent::assistant_message(answer));
            return true;
        }

        add_error_message(result.error);
        return true;
    }

    static bool looks_like_local_file_write_request(const std::string& line) {
        const bool mentions_file = line.find(utf8_file()) != std::string::npos || line.find("file") != std::string::npos;
        const bool asks_create = line.find(utf8_create()) != std::string::npos || line.find("create") != std::string::npos;
        const bool asks_write = line.find(utf8_write()) != std::string::npos || line.find("write") != std::string::npos;
        return mentions_file && (asks_create || asks_write);
    }

    bool should_stream_plain_chat(const std::string& line) const {
        if (config_.provider != "openai-compatible" && config_.provider != "openai") {
            return false;
        }

        const std::vector<std::string> workspace_words = {
            utf8_file(),
            utf8_create(),
            utf8_modify(),
            utf8_search(),
            utf8_build(),
            utf8_test(),
            "file",
            "create",
            "edit",
            "modify",
            "search",
            "build",
            "test",
            "run",
            "fix",
            "bug",
            "code",
        };
        for (const auto& word : workspace_words) {
            if (line.find(word) != std::string::npos) {
                return false;
            }
        }
        return true;
    }

    static std::string extract_requested_file_content(const std::string& line) {
        const std::vector<std::string> markers = {
            utf8_content_only_colon(),
            utf8_content_only_ascii_colon(),
            utf8_content_write_colon(),
            utf8_content_write_ascii_colon(),
            utf8_write_colon(),
            utf8_write_ascii_colon(),
            utf8_write(),
            "content:",
            "with content:",
            "write ",
        };
        for (const auto& marker : markers) {
            const auto pos = line.find(marker);
            if (pos != std::string::npos) {
                return trim_copy(line.substr(pos + marker.size()));
            }
        }
        return {};
    }

    static std::string trim_copy(std::string value) {
        const auto is_space = [](unsigned char ch) {
            return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
        };
        while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
            value.erase(value.begin());
        }
        while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
            value.pop_back();
        }
        return value;
    }

    static std::string bytes(std::initializer_list<unsigned char> values) {
        std::string out;
        out.reserve(values.size());
        for (const auto value : values) {
            out.push_back(static_cast<char>(value));
        }
        return out;
    }

    static std::string utf8_create() { return bytes({0xE5, 0x88, 0x9B, 0xE5, 0xBB, 0xBA}); }
    static std::string utf8_file() { return bytes({0xE6, 0x96, 0x87, 0xE4, 0xBB, 0xB6}); }
    static std::string utf8_modify() { return bytes({0xE4, 0xBF, 0xAE, 0xE6, 0x94, 0xB9}); }
    static std::string utf8_search() { return bytes({0xE6, 0x90, 0x9C, 0xE7, 0xB4, 0xA2}); }
    static std::string utf8_build() { return bytes({0xE6, 0x9E, 0x84, 0xE5, 0xBB, 0xBA}); }
    static std::string utf8_test() { return bytes({0xE6, 0xB5, 0x8B, 0xE8, 0xAF, 0x95}); }
    static std::string utf8_write() { return bytes({0xE5, 0x86, 0x99, 0xE5, 0x85, 0xA5}); }
    static std::string utf8_content_only_colon() { return bytes({0xE5, 0x86, 0x85, 0xE5, 0xAE, 0xB9, 0xE5, 0x8F, 0xAA, 0xE5, 0x86, 0x99, 0xEF, 0xBC, 0x9A}); }
    static std::string utf8_content_only_ascii_colon() { return bytes({0xE5, 0x86, 0x85, 0xE5, 0xAE, 0xB9, 0xE5, 0x8F, 0xAA, 0xE5, 0x86, 0x99, 0x3A}); }
    static std::string utf8_content_write_colon() { return bytes({0xE5, 0x86, 0x85, 0xE5, 0xAE, 0xB9, 0xE5, 0x86, 0x99, 0xE5, 0x85, 0xA5, 0xEF, 0xBC, 0x9A}); }
    static std::string utf8_content_write_ascii_colon() { return bytes({0xE5, 0x86, 0x85, 0xE5, 0xAE, 0xB9, 0xE5, 0x86, 0x99, 0xE5, 0x85, 0xA5, 0x3A}); }
    static std::string utf8_write_colon() { return bytes({0xE5, 0x86, 0x99, 0xE5, 0x85, 0xA5, 0xEF, 0xBC, 0x9A}); }
    static std::string utf8_write_ascii_colon() { return bytes({0xE5, 0x86, 0x99, 0xE5, 0x85, 0xA5, 0x3A}); }

    void append_tool_events(const std::vector<Message>& messages) {
        for (const auto& message : messages) {
            if (message.role == Role::Tool) {
                add_chat_line("tool", message.content);
            }
        }
    }

    void render() {
        sync_global_interrupt();
        const auto status = interrupted_ ? std::string{"INTERRUPTED"} : std::string{"IDLE"};
        *output_ << "\n"
                 << ansi("1;38;5;81") << "Agent TUI" << ansi("0")
                 << "  " << ansi(interrupted_ ? "1;38;5;203" : "1;38;5;114") << "[" << status << "]" << ansi("0")
                 << "\n";
        *output_ << ansi("38;5;245") << "Provider" << ansi("0")
                 << "  " << config_.provider
                 << "    " << ansi("38;5;245") << "Model" << ansi("0")
                 << "  " << config_.model << "\n";
        *output_ << ansi("38;5;245") << "API" << ansi("0")
                 << "       " << (config_.api_base.empty() ? "<not set>" : config_.api_base)
                 << "    " << ansi("38;5;245") << "Key" << ansi("0")
                 << "  " << config_.api_key_status()
                 << "\n\n";
        *output_ << ansi("1;38;5;252") << "Recent messages" << ansi("0") << "\n";
        if (chat_lines_.empty()) {
            *output_ << "  " << ansi("38;5;245") << "No messages yet. Type a prompt or /help." << ansi("0") << "\n";
        } else {
            const std::size_t start = chat_lines_.size() > 12 ? chat_lines_.size() - 12 : 0;
            for (std::size_t i = start; i < chat_lines_.size(); ++i) {
                render_chat_line(chat_lines_[i]);
            }
        }
        *output_ << "\n"
                 << ansi("38;5;245") << "Commands" << ansi("0")
                 << "  /help /status /clear /model /api /config /interrupt /skills /exit\n";
        *output_ << ansi("1;38;5;81") << "agent_tui>" << ansi("0") << " ";
    }

    void render_prompt_only() {
        *output_ << ansi("1;38;5;81") << "agent_tui>" << ansi("0") << " ";
    }

    std::string normalize_input_line(const std::string& line) const {
#ifdef _WIN32
        if (tui_detail::is_interactive_stdin(input_)) {
            return tui_detail::decode_code_page(line, GetConsoleCP());
        }
#endif
        return line;
    }

    void show_help() {
        add_system_message(
            "commands:\n"
            "  /help                         show help\n"
            "  /status                       show runtime status\n"
            "  /clear                        clear chat and session history\n"
            "  /model [name]                 show or set model\n"
            "  /api                          show runtime api config\n"
            "  /api provider <name>          set provider\n"
            "  /api base <url>               set API base URL\n"
            "  /api key-env <ENV_NAME>       set API key environment variable name\n"
            "  /api timeout <seconds>        set timeout\n"
            "  /api max-loops <n>            set max agent loops\n"
            "  /config show                  show redacted loaded config\n"
            "  /config paths                 show user/project config paths\n"
            "  /config init user             create ~/.agent_tui/config.toml\n"
            "  /config init project          create ./.agent_tui/config.toml\n"
            "  /config reload                reload default + user + project config\n"
            "  /interrupt                    request interrupt\n"
            "  /skills                       show skill runtime status\n"
            "  /exit                         quit");
    }

    void show_status() {
        std::ostringstream out;
        out << "status: " << (interrupted_ ? "INTERRUPTED" : "IDLE") << '\n';
        out << config_.summary();
        out << "history_events: " << history_.size();
        add_system_message(out.str());
    }

    void handle_api_command(std::istringstream& input) {
        std::string field;
        input >> field;
        if (field.empty()) {
            add_system_message(config_.summary());
            return;
        }

        std::string value;
        input >> value;
        if (value.empty()) {
            add_system_message("missing value for /api " + field);
            return;
        }

        if (field == "provider") {
            config_.provider = value;
            add_system_message("provider set to: " + config_.provider);
            return;
        }
        if (field == "base") {
            config_.api_base = value;
            add_system_message("api_base set");
            return;
        }
        if (field == "key-env") {
            config_.api_key_env = value;
            add_system_message("api_key_env set to: " + config_.api_key_env + " (key value is never printed)");
            return;
        }
        if (field == "timeout") {
            config_.timeout_seconds = parse_positive_int(value, config_.timeout_seconds);
            add_system_message("timeout_seconds set to: " + std::to_string(config_.timeout_seconds));
            return;
        }
        if (field == "max-loops") {
            config_.max_loops = parse_positive_int(value, config_.max_loops);
            add_system_message("max_loops set to: " + std::to_string(config_.max_loops));
            return;
        }

        add_system_message("unknown /api field: " + field);
    }

    void handle_config_command(std::istringstream& input) {
        std::string subcommand;
        input >> subcommand;

        if (subcommand.empty() || subcommand == "show") {
            add_system_message(config_.summary());
            return;
        }
        if (subcommand == "paths") {
            std::ostringstream out;
            out << "user_config: " << ConfigLoader::user_config_path().generic_string() << '\n';
            out << "project_config: " << ConfigLoader::project_config_path(workspace_).generic_string() << '\n';
            out << "project_state: " << ConfigLoader::project_state_path(workspace_).generic_string();
            add_system_message(out.str());
            return;
        }
        if (subcommand == "init") {
            std::string scope;
            input >> scope;
            if (scope == "user") {
                const bool ok = ConfigLoader::init_user_config(false);
                add_system_message(ok ? "user config ready: " + ConfigLoader::user_config_path().generic_string()
                                      : "failed to create user config");
                return;
            }
            if (scope == "project") {
                const bool ok = ConfigLoader::init_project_config(workspace_, false);
                add_system_message(ok ? "project config ready: " + ConfigLoader::project_config_path(workspace_).generic_string()
                                      : "failed to create project config");
                return;
            }
            add_system_message("usage: /config init user|project");
            return;
        }
        if (subcommand == "reload") {
            config_ = ConfigLoader::load(workspace_);
            add_system_message("config reloaded\n" + config_.summary());
            return;
        }

        add_system_message("unknown /config command: " + subcommand);
    }

    static int parse_positive_int(const std::string& value, int fallback) {
        try {
            const auto parsed = std::stoi(value);
            return parsed > 0 ? parsed : fallback;
        } catch (...) {
            return fallback;
        }
    }

    void add_system_message(const std::string& message) {
        chat_lines_.push_back("system: " + message);
        history_.add(SessionEvent::assistant_message(message));
    }

    void add_error_message(const std::string& message) {
        chat_lines_.push_back("error: " + message);
        history_.add(SessionEvent::error(message));
    }

    void add_chat_line(const std::string& role, const std::string& message) {
        chat_lines_.push_back(role + ": " + message);
    }

    std::string ansi(const char* code) const {
        return color_enabled_ ? "\x1b[" + std::string(code) + "m" : "";
    }

    void render_chat_line(const std::string& line) {
        auto role = std::string{"log"};
        auto message = line;
        const auto separator = line.find(": ");
        if (separator != std::string::npos) {
            role = line.substr(0, separator);
            message = line.substr(separator + 2);
        }

        const char* color = "38;5;245";
        if (role == "user") {
            color = "38;5;81";
        } else if (role == "assistant") {
            color = "38;5;114";
        } else if (role == "error") {
            color = "38;5;203";
        }

        *output_ << "  " << ansi(color) << role << ansi("0") << " > ";
        const auto wrapped = wrap_text(message, 84);
        if (wrapped.empty()) {
            *output_ << "\n";
            return;
        }
        *output_ << wrapped.front() << "\n";
        for (std::size_t i = 1; i < wrapped.size(); ++i) {
            *output_ << "       " << wrapped[i] << "\n";
        }
    }

    static std::vector<std::string> wrap_text(const std::string& text, std::size_t width) {
        std::vector<std::string> lines;
        std::istringstream words(text);
        std::string word;
        std::string current;
        while (words >> word) {
            if (current.empty()) {
                current = word;
                continue;
            }
            if (current.size() + 1 + word.size() > width) {
                lines.push_back(current);
                current = word;
                continue;
            }
            current += " " + word;
        }
        if (!current.empty()) {
            lines.push_back(current);
        }
        return lines;
    }

    void initialize_project_state() {
        if (!ConfigLoader::ensure_project_state(workspace_)) {
            add_error_message("failed to initialize project state: " + ConfigLoader::project_state_path(workspace_).generic_string());
        }
    }

    inline static volatile std::sig_atomic_t global_interrupted_ = 0;

    std::istream* input_ = &std::cin;
    std::ostream* output_ = &std::cout;
    std::filesystem::path workspace_;
    TuiConfig config_;
    SessionHistory history_;
    std::vector<std::string> chat_lines_;
    bool color_enabled_ = true;
    bool running_ = true;
    bool interrupted_ = false;
};

}  // namespace agent_tui

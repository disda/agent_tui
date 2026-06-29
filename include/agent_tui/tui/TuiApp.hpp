#pragma once

#include <algorithm>
#include <csignal>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

#include "agent_tui/agent/AgentRunner.hpp"
#include "agent_tui/config/ConfigLoader.hpp"
#include "agent_tui/llm/ProviderFactory.hpp"
#include "agent_tui/permissions/ApprovalService.hpp"
#include "agent_tui/session/SessionHistory.hpp"
#include "agent_tui/tools/FileTools.hpp"
#include "agent_tui/tools/ShellTool.hpp"
#include "agent_tui/tools/ToolRegistry.hpp"
#include "agent_tui/tools/WriteEditTools.hpp"
#include "agent_tui/tui/TuiConfig.hpp"
#include "agent_tui/tui/TuiTranscript.hpp"
#include "agent_tui/workspace/Workspace.hpp"

namespace agent_tui {

enum class TuiRuntimeStatus {
    Idle,
    Thinking,
    WaitingApproval,
    RunningTool,
    Done,
    Error,
    Interrupted,
};

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
            transcript_.clear();
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

    void request_interrupt() { interrupted_ = true; }

    static void request_global_interrupt() { global_interrupted_ = true; }

    void sync_global_interrupt() {
        if (global_interrupted_) {
            interrupted_ = true;
            global_interrupted_ = false;
        }
    }

    void set_streams(std::istream& input, std::ostream& output) {
        input_ = &input;
        output_ = &output;
        color_enabled_ = false;
    }

    static std::string coding_agent_system_prompt(const Workspace& workspace) {
        return "You are agent_tui, a local coding agent. "
               "Use tools to inspect, create, edit, and test code when useful. "
               "Call write_file, edit_file, or run_shell only when necessary; the TUI will ask the user for permission. "
               "After each tool result, continue reasoning until the task is complete, then provide a concise final answer.\n"
               "Current working directory: " +
               workspace.root().generic_string();
    }

    static std::vector<std::string> wrap_text_for_terminal(const std::string& text, std::size_t width) {
        return TuiTranscript::wrap_text_for_terminal(text, width);
    }

private:
    static void signal_handler(int) {
        global_interrupted_ = true;
    }

    void install_signal_handler() {
        std::signal(SIGINT, signal_handler);
    }

    void initialize_project_state() {
        if (!ConfigLoader::ensure_project_state(workspace_)) {
            add_error_message("failed to initialize project state: " + ConfigLoader::project_state_path(workspace_).generic_string());
        }
    }

    void handle_user_input(const std::string& line) {
        history_.add(SessionEvent::user_input(line));
        add_chat_line("user", line);
        run_agent_loop(line);
    }

    class TuiApprovalService final : public ApprovalService {
    public:
        TuiApprovalService(std::istream& input, std::ostream& output)
            : input_(input), output_(output) {}

        ApprovalDecision request(const ToolCall& call, const Tool& tool) override {
            if (before_request_) {
                before_request_(call, tool);
            }
            output_ << "\nApprove " << tool.name() << ": " << summarize(call.arguments)
                    << " ? [y/N, n: feedback, edit: feedback] " << std::flush;
            std::string answer;
            if (!std::getline(input_, answer)) {
                return ApprovalDecision::deny("approval input ended before a decision");
            }
            const auto raw_answer = trim_copy(answer);
            const auto normalized = lower_ascii(raw_answer);
            if (normalized == "y" || normalized == "yes") {
                return ApprovalDecision::approve();
            }
            if (normalized.rfind("n:", 0) == 0 || normalized.rfind("no:", 0) == 0) {
                const auto colon = raw_answer.find(':');
                return ApprovalDecision::deny(colon == std::string::npos ? std::string{} : trim_copy(raw_answer.substr(colon + 1)));
            }
            if (normalized.rfind("edit:", 0) == 0) {
                const auto colon = raw_answer.find(':');
                return ApprovalDecision::user_feedback(colon == std::string::npos ? std::string{} : trim_copy(raw_answer.substr(colon + 1)));
            }
            return ApprovalDecision::deny("user rejected in TUI");
        }

        std::function<void(const ToolCall&, const Tool&)> before_request_;

    private:
        static std::string summarize(const JsonLike& arguments) {
            std::ostringstream out;
            bool first = true;
            for (const auto& [key, value] : arguments) {
                if (!first) {
                    out << ", ";
                }
                first = false;
                out << key << "=" << truncate(value, 80);
            }
            return out.str();
        }

        static std::string truncate(const std::string& value, std::size_t max_size) {
            if (value.size() <= max_size) {
                return value;
            }
            return value.substr(0, max_size) + "...";
        }

        std::istream& input_;
        std::ostream& output_;
    };

    void run_agent_loop(const std::string& line) {
        try {
            Workspace workspace(workspace_);
            auto provider = ProviderFactory::create(config_);
            auto registry = make_tool_registry(workspace);
            TuiApprovalService approval(*input_, *output_);
            approval.before_request_ = [&](const ToolCall&, const Tool&) {
                status_ = TuiRuntimeStatus::WaitingApproval;
                render();
            };
            AgentRunner runner(*provider, registry, approval, history_, config_.max_loops);
            runner.set_interrupt_checker([&]() {
                sync_global_interrupt();
                return interrupted_;
            });
            bool streamed_assistant = false;
            transcript_.finish_assistant_stream();
            status_ = TuiRuntimeStatus::Thinking;
            add_chat_line("agent", "thinking with " + config_.provider + " (" + std::to_string(config_.max_loops) + " max steps)");
            render();
            runner.set_observer(AgentRunObserver{
                [&](const SessionEvent& event) {
                    if (event.type == SessionEventType::UserInput ||
                        event.type == SessionEventType::AssistantMessage ||
                        event.type == SessionEventType::ModelCompleted) {
                        return;
                    }
                    if (event.type == SessionEventType::ToolCall) {
                        status_ = TuiRuntimeStatus::RunningTool;
                    }
                    add_flow_line(event);
                    render();
                },
                [&](const std::string& delta) {
                    status_ = TuiRuntimeStatus::Thinking;
                    streamed_assistant = true;
                    append_assistant_delta(delta);
                    render_assistant_delta(delta);
                },
            });
            auto result = runner.run({
                Message{Role::System, coding_agent_system_prompt(workspace), {}},
                Message{Role::User, line, {}},
            });

            if (result.ok() && !streamed_assistant) {
                status_ = TuiRuntimeStatus::Done;
                transcript_.add_assistant_done(result.output);
                return;
            }
            if (result.ok()) {
                status_ = TuiRuntimeStatus::Done;
                finish_streaming_assistant_cell();
                render_finished_streaming_assistant();
                return;
            }
            status_ = TuiRuntimeStatus::Error;
            add_error_message(result.error);
        } catch (const std::exception& error) {
            status_ = TuiRuntimeStatus::Error;
            add_error_message(error.what());
        }
    }

    static ToolRegistry make_tool_registry(const Workspace& workspace) {
        ToolRegistry registry;
        registry.register_tool(std::make_unique<WorkspaceInfoTool>(workspace));
        registry.register_tool(std::make_unique<ListDirTool>(workspace));
        registry.register_tool(std::make_unique<ReadFileTool>(workspace));
        registry.register_tool(std::make_unique<GlobFilesTool>(workspace));
        registry.register_tool(std::make_unique<SearchTextTool>(workspace));
        registry.register_tool(std::make_unique<WriteFileTool>(workspace));
        registry.register_tool(std::make_unique<EditFileTool>(workspace));
        registry.register_tool(std::make_unique<ShellTool>(workspace));
        return registry;
    }

    void render() {
        sync_global_interrupt();
        const auto status = runtime_status_label();
        *output_ << "\n" << ansi("1;38;5;81") << "Agent TUI" << ansi("0")
                 << "  " << ansi(status_color()) << "[" << status << "]" << ansi("0") << "\n";
        *output_ << ansi("38;5;245") << "Provider" << ansi("0") << "  " << config_.provider
                 << "    " << ansi("38;5;245") << "Model" << ansi("0") << "  " << config_.model << "\n";
        *output_ << ansi("38;5;245") << "API" << ansi("0") << "       " << (config_.api_base.empty() ? "<not set>" : config_.api_base)
                 << "    " << ansi("38;5;245") << "Key" << ansi("0") << "  " << config_.api_key_status() << "\n\n";
        *output_ << ansi("1;38;5;252") << "Transcript" << ansi("0") << "\n";
        if (transcript_.empty()) {
            *output_ << "  " << ansi("38;5;245") << "No messages yet. Type a prompt or /help." << ansi("0") << "\n";
        } else {
            for (const auto& line : transcript_.render_lines(94, 12)) {
                *output_ << line << "\n";
            }
        }
        *output_ << "\n" << ansi("38;5;245") << "Commands" << ansi("0")
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
        out << "status: " << runtime_status_label() << '\n';
        out << config_.summary();
        out << "workspace: " << workspace_.generic_string() << '\n';
        out << "history_events: " << history_.size();
        add_system_message(out.str());
    }

    std::string runtime_status_label() const {
        if (interrupted_) {
            return "INTERRUPTED";
        }
        switch (status_) {
            case TuiRuntimeStatus::Idle:
                return "IDLE";
            case TuiRuntimeStatus::Thinking:
                return "THINKING";
            case TuiRuntimeStatus::WaitingApproval:
                return "WAITING_APPROVAL";
            case TuiRuntimeStatus::RunningTool:
                return "RUNNING_TOOL";
            case TuiRuntimeStatus::Done:
                return "DONE";
            case TuiRuntimeStatus::Error:
                return "ERROR";
            case TuiRuntimeStatus::Interrupted:
                return "INTERRUPTED";
        }
        return "IDLE";
    }

    const char* status_color() const {
        if (interrupted_ || status_ == TuiRuntimeStatus::Error) {
            return "1;38;5;203";
        }
        if (status_ == TuiRuntimeStatus::WaitingApproval) {
            return "1;38;5;214";
        }
        if (status_ == TuiRuntimeStatus::RunningTool || status_ == TuiRuntimeStatus::Thinking) {
            return "1;38;5;220";
        }
        return "1;38;5;114";
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
        transcript_.add_system(message);
        history_.add(SessionEvent::assistant_message(message));
    }

    void add_error_message(const std::string& message) {
        transcript_.add_error(message);
        history_.add(SessionEvent::error(message));
    }

    void add_chat_line(const std::string& role, const std::string& message) {
        if (role == "user") {
            transcript_.add_user(message);
            return;
        }
        if (role == "assistant") {
            transcript_.add_assistant_done(message);
            return;
        }
        if (role == "agent") {
            transcript_.add_agent(message);
            return;
        }
        if (role == "error") {
            transcript_.add_error(message);
            return;
        }
        transcript_.add_system(message);
    }

    void add_flow_line(const SessionEvent& event) {
        switch (event.type) {
            case SessionEventType::ToolCall:
                transcript_.add_tool_call(event.tool_name, summarize_arguments(event.arguments));
                break;
            case SessionEventType::PermissionRequested:
                transcript_.add_approval_required(event.tool_name, summarize_arguments(event.arguments));
                break;
            case SessionEventType::PermissionDenied:
                transcript_.add_approval_denied(event.tool_name, truncate_text(event.content, 120));
                break;
            case SessionEventType::UserFeedback:
                transcript_.add_approval_feedback(event.tool_name, truncate_text(event.content, 120));
                break;
            case SessionEventType::ToolResult:
                transcript_.add_tool_result(event.tool_name, summarize_tool_result(event.content));
                break;
            case SessionEventType::ModelStarted:
                transcript_.add_agent("model request started");
                break;
            case SessionEventType::ModelCompleted:
                break;
            case SessionEventType::ToolStarted:
                transcript_.add_agent("running " + event.tool_name);
                break;
            case SessionEventType::ToolCompleted:
                transcript_.add_agent("completed " + event.tool_name);
                break;
            case SessionEventType::Interrupted:
                transcript_.add_error(event.content);
                break;
            case SessionEventType::Error:
                transcript_.add_error(event.content);
                break;
            case SessionEventType::UserInput:
            case SessionEventType::AssistantMessage:
                break;
        }
    }

    void append_assistant_delta(const std::string& delta) {
        transcript_.append_assistant_delta(delta);
    }

    void finish_streaming_assistant_cell() {
        transcript_.finish_assistant_stream();
    }

    void render_assistant_delta(const std::string& delta) {
        if (!streaming_line_open_) {
            *output_ << "\n  " << ansi("38;5;114") << "assistant streaming" << ansi("0") << " > ";
            streaming_line_open_ = true;
        }
        *output_ << delta << std::flush;
    }

    void render_finished_streaming_assistant() {
        if (streaming_line_open_) {
            *output_ << "\n";
            streaming_line_open_ = false;
        }
        if (transcript_.size() == 0) {
            return;
        }
        for (const auto& line : transcript_.render_cell_lines(transcript_.size() - 1, 94)) {
            *output_ << line << "\n";
        }
    }

    static constexpr std::size_t npos() {
        return static_cast<std::size_t>(-1);
    }

    static std::string summarize_arguments(const JsonLike& arguments) {
        if (arguments.empty()) {
            return "";
        }
        std::ostringstream out;
        out << "(";
        bool first = true;
        for (const auto& [key, value] : arguments) {
            if (!first) {
                out << ", ";
            }
            first = false;
            out << key << "=" << truncate_text(value, 48);
        }
        out << ")";
        return out.str();
    }

    static std::string summarize_tool_result(const std::string& content) {
        auto summary = content;
        const auto newline = summary.find('\n');
        if (newline != std::string::npos) {
            summary = summary.substr(0, newline);
        }
        if (summary.empty()) {
            summary = "<empty result>";
        }
        return truncate_text(summary, 120);
    }

    static std::string truncate_text(const std::string& value, std::size_t max_size) {
        if (value.size() <= max_size) {
            return value;
        }
        return value.substr(0, max_size) + "...";
    }

    std::string ansi(const char* code) const {
        return color_enabled_ ? "\x1b[" + std::string(code) + "m" : "";
    }

    static std::string trim_copy(std::string value) {
        auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) { return !is_space(static_cast<unsigned char>(ch)); }));
        value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch) { return !is_space(static_cast<unsigned char>(ch)); }).base(), value.end());
        return value;
    }

    static std::string lower_ascii(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return value;
    }

    inline static volatile std::sig_atomic_t global_interrupted_ = 0;

    std::filesystem::path workspace_;
    std::istream* input_ = &std::cin;
    std::ostream* output_ = &std::cout;
    TuiConfig config_;
    SessionHistory history_;
    TuiTranscript transcript_;
    bool streaming_line_open_ = false;
    TuiRuntimeStatus status_ = TuiRuntimeStatus::Idle;
    bool running_ = true;
    bool interrupted_ = false;
    bool color_enabled_ = true;
};

}  // namespace agent_tui

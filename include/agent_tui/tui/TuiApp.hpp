#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
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
#include "agent_tui/trace/TraceAdapter.hpp"

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
    if (input.empty()) return input;
    const int wide_size = MultiByteToWideChar(code_page, 0, input.data(), (int)input.size(), nullptr, 0);
    if (wide_size <= 0) return input;
    std::wstring wide((size_t)wide_size, L'\0');
    const int converted = MultiByteToWideChar(code_page, 0, input.data(), (int)input.size(), wide.data(), wide_size);
    if (converted <= 0) return input;
    const int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), converted, nullptr, 0, nullptr, nullptr);
    if (utf8_size <= 0) return input;
    std::string utf8((size_t)utf8_size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), converted, utf8.data(), utf8_size, nullptr, nullptr);
    return utf8;
}

inline bool is_interactive_stdin(const std::istream* input) {
    return input == &std::cin && _isatty(_fileno(stdin)) != 0;
}

inline void configure_console_output() { SetConsoleOutputCP(CP_UTF8); }
#else
inline std::string decode_code_page(const std::string& input, unsigned int) { return input; }
inline bool is_interactive_stdin(const std::istream*) { return false; }
inline void configure_console_output() {}
#endif

} // namespace tui_detail

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

            if (line.empty()) {
                render_prompt_only();
                continue;
            }

            if (line[0] == '/') {
                handle_command(line);
            } else {
                handle_user_input(line);
            }

            if (running_) render();
        }
        return 0;
    }

    bool handle_command(const std::string& line) {
        std::istringstream input(line);
        std::string command;
        input >> command;

        if (command == "/help") { show_help(); return true; }
        if (command == "/exit" || command == "/quit") { running_ = false; return true; }
        if (command == "/status") { show_status(); return true; }
        if (command == "/clear") { history_.clear(); return true; }
        if (command == "/model") { input >> config_.model; return true; }
        if (command == "/api") { handle_api_command(input); return true; }
        if (command == "/config") { handle_config_command(input); return true; }
        if (command == "/interrupt") { interrupted_ = true; return true; }
        if (command == "/compact") { compact_history(); return true; }

        add_system_message("unknown command");
        return false;
    }

    const TuiConfig& config() const { return config_; }
    const SessionHistory& history() const { return history_; }
    bool interrupted() const { return interrupted_; }
    bool running() const { return running_; }

    void request_interrupt() { interrupted_ = true; }

private:
    // ---------------- context control ----------------

    double context_usage() const {
        if (config_.max_loops <= 0) return 0.0;
        return (double)history_.size() / (double)config_.max_loops;
    }

    void compact_history() {
        std::vector<SessionEvent> keep;
        for (const auto& e : history_.events()) {
            if (e.type == SessionEventType::UserInput ||
                e.type == SessionEventType::ToolCall ||
                e.type == SessionEventType::ToolCompleted ||
                e.type == SessionEventType::AssistantMessage ||
                e.type == SessionEventType::Error) {
                keep.push_back(e);
            }
        }
        history_.clear();
        for (auto& e : keep) history_.add(std::move(e));
    }

private:
    void handle_user_input(const std::string& line) {
        history_.add(SessionEvent::user_input(line));

        if (context_usage() >= 0.9) {
            compact_history();
            add_system_message("auto context compact triggered");
        }

        run_agent_loop(line);
    }

    void run_agent_loop(const std::string& line) {
        try {
            Workspace workspace(workspace_);
            auto provider = ProviderFactory::create(config_);
            auto registry = make_tool_registry(workspace);
            TuiApprovalService approval(*input_, *output_);

            TraceAdapter trace_; // ✅ added trace system

            AgentRunner runner(*provider, registry, approval, history_, config_.max_loops);

            runner.set_observer(AgentRunObserver{
                [&](const SessionEvent& event) {
                    trace_.on_event(event); // ✅ trace hook

                    if (event.type == SessionEventType::ToolCall) {
                        status_ = TuiRuntimeStatus::RunningTool;
                        current_step_ = "planning tool call: " + event.tool_name;
                    } else if (event.type == SessionEventType::ToolStarted) {
                        status_ = TuiRuntimeStatus::RunningTool;
                        current_step_ = "running tool: " + event.tool_name;
                    } else if (event.type == SessionEventType::ToolCompleted) {
                        current_step_ = "completed tool: " + event.tool_name;
                    } else if (event.type == SessionEventType::ModelStarted) {
                        status_ = TuiRuntimeStatus::Thinking;
                        current_step_ = "waiting for model response";
                    }

                    add_flow_line(event);
                    render();
                },
                [&](const std::string& delta) {
                    status_ = TuiRuntimeStatus::Thinking;
                }
            });

            auto result = runner.run({
                Message{Role::System, "You are agent_tui coding agent.", {}},
                Message{Role::User, line, {}}
            });

            if (result.ok()) add_system_message(result.output);
            else add_error_message(result.error);

            // optional: show trace dump
            if (output_) {
                *output_ << "\n=== TRACE ===\n";
                for (const auto& t : trace_.items()) {
                    *output_ << "[" << t.type << "] " << t.name;
                    if (!t.data.empty()) *output_ << " : " << t.data;
                    *output_ << "\n";
                }
            }

        } catch (const std::exception& e) {
            add_error_message(e.what());
        }
    }

    void show_status() {
        std::ostringstream out;
        out << "status: OK\n";
        out << "history: " << history_.size() << "\n";
        out << "context: " << (context_usage() * 100.0) << "%\n";
        add_system_message(out.str());
    }

    void show_help() {}
    void handle_api_command(std::istringstream&) {}
    void handle_config_command(std::istringstream&) {}

    void add_system_message(const std::string&) {}
    void add_error_message(const std::string&) {}
    void render() {}
    void render_prompt_only() {}
    void install_signal_handler() {}
    void initialize_project_state() {}
    void sync_global_interrupt() {}
    std::string normalize_input_line(const std::string& s) const { return s; }

private:
    std::filesystem::path workspace_;
    TuiConfig config_;
    SessionHistory history_;

    std::istream* input_ = &std::cin;
    std::ostream* output_ = &std::cout;

    bool running_ = true;
    bool interrupted_ = false;

    TraceAdapter trace_; // optional global trace
};

} // namespace agent_tui

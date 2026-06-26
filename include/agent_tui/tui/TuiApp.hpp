#pragma once

#include <csignal>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "agent_tui/session/SessionHistory.hpp"
#include "agent_tui/tui/TuiConfig.hpp"

namespace agent_tui {

class TuiApp {
public:
    int run() {
        install_signal_handler();
        add_system_message("Welcome to agent_tui. Type /help for commands.");
        render();

        std::string line;
        while (running_ && std::getline(*input_, line)) {
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
                history_.add(SessionEvent::user_input(line));
                add_chat_line("user", line);
                add_system_message("AgentLoop is not wired yet. This input was recorded in SessionHistory.");
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

    void render() {
        sync_global_interrupt();
        *output_ << "\n";
        *output_ << "============================================================\n";
        *output_ << " agent_tui  |  status: " << (interrupted_ ? "INTERRUPTED" : "IDLE") << "\n";
        *output_ << "------------------------------------------------------------\n";
        *output_ << " provider=" << config_.provider
                 << " model=" << config_.model
                 << " api_base=" << (config_.api_base.empty() ? "<not set>" : config_.api_base)
                 << " key_env=" << (config_.api_key_env.empty() ? "<not set>" : config_.api_key_env)
                 << "\n";
        *output_ << "------------------------------------------------------------\n";
        if (chat_lines_.empty()) {
            *output_ << " Chat: <empty>\n";
        } else {
            *output_ << " Chat:\n";
            const std::size_t start = chat_lines_.size() > 12 ? chat_lines_.size() - 12 : 0;
            for (std::size_t i = start; i < chat_lines_.size(); ++i) {
                *output_ << "  " << chat_lines_[i] << "\n";
            }
        }
        *output_ << "------------------------------------------------------------\n";
        *output_ << " Commands: /help /status /clear /model /api /interrupt /skills /exit\n";
        *output_ << "> ";
    }

    void render_prompt_only() {
        *output_ << "> ";
    }

    void show_help() {
        add_system_message(
            "commands:\n"
            "  /help                         show help\n"
            "  /status                       show runtime status\n"
            "  /clear                        clear chat and session history\n"
            "  /model [name]                 show or set model\n"
            "  /api                          show api config\n"
            "  /api provider <name>          set provider\n"
            "  /api base <url>               set API base URL\n"
            "  /api key-env <ENV_NAME>       set API key environment variable name\n"
            "  /api timeout <seconds>        set timeout\n"
            "  /api max-loops <n>            set max agent loops\n"
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

    void add_chat_line(const std::string& role, const std::string& message) {
        chat_lines_.push_back(role + ": " + message);
    }

    inline static volatile std::sig_atomic_t global_interrupted_ = 0;

    std::istream* input_ = &std::cin;
    std::ostream* output_ = &std::cout;
    TuiConfig config_;
    SessionHistory history_;
    std::vector<std::string> chat_lines_;
    bool running_ = true;
    bool interrupted_ = false;
};

}  // namespace agent_tui

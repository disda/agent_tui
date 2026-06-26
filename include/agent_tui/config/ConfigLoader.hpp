#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "agent_tui/config/Config.hpp"

namespace agent_tui {

class ConfigLoader {
public:
    static std::filesystem::path user_config_path() {
        if (const char* override_home = std::getenv("AGENT_TUI_HOME")) {
            if (std::string(override_home).size() > 0) {
                return std::filesystem::path{override_home} / "config.toml";
            }
        }

#ifdef _WIN32
        const char* home = std::getenv("USERPROFILE");
#else
        const char* home = std::getenv("HOME");
#endif
        if (home == nullptr || std::string(home).empty()) {
            return std::filesystem::path{".agent_tui"} / "config.toml";
        }
        return std::filesystem::path{home} / ".agent_tui" / "config.toml";
    }

    static std::filesystem::path project_config_path(const std::filesystem::path& workspace = std::filesystem::current_path()) {
        return workspace / ".agent_tui" / "config.toml";
    }

    static std::filesystem::path project_state_path(const std::filesystem::path& workspace = std::filesystem::current_path()) {
        return workspace / ".agent_tui";
    }

    static std::filesystem::path project_sessions_path(const std::filesystem::path& workspace = std::filesystem::current_path()) {
        return project_state_path(workspace) / "sessions";
    }

    static Config load(const std::filesystem::path& workspace = std::filesystem::current_path()) {
        return load_from_paths(user_config_path(), project_config_path(workspace));
    }

    static Config load_from_paths(const std::filesystem::path& user_path, const std::filesystem::path& project_path) {
        Config config;
        apply_file_if_exists(config, user_path);
        apply_file_if_exists(config, project_path);
        return config;
    }

    static bool init_user_config(bool overwrite = false) {
        return write_example(user_config_path(), overwrite);
    }

    static bool init_project_config(const std::filesystem::path& workspace = std::filesystem::current_path(), bool overwrite = false) {
        return write_example(project_config_path(workspace), overwrite);
    }

    static bool ensure_project_state(const std::filesystem::path& workspace = std::filesystem::current_path()) {
        std::filesystem::create_directories(project_sessions_path(workspace));
        return std::filesystem::exists(project_state_path(workspace)) &&
               std::filesystem::exists(project_sessions_path(workspace));
    }

    static bool write_example(const std::filesystem::path& path, bool overwrite = false) {
        if (std::filesystem::exists(path) && !overwrite) {
            return true;
        }
        const auto parent = path.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }
        output << Config::example_toml();
        return static_cast<bool>(output);
    }

private:
    static void apply_file_if_exists(Config& config, const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) {
            return;
        }
        std::ifstream input(path);
        if (!input) {
            return;
        }
        std::string line;
        while (std::getline(input, line)) {
            apply_line(config, line);
        }
    }

    static void apply_line(Config& config, const std::string& line) {
        auto stripped = trim(strip_comment(line));
        if (stripped.empty()) {
            return;
        }
        const auto eq = stripped.find('=');
        if (eq == std::string::npos) {
            return;
        }
        const auto key = trim(stripped.substr(0, eq));
        const auto value = unquote(trim(stripped.substr(eq + 1)));

        if (key == "provider") {
            config.provider = value;
            return;
        }
        if (key == "model") {
            config.model = value;
            return;
        }
        if (key == "api_base") {
            config.api_base = value;
            return;
        }
        if (key == "api_key") {
            config.api_key = value;
            return;
        }
        if (key == "api_key_env") {
            config.api_key_env = value;
            return;
        }
        if (key == "timeout_seconds") {
            config.timeout_seconds = parse_positive_int(value, config.timeout_seconds);
            return;
        }
        if (key == "max_loops") {
            config.max_loops = parse_positive_int(value, config.max_loops);
            return;
        }
    }

    static std::string strip_comment(const std::string& line) {
        bool in_quote = false;
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '"') {
                in_quote = !in_quote;
            }
            if (!in_quote && line[i] == '#') {
                return line.substr(0, i);
            }
        }
        return line;
    }

    static std::string trim(std::string value) {
        auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) { return !is_space(static_cast<unsigned char>(ch)); }));
        value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch) { return !is_space(static_cast<unsigned char>(ch)); }).base(), value.end());
        return value;
    }

    static std::string unquote(const std::string& value) {
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            return value.substr(1, value.size() - 2);
        }
        return value;
    }

    static int parse_positive_int(const std::string& value, int fallback) {
        try {
            const int parsed = std::stoi(value);
            return parsed > 0 ? parsed : fallback;
        } catch (...) {
            return fallback;
        }
    }
};

}  // namespace agent_tui

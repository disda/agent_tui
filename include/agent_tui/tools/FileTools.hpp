#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "agent_tui/tools/Tool.hpp"
#include "agent_tui/workspace/Workspace.hpp"

namespace agent_tui {

namespace file_tools_detail {

inline std::string get_arg(const JsonLike& arguments, const std::string& key, const std::string& fallback = {}) {
    const auto it = arguments.find(key);
    return it == arguments.end() ? fallback : it->second;
}

inline std::size_t get_size_arg(const JsonLike& arguments, const std::string& key, std::size_t fallback) {
    const auto it = arguments.find(key);
    if (it == arguments.end() || it->second.empty()) {
        return fallback;
    }
    try {
        return static_cast<std::size_t>(std::stoull(it->second));
    } catch (...) {
        return fallback;
    }
}

inline bool get_bool_arg(const JsonLike& arguments, const std::string& key, bool fallback = false) {
    const auto value = get_arg(arguments, key, fallback ? "true" : "false");
    return value == "true" || value == "1" || value == "yes" || value == "on";
}

inline bool should_skip_dir(const std::filesystem::path& path) {
    const auto name = path.filename().string();
    return name == ".git" || name == "build" || name.rfind("cmake-build-", 0) == 0;
}

inline std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline std::string glob_to_regex(const std::string& pattern) {
    std::string output = "^";
    for (const char c : pattern) {
        switch (c) {
            case '*':
                output += ".*";
                break;
            case '?':
                output += ".";
                break;
            case '.': case '+': case '(': case ')': case '^': case '$':
            case '|': case '{': case '}': case '[': case ']': case '\\':
                output += '\\';
                output += c;
                break;
            default:
                output += c;
                break;
        }
    }
    output += "$";
    return output;
}

inline std::string truncate(std::string value, std::size_t max_bytes) {
    if (value.size() <= max_bytes) {
        return value;
    }
    value.resize(max_bytes);
    value += "\n[truncated: max_bytes " + std::to_string(max_bytes) + "]";
    return value;
}

inline std::vector<std::string> split_lines(const std::string& content) {
    std::vector<std::string> lines;
    std::istringstream input(content);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

inline std::vector<std::string> load_gitignore(const std::filesystem::path& root) {
    std::ifstream input(root / ".gitignore");
    std::vector<std::string> patterns;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::replace(line.begin(), line.end(), '\\', '/');
        patterns.push_back(line);
    }
    return patterns;
}

inline bool is_gitignored(const std::string& relative, const std::vector<std::string>& patterns) {
    for (const auto& pattern : patterns) {
        if (pattern.empty()) {
            continue;
        }
        if (pattern.back() == '/') {
            const auto dir = pattern.substr(0, pattern.size() - 1);
            if (relative == dir || relative.rfind(dir + "/", 0) == 0 || relative.find("/" + dir + "/") != std::string::npos) {
                return true;
            }
            continue;
        }
        if (relative == pattern || relative.size() > pattern.size() &&
                                   relative.compare(relative.size() - pattern.size(), pattern.size(), pattern) == 0 &&
                                   relative[relative.size() - pattern.size() - 1] == '/') {
            return true;
        }
    }
    return false;
}

inline bool glob_matches(const std::string& pattern, const std::string& relative, const std::string& filename) {
    const std::regex matcher(glob_to_regex(pattern));
    const bool match_full_relative_path = pattern.find('/') != std::string::npos;
    return std::regex_match(match_full_relative_path ? relative : filename, matcher);
}

#ifdef _WIN32
inline std::string quote_shell_arg(const std::string& value) {
    std::string quoted = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += "\"";
    return quoted;
}

inline FILE* open_process(const std::string& command) {
    return _popen(command.c_str(), "r");
}

inline int close_process(FILE* pipe) {
    return _pclose(pipe);
}
#else
inline std::string quote_shell_arg(const std::string& value) {
    std::string quoted = "'";
    for (const char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

inline FILE* open_process(const std::string& command) {
    return popen(command.c_str(), "r");
}

inline int close_process(FILE* pipe) {
    return pclose(pipe);
}
#endif

inline std::string run_command_capture(const std::string& command, int& exit_code) {
    FILE* pipe = open_process(command);
    if (pipe == nullptr) {
        exit_code = -1;
        return {};
    }

    std::string output;
    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    exit_code = close_process(pipe);
    return output;
}

}  // namespace file_tools_detail

class ListDirTool final : public Tool {
public:
    explicit ListDirTool(const Workspace& workspace) : workspace_(workspace) {}

    std::string name() const override { return "list_dir"; }
    std::string description() const override { return "List files and directories under a workspace path."; }
    std::string parameters_schema_json() const override {
        return R"({"type":"object","properties":{"path":{"type":"string","description":"Workspace-relative directory path, defaults to ."}}})";
    }
    PermissionMode permission_mode() const override { return PermissionMode::Auto; }

    ToolResult run(const JsonLike& arguments) override {
        try {
            const auto path = workspace_.resolve(file_tools_detail::get_arg(arguments, "path", "."));
            if (!std::filesystem::is_directory(path)) {
                return ToolResult::failure("not a directory: " + workspace_.display_path(path));
            }

            std::vector<std::string> entries;
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                auto label = entry.path().filename().generic_string();
                if (entry.is_directory()) {
                    label += "/";
                }
                entries.push_back(label);
            }
            std::sort(entries.begin(), entries.end());

            std::ostringstream out;
            out << "path: " << path.generic_string() << '\n';
            for (const auto& entry : entries) {
                out << entry << '\n';
            }
            return ToolResult::success(out.str());
        } catch (const std::exception& error) {
            return ToolResult::failure(error.what());
        }
    }

private:
    const Workspace& workspace_;
};

class WorkspaceInfoTool final : public Tool {
public:
    explicit WorkspaceInfoTool(const Workspace& workspace) : workspace_(workspace) {}

    std::string name() const override { return "workspace_info"; }
    std::string description() const override { return "Return the current workspace root path."; }
    std::string parameters_schema_json() const override {
        return R"({"type":"object","properties":{}})";
    }
    PermissionMode permission_mode() const override { return PermissionMode::Auto; }

    ToolResult run(const JsonLike&) override {
        return ToolResult::success("workspace: " + workspace_.root().generic_string() + "\n");
    }

private:
    const Workspace& workspace_;
};

class ReadFileTool final : public Tool {
public:
    explicit ReadFileTool(const Workspace& workspace) : workspace_(workspace) {}

    std::string name() const override { return "read_file"; }
    std::string description() const override { return "Read a UTF-8 text file from the workspace."; }
    std::string parameters_schema_json() const override {
        return R"({"type":"object","properties":{"path":{"type":"string","description":"Workspace-relative file path"},"offset":{"type":"string","description":"1-based starting line, defaults to 1"},"limit":{"type":"string","description":"Maximum number of lines to return"},"max_bytes":{"type":"string","description":"Maximum bytes to return"}},"required":["path"]})";
    }
    PermissionMode permission_mode() const override { return PermissionMode::Auto; }

    ToolResult run(const JsonLike& arguments) override {
        try {
            const auto requested_path = file_tools_detail::get_arg(arguments, "path");
            if (requested_path.empty()) {
                return ToolResult::failure("missing required argument: path");
            }
            const auto path = workspace_.resolve(requested_path);
            if (!std::filesystem::is_regular_file(path)) {
                return ToolResult::failure("not a regular file: " + workspace_.display_path(path));
            }

            const auto max_bytes = file_tools_detail::get_size_arg(arguments, "max_bytes", 64 * 1024);
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                return ToolResult::failure("failed to open file: " + workspace_.display_path(path));
            }
            std::ostringstream buffer;
            buffer << input.rdbuf();
            const auto content = buffer.str();
            const auto offset = file_tools_detail::get_size_arg(arguments, "offset", 1);
            const auto limit = file_tools_detail::get_size_arg(arguments, "limit", 0);
            if (offset <= 1 && limit == 0) {
                return ToolResult::success(file_tools_detail::truncate(content, max_bytes));
            }

            const auto lines = file_tools_detail::split_lines(content);
            const auto start_line = (std::max<std::size_t>)(1, offset);
            const auto start_index = (std::min)(start_line - 1, lines.size());
            const auto end_index = limit == 0 ? lines.size() : (std::min)(lines.size(), start_index + limit);

            std::ostringstream out;
            for (std::size_t i = start_index; i < end_index; ++i) {
                out << lines[i];
                if (i + 1 < end_index) {
                    out << '\n';
                }
            }
            if (start_index > 0 || end_index < lines.size()) {
                if (out.tellp() > 0) {
                    out << '\n';
                }
                out << "[truncated: showing lines " << start_line << '-' << end_index << " of " << lines.size() << ']';
            }
            return ToolResult::success(file_tools_detail::truncate(out.str(), max_bytes));
        } catch (const std::exception& error) {
            return ToolResult::failure(error.what());
        }
    }

private:
    const Workspace& workspace_;
};

class GlobFilesTool final : public Tool {
public:
    explicit GlobFilesTool(const Workspace& workspace) : workspace_(workspace) {}

    std::string name() const override { return "glob_files"; }
    std::string description() const override { return "Find workspace files matching a glob pattern."; }
    std::string parameters_schema_json() const override {
        return R"({"type":"object","properties":{"pattern":{"type":"string","description":"Glob pattern such as *.md or src/**/*.cpp"},"path":{"type":"string","description":"Workspace-relative directory to search, defaults to ."},"limit":{"type":"string","description":"Maximum matches to return"},"max_matches":{"type":"string","description":"Legacy alias for limit"}},"required":["pattern"]})";
    }
    PermissionMode permission_mode() const override { return PermissionMode::Auto; }

    ToolResult run(const JsonLike& arguments) override {
        try {
            const auto pattern = file_tools_detail::get_arg(arguments, "pattern");
            if (pattern.empty()) {
                return ToolResult::failure("missing required argument: pattern");
            }
            const auto start = workspace_.resolve(file_tools_detail::get_arg(arguments, "path", "."));
            const auto max_matches = file_tools_detail::get_size_arg(
                arguments, "limit", file_tools_detail::get_size_arg(arguments, "max_matches", 100));
            const auto ignored = file_tools_detail::load_gitignore(workspace_.root());

            std::vector<std::string> matches;
            std::filesystem::recursive_directory_iterator it(start);
            const std::filesystem::recursive_directory_iterator end;
            for (; it != end; ++it) {
                const auto relative = workspace_.display_path(it->path());
                if (it->is_directory() && file_tools_detail::should_skip_dir(it->path())) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (file_tools_detail::is_gitignored(relative, ignored)) {
                    if (it->is_directory()) {
                        it.disable_recursion_pending();
                    }
                    continue;
                }
                if (!it->is_regular_file()) {
                    continue;
                }
                if (file_tools_detail::glob_matches(pattern, relative, it->path().filename().generic_string())) {
                    matches.push_back(relative);
                }
            }
            std::sort(matches.begin(), matches.end());
            const bool truncated = matches.size() > max_matches;
            if (truncated) {
                matches.resize(max_matches);
            }

            std::ostringstream out;
            for (const auto& match : matches) {
                out << match << '\n';
            }
            if (truncated) {
                out << "[truncated: match limit " << max_matches << " reached]\n";
            }
            return ToolResult::success(out.str());
        } catch (const std::exception& error) {
            return ToolResult::failure(error.what());
        }
    }

private:
    const Workspace& workspace_;
};

class SearchTextTool final : public Tool {
public:
    explicit SearchTextTool(const Workspace& workspace) : workspace_(workspace) {}

    std::string name() const override { return "search_text"; }
    std::string description() const override { return "Search text in workspace files."; }
    std::string parameters_schema_json() const override {
        return R"({"type":"object","properties":{"query":{"type":"string","description":"Text query or regex to search for"},"path":{"type":"string","description":"Workspace-relative directory or file to search, defaults to ."},"glob":{"type":"string","description":"Only scan files matching this glob"},"ignoreCase":{"type":"string","description":"true for case-insensitive matching"},"literal":{"type":"string","description":"true to treat query as literal text instead of regex"},"limit":{"type":"string","description":"Maximum matches to return"},"max_matches":{"type":"string","description":"Legacy alias for limit"},"max_file_bytes":{"type":"string","description":"Maximum bytes to scan per file"}},"required":["query"]})";
    }
    PermissionMode permission_mode() const override { return PermissionMode::Auto; }

    ToolResult run(const JsonLike& arguments) override {
        try {
            const auto query = file_tools_detail::get_arg(arguments, "query");
            if (query.empty()) {
                return ToolResult::failure("missing required argument: query");
            }
            const auto start = workspace_.resolve(file_tools_detail::get_arg(arguments, "path", "."));
            const auto max_matches = file_tools_detail::get_size_arg(
                arguments, "limit", file_tools_detail::get_size_arg(arguments, "max_matches", 50));
            const auto max_file_bytes = file_tools_detail::get_size_arg(arguments, "max_file_bytes", 1024 * 1024);
            const auto glob = file_tools_detail::get_arg(arguments, "glob");
            const auto ignore_case = file_tools_detail::get_bool_arg(arguments, "ignoreCase", false);
            const auto literal = file_tools_detail::get_bool_arg(arguments, "literal", false);
            const auto search_path = workspace_.display_path(start);
            const auto rg_path = file_tools_detail::get_arg(arguments, "rg_path", "rg");

            std::ostringstream command;
#ifdef _WIN32
            command << "cmd /C \"cd /d " << file_tools_detail::quote_shell_arg(workspace_.root().string()) << " && ";
#else
            command << "cd " << file_tools_detail::quote_shell_arg(workspace_.root().string()) << " && ";
#endif
            command << file_tools_detail::quote_shell_arg(rg_path)
                    << " --line-number --color=never --hidden --no-heading --path-separator /"
                    << " --max-filesize " << max_file_bytes
                    << " --glob " << file_tools_detail::quote_shell_arg("!build/**")
                    << " --glob " << file_tools_detail::quote_shell_arg("!cmake-build-*/**")
                    << " --glob " << file_tools_detail::quote_shell_arg("!.git/**");
            if (ignore_case) {
                command << " --ignore-case";
            }
            if (literal) {
                command << " --fixed-strings";
            }
            if (!glob.empty()) {
                command << " --glob " << file_tools_detail::quote_shell_arg(glob);
            }
            command << " -- " << file_tools_detail::quote_shell_arg(query)
                    << " " << file_tools_detail::quote_shell_arg(search_path)
                    << " 2>&1";
#ifdef _WIN32
            command << "\"";
#endif

            int exit_code = 0;
            const auto raw_output = file_tools_detail::run_command_capture(command.str(), exit_code);
            if (exit_code == -1) {
                return ToolResult::failure("failed to start rg");
            }

            std::vector<std::string> matches;
            bool truncated = false;
            std::istringstream lines(raw_output);
            std::string line;
            while (std::getline(lines, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.empty()) {
                    continue;
                }
                if (matches.size() >= max_matches) {
                    truncated = true;
                    break;
                }
                matches.push_back(line);
            }

            std::ostringstream out;
            out << "engine: rg\n";
            for (const auto& match : matches) {
                out << match << '\n';
            }
            if (truncated) {
                out << "[truncated: match limit " << max_matches << " reached]\n";
            }
            return ToolResult::success(out.str());
        } catch (const std::exception& error) {
            return ToolResult::failure(error.what());
        }
    }

private:
    const Workspace& workspace_;
};

}  // namespace agent_tui

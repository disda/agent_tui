#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "agent_tui/tools/Tool.hpp"
#include "agent_tui/workspace/Workspace.hpp"

namespace agent_tui {

namespace write_edit_tools_detail {

inline std::string get_arg(const JsonLike& arguments, const std::string& key, const std::string& fallback = {}) {
    const auto it = arguments.find(key);
    return it == arguments.end() ? fallback : it->second;
}

inline bool get_bool_arg(const JsonLike& arguments, const std::string& key, bool fallback = false) {
    const auto value = get_arg(arguments, key, fallback ? "true" : "false");
    return value == "true" || value == "1" || value == "yes" || value == "on";
}

inline bool write_text_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << content;
    return static_cast<bool>(output);
}

inline bool read_text_file(const std::filesystem::path& path, std::string& content) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    content = buffer.str();
    return true;
}

inline std::size_t replace_all(std::string& value, const std::string& old_text, const std::string& new_text) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = value.find(old_text, pos)) != std::string::npos) {
        value.replace(pos, old_text.size(), new_text);
        pos += new_text.size();
        ++count;
    }
    return count;
}

struct ExactEdit {
    std::string old_text;
    std::string new_text;
};

struct LocatedEdit {
    std::size_t position;
    std::size_t old_size;
    std::string new_text;
};

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

inline std::string make_line_diff(const std::string& path, const std::string& before, const std::string& after) {
    const auto before_lines = split_lines(before);
    const auto after_lines = split_lines(after);
    const auto count = (std::max)(before_lines.size(), after_lines.size());

    std::ostringstream out;
    out << "diff:\n";
    out << "--- " << path << '\n';
    out << "+++ " << path << '\n';
    out << "@@\n";
    for (std::size_t i = 0; i < count; ++i) {
        const bool has_before = i < before_lines.size();
        const bool has_after = i < after_lines.size();
        if (has_before && has_after && before_lines[i] == after_lines[i]) {
            out << ' ' << before_lines[i] << '\n';
        } else {
            if (has_before) {
                out << '-' << before_lines[i] << '\n';
            }
            if (has_after) {
                out << '+' << after_lines[i] << '\n';
            }
        }
    }
    return out.str();
}

inline std::vector<ExactEdit> collect_edits(const JsonLike& arguments) {
    std::vector<ExactEdit> edits;
    const auto legacy_old = get_arg(arguments, "old_text");
    if (!legacy_old.empty()) {
        edits.push_back({legacy_old, get_arg(arguments, "new_text")});
    }

    for (int i = 1; i <= 50; ++i) {
        const auto old_key = "old_text_" + std::to_string(i);
        const auto new_key = "new_text_" + std::to_string(i);
        const auto old_it = arguments.find(old_key);
        const auto new_it = arguments.find(new_key);
        if (old_it == arguments.end() && new_it == arguments.end()) {
            continue;
        }
        if (old_it == arguments.end() || old_it->second.empty()) {
            continue;
        }
        edits.push_back({old_it->second, new_it == arguments.end() ? std::string{} : new_it->second});
    }
    return edits;
}

}  // namespace write_edit_tools_detail

class WriteFileTool final : public Tool {
public:
    explicit WriteFileTool(const Workspace& workspace) : workspace_(workspace) {}

    std::string name() const override { return "write_file"; }
    std::string description() const override { return "Write a UTF-8 text file inside the workspace."; }
    std::string parameters_schema_json() const override {
        return R"({"type":"object","properties":{"path":{"type":"string","description":"Workspace-relative file path"},"content":{"type":"string","description":"Content to write"},"create_parent_dirs":{"type":"string","description":"true to create parent directories"}},"required":["path","content"]})";
    }
    PermissionMode permission_mode() const override { return PermissionMode::Confirm; }

    ToolResult run(const JsonLike& arguments) override {
        const auto requested_path = write_edit_tools_detail::get_arg(arguments, "path");
        if (requested_path.empty()) {
            return ToolResult::failure("missing required argument: path");
        }

        const auto content = write_edit_tools_detail::get_arg(arguments, "content");
        const auto create_parent_dirs = write_edit_tools_detail::get_bool_arg(arguments, "create_parent_dirs", false);

        std::filesystem::path path;
        try {
            path = workspace_.resolve(requested_path);
        } catch (const std::exception& error) {
            return ToolResult::failure(error.what());
        }

        if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
            return ToolResult::failure("target path is a directory: " + workspace_.display_path(path));
        }

        const auto parent = path.parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent)) {
            if (!create_parent_dirs) {
                return ToolResult::failure("parent directory does not exist: " + parent.generic_string());
            }
            std::filesystem::create_directories(parent);
        }

        if (!write_edit_tools_detail::write_text_file(path, content)) {
            return ToolResult::failure("failed to write file: " + workspace_.display_path(path));
        }

        std::ostringstream out;
        out << "wrote file: " << workspace_.display_path(path) << '\n';
        out << "bytes: " << content.size() << '\n';
        return ToolResult::success(out.str());
    }

private:
    const Workspace& workspace_;
};

class EditFileTool final : public Tool {
public:
    explicit EditFileTool(const Workspace& workspace) : workspace_(workspace) {}

    std::string name() const override { return "edit_file"; }
    std::string description() const override { return "Replace text inside a workspace file."; }
    std::string parameters_schema_json() const override {
        return R"({"type":"object","properties":{"path":{"type":"string","description":"Workspace-relative file path"},"old_text":{"type":"string","description":"Legacy exact text to replace"},"new_text":{"type":"string","description":"Legacy replacement text"},"replace_all":{"type":"string","description":"true to replace all legacy old_text matches"},"old_text_1":{"type":"string","description":"Exact text for first replacement"},"new_text_1":{"type":"string","description":"Replacement text for first replacement"},"old_text_2":{"type":"string","description":"Exact text for second replacement"},"new_text_2":{"type":"string","description":"Replacement text for second replacement"}},"required":["path"]})";
    }
    PermissionMode permission_mode() const override { return PermissionMode::Confirm; }

    ToolResult run(const JsonLike& arguments) override {
        const auto requested_path = write_edit_tools_detail::get_arg(arguments, "path");
        if (requested_path.empty()) {
            return ToolResult::failure("missing required argument: path");
        }

        const auto edits = write_edit_tools_detail::collect_edits(arguments);
        if (edits.empty()) {
            return ToolResult::failure("missing required argument: old_text or old_text_N");
        }
        const auto replace_all_matches = write_edit_tools_detail::get_bool_arg(arguments, "replace_all", false);

        std::filesystem::path path;
        try {
            path = workspace_.resolve(requested_path);
        } catch (const std::exception& error) {
            return ToolResult::failure(error.what());
        }

        if (!std::filesystem::is_regular_file(path)) {
            return ToolResult::failure("not a regular file: " + workspace_.display_path(path));
        }

        std::string content;
        if (!write_edit_tools_detail::read_text_file(path, content)) {
            return ToolResult::failure("failed to read file: " + workspace_.display_path(path));
        }

        const auto before = content;
        std::size_t replacements = 0;
        if (edits.size() == 1 && replace_all_matches) {
            replacements = write_edit_tools_detail::replace_all(content, edits[0].old_text, edits[0].new_text);
        } else {
            std::vector<write_edit_tools_detail::LocatedEdit> located;
            for (const auto& edit : edits) {
                const auto pos = before.find(edit.old_text);
                if (pos == std::string::npos) {
                    return ToolResult::failure("old_text not found in file: " + workspace_.display_path(path));
                }
                located.push_back({pos, edit.old_text.size(), edit.new_text});
            }

            std::sort(located.begin(), located.end(), [](const auto& left, const auto& right) {
                return left.position < right.position;
            });
            for (std::size_t i = 1; i < located.size(); ++i) {
                if (located[i].position < located[i - 1].position + located[i - 1].old_size) {
                    return ToolResult::failure("overlapping edits are not allowed: " + workspace_.display_path(path));
                }
            }

            for (auto it = located.rbegin(); it != located.rend(); ++it) {
                content.replace(it->position, it->old_size, it->new_text);
                ++replacements;
            }
        }

        if (replacements == 0) {
            return ToolResult::failure("old_text not found in file: " + workspace_.display_path(path));
        }

        if (!write_edit_tools_detail::write_text_file(path, content)) {
            return ToolResult::failure("failed to write file: " + workspace_.display_path(path));
        }

        std::ostringstream out;
        out << "edited file: " << workspace_.display_path(path) << '\n';
        out << "replacements: " << replacements << '\n';
        out << write_edit_tools_detail::make_line_diff(workspace_.display_path(path), before, content);
        return ToolResult::success(out.str());
    }

private:
    const Workspace& workspace_;
};

}  // namespace agent_tui

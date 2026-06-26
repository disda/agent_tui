#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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

}  // namespace write_edit_tools_detail

class WriteFileTool final : public Tool {
public:
    explicit WriteFileTool(const Workspace& workspace) : workspace_(workspace) {}

    std::string name() const override { return "write_file"; }
    std::string description() const override { return "Write a UTF-8 text file inside the workspace."; }
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
    PermissionMode permission_mode() const override { return PermissionMode::Confirm; }

    ToolResult run(const JsonLike& arguments) override {
        const auto requested_path = write_edit_tools_detail::get_arg(arguments, "path");
        if (requested_path.empty()) {
            return ToolResult::failure("missing required argument: path");
        }

        const auto old_text = write_edit_tools_detail::get_arg(arguments, "old_text");
        if (old_text.empty()) {
            return ToolResult::failure("missing required argument: old_text");
        }

        const auto new_text = write_edit_tools_detail::get_arg(arguments, "new_text");
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

        std::size_t replacements = 0;
        if (replace_all_matches) {
            replacements = write_edit_tools_detail::replace_all(content, old_text, new_text);
        } else {
            const auto pos = content.find(old_text);
            if (pos != std::string::npos) {
                content.replace(pos, old_text.size(), new_text);
                replacements = 1;
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
        return ToolResult::success(out.str());
    }

private:
    const Workspace& workspace_;
};

}  // namespace agent_tui

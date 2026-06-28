#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "agent_tui/filesystem/AllowedRoots.hpp"
#include "agent_tui/tools/Tool.hpp"

namespace agent_tui {

namespace file_manager_detail {

inline std::string get_arg(const JsonLike& arguments, const std::string& key, const std::string& fallback = {}) {
    const auto it = arguments.find(key);
    return it == arguments.end() ? fallback : it->second;
}

inline int get_int_arg(const JsonLike& arguments, const std::string& key, int fallback) {
    const auto it = arguments.find(key);
    if (it == arguments.end() || it->second.empty()) {
        return fallback;
    }
    try {
        return std::stoi(it->second);
    } catch (...) {
        return fallback;
    }
}

inline bool get_bool_arg(const JsonLike& arguments, const std::string& key, bool fallback) {
    auto value = get_arg(arguments, key);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (value == "true" || value == "1" || value == "yes" || value == "y") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "n") {
        return false;
    }
    return fallback;
}

inline std::vector<std::string> split_extensions(std::string value) {
    std::vector<std::string> result;
    std::string current;
    for (char ch : value) {
        if (ch == ',' || ch == ';' || ch == ' ') {
            if (!current.empty()) {
                result.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    if (!current.empty()) {
        result.push_back(current);
    }
    for (auto& ext : result) {
        if (!ext.empty() && ext.front() != '.') {
            ext = "." + ext;
        }
    }
    return result;
}

inline bool extension_matches(const std::filesystem::path& path, const std::vector<std::string>& extensions) {
    auto ext = path.extension().generic_string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
}

inline std::string file_type_label(const std::filesystem::directory_entry& entry) {
    std::error_code ec;
    if (entry.is_directory(ec)) {
        return "dir";
    }
    if (entry.is_regular_file(ec)) {
        return "file";
    }
    return "other";
}

}  // namespace file_manager_detail

class ListPathTool final : public Tool {
public:
    explicit ListPathTool(AllowedRoots roots) : roots_(std::move(roots)) {}

    std::string name() const override { return "list_path"; }
    std::string description() const override { return "List files under an allowed local path such as desktop or downloads."; }
    PermissionMode permission_mode() const override { return PermissionMode::Auto; }

    ToolResult run(const JsonLike& arguments) override {
        const auto path_arg = file_manager_detail::get_arg(arguments, "path", ".");
        const auto max_entries = (std::max)(1, file_manager_detail::get_int_arg(arguments, "max_entries", 100));

        std::filesystem::path path;
        try {
            path = roots_.resolve(path_arg);
        } catch (const std::exception& error) {
            return ToolResult::failure(error.what());
        }

        if (!std::filesystem::exists(path)) {
            return ToolResult::failure("path does not exist: " + path.generic_string());
        }
        if (!std::filesystem::is_directory(path)) {
            return ToolResult::failure("path is not a directory: " + path.generic_string());
        }

        std::vector<std::filesystem::directory_entry> entries;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
            if (ec) {
                return ToolResult::failure("failed to read directory: " + ec.message());
            }
            entries.push_back(entry);
        }
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return a.path().filename().generic_string() < b.path().filename().generic_string();
        });

        std::ostringstream out;
        out << "path: " << path.generic_string() << '\n';
        int count = 0;
        for (const auto& entry : entries) {
            if (count >= max_entries) {
                out << "...[truncated]\n";
                break;
            }
            out << file_manager_detail::file_type_label(entry) << "\t" << entry.path().filename().generic_string() << '\n';
            ++count;
        }
        return ToolResult::success(out.str());
    }

private:
    AllowedRoots roots_;
};

class MakeDirTool final : public Tool {
public:
    explicit MakeDirTool(AllowedRoots roots) : roots_(std::move(roots)) {}

    std::string name() const override { return "make_dir"; }
    std::string description() const override { return "Create a directory under allowed roots."; }
    PermissionMode permission_mode() const override { return PermissionMode::Confirm; }

    ToolResult run(const JsonLike& arguments) override {
        const auto path_arg = file_manager_detail::get_arg(arguments, "path");
        if (path_arg.empty()) {
            return ToolResult::failure("missing required argument: path");
        }

        std::filesystem::path path;
        try {
            path = roots_.resolve(path_arg);
        } catch (const std::exception& error) {
            return ToolResult::failure(error.what());
        }

        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        if (ec) {
            return ToolResult::failure("failed to create directory: " + ec.message());
        }
        return ToolResult::success("created directory: " + path.generic_string());
    }

private:
    AllowedRoots roots_;
};

class MoveFileTool final : public Tool {
public:
    explicit MoveFileTool(AllowedRoots roots) : roots_(std::move(roots)) {}

    std::string name() const override { return "move_file"; }
    std::string description() const override { return "Move a file between allowed roots without overwriting."; }
    PermissionMode permission_mode() const override { return PermissionMode::Confirm; }

    ToolResult run(const JsonLike& arguments) override {
        const auto source_arg = file_manager_detail::get_arg(arguments, "source");
        const auto target_arg = file_manager_detail::get_arg(arguments, "target");
        if (source_arg.empty() || target_arg.empty()) {
            return ToolResult::failure("missing required arguments: source and target");
        }

        std::filesystem::path source;
        std::filesystem::path target;
        try {
            source = roots_.resolve(source_arg);
            target = roots_.resolve(target_arg);
        } catch (const std::exception& error) {
            return ToolResult::failure(error.what());
        }

        if (!std::filesystem::is_regular_file(source)) {
            return ToolResult::failure("source is not a regular file: " + source.generic_string());
        }
        if (std::filesystem::exists(target)) {
            return ToolResult::failure("target already exists: " + target.generic_string());
        }

        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) {
            return ToolResult::failure("failed to create target parent: " + ec.message());
        }
        std::filesystem::rename(source, target, ec);
        if (ec) {
            return ToolResult::failure("failed to move file: " + ec.message());
        }
        return ToolResult::success("moved: " + source.generic_string() + " -> " + target.generic_string());
    }

private:
    AllowedRoots roots_;
};

class MoveFilesByExtensionTool final : public Tool {
public:
    explicit MoveFilesByExtensionTool(AllowedRoots roots) : roots_(std::move(roots)) {}

    std::string name() const override { return "move_files_by_extension"; }
    std::string description() const override { return "Move files with selected extensions from one allowed directory to another. Dry-run by default."; }
    PermissionMode permission_mode() const override { return PermissionMode::Confirm; }

    ToolResult run(const JsonLike& arguments) override {
        const auto source_arg = file_manager_detail::get_arg(arguments, "source_dir");
        const auto target_arg = file_manager_detail::get_arg(arguments, "target_dir");
        const auto extensions_arg = file_manager_detail::get_arg(arguments, "extensions");
        const bool execute = file_manager_detail::get_bool_arg(arguments, "execute", false);

        if (source_arg.empty() || target_arg.empty() || extensions_arg.empty()) {
            return ToolResult::failure("missing required arguments: source_dir, target_dir, extensions");
        }

        const auto extensions = file_manager_detail::split_extensions(extensions_arg);
        if (extensions.empty()) {
            return ToolResult::failure("no extensions provided");
        }

        std::filesystem::path source_dir;
        std::filesystem::path target_dir;
        try {
            source_dir = roots_.resolve(source_arg);
            target_dir = roots_.resolve(target_arg);
        } catch (const std::exception& error) {
            return ToolResult::failure(error.what());
        }

        if (!std::filesystem::is_directory(source_dir)) {
            return ToolResult::failure("source_dir is not a directory: " + source_dir.generic_string());
        }

        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moves;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(source_dir, ec)) {
            if (ec) {
                return ToolResult::failure("failed to read source_dir: " + ec.message());
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            if (!file_manager_detail::extension_matches(entry.path(), extensions)) {
                continue;
            }
            moves.emplace_back(entry.path(), target_dir / entry.path().filename());
        }
        std::sort(moves.begin(), moves.end(), [](const auto& a, const auto& b) {
            return a.first.filename().generic_string() < b.first.filename().generic_string();
        });

        std::ostringstream out;
        out << (execute ? "execute" : "dry_run") << " move_files_by_extension\n";
        out << "source_dir: " << source_dir.generic_string() << '\n';
        out << "target_dir: " << target_dir.generic_string() << '\n';
        out << "count: " << moves.size() << '\n';

        if (!execute) {
            for (const auto& move : moves) {
                out << "plan: " << move.first.filename().generic_string() << " -> " << move.second.generic_string() << '\n';
            }
            return ToolResult::success(out.str());
        }

        std::filesystem::create_directories(target_dir, ec);
        if (ec) {
            return ToolResult::failure("failed to create target_dir: " + ec.message());
        }

        for (const auto& move : moves) {
            if (std::filesystem::exists(move.second)) {
                return ToolResult::failure("target already exists: " + move.second.generic_string());
            }
        }

        for (const auto& move : moves) {
            std::filesystem::rename(move.first, move.second, ec);
            if (ec) {
                return ToolResult::failure("failed to move " + move.first.generic_string() + ": " + ec.message());
            }
            out << "moved: " << move.first.filename().generic_string() << " -> " << move.second.generic_string() << '\n';
        }
        return ToolResult::success(out.str());
    }

private:
    AllowedRoots roots_;
};

}  // namespace agent_tui

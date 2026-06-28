#include "agent_tui/tools/FileTools.hpp"

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
    auto root = std::filesystem::temp_directory_path() / ("agent_tui_file_tools_" + std::to_string(stamp));
    std::filesystem::create_directories(root / "src");
    std::filesystem::create_directories(root / "docs");
    std::filesystem::create_directories(root / "build");

    std::ofstream(root / "README.md") << "# Test Project\nhello workspace\n";
    std::ofstream(root / "src" / "main.cpp") << "int main() { return 0; }\n";
    std::ofstream(root / "docs" / "guide.md") << "needle in guide\nsecond line\n";
    std::ofstream(root / "docs" / "caps.txt") << "Needle with case\nanother needle\n";
    std::ofstream(root / "docs" / "literal.txt") << "a.b literal\naxb regex-ish\n";
    std::ofstream(root / "build" / "ignored.txt") << "needle ignored\n";
    return root;
}

void test_workspace_rejects_path_escape(const std::filesystem::path& root) {
    Workspace workspace(root);
    bool rejected = false;
    try {
        (void)workspace.resolve("../outside.txt");
    } catch (const std::exception&) {
        rejected = true;
    }
    assert(rejected);
}

void test_list_dir(const std::filesystem::path& root) {
    Workspace workspace(root);
    ListDirTool tool(workspace);
    auto result = tool.run({{"path", "."}});
    assert(result.ok);
    assert(result.output.find("path: " + workspace.root().generic_string()) != std::string::npos);
    assert(result.output.find("README.md") != std::string::npos);
    assert(result.output.find("src/") != std::string::npos);
}

void test_workspace_info_reports_root_path(const std::filesystem::path& root) {
    Workspace workspace(root);
    WorkspaceInfoTool tool(workspace);
    auto result = tool.run({});
    assert(result.ok);
    assert(result.output.find("workspace: " + workspace.root().generic_string()) != std::string::npos);
}

void test_read_file(const std::filesystem::path& root) {
    Workspace workspace(root);
    ReadFileTool tool(workspace);
    auto result = tool.run({{"path", "README.md"}});
    assert(result.ok);
    assert(result.output.find("hello workspace") != std::string::npos);
}

void test_read_file_supports_offset_limit_and_truncation(const std::filesystem::path& root) {
    std::ofstream(root / "lines.txt") << "line1\nline2\nline3\nline4\n";

    Workspace workspace(root);
    ReadFileTool tool(workspace);
    auto result = tool.run({{"path", "lines.txt"}, {"offset", "2"}, {"limit", "2"}});

    assert(result.ok);
    assert(result.output.find("line2") != std::string::npos);
    assert(result.output.find("line3") != std::string::npos);
    assert(result.output.find("line1") == std::string::npos);
    assert(result.output.find("line4") == std::string::npos);
    assert(result.output.find("[truncated: showing lines 2-3 of 4]") != std::string::npos);
}

void test_read_file_reports_byte_truncation(const std::filesystem::path& root) {
    Workspace workspace(root);
    ReadFileTool tool(workspace);
    auto result = tool.run({{"path", "README.md"}, {"max_bytes", "8"}});

    assert(result.ok);
    assert(result.output.size() > 8);
    assert(result.output.find("[truncated: max_bytes 8]") != std::string::npos);
}

void test_read_file_rejects_escape(const std::filesystem::path& root) {
    Workspace workspace(root);
    ReadFileTool tool(workspace);
    auto result = tool.run({{"path", "../outside.txt"}});
    assert(!result.ok);
    assert(result.error.find("escapes workspace") != std::string::npos);
}

void test_glob_files(const std::filesystem::path& root) {
    Workspace workspace(root);
    GlobFilesTool tool(workspace);
    auto result = tool.run({{"pattern", "*.md"}});
    assert(result.ok);
    assert(result.output.find("README.md") != std::string::npos);
    assert(result.output.find("docs/guide.md") != std::string::npos);
}

void test_glob_files_honors_limit_and_gitignore(const std::filesystem::path& root) {
    std::ofstream(root / ".gitignore") << "ignored-by-gitignore.txt\nignored-dir/\n";
    std::ofstream(root / "visible.txt") << "visible\n";
    std::ofstream(root / "ignored-by-gitignore.txt") << "ignored\n";
    std::filesystem::create_directories(root / "ignored-dir");
    std::ofstream(root / "ignored-dir" / "also.txt") << "ignored\n";

    Workspace workspace(root);
    GlobFilesTool tool(workspace);
    auto result = tool.run({{"pattern", "*.txt"}, {"path", "."}, {"limit", "20"}});

    assert(result.ok);
    assert(result.output.find("visible.txt") != std::string::npos);
    assert(result.output.find("ignored-by-gitignore.txt") == std::string::npos);
    assert(result.output.find("ignored-dir/also.txt") == std::string::npos);
}

void test_search_text(const std::filesystem::path& root) {
    Workspace workspace(root);
    SearchTextTool tool(workspace);
    auto result = tool.run({{"query", "needle"}, {"path", "."}});
    assert(result.ok);
    assert(result.output.find("engine: rg") != std::string::npos);
    assert(result.output.find("docs/guide.md:1:needle in guide") != std::string::npos);
    assert(result.output.find("ignored.txt") == std::string::npos);
}

void test_search_text_supports_glob_ignore_case_literal_and_limit(const std::filesystem::path& root) {
    Workspace workspace(root);
    SearchTextTool tool(workspace);

    auto case_result = tool.run({
        {"query", "needle"},
        {"path", "docs"},
        {"glob", "*.txt"},
        {"ignoreCase", "true"},
        {"literal", "true"},
        {"limit", "1"},
    });
    assert(case_result.ok);
    assert(case_result.output.find("docs/caps.txt:1:Needle with case") != std::string::npos);
    assert(case_result.output.find("[truncated: match limit 1 reached]") != std::string::npos);

    auto literal_result = tool.run({
        {"query", "a.b"},
        {"path", "docs"},
        {"glob", "literal.txt"},
        {"literal", "true"},
    });
    assert(literal_result.ok);
    assert(literal_result.output.find("docs/literal.txt:1:a.b literal") != std::string::npos);
    assert(literal_result.output.find("docs/literal.txt:2:axb regex-ish") == std::string::npos);
}

}  // namespace

int main() {
    const auto root = make_test_root();
    test_workspace_rejects_path_escape(root);
    test_list_dir(root);
    test_workspace_info_reports_root_path(root);
    test_read_file(root);
    test_read_file_supports_offset_limit_and_truncation(root);
    test_read_file_reports_byte_truncation(root);
    test_read_file_rejects_escape(root);
    test_glob_files(root);
    test_glob_files_honors_limit_and_gitignore(root);
    test_search_text(root);
    test_search_text_supports_glob_ignore_case_literal_and_limit(root);
    std::filesystem::remove_all(root);
    return 0;
}

#include "agent_tui/tools/FileTools.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
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
    assert(result.output.find("README.md") != std::string::npos);
    assert(result.output.find("src/") != std::string::npos);
}

void test_read_file(const std::filesystem::path& root) {
    Workspace workspace(root);
    ReadFileTool tool(workspace);
    auto result = tool.run({{"path", "README.md"}});
    assert(result.ok);
    assert(result.output.find("hello workspace") != std::string::npos);
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

void test_search_text(const std::filesystem::path& root) {
    Workspace workspace(root);
    SearchTextTool tool(workspace);
    auto result = tool.run({{"query", "needle"}, {"path", "."}});
    assert(result.ok);
    assert(result.output.find("docs/guide.md:1:needle in guide") != std::string::npos);
    assert(result.output.find("ignored.txt") == std::string::npos);
}

}  // namespace

int main() {
    const auto root = make_test_root();
    test_workspace_rejects_path_escape(root);
    test_list_dir(root);
    test_read_file(root);
    test_read_file_rejects_escape(root);
    test_glob_files(root);
    test_search_text(root);
    std::filesystem::remove_all(root);
    return 0;
}

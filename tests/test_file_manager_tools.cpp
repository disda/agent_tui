#include "agent_tui/filesystem/AllowedRoots.hpp"
#include "agent_tui/filesystem/KnownPaths.hpp"
#include "agent_tui/tools/FileManagerTools.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <crtdbg.h>
#endif

using namespace agent_tui;

namespace {

std::filesystem::path make_test_root() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto root = std::filesystem::temp_directory_path() / ("agent_tui_file_manager_" + std::to_string(stamp));
    std::filesystem::create_directories(root);
    return root;
}

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

}  // namespace

void test_known_paths_resolve_aliases() {
    assert(!KnownPaths::home().empty());
    assert(KnownPaths::resolve_alias("home") == KnownPaths::home());
    assert(KnownPaths::resolve_alias("desktop") == KnownPaths::desktop());
    assert(KnownPaths::resolve_alias("downloads") == KnownPaths::downloads());
}

void test_allowed_roots_allows_workspace_and_rejects_escape() {
    const auto root = make_test_root();
    AllowedRoots roots = AllowedRoots::workspace_only(root);

    const auto inside = roots.resolve(".");
    assert(roots.is_allowed(inside));

    bool rejected = false;
    try {
        (void)roots.resolve(root.parent_path().generic_string());
    } catch (...) {
        rejected = true;
    }
    assert(rejected);

    std::filesystem::remove_all(root);
}

void test_list_path_lists_files() {
    const auto root = make_test_root();
    write_file(root / "a.txt", "a");
    write_file(root / "b.md", "b");

    ListPathTool tool(AllowedRoots::workspace_only(root));
    const auto result = tool.run({{"path", "."}});

    assert(result.ok);
    assert(result.output.find("a.txt") != std::string::npos);
    assert(result.output.find("b.md") != std::string::npos);

    std::filesystem::remove_all(root);
}

void test_make_dir_creates_directory() {
    const auto root = make_test_root();
    MakeDirTool tool(AllowedRoots::workspace_only(root));

    const auto result = tool.run({{"path", "new-folder/sub"}});

    assert(result.ok);
    assert(std::filesystem::is_directory(root / "new-folder" / "sub"));

    std::filesystem::remove_all(root);
}

void test_move_file_moves_without_overwrite() {
    const auto root = make_test_root();
    write_file(root / "source.txt", "hello");

    MoveFileTool tool(AllowedRoots::workspace_only(root));
    const auto result = tool.run({{"source", "source.txt"}, {"target", "target/source.txt"}});

    assert(result.ok);
    assert(!std::filesystem::exists(root / "source.txt"));
    assert(read_file(root / "target" / "source.txt") == "hello");

    std::filesystem::remove_all(root);
}

void test_move_file_rejects_target_conflict() {
    const auto root = make_test_root();
    write_file(root / "source.txt", "hello");
    write_file(root / "target.txt", "exists");

    MoveFileTool tool(AllowedRoots::workspace_only(root));
    const auto result = tool.run({{"source", "source.txt"}, {"target", "target.txt"}});

    assert(!result.ok);
    assert(std::filesystem::exists(root / "source.txt"));
    assert(read_file(root / "target.txt") == "exists");

    std::filesystem::remove_all(root);
}

void test_move_files_by_extension_dry_run_does_not_move() {
    const auto root = make_test_root();
    write_file(root / "desktop" / "a.png", "png");
    write_file(root / "desktop" / "b.jpg", "jpg");
    write_file(root / "desktop" / "c.txt", "txt");

    MoveFilesByExtensionTool tool(AllowedRoots::workspace_only(root));
    const auto result = tool.run({
        {"source_dir", "desktop"},
        {"target_dir", "pictures"},
        {"extensions", "png,jpg"},
        {"execute", "false"},
    });

    assert(result.ok);
    assert(result.output.find("dry_run") != std::string::npos);
    assert(result.output.find("a.png") != std::string::npos);
    assert(result.output.find("b.jpg") != std::string::npos);
    assert(std::filesystem::exists(root / "desktop" / "a.png"));
    assert(!std::filesystem::exists(root / "pictures" / "a.png"));

    std::filesystem::remove_all(root);
}

void test_move_files_by_extension_execute_moves_only_matching_files() {
    const auto root = make_test_root();
    write_file(root / "desktop" / "a.png", "png");
    write_file(root / "desktop" / "b.jpg", "jpg");
    write_file(root / "desktop" / "c.txt", "txt");

    MoveFilesByExtensionTool tool(AllowedRoots::workspace_only(root));
    const auto result = tool.run({
        {"source_dir", "desktop"},
        {"target_dir", "pictures"},
        {"extensions", ".png,.jpg"},
        {"execute", "true"},
    });

    assert(result.ok);
    assert(!std::filesystem::exists(root / "desktop" / "a.png"));
    assert(!std::filesystem::exists(root / "desktop" / "b.jpg"));
    assert(std::filesystem::exists(root / "desktop" / "c.txt"));
    assert(read_file(root / "pictures" / "a.png") == "png");
    assert(read_file(root / "pictures" / "b.jpg") == "jpg");

    std::filesystem::remove_all(root);
}

void test_move_files_by_extension_rejects_escape() {
    const auto root = make_test_root();
    write_file(root / "desktop" / "a.png", "png");

    MoveFilesByExtensionTool tool(AllowedRoots::workspace_only(root));
    const auto result = tool.run({
        {"source_dir", "desktop"},
        {"target_dir", root.parent_path().generic_string()},
        {"extensions", "png"},
        {"execute", "true"},
    });

    assert(!result.ok);
    assert(std::filesystem::exists(root / "desktop" / "a.png"));

    std::filesystem::remove_all(root);
}

int main() {
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    test_known_paths_resolve_aliases();
    test_allowed_roots_allows_workspace_and_rejects_escape();
    test_list_path_lists_files();
    test_make_dir_creates_directory();
    test_move_file_moves_without_overwrite();
    test_move_file_rejects_target_conflict();
    test_move_files_by_extension_dry_run_does_not_move();
    test_move_files_by_extension_execute_moves_only_matching_files();
    test_move_files_by_extension_rejects_escape();
    return 0;
}

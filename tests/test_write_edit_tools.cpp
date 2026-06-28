#include "agent_tui/agent/AgentRunner.hpp"
#include "agent_tui/llm/MockProvider.hpp"
#include "agent_tui/permissions/MockApprovalService.hpp"
#include "agent_tui/tools/WriteEditTools.hpp"

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
    auto root = std::filesystem::temp_directory_path() / ("agent_tui_write_edit_" + std::to_string(stamp));
    std::filesystem::create_directories(root);
    return root;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void write_seed_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
}

void test_write_file_approved_creates_file(const std::filesystem::path& root) {
    ToolCall call;
    call.id = "call_write";
    call.name = "write_file";
    call.arguments = {
        {"path", "docs/output.txt"},
        {"content", "hello write"},
        {"create_parent_dirs", "true"},
    };

    ToolCall done{"call_done", "Done", {{"final_answer", "done"}}};
    MockProvider provider({
        ProviderResponse::tool_calls_response({call}),
        ProviderResponse::tool_calls_response({done}),
    });
    MockApprovalService approval({ApprovalDecision::approve()});

    Workspace workspace(root);
    ToolRegistry registry;
    registry.register_tool(std::make_unique<WriteFileTool>(workspace));

    AgentRunner runner(provider, registry, approval, 4);
    auto result = runner.run({Message{Role::User, "write", {}}});

    assert(result.ok());
    assert(std::filesystem::exists(root / "docs" / "output.txt"));
    assert(read_file(root / "docs" / "output.txt") == "hello write");
    assert(runner.last_messages()[2].content.find("wrote file: docs/output.txt") != std::string::npos);
}

void test_write_file_denied_not_created(const std::filesystem::path& root) {
    ToolCall call;
    call.id = "call_write";
    call.name = "write_file";
    call.arguments = {
        {"path", "denied.txt"},
        {"content", "must not write"},
    };

    ToolCall done{"call_done", "Done", {{"final_answer", "done"}}};
    MockProvider provider({
        ProviderResponse::tool_calls_response({call}),
        ProviderResponse::tool_calls_response({done}),
    });
    MockApprovalService approval({ApprovalDecision::deny()});

    Workspace workspace(root);
    ToolRegistry registry;
    registry.register_tool(std::make_unique<WriteFileTool>(workspace));

    AgentRunner runner(provider, registry, approval, 4);
    auto result = runner.run({Message{Role::User, "write", {}}});

    assert(result.ok());
    assert(!std::filesystem::exists(root / "denied.txt"));
    assert(runner.last_messages()[2].content == "User denied permission.");
}

void test_write_file_rejects_path_escape(const std::filesystem::path& root) {
    Workspace workspace(root);
    WriteFileTool tool(workspace);
    auto result = tool.run({
        {"path", "../escape.txt"},
        {"content", "nope"},
    });

    assert(!result.ok);
    assert(result.error.find("escapes workspace") != std::string::npos);
}

void test_edit_file_approved_replaces_text(const std::filesystem::path& root) {
    write_seed_file(root / "README.md", "hello old world\nold again\n");

    ToolCall call;
    call.id = "call_edit";
    call.name = "edit_file";
    call.arguments = {
        {"path", "README.md"},
        {"old_text", "old"},
        {"new_text", "new"},
        {"replace_all", "false"},
    };

    ToolCall done{"call_done", "Done", {{"final_answer", "done"}}};
    MockProvider provider({
        ProviderResponse::tool_calls_response({call}),
        ProviderResponse::tool_calls_response({done}),
    });
    MockApprovalService approval({ApprovalDecision::approve()});

    Workspace workspace(root);
    ToolRegistry registry;
    registry.register_tool(std::make_unique<EditFileTool>(workspace));

    AgentRunner runner(provider, registry, approval, 4);
    auto result = runner.run({Message{Role::User, "edit", {}}});

    assert(result.ok());
    assert(read_file(root / "README.md") == "hello new world\nold again\n");
    assert(runner.last_messages()[2].content.find("replacements: 1") != std::string::npos);
}

void test_edit_file_denied_not_modified(const std::filesystem::path& root) {
    write_seed_file(root / "denied_edit.txt", "before\n");

    ToolCall call;
    call.id = "call_edit";
    call.name = "edit_file";
    call.arguments = {
        {"path", "denied_edit.txt"},
        {"old_text", "before"},
        {"new_text", "after"},
    };

    ToolCall done{"call_done", "Done", {{"final_answer", "done"}}};
    MockProvider provider({
        ProviderResponse::tool_calls_response({call}),
        ProviderResponse::tool_calls_response({done}),
    });
    MockApprovalService approval({ApprovalDecision::deny()});

    Workspace workspace(root);
    ToolRegistry registry;
    registry.register_tool(std::make_unique<EditFileTool>(workspace));

    AgentRunner runner(provider, registry, approval, 4);
    auto result = runner.run({Message{Role::User, "edit", {}}});

    assert(result.ok());
    assert(read_file(root / "denied_edit.txt") == "before\n");
    assert(runner.last_messages()[2].content == "User denied permission.");
}

void test_edit_file_missing_old_text_fails(const std::filesystem::path& root) {
    write_seed_file(root / "missing.txt", "abc\n");

    Workspace workspace(root);
    EditFileTool tool(workspace);
    auto result = tool.run({
        {"path", "missing.txt"},
        {"old_text", "does-not-exist"},
        {"new_text", "replacement"},
    });

    assert(!result.ok);
    assert(result.error.find("old_text not found") != std::string::npos);
    assert(read_file(root / "missing.txt") == "abc\n");
}

void test_edit_file_replace_all(const std::filesystem::path& root) {
    write_seed_file(root / "replace_all.txt", "x x x");

    Workspace workspace(root);
    EditFileTool tool(workspace);
    auto result = tool.run({
        {"path", "replace_all.txt"},
        {"old_text", "x"},
        {"new_text", "y"},
        {"replace_all", "true"},
    });

    assert(result.ok);
    assert(result.output.find("replacements: 3") != std::string::npos);
    assert(read_file(root / "replace_all.txt") == "y y y");
}

void test_edit_file_supports_multiple_exact_replacements_and_diff(const std::filesystem::path& root) {
    write_seed_file(root / "multi.txt", "alpha\nbeta\ngamma\n");

    Workspace workspace(root);
    EditFileTool tool(workspace);
    auto result = tool.run({
        {"path", "multi.txt"},
        {"old_text_1", "alpha"},
        {"new_text_1", "ALPHA"},
        {"old_text_2", "gamma"},
        {"new_text_2", "GAMMA"},
    });

    assert(result.ok);
    assert(read_file(root / "multi.txt") == "ALPHA\nbeta\nGAMMA\n");
    assert(result.output.find("replacements: 2") != std::string::npos);
    assert(result.output.find("diff:") != std::string::npos);
    assert(result.output.find("-alpha") != std::string::npos);
    assert(result.output.find("+ALPHA") != std::string::npos);
    assert(result.output.find("-gamma") != std::string::npos);
    assert(result.output.find("+GAMMA") != std::string::npos);
}

void test_edit_file_rejects_overlapping_multiple_replacements(const std::filesystem::path& root) {
    write_seed_file(root / "overlap.txt", "abcdef\n");

    Workspace workspace(root);
    EditFileTool tool(workspace);
    auto result = tool.run({
        {"path", "overlap.txt"},
        {"old_text_1", "abc"},
        {"new_text_1", "ABC"},
        {"old_text_2", "bc"},
        {"new_text_2", "BC"},
    });

    assert(!result.ok);
    assert(result.error.find("overlapping edits") != std::string::npos);
    assert(read_file(root / "overlap.txt") == "abcdef\n");
}

}  // namespace

int main() {
    const auto root = make_test_root();
    test_write_file_approved_creates_file(root);
    test_write_file_denied_not_created(root);
    test_write_file_rejects_path_escape(root);
    test_edit_file_approved_replaces_text(root);
    test_edit_file_denied_not_modified(root);
    test_edit_file_missing_old_text_fails(root);
    test_edit_file_replace_all(root);
    test_edit_file_supports_multiple_exact_replacements_and_diff(root);
    test_edit_file_rejects_overlapping_multiple_replacements(root);
    std::filesystem::remove_all(root);
    return 0;
}

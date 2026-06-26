#include "agent_tui/agent/AgentRunner.hpp"
#include "agent_tui/llm/MockProvider.hpp"
#include "agent_tui/permissions/MockApprovalService.hpp"
#include "agent_tui/tools/ShellTool.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>

using namespace agent_tui;

namespace {

std::filesystem::path make_test_root() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto root = std::filesystem::temp_directory_path() / ("agent_tui_shell_tool_" + std::to_string(stamp));
    std::filesystem::create_directories(root);
    return root;
}

#ifndef _WIN32

void test_run_shell_echo(const std::filesystem::path& root) {
    Workspace workspace(root);
    ShellTool tool(workspace);
    auto result = tool.run({
        {"command", "printf hello"},
        {"cwd", "."},
        {"timeout_seconds", "5"},
    });

    assert(result.ok);
    assert(result.output.find("exit_code: 0") != std::string::npos);
    assert(result.output.find("timeout: false") != std::string::npos);
    assert(result.output.find("hello") != std::string::npos);
}

void test_run_shell_nonzero_exit(const std::filesystem::path& root) {
    Workspace workspace(root);
    ShellTool tool(workspace);
    auto result = tool.run({
        {"command", "printf fail >&2; exit 7"},
        {"cwd", "."},
        {"timeout_seconds", "5"},
    });

    assert(result.ok);
    assert(result.output.find("exit_code: 7") != std::string::npos);
    assert(result.output.find("timeout: false") != std::string::npos);
    assert(result.output.find("fail") != std::string::npos);
}

void test_run_shell_timeout(const std::filesystem::path& root) {
    Workspace workspace(root);
    ShellTool tool(workspace);
    auto result = tool.run({
        {"command", "sleep 2"},
        {"cwd", "."},
        {"timeout_seconds", "1"},
    });

    assert(result.ok);
    assert(result.output.find("exit_code: -1") != std::string::npos);
    assert(result.output.find("timeout: true") != std::string::npos);
}

void test_run_shell_denied_not_executed(const std::filesystem::path& root) {
    ToolCall call;
    call.id = "call_shell";
    call.name = "run_shell";
    call.arguments = {
        {"command", "touch marker"},
        {"cwd", "."},
        {"timeout_seconds", "5"},
    };

    ToolCall done;
    done.id = "call_done";
    done.name = "Done";
    done.arguments = {{"final_answer", "done"}};

    MockProvider provider({
        ProviderResponse::tool_calls_response({call}),
        ProviderResponse::tool_calls_response({done}),
    });
    MockApprovalService approval({ApprovalDecision::deny()});

    Workspace workspace(root);
    ToolRegistry registry;
    registry.register_tool(std::make_unique<ShellTool>(workspace));

    AgentRunner runner(provider, registry, approval, 4);
    auto result = runner.run({Message{Role::User, "try shell", {}}});

    assert(result.ok());
    assert(!std::filesystem::exists(root / "marker"));
    assert(runner.last_messages()[1].content == "User denied permission.");
}

void test_run_shell_rejects_cwd_escape(const std::filesystem::path& root) {
    Workspace workspace(root);
    ShellTool tool(workspace);
    auto result = tool.run({
        {"command", "printf nope"},
        {"cwd", ".."},
        {"timeout_seconds", "5"},
    });

    assert(!result.ok);
    assert(result.error.find("escapes workspace") != std::string::npos);
}

#endif

}  // namespace

int main() {
#ifdef _WIN32
    return 0;
#else
    const auto root = make_test_root();
    test_run_shell_echo(root);
    test_run_shell_nonzero_exit(root);
    test_run_shell_timeout(root);
    test_run_shell_denied_not_executed(root);
    test_run_shell_rejects_cwd_escape(root);
    std::filesystem::remove_all(root);
    return 0;
#endif
}

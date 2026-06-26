#include "agent_tui/agent/AgentRunner.hpp"
#include "agent_tui/llm/MockProvider.hpp"
#include "agent_tui/permissions/MockApprovalService.hpp"
#include "agent_tui/session/AuditLog.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

using namespace agent_tui;

namespace {

class EchoTool final : public Tool {
public:
    std::string name() const override { return "echo"; }
    std::string description() const override { return "Echo text."; }
    PermissionMode permission_mode() const override { return PermissionMode::Auto; }

    ToolResult run(const JsonLike& arguments) override {
        auto it = arguments.find("text");
        if (it == arguments.end()) {
            return ToolResult::failure("missing text");
        }
        return ToolResult::success(it->second);
    }
};

class ConfirmTool final : public Tool {
public:
    std::string name() const override { return "confirm_tool"; }
    std::string description() const override { return "Needs approval."; }
    PermissionMode permission_mode() const override { return PermissionMode::Confirm; }

    ToolResult run(const JsonLike&) override {
        return ToolResult::success("should not run when denied");
    }
};

std::filesystem::path make_test_root() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto root = std::filesystem::temp_directory_path() / ("agent_tui_session_" + std::to_string(stamp));
    std::filesystem::create_directories(root);
    return root;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool has_event_type(const SessionHistory& history, SessionEventType type) {
    for (const auto& event : history.events()) {
        if (event.type == type) {
            return true;
        }
    }
    return false;
}

void test_session_history_records_tool_flow() {
    ToolCall echo_call;
    echo_call.id = "call_echo";
    echo_call.name = "echo";
    echo_call.arguments = {{"text", "hello session"}};

    ToolCall done_call;
    done_call.id = "call_done";
    done_call.name = "Done";
    done_call.arguments = {{"final_answer", "finished"}};

    MockProvider provider({
        ProviderResponse::tool_calls_response({echo_call}),
        ProviderResponse::tool_calls_response({done_call}),
    });

    ToolRegistry registry;
    registry.register_tool(std::make_unique<EchoTool>());

    SessionHistory history;
    AgentRunner runner(provider, registry, history, 4);
    auto result = runner.run({Message{Role::User, "run echo", {}}});

    assert(result.ok());
    assert(has_event_type(history, SessionEventType::UserInput));
    assert(has_event_type(history, SessionEventType::ToolCall));
    assert(has_event_type(history, SessionEventType::ToolResult));
    assert(has_event_type(history, SessionEventType::AssistantMessage));

    bool saw_echo_result = false;
    for (const auto& event : history.events()) {
        if (event.type == SessionEventType::ToolResult && event.content == "hello session") {
            saw_echo_result = true;
        }
    }
    assert(saw_echo_result);
}

void test_session_history_records_permission_denial() {
    ToolCall call;
    call.id = "call_confirm";
    call.name = "confirm_tool";

    ToolCall done_call;
    done_call.id = "call_done";
    done_call.name = "Done";
    done_call.arguments = {{"final_answer", "handled"}};

    MockProvider provider({
        ProviderResponse::tool_calls_response({call}),
        ProviderResponse::tool_calls_response({done_call}),
    });

    ToolRegistry registry;
    registry.register_tool(std::make_unique<ConfirmTool>());

    MockApprovalService approval({ApprovalDecision::deny()});
    SessionHistory history;
    AgentRunner runner(provider, registry, approval, history, 4);
    auto result = runner.run({Message{Role::User, "try denied", {}}});

    assert(result.ok());
    assert(has_event_type(history, SessionEventType::PermissionDenied));
    assert(has_event_type(history, SessionEventType::ToolResult));

    bool saw_denial_result = false;
    for (const auto& event : history.events()) {
        if (event.type == SessionEventType::ToolResult && event.content == "User denied permission.") {
            saw_denial_result = true;
        }
    }
    assert(saw_denial_result);
}

void test_audit_log_writes_jsonl() {
    const auto root = make_test_root();
    const auto log_path = root / ".agent-tui" / "sessions" / "test.jsonl";

    SessionHistory history;
    history.add(SessionEvent::user_input("hello"));
    history.add(SessionEvent::assistant_message("world"));

    AuditLog audit(log_path);
    assert(audit.append_all(history));

    const auto content = read_file(log_path);
    assert(content.find("\"type\":\"user_input\"") != std::string::npos);
    assert(content.find("\"content\":\"hello\"") != std::string::npos);
    assert(content.find("\"type\":\"assistant_message\"") != std::string::npos);

    std::filesystem::remove_all(root);
}

void test_json_escape_in_audit_log() {
    const auto root = make_test_root();
    const auto log_path = root / "escape.jsonl";

    AuditLog audit(log_path);
    assert(audit.append(SessionEvent::user_input("quote \" and newline\n")));

    const auto content = read_file(log_path);
    assert(content.find("quote \\\" and newline\\n") != std::string::npos);

    std::filesystem::remove_all(root);
}

}  // namespace

int main() {
    test_session_history_records_tool_flow();
    test_session_history_records_permission_denial();
    test_audit_log_writes_jsonl();
    test_json_escape_in_audit_log();
    return 0;
}

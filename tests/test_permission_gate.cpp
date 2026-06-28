#include "agent_tui/agent/AgentRunner.hpp"
#include "agent_tui/llm/MockProvider.hpp"
#include "agent_tui/permissions/MockApprovalService.hpp"

#include <cassert>
#include <memory>
#include <string>

using namespace agent_tui;

class ConfirmEchoTool final : public Tool {
public:
    std::string name() const override { return "confirm_echo"; }
    std::string description() const override { return "Echo after confirmation."; }
    PermissionMode permission_mode() const override { return PermissionMode::Confirm; }

    ToolResult run(const JsonLike& arguments) override {
        ++run_count;
        auto it = arguments.find("text");
        if (it == arguments.end()) {
            return ToolResult::failure("missing text");
        }
        return ToolResult::success(it->second);
    }

    int run_count = 0;
};

void test_confirm_tool_denied_goes_back_to_model() {
    ToolCall call{"call_confirm", "confirm_echo", {{"text", "secret"}}};
    ToolCall done{"call_done", "Done", {{"final_answer", "permission handled"}}};

    MockProvider provider({
        ProviderResponse::tool_calls_response({call}),
        ProviderResponse::tool_calls_response({done}),
    });
    MockApprovalService approval({ApprovalDecision::deny()});

    auto tool = std::make_unique<ConfirmEchoTool>();
    auto* tool_ptr = tool.get();
    ToolRegistry registry;
    registry.register_tool(std::move(tool));

    AgentRunner runner(provider, registry, approval, 4);
    auto result = runner.run({Message{Role::User, "try confirm", {}}});

    assert(result.ok());
    assert(result.output == "permission handled");
    assert(approval.request_count() == 1);
    assert(tool_ptr->run_count == 0);
    assert(runner.last_messages()[2].content == "User denied permission.");
}

void test_confirm_tool_approved_executes() {
    ToolCall call{"call_confirm", "confirm_echo", {{"text", "approved text"}}};
    ToolCall done{"call_done", "Done", {{"final_answer", "done"}}};

    MockProvider provider({
        ProviderResponse::tool_calls_response({call}),
        ProviderResponse::tool_calls_response({done}),
    });
    MockApprovalService approval({ApprovalDecision::approve()});

    auto tool = std::make_unique<ConfirmEchoTool>();
    auto* tool_ptr = tool.get();
    ToolRegistry registry;
    registry.register_tool(std::move(tool));

    AgentRunner runner(provider, registry, approval, 4);
    auto result = runner.run({Message{Role::User, "try confirm", {}}});

    assert(result.ok());
    assert(tool_ptr->run_count == 1);
    assert(runner.last_messages()[2].content == "approved text");
}

void test_confirm_tool_edit_changes_args() {
    ToolCall call{"call_confirm", "confirm_echo", {{"text", "original"}}};
    ToolCall done{"call_done", "Done", {{"final_answer", "done"}}};

    MockProvider provider({
        ProviderResponse::tool_calls_response({call}),
        ProviderResponse::tool_calls_response({done}),
    });
    MockApprovalService approval({ApprovalDecision::edit({{"text", "edited"}})});

    ToolRegistry registry;
    registry.register_tool(std::make_unique<ConfirmEchoTool>());

    AgentRunner runner(provider, registry, approval, 4);
    auto result = runner.run({Message{Role::User, "try confirm", {}}});

    assert(result.ok());
    assert(runner.last_messages()[2].content == "edited");
}

void test_confirm_tool_feedback_goes_back_to_model_without_execution() {
    ToolCall call{"call_confirm", "confirm_echo", {{"text", "original"}}};
    ToolCall done{"call_done", "Done", {{"final_answer", "done"}}};

    MockProvider provider({
        ProviderResponse::tool_calls_response({call}),
        ProviderResponse::tool_calls_response({done}),
    });
    MockApprovalService approval({ApprovalDecision::user_feedback("explain first")});

    auto tool = std::make_unique<ConfirmEchoTool>();
    auto* tool_ptr = tool.get();
    ToolRegistry registry;
    registry.register_tool(std::move(tool));

    AgentRunner runner(provider, registry, approval, 4);
    auto result = runner.run({Message{Role::User, "try confirm", {}}});

    assert(result.ok());
    assert(tool_ptr->run_count == 0);
    assert(runner.last_messages()[2].content == "User feedback: explain first");
}

int main() {
    test_confirm_tool_denied_goes_back_to_model();
    test_confirm_tool_approved_executes();
    test_confirm_tool_edit_changes_args();
    test_confirm_tool_feedback_goes_back_to_model_without_execution();
    return 0;
}

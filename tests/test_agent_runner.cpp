#include "agent_tui/agent/AgentRunner.hpp"
#include "agent_tui/llm/MockProvider.hpp"

#include <cassert>
#include <memory>
#include <string>

using namespace agent_tui;

class EchoTool final : public Tool {
public:
    std::string name() const override { return "echo"; }
    std::string description() const override { return "Echo the text argument."; }
    PermissionMode permission_mode() const override { return PermissionMode::Auto; }

    ToolResult run(const JsonLike& arguments) override {
        auto it = arguments.find("text");
        if (it == arguments.end()) {
            return ToolResult::failure("missing text");
        }
        return ToolResult::success(it->second);
    }
};

void test_single_tool_call_then_done() {
    ToolCall echo_call;
    echo_call.id = "call_1";
    echo_call.name = "echo";
    echo_call.arguments = {{"text", "hello from tool"}};

    ToolCall done_call;
    done_call.id = "call_2";
    done_call.name = "Done";
    done_call.arguments = {{"final_answer", "finished"}};

    MockProvider provider({
        ProviderResponse::tool_calls_response({echo_call}),
        ProviderResponse::tool_calls_response({done_call}),
    });

    ToolRegistry registry;
    registry.register_tool(std::make_unique<EchoTool>());

    AgentRunner runner(provider, registry, 4);
    auto result = runner.run({Message{Role::User, "run echo", {}}});

    assert(result.ok());
    assert(result.output == "finished");
    assert(provider.call_count() == 2);
    assert(runner.last_messages().size() == 3);
    assert(runner.last_messages()[1].role == Role::Tool);
    assert(runner.last_messages()[1].content == "hello from tool");
}

void test_tool_not_found_goes_back_to_model() {
    ToolCall missing_call;
    missing_call.id = "call_missing";
    missing_call.name = "missing_tool";

    ToolCall done_call;
    done_call.id = "call_done";
    done_call.name = "Done";
    done_call.arguments = {{"final_answer", "handled missing tool"}};

    MockProvider provider({
        ProviderResponse::tool_calls_response({missing_call}),
        ProviderResponse::tool_calls_response({done_call}),
    });

    ToolRegistry registry;
    AgentRunner runner(provider, registry, 4);
    auto result = runner.run({Message{Role::User, "call missing", {}}});

    assert(result.ok());
    assert(result.output == "handled missing tool");
    assert(runner.last_messages()[1].content == "Tool not found: missing_tool");
}

void test_max_loop_exceeded() {
    ToolCall missing_call;
    missing_call.id = "call_missing";
    missing_call.name = "missing_tool";

    MockProvider provider({
        ProviderResponse::tool_calls_response({missing_call}),
        ProviderResponse::tool_calls_response({missing_call}),
    });

    ToolRegistry registry;
    AgentRunner runner(provider, registry, 2);
    auto result = runner.run({Message{Role::User, "loop", {}}});

    assert(!result.ok());
    assert(result.error == "Max loop count exceeded.");
}

int main() {
    test_single_tool_call_then_done();
    test_tool_not_found_goes_back_to_model();
    test_max_loop_exceeded();
    return 0;
}

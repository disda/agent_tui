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

class OtherTool final : public Tool {
public:
    std::string name() const override { return "run_shell"; }
    std::string description() const override { return "Other tool."; }
    PermissionMode permission_mode() const override { return PermissionMode::Auto; }

    ToolResult run(const JsonLike&) override {
        return ToolResult::success("other");
    }
};

class StreamingTextProvider final : public Provider {
public:
    ProviderResponse chat(const std::vector<Message>&, const std::string& = {}) override {
        chat_called = true;
        return ProviderResponse::text_response("non-stream");
    }

    ProviderResponse chat_stream(const std::vector<Message>&,
                                 const std::string&,
                                 const std::function<void(const std::string&)>& on_delta) override {
        stream_called = true;
        on_delta("hel");
        on_delta("lo");
        return ProviderResponse::text_response("hello");
    }

    bool chat_called = false;
    bool stream_called = false;
};

class ToolAwareProvider final : public Provider {
public:
    ProviderResponse chat(const std::vector<Message>&, const std::string& tools_schema_json = {}) override {
        chat_called = true;
        observed_tools_schema_json = tools_schema_json;
        ToolCall echo_call;
        echo_call.id = "call_echo";
        echo_call.name = "echo";
        echo_call.arguments = {{"text", "tool path"}};
        return ProviderResponse::tool_calls_response({echo_call});
    }

    ProviderResponse chat_stream(const std::vector<Message>&,
                                 const std::string&,
                                 const std::function<void(const std::string&)>& on_delta) override {
        stream_called = true;
        on_delta("I will inspect first");
        return ProviderResponse::text_response("I will inspect first");
    }

    bool chat_called = false;
    bool stream_called = false;
    std::string observed_tools_schema_json;
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
    assert(provider.observed_tools_schema_json().find("\"echo\"") != std::string::npos);
    assert(runner.last_messages().size() == 5);
    assert(runner.last_messages()[1].role == Role::Assistant);
    assert(runner.last_messages()[1].tool_calls.size() == 1);
    assert(runner.last_messages()[2].role == Role::Tool);
    assert(runner.last_messages()[2].content == "hello from tool");
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
    assert(runner.last_messages()[2].content == "Tool not found: missing_tool");
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

void test_default_max_loop_allows_twenty_five_provider_iterations() {
    ToolCall missing_call;
    missing_call.id = "call_missing";
    missing_call.name = "missing_tool";

    ToolCall done_call;
    done_call.id = "call_done";
    done_call.name = "Done";
    done_call.arguments = {{"final_answer", "done after 25"}};

    std::vector<ProviderResponse> responses;
    for (int i = 0; i < 24; ++i) {
        responses.push_back(ProviderResponse::tool_calls_response({missing_call}));
    }
    responses.push_back(ProviderResponse::tool_calls_response({done_call}));

    MockProvider provider(responses);
    ToolRegistry registry;
    AgentRunner runner(provider, registry);
    auto result = runner.run({Message{Role::User, "use default loop budget", {}}});

    assert(result.ok());
    assert(result.output == "done after 25");
    assert(provider.call_count() == 25);
}

void test_tool_schema_can_be_limited_by_active_allow_deny_lists() {
    ToolRegistry registry;
    registry.register_tool(std::make_unique<EchoTool>());
    registry.register_tool(std::make_unique<OtherTool>());

    ToolExposurePolicy policy;
    policy.active_tools = {"echo", "run_shell"};
    policy.allow_tools = {"echo"};
    policy.deny_tools = {"run_shell"};

    const auto schema = registry.tools_schema_json(policy);

    assert(schema.find("\"echo\"") != std::string::npos);
    assert(schema.find("\"run_shell\"") == std::string::npos);
}

void test_agent_runner_uses_provider_streaming_deltas_for_text() {
    StreamingTextProvider provider;
    ToolRegistry registry;
    AgentRunner runner(provider, registry, 2);

    std::string streamed;
    runner.set_observer(AgentRunObserver{
        {},
        [&](const std::string& delta) {
            streamed += delta;
        },
    });

    auto result = runner.run({Message{Role::User, "stream", {}}});

    assert(result.ok());
    assert(result.output == "hello");
    assert(streamed == "hello");
    assert(provider.stream_called);
    assert(!provider.chat_called);
}

void test_agent_runner_uses_non_streaming_chat_when_tools_are_available() {
    ToolAwareProvider provider;
    ToolRegistry registry;
    registry.register_tool(std::make_unique<EchoTool>());
    AgentRunner runner(provider, registry, 1);

    auto result = runner.run({Message{Role::User, "inspect with tools", {}}});

    assert(!result.ok());
    assert(provider.chat_called);
    assert(!provider.stream_called);
    assert(provider.observed_tools_schema_json.find("\"echo\"") != std::string::npos);
    assert(runner.last_messages()[2].content == "tool path");
}

void test_agent_runner_uses_tool_exposure_policy_for_provider_schema() {
    ToolCall done_call;
    done_call.id = "call_done";
    done_call.name = "Done";
    done_call.arguments = {{"final_answer", "done"}};

    MockProvider provider({
        ProviderResponse::tool_calls_response({done_call}),
    });

    ToolRegistry registry;
    registry.register_tool(std::make_unique<EchoTool>());
    registry.register_tool(std::make_unique<OtherTool>());

    ToolExposurePolicy policy;
    policy.allow_tools = {"echo"};

    AgentRunner runner(provider, registry, 2);
    runner.set_tool_exposure_policy(policy);
    auto result = runner.run({Message{Role::User, "done", {}}});

    assert(result.ok());
    assert(provider.observed_tools_schema_json().find("\"echo\"") != std::string::npos);
    assert(provider.observed_tools_schema_json().find("\"run_shell\"") == std::string::npos);
}

void test_agent_runner_blocks_tool_call_denied_by_exposure_policy() {
    ToolCall blocked_call;
    blocked_call.id = "call_blocked";
    blocked_call.name = "run_shell";

    ToolCall done_call;
    done_call.id = "call_done";
    done_call.name = "Done";
    done_call.arguments = {{"final_answer", "done"}};

    MockProvider provider({
        ProviderResponse::tool_calls_response({blocked_call}),
        ProviderResponse::tool_calls_response({done_call}),
    });

    ToolRegistry registry;
    registry.register_tool(std::make_unique<OtherTool>());

    ToolExposurePolicy policy;
    policy.deny_tools = {"run_shell"};

    AgentRunner runner(provider, registry, 4);
    runner.set_tool_exposure_policy(policy);
    auto result = runner.run({Message{Role::User, "try blocked", {}}});

    assert(result.ok());
    assert(runner.last_messages()[2].content.find("Tool not allowed by exposure policy: run_shell") != std::string::npos);
}

int main() {
    test_single_tool_call_then_done();
    test_tool_not_found_goes_back_to_model();
    test_max_loop_exceeded();
    test_default_max_loop_allows_twenty_five_provider_iterations();
    test_tool_schema_can_be_limited_by_active_allow_deny_lists();
    test_agent_runner_uses_provider_streaming_deltas_for_text();
    test_agent_runner_uses_non_streaming_chat_when_tools_are_available();
    test_agent_runner_uses_tool_exposure_policy_for_provider_schema();
    test_agent_runner_blocks_tool_call_denied_by_exposure_policy();
    return 0;
}

#include "agent_tui/llm/OpenAICompatibleProvider.hpp"
#include "agent_tui/llm/ProviderFactory.hpp"
#include "agent_tui/tools/FileTools.hpp"
#include "agent_tui/tools/ShellTool.hpp"
#include "agent_tui/tools/ToolRegistry.hpp"
#include "agent_tui/tools/WriteEditTools.hpp"
#include "agent_tui/workspace/Workspace.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

using namespace agent_tui;

namespace {

class CustomSchemaTool final : public Tool {
public:
    std::string name() const override { return "custom_schema_tool"; }
    std::string description() const override { return "Custom schema tool for registry tests."; }
    PermissionMode permission_mode() const override { return PermissionMode::Auto; }
    std::string parameters_schema_json() const override {
        return R"({"type":"object","properties":{"value":{"type":"string"}},"required":["value"]})";
    }
    ToolResult run(const JsonLike&) override { return ToolResult::success("ok"); }
};

std::filesystem::path make_test_root() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto root = std::filesystem::temp_directory_path() / ("agent_tui_openai_provider_" + std::to_string(stamp));
    std::filesystem::create_directories(root);
    return root;
}

ToolRegistry make_core_registry(const Workspace& workspace) {
    ToolRegistry registry;
    registry.register_tool(std::make_unique<WorkspaceInfoTool>(workspace));
    registry.register_tool(std::make_unique<ListDirTool>(workspace));
    registry.register_tool(std::make_unique<ReadFileTool>(workspace));
    registry.register_tool(std::make_unique<GlobFilesTool>(workspace));
    registry.register_tool(std::make_unique<SearchTextTool>(workspace));
    registry.register_tool(std::make_unique<WriteFileTool>(workspace));
    registry.register_tool(std::make_unique<EditFileTool>(workspace));
    registry.register_tool(std::make_unique<ShellTool>(workspace));
    return registry;
}

}  // namespace

void test_build_request_body_contains_model_and_messages() {
    Config config;
    config.model = "gpt-test";

    const auto body = OpenAICompatibleProvider::build_request_body(config, {
        Message{Role::System, "You are terse.", {}},
        Message{Role::User, "hello world", {}},
    });

    assert(body.find("\"model\":\"gpt-test\"") != std::string::npos);
    assert(body.find("\"role\":\"system\"") != std::string::npos);
    assert(body.find("\"role\":\"user\"") != std::string::npos);
    assert(body.find("hello world") != std::string::npos);
    assert(body.find("\"stream\":false") != std::string::npos);
}

void test_parse_text_response() {
    const std::string body = R"({
        "id": "chatcmpl-test",
        "choices": [
            {"message": {"role": "assistant", "content": "hello from api"}}
        ]
    })";

    const auto response = OpenAICompatibleProvider::parse_response_body(body);
    assert(response.type == ProviderResponseType::Text);
    assert(response.text == "hello from api");
}

void test_parse_error_response() {
    const std::string body = R"({
        "error": {"message": "provider rejected request", "type": "invalid_request_error"}
    })";

    const auto response = OpenAICompatibleProvider::parse_response_body(body);
    assert(response.type == ProviderResponseType::Error);
    assert(response.error == "provider rejected request");
}

void test_parse_tool_call_response() {
    const std::string body = R"({
        "choices": [
            {
                "message": {
                    "role": "assistant",
                    "tool_calls": [
                        {
                            "id": "call_write",
                            "type": "function",
                            "function": {
                                "name": "write_file",
                                "arguments": "{\"path\":\"hello.txt\",\"content\":\"你好\"}"
                            }
                        }
                    ]
                }
            }
        ]
    })";

    const auto response = OpenAICompatibleProvider::parse_response_body(body);
    assert(response.type == ProviderResponseType::ToolCalls);
    assert(response.tool_calls.size() == 1);
    assert(response.tool_calls[0].id == "call_write");
    assert(response.tool_calls[0].name == "write_file");
    assert(response.tool_calls[0].arguments.at("path") == "hello.txt");
    assert(response.tool_calls[0].arguments.at("content") == "你好");
}

void test_parse_tool_call_arguments_with_non_string_values() {
    const std::string body = R"({
        "choices": [
            {
                "message": {
                    "role": "assistant",
                    "tool_calls": [
                        {
                            "id": "call_read",
                            "type": "function",
                            "function": {
                                "name": "read_file",
                                "arguments": "{\"path\":\"TODO.md\",\"limit\":200,\"ignoreCase\":true,\"options\":{\"mode\":\"fast\"},\"globs\":[\"*.cpp\",\"*.hpp\"]}"
                            }
                        }
                    ]
                }
            }
        ]
    })";

    const auto response = OpenAICompatibleProvider::parse_response_body(body);
    assert(response.type == ProviderResponseType::ToolCalls);
    assert(response.tool_calls.size() == 1);
    assert(response.tool_calls[0].arguments.at("path") == "TODO.md");
    assert(response.tool_calls[0].arguments.at("limit") == "200");
    assert(response.tool_calls[0].arguments.at("ignoreCase") == "true");
    assert(response.tool_calls[0].arguments.at("options") == "{\"mode\":\"fast\"}");
    assert(response.tool_calls[0].arguments.at("globs") == "[\"*.cpp\",\"*.hpp\"]");
}

void test_request_body_includes_tool_definitions_and_tool_results() {
    const auto root = make_test_root();
    Workspace workspace(root);
    auto registry = make_core_registry(workspace);

    Config config;
    config.model = "gpt-test";
    ToolCall call;
    call.id = "call_write";
    call.name = "write_file";
    call.arguments = {{"path", "hello.txt"}, {"content", "hello"}};

    const auto body = OpenAICompatibleProvider::build_request_body(config, {
        Message{Role::System, "Use tools when useful.", {}},
        Message{Role::User, "create a file", {}},
        Message{Role::Assistant, "", "", {call}},
        Message{Role::Tool, "wrote file: hello.txt", "call_write"},
    }, registry.tools_schema_json());

    assert(body.find("\"tools\"") != std::string::npos);
    assert(body.find("\"workspace_info\"") != std::string::npos);
    assert(body.find("\"list_dir\"") != std::string::npos);
    assert(body.find("\"read_file\"") != std::string::npos);
    assert(body.find("\"glob_files\"") != std::string::npos);
    assert(body.find("\"search_text\"") != std::string::npos);
    assert(body.find("\"write_file\"") != std::string::npos);
    assert(body.find("\"edit_file\"") != std::string::npos);
    assert(body.find("\"run_shell\"") != std::string::npos);
    assert(body.find("\"role\":\"assistant\"") != std::string::npos);
    assert(body.find("\"tool_calls\"") != std::string::npos);
    assert(body.find("\"role\":\"tool\"") != std::string::npos);
    assert(body.find("\"tool_call_id\":\"call_write\"") != std::string::npos);
    assert(body.find("wrote file: hello.txt") != std::string::npos);
    std::filesystem::remove_all(root);
}

void test_request_body_uses_tool_registry_schema() {
    Config config;
    config.model = "gpt-test";
    ToolRegistry registry;
    registry.register_tool(std::make_unique<CustomSchemaTool>());

    const auto body = OpenAICompatibleProvider::build_request_body(config, {
        Message{Role::User, "use custom tool", {}},
    }, registry.tools_schema_json());

    assert(body.find("\"tools\"") != std::string::npos);
    assert(body.find("\"custom_schema_tool\"") != std::string::npos);
    assert(body.find("\"value\"") != std::string::npos);
    assert(body.find("\"write_file\"") == std::string::npos);
    assert(body.find("\"run_shell\"") == std::string::npos);
}

void test_streaming_request_body_can_omit_tools() {
    Config config;
    config.model = "gpt-test";

    const auto body = OpenAICompatibleProvider::build_request_body(config, {
        Message{Role::User, "hello", {}},
    }, false, true);

    assert(body.find("\"stream\":true") != std::string::npos);
    assert(body.find("\"tools\"") == std::string::npos);
    assert(body.find("\"tool_choice\"") == std::string::npos);
}

void test_curl_config_path_uses_forward_slashes() {
    const std::filesystem::path path{"C:\\Users\\Example\\Temp\\request.json"};
    const auto value = openai_compatible_detail::curl_config_path(path);
    assert(value.find('\\') == std::string::npos);
    assert(value.find("request.json") != std::string::npos);
}

void test_inline_api_key_takes_precedence_over_env_key() {
    Config config;
    config.api_key = "sk-inline-key";
    config.api_key_env = "AGENT_TUI_TEST_MISSING_ENV";
    assert(OpenAICompatibleProvider::resolve_api_key(config) == "sk-inline-key");
}

void test_provider_factory_creates_openai_compatible() {
    Config config;
    config.provider = "openai-compatible";
    config.api_key_env = "AGENT_TUI_TEST_MISSING_ENV";

    auto provider = ProviderFactory::create(config);
    const auto response = provider->chat({Message{Role::User, "hi", {}}});
    assert(response.type == ProviderResponseType::Error);
    assert(response.error.find("missing API key") != std::string::npos);
}

int main() {
    test_build_request_body_contains_model_and_messages();
    test_parse_text_response();
    test_parse_error_response();
    test_parse_tool_call_response();
    test_parse_tool_call_arguments_with_non_string_values();
    test_request_body_includes_tool_definitions_and_tool_results();
    test_request_body_uses_tool_registry_schema();
    test_streaming_request_body_can_omit_tools();
    test_curl_config_path_uses_forward_slashes();
    test_inline_api_key_takes_precedence_over_env_key();
    test_provider_factory_creates_openai_compatible();
    return 0;
}

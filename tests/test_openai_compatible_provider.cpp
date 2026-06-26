#include "agent_tui/llm/OpenAICompatibleProvider.hpp"
#include "agent_tui/llm/ProviderFactory.hpp"

#include <cassert>
#include <filesystem>
#include <string>

using namespace agent_tui;

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

void test_request_body_includes_tool_definitions_and_tool_results() {
    Config config;
    config.model = "gpt-test";

    const auto body = OpenAICompatibleProvider::build_request_body(config, {
        Message{Role::System, "Use tools when useful.", {}},
        Message{Role::User, "create a file", {}},
        Message{Role::Tool, "wrote file: hello.txt", "call_write"},
    });

    assert(body.find("\"tools\"") != std::string::npos);
    assert(body.find("\"write_file\"") != std::string::npos);
    assert(body.find("Tool result") != std::string::npos);
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
    test_request_body_includes_tool_definitions_and_tool_results();
    test_streaming_request_body_can_omit_tools();
    test_curl_config_path_uses_forward_slashes();
    test_inline_api_key_takes_precedence_over_env_key();
    test_provider_factory_creates_openai_compatible();
    return 0;
}

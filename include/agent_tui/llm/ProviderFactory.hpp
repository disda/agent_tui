#pragma once

#include <memory>
#include <string>
#include <utility>

#include "agent_tui/config/Config.hpp"
#include "agent_tui/llm/EchoProvider.hpp"
#include "agent_tui/llm/MockProvider.hpp"
#include "agent_tui/llm/OpenAICompatibleProvider.hpp"
#include "agent_tui/llm/Provider.hpp"

namespace agent_tui {

class PlaceholderProvider final : public Provider {
public:
    explicit PlaceholderProvider(std::string provider_name) : provider_name_(std::move(provider_name)) {}

    ProviderResponse chat(const std::vector<Message>&, const std::string& = {}) override {
        return ProviderResponse::error_response(
            "provider '" + provider_name_ + "' is configured but the real HTTP adapter is not implemented yet. "
            "Use provider=mock for local terminal chat, or implement the provider adapter next.");
    }

private:
    std::string provider_name_;
};

class ProviderFactory {
public:
    static std::unique_ptr<Provider> create(const Config& config) {
        if (config.provider == "mock" || config.provider == "echo") {
            return std::make_unique<EchoProvider>();
        }
        if (config.provider == "mock-agent-demo") {
            ToolCall write_demo;
            write_demo.id = "call_write_demo";
            write_demo.name = "write_file";
            write_demo.arguments = {
                {"path", "demo.py"},
                {"content", "print('hello from agent_tui')\n"},
                {"create_parent_dirs", "true"},
            };

            ToolCall done;
            done.id = "call_done";
            done.name = "Done";
            done.arguments = {{"final_answer", "demo.py is ready and was executed"}};

            ToolCall run_demo;
            run_demo.id = "call_run_demo";
            run_demo.name = "run_shell";
            run_demo.arguments = {
                {"command", "python demo.py"},
                {"cwd", "."},
                {"timeout_seconds", "10"},
                {"max_output_bytes", "2000"},
            };

            return std::make_unique<MockProvider>(std::vector<ProviderResponse>{
                ProviderResponse::tool_calls_response({write_demo}),
                ProviderResponse::tool_calls_response({run_demo}),
                ProviderResponse::tool_calls_response({done}),
            });
        }
        if (config.provider == "openai-compatible" || config.provider == "openai") {
            return std::make_unique<OpenAICompatibleProvider>(config);
        }
        return std::make_unique<PlaceholderProvider>(config.provider);
    }
};

}  // namespace agent_tui

#pragma once

#include <memory>
#include <string>
#include <utility>

#include "agent_tui/config/Config.hpp"
#include "agent_tui/llm/EchoProvider.hpp"
#include "agent_tui/llm/Provider.hpp"

namespace agent_tui {

class PlaceholderProvider final : public Provider {
public:
    explicit PlaceholderProvider(std::string provider_name) : provider_name_(std::move(provider_name)) {}

    ProviderResponse chat(const std::vector<Message>&) override {
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
        return std::make_unique<PlaceholderProvider>(config.provider);
    }
};

}  // namespace agent_tui

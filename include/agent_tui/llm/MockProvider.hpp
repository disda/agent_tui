#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "agent_tui/llm/Provider.hpp"

namespace agent_tui {

class MockProvider final : public Provider {
public:
    explicit MockProvider(std::vector<ProviderResponse> scripted_responses)
        : scripted_responses_(std::move(scripted_responses)) {}

    ProviderResponse chat(const std::vector<Message>& messages, const std::string& tools_schema_json = {}) override {
        observed_messages_ = messages;
        observed_tools_schema_json_ = tools_schema_json;
        if (index_ >= scripted_responses_.size()) {
            return ProviderResponse::error_response("MockProvider has no more scripted responses");
        }
        return scripted_responses_[index_++];
    }

    const std::vector<Message>& observed_messages() const { return observed_messages_; }
    const std::string& observed_tools_schema_json() const { return observed_tools_schema_json_; }
    std::size_t call_count() const { return index_; }

private:
    std::vector<ProviderResponse> scripted_responses_;
    std::vector<Message> observed_messages_;
    std::string observed_tools_schema_json_;
    std::size_t index_ = 0;
};

}  // namespace agent_tui

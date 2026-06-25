#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "agent_tui/llm/Provider.hpp"

namespace agent_tui {

class MockProvider final : public Provider {
public:
    explicit MockProvider(std::vector<ProviderResponse> scripted_responses)
        : scripted_responses_(std::move(scripted_responses)) {}

    ProviderResponse chat(const std::vector<Message>& messages) override {
        observed_messages_ = messages;
        if (index_ >= scripted_responses_.size()) {
            return ProviderResponse::error_response("MockProvider has no more scripted responses");
        }
        return scripted_responses_[index_++];
    }

    const std::vector<Message>& observed_messages() const { return observed_messages_; }
    std::size_t call_count() const { return index_; }

private:
    std::vector<ProviderResponse> scripted_responses_;
    std::vector<Message> observed_messages_;
    std::size_t index_ = 0;
};

}  // namespace agent_tui

#pragma once

#include <string>
#include <vector>

#include "agent_tui/llm/Provider.hpp"

namespace agent_tui {

class EchoProvider final : public Provider {
public:
    ProviderResponse chat(const std::vector<Message>& messages, const std::string& = {}) override {
        std::string last_user;
        for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
            if (it->role == Role::User) {
                last_user = it->content;
                break;
            }
        }
        if (last_user.empty()) {
            return ProviderResponse::text_response("mock assistant: hello");
        }
        return ProviderResponse::text_response("mock assistant: " + last_user);
    }
};

}  // namespace agent_tui

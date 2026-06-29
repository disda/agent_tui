#pragma once

#include <functional>
#include <string>
#include <vector>

#include "agent_tui/llm/Provider.hpp"

namespace agent_tui {

class EchoProvider final : public Provider {
public:
    ProviderResponse chat(const std::vector<Message>& messages, const std::string& = {}) override {
        return ProviderResponse::text_response(reply_for(messages));
    }

    ProviderResponse chat_stream(const std::vector<Message>& messages,
                                 const std::string&,
                                 const std::function<void(const std::string&)>& on_delta) override {
        const auto reply = reply_for(messages);
        const auto split = reply.size() / 2;
        on_delta(reply.substr(0, split));
        on_delta(reply.substr(split));
        return ProviderResponse::text_response(reply);
    }

private:
    static std::string reply_for(const std::vector<Message>& messages) {
        std::string last_user;
        for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
            if (it->role == Role::User) {
                last_user = it->content;
                break;
            }
        }
        if (last_user.empty()) {
            return "mock assistant: hello";
        }
        return "mock assistant: " + last_user;
    }
};

}  // namespace agent_tui

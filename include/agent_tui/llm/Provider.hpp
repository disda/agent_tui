#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "agent_tui/agent/Message.hpp"
#include "agent_tui/agent/ToolCall.hpp"

namespace agent_tui {

enum class ProviderResponseType {
    Text,
    ToolCalls,
    Error,
};

struct ProviderResponse {
    ProviderResponseType type = ProviderResponseType::Text;
    std::string text;
    std::vector<ToolCall> tool_calls;
    std::string error;

    static ProviderResponse text_response(std::string text) {
        ProviderResponse response;
        response.type = ProviderResponseType::Text;
        response.text = std::move(text);
        return response;
    }

    static ProviderResponse tool_calls_response(std::vector<ToolCall> calls) {
        ProviderResponse response;
        response.type = ProviderResponseType::ToolCalls;
        response.tool_calls = std::move(calls);
        return response;
    }

    static ProviderResponse error_response(std::string error) {
        ProviderResponse response;
        response.type = ProviderResponseType::Error;
        response.error = std::move(error);
        return response;
    }
};

class Provider {
public:
    virtual ~Provider() = default;

    virtual void set_interrupt_checker(std::function<bool()> interrupt_checker) {
        interrupt_checker_ = std::move(interrupt_checker);
    }

    virtual ProviderResponse chat(const std::vector<Message>& messages, const std::string& tools_schema_json = {}) = 0;

    virtual ProviderResponse chat_stream(const std::vector<Message>& messages,
                                         const std::string& tools_schema_json,
                                         const std::function<void(const std::string&)>& on_delta) {
        auto response = chat(messages, tools_schema_json);
        if (response.type == ProviderResponseType::Text && !response.text.empty()) {
            on_delta(response.text);
        }
        return response;
    }

protected:
    bool interrupted() const {
        return interrupt_checker_ && interrupt_checker_();
    }

private:
    std::function<bool()> interrupt_checker_;
};

}  // namespace agent_tui

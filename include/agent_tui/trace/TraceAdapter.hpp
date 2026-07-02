#pragma once

#include <string>
#include <vector>

#include "agent_tui/session/SessionEvent.hpp"

namespace agent_tui {

struct TraceItem {
    std::string type;
    std::string name;
    std::string data;
};

class TraceAdapter {
public:
    void on_event(const SessionEvent& e) {
        switch (e.type) {
            case SessionEventType::ToolCall:
                trace_.push_back({"tool_call", e.tool_name, ""});
                break;

            case SessionEventType::ToolStarted:
                trace_.push_back({"tool_start", e.tool_name, ""});
                break;

            case SessionEventType::ToolCompleted:
                trace_.push_back({"tool_result", e.tool_name, e.content});
                break;

            case SessionEventType::ModelStarted:
                trace_.push_back({"llm", "start", ""});
                break;

            case SessionEventType::ModelCompleted:
                trace_.push_back({"llm", "end", e.content});
                break;

            default:
                break;
        }
    }

    const std::vector<TraceItem>& items() const {
        return trace_;
    }

    void clear() {
        trace_.clear();
    }

private:
    std::vector<TraceItem> trace_;
};

} // namespace agent_tui
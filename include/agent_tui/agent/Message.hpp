#pragma once

#include <string>
#include <vector>

#include "agent_tui/agent/ToolCall.hpp"

namespace agent_tui {

enum class Role {
    System,
    User,
    Assistant,
    Tool,
};

struct Message {
    Role role = Role::User;
    std::string content;
    std::string tool_call_id;
    std::vector<ToolCall> tool_calls;
};

}  // namespace agent_tui

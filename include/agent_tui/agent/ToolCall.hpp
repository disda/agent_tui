#pragma once

#include <map>
#include <string>

namespace agent_tui {

using JsonLike = std::map<std::string, std::string>;

struct ToolCall {
    std::string id;
    std::string name;
    JsonLike arguments;
};

}  // namespace agent_tui

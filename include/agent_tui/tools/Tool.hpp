#pragma once

#include <string>
#include <utility>

#include "agent_tui/agent/ToolCall.hpp"

namespace agent_tui {

enum class PermissionMode {
    Auto,
    Confirm,
};

struct ToolResult {
    bool ok = true;
    std::string output;
    std::string error;

    static ToolResult success(std::string text) {
        ToolResult result;
        result.ok = true;
        result.output = std::move(text);
        return result;
    }

    static ToolResult failure(std::string message) {
        ToolResult result;
        result.ok = false;
        result.error = std::move(message);
        return result;
    }
};

class Tool {
public:
    virtual ~Tool() = default;

    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual std::string parameters_schema_json() const {
        return R"({"type":"object","properties":{}})";
    }
    virtual PermissionMode permission_mode() const = 0;
    virtual ToolResult run(const JsonLike& arguments) = 0;
};

}  // namespace agent_tui

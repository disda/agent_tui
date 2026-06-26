#pragma once

#include <string>
#include <utility>

#include "agent_tui/agent/ToolCall.hpp"

namespace agent_tui {

enum class ApprovalType {
    Approve,
    Deny,
    Edit,
    Feedback,
};

struct ApprovalDecision {
    ApprovalType type = ApprovalType::Deny;
    JsonLike edited_arguments;
    std::string feedback;

    static ApprovalDecision approve() {
        ApprovalDecision decision;
        decision.type = ApprovalType::Approve;
        return decision;
    }

    static ApprovalDecision deny(std::string reason = {}) {
        ApprovalDecision decision;
        decision.type = ApprovalType::Deny;
        decision.feedback = std::move(reason);
        return decision;
    }

    static ApprovalDecision edit(JsonLike arguments) {
        ApprovalDecision decision;
        decision.type = ApprovalType::Edit;
        decision.edited_arguments = std::move(arguments);
        return decision;
    }

    static ApprovalDecision user_feedback(std::string text) {
        ApprovalDecision decision;
        decision.type = ApprovalType::Feedback;
        decision.feedback = std::move(text);
        return decision;
    }
};

}  // namespace agent_tui

#pragma once

#include <cstddef>

namespace agent_tui {

// L2 Agent 上下文管理器（轻量版）
class ContextManager {
public:
    explicit ContextManager(std::size_t max_events)
        : max_events_(max_events) {}

    // 90% 触发压缩
    bool should_compact(std::size_t current_size) const {
        if (max_events_ == 0) return false;
        return current_size >= static_cast<std::size_t>(max_events_ * 0.9);
    }

private:
    std::size_t max_events_;
};

} // namespace agent_tui
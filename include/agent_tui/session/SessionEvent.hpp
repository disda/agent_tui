#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

#include "agent_tui/agent/ToolCall.hpp"

namespace agent_tui {

enum class SessionEventType {
    UserInput,
    AssistantMessage,
    ToolCall,
    PermissionRequested,
    ToolResult,
    PermissionDenied,
    UserFeedback,
    Error,
};

inline std::string session_event_type_name(SessionEventType type) {
    switch (type) {
        case SessionEventType::UserInput:
            return "user_input";
        case SessionEventType::AssistantMessage:
            return "assistant_message";
        case SessionEventType::ToolCall:
            return "tool_call";
        case SessionEventType::PermissionRequested:
            return "permission_requested";
        case SessionEventType::ToolResult:
            return "tool_result";
        case SessionEventType::PermissionDenied:
            return "permission_denied";
        case SessionEventType::UserFeedback:
            return "user_feedback";
        case SessionEventType::Error:
            return "error";
    }
    return "unknown";
}

inline std::string session_now_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto now_time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now_time);
#else
    gmtime_r(&now_time, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

inline std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                out << "\\\\";
                break;
            case '"':
                out << "\\\"";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                out << ch;
                break;
        }
    }
    return out.str();
}

inline std::string json_like_to_json(const JsonLike& value) {
    std::ostringstream out;
    out << "{";
    bool first = true;
    for (const auto& [key, item] : value) {
        if (!first) {
            out << ",";
        }
        first = false;
        out << "\"" << json_escape(key) << "\":\"" << json_escape(item) << "\"";
    }
    out << "}";
    return out.str();
}

struct SessionEvent {
    SessionEventType type = SessionEventType::UserInput;
    std::string content;
    std::string tool_call_id;
    std::string tool_name;
    std::string timestamp = session_now_timestamp();
    JsonLike arguments;

    static SessionEvent user_input(std::string content) {
        SessionEvent event;
        event.type = SessionEventType::UserInput;
        event.content = std::move(content);
        return event;
    }

    static SessionEvent assistant_message(std::string content) {
        SessionEvent event;
        event.type = SessionEventType::AssistantMessage;
        event.content = std::move(content);
        return event;
    }

    static SessionEvent tool_call(const ToolCall& call) {
        SessionEvent event;
        event.type = SessionEventType::ToolCall;
        event.tool_call_id = call.id;
        event.tool_name = call.name;
        event.arguments = call.arguments;
        return event;
    }

    static SessionEvent permission_requested(const ToolCall& call) {
        SessionEvent event;
        event.type = SessionEventType::PermissionRequested;
        event.tool_call_id = call.id;
        event.tool_name = call.name;
        event.arguments = call.arguments;
        return event;
    }

    static SessionEvent tool_result(std::string call_id, std::string tool_name, std::string content) {
        SessionEvent event;
        event.type = SessionEventType::ToolResult;
        event.tool_call_id = std::move(call_id);
        event.tool_name = std::move(tool_name);
        event.content = std::move(content);
        return event;
    }

    static SessionEvent permission_denied(std::string call_id, std::string tool_name, std::string content) {
        SessionEvent event;
        event.type = SessionEventType::PermissionDenied;
        event.tool_call_id = std::move(call_id);
        event.tool_name = std::move(tool_name);
        event.content = std::move(content);
        return event;
    }

    static SessionEvent user_feedback(std::string call_id, std::string tool_name, std::string content) {
        SessionEvent event;
        event.type = SessionEventType::UserFeedback;
        event.tool_call_id = std::move(call_id);
        event.tool_name = std::move(tool_name);
        event.content = std::move(content);
        return event;
    }

    static SessionEvent error(std::string content) {
        SessionEvent event;
        event.type = SessionEventType::Error;
        event.content = std::move(content);
        return event;
    }

    std::string to_json_line() const {
        std::ostringstream out;
        out << "{";
        out << "\"timestamp\":\"" << json_escape(timestamp) << "\",";
        out << "\"type\":\"" << session_event_type_name(type) << "\",";
        out << "\"content\":\"" << json_escape(content) << "\",";
        out << "\"tool_call_id\":\"" << json_escape(tool_call_id) << "\",";
        out << "\"tool_name\":\"" << json_escape(tool_name) << "\",";
        out << "\"arguments\":" << json_like_to_json(arguments);
        out << "}";
        return out.str();
    }
};

}  // namespace agent_tui

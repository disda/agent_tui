#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "agent_tui/agent/AgentResult.hpp"
#include "agent_tui/llm/Provider.hpp"
#include "agent_tui/permissions/ApprovalService.hpp"
#include "agent_tui/session/SessionHistory.hpp"
#include "agent_tui/tools/ToolRegistry.hpp"

namespace agent_tui {

struct AgentRunObserver {
    std::function<void(const SessionEvent&)> on_event;
    std::function<void(const std::string&)> on_assistant_delta;
};

class AgentRunner {
public:
    static constexpr int kDefaultMaxLoops = 25;

    AgentRunner(Provider& provider, ToolRegistry& tools, int max_loops = kDefaultMaxLoops)
        : provider_(provider), tools_(tools), max_loops_(max_loops) {}

    AgentRunner(Provider& provider, ToolRegistry& tools, SessionHistory& session_history, int max_loops = kDefaultMaxLoops)
        : provider_(provider), tools_(tools), session_history_(&session_history), max_loops_(max_loops) {}

    AgentRunner(Provider& provider, ToolRegistry& tools, ApprovalService& approval_service, int max_loops = kDefaultMaxLoops)
        : provider_(provider), tools_(tools), approval_service_(&approval_service), max_loops_(max_loops) {}

    AgentRunner(Provider& provider,
                ToolRegistry& tools,
                ApprovalService& approval_service,
                SessionHistory& session_history,
                int max_loops = kDefaultMaxLoops)
        : provider_(provider), tools_(tools), approval_service_(&approval_service), session_history_(&session_history), max_loops_(max_loops) {}

    AgentResult run(std::vector<Message> messages) {
        log_initial_messages(messages);

        for (int step = 0; step < max_loops_; ++step) {
            if (is_interrupted()) {
                return interrupted_result(messages);
            }
            const auto tools_schema_json = tools_.tools_schema_json(tool_exposure_policy_);
            provider_.set_interrupt_checker(interrupt_checker_);
            log_model_started();
            auto response = provider_.chat_stream(
                messages,
                tools_schema_json,
                [&](const std::string& delta) {
                    if (observer_.on_assistant_delta) {
                        observer_.on_assistant_delta(delta);
                    }
                });

            if (response.type == ProviderResponseType::Text) {
                messages.push_back(Message{Role::Assistant, response.text, {}});
                log_assistant(response.text);
                log_model_completed(response.text);
                last_messages_ = messages;
                return AgentResult::done(response.text);
            }

            if (response.type == ProviderResponseType::Error) {
                log_model_completed(response.error);
                log_error(response.error);
                last_messages_ = messages;
                return AgentResult::failed(response.error);
            }

            log_model_completed("tool_calls");
            messages.push_back(Message{Role::Assistant, {}, {}, response.tool_calls});
            for (const auto& call : response.tool_calls) {
                log_tool_call(call);

                if (call.name == "Done") {
                    auto it = call.arguments.find("final_answer");
                    const auto answer = it == call.arguments.end() ? std::string{"Done"} : it->second;
                    messages.push_back(Message{Role::Assistant, answer, {}});
                    log_assistant(answer);
                    log_model_completed(answer);
                    last_messages_ = messages;
                    return AgentResult::done(answer);
                }

                if (!tool_exposure_policy_.exposes(call.name)) {
                    auto message = "Tool not allowed by exposure policy: " + call.name;
                    messages.push_back(Message{Role::Tool, message, call.id});
                    log_error(message);
                    log_tool_result(call.id, call.name, message);
                    continue;
                }

                auto* tool = tools_.find(call.name);
                if (tool == nullptr) {
                    auto message = "Tool not found: " + call.name;
                    messages.push_back(Message{Role::Tool, message, call.id});
                    log_error(message);
                    log_tool_result(call.id, call.name, message);
                    continue;
                }

                JsonLike arguments = call.arguments;
                if (tool->permission_mode() == PermissionMode::Confirm) {
                    if (approval_service_ == nullptr) {
                        auto message = std::string{"Permission required but no approval service configured."};
                        messages.push_back(Message{Role::Tool, message, call.id});
                        log_error(message);
                        log_tool_result(call.id, call.name, message);
                        continue;
                    }

                    log_permission_requested(call);
                    auto decision = approval_service_->request(call, *tool);
                    if (decision.type == ApprovalType::Deny) {
                        auto message = decision.feedback.empty() ? std::string{"User denied permission."}
                                                                : std::string{"User denied permission: "} + decision.feedback;
                        messages.push_back(Message{Role::Tool, message, call.id});
                        log_permission_denied(call.id, call.name, message);
                        log_tool_result(call.id, call.name, message);
                        continue;
                    }
                    if (decision.type == ApprovalType::Feedback) {
                        auto message = "User feedback: " + decision.feedback;
                        messages.push_back(Message{Role::Tool, message, call.id});
                        log_user_feedback(call.id, call.name, decision.feedback);
                        log_tool_result(call.id, call.name, message);
                        continue;
                    }
                    if (decision.type == ApprovalType::Edit) {
                        arguments = decision.edited_arguments;
                    }
                }

                if (is_interrupted()) {
                    return interrupted_result(messages);
                }
                log_tool_started(call);
                auto result = tool->run(arguments);
                auto output = result.ok ? result.output : result.error;
                messages.push_back(Message{
                    Role::Tool,
                    output,
                    call.id,
                });
                log_tool_completed(call.id, call.name, output);
                log_tool_result(call.id, call.name, output);
                if (!result.ok) {
                    log_error(output);
                }
            }
        }

        auto message = std::string{"Max loop count exceeded."};
        log_error(message);
        last_messages_ = messages;
        return AgentResult::failed(message);
    }

    const std::vector<Message>& last_messages() const { return last_messages_; }

    void request_interrupt() {
        interrupted_ = true;
    }

    bool interrupted() const {
        return interrupted_;
    }

    void set_tool_exposure_policy(ToolExposurePolicy policy) {
        tool_exposure_policy_ = std::move(policy);
    }

    void set_observer(AgentRunObserver observer) {
        observer_ = std::move(observer);
    }

    void set_interrupt_checker(std::function<bool()> interrupt_checker) {
        interrupt_checker_ = std::move(interrupt_checker);
        provider_.set_interrupt_checker(interrupt_checker_);
    }

private:
    bool is_interrupted() const {
        return interrupted_ || (interrupt_checker_ && interrupt_checker_());
    }

    void record_event(SessionEvent event) {
        if (session_history_ != nullptr) {
            session_history_->add(event);
        }
        if (observer_.on_event) {
            observer_.on_event(event);
        }
    }

    void log_initial_messages(const std::vector<Message>& messages) {
        if (session_history_ == nullptr) {
            return;
        }
        for (const auto& message : messages) {
            if (message.role == Role::User) {
                record_event(SessionEvent::user_input(message.content));
            }
        }
    }

    void log_assistant(const std::string& content) {
        record_event(SessionEvent::assistant_message(content));
    }

    void log_model_started() {
        record_event(SessionEvent::model_started());
    }

    void log_model_completed(const std::string& content) {
        record_event(SessionEvent::model_completed(content));
    }

    void log_tool_call(const ToolCall& call) {
        record_event(SessionEvent::tool_call(call));
    }

    void log_permission_requested(const ToolCall& call) {
        record_event(SessionEvent::permission_requested(call));
    }

    void log_tool_result(const std::string& call_id, const std::string& tool_name, const std::string& content) {
        record_event(SessionEvent::tool_result(call_id, tool_name, content));
    }

    void log_tool_started(const ToolCall& call) {
        record_event(SessionEvent::tool_started(call));
    }

    void log_tool_completed(const std::string& call_id, const std::string& tool_name, const std::string& content) {
        record_event(SessionEvent::tool_completed(call_id, tool_name, content));
    }

    void log_permission_denied(const std::string& call_id, const std::string& tool_name, const std::string& content) {
        record_event(SessionEvent::permission_denied(call_id, tool_name, content));
    }

    void log_user_feedback(const std::string& call_id, const std::string& tool_name, const std::string& content) {
        record_event(SessionEvent::user_feedback(call_id, tool_name, content));
    }

    void log_error(const std::string& content) {
        record_event(SessionEvent::error(content));
    }

    AgentResult interrupted_result(const std::vector<Message>& messages) {
        auto message = std::string{"Interrupted by user."};
        record_event(SessionEvent::interrupted(message));
        log_error(message);
        last_messages_ = messages;
        return AgentResult::failed(message);
    }

    Provider& provider_;
    ToolRegistry& tools_;
    ApprovalService* approval_service_ = nullptr;
    SessionHistory* session_history_ = nullptr;
    ToolExposurePolicy tool_exposure_policy_;
    AgentRunObserver observer_;
    std::function<bool()> interrupt_checker_;
    int max_loops_ = kDefaultMaxLoops;
    std::vector<Message> last_messages_;
    bool interrupted_ = false;
};

}  // namespace agent_tui

#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "agent_tui/config/Config.hpp"
#include "agent_tui/llm/Provider.hpp"

namespace agent_tui {

namespace openai_compatible_detail {

using Json = nlohmann::json;

inline std::string role_to_string(Role role) {
    switch (role) {
        case Role::System: return "system";
        case Role::User: return "user";
        case Role::Assistant: return "assistant";
        case Role::Tool: return "tool";
    }
    return "user";
}

inline std::string json_value_to_argument_string(const Json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_null()) {
        return {};
    }
    return value.dump();
}

inline JsonLike json_object_to_arguments(const Json& object) {
    JsonLike result;
    if (!object.is_object()) {
        return result;
    }
    for (auto it = object.begin(); it != object.end(); ++it) {
        result[it.key()] = json_value_to_argument_string(it.value());
    }
    return result;
}

inline std::string escape_control_chars_in_json_strings(const std::string& value) {
    std::string escaped_value;
    escaped_value.reserve(value.size());
    bool in_string = false;
    bool escaped = false;
    for (const char ch : value) {
        if (escaped) {
            escaped_value.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped_value.push_back(ch);
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            escaped_value.push_back(ch);
            continue;
        }
        if (in_string && ch == '\n') {
            escaped_value += "\\n";
            continue;
        }
        if (in_string && ch == '\r') {
            escaped_value += "\\r";
            continue;
        }
        if (in_string && ch == '\t') {
            escaped_value += "\\t";
            continue;
        }
        escaped_value.push_back(ch);
    }
    return escaped_value;
}

inline JsonLike parse_arguments_json(const Json& arguments) {
    if (arguments.is_object()) {
        return json_object_to_arguments(arguments);
    }
    if (!arguments.is_string()) {
        return {};
    }
    const auto parsed = Json::parse(arguments.get<std::string>(), nullptr, false);
    if (parsed.is_object()) {
        return json_object_to_arguments(parsed);
    }
    const auto compatible = Json::parse(escape_control_chars_in_json_strings(arguments.get<std::string>()), nullptr, false);
    if (!compatible.is_object()) {
        return {};
    }
    return json_object_to_arguments(compatible);
}

inline Json json_from_arguments(const JsonLike& arguments) {
    Json result = Json::object();
    for (const auto& [key, value] : arguments) {
        result[key] = value;
    }
    return result;
}

inline std::vector<ToolCall> parse_tool_calls(const Json& tool_calls_json) {
    std::vector<ToolCall> calls;
    if (!tool_calls_json.is_array()) {
        return calls;
    }
    for (const auto& item : tool_calls_json) {
        const auto function = item.value("function", Json::object());
        if (!function.is_object()) {
            continue;
        }
        ToolCall call;
        call.id = item.value("id", "");
        call.name = function.value("name", "");
        if (call.name.empty()) {
            continue;
        }
        const auto arguments = function.find("arguments");
        if (arguments != function.end()) {
            call.arguments = parse_arguments_json(*arguments);
        }
        calls.push_back(std::move(call));
    }
    return calls;
}

inline std::string shell_quote(const std::filesystem::path& path) {
    auto value = path.generic_string();
    std::string quoted = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += "\"";
    return quoted;
}

inline std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

inline bool write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << content;
    return static_cast<bool>(output);
}

}  // namespace openai_compatible_detail

class OpenAICompatibleProvider final : public Provider {
public:
    explicit OpenAICompatibleProvider(Config config) : config_(std::move(config)) {}

    ProviderResponse chat(const std::vector<Message>& messages, const std::string& tools_schema_json = {}) override {
        const auto api_key = resolve_api_key(config_);
        if (api_key.empty()) {
            return ProviderResponse::error_response("missing API key: set api_key or api_key_env");
        }

        const auto api_base = config_.api_base.empty() ? std::string{"https://api.openai.com/v1"} : config_.api_base;
        const auto request_body = build_request_body(config_, messages, tools_schema_json);
        if (std::getenv("AGENT_TUI_DEBUG_REQUEST") != nullptr) {
            std::filesystem::create_directories("output");
            openai_compatible_detail::write_file(std::filesystem::path{"output"} / "last-openai-request.json", request_body);
        }

        return chat_with_libcurl(api_base, api_key, request_body);
    }

    ProviderResponse chat_stream(const std::vector<Message>& messages,
                                 const std::string& tools_schema_json,
                                 const std::function<void(const std::string&)>& on_delta) override {
        const auto api_key = resolve_api_key(config_);
        if (api_key.empty()) {
            return ProviderResponse::error_response("missing API key: set api_key or api_key_env");
        }

        const auto api_base = config_.api_base.empty() ? std::string{"https://api.openai.com/v1"} : config_.api_base;
        const auto request_body = build_request_body(config_, messages, tools_schema_json, true, true);
        return chat_stream_with_libcurl(api_base, api_key, request_body, on_delta);
    }

    static std::string build_request_body(const Config& config,
                                          const std::vector<Message>& messages,
                                          const std::string& tools_schema_json = {},
                                          bool include_tools = true,
                                          bool stream = false) {
        using openai_compatible_detail::Json;
        Json body;
        body["model"] = config.model;
        body["messages"] = Json::array();
        for (const auto& message : messages) {
            Json item;
            if (message.role == Role::Tool) {
                item["role"] = "tool";
                item["tool_call_id"] = message.tool_call_id;
                item["content"] = message.content;
            } else if (message.role == Role::Assistant && !message.tool_calls.empty()) {
                item["role"] = "assistant";
                item["content"] = message.content;
                item["tool_calls"] = Json::array();
                for (const auto& call : message.tool_calls) {
                    item["tool_calls"].push_back({
                        {"id", call.id},
                        {"type", "function"},
                        {"function", {
                            {"name", call.name},
                            {"arguments", openai_compatible_detail::json_from_arguments(call.arguments).dump()},
                        }},
                    });
                }
            } else {
                item["role"] = openai_compatible_detail::role_to_string(message.role);
                item["content"] = message.content;
            }
            body["messages"].push_back(std::move(item));
        }
        if (include_tools && !tools_schema_json.empty()) {
            auto tools = Json::parse(tools_schema_json, nullptr, false);
            if (!tools.is_discarded()) {
                body["tools"] = std::move(tools);
                body["tool_choice"] = "auto";
            }
        }
        body["stream"] = stream;
        return body.dump();
    }

    static std::string build_request_body(const Config& config, const std::vector<Message>& messages, bool include_tools, bool stream = false) {
        return build_request_body(config, messages, std::string{}, include_tools, stream);
    }

    static ProviderResponse parse_response_body(const std::string& body) {
        using openai_compatible_detail::Json;
        const auto parsed = Json::parse(body, nullptr, false);
        if (parsed.is_discarded()) {
            return ProviderResponse::error_response("failed to parse openai-compatible response");
        }

        if (parsed.contains("error") && parsed["error"].is_object()) {
            const auto message = parsed["error"].value("message", "");
            if (!message.empty()) {
                return ProviderResponse::error_response(message);
            }
        }

        const auto choices = parsed.find("choices");
        if (choices != parsed.end() && choices->is_array() && !choices->empty()) {
            const auto message = (*choices)[0].value("message", Json::object());
            if (message.is_object()) {
                const auto tool_calls = openai_compatible_detail::parse_tool_calls(message.value("tool_calls", Json::array()));
                if (!tool_calls.empty()) {
                    return ProviderResponse::tool_calls_response(tool_calls);
                }
                const auto content = message.find("content");
                if (content != message.end() && content->is_string()) {
                    return ProviderResponse::text_response(content->get<std::string>());
                }
            }
        }

        return ProviderResponse::error_response("failed to parse openai-compatible response");
    }

    static ProviderResponse parse_stream_chunks_for_test(const std::vector<std::string>& chunks) {
        return parse_stream_chunks(chunks, [](const std::string&) {});
    }

    static std::string resolve_api_key(const Config& config) {
        if (!config.api_key.empty()) {
            return config.api_key;
        }
        if (config.api_key_env.empty()) {
            return {};
        }
        const char* value = std::getenv(config.api_key_env.c_str());
        if (value == nullptr) {
            return {};
        }
        return value;
    }

private:
    struct StreamToolCallPart {
        std::string id;
        std::string name;
        std::string arguments;
    };

    struct StreamAccumulator {
        std::string text;
        std::map<int, StreamToolCallPart> tool_calls;
    };

    struct CurlStreamContext {
        StreamAccumulator accumulator;
        std::string raw;
        std::string pending_line;
        const std::function<void(const std::string&)>* on_delta = nullptr;
    };

    static std::string curl_error_message(CURLcode code, long status_code, const std::string& response_body) {
        std::ostringstream out;
        if (code != CURLE_OK) {
            out << "HTTP transport failed: " << curl_easy_strerror(code);
            return out.str();
        }
        out << "HTTP " << status_code;
        if (!response_body.empty()) {
            out << ": " << response_body.substr(0, 800);
        }
        return out.str();
    }

    static size_t curl_write_to_string(char* ptr, size_t size, size_t nmemb, void* userdata) {
        const auto bytes = size * nmemb;
        auto* output = static_cast<std::string*>(userdata);
        output->append(ptr, bytes);
        return bytes;
    }

    static void consume_stream_line(CurlStreamContext& context, std::string line) {
        context.raw += line;
        const auto chunk = normalize_stream_chunk(std::move(line));
        if (!chunk.empty()) {
            consume_stream_chunk(chunk, context.accumulator, *context.on_delta);
        }
    }

    static size_t curl_write_stream(char* ptr, size_t size, size_t nmemb, void* userdata) {
        const auto bytes = size * nmemb;
        auto* context = static_cast<CurlStreamContext*>(userdata);
        context->pending_line.append(ptr, bytes);
        std::size_t newline = 0;
        while ((newline = context->pending_line.find('\n')) != std::string::npos) {
            auto line = context->pending_line.substr(0, newline + 1);
            context->pending_line.erase(0, newline + 1);
            consume_stream_line(*context, std::move(line));
        }
        return bytes;
    }

    static curl_slist* append_header(curl_slist* headers, const std::string& header) {
        return curl_slist_append(headers, header.c_str());
    }

    ProviderResponse chat_with_libcurl(const std::string& api_base,
                                       const std::string& api_key,
                                       const std::string& request_body) const {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
        if (!curl) {
            return ProviderResponse::error_response("failed to initialize libcurl");
        }

        std::string response_body;
        const auto url = api_base + "/chat/completions";
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, request_body.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(request_body.size()));
        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, static_cast<long>(config_.timeout_seconds));
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, curl_write_to_string);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response_body);

        curl_slist* headers = nullptr;
        headers = append_header(headers, "Content-Type: application/json");
        headers = append_header(headers, "Accept: application/json");
        headers = append_header(headers, "Authorization: Bearer " + api_key);
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);

        const CURLcode code = curl_easy_perform(curl.get());
        long status_code = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status_code);
        if (headers != nullptr) {
            curl_slist_free_all(headers);
        }
        if (code != CURLE_OK || status_code < 200 || status_code >= 300) {
            return ProviderResponse::error_response(curl_error_message(code, status_code, response_body));
        }
        return parse_response_body(response_body);
    }

    ProviderResponse chat_stream_with_libcurl(const std::string& api_base,
                                              const std::string& api_key,
                                              const std::string& request_body,
                                              const std::function<void(const std::string&)>& on_delta) const {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
        if (!curl) {
            return ProviderResponse::error_response("failed to initialize libcurl");
        }

        CurlStreamContext context;
        context.on_delta = &on_delta;
        const auto url = api_base + "/chat/completions";
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, request_body.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(request_body.size()));
        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, static_cast<long>(config_.timeout_seconds));
        curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_TIME, static_cast<long>((std::max)(config_.timeout_seconds, 300)));
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, curl_write_stream);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &context);

        curl_slist* headers = nullptr;
        headers = append_header(headers, "Content-Type: application/json");
        headers = append_header(headers, "Accept: text/event-stream");
        headers = append_header(headers, "Authorization: Bearer " + api_key);
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);

        const CURLcode code = curl_easy_perform(curl.get());
        if (!context.pending_line.empty()) {
            consume_stream_line(context, context.pending_line);
            context.pending_line.clear();
        }
        long status_code = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status_code);
        if (headers != nullptr) {
            curl_slist_free_all(headers);
        }
        if (code != CURLE_OK || status_code < 200 || status_code >= 300) {
            return ProviderResponse::error_response(curl_error_message(code, status_code, context.raw));
        }
        auto response = finalize_stream(context.accumulator);
        if (response.type != ProviderResponseType::Error) {
            return response;
        }
        return parse_response_body(context.raw);
    }

    static ProviderResponse parse_stream_chunks(const std::vector<std::string>& chunks,
                                                const std::function<void(const std::string&)>& on_delta) {
        StreamAccumulator accumulator;
        for (const auto& chunk : chunks) {
            const auto normalized = normalize_stream_chunk(chunk);
            if (!normalized.empty()) {
                consume_stream_chunk(normalized, accumulator, on_delta);
            }
        }
        return finalize_stream(accumulator);
    }

    static std::string normalize_stream_chunk(std::string line) {
        if (line.rfind("data:", 0) == 0) {
            line = line.substr(5);
        }
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            line.erase(line.begin());
        }
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() || line == "[DONE]") {
            return {};
        }
        return line;
    }

    static void consume_stream_chunk(const std::string& chunk,
                                     StreamAccumulator& accumulator,
                                     const std::function<void(const std::string&)>& on_delta) {
        using openai_compatible_detail::Json;
        const auto parsed = Json::parse(chunk, nullptr, false);
        if (parsed.is_discarded()) {
            return;
        }
        const auto choices = parsed.find("choices");
        if (choices == parsed.end() || !choices->is_array()) {
            return;
        }
        for (const auto& choice : *choices) {
            const auto delta = choice.value("delta", Json::object());
            if (!delta.is_object()) {
                continue;
            }
            const auto content = delta.find("content");
            if (content != delta.end() && content->is_string()) {
                const auto text = content->get<std::string>();
                accumulator.text += text;
                on_delta(text);
            }
            const auto tool_calls = delta.find("tool_calls");
            if (tool_calls == delta.end() || !tool_calls->is_array()) {
                continue;
            }
            for (const auto& item : *tool_calls) {
                const int index = item.value("index", 0);
                auto& part = accumulator.tool_calls[index];
                if (item.contains("id") && item["id"].is_string()) {
                    part.id = item["id"].get<std::string>();
                }
                const auto function = item.value("function", Json::object());
                if (!function.is_object()) {
                    continue;
                }
                if (function.contains("name") && function["name"].is_string()) {
                    part.name = function["name"].get<std::string>();
                }
                const auto arguments = function.find("arguments");
                if (arguments == function.end()) {
                    continue;
                }
                if (arguments->is_string()) {
                    part.arguments += arguments->get<std::string>();
                } else if (arguments->is_object()) {
                    part.arguments = arguments->dump();
                }
            }
        }
    }

    static ProviderResponse finalize_stream(const StreamAccumulator& accumulator) {
        if (!accumulator.tool_calls.empty()) {
            std::vector<ToolCall> calls;
            for (const auto& [index, part] : accumulator.tool_calls) {
                if (part.name.empty()) {
                    continue;
                }
                ToolCall call;
                call.id = part.id.empty() ? "stream_tool_call_" + std::to_string(index) : part.id;
                call.name = part.name;
                const auto arguments = openai_compatible_detail::Json::parse(part.arguments, nullptr, false);
                call.arguments = openai_compatible_detail::parse_arguments_json(arguments);
                calls.push_back(std::move(call));
            }
            if (!calls.empty()) {
                return ProviderResponse::tool_calls_response(std::move(calls));
            }
        }
        if (!accumulator.text.empty()) {
            return ProviderResponse::text_response(accumulator.text);
        }
        return ProviderResponse::error_response("failed to parse openai-compatible streaming response");
    }

    Config config_;
};

}  // namespace agent_tui

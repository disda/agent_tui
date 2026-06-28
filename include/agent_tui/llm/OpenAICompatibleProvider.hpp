#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "agent_tui/config/Config.hpp"
#include "agent_tui/llm/Provider.hpp"

namespace agent_tui {

namespace openai_compatible_detail {

inline std::string json_escape_string(const std::string& value) {
    std::ostringstream out;
    for (const char ch : value) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << ch; break;
        }
    }
    return out.str();
}

inline std::string role_to_string(Role role) {
    switch (role) {
        case Role::System: return "system";
        case Role::User: return "user";
        case Role::Assistant: return "assistant";
        case Role::Tool: return "tool";
    }
    return "user";
}

inline std::string json_unescape_string(const std::string& value) {
    std::ostringstream out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\' || i + 1 >= value.size()) {
            out << value[i];
            continue;
        }
        const char next = value[++i];
        switch (next) {
            case 'n': out << '\n'; break;
            case 'r': out << '\r'; break;
            case 't': out << '\t'; break;
            case '"': out << '"'; break;
            case '\\': out << '\\'; break;
            default: out << next; break;
        }
    }
    return out.str();
}

inline std::string extract_json_string_field(const std::string& body, const std::string& field) {
    const auto key = "\"" + field + "\"";
    auto pos = body.find(key);
    if (pos == std::string::npos) {
        return {};
    }
    pos = body.find(':', pos + key.size());
    if (pos == std::string::npos) {
        return {};
    }
    pos = body.find('"', pos + 1);
    if (pos == std::string::npos) {
        return {};
    }

    std::string value;
    bool escaped = false;
    for (std::size_t i = pos + 1; i < body.size(); ++i) {
        const char ch = body[i];
        if (escaped) {
            value.push_back('\\');
            value.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            return json_unescape_string(value);
        }
        value.push_back(ch);
    }
    return {};
}

inline JsonLike parse_flat_json_string_object(const std::string& body) {
    JsonLike result;
    std::size_t pos = 0;
    auto skip_ws = [&](std::size_t& index) {
        while (index < body.size() && std::isspace(static_cast<unsigned char>(body[index])) != 0) {
            ++index;
        }
    };
    auto parse_json_string = [&](std::size_t& index) {
        std::string value;
        bool escaped = false;
        if (index >= body.size() || body[index] != '"') {
            return value;
        }
        for (++index; index < body.size(); ++index) {
            const char ch = body[index];
            if (escaped) {
                value.push_back('\\');
                value.push_back(ch);
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (ch == '"') {
                ++index;
                return json_unescape_string(value);
            }
            value.push_back(ch);
        }
        return json_unescape_string(value);
    };
    auto parse_compound = [&](std::size_t& index) {
        const char open = body[index];
        const char close = open == '{' ? '}' : ']';
        const auto start = index;
        int depth = 0;
        bool in_string = false;
        bool escaped = false;
        for (; index < body.size(); ++index) {
            const char ch = body[index];
            if (in_string) {
                if (escaped) {
                    escaped = false;
                } else if (ch == '\\') {
                    escaped = true;
                } else if (ch == '"') {
                    in_string = false;
                }
                continue;
            }
            if (ch == '"') {
                in_string = true;
                continue;
            }
            if (ch == open) {
                ++depth;
            } else if (ch == close) {
                --depth;
                if (depth == 0) {
                    ++index;
                    return body.substr(start, index - start);
                }
            }
        }
        return body.substr(start);
    };

    while (pos < body.size()) {
        skip_ws(pos);
        if (pos < body.size() && (body[pos] == '{' || body[pos] == ',')) {
            ++pos;
            continue;
        }
        pos = body.find('"', pos);
        if (pos == std::string::npos) {
            break;
        }
        auto key = parse_json_string(pos);
        skip_ws(pos);
        if (pos >= body.size() || body[pos] != ':') {
            break;
        }
        ++pos;
        skip_ws(pos);
        if (pos == std::string::npos) {
            break;
        }

        if (pos < body.size() && body[pos] == '"') {
            result[key] = parse_json_string(pos);
            continue;
        }
        if (pos < body.size() && (body[pos] == '{' || body[pos] == '[')) {
            result[key] = parse_compound(pos);
            continue;
        }

        const auto value_start = pos;
        while (pos < body.size() && body[pos] != ',' && body[pos] != '}') {
            ++pos;
        }
        auto value = body.substr(value_start, pos - value_start);
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
            return std::isspace(ch) == 0;
        }));
        value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
            return std::isspace(ch) == 0;
        }).base(), value.end());
        result[key] = value;
    }
    return result;
}

inline std::string flat_json_string_object(const JsonLike& object) {
    std::ostringstream out;
    out << "{";
    bool first = true;
    for (const auto& item : object) {
        if (!first) {
            out << ",";
        }
        first = false;
        out << "\"" << json_escape_string(item.first) << "\":\"" << json_escape_string(item.second) << "\"";
    }
    out << "}";
    return out.str();
}

inline std::string tool_calls_json(const std::vector<ToolCall>& calls) {
    std::ostringstream out;
    out << "[";
    bool first = true;
    for (const auto& call : calls) {
        if (!first) {
            out << ",";
        }
        first = false;
        out << "{";
        out << "\"id\":\"" << json_escape_string(call.id) << "\",";
        out << "\"type\":\"function\",";
        out << "\"function\":{";
        out << "\"name\":\"" << json_escape_string(call.name) << "\",";
        out << "\"arguments\":\"" << json_escape_string(flat_json_string_object(call.arguments)) << "\"";
        out << "}";
        out << "}";
    }
    out << "]";
    return out.str();
}

inline std::vector<ToolCall> extract_tool_calls(const std::string& body) {
    std::vector<ToolCall> calls;
    const std::regex pattern(
        R"REGEX("id"\s*:\s*"([^"]+)"[\s\S]*?"function"\s*:\s*\{[\s\S]*?"name"\s*:\s*"([^"]+)"[\s\S]*?"arguments"\s*:\s*"((?:\\.|[^"\\])*)")REGEX");
    auto begin = std::sregex_iterator(body.begin(), body.end(), pattern);
    const auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        ToolCall call;
        call.id = (*it)[1].str();
        call.name = (*it)[2].str();
        call.arguments = parse_flat_json_string_object(json_unescape_string((*it)[3].str()));
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

inline std::string curl_config_path(const std::filesystem::path& path) {
    auto value = path.generic_string();
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
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

        const auto temp_dir = std::filesystem::temp_directory_path() / "agent_tui_openai_compatible";
        std::filesystem::create_directories(temp_dir);
        const auto stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto request_path = temp_dir / ("request_" + stamp + ".json");
        const auto response_path = temp_dir / ("response_" + stamp + ".json");
        const auto curl_config_path = temp_dir / ("curl_" + stamp + ".txt");

        if (!openai_compatible_detail::write_file(request_path, request_body)) {
            return ProviderResponse::error_response("failed to write request temp file");
        }

        std::ostringstream curl_config;
        curl_config << "silent\n";
        curl_config << "show-error\n";
        curl_config << "request = \"POST\"\n";
        curl_config << "url = \"" << api_base << "/chat/completions\"\n";
        curl_config << "header = \"Content-Type: application/json\"\n";
        curl_config << "header = \"Authorization: Bearer " << api_key << "\"\n";
        curl_config << "data-binary = \"@" << openai_compatible_detail::curl_config_path(request_path) << "\"\n";
        curl_config << "output = \"" << openai_compatible_detail::curl_config_path(response_path) << "\"\n";
        curl_config << "max-time = " << config_.timeout_seconds << "\n";

        if (!openai_compatible_detail::write_file(curl_config_path, curl_config.str())) {
            return ProviderResponse::error_response("failed to write curl config temp file");
        }

        const auto command = "curl -K " + openai_compatible_detail::shell_quote(curl_config_path);
        const int exit_code = std::system(command.c_str());
        const auto response_body = openai_compatible_detail::read_file(response_path);

        std::error_code ignored;
        std::filesystem::remove(request_path, ignored);
        std::filesystem::remove(response_path, ignored);
        std::filesystem::remove(curl_config_path, ignored);

        if (exit_code != 0) {
            return ProviderResponse::error_response("curl failed while calling openai-compatible provider");
        }
        return parse_response_body(response_body);
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
        const auto temp_dir = std::filesystem::temp_directory_path() / "agent_tui_openai_compatible";
        std::filesystem::create_directories(temp_dir);
        const auto stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto request_path = temp_dir / ("request_stream_" + stamp + ".json");
        const auto curl_config_path = temp_dir / ("curl_stream_" + stamp + ".txt");

        if (!openai_compatible_detail::write_file(request_path, request_body)) {
            return ProviderResponse::error_response("failed to write request temp file");
        }

        std::ostringstream curl_config;
        curl_config << "silent\n";
        curl_config << "show-error\n";
        curl_config << "no-buffer\n";
        curl_config << "request = \"POST\"\n";
        curl_config << "url = \"" << api_base << "/chat/completions\"\n";
        curl_config << "header = \"Content-Type: application/json\"\n";
        curl_config << "header = \"Authorization: Bearer " << api_key << "\"\n";
        curl_config << "data-binary = \"@" << openai_compatible_detail::curl_config_path(request_path) << "\"\n";
        curl_config << "max-time = " << config_.timeout_seconds << "\n";

        if (!openai_compatible_detail::write_file(curl_config_path, curl_config.str())) {
            return ProviderResponse::error_response("failed to write curl config temp file");
        }

        const auto command = "curl -N -K " + openai_compatible_detail::shell_quote(curl_config_path);
#ifdef _WIN32
        FILE* pipe = _popen(command.c_str(), "r");
#else
        FILE* pipe = popen(command.c_str(), "r");
#endif
        if (pipe == nullptr) {
            return ProviderResponse::error_response("failed to start curl for streaming response");
        }

        std::string accumulated;
        std::string raw;
        ToolCall streamed_tool_call;
        bool saw_streamed_tool_call = false;
        char buffer[4096];
        while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::string line(buffer);
            raw += line;
            if (line.rfind("data:", 0) != 0) {
                continue;
            }
            line = line.substr(5);
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
                line.erase(line.begin());
            }
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                line.pop_back();
            }
            if (line == "[DONE]") {
                continue;
            }
            const auto delta = openai_compatible_detail::extract_json_string_field(line, "content");
            if (!delta.empty()) {
                auto piece = delta;
                if (!accumulated.empty()) {
                    if (delta == accumulated) {
                        piece.clear();
                    } else if (delta.rfind(accumulated, 0) == 0) {
                        piece = delta.substr(accumulated.size());
                    }
                }
                if (!piece.empty()) {
                    accumulated += piece;
                    on_delta(piece);
                }
            }
            if (line.find("\"tool_calls\"") != std::string::npos) {
                saw_streamed_tool_call = true;
                const auto id = openai_compatible_detail::extract_json_string_field(line, "id");
                const auto name = openai_compatible_detail::extract_json_string_field(line, "name");
                const auto arguments = openai_compatible_detail::extract_json_string_field(line, "arguments");
                if (!id.empty()) {
                    streamed_tool_call.id = id;
                }
                if (!name.empty()) {
                    streamed_tool_call.name = name;
                }
                streamed_tool_call.arguments["_raw"] += arguments;
            }
        }

#ifdef _WIN32
        const int exit_code = _pclose(pipe);
#else
        const int exit_code = pclose(pipe);
#endif
        std::error_code ignored;
        std::filesystem::remove(request_path, ignored);
        std::filesystem::remove(curl_config_path, ignored);

        if (exit_code != 0) {
            return ProviderResponse::error_response("curl failed while streaming openai-compatible provider");
        }
        if (!accumulated.empty()) {
            return ProviderResponse::text_response(accumulated);
        }
        if (saw_streamed_tool_call && !streamed_tool_call.name.empty()) {
            const auto raw_arguments = streamed_tool_call.arguments["_raw"];
            streamed_tool_call.arguments = openai_compatible_detail::parse_flat_json_string_object(raw_arguments);
            if (streamed_tool_call.id.empty()) {
                streamed_tool_call.id = "stream_tool_call";
            }
            return ProviderResponse::tool_calls_response({streamed_tool_call});
        }
        return parse_response_body(raw);
    }

    static std::string build_request_body(const Config& config,
                                          const std::vector<Message>& messages,
                                          const std::string& tools_schema_json = {},
                                          bool include_tools = true,
                                          bool stream = false) {
        std::ostringstream out;
        out << "{";
        out << "\"model\":\"" << openai_compatible_detail::json_escape_string(config.model) << "\",";
        out << "\"messages\":[";
        bool first = true;
        for (const auto& message : messages) {
            if (!first) {
                out << ",";
            }
            first = false;
            out << "{";
            if (message.role == Role::Tool) {
                out << "\"role\":\"tool\",";
                out << "\"tool_call_id\":\"" << openai_compatible_detail::json_escape_string(message.tool_call_id) << "\",";
                out << "\"content\":\"" << openai_compatible_detail::json_escape_string(message.content) << "\"";
            } else if (message.role == Role::Assistant && !message.tool_calls.empty()) {
                out << "\"role\":\"assistant\",";
                out << "\"content\":\"" << openai_compatible_detail::json_escape_string(message.content) << "\",";
                out << "\"tool_calls\":" << openai_compatible_detail::tool_calls_json(message.tool_calls);
            } else {
                out << "\"role\":\"" << openai_compatible_detail::role_to_string(message.role) << "\",";
                out << "\"content\":\"" << openai_compatible_detail::json_escape_string(message.content) << "\"";
            }
            out << "}";
        }
        out << "],";
        if (include_tools && !tools_schema_json.empty()) {
            out << "\"tools\":" << tools_schema_json << ",";
            out << "\"tool_choice\":\"auto\",";
        }
        out << "\"stream\":" << (stream ? "true" : "false");
        out << "}";
        return out.str();
    }

    static std::string build_request_body(const Config& config, const std::vector<Message>& messages, bool include_tools, bool stream = false) {
        return build_request_body(config, messages, std::string{}, include_tools, stream);
    }

    static ProviderResponse parse_response_body(const std::string& body) {
        const auto tool_calls = openai_compatible_detail::extract_tool_calls(body);
        if (!tool_calls.empty()) {
            return ProviderResponse::tool_calls_response(tool_calls);
        }

        const auto error_message = openai_compatible_detail::extract_json_string_field(body, "message");
        if (body.find("\"error\"") != std::string::npos && !error_message.empty()) {
            return ProviderResponse::error_response(error_message);
        }

        const auto content = openai_compatible_detail::extract_json_string_field(body, "content");
        if (!content.empty()) {
            return ProviderResponse::text_response(content);
        }
        return ProviderResponse::error_response("failed to parse openai-compatible response");
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
    Config config_;
};

}  // namespace agent_tui

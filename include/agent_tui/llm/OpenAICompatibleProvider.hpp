#pragma once

#include <chrono>
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
    while (pos < body.size()) {
        pos = body.find('"', pos);
        if (pos == std::string::npos) {
            break;
        }
        const auto key_start = pos + 1;
        pos = body.find('"', key_start);
        if (pos == std::string::npos) {
            break;
        }
        const auto key = body.substr(key_start, pos - key_start);
        pos = body.find(':', pos + 1);
        if (pos == std::string::npos) {
            break;
        }
        pos = body.find('"', pos + 1);
        if (pos == std::string::npos) {
            break;
        }

        std::string value;
        bool escaped = false;
        for (++pos; pos < body.size(); ++pos) {
            const char ch = body[pos];
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
                ++pos;
                break;
            }
            value.push_back(ch);
        }
        result[key] = json_unescape_string(value);
    }
    return result;
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

inline std::string tool_definitions_json() {
    return R"([
{"type":"function","function":{"name":"list_dir","description":"List files and directories under the workspace.","parameters":{"type":"object","properties":{"path":{"type":"string","description":"Workspace-relative directory path."}}}}},
{"type":"function","function":{"name":"read_file","description":"Read a UTF-8 text file from the workspace.","parameters":{"type":"object","properties":{"path":{"type":"string"},"max_bytes":{"type":"string"}},"required":["path"]}}},
{"type":"function","function":{"name":"glob_files","description":"Find workspace files matching a glob pattern.","parameters":{"type":"object","properties":{"pattern":{"type":"string"},"path":{"type":"string"},"max_matches":{"type":"string"}},"required":["pattern"]}}},
{"type":"function","function":{"name":"search_text","description":"Search text in workspace files.","parameters":{"type":"object","properties":{"query":{"type":"string"},"path":{"type":"string"},"max_matches":{"type":"string"}},"required":["query"]}}},
{"type":"function","function":{"name":"write_file","description":"Write a UTF-8 text file inside the workspace.","parameters":{"type":"object","properties":{"path":{"type":"string"},"content":{"type":"string"},"create_parent_dirs":{"type":"string","description":"true to create parent directories"}},"required":["path","content"]}}},
{"type":"function","function":{"name":"edit_file","description":"Replace text inside a workspace file.","parameters":{"type":"object","properties":{"path":{"type":"string"},"old_text":{"type":"string"},"new_text":{"type":"string"},"replace_all":{"type":"string"}},"required":["path","old_text","new_text"]}}}])";
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
    return path.generic_string();
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

    ProviderResponse chat(const std::vector<Message>& messages) override {
        const auto api_key = resolve_api_key(config_);
        if (api_key.empty()) {
            return ProviderResponse::error_response("missing API key: set api_key or api_key_env");
        }

        const auto api_base = config_.api_base.empty() ? std::string{"https://api.openai.com/v1"} : config_.api_base;
        const auto request_body = build_request_body(config_, messages);
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

    ProviderResponse chat_stream(const std::vector<Message>& messages, const std::function<void(const std::string&)>& on_delta) {
        const auto api_key = resolve_api_key(config_);
        if (api_key.empty()) {
            return ProviderResponse::error_response("missing API key: set api_key or api_key_env");
        }

        const auto api_base = config_.api_base.empty() ? std::string{"https://api.openai.com/v1"} : config_.api_base;
        const auto request_body = build_request_body(config_, messages, false, true);
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
        return parse_response_body(raw);
    }

    static std::string build_request_body(const Config& config, const std::vector<Message>& messages, bool include_tools = true, bool stream = false) {
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
                out << "\"role\":\"user\",";
                out << "\"content\":\"" << openai_compatible_detail::json_escape_string("Tool result for " + message.tool_call_id + ":\n" + message.content) << "\"";
            } else {
                out << "\"role\":\"" << openai_compatible_detail::role_to_string(message.role) << "\",";
                out << "\"content\":\"" << openai_compatible_detail::json_escape_string(message.content) << "\"";
            }
            out << "}";
        }
        out << "],";
        if (include_tools) {
            out << "\"tools\":" << openai_compatible_detail::tool_definitions_json() << ",";
            out << "\"tool_choice\":\"auto\",";
        }
        out << "\"stream\":" << (stream ? "true" : "false");
        out << "}";
        return out.str();
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

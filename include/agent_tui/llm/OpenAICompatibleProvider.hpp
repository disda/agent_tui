#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

inline std::string shell_quote(const std::filesystem::path& path) {
    auto value = path.string();
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

    ProviderResponse chat(const std::vector<Message>& messages) override {
        const auto api_key = read_api_key();
        if (api_key.empty()) {
            return ProviderResponse::error_response("missing API key env: " + config_.api_key_env);
        }

        const auto api_base = config_.api_base.empty() ? std::string{"https://api.openai.com/v1"} : config_.api_base;
        const auto request_body = build_request_body(config_, messages);

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
        curl_config << "data-binary = @" << request_path.string() << "\n";
        curl_config << "output = \"" << response_path.string() << "\"\n";
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

    static std::string build_request_body(const Config& config, const std::vector<Message>& messages) {
        std::ostringstream out;
        out << "{";
        out << "\"model\":\"" << openai_compatible_detail::json_escape_string(config.model) << "\",";
        out << "\"messages\":[";
        bool first = true;
        for (const auto& message : messages) {
            if (message.role == Role::Tool) {
                continue;
            }
            if (!first) {
                out << ",";
            }
            first = false;
            out << "{";
            out << "\"role\":\"" << openai_compatible_detail::role_to_string(message.role) << "\",";
            out << "\"content\":\"" << openai_compatible_detail::json_escape_string(message.content) << "\"";
            out << "}";
        }
        out << "],";
        out << "\"stream\":false";
        out << "}";
        return out.str();
    }

    static ProviderResponse parse_response_body(const std::string& body) {
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

private:
    std::string read_api_key() const {
        if (config_.api_key_env.empty()) {
            return {};
        }
        const char* value = std::getenv(config_.api_key_env.c_str());
        if (value == nullptr) {
            return {};
        }
        return value;
    }

    Config config_;
};

}  // namespace agent_tui

#pragma once

#include <cstdlib>
#include <sstream>
#include <string>

namespace agent_tui {

struct Config {
    std::string provider = "mock";
    std::string model = "mock-model";
    std::string api_base;
    std::string api_key;
    std::string api_key_env;
    int timeout_seconds = 60;
    int max_loops = 8;

    std::string api_key_status() const {
        if (!api_key.empty()) {
            return "<inline set>";
        }
        if (api_key_env.empty()) {
            return "<not set>";
        }
        const char* value = std::getenv(api_key_env.c_str());
        if (value == nullptr || std::string(value).empty()) {
            return api_key_env + "=<missing>";
        }
        return api_key_env + "=<set>";
    }

    std::string summary() const {
        std::ostringstream out;
        out << "provider: " << provider << '\n';
        out << "model: " << model << '\n';
        out << "api_base: " << (api_base.empty() ? "<not set>" : api_base) << '\n';
        out << "api_key: " << (api_key.empty() ? "<not set>" : "<inline set>") << '\n';
        out << "api_key_env: " << (api_key_env.empty() ? "<not set>" : api_key_env) << '\n';
        out << "api_key_status: " << api_key_status() << '\n';
        out << "timeout_seconds: " << timeout_seconds << '\n';
        out << "max_loops: " << max_loops << '\n';
        return out.str();
    }

    static std::string example_toml() {
        return
            "# agent_tui user configuration\n"
            "# For local-only config, api_key may be set directly. api_key_env is safer for shared configs.\n"
            "provider = \"mock\"\n"
            "model = \"mock-model\"\n"
            "api_base = \"\"\n"
            "api_key = \"\"\n"
            "api_key_env = \"OPENAI_API_KEY\"\n"
            "timeout_seconds = 60\n"
            "max_loops = 8\n";
    }
};

}  // namespace agent_tui

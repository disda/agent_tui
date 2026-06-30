#pragma once

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "agent_tui/tools/Tool.hpp"

namespace agent_tui {

struct ToolExposurePolicy {
    std::vector<std::string> active_tools;
    std::vector<std::string> allow_tools;
    std::vector<std::string> deny_tools;

    bool exposes(const std::string& name) const {
        if (!active_tools.empty() && !contains(active_tools, name)) {
            return false;
        }
        if (!allow_tools.empty() && !contains(allow_tools, name)) {
            return false;
        }
        if (contains(deny_tools, name)) {
            return false;
        }
        return true;
    }

private:
    static bool contains(const std::vector<std::string>& values, const std::string& value) {
        return std::find(values.begin(), values.end(), value) != values.end();
    }
};

class ToolRegistry {
public:
    void register_tool(std::unique_ptr<Tool> tool) {
        auto key = tool->name();
        tools_[key] = std::move(tool);
    }

    Tool* find(const std::string& name) const {
        auto it = tools_.find(name);
        if (it == tools_.end()) {
            return nullptr;
        }
        return it->second.get();
    }

    std::vector<std::string> names() const {
        return names(ToolExposurePolicy{});
    }

    std::vector<std::string> names(const ToolExposurePolicy& policy) const {
        std::vector<std::string> result;
        result.reserve(tools_.size());
        for (const auto& [name, _] : tools_) {
            if (policy.exposes(name)) {
                result.push_back(name);
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    std::string tools_schema_json() const {
        return tools_schema_json(ToolExposurePolicy{});
    }

    std::string tools_schema_json(const ToolExposurePolicy& policy) const {
        nlohmann::json schema = nlohmann::json::array();
        for (const auto& name : names(policy)) {
            const auto* tool = find(name);
            if (tool == nullptr) {
                continue;
            }
            auto parameters = nlohmann::json::parse(tool->parameters_schema_json(), nullptr, false);
            if (parameters.is_discarded()) {
                parameters = nlohmann::json::object();
            }
            schema.push_back({
                {"type", "function"},
                {"function", {
                    {"name", tool->name()},
                    {"description", tool->description()},
                    {"parameters", std::move(parameters)},
                }},
            });
        }
        return schema.dump();
    }

private:
    std::unordered_map<std::string, std::unique_ptr<Tool>> tools_;
};

}  // namespace agent_tui

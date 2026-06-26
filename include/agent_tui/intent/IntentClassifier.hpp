#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "agent_tui/intent/Intent.hpp"

namespace agent_tui {

class IntentClassifier {
public:
    static Intent classify(const std::string& input) {
        const auto trimmed = trim(input);
        const auto lowered = lower_ascii(trimmed);

        if (trimmed.empty()) {
            return {};
        }

        if (lowered == "ls" || lowered == "dir" || lowered == "list" || contains_any(trimmed, {"列出目录", "看看目录", "目录"})) {
            return Intent{IntentType::ListDir, ".", 95, "directory listing keyword"};
        }

        if (starts_with_any(lowered, {"ls ", "dir ", "list "})) {
            return Intent{IntentType::ListDir, trim(trimmed.substr(trimmed.find(' ') + 1)), 90, "directory listing with path"};
        }

        if (starts_with_any(lowered, {"read ", "cat ", "open ", "show "})) {
            return Intent{IntentType::ReadFile, trim(trimmed.substr(trimmed.find(' ') + 1)), 92, "read file command"};
        }

        if (auto arg = after_keyword(trimmed, {"读取", "打开", "查看文件", "看文件"}); !arg.empty()) {
            return Intent{IntentType::ReadFile, arg, 88, "Chinese read file keyword"};
        }

        if (starts_with_any(lowered, {"search ", "grep ", "find "})) {
            return Intent{IntentType::SearchText, trim(trimmed.substr(trimmed.find(' ') + 1)), 90, "search command"};
        }

        if (auto arg = after_keyword(trimmed, {"搜索", "查找", "搜一下", "找一下"}); !arg.empty()) {
            return Intent{IntentType::SearchText, arg, 86, "Chinese search keyword"};
        }

        if (contains_any(lowered, {"ctest", "run tests", "run test", "test project"}) || contains_any(trimmed, {"运行测试", "执行测试", "跑测试", "测试"})) {
            return Intent{IntentType::TestProject, {}, 82, "test project keyword"};
        }

        if (contains_any(lowered, {"cmake configure", "configure cmake", "configure project"}) || contains_any(trimmed, {"配置 cmake", "配置项目", "生成 build", "生成构建"})) {
            return Intent{IntentType::ConfigureProject, {}, 82, "configure project keyword"};
        }

        if (lowered == "build" || contains_any(lowered, {"build project", "cmake --build"}) || contains_any(trimmed, {"编译", "构建项目", "构建"})) {
            return Intent{IntentType::BuildProject, {}, 82, "build project keyword"};
        }

        return Intent{IntentType::Unknown, {}, 0, "no local intent matched"};
    }

private:
    static std::string trim(std::string value) {
        auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) { return !is_space(static_cast<unsigned char>(ch)); }));
        value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch) { return !is_space(static_cast<unsigned char>(ch)); }).base(), value.end());
        return value;
    }

    static std::string lower_ascii(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return value;
    }

    static bool starts_with_any(const std::string& value, const std::vector<std::string>& prefixes) {
        for (const auto& prefix : prefixes) {
            if (value.rfind(prefix, 0) == 0) {
                return true;
            }
        }
        return false;
    }

    static bool contains_any(const std::string& value, const std::vector<std::string>& needles) {
        for (const auto& needle : needles) {
            if (value.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    static std::string after_keyword(const std::string& value, const std::vector<std::string>& keywords) {
        for (const auto& keyword : keywords) {
            const auto pos = value.find(keyword);
            if (pos != std::string::npos) {
                return trim(value.substr(pos + keyword.size()));
            }
        }
        return {};
    }
};

}  // namespace agent_tui

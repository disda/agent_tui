#pragma once

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "agent_tui/filesystem/KnownPaths.hpp"

namespace agent_tui {

class AllowedRoots {
public:
    AllowedRoots() = default;

    explicit AllowedRoots(std::vector<std::filesystem::path> roots) {
        for (const auto& root : roots) {
            add_root(root);
        }
    }

    static AllowedRoots workspace_only(const std::filesystem::path& workspace) {
        AllowedRoots roots;
        roots.add_root(workspace);
        return roots;
    }

    static AllowedRoots with_known_user_dirs(const std::filesystem::path& workspace) {
        AllowedRoots roots;
        roots.resolve_known_aliases_ = true;
        roots.add_root(workspace);
        roots.add_root(KnownPaths::desktop());
        roots.add_root(KnownPaths::downloads());
        roots.add_root(KnownPaths::documents());
        roots.add_root(KnownPaths::pictures());
        return roots;
    }

    void add_root(const std::filesystem::path& root) {
        if (root.empty()) {
            return;
        }
        std::error_code ec;
        const auto absolute = std::filesystem::absolute(root, ec);
        if (ec) {
            return;
        }
        const auto normalized = weakly_canonical_or_absolute(absolute);
        roots_.push_back(normalized);
    }

    const std::vector<std::filesystem::path>& roots() const { return roots_; }

    std::filesystem::path resolve(const std::string& alias_or_path) const {
        const auto input = alias_or_path.empty() ? std::string{"."} : alias_or_path;
        auto path = resolve_known_aliases_ ? KnownPaths::resolve_alias(input)
                                           : std::filesystem::path(input);
        if (path.is_relative()) {
            if (roots_.empty()) {
                path = std::filesystem::current_path() / path;
            } else {
                path = roots_.front() / path;
            }
        }

        const auto resolved = weakly_canonical_or_absolute(path);
        if (!is_allowed(resolved) && std::filesystem::path{input}.is_relative() && !roots_.empty()) {
            const auto workspace_relative = weakly_canonical_or_absolute(roots_.front() / input);
            if (is_allowed(workspace_relative)) {
                return workspace_relative;
            }
        }
        if (!is_allowed(resolved)) {
            throw std::runtime_error("path is outside allowed roots: " + resolved.generic_string());
        }
        return resolved;
    }

    bool is_allowed(const std::filesystem::path& path) const {
        const auto candidate = weakly_canonical_or_absolute(path);
        for (const auto& root : roots_) {
            if (is_same_or_child(candidate, root)) {
                return true;
            }
        }
        return false;
    }

    std::string summary() const {
        std::ostringstream out;
        for (const auto& root : roots_) {
            out << root.generic_string() << '\n';
        }
        return out.str();
    }

private:
    static std::filesystem::path weakly_canonical_or_absolute(const std::filesystem::path& path) {
        std::error_code ec;
        auto canonical = std::filesystem::weakly_canonical(path, ec);
        if (!ec) {
            return canonical.lexically_normal();
        }
        return std::filesystem::absolute(path).lexically_normal();
    }

    static bool is_same_or_child(const std::filesystem::path& candidate, const std::filesystem::path& root) {
        const auto candidate_text = candidate.lexically_normal().generic_string();
        auto root_text = root.lexically_normal().generic_string();
        while (root_text.size() > 1 && root_text.back() == '/') {
            root_text.pop_back();
        }
        if (candidate_text == root_text) {
            return true;
        }
        return candidate_text.rfind(root_text + "/", 0) == 0;
    }

    std::vector<std::filesystem::path> roots_;
    bool resolve_known_aliases_ = false;
};

}  // namespace agent_tui

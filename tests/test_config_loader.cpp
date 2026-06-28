#include "agent_tui/config/ConfigLoader.hpp"
#include "agent_tui/llm/ProviderFactory.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace agent_tui;

namespace {

std::filesystem::path make_test_root() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto root = std::filesystem::temp_directory_path() / ("agent_tui_config_" + std::to_string(stamp));
    std::filesystem::create_directories(root);
    return root;
}

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
}

void test_project_config_overrides_user_config() {
    const auto root = make_test_root();
    const auto user_config = root / "user" / "config.toml";
    const auto project_config = root / "project" / ".agent_tui" / "config.toml";

    write_file(user_config,
               "provider = \"mock\"\n"
               "model = \"user-model\"\n"
               "api_base = \"https://user.example/v1\"\n"
               "timeout_seconds = 10\n"
               "max_loops = 2\n");

    write_file(project_config,
               "model = \"project-model\"\n"
               "timeout_seconds = 30\n");

    auto config = ConfigLoader::load_from_paths(user_config, project_config);
    assert(config.provider == "mock");
    assert(config.model == "project-model");
    assert(config.api_base == "https://user.example/v1");
    assert(config.timeout_seconds == 30);
    assert(config.max_loops == 2);

    std::filesystem::remove_all(root);
}

void test_api_key_is_not_exposed() {
    Config config;
    config.api_key_env = "SECRET_TOKEN_ENV";
    config.api_key = "sk-test-secret-value";
    const auto summary = config.summary();
    assert(summary.find("SECRET_TOKEN_ENV") != std::string::npos);
    assert(summary.find("sk-test-secret-value") == std::string::npos);
}

void test_default_and_example_max_loops_are_twenty_five() {
    Config config;
    assert(config.max_loops == 25);
    assert(Config::example_toml().find("max_loops = 25") != std::string::npos);
}

void test_inline_api_key_can_be_loaded_from_project_config() {
    const auto root = make_test_root();
    const auto project_config = root / "project" / ".agent_tui" / "config.toml";

    write_file(project_config,
               "provider = \"openai-compatible\"\n"
               "model = \"gpt-5.4-mini\"\n"
               "api_base = \"http://127.0.0.1:1455/v1\"\n"
               "api_key = \"sk-test-inline-key\"\n");

    auto config = ConfigLoader::load_from_paths(root / "missing.toml", project_config);
    assert(config.provider == "openai-compatible");
    assert(config.model == "gpt-5.4-mini");
    assert(config.api_key == "sk-test-inline-key");
    assert(config.api_key_status() == "<inline set>");
    assert(config.summary().find("sk-test-inline-key") == std::string::npos);

    std::filesystem::remove_all(root);
}

void test_write_example_config() {
    const auto root = make_test_root();
    const auto path = root / ".agent_tui" / "config.toml";
    assert(ConfigLoader::write_example(path, false));
    assert(std::filesystem::exists(path));
    auto config = ConfigLoader::load_from_paths(path, root / "missing.toml");
    assert(config.provider == "mock");
    assert(config.model == "mock-model");
    std::filesystem::remove_all(root);
}

void test_init_project_config_creates_agent_tui_directory() {
    const auto root = make_test_root();
    assert(ConfigLoader::init_project_config(root, false));
    assert(std::filesystem::exists(root / ".agent_tui"));
    assert(std::filesystem::exists(root / ".agent_tui" / "config.toml"));
    std::filesystem::remove_all(root);
}

void test_provider_factory_mock_chat() {
    Config config;
    config.provider = "mock";
    auto provider = ProviderFactory::create(config);
    auto response = provider->chat({Message{Role::User, "hi", {}}});
    assert(response.type == ProviderResponseType::Text);
    assert(response.text == "mock assistant: hi");
}

void test_provider_factory_placeholder_for_unknown_provider() {
    Config config;
    config.provider = "unknown-provider";
    auto provider = ProviderFactory::create(config);
    auto response = provider->chat({Message{Role::User, "hi", {}}});
    assert(response.type == ProviderResponseType::Error);
    assert(response.error.find("not implemented") != std::string::npos);
}

}  // namespace

int main() {
    test_project_config_overrides_user_config();
    test_api_key_is_not_exposed();
    test_default_and_example_max_loops_are_twenty_five();
    test_inline_api_key_can_be_loaded_from_project_config();
    test_write_example_config();
    test_init_project_config_creates_agent_tui_directory();
    test_provider_factory_mock_chat();
    test_provider_factory_placeholder_for_unknown_provider();
    return 0;
}

#include "Config/ConfigDocument.h"
#include "Config/RegistryConfig.h"
#include "SystemPath/SystemPathRouter.h"

#include <algorithm>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef GC_TEST_CONFIG_PATH
#error GC_TEST_CONFIG_PATH must name the distributed config.toml
#endif

namespace {

int Expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return 1;
    }
    return 0;
}

std::string ReadDistributedConfig()
{
    std::ifstream input{GC_TEST_CONFIG_PATH, std::ios::binary};
    if (!input) {
        std::cerr << "Could not open distributed config: "
                  << GC_TEST_CONFIG_PATH << '\n';
        std::exit(2);
    }
    return {
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
}

std::size_t FindAssignment(
    const std::string& text,
    std::string_view key)
{
    const std::string marker = std::string{key} + " = ";
    std::size_t position = text.find(marker);
    while (position != std::string::npos && position != 0 &&
           text[position - 1] != '\n') {
        position = text.find(marker, position + marker.size());
    }
    if (position == std::string::npos) {
        std::cerr << "Config fixture lacks assignment: " << key << '\n';
        std::exit(2);
    }
    return position;
}

std::string RemoveAssignment(
    std::string text,
    std::string_view key)
{
    const auto position = FindAssignment(text, key);
    auto end = text.find('\n', position);
    end = end == std::string::npos ? text.size() : end + 1;
    text.erase(position, end - position);
    return text;
}

std::string ReplaceAssignmentValue(
    std::string text,
    std::string_view key,
    std::string_view value)
{
    const auto position = FindAssignment(text, key);
    auto end = text.find('\n', position);
    if (end == std::string::npos) {
        end = text.size();
    }
    text.replace(
        position,
        end - position,
        std::string{key} + " = " + std::string{value});
    return text;
}

std::string InsertAfterLine(
    std::string text,
    std::string_view line,
    std::string_view insertion)
{
    const auto position = text.find(line);
    if (position == std::string::npos) {
        std::cerr << "Config fixture lacks table: " << line << '\n';
        std::exit(2);
    }
    const auto line_end = text.find('\n', position + line.size());
    if (line_end == std::string::npos) {
        std::cerr << "Config fixture table has no line terminator\n";
        std::exit(2);
    }
    text.insert(line_end + 1, std::string{insertion});
    return text;
}

std::string LegacyConfig(bool registry_enabled)
{
    auto legacy = RemoveAssignment(ReadDistributedConfig(), "system_path");
    legacy = ReplaceAssignmentValue(
        std::move(legacy),
        "enabled",
        registry_enabled ? "true" : "false");
    return InsertAfterLine(
        std::move(legacy),
        "[registry.nesys]",
        "news_path = 'D:\\system\\DUA\\news'\n"
        "event_path = 'D:\\system\\DUA\\event'\n"
        "log_path = 'D:\\system\\CmdFile\\log'\n");
}

struct DirectoryFake {
    bool fail_default{};
    std::vector<std::filesystem::path> calls;
};

bool CreateDirectories(
    void* context,
    const std::filesystem::path& path,
    std::error_code& error) noexcept
{
    auto& fake = *static_cast<DirectoryFake*>(context);
    fake.calls.push_back(path);
    if (fake.fail_default &&
        path.native().starts_with(L"D:\\system\\")) {
        error = std::make_error_code(std::errc::permission_denied);
        return false;
    }
    error.clear();
    return true;
}

gc::system_path::DirectoryActions DirectoryActions(DirectoryFake& fake)
{
    return {
        .context = &fake,
        .create_directories = &CreateDirectories,
    };
}

struct AtomicFake {
    int writes{};
    int replaces{};
    int removes{};
    std::filesystem::path destination;
    std::string serialized;
};

std::expected<void, std::string> WriteConfig(
    void* context,
    const std::filesystem::path&,
    std::string_view serialized) noexcept
{
    auto& fake = *static_cast<AtomicFake*>(context);
    ++fake.writes;
    fake.serialized = serialized;
    return {};
}

std::expected<void, std::string> ReplaceConfig(
    void* context,
    const std::filesystem::path& destination,
    const std::filesystem::path&) noexcept
{
    auto& fake = *static_cast<AtomicFake*>(context);
    ++fake.replaces;
    fake.destination = destination;
    return {};
}

void RemoveConfig(
    void* context,
    const std::filesystem::path&) noexcept
{
    ++static_cast<AtomicFake*>(context)->removes;
}

gc::config::AtomicConfigWriteActions AtomicActions(AtomicFake& fake)
{
    return {
        .context = &fake,
        .write = &WriteConfig,
        .replace = &ReplaceConfig,
        .remove = &RemoveConfig,
    };
}

gc::config::GameSystemPathPreparationActions PreparationActions(
    DirectoryFake& directories,
    AtomicFake& config)
{
    return {
        .directories = DirectoryActions(directories),
        .config_write = AtomicActions(config),
    };
}

int TestEnabledFallbackFlow()
{
    int failures = 0;
    const auto parsed = gc::config::ParseAndValidateInputConfigDocument(
        LegacyConfig(true));
    failures += Expect(
        parsed && parsed->migrations.registry_paths &&
            parsed->config.registry().enabled() &&
            parsed->config.registry().system_path() == "D:\\system",
        "legacy enabled config migrates before preparation");
    if (!parsed) {
        return failures;
    }

    const std::filesystem::path config_path =
        L"H:\\遊戲\\config.toml";
    DirectoryFake directories{
        .fail_default = true,
    };
    AtomicFake persisted;
    const auto prepared =
        gc::config::PrepareAndPersistGameSystemPathConfiguration(
            parsed->config,
            parsed->migrations.any(),
            config_path,
            true,
            PreparationActions(directories, persisted));
    failures += Expect(
        prepared && prepared->persisted && persisted.writes == 1 &&
            persisted.replaces == 1 && persisted.removes == 0 &&
            persisted.destination == config_path &&
            persisted.serialized.find("system_path") != std::string::npos &&
            persisted.serialized.find("news_path") == std::string::npos &&
            prepared->config.registry().system_path() == ".\\system" &&
            prepared->runtime.configured_path == ".\\system" &&
            prepared->runtime.resolved_path == L"H:\\遊戲\\system" &&
            prepared->runtime.redirect_enabled &&
            directories.calls.size() == 9,
        "default failure persists the config-directory fallback");
    if (!prepared) {
        return failures;
    }

    const auto derived = gc::registry_config::DeriveNesysPaths(
        prepared->config.registry().system_path());
    gc::system_path::SystemPathRouter router{prepared->runtime};
    const auto ttx_path = router.RoutePathW(
        L"D:\\system\\DUA\\download\\item.dat");
    failures += Expect(
        derived && derived->news == ".\\system\\DUA\\news" &&
            derived->event == ".\\system\\DUA\\event" &&
            derived->log == ".\\system\\CmdFile\\log" &&
            ttx_path && ttx_path->matched &&
            ttx_path->path ==
                L"H:\\遊戲\\system\\DUA\\download\\item.dat",
        "persisted root is shared by NESYS derivation and Ttx routing");
    return failures;
}

int TestRegistryDisabledFlow()
{
    int failures = 0;
    const auto parsed = gc::config::ParseAndValidateInputConfigDocument(
        LegacyConfig(false));
    failures += Expect(
        parsed && parsed->migrations.registry_paths &&
            !parsed->config.registry().enabled(),
        "legacy disabled config is marked for canonical persistence");
    if (!parsed) {
        return failures;
    }

    DirectoryFake directories;
    AtomicFake writer;
    const auto prepared =
        gc::config::PrepareAndPersistGameSystemPathConfiguration(
            parsed->config,
            parsed->migrations.any(),
            L"H:\\遊戲\\config.toml",
            true,
            PreparationActions(directories, writer));
    failures += Expect(
        prepared && prepared->persisted && writer.writes == 1 &&
            writer.replaces == 1 && writer.removes == 0 &&
            prepared->config.registry().system_path() == "D:\\system" &&
            prepared->runtime.configured_path == "D:\\system" &&
            prepared->runtime.resolved_path == L"D:\\system" &&
            !prepared->runtime.redirect_enabled &&
            directories.calls.size() ==
                gc::system_path::kRequiredTreeLeaves.size(),
        "disabled registry mode persists the canonical migrated document");
    if (!prepared) {
        return failures;
    }

    gc::system_path::SystemPathRouter router{prepared->runtime};
    const auto route = router.RoutePathW(
        L"D:\\system\\DUA\\download\\item.dat");
    failures += Expect(
        route && !route->matched && !router.enabled(),
        "disabled registry mode does not consume derived overrides or route");
    return failures;
}

} // namespace

int main()
{
    const int failures =
        TestEnabledFallbackFlow() + TestRegistryDisabledFlow();
    return failures == 0 ? 0 : 1;
}

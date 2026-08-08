#include "Config/ConfigDocument.h"
#include "SystemPath/StartupFatal.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <expected>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

struct DirectoryFake {
    std::vector<std::filesystem::path> calls;
    std::vector<std::size_t> failing_calls;
};

bool FakeCreateDirectories(
    void* context,
    const std::filesystem::path& path,
    std::error_code& error) noexcept {
    auto& fake = *static_cast<DirectoryFake*>(context);
    const std::size_t call = fake.calls.size();
    fake.calls.push_back(path);
    if (std::ranges::find(fake.failing_calls, call) !=
        fake.failing_calls.end()) {
        error = std::make_error_code(std::errc::permission_denied);
        return false;
    }
    error.clear();
    return true;
}

gc::system_path::DirectoryActions MakeDirectoryActions(
    DirectoryFake& fake) {
    return {
        .context = &fake,
        .create_directories = &FakeCreateDirectories,
    };
}

struct WriterFake {
    bool fail_write{};
    bool fail_replace{};
    int writes{};
    int replaces{};
    int removes{};
    std::filesystem::path destination;
    std::string serialized;
};

std::expected<void, std::string> FakeWrite(
    void* context,
    const std::filesystem::path&,
    std::string_view serialized) noexcept {
    auto& fake = *static_cast<WriterFake*>(context);
    ++fake.writes;
    fake.serialized = serialized;
    if (fake.fail_write) {
        return std::unexpected("injected write failure");
    }
    return {};
}

std::expected<void, std::string> FakeReplace(
    void* context,
    const std::filesystem::path& destination,
    const std::filesystem::path&) noexcept {
    auto& fake = *static_cast<WriterFake*>(context);
    ++fake.replaces;
    fake.destination = destination;
    if (fake.fail_replace) {
        return std::unexpected("injected replacement failure");
    }
    return {};
}

void FakeRemove(
    void* context,
    const std::filesystem::path&) noexcept {
    ++static_cast<WriterFake*>(context)->removes;
}

gc::config::AtomicConfigWriteActions MakeAtomicActions(
    WriterFake& fake) {
    return {
        .context = &fake,
        .write = &FakeWrite,
        .replace = &FakeReplace,
        .remove = &FakeRemove,
    };
}

gc::config::GameSystemPathPreparationActions MakePreparationActions(
    DirectoryFake& directories,
    WriterFake& writer) {
    return {
        .directories = MakeDirectoryActions(directories),
        .config_write = MakeAtomicActions(writer),
    };
}

InputConfig MakeInputConfig(
    bool enabled,
    std::string system_path) {
    InputConfig config{};
    config.registry().enabled = enabled;
    config.registry().system_path = std::move(system_path);
    return config;
}

struct FatalFake {
    int logs{};
    int modals{};
    int terminations{};
    int fail_fast_calls{};
    std::string log;
    std::wstring modal;
    std::wstring title;
    DWORD exit_code{};
};

void FakeLog(void* context, const char* text) noexcept {
    auto& fake = *static_cast<FatalFake*>(context);
    ++fake.logs;
    fake.log = text == nullptr ? "" : text;
}

void FakeShow(
    void* context,
    const wchar_t* text,
    const wchar_t* title) noexcept {
    auto& fake = *static_cast<FatalFake*>(context);
    ++fake.modals;
    fake.modal = text == nullptr ? L"" : text;
    fake.title = title == nullptr ? L"" : title;
}

void FakeTerminate(void* context, DWORD exit_code) noexcept {
    auto& fake = *static_cast<FatalFake*>(context);
    ++fake.terminations;
    fake.exit_code = exit_code;
}

void FakeFailFast(void* context) noexcept {
    ++static_cast<FatalFake*>(context)->fail_fast_calls;
}

gc::system_path::StartupFatalActions FatalActions(FatalFake& fake) {
    return {
        .context = &fake,
        .log_error = &FakeLog,
        .show_error = &FakeShow,
        .terminate_process = &FakeTerminate,
        .fail_fast = &FakeFailFast,
    };
}

} // namespace

int main() {
    using gc::config::PrepareAndPersistGameSystemPathConfiguration;
    int failures = 0;
    const std::filesystem::path config_path =
        L"H:\\game\\config.toml";

    DirectoryFake migration_directories{};
    WriterFake migration_writer{};
    const auto enabled_migration =
        PrepareAndPersistGameSystemPathConfiguration(
            MakeInputConfig(true, "D:\\system"),
            true,
            config_path,
            true,
            MakePreparationActions(
                migration_directories,
                migration_writer));
    failures += Expect(
        enabled_migration && enabled_migration->persisted &&
            migration_writer.writes == 1 &&
            migration_writer.replaces == 1 &&
            migration_writer.destination == config_path &&
            enabled_migration->config.registry().system_path() ==
                "D:\\system" &&
            !enabled_migration->runtime.redirect_enabled,
        "enabled legacy migration is persisted before publication");

    DirectoryFake disabled_directories{};
    WriterFake disabled_writer{};
    const auto disabled_migration =
        PrepareAndPersistGameSystemPathConfiguration(
            MakeInputConfig(false, ".\\legacy"),
            true,
            config_path,
            true,
            MakePreparationActions(
                disabled_directories,
                disabled_writer));
    failures += Expect(
        disabled_migration && disabled_migration->persisted &&
            disabled_writer.writes == 1 &&
            disabled_writer.replaces == 1 &&
            disabled_migration->config.registry().system_path() ==
                ".\\legacy" &&
            disabled_migration->runtime.configured_path == "D:\\system" &&
            disabled_migration->runtime.resolved_path == L"D:\\system" &&
            !disabled_migration->runtime.redirect_enabled &&
            !disabled_directories.calls.empty() &&
            disabled_directories.calls.front() ==
                std::filesystem::path{L"D:\\system\\CmdFile\\log"},
        "document migration persists even with registry overrides disabled");

    DirectoryFake fallback_directories{.failing_calls = {0}};
    WriterFake fallback_writer{};
    const auto fallback = PrepareAndPersistGameSystemPathConfiguration(
        MakeInputConfig(true, "D:\\system"),
        false,
        config_path,
        false,
        MakePreparationActions(
            fallback_directories,
            fallback_writer));
    failures += Expect(
        fallback && fallback->persisted &&
            fallback_writer.writes == 1 &&
            fallback_writer.replaces == 1 &&
            fallback->config.registry().system_path() == ".\\system" &&
            fallback->runtime.configured_path == ".\\system" &&
            fallback->runtime.resolved_path == L"H:\\game\\system" &&
            fallback->runtime.redirect_enabled &&
            fallback->config.experimental()
                .enable_testmode_storage_redirect() &&
            fallback_writer.serialized.find("system_path") !=
                std::string::npos &&
            fallback_writer.serialized.find(".\\system") !=
                std::string::npos &&
            fallback_writer.serialized.find(
                "enable_testmode_storage_redirect = true") !=
                std::string::npos,
        "fallback and unavailable storage persist together before return");

    DirectoryFake unchanged_directories{};
    WriterFake unchanged_writer{};
    const auto unchanged = PrepareAndPersistGameSystemPathConfiguration(
        MakeInputConfig(true, ".\\custom"),
        false,
        config_path,
        true,
        MakePreparationActions(
            unchanged_directories,
            unchanged_writer));
    failures += Expect(
        unchanged && !unchanged->persisted &&
            unchanged_writer.writes == 0 &&
            unchanged_writer.replaces == 0 &&
            !unchanged->config.experimental()
                 .enable_testmode_storage_redirect(),
        "enabled unchanged custom root does not rewrite config");

    DirectoryFake unavailable_storage_directories{};
    WriterFake unavailable_storage_writer{};
    const auto unavailable_storage =
        PrepareAndPersistGameSystemPathConfiguration(
            MakeInputConfig(true, ".\\custom"),
            false,
            config_path,
            false,
            MakePreparationActions(
                unavailable_storage_directories,
                unavailable_storage_writer));
    failures += Expect(
        unavailable_storage && unavailable_storage->persisted &&
            unavailable_storage_writer.writes == 1 &&
            unavailable_storage_writer.replaces == 1 &&
            unavailable_storage->runtime.configured_path == ".\\custom" &&
            unavailable_storage->config.experimental()
                .enable_testmode_storage_redirect() &&
            unavailable_storage_writer.serialized.find(
                "enable_testmode_storage_redirect = true") !=
                std::string::npos,
        "unavailable native storage persists redirect for custom system path");

    InputConfig already_enabled_input =
        MakeInputConfig(true, ".\\custom");
    already_enabled_input.experimental().enable_testmode_storage_redirect =
        true;
    DirectoryFake already_enabled_directories{};
    WriterFake already_enabled_writer{};
    const auto already_enabled =
        PrepareAndPersistGameSystemPathConfiguration(
            std::move(already_enabled_input),
            false,
            config_path,
            false,
            MakePreparationActions(
                already_enabled_directories,
                already_enabled_writer));
    failures += Expect(
        already_enabled && !already_enabled->persisted &&
            already_enabled_writer.writes == 0 &&
            already_enabled_writer.replaces == 0 &&
            already_enabled->config.experimental()
                .enable_testmode_storage_redirect(),
        "unavailable native storage does not rewrite an enabled redirect");

    DirectoryFake replace_failure_directories{.failing_calls = {0}};
    WriterFake replace_failure_writer{.fail_replace = true};
    const InputConfig replacement_input =
        MakeInputConfig(true, "D:\\system");
    const auto replacement_failure =
        PrepareAndPersistGameSystemPathConfiguration(
            replacement_input,
            false,
            config_path,
            true,
            MakePreparationActions(
                replace_failure_directories,
                replace_failure_writer));
    failures += Expect(
        !replacement_failure && replace_failure_writer.writes == 1 &&
            replace_failure_writer.replaces == 1 &&
            replace_failure_writer.removes == 1 &&
            replace_failure_writer.serialized.find(".\\system") !=
                std::string::npos &&
            replacement_input.registry().system_path() == "D:\\system" &&
            replacement_failure.error().find("persistence") !=
                std::string::npos &&
            replacement_failure.error().find("replace") !=
                std::string::npos &&
            replacement_failure.error().find(
                "H:\\game\\config.toml") != std::string::npos,
        "atomic replacement failure publishes no prepared config");

    DirectoryFake custom_failure_directories{.failing_calls = {0}};
    WriterFake custom_failure_writer{};
    const auto custom_failure =
        PrepareAndPersistGameSystemPathConfiguration(
            MakeInputConfig(true, "R:\\cabinet"),
            false,
            config_path,
            true,
            MakePreparationActions(
                custom_failure_directories,
                custom_failure_writer));
    failures += Expect(
        !custom_failure &&
            custom_failure.error().find("configured_tree") !=
                std::string::npos &&
            custom_failure.error().find("R:\\cabinet") !=
                std::string::npos &&
            custom_failure.error().find(
                "R:\\cabinet\\CmdFile\\log") !=
                std::string::npos &&
            custom_failure.error().find("error=13") !=
                std::string::npos &&
            custom_failure.error().find(
                "correct [registry].system_path or permissions") !=
                std::string::npos &&
            custom_failure_writer.writes == 0,
        "custom root failure explains the configured path fix");

    DirectoryFake disabled_failure_directories{.failing_calls = {0}};
    WriterFake disabled_failure_writer{};
    const auto disabled_failure =
        PrepareAndPersistGameSystemPathConfiguration(
            MakeInputConfig(false, ".\\ignored"),
            false,
            config_path,
            true,
            MakePreparationActions(
                disabled_failure_directories,
                disabled_failure_writer));
    failures += Expect(
        !disabled_failure &&
            disabled_failure.error().find(
                "D:\\system\\CmdFile\\log") !=
                std::string::npos &&
            disabled_failure.error().find(
                "create a writable D:\\system or enable registry overrides") !=
                std::string::npos &&
            disabled_failure_writer.writes == 0,
        "disabled root failure explains the real D requirement");

    DirectoryFake fallback_failure_directories{
        .failing_calls = {0, 1}};
    WriterFake fallback_failure_writer{};
    const auto fallback_failure =
        PrepareAndPersistGameSystemPathConfiguration(
            MakeInputConfig(true, "D:\\system"),
            false,
            config_path,
            true,
            MakePreparationActions(
                fallback_failure_directories,
                fallback_failure_writer));
    failures += Expect(
        !fallback_failure &&
            fallback_failure.error().find("fallback_tree") !=
                std::string::npos &&
            fallback_failure.error().find(
                "H:\\game\\system\\CmdFile\\log") !=
                std::string::npos &&
            fallback_failure.error().find(
                "make the config directory writable or set .\\system manually") !=
                std::string::npos &&
            fallback_failure_writer.writes == 0,
        "fallback root failure explains the config-directory fix");

    FatalFake fatal_fake{};
    std::atomic_bool fatal_latch{false};
    gc::system_path::PublishStartupFatal(
        fatal_latch,
        "System path preparation failed stage=configured_tree error=5",
        L"The configured system path is unavailable. Use .\\system or fix permissions.",
        21,
        FatalActions(fatal_fake));
    gc::system_path::PublishStartupFatal(
        fatal_latch,
        "duplicate",
        L"duplicate",
        21,
        FatalActions(fatal_fake));
    failures += Expect(
        fatal_fake.logs == 1 && fatal_fake.modals == 1 &&
            fatal_fake.terminations == 1 &&
            fatal_fake.fail_fast_calls == 1 &&
            fatal_fake.exit_code == 21 &&
            fatal_fake.log.find("configured_tree") != std::string::npos &&
            fatal_fake.modal.find(L".\\system") != std::wstring::npos &&
            fatal_fake.title == L"GCLoader startup error",
        "startup fatal is one-shot and exhausts termination fallbacks");

    FatalFake titled_fatal{};
    std::atomic_bool titled_latch{false};
    gc::system_path::PublishStartupFatal(
        titled_latch,
        "Ttx initialization failed",
        L"Ttx initialization failed",
        L"TtxUpdateDownloader initialization error",
        22,
        FatalActions(titled_fatal));
    failures += Expect(
        titled_fatal.logs == 1 && titled_fatal.modals == 1 &&
            titled_fatal.terminations == 1 &&
            titled_fatal.fail_fast_calls == 1 &&
            titled_fatal.exit_code == 22 &&
            titled_fatal.title ==
                L"TtxUpdateDownloader initialization error",
        "startup fatal supports a feature-specific title");

    return failures == 0 ? 0 : 1;
}

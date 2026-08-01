#include "SystemPath/SystemRoot.h"

#include <algorithm>
#include <array>
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
    bool already_exists{};
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
    return !fake.already_exists;
}

gc::system_path::DirectoryActions MakeDirectoryActions(
    DirectoryFake& fake) {
    return {
        .context = &fake,
        .create_directories = &FakeCreateDirectories,
    };
}

constexpr std::array<std::wstring_view, 8> kRequiredLeaves{
    L"CmdFile\\log",
    L"DUA\\data",
    L"DUA\\decrypt",
    L"DUA\\download",
    L"DUA\\event",
    L"DUA\\news",
    L"DUA\\unpack",
    L"DUA\\work",
};

int ExpectCompleteTree(
    const DirectoryFake& fake,
    std::size_t offset,
    const std::filesystem::path& root,
    std::string_view name) {
    if (fake.calls.size() != offset + kRequiredLeaves.size()) {
        return Expect(false, name);
    }
    for (std::size_t index = 0; index < kRequiredLeaves.size(); ++index) {
        if (fake.calls[offset + index] !=
            root / kRequiredLeaves[index]) {
            return Expect(false, name);
        }
    }
    return 0;
}

} // namespace

int main() {
    using namespace gc::system_path;
    int failures = 0;

    DirectoryFake fallback_fake{.failing_calls = {0}};
    const auto fallback = PrepareGameSystemRoot(
        {
            .registry_enabled = true,
            .configured_path = "D:\\system",
            .config_directory = L"H:\\遊戲",
        },
        MakeDirectoryActions(fallback_fake));
    failures += Expect(
        fallback && fallback->configured_path_changed &&
            fallback->runtime.configured_path == ".\\system" &&
            fallback->runtime.resolved_path == L"H:\\遊戲\\system" &&
            fallback->runtime.redirect_enabled,
        "unavailable shipped default falls back beside config");
    failures += Expect(
        fallback_fake.calls.front() ==
            std::filesystem::path{L"D:\\system\\CmdFile\\log"},
        "fallback probes the configured default first");
    failures += ExpectCompleteTree(
        fallback_fake,
        1,
        L"H:\\遊戲\\system",
        "fallback creates the complete required tree");

    DirectoryFake relative_fake{};
    const auto relative = PrepareGameSystemRoot(
        {
            .registry_enabled = true,
            .configured_path = ".\\custom",
            .config_directory = L"H:\\遊戲",
        },
        MakeDirectoryActions(relative_fake));
    failures += Expect(
        relative && !relative->configured_path_changed &&
            relative->runtime.configured_path == ".\\custom" &&
            relative->runtime.resolved_path == L"H:\\遊戲\\custom" &&
            relative->runtime.redirect_enabled,
        "relative custom root resolves once against config directory");
    failures += ExpectCompleteTree(
        relative_fake,
        0,
        L"H:\\遊戲\\custom",
        "relative custom root creates the complete required tree");

    DirectoryFake default_fake{};
    const auto available_default = PrepareGameSystemRoot(
        {
            .registry_enabled = true,
            .configured_path = "D:\\system",
            .config_directory = L"H:\\game",
        },
        MakeDirectoryActions(default_fake));
    failures += Expect(
        available_default &&
            !available_default->configured_path_changed &&
            available_default->runtime.configured_path == "D:\\system" &&
            available_default->runtime.resolved_path == L"D:\\system" &&
            !available_default->runtime.redirect_enabled,
        "available shipped default stays configured without redirect");
    failures += ExpectCompleteTree(
        default_fake,
        0,
        L"D:\\system",
        "available default creates the complete required tree");

    DirectoryFake absolute_failure_fake{.failing_calls = {0}};
    const auto absolute_failure = PrepareGameSystemRoot(
        {
            .registry_enabled = true,
            .configured_path = "R:\\cabinet",
            .config_directory = L"H:\\game",
        },
        MakeDirectoryActions(absolute_failure_fake));
    failures += Expect(
        !absolute_failure &&
            absolute_failure.error().stage ==
                RootPrepareStage::configured_tree &&
            absolute_failure.error().path ==
                L"R:\\cabinet\\CmdFile\\log" &&
            !absolute_failure.error().configured_was_default &&
            absolute_failure_fake.calls.size() == 1,
        "unavailable custom absolute root fails without fallback");

    DirectoryFake relative_failure_fake{.failing_calls = {0}};
    const auto relative_failure = PrepareGameSystemRoot(
        {
            .registry_enabled = true,
            .configured_path = ".\\custom",
            .config_directory = L"H:\\game",
        },
        MakeDirectoryActions(relative_failure_fake));
    failures += Expect(
        !relative_failure &&
            relative_failure.error().stage ==
                RootPrepareStage::configured_tree &&
            relative_failure.error().path ==
                L"H:\\game\\custom\\CmdFile\\log" &&
            !relative_failure.error().configured_was_default &&
            relative_failure_fake.calls.size() == 1,
        "unavailable custom relative root fails without fallback");

    DirectoryFake disabled_fake{};
    const auto disabled = PrepareGameSystemRoot(
        {
            .registry_enabled = false,
            .configured_path = ".\\ignored",
            .config_directory = L"H:\\game",
        },
        MakeDirectoryActions(disabled_fake));
    failures += Expect(
        disabled && !disabled->configured_path_changed &&
            disabled->runtime.configured_path == "D:\\system" &&
            disabled->runtime.resolved_path == L"D:\\system" &&
            !disabled->runtime.redirect_enabled,
        "disabled mode ignores alternate config and uses real default");
    failures += ExpectCompleteTree(
        disabled_fake,
        0,
        L"D:\\system",
        "disabled mode creates the complete real default tree");

    DirectoryFake disabled_failure_fake{.failing_calls = {0}};
    const auto disabled_failure = PrepareGameSystemRoot(
        {
            .registry_enabled = false,
            .configured_path = "R:\\ignored",
            .config_directory = L"H:\\game",
        },
        MakeDirectoryActions(disabled_failure_fake));
    failures += Expect(
        !disabled_failure &&
            disabled_failure.error().stage ==
                RootPrepareStage::configured_tree &&
            !disabled_failure.error().registry_enabled &&
            disabled_failure.error().configured_was_default &&
            disabled_failure_fake.calls.size() == 1,
        "disabled mode explicitly fails when real default is unavailable");

    DirectoryFake existing_fake{.already_exists = true};
    const auto existing = PrepareGameSystemRoot(
        {
            .registry_enabled = true,
            .configured_path = ".\\existing",
            .config_directory = L"H:\\game",
        },
        MakeDirectoryActions(existing_fake));
    failures += Expect(
        existing && existing_fake.calls.size() == kRequiredLeaves.size(),
        "false with clear error means required directories already exist");

    constexpr std::array default_equivalents{
        std::string_view{"d:/SYSTEM/."},
        std::string_view{"D:\\temp\\..\\system"},
    };
    for (const std::string_view spelling : default_equivalents) {
        DirectoryFake equivalent_fake{.failing_calls = {0}};
        const auto equivalent = PrepareGameSystemRoot(
            {
                .registry_enabled = true,
                .configured_path = spelling,
                .config_directory = L"H:\\game",
            },
            MakeDirectoryActions(equivalent_fake));
        failures += Expect(
            equivalent && equivalent->configured_path_changed &&
                equivalent->runtime.configured_path == ".\\system" &&
                equivalent_fake.calls.size() ==
                    1 + kRequiredLeaves.size(),
            "lexical and case-equivalent default spelling can fall back");
    }

    DirectoryFake fallback_failure_fake{.failing_calls = {0, 1}};
    const auto fallback_failure = PrepareGameSystemRoot(
        {
            .registry_enabled = true,
            .configured_path = "D:\\system",
            .config_directory = L"H:\\game",
        },
        MakeDirectoryActions(fallback_failure_fake));
    failures += Expect(
        !fallback_failure &&
            fallback_failure.error().stage ==
                RootPrepareStage::fallback_tree &&
            fallback_failure.error().path ==
                L"H:\\game\\system\\CmdFile\\log" &&
            fallback_failure.error().registry_enabled &&
            fallback_failure.error().configured_was_default,
        "failed fallback reports its concrete fallback path");

    DirectoryFake empty_fake{};
    const auto empty = PrepareGameSystemRoot(
        {
            .registry_enabled = true,
            .configured_path = "",
            .config_directory = L"H:\\game",
        },
        MakeDirectoryActions(empty_fake));
    failures += Expect(
        !empty &&
            empty.error().stage ==
                RootPrepareStage::invalid_configured_path &&
            empty.error().error ==
                std::make_error_code(std::errc::invalid_argument) &&
            empty_fake.calls.empty(),
        "empty configured path fails before filesystem access");

    DirectoryFake invalid_utf8_fake{};
    const std::string invalid_utf8{"\xF0\x28\x8C\x28", 4};
    const auto invalid_utf8_result = PrepareGameSystemRoot(
        {
            .registry_enabled = true,
            .configured_path = invalid_utf8,
            .config_directory = L"H:\\game",
        },
        MakeDirectoryActions(invalid_utf8_fake));
    failures += Expect(
        !invalid_utf8_result &&
            invalid_utf8_result.error().stage ==
                RootPrepareStage::invalid_configured_path &&
            invalid_utf8_fake.calls.empty(),
        "invalid UTF-8 configured path fails before filesystem access");

    return failures == 0 ? 0 : 1;
}

#include "Loader/StartupConfiguration.h"

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
#include <variant>

namespace
{
    int g_failures = 0;

    void Expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++g_failures;
        }
    }

    std::string ReadDistributedConfig()
    {
        std::ifstream input{GC_TEST_CONFIG_PATH, std::ios::binary};
        Expect(input.is_open(), "distributed config opens");
        if (!input)
        {
            return {};
        }
        return {
            std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{},
        };
    }

    std::string ReplaceUnique(
        std::string source,
        std::string_view before,
        std::string_view after)
    {
        const auto position = source.find(before);
        const bool unique =
            position != std::string::npos &&
            source.find(before, position + before.size()) == std::string::npos;
        Expect(unique, "fixture replacement has one unambiguous source");
        if (!unique)
        {
            return source;
        }
        source.replace(position, before.size(), after);
        return source;
    }

    struct FakeActions
    {
        std::string source;
        std::string destination_text;
        std::string temporary_text;
        std::filesystem::path temporary_path;
        std::filesystem::path destination_path;
        int read_calls{};
        int probe_calls{};
        int directory_calls{};
        int write_calls{};
        int replace_calls{};
        int remove_calls{};
        bool native_storage_available{true};
        bool fail_first_directory{};
        bool fail_replace{};
    };

    std::expected<std::string, std::string> FakeRead(
        void* context,
        const std::filesystem::path&) noexcept
    {
        auto& fake = *static_cast<FakeActions*>(context);
        ++fake.read_calls;
        try
        {
            return fake.source;
        }
        catch (...)
        {
            return std::unexpected(std::string{"fake read allocation failed"});
        }
    }

    gc::testmode_storage::NativeStorageProbeResult FakeProbe(
        void* context) noexcept
    {
        auto& fake = *static_cast<FakeActions*>(context);
        ++fake.probe_calls;
        return {
            .available = fake.native_storage_available,
        };
    }

    bool FakeCreateDirectories(
        void* context,
        const std::filesystem::path&,
        std::error_code& error) noexcept
    {
        auto& fake = *static_cast<FakeActions*>(context);
        ++fake.directory_calls;
        if (fake.fail_first_directory && fake.directory_calls == 1)
        {
            error = std::make_error_code(std::errc::permission_denied);
            return false;
        }
        error.clear();
        return true;
    }

    std::expected<void, std::string> FakeWrite(
        void* context,
        const std::filesystem::path& path,
        std::string_view text) noexcept
    {
        auto& fake = *static_cast<FakeActions*>(context);
        ++fake.write_calls;
        try
        {
            fake.temporary_path = path;
            fake.temporary_text.assign(text);
            return {};
        }
        catch (...)
        {
            return std::unexpected(std::string{"fake write allocation failed"});
        }
    }

    std::expected<void, std::string> FakeReplace(
        void* context,
        const std::filesystem::path& destination,
        const std::filesystem::path& replacement) noexcept
    {
        auto& fake = *static_cast<FakeActions*>(context);
        ++fake.replace_calls;
        if (fake.fail_replace)
        {
            return std::unexpected(std::string{"injected replacement failure"});
        }
        try
        {
            if (replacement != fake.temporary_path)
            {
                return std::unexpected(
                    std::string{"replacement did not use the written temporary"});
            }
            fake.destination_path = destination;
            fake.destination_text = fake.temporary_text;
            fake.temporary_text.clear();
            return {};
        }
        catch (...)
        {
            return std::unexpected(std::string{"fake replace allocation failed"});
        }
    }

    void FakeRemove(
        void* context,
        const std::filesystem::path& path) noexcept
    {
        auto& fake = *static_cast<FakeActions*>(context);
        ++fake.remove_calls;
        if (path == fake.temporary_path)
        {
            fake.temporary_text.clear();
        }
    }

    gc::loader::StartupConfigurationActions ActionsFor(FakeActions& fake)
    {
        return {
            .config_read = {
                .context = &fake,
                .read = &FakeRead,
            },
            .probe_native_storage = &FakeProbe,
            .probe_context = &fake,
            .directories = {
                .context = &fake,
                .create_directories = &FakeCreateDirectories,
            },
            .config_write = {
                .context = &fake,
                .write = &FakeWrite,
                .replace = &FakeReplace,
                .remove = &FakeRemove,
            },
        };
    }

    bool HasChange(
        const gc::loader::GameProcessConfiguration& game,
        gc::loader::StartupConfigChange change)
    {
        return std::ranges::find(game.changes, change) != game.changes.end();
    }

    bool HasSemanticPath(
        const gc::loader::StartupConfigurationError& error,
        std::string_view path)
    {
        return std::ranges::find_if(
            error.semantic_errors,
            [&](const gc::config::ConfigError& item)
            {
                return item.path.Render() == path;
            }) != error.semantic_errors.end();
    }

    void ValidCurrentGameConfigPublishesWithoutWriting()
    {
        FakeActions fake{
            .source = ReadDistributedConfig(),
        };
        fake.destination_text = fake.source;

        const auto result = gc::loader::PrepareProcessConfiguration(
            R"(C:\game\config.toml)",
            gc::nesys_service::ProcessRole::Game,
            ActionsFor(fake));

        Expect(result.has_value(), "valid game config is prepared");
        Expect(fake.read_calls == 1, "game config is read exactly once");
        Expect(fake.probe_calls == 1, "game native storage is probed once");
        Expect(
            fake.directory_calls > 0,
            "game system-root directories are prepared");
        Expect(fake.write_calls == 0, "unchanged game config is not written");
        Expect(fake.replace_calls == 0, "unchanged config is not replaced");
        Expect(fake.remove_calls == 0, "unchanged config needs no cleanup");
        if (!result)
        {
            return;
        }

        const auto* game =
            std::get_if<gc::loader::GameProcessConfiguration>(&*result);
        Expect(game != nullptr, "game role publishes only game settings");
        if (game != nullptr)
        {
            Expect(game->changes.empty(), "unchanged config records no repairs");
            Expect(!game->persisted, "unchanged config is not marked persisted");
            const auto& widescreen = game->settings.windowed_widescreen();
            Expect(!widescreen.enabled(), "distributed widescreen is disabled");
            Expect(
                widescreen.output_width() == 1920 &&
                    widescreen.output_height() == 1280,
                "distributed widescreen dimensions compile exactly");
            Expect(
                widescreen.clip_policy() ==
                    gc::windowed_widescreen::StageClipPolicy::live_frustum,
                "distributed widescreen clip policy compiles exactly");
        }
    }

    void RecognizedMigrationPersistsExactlyOnce()
    {
        const auto current = ReadDistributedConfig();
        FakeActions fake{
            .source = ReplaceUnique(
                current,
                "audio_backend = 'directsound'",
                "enable_wasapi_exclusive_audio = false"),
        };
        fake.destination_text = fake.source;

        const auto result = gc::loader::PrepareProcessConfiguration(
            R"(C:\game\config.toml)",
            gc::nesys_service::ProcessRole::Game,
            ActionsFor(fake));

        Expect(result.has_value(), "recognized migration is prepared");
        Expect(fake.read_calls == 1, "migrated config is read once");
        Expect(fake.write_calls == 1, "migration writes one temporary");
        Expect(fake.replace_calls == 1, "migration replaces once");
        Expect(fake.remove_calls == 0, "successful migration needs no cleanup");
        if (!result)
        {
            return;
        }

        const auto* game =
            std::get_if<gc::loader::GameProcessConfiguration>(&*result);
        Expect(game != nullptr, "migrated game config publishes game settings");
        if (game != nullptr)
        {
            Expect(game->persisted, "migration is marked persisted");
            Expect(
                game->changes.size() == 1 &&
                HasChange(
                    *game,
                    gc::loader::StartupConfigChange::recognized_migration),
                "migration records its reason once");
        }

        const auto reparsed =
            gc::config::ParseConfigDocument(fake.destination_text);
        Expect(reparsed.has_value(), "persisted migration strictly reparses");
        if (reparsed)
        {
            Expect(
                !reparsed->migrations.any(),
                "persisted migration is already canonical");
            Expect(
                gc::config::ConfigCompiler::Compile(reparsed->document)
                .has_value(),
                "persisted migration semantically compiles");
        }
    }

    void ApprovedRepairsShareOneAtomicReplacement()
    {
        const auto current = ReadDistributedConfig();
        FakeActions fake{
            .source = ReplaceUnique(
                current,
                "enabled = false",
                "enabled = true"),
            .native_storage_available = false,
            .fail_first_directory = true,
        };
        fake.destination_text = fake.source;

        const auto result = gc::loader::PrepareProcessConfiguration(
            R"(C:\game\config.toml)",
            gc::nesys_service::ProcessRole::Game,
            ActionsFor(fake));

        Expect(result.has_value(), "approved fallback repairs are prepared");
        Expect(fake.probe_calls == 1, "fallback run probes native storage once");
        Expect(
            fake.directory_calls > 1,
            "fallback retries directory preparation at the fallback root");
        Expect(fake.write_calls == 1, "both repairs share one temporary write");
        Expect(fake.replace_calls == 1, "both repairs share one replacement");
        if (!result)
        {
            return;
        }

        const auto* game =
            std::get_if<gc::loader::GameProcessConfiguration>(&*result);
        Expect(game != nullptr, "fallback run publishes game settings");
        if (game != nullptr)
        {
            Expect(game->persisted, "approved repairs are marked persisted");
            Expect(
                game->changes.size() == 2 &&
                HasChange(
                    *game,
                    gc::loader::StartupConfigChange::system_path_fallback) &&
                HasChange(
                    *game,
                    gc::loader::StartupConfigChange::native_storage_redirect),
                "both approved repair reasons are recorded");
        }

        const auto reparsed =
            gc::config::ParseConfigDocument(fake.destination_text);
        Expect(reparsed.has_value(), "repaired document strictly reparses");
        if (reparsed)
        {
            Expect(
                reparsed->document.registry().system_path() ==
                gc::system_path::kFallbackConfiguredPath,
                "persisted document contains the system-path fallback");
            Expect(
                reparsed->document.experimental()
                        .enable_testmode_storage_redirect(),
                "persisted document contains the storage redirect repair");
            Expect(
                gc::config::ConfigCompiler::Compile(reparsed->document)
                .has_value(),
                "persisted repairs semantically compile");
        }
    }

    void SemanticFailurePreventsGameSideEffectsAndWrites()
    {
        const auto current = ReadDistributedConfig();
        auto migrated = ReplaceUnique(
            current,
            "audio_backend = 'directsound'",
            "enable_wasapi_exclusive_audio = false");
        FakeActions fake{
            .source = ReplaceUnique(
                std::move(migrated),
                "target_fps = 60",
                "target_fps = 59"),
        };
        fake.destination_text = fake.source;

        const auto result = gc::loader::PrepareProcessConfiguration(
            R"(C:\game\config.toml)",
            gc::nesys_service::ProcessRole::Game,
            ActionsFor(fake));

        Expect(!result.has_value(), "semantic-invalid migration is rejected");
        Expect(fake.read_calls == 1, "semantic-invalid config is read once");
        Expect(fake.probe_calls == 0, "semantic failure prevents game probe");
        Expect(
            fake.directory_calls == 0,
            "semantic failure prevents directory preparation");
        Expect(fake.write_calls == 0, "semantic failure performs no write");
        Expect(fake.replace_calls == 0, "semantic failure performs no replace");
        if (!result)
        {
            Expect(
                result.error().stage ==
                gc::loader::StartupConfigurationStage::semantic,
                "semantic failure reports the semantic stage");
            Expect(
                HasSemanticPath(result.error(), "experimental.target_fps"),
                "semantic failure retains structured target FPS error");
        }
    }

    void ReplacementFailurePublishesNothingAndRequestsCleanup()
    {
        const auto current = ReadDistributedConfig();
        FakeActions fake{
            .source = ReplaceUnique(
                current,
                "audio_backend = 'directsound'",
                "enable_wasapi_exclusive_audio = false"),
            .fail_replace = true,
        };
        fake.destination_text = fake.source;
        const auto original_destination = fake.destination_text;

        const auto result = gc::loader::PrepareProcessConfiguration(
            R"(C:\game\config.toml)",
            gc::nesys_service::ProcessRole::Game,
            ActionsFor(fake));

        Expect(!result.has_value(), "replacement failure publishes no settings");
        Expect(fake.write_calls == 1, "replacement failure wrote a temporary");
        Expect(fake.replace_calls == 1, "replacement failure was attempted once");
        Expect(fake.remove_calls == 1, "replacement failure requests cleanup");
        Expect(
            fake.destination_text == original_destination,
            "replacement failure retains the prior destination");
        Expect(
            fake.temporary_text.empty(),
            "replacement failure cleans the fake temporary");
        if (!result)
        {
            Expect(
                result.error().stage ==
                gc::loader::StartupConfigurationStage::persistence,
                "replacement failure reports persistence stage");
        }
    }

    void NesysRoleReadsOnlyAndPublishesNesysSettings()
    {
        FakeActions fake{
            .source = ReadDistributedConfig(),
        };
        fake.destination_text = fake.source;

        const auto result = gc::loader::PrepareProcessConfiguration(
            R"(C:\game\config.toml)",
            gc::nesys_service::ProcessRole::Service,
            ActionsFor(fake));

        Expect(result.has_value(), "NESYS config is prepared");
        Expect(fake.read_calls == 1, "NESYS config is read exactly once");
        Expect(fake.probe_calls == 0, "NESYS never runs game storage probe");
        Expect(fake.directory_calls == 0, "NESYS never prepares game directories");
        Expect(fake.write_calls == 0, "NESYS never writes config");
        Expect(fake.replace_calls == 0, "NESYS never replaces config");
        Expect(fake.remove_calls == 0, "NESYS never requests write cleanup");
        if (!result)
        {
            return;
        }

        const auto* nesys =
            std::get_if<gc::loader::NesysProcessConfiguration>(&*result);
        Expect(nesys != nullptr, "NESYS role publishes only NESYS settings");
        if (nesys != nullptr)
        {
            Expect(
                nesys->logging.level() == gc::logging::LoaderLogLevel::Info,
                "NESYS receives compiled logging settings");
            Expect(
                nesys->nesys.server_address().ansi() == "127.0.0.1",
                "NESYS receives compiled server settings");
        }
    }
} // namespace

int main()
{
    ValidCurrentGameConfigPublishesWithoutWriting();
    RecognizedMigrationPersistsExactlyOnce();
    ApprovedRepairsShareOneAtomicReplacement();
    SemanticFailurePreventsGameSideEffectsAndWrites();
    ReplacementFailurePublishesNothingAndRequestsCleanup();
    NesysRoleReadsOnlyAndPublishesNesysSettings();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

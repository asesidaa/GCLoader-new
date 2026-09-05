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
#include <utility>
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

    // Substitute complete startup operations. Filesystem mechanics remain in
    // their production owners; these cases exercise the startup coordinator.
    struct StartupScenario
    {
        std::string source;
        std::string persisted_text;
        int read_calls{};
        int probe_calls{};
        int root_calls{};
        int persist_calls{};
        bool native_storage_available{true};
        bool fail_persistence{};
        gc::system_path::PreparedRoot prepared_root{
            .runtime = {
                .configured_path = "D:\\system",
                .resolved_path = L"D:\\system",
                .redirect_enabled = false,
            },
        };
    };

    std::expected<std::string, std::string> ReadConfig(
        void* context, const std::filesystem::path&) noexcept
    {
        auto& scenario = *static_cast<StartupScenario*>(context);
        ++scenario.read_calls;
        return scenario.source;
    }

    gc::testmode_storage::NativeStorageProbeResult ProbeStorage(void* context) noexcept
    {
        auto& scenario = *static_cast<StartupScenario*>(context);
        ++scenario.probe_calls;
        return {.available = scenario.native_storage_available};
    }

    std::expected<gc::system_path::PreparedRoot, gc::system_path::RootPrepareError>
    PrepareRoot(void* context, gc::system_path::RootPrepareRequest) noexcept
    {
        auto& scenario = *static_cast<StartupScenario*>(context);
        ++scenario.root_calls;
        return scenario.prepared_root;
    }

    std::expected<void, gc::config::ConfigPersistenceError> PersistConfig(
        void* context, const std::filesystem::path&,
        const gc::config::ConfigDocument& document) noexcept
    {
        auto& scenario = *static_cast<StartupScenario*>(context);
        ++scenario.persist_calls;
        if (scenario.fail_persistence)
            return std::unexpected(gc::config::ConfigPersistenceError{
                .stage = gc::config::ConfigPersistenceStage::atomic_replace,
                .message = "replacement operation failed",
            });
        auto serialized = gc::config::SerializeConfigDocument(document);
        if (!serialized)
            return std::unexpected(gc::config::ConfigPersistenceError{
                .stage = gc::config::ConfigPersistenceStage::serialize,
                .message = serialized.error().message,
            });
        scenario.persisted_text = std::move(*serialized);
        return {};
    }

    gc::loader::StartupConfigurationActions ActionsFor(StartupScenario& scenario)
    {
        return {
            .context = &scenario,
            .read_config = &ReadConfig,
            .probe_native_storage = &ProbeStorage,
            .prepare_system_root = &PrepareRoot,
            .persist_config = &PersistConfig,
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
        StartupScenario scenario{
            .source = ReadDistributedConfig(),
        };

        const auto result = gc::loader::PrepareProcessConfiguration(
            R"(C:\game\config.toml)",
            gc::nesys_service::ProcessRole::Game,
            ActionsFor(scenario));

        Expect(result.has_value(), "valid game config is prepared");
        Expect(scenario.read_calls == 1, "game config is read exactly once");
        Expect(scenario.probe_calls == 1, "game native storage is probed once");
        Expect(
            scenario.root_calls > 0,
            "game system-root preparation is requested");
        Expect(scenario.persist_calls == 0, "unchanged game config is not written");
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
        }
    }

    void RecognizedMigrationPersistsExactlyOnce()
    {
        const auto current = ReadDistributedConfig();
        StartupScenario scenario{
            .source = ReplaceUnique(
                current,
                "audio_backend = 'directsound'",
                "enable_wasapi_exclusive_audio = false"),
        };

        const auto result = gc::loader::PrepareProcessConfiguration(
            R"(C:\game\config.toml)",
            gc::nesys_service::ProcessRole::Game,
            ActionsFor(scenario));

        Expect(result.has_value(), "recognized migration is prepared");
        Expect(scenario.read_calls == 1, "migrated config is read once");
        Expect(scenario.persist_calls == 1, "migration persists exactly once");
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
            gc::config::ParseConfigDocument(scenario.persisted_text);
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

    void ApprovedRepairsShareOnePersistenceOperation()
    {
        const auto current = ReadDistributedConfig();
        StartupScenario scenario{
            .source = ReplaceUnique(
                current,
                "enabled = false",
                "enabled = true"),
            .native_storage_available = false,
        };

        scenario.prepared_root = {
            .runtime = {
                .configured_path = std::string{gc::system_path::kFallbackConfiguredPath},
                .resolved_path = LR"(C:\game\system)",
                .redirect_enabled = true,
            },
            .configured_path_changed = true,
        };

        const auto result = gc::loader::PrepareProcessConfiguration(
            R"(C:\game\config.toml)",
            gc::nesys_service::ProcessRole::Game,
            ActionsFor(scenario));

        Expect(result.has_value(), "approved fallback repairs are prepared");
        Expect(scenario.probe_calls == 1, "fallback run probes native storage once");
        Expect(scenario.root_calls == 1, "startup prepares its system root once");
        Expect(scenario.persist_calls == 1, "both repairs share one persistence operation");
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
            gc::config::ParseConfigDocument(scenario.persisted_text);
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
        StartupScenario scenario{
            .source = ReplaceUnique(
                std::move(migrated),
                "target_fps = 60",
                "target_fps = 59"),
        };

        const auto result = gc::loader::PrepareProcessConfiguration(
            R"(C:\game\config.toml)",
            gc::nesys_service::ProcessRole::Game,
            ActionsFor(scenario));

        Expect(!result.has_value(), "semantic-invalid migration is rejected");
        Expect(scenario.read_calls == 1, "semantic-invalid config is read once");
        Expect(scenario.probe_calls == 0, "semantic failure prevents game probe");
        Expect(
            scenario.root_calls == 0,
            "semantic failure prevents directory preparation");
        Expect(scenario.persist_calls == 0, "semantic failure performs no write");
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

    void PersistenceFailurePublishesNothing()
    {
        const auto current = ReadDistributedConfig();
        StartupScenario scenario{
            .source = ReplaceUnique(
                current,
                "audio_backend = 'directsound'",
                "enable_wasapi_exclusive_audio = false"),
            .fail_persistence = true,
        };

        const auto result = gc::loader::PrepareProcessConfiguration(
            R"(C:\game\config.toml)",
            gc::nesys_service::ProcessRole::Game,
            ActionsFor(scenario));

        Expect(!result.has_value(), "replacement failure publishes no settings");
        Expect(scenario.persist_calls == 1, "persistence failure was attempted once");
        Expect(scenario.persisted_text.empty(),
               "failed persistence is not marked successful");
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
        StartupScenario scenario{
            .source = ReadDistributedConfig(),
        };

        const auto result = gc::loader::PrepareProcessConfiguration(
            R"(C:\game\config.toml)",
            gc::nesys_service::ProcessRole::Service,
            ActionsFor(scenario));

        Expect(result.has_value(), "NESYS config is prepared");
        Expect(scenario.read_calls == 1, "NESYS config is read exactly once");
        Expect(scenario.probe_calls == 0, "NESYS never runs game storage probe");
        Expect(scenario.root_calls == 0, "NESYS never prepares game directories");
        Expect(scenario.persist_calls == 0, "NESYS never writes config");
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
    ApprovedRepairsShareOnePersistenceOperation();
    SemanticFailurePreventsGameSideEffectsAndWrites();
    PersistenceFailurePublishesNothing();
    NesysRoleReadsOnlyAndPublishesNesysSettings();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

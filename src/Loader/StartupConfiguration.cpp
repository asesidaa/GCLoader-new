#include "Loader/StartupConfiguration.h"

#include <cassert>
#include <exception>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

namespace gc::loader
{
    namespace
    {
        std::string PathForDiagnostic(const std::filesystem::path& path)
        {
            const auto utf8 = path.u8string();
            return {
                reinterpret_cast<const char*>(utf8.data()),
                utf8.size(),
            };
        }

        std::expected<std::string, std::string> ProductionRead(
            void*,
            const std::filesystem::path& path) noexcept
        {
            try
            {
                std::ifstream input{path, std::ios::binary};
                if (!input)
                {
                    return std::unexpected(
                        "Could not open config file '" +
                        PathForDiagnostic(path) + "' for reading");
                }
                return std::string{
                    std::istreambuf_iterator<char>{input},
                    std::istreambuf_iterator<char>{},
                };
            }
            catch (const std::exception& error)
            {
                return std::unexpected(
                    "Could not read config file '" +
                    PathForDiagnostic(path) + "': " + error.what());
            }
            catch (...)
            {
                return std::unexpected(
                    "Could not read config file '" +
                    PathForDiagnostic(path) + "'");
            }
        }

        testmode_storage::NativeStorageProbeResult ProductionProbe(
            void*) noexcept
        {
            return testmode_storage::ProbeNativeStorage();
        }

        StartupConfigurationError Error(
            StartupConfigurationStage stage,
            std::string message)
        {
            return {
                .stage = stage,
                .message = std::move(message),
            };
        }

        StartupConfigurationError SemanticError(
            config::ConfigErrors errors)
        {
            auto message = config::FormatConfigErrors(errors);
            return {
                .stage = StartupConfigurationStage::semantic,
                .message = std::move(message),
                .semantic_errors = std::move(errors),
            };
        }

        const char* RootPrepareStageName(
            system_path::RootPrepareStage stage) noexcept
        {
            using enum system_path::RootPrepareStage;
            switch (stage)
            {
            case invalid_configured_path:
                return "invalid_configured_path";
            case configured_tree:
                return "configured_tree";
            case fallback_tree:
                return "fallback_tree";
            }
            return "unknown";
        }

        std::string FormatRootPrepareError(
            const system_path::RootPrepareError& error,
            std::string_view configured_path)
        {
            std::ostringstream message;
            message
                << "System path preparation failed"
                << " stage=" << RootPrepareStageName(error.stage)
                << " configured_path='" << configured_path << "'"
                << " failed_path='" << PathForDiagnostic(error.path) << "'"
                << " error=" << error.error.value()
                << " system_message='" << error.error.message() << "'";
            return message.str();
        }

        bool IsKnownRole(nesys_service::ProcessRole role) noexcept
        {
            return role == nesys_service::ProcessRole::Game ||
                role == nesys_service::ProcessRole::Service;
        }

        std::optional<StartupConfigurationError> ValidateActions(
            nesys_service::ProcessRole role,
            const StartupConfigurationActions& actions)
        {
            if (actions.config_read.read == nullptr)
            {
                return Error(
                    StartupConfigurationStage::read,
                    "Config read action is missing");
            }
            if (role != nesys_service::ProcessRole::Game)
            {
                return std::nullopt;
            }
            if (actions.probe_native_storage == nullptr)
            {
                return Error(
                    StartupConfigurationStage::system_path,
                    "Native-storage probe action is missing");
            }
            if (actions.directories.create_directories == nullptr)
            {
                return Error(
                    StartupConfigurationStage::system_path,
                    "System-path directory action is missing");
            }
            if (actions.config_write.write == nullptr ||
                actions.config_write.replace == nullptr ||
                actions.config_write.remove == nullptr)
            {
                return Error(
                    StartupConfigurationStage::persistence,
                    "Atomic config write actions are incomplete");
            }
            return std::nullopt;
        }
    } // namespace

    StartupConfigurationActions
    ProductionStartupConfigurationActions() noexcept
    {
        return {
            .config_read = {
                .read = &ProductionRead,
            },
            .probe_native_storage = &ProductionProbe,
            .directories = system_path::ProductionDirectoryActions(),
            .config_write = config::ProductionAtomicConfigWriteActions(),
        };
    }

    std::expected<PreparedProcessConfiguration, StartupConfigurationError>
    PrepareProcessConfiguration(
        const std::filesystem::path& config_path,
        nesys_service::ProcessRole role,
        const StartupConfigurationActions& actions) noexcept
    {
        StartupConfigurationStage active_stage =
            StartupConfigurationStage::read;
        try
        {
            if (!IsKnownRole(role))
            {
                return std::unexpected(Error(
                    StartupConfigurationStage::read,
                    "Unsupported process role"));
            }
            if (auto invalid = ValidateActions(role, actions); invalid)
            {
                return std::unexpected(std::move(*invalid));
            }

            auto text = actions.config_read.read(
                actions.config_read.context,
                config_path);
            if (!text)
            {
                return std::unexpected(Error(
                    StartupConfigurationStage::read,
                    std::move(text.error())));
            }

            active_stage = StartupConfigurationStage::document;
            auto parsed = config::ParseConfigDocument(*text);
            if (!parsed)
            {
                return std::unexpected(Error(
                    StartupConfigurationStage::document,
                    std::move(parsed.error().message)));
            }

            {
                active_stage = StartupConfigurationStage::semantic;
                auto compiled = config::ConfigCompiler::Compile(
                    parsed->document);
                if (!compiled)
                {
                    return std::unexpected(
                        SemanticError(std::move(compiled.error())));
                }

                if (role == nesys_service::ProcessRole::Service)
                {
                    PreparedProcessConfiguration result{
                        std::in_place_type<NesysProcessConfiguration>,
                        compiled->logging(),
                        compiled->nesys(),
                    };
                    assert(std::holds_alternative<
                        NesysProcessConfiguration>(result));
                    return result;
                }
            }

            active_stage = StartupConfigurationStage::system_path;
            std::vector<StartupConfigChange> changes;
            if (parsed->migrations.any())
            {
                changes.push_back(
                    StartupConfigChange::recognized_migration);
            }

            auto candidate = std::move(parsed->document);
            const auto native_storage = actions.probe_native_storage(
                actions.probe_context);
            auto prepared_root = system_path::PrepareGameSystemRoot(
                {
                    .registry_enabled = candidate.registry().enabled(),
                    .configured_path = candidate.registry().system_path(),
                    .config_directory = config_path.parent_path(),
                },
                actions.directories);
            if (!prepared_root)
            {
                return std::unexpected(Error(
                    StartupConfigurationStage::system_path,
                    FormatRootPrepareError(
                        prepared_root.error(),
                        candidate.registry().system_path())));
            }

            if (prepared_root->configured_path_changed)
            {
                candidate.registry().system_path =
                    prepared_root->runtime.configured_path;
                changes.push_back(
                    StartupConfigChange::system_path_fallback);
            }
            if (!native_storage.available &&
                !candidate.experimental()
                          .enable_testmode_storage_redirect())
            {
                candidate.experimental().enable_testmode_storage_redirect =
                    true;
                changes.push_back(
                    StartupConfigChange::native_storage_redirect);
            }

            active_stage = StartupConfigurationStage::semantic;
            auto compiled =
                config::ConfigCompiler::Compile(candidate);
            if (!compiled)
            {
                return std::unexpected(
                    SemanticError(std::move(compiled.error())));
            }

            const bool must_persist = !changes.empty();
            if (must_persist)
            {
                active_stage = StartupConfigurationStage::persistence;
                auto persisted = config::WriteConfigDocumentAtomically(
                    config_path,
                    candidate,
                    actions.config_write);
                if (!persisted)
                {
                    return std::unexpected(Error(
                        StartupConfigurationStage::persistence,
                        std::move(persisted.error().message)));
                }
            }

            PreparedProcessConfiguration result{
                std::in_place_type<GameProcessConfiguration>,
                std::move(*compiled),
                std::move(prepared_root->runtime),
                std::move(changes),
                must_persist,
            };
            assert(std::holds_alternative<GameProcessConfiguration>(result));
            return result;
        }
        catch (const std::exception& error)
        {
            return std::unexpected(Error(
                active_stage,
                "Startup configuration failed: " +
                std::string{error.what()}));
        }
        catch (...)
        {
            return std::unexpected(Error(
                active_stage,
                "Startup configuration failed unexpectedly"));
        }
    }
} // namespace gc::loader

#pragma once

#include "Config/config.h"
#include "SystemPath/SystemRoot.h"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace gc::config {

struct ConfigDocumentMigrations {
    bool registry_paths{};
    bool audio_backend{};

    [[nodiscard]] bool any() const noexcept {
        return registry_paths || audio_backend;
    }
};

struct ParsedInputConfigDocument {
    InputConfig config;
    ConfigDocumentMigrations migrations;
};

[[nodiscard]] std::expected<ParsedInputConfigDocument, std::string>
ParseAndValidateInputConfigDocument(std::string_view text);

struct AtomicConfigWriteActions {
    void* context{};
    std::expected<void, std::string> (*write)(
        void*,
        const std::filesystem::path&,
        std::string_view) noexcept{};
    std::expected<void, std::string> (*replace)(
        void*,
        const std::filesystem::path& destination,
        const std::filesystem::path& replacement) noexcept{};
    void (*remove)(
        void*,
        const std::filesystem::path&) noexcept{};
};

[[nodiscard]] AtomicConfigWriteActions
ProductionAtomicConfigWriteActions() noexcept;

[[nodiscard]] std::expected<void, std::string>
WriteInputConfigAtomically(
    const std::filesystem::path& config_path,
    const InputConfig& config,
    AtomicConfigWriteActions actions =
        ProductionAtomicConfigWriteActions()) noexcept;

struct GameSystemPathPreparationActions {
    gc::system_path::DirectoryActions directories;
    AtomicConfigWriteActions config_write;
};

[[nodiscard]] GameSystemPathPreparationActions
ProductionGameSystemPathPreparationActions() noexcept;

struct PreparedGameSystemPathConfig {
    InputConfig config;
    gc::system_path::RuntimeRoot runtime;
    bool persisted{};
};

[[nodiscard]] std::expected<PreparedGameSystemPathConfig, std::string>
PrepareAndPersistGameSystemPathConfiguration(
    InputConfig config,
    bool document_migrated,
    const std::filesystem::path& config_path,
    bool native_testmode_storage_available,
    GameSystemPathPreparationActions actions =
        ProductionGameSystemPathPreparationActions()) noexcept;

} // namespace gc::config

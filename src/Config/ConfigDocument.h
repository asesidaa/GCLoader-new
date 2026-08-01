#pragma once

#include "Config/config.h"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace gc::config {

struct ParsedInputConfigDocument {
    InputConfig config;
    bool registry_paths_migrated{};
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

} // namespace gc::config

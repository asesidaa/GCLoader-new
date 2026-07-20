#pragma once

#include "Patches/TestModeTiming/TimingSettingsModel.h"

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace gc::test_mode_timing {

enum class ConfigKey {
    GameTimeOffset,
    JudgTimeOffset,
};

enum class ConfigEditStage {
    Missing,
    Duplicate,
    Malformed,
};

struct ConfigEditError {
    ConfigEditStage stage{};
    ConfigKey key{};

    friend bool operator==(
        const ConfigEditError&,
        const ConfigEditError&) = default;
};

struct RewrittenConfig {
    std::vector<std::uint8_t> bytes;
    bool changed{};
};

[[nodiscard]] std::expected<RewrittenConfig, ConfigEditError>
RewriteTimingAssignments(
    std::span<const std::uint8_t> input,
    TimingOffsets offsets);

} // namespace gc::test_mode_timing

#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioTypes.h"

#include <cstdint>
#include <expected>

namespace gc::audio {

enum class AsioBufferRuleError : std::uint8_t {
    invalid_metadata,
    below_minimum,
    above_maximum,
    not_power_of_two,
    not_granular,
};

[[nodiscard]] std::expected<void, AsioBufferRuleError>
ValidateAsioBufferFrames(
    const AsioBufferLimits& limits,
    std::uint32_t requested) noexcept;

} // namespace gc::audio

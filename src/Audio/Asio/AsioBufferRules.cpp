// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioBufferRules.h"

#include <climits>

namespace gc::audio {

std::expected<void, AsioBufferRuleError> ValidateAsioBufferFrames(
    const AsioBufferLimits& limits,
    std::uint32_t requested) noexcept {
    const bool fixed = limits.minimum == limits.maximum;
    if (limits.minimum <= 0 || limits.maximum <= 0 ||
        limits.preferred <= 0 ||
        limits.minimum > limits.maximum ||
        limits.preferred < limits.minimum ||
        limits.preferred > limits.maximum ||
        limits.granularity < -1 ||
        (fixed && limits.granularity != 0) ||
        (!fixed && limits.granularity == 0)) {
        return std::unexpected(
            AsioBufferRuleError::invalid_metadata);
    }

    if (requested > static_cast<std::uint32_t>(LONG_MAX)) {
        return std::unexpected(AsioBufferRuleError::above_maximum);
    }
    const auto requested_frames = static_cast<long>(requested);
    if (requested_frames < limits.minimum) {
        return std::unexpected(AsioBufferRuleError::below_minimum);
    }
    if (requested_frames > limits.maximum) {
        return std::unexpected(AsioBufferRuleError::above_maximum);
    }

    if (fixed || limits.granularity == 1) {
        return {};
    }
    if (limits.granularity == -1) {
        if ((requested & (requested - 1U)) != 0) {
            return std::unexpected(
                AsioBufferRuleError::not_power_of_two);
        }
        return {};
    }
    if (requested % static_cast<std::uint32_t>(limits.granularity) != 0) {
        return std::unexpected(AsioBufferRuleError::not_granular);
    }
    return {};
}

} // namespace gc::audio

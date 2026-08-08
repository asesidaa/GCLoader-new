// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioBufferRules.h"

#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

int ExpectValid(
    const gc::audio::AsioBufferLimits& limits,
    std::uint32_t requested,
    std::string_view name) {
    const auto result = gc::audio::ValidateAsioBufferFrames(
        limits,
        requested);
    if (result) {
        return 0;
    }
    std::cerr << "Expected valid ASIO buffer rule: " << name
              << " requested=" << requested << '\n';
    return 1;
}

int ExpectInvalid(
    const gc::audio::AsioBufferLimits& limits,
    std::uint32_t requested,
    gc::audio::AsioBufferRuleError expected,
    std::string_view name) {
    const auto result = gc::audio::ValidateAsioBufferFrames(
        limits,
        requested);
    if (!result && result.error() == expected) {
        return 0;
    }
    std::cerr << "Expected invalid ASIO buffer rule: " << name
              << " requested=" << requested << '\n';
    return 1;
}

} // namespace

int main() {
    using gc::audio::AsioBufferLimits;
    using gc::audio::AsioBufferRuleError;

    int failures = 0;

    constexpr AsioBufferLimits xonar{192, 2400, 192, 1};
    failures += ExpectValid(xonar, 192, "Xonar minimum");
    failures += ExpectValid(xonar, 193, "Xonar adjacent frame count");
    failures += ExpectValid(xonar, 2400, "Xonar maximum");
    failures += ExpectInvalid(
        xonar,
        191,
        AsioBufferRuleError::below_minimum,
        "Xonar below minimum");
    failures += ExpectInvalid(
        xonar,
        2401,
        AsioBufferRuleError::above_maximum,
        "Xonar above maximum");

    constexpr AsioBufferLimits fixed{256, 256, 256, 0};
    failures += ExpectValid(fixed, 256, "fixed size");
    failures += ExpectInvalid(
        fixed,
        255,
        AsioBufferRuleError::below_minimum,
        "fixed below size");
    failures += ExpectInvalid(
        fixed,
        257,
        AsioBufferRuleError::above_maximum,
        "fixed above size");

    constexpr AsioBufferLimits power_of_two{64, 1024, 256, -1};
    failures += ExpectValid(power_of_two, 64, "power-of-two minimum");
    failures += ExpectValid(power_of_two, 128, "power-of-two middle");
    failures += ExpectValid(power_of_two, 1024, "power-of-two maximum");
    failures += ExpectInvalid(
        power_of_two,
        96,
        AsioBufferRuleError::not_power_of_two,
        "non-power-of-two size");

    constexpr AsioBufferLimits granular{64, 1024, 256, 64};
    failures += ExpectValid(granular, 64, "granular minimum");
    failures += ExpectValid(granular, 128, "granular middle");
    failures += ExpectValid(granular, 1024, "granular maximum");
    failures += ExpectInvalid(
        granular,
        96,
        AsioBufferRuleError::not_granular,
        "non-granular size");

    constexpr AsioBufferLimits invalid_metadata[] = {
        {0, 1024, 256, 1},
        {-1, 1024, 256, 1},
        {64, 0, 256, 1},
        {64, -1, 256, 1},
        {1024, 64, 256, 1},
        {64, 1024, 63, 1},
        {64, 1024, 1025, 1},
        {64, 1024, 0, 1},
        {64, 1024, -1, 1},
        {256, 256, 256, 1},
        {64, 1024, 256, 0},
        {64, 1024, 256, -2},
    };
    for (const auto& invalid : invalid_metadata) {
        failures += ExpectInvalid(
            invalid,
            256,
            AsioBufferRuleError::invalid_metadata,
            "internally inconsistent metadata");
    }

    return failures == 0 ? 0 : 1;
}

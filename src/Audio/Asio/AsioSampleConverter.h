#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace gc::audio {

[[nodiscard]] std::optional<std::size_t> AsioBytesPerSample(
    ASIOSampleType type) noexcept;

[[nodiscard]] bool IsSupportedAsioOutputType(
    ASIOSampleType type) noexcept;

[[nodiscard]] bool ClearAsioChannel(
    ASIOSampleType type,
    std::span<std::byte> destination,
    std::uint32_t frames) noexcept;

[[nodiscard]] bool ConvertFloatStereoChannelToAsio(
    std::span<const float> interleaved_stereo,
    std::uint32_t channel,
    ASIOSampleType type,
    std::span<std::byte> destination) noexcept;

struct AsioStereoConversionStats {
    std::uint64_t clipped_samples{};
    float maximum_absolute_sample{};
    bool all_zero{};
    bool non_finite{};
};

struct AsioStereoConversionResult {
    bool converted{};
    AsioStereoConversionStats stats;
};

[[nodiscard]] AsioStereoConversionResult ConvertFloatStereoToAsio(
    std::span<const float> interleaved_stereo,
    const std::array<ASIOSampleType, 2>& types,
    const std::array<std::span<std::byte>, 2>& destinations) noexcept;

} // namespace gc::audio

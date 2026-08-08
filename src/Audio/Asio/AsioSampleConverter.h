#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioTypes.h"

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

} // namespace gc::audio

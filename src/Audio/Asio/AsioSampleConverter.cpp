// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioSampleConverter.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace gc::audio {

namespace {

static_assert(std::endian::native == std::endian::little);
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(std::numeric_limits<double>::is_iec559);

std::int32_t QuantizeInteger(float input, unsigned width) noexcept {
    const double clipped = std::clamp(
        static_cast<double>(input),
        -1.0,
        1.0);
    const std::int64_t scale = std::int64_t{1} << (width - 1U);
    const auto rounded = static_cast<std::int64_t>(
        std::round(clipped * static_cast<double>(scale)));
    return static_cast<std::int32_t>(std::clamp(
        rounded,
        -scale,
        scale - 1));
}

void StoreInteger16(
    std::byte* destination,
    std::int32_t value) noexcept {
    const auto bits = static_cast<std::uint32_t>(value);
    destination[0] = static_cast<std::byte>(bits & 0xFFU);
    destination[1] = static_cast<std::byte>((bits >> 8U) & 0xFFU);
}

void StoreInteger24(
    std::byte* destination,
    std::int32_t value) noexcept {
    const auto bits = static_cast<std::uint32_t>(value);
    destination[0] = static_cast<std::byte>(bits & 0xFFU);
    destination[1] = static_cast<std::byte>((bits >> 8U) & 0xFFU);
    destination[2] = static_cast<std::byte>((bits >> 16U) & 0xFFU);
}

void StoreInteger32(
    std::byte* destination,
    std::int32_t value) noexcept {
    std::memcpy(destination, &value, sizeof(value));
}

void StoreFloat32(std::byte* destination, float value) noexcept {
    std::memcpy(destination, &value, sizeof(value));
}

void StoreFloat64(std::byte* destination, double value) noexcept {
    std::memcpy(destination, &value, sizeof(value));
}

bool RequiredBytes(
    std::size_t frames,
    std::size_t bytes_per_sample,
    std::size_t& required) noexcept {
    if (bytes_per_sample != 0 &&
        frames > std::numeric_limits<std::size_t>::max() /
            bytes_per_sample) {
        return false;
    }
    required = frames * bytes_per_sample;
    return true;
}

bool DestinationSpansOverlap(
    std::span<std::byte> left,
    std::span<std::byte> right) noexcept {
    if (left.empty() || right.empty()) {
        return false;
    }
    const auto left_begin =
        reinterpret_cast<std::uintptr_t>(left.data());
    const auto right_begin =
        reinterpret_cast<std::uintptr_t>(right.data());
    return left_begin <= right_begin
        ? right_begin - left_begin < left.size()
        : left_begin - right_begin < right.size();
}

void StoreSample(
    ASIOSampleType type,
    float input,
    std::byte* output) noexcept {
    switch (type) {
    case ASIOSTInt16LSB:
        StoreInteger16(output, QuantizeInteger(input, 16));
        break;
    case ASIOSTInt24LSB:
        StoreInteger24(output, QuantizeInteger(input, 24));
        break;
    case ASIOSTInt32LSB:
        StoreInteger32(output, QuantizeInteger(input, 32));
        break;
    case ASIOSTFloat32LSB:
        StoreFloat32(output, std::clamp(input, -1.0F, 1.0F));
        break;
    case ASIOSTFloat64LSB:
        StoreFloat64(
            output,
            std::clamp(static_cast<double>(input), -1.0, 1.0));
        break;
    case ASIOSTInt32LSB16:
        StoreInteger32(output, QuantizeInteger(input, 16));
        break;
    case ASIOSTInt32LSB18:
        StoreInteger32(output, QuantizeInteger(input, 18));
        break;
    case ASIOSTInt32LSB20:
        StoreInteger32(output, QuantizeInteger(input, 20));
        break;
    case ASIOSTInt32LSB24:
        StoreInteger32(output, QuantizeInteger(input, 24));
        break;
    default:
        break;
    }
}

} // namespace

std::optional<std::size_t> AsioBytesPerSample(
    ASIOSampleType type) noexcept {
    switch (type) {
    case ASIOSTInt16LSB:
        return 2;
    case ASIOSTInt24LSB:
        return 3;
    case ASIOSTInt32LSB:
    case ASIOSTFloat32LSB:
    case ASIOSTInt32LSB16:
    case ASIOSTInt32LSB18:
    case ASIOSTInt32LSB20:
    case ASIOSTInt32LSB24:
        return 4;
    case ASIOSTFloat64LSB:
        return 8;
    default:
        return std::nullopt;
    }
}

bool IsSupportedAsioOutputType(ASIOSampleType type) noexcept {
    return AsioBytesPerSample(type).has_value();
}

bool ClearAsioChannel(
    ASIOSampleType type,
    std::span<std::byte> destination,
    std::uint32_t frames) noexcept {
    const auto bytes_per_sample = AsioBytesPerSample(type);
    if (!bytes_per_sample) {
        return false;
    }
    std::size_t required{};
    if (!RequiredBytes(frames, *bytes_per_sample, required) ||
        destination.size() < required) {
        return false;
    }
    std::fill_n(destination.begin(), required, std::byte{0});
    return true;
}

bool ConvertFloatStereoChannelToAsio(
    std::span<const float> interleaved_stereo,
    std::uint32_t channel,
    ASIOSampleType type,
    std::span<std::byte> destination) noexcept {
    const auto bytes_per_sample = AsioBytesPerSample(type);
    if (!bytes_per_sample || channel > 1 ||
        interleaved_stereo.size() % 2 != 0) {
        return false;
    }

    const std::size_t frames = interleaved_stereo.size() / 2;
    std::size_t required{};
    if (!RequiredBytes(frames, *bytes_per_sample, required) ||
        destination.size() < required) {
        return false;
    }
    for (const float sample : interleaved_stereo) {
        if (!std::isfinite(sample)) {
            return false;
        }
    }

    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float input = interleaved_stereo[frame * 2 + channel];
        std::byte* output =
            destination.data() + frame * *bytes_per_sample;
        StoreSample(type, input, output);
    }
    return true;
}

AsioStereoConversionResult ConvertFloatStereoToAsio(
    std::span<const float> interleaved_stereo,
    const std::array<ASIOSampleType, 2>& types,
    const std::array<std::span<std::byte>, 2>& destinations) noexcept {
    std::array<std::size_t, 2> bytes_per_sample{};
    std::array<std::size_t, 2> required_bytes{};
    if (interleaved_stereo.size() % 2 != 0) {
        return {};
    }
    const auto frames = interleaved_stereo.size() / 2;
    for (std::size_t channel{}; channel < 2; ++channel) {
        const auto bytes = AsioBytesPerSample(types[channel]);
        if (!bytes ||
            !RequiredBytes(frames, *bytes, required_bytes[channel]) ||
            destinations[channel].size() < required_bytes[channel]) {
            return {};
        }
        bytes_per_sample[channel] = *bytes;
    }
    if (DestinationSpansOverlap(destinations[0], destinations[1])) {
        return {};
    }

    AsioStereoConversionStats stats{
        .all_zero = true,
    };
    for (const float sample : interleaved_stereo) {
        if (!std::isfinite(sample)) {
            return {
                .converted = false,
                .stats = {.non_finite = true},
            };
        }
        const float magnitude = std::abs(sample);
        stats.maximum_absolute_sample =
            (std::max)(stats.maximum_absolute_sample, magnitude);
        stats.all_zero = stats.all_zero && sample == 0.0F;
        if (magnitude > 1.0F) {
            ++stats.clipped_samples;
        }
    }

    for (std::size_t frame{}; frame < frames; ++frame) {
        for (std::size_t channel{}; channel < 2; ++channel) {
            StoreSample(
                types[channel],
                interleaved_stereo[frame * 2 + channel],
                destinations[channel].data() +
                    frame * bytes_per_sample[channel]);
        }
    }
    return {
        .converted = true,
        .stats = stats,
    };
}

} // namespace gc::audio

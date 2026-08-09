// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioSampleConverter.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace {

constexpr std::array<float, 6> kStereo{
    -1.25F,
    1.25F,
    -1.0F,
    1.0F,
    0.0F,
    0.5F,
};

constexpr std::byte kSentinel{0xA5};

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

int ExpectConversion(
    ASIOSampleType type,
    std::uint32_t channel,
    std::initializer_list<std::uint8_t> expected,
    std::string_view name) {
    std::vector<std::byte> destination(
        expected.size() + 2,
        kSentinel);
    const bool converted =
        gc::audio::ConvertFloatStereoChannelToAsio(
            kStereo,
            channel,
            type,
            destination);
    bool matches = converted;
    std::size_t index = 0;
    for (const auto byte : expected) {
        matches = matches &&
            destination[index] == static_cast<std::byte>(byte);
        ++index;
    }
    matches = matches &&
        destination[expected.size()] == kSentinel &&
        destination[expected.size() + 1] == kSentinel;
    return Expect(matches, name);
}

int ExpectPairedConversionMatchesLegacy(
    ASIOSampleType type,
    std::size_t bytes_per_sample,
    std::string_view name) {
    const auto required = bytes_per_sample * (kStereo.size() / 2);
    std::vector<std::byte> expected_left(required, kSentinel);
    std::vector<std::byte> expected_right(required, kSentinel);
    std::vector<std::byte> actual_left(required + 2, kSentinel);
    std::vector<std::byte> actual_right(required + 2, kSentinel);
    const bool expected_converted =
        gc::audio::ConvertFloatStereoChannelToAsio(
            kStereo,
            0,
            type,
            expected_left) &&
        gc::audio::ConvertFloatStereoChannelToAsio(
            kStereo,
            1,
            type,
            expected_right);
    const auto actual = gc::audio::ConvertFloatStereoToAsio(
        kStereo,
        {type, type},
        {
            std::span<std::byte>{actual_left},
            std::span<std::byte>{actual_right},
        });
    return Expect(
        expected_converted && actual.converted &&
            actual.stats.clipped_samples == 2 &&
            actual.stats.maximum_absolute_sample == 1.25F &&
            !actual.stats.all_zero && !actual.stats.non_finite &&
            std::equal(
                expected_left.begin(),
                expected_left.end(),
                actual_left.begin()) &&
            std::equal(
                expected_right.begin(),
                expected_right.end(),
                actual_right.begin()) &&
            actual_left[required] == kSentinel &&
            actual_left[required + 1] == kSentinel &&
            actual_right[required] == kSentinel &&
            actual_right[required + 1] == kSentinel,
        name);
}

template <typename InputSpan>
int ExpectPairedFailureWithoutWrites(
    InputSpan input,
    const std::array<ASIOSampleType, 2>& types,
    const std::array<std::span<std::byte>, 2>& destinations,
    std::span<const std::byte> complete_left,
    std::span<const std::byte> complete_right,
    bool expected_non_finite,
    std::string_view name) {
    const std::vector<std::byte> left_before(
        complete_left.begin(),
        complete_left.end());
    const std::vector<std::byte> right_before(
        complete_right.begin(),
        complete_right.end());
    const auto result = gc::audio::ConvertFloatStereoToAsio(
        input,
        types,
        destinations);
    return Expect(
        !result.converted &&
            result.stats.non_finite == expected_non_finite &&
            std::equal(
                left_before.begin(),
                left_before.end(),
                complete_left.begin()) &&
            std::equal(
                right_before.begin(),
                right_before.end(),
                complete_right.begin()),
        name);
}

template <typename InputSpan, typename OutputSpan>
int ExpectConversionFailureWithoutWrites(
    InputSpan input,
    std::uint32_t channel,
    ASIOSampleType type,
    OutputSpan output,
    std::span<const std::byte> complete_output,
    std::string_view name) {
    std::vector<std::byte> before(
        complete_output.begin(),
        complete_output.end());
    const bool converted =
        gc::audio::ConvertFloatStereoChannelToAsio(
            input,
            channel,
            type,
            output);
    return Expect(
        !converted && std::equal(
            before.begin(),
            before.end(),
            complete_output.begin(),
            complete_output.end()),
        name);
}

} // namespace

int main() {
    int failures = 0;

    struct SampleSizeCase {
        ASIOSampleType type;
        std::size_t bytes;
    };
    constexpr SampleSizeCase supported[] = {
        {ASIOSTInt16LSB, 2},
        {ASIOSTInt24LSB, 3},
        {ASIOSTInt32LSB, 4},
        {ASIOSTFloat32LSB, 4},
        {ASIOSTFloat64LSB, 8},
        {ASIOSTInt32LSB16, 4},
        {ASIOSTInt32LSB18, 4},
        {ASIOSTInt32LSB20, 4},
        {ASIOSTInt32LSB24, 4},
    };
    for (const auto& test : supported) {
        const auto bytes = gc::audio::AsioBytesPerSample(test.type);
        failures += Expect(
            bytes && *bytes == test.bytes &&
                gc::audio::IsSupportedAsioOutputType(test.type),
            "supported ASIO output reports its byte width");
        failures += ExpectPairedConversionMatchesLegacy(
            test.type,
            test.bytes,
            "paired conversion preserves exact channel bytes and integrity stats");
    }

    constexpr ASIOSampleType unsupported[] = {
        ASIOSTInt16MSB,
        ASIOSTInt24MSB,
        ASIOSTInt32MSB,
        ASIOSTFloat32MSB,
        ASIOSTFloat64MSB,
        ASIOSTInt32MSB16,
        ASIOSTDSDInt8LSB1,
        ASIOSTDSDInt8MSB1,
        ASIOSTDSDInt8NER8,
    };
    for (const auto type : unsupported) {
        failures += Expect(
            !gc::audio::AsioBytesPerSample(type) &&
                !gc::audio::IsSupportedAsioOutputType(type),
            "unsupported ASIO output has no byte width");
    }

    failures += ExpectConversion(
        ASIOSTInt16LSB,
        0,
        {0x00, 0x80, 0x00, 0x80, 0x00, 0x00},
        "Int16 left clips and deinterleaves");
    failures += ExpectConversion(
        ASIOSTInt16LSB,
        1,
        {0xFF, 0x7F, 0xFF, 0x7F, 0x00, 0x40},
        "Int16 right clips and deinterleaves");

    failures += ExpectConversion(
        ASIOSTInt24LSB,
        0,
        {
            0x00, 0x00, 0x80,
            0x00, 0x00, 0x80,
            0x00, 0x00, 0x00,
        },
        "packed Int24 left includes exact Xonar zero bytes");
    failures += ExpectConversion(
        ASIOSTInt24LSB,
        1,
        {
            0xFF, 0xFF, 0x7F,
            0xFF, 0xFF, 0x7F,
            0x00, 0x00, 0x40,
        },
        "packed Int24 right clips and scales");

    failures += ExpectConversion(
        ASIOSTInt32LSB,
        0,
        {
            0x00, 0x00, 0x00, 0x80,
            0x00, 0x00, 0x00, 0x80,
            0x00, 0x00, 0x00, 0x00,
        },
        "Int32 left uses full-scale 32-bit samples");
    failures += ExpectConversion(
        ASIOSTInt32LSB,
        1,
        {
            0xFF, 0xFF, 0xFF, 0x7F,
            0xFF, 0xFF, 0xFF, 0x7F,
            0x00, 0x00, 0x00, 0x40,
        },
        "Int32 right uses full-scale 32-bit samples");

    failures += ExpectConversion(
        ASIOSTFloat32LSB,
        0,
        {
            0x00, 0x00, 0x80, 0xBF,
            0x00, 0x00, 0x80, 0xBF,
            0x00, 0x00, 0x00, 0x00,
        },
        "Float32 left clips to IEEE little-endian");
    failures += ExpectConversion(
        ASIOSTFloat32LSB,
        1,
        {
            0x00, 0x00, 0x80, 0x3F,
            0x00, 0x00, 0x80, 0x3F,
            0x00, 0x00, 0x00, 0x3F,
        },
        "Float32 right clips to IEEE little-endian");

    failures += ExpectConversion(
        ASIOSTFloat64LSB,
        0,
        {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0xBF,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0xBF,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        },
        "Float64 left clips to IEEE little-endian");
    failures += ExpectConversion(
        ASIOSTFloat64LSB,
        1,
        {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x3F,
        },
        "Float64 right clips to IEEE little-endian");

    failures += ExpectConversion(
        ASIOSTInt32LSB16,
        0,
        {
            0x00, 0x80, 0xFF, 0xFF,
            0x00, 0x80, 0xFF, 0xFF,
            0x00, 0x00, 0x00, 0x00,
        },
        "Int32LSB16 right-aligns and sign-extends negatives");
    failures += ExpectConversion(
        ASIOSTInt32LSB16,
        1,
        {
            0xFF, 0x7F, 0x00, 0x00,
            0xFF, 0x7F, 0x00, 0x00,
            0x00, 0x40, 0x00, 0x00,
        },
        "Int32LSB16 right-aligns positives");
    failures += ExpectConversion(
        ASIOSTInt32LSB18,
        0,
        {
            0x00, 0x00, 0xFE, 0xFF,
            0x00, 0x00, 0xFE, 0xFF,
            0x00, 0x00, 0x00, 0x00,
        },
        "Int32LSB18 sign-extends negatives");
    failures += ExpectConversion(
        ASIOSTInt32LSB18,
        1,
        {
            0xFF, 0xFF, 0x01, 0x00,
            0xFF, 0xFF, 0x01, 0x00,
            0x00, 0x00, 0x01, 0x00,
        },
        "Int32LSB18 right-aligns positives");
    failures += ExpectConversion(
        ASIOSTInt32LSB20,
        0,
        {
            0x00, 0x00, 0xF8, 0xFF,
            0x00, 0x00, 0xF8, 0xFF,
            0x00, 0x00, 0x00, 0x00,
        },
        "Int32LSB20 sign-extends negatives");
    failures += ExpectConversion(
        ASIOSTInt32LSB20,
        1,
        {
            0xFF, 0xFF, 0x07, 0x00,
            0xFF, 0xFF, 0x07, 0x00,
            0x00, 0x00, 0x04, 0x00,
        },
        "Int32LSB20 right-aligns positives");
    failures += ExpectConversion(
        ASIOSTInt32LSB24,
        0,
        {
            0x00, 0x00, 0x80, 0xFF,
            0x00, 0x00, 0x80, 0xFF,
            0x00, 0x00, 0x00, 0x00,
        },
        "Int32LSB24 sign-extends negatives");
    failures += ExpectConversion(
        ASIOSTInt32LSB24,
        1,
        {
            0xFF, 0xFF, 0x7F, 0x00,
            0xFF, 0xFF, 0x7F, 0x00,
            0x00, 0x00, 0x40, 0x00,
        },
        "Int32LSB24 right-aligns positives");

    std::array<std::byte, 32> failure_output;
    failure_output.fill(kSentinel);
    failures += ExpectConversionFailureWithoutWrites(
        std::span<const float>{kStereo},
        0,
        ASIOSTInt16LSB,
        std::span<std::byte>{failure_output}.first(5),
        failure_output,
        "undersized output fails before writing");
    failures += ExpectConversionFailureWithoutWrites(
        std::span<const float>{kStereo}.first(5),
        0,
        ASIOSTInt16LSB,
        std::span<std::byte>{failure_output},
        failure_output,
        "odd interleaved input fails before writing");
    failures += ExpectConversionFailureWithoutWrites(
        std::span<const float>{kStereo},
        2,
        ASIOSTInt16LSB,
        std::span<std::byte>{failure_output},
        failure_output,
        "channel above one fails before writing");
    failures += ExpectConversionFailureWithoutWrites(
        std::span<const float>{kStereo},
        0,
        ASIOSTInt16MSB,
        std::span<std::byte>{failure_output},
        failure_output,
        "MSB output fails before writing");
    failures += ExpectConversionFailureWithoutWrites(
        std::span<const float>{kStereo},
        0,
        ASIOSTDSDInt8LSB1,
        std::span<std::byte>{failure_output},
        failure_output,
        "DSD output fails before writing");

    auto non_finite = kStereo;
    non_finite.back() = std::numeric_limits<float>::quiet_NaN();
    failures += ExpectConversionFailureWithoutWrites(
        std::span<const float>{non_finite},
        0,
        ASIOSTInt24LSB,
        std::span<std::byte>{failure_output},
        failure_output,
        "non-finite unselected input is rejected before writing");
    non_finite.back() = std::numeric_limits<float>::infinity();
    failures += ExpectConversionFailureWithoutWrites(
        std::span<const float>{non_finite},
        1,
        ASIOSTFloat64LSB,
        std::span<std::byte>{failure_output},
        failure_output,
        "infinite input is rejected before writing");

    constexpr std::array<float, 8> zero_stereo{};
    std::array<std::byte, 8> zero_left;
    std::array<std::byte, 8> zero_right;
    zero_left.fill(kSentinel);
    zero_right.fill(kSentinel);
    const auto zero_result = gc::audio::ConvertFloatStereoToAsio(
        zero_stereo,
        {ASIOSTInt16LSB, ASIOSTInt16LSB},
        {
            std::span<std::byte>{zero_left},
            std::span<std::byte>{zero_right},
        });
    failures += Expect(
        zero_result.converted && zero_result.stats.all_zero &&
            !zero_result.stats.non_finite &&
            zero_result.stats.clipped_samples == 0 &&
            zero_result.stats.maximum_absolute_sample == 0.0F &&
            std::all_of(
                zero_left.begin(),
                zero_left.end(),
                [](std::byte value) { return value == std::byte{0}; }) &&
            std::all_of(
                zero_right.begin(),
                zero_right.end(),
                [](std::byte value) { return value == std::byte{0}; }),
        "paired zero block reports exact silence");

    for (const auto non_finite_value : {
             std::numeric_limits<float>::quiet_NaN(),
             std::numeric_limits<float>::infinity()}) {
        for (const auto channel : {0U, 1U}) {
            auto invalid_stereo = kStereo;
            invalid_stereo[channel] = non_finite_value;
            std::array<std::byte, 24> left;
            std::array<std::byte, 24> right;
            left.fill(kSentinel);
            right.fill(kSentinel);
            failures += ExpectPairedFailureWithoutWrites(
                std::span<const float>{invalid_stereo},
                {ASIOSTInt24LSB, ASIOSTFloat64LSB},
                {
                    std::span<std::byte>{left},
                    std::span<std::byte>{right},
                },
                left,
                right,
                true,
                "non-finite sample in either channel rejects both destinations");
        }
    }

    std::array<std::byte, 32> pair_left;
    std::array<std::byte, 32> pair_right;
    pair_left.fill(kSentinel);
    pair_right.fill(kSentinel);
    failures += ExpectPairedFailureWithoutWrites(
        std::span<const float>{kStereo},
        {ASIOSTInt16MSB, ASIOSTInt16LSB},
        {
            std::span<std::byte>{pair_left},
            std::span<std::byte>{pair_right},
        },
        pair_left,
        pair_right,
        false,
        "invalid paired type fails before writing either channel");
    failures += ExpectPairedFailureWithoutWrites(
        std::span<const float>{kStereo}.first(5),
        {ASIOSTInt16LSB, ASIOSTInt16LSB},
        {
            std::span<std::byte>{pair_left},
            std::span<std::byte>{pair_right},
        },
        pair_left,
        pair_right,
        false,
        "odd paired input fails before writing either channel");
    failures += ExpectPairedFailureWithoutWrites(
        std::span<const float>{kStereo},
        {ASIOSTInt16LSB, ASIOSTInt16LSB},
        {
            std::span<std::byte>{pair_left}.first(5),
            std::span<std::byte>{pair_right},
        },
        pair_left,
        pair_right,
        false,
        "undersized paired destination fails before writing either channel");

    std::array<std::byte, 20> overlapping;
    overlapping.fill(kSentinel);
    failures += ExpectPairedFailureWithoutWrites(
        std::span<const float>{kStereo},
        {ASIOSTInt16LSB, ASIOSTInt16LSB},
        {
            std::span<std::byte>{overlapping}.subspan(0, 8),
            std::span<std::byte>{overlapping}.subspan(4, 8),
        },
        overlapping,
        std::span<const std::byte>{},
        false,
        "overlapping paired destinations fail before any write");

    std::array<std::byte, 11> clear_output;
    clear_output.fill(kSentinel);
    failures += Expect(
        gc::audio::ClearAsioChannel(
            ASIOSTInt24LSB,
            clear_output,
            3) &&
            std::all_of(
                clear_output.begin(),
                clear_output.begin() + 9,
                [](std::byte value) { return value == std::byte{0}; }) &&
            clear_output[9] == kSentinel &&
            clear_output[10] == kSentinel,
        "channel clear zeros exactly frames times sample width");

    std::array<std::byte, 5> short_clear;
    short_clear.fill(kSentinel);
    failures += Expect(
        !gc::audio::ClearAsioChannel(
            ASIOSTInt16LSB,
            short_clear,
            3) &&
            std::all_of(
                short_clear.begin(),
                short_clear.end(),
                [](std::byte value) { return value == kSentinel; }),
        "undersized channel clear fails without writing");
    failures += Expect(
        !gc::audio::ClearAsioChannel(
            ASIOSTInt16MSB,
            short_clear,
            2) &&
            std::all_of(
                short_clear.begin(),
                short_clear.end(),
                [](std::byte value) { return value == kSentinel; }),
        "unsupported channel clear fails without writing");

    return failures == 0 ? 0 : 1;
}

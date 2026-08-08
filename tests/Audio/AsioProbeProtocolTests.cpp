// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioProbeProtocol.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using gc::audio::AsioCapabilityReport;
using gc::audio::AsioChannelDescriptor;
using gc::audio::AsioFailure;
using gc::audio::AsioFailureStage;
using gc::audio::AsioProbeMode;
using gc::audio::AsioProbeProtocolError;
using gc::audio::AsioProbeRequest;
using gc::audio::AsioProbeResult;
using gc::audio::AsioResultDomain;
using gc::audio::DecodeAsioProbeRequest;
using gc::audio::DecodeAsioProbeResult;
using gc::audio::EncodeAsioProbeRequest;
using gc::audio::EncodeAsioProbeResult;
using gc::audio::kAsioProbeMaxChannels;
using gc::audio::kAsioProbeMaxDriverNameBytes;
using gc::audio::kAsioProbeMaxPayloadBytes;
using gc::audio::kAsioProbeProtocolVersion;

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

template <typename Value>
bool HasProtocolError(
    const std::expected<Value, AsioProbeProtocolError>& result,
    AsioProbeProtocolError error) {
    return !result.has_value() && result.error() == error;
}

void WriteU16(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void WriteU32(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::byte>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

std::uint32_t ReadU32(
    std::span<const std::byte> bytes,
    std::size_t offset) {
    std::uint32_t value{};
    for (std::size_t index = 0; index < 4; ++index) {
        value |= std::to_integer<std::uint32_t>(bytes[offset + index])
            << (index * 8U);
    }
    return value;
}

AsioCapabilityReport SampleReport() {
    AsioCapabilityReport report;
    report.registration.registry_name = "XONAR SOUND CARD 音频";
    report.registration.clsid = {
        0x12345678,
        0x1234,
        0xABCD,
        {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF},
    };
    report.reported_driver_name = "Xonar ASIO 驱动";
    report.driver_version = 42;
    report.original_sample_rate = 44'100.0;
    report.sample_rate = 48'000.0;
    report.buffer_limits = {192, 2400, 192, -1};
    report.input_channels = 0;
    report.output_channels = {
        AsioChannelDescriptor{0, "Front 左", ASIOSTInt24LSB},
        AsioChannelDescriptor{1, "Front 右", ASIOSTInt24LSB},
    };
    report.selected_base_channel = 0;
    report.effective_buffer_frames = 192;
    report.input_latency_frames = 0;
    report.output_latency_frames = 384;
    report.output_ready_supported = true;
    report.overload_reporting_supported = false;
    return report;
}

bool EqualGuid(const CLSID& left, const CLSID& right) {
    return InlineIsEqualGUID(left, right) != FALSE;
}

bool EqualReport(
    const AsioCapabilityReport& left,
    const AsioCapabilityReport& right) {
    if (left.registration.registry_name !=
            right.registration.registry_name ||
        !EqualGuid(left.registration.clsid, right.registration.clsid) ||
        left.reported_driver_name != right.reported_driver_name ||
        left.driver_version != right.driver_version ||
        left.original_sample_rate != right.original_sample_rate ||
        left.sample_rate != right.sample_rate ||
        left.buffer_limits.minimum != right.buffer_limits.minimum ||
        left.buffer_limits.maximum != right.buffer_limits.maximum ||
        left.buffer_limits.preferred != right.buffer_limits.preferred ||
        left.buffer_limits.granularity != right.buffer_limits.granularity ||
        left.input_channels != right.input_channels ||
        left.output_channels.size() != right.output_channels.size() ||
        left.selected_base_channel != right.selected_base_channel ||
        left.effective_buffer_frames != right.effective_buffer_frames ||
        left.input_latency_frames != right.input_latency_frames ||
        left.output_latency_frames != right.output_latency_frames ||
        left.output_ready_supported != right.output_ready_supported ||
        left.overload_reporting_supported !=
            right.overload_reporting_supported) {
        return false;
    }
    for (std::size_t index = 0;
         index < left.output_channels.size();
         ++index) {
        const auto& a = left.output_channels[index];
        const auto& b = right.output_channels[index];
        if (a.index != b.index || a.name != b.name || a.sample_type != b.sample_type) {
            return false;
        }
    }
    return true;
}

int TestRequestRoundTrip() {
    const AsioProbeRequest request{
        AsioProbeMode::validate,
        "任意 ASIO 驱动",
        192,
        6,
    };
    const auto encoded = EncodeAsioProbeRequest(request);
    int failures = Expect(encoded.has_value(), "Unicode request encodes");
    if (!encoded.has_value()) {
        return failures;
    }
    const auto decoded = DecodeAsioProbeRequest(*encoded);
    failures += Expect(
        decoded.has_value() &&
            decoded->mode == request.mode &&
            decoded->driver_name == request.driver_name &&
            decoded->buffer_frames == request.buffer_frames &&
            decoded->output_base_channel == request.output_base_channel,
        "request round-trips every field");
    return failures;
}

int TestCapabilityAndFailureRoundTrips() {
    const auto report = SampleReport();
    const AsioProbeResult success{report};
    const auto encoded_success = EncodeAsioProbeResult(success);
    int failures = Expect(
        encoded_success.has_value(),
        "capability result encodes");
    if (encoded_success.has_value()) {
        const auto decoded = DecodeAsioProbeResult(*encoded_success);
        failures += Expect(
            decoded.has_value() && decoded->has_value() &&
                EqualReport(**decoded, report),
            "capability result round-trips all identity, buffer and channel fields");
    }

    const AsioFailure failure{
        .stage = AsioFailureStage::probe_crash,
        .domain = AsioResultDomain::win32,
        .result = -2'147'483'648LL,
        .driver_message = "驱动 failed",
        .detail = "helper 失败",
    };
    const AsioProbeResult negative{std::unexpected(failure)};
    const auto encoded_failure = EncodeAsioProbeResult(negative);
    failures += Expect(
        encoded_failure.has_value(),
        "structured failure encodes");
    if (encoded_failure.has_value()) {
        const auto decoded = DecodeAsioProbeResult(*encoded_failure);
        failures += Expect(
            decoded.has_value() && !decoded->has_value() &&
                decoded->error().stage == failure.stage &&
                decoded->error().domain == failure.domain &&
                decoded->error().result == failure.result &&
                decoded->error().driver_message == failure.driver_message &&
                decoded->error().detail == failure.detail,
            "structured failure round-trips without becoming a capability");
    }
    return failures;
}

int TestEnvelopeValidation() {
    const auto valid = EncodeAsioProbeRequest({
        AsioProbeMode::inspect,
        "FlexASIO",
        0,
        0,
    });
    if (!valid.has_value()) {
        return 1;
    }
    int failures{};

    auto wrong_magic = *valid;
    WriteU32(wrong_magic, 0, 0xDEADBEEFU);
    failures += Expect(
        HasProtocolError(
            DecodeAsioProbeRequest(wrong_magic),
            AsioProbeProtocolError::wrong_magic),
        "wrong magic is rejected");

    auto wrong_version = *valid;
    WriteU16(
        wrong_version,
        4,
        static_cast<std::uint16_t>(kAsioProbeProtocolVersion + 1));
    failures += Expect(
        HasProtocolError(
            DecodeAsioProbeRequest(wrong_version),
            AsioProbeProtocolError::wrong_version),
        "wrong protocol version is rejected");

    auto wrong_kind = *valid;
    WriteU16(wrong_kind, 6, 99);
    failures += Expect(
        HasProtocolError(
            DecodeAsioProbeRequest(wrong_kind),
            AsioProbeProtocolError::wrong_kind),
        "wrong message kind is rejected");

    auto oversized_payload = *valid;
    WriteU32(oversized_payload, 8, kAsioProbeMaxPayloadBytes + 1);
    failures += Expect(
        HasProtocolError(
            DecodeAsioProbeRequest(oversized_payload),
            AsioProbeProtocolError::limit_exceeded),
        "oversized envelope is rejected before payload access");

    auto trailing = *valid;
    trailing.push_back(std::byte{0});
    failures += Expect(
        HasProtocolError(
            DecodeAsioProbeRequest(trailing),
            AsioProbeProtocolError::trailing_data),
        "bytes beyond framed payload are rejected");

    for (std::size_t size = 0; size < valid->size(); ++size) {
        const auto decoded = DecodeAsioProbeRequest(
            std::span<const std::byte>{valid->data(), size});
        if (decoded.has_value()) {
            failures += Expect(false, "every truncated prefix is rejected");
            break;
        }
    }
    return failures;
}

int TestRequestValueValidation() {
    const auto valid = EncodeAsioProbeRequest({
        AsioProbeMode::inspect,
        "FlexASIO",
        0,
        0,
    });
    if (!valid.has_value()) {
        return 1;
    }
    int failures{};

    auto unknown_mode = *valid;
    WriteU32(unknown_mode, 12, 99);
    failures += Expect(
        HasProtocolError(
            DecodeAsioProbeRequest(unknown_mode),
            AsioProbeProtocolError::unknown_enum),
        "unknown probe mode is rejected");

    auto oversized_name = *valid;
    WriteU32(oversized_name, 24, kAsioProbeMaxDriverNameBytes + 1);
    failures += Expect(
        HasProtocolError(
            DecodeAsioProbeRequest(oversized_name),
            AsioProbeProtocolError::limit_exceeded),
        "oversized driver-name length is rejected before allocation");

    auto invalid_utf8 = *valid;
    invalid_utf8[28] = std::byte{0xFF};
    failures += Expect(
        HasProtocolError(
            DecodeAsioProbeRequest(invalid_utf8),
            AsioProbeProtocolError::invalid_utf8),
        "invalid UTF-8 driver name is rejected");

    AsioProbeRequest too_long{
        AsioProbeMode::inspect,
        std::string(kAsioProbeMaxDriverNameBytes + 1, 'A'),
        0,
        0,
    };
    failures += Expect(
        HasProtocolError(
            EncodeAsioProbeRequest(too_long),
            AsioProbeProtocolError::limit_exceeded),
        "encoder enforces driver-name bound");

    AsioProbeRequest malformed{
        AsioProbeMode::inspect,
        std::string{"\xF0\x28\x8C\x28", 4},
        0,
        0,
    };
    failures += Expect(
        HasProtocolError(
            EncodeAsioProbeRequest(malformed),
            AsioProbeProtocolError::invalid_utf8),
        "encoder enforces UTF-8 validity");
    return failures;
}

struct ReportOffsets {
    std::size_t input_channels{};
    std::size_t channel_count{};
    std::size_t first_channel_name_length{};
    std::size_t first_channel_name{};
    std::size_t first_channel_type{};
};

ReportOffsets LocateReportFields(
    std::span<const std::byte> bytes) {
    std::size_t cursor = 12;
    const auto registration_size = ReadU32(bytes, cursor);
    cursor += 4 + registration_size + 16;
    const auto reported_size = ReadU32(bytes, cursor);
    cursor += 4 + reported_size;
    cursor += 4 + 8 + 8 + 16 + 4;
    const auto input_channels = cursor - 4;
    const auto channel_count = cursor;
    cursor += 4;
    cursor += 4;
    const auto name_length = cursor;
    const auto name_size = ReadU32(bytes, cursor);
    cursor += 4;
    const auto name = cursor;
    cursor += name_size;
    return {input_channels, channel_count, name_length, name, cursor};
}

int TestCapabilityBoundsAndEnums() {
    const auto encoded = EncodeAsioProbeResult(AsioProbeResult{SampleReport()});
    if (!encoded.has_value()) {
        return 1;
    }
    const auto offsets = LocateReportFields(*encoded);
    int failures{};

    auto too_many_inputs = *encoded;
    WriteU32(
        too_many_inputs,
        offsets.input_channels,
        kAsioProbeMaxChannels + 1);
    const auto decoded_too_many_inputs =
        DecodeAsioProbeResult(too_many_inputs);
    failures += Expect(
        !decoded_too_many_inputs.has_value() &&
            decoded_too_many_inputs.error() ==
                AsioProbeProtocolError::limit_exceeded,
        "oversized input-channel count is rejected");

    auto too_many_channels = *encoded;
    WriteU32(
        too_many_channels,
        offsets.channel_count,
        kAsioProbeMaxChannels + 1);
    failures += Expect(
        HasProtocolError(
            DecodeAsioProbeResult(too_many_channels),
            AsioProbeProtocolError::limit_exceeded),
        "oversized channel count is rejected before reserve");

    auto long_channel_name = *encoded;
    WriteU32(
        long_channel_name,
        offsets.first_channel_name_length,
        gc::audio::kAsioProbeMaxChannelNameBytes + 1);
    failures += Expect(
        HasProtocolError(
            DecodeAsioProbeResult(long_channel_name),
            AsioProbeProtocolError::limit_exceeded),
        "oversized channel name is rejected");

    auto invalid_channel_utf8 = *encoded;
    invalid_channel_utf8[offsets.first_channel_name] = std::byte{0xFF};
    failures += Expect(
        HasProtocolError(
            DecodeAsioProbeResult(invalid_channel_utf8),
            AsioProbeProtocolError::invalid_utf8),
        "invalid UTF-8 channel name is rejected");

    auto unknown_sample_type = *encoded;
    WriteU32(unknown_sample_type, offsets.first_channel_type, 999);
    failures += Expect(
        HasProtocolError(
            DecodeAsioProbeResult(unknown_sample_type),
            AsioProbeProtocolError::unknown_enum),
        "unknown sample format is rejected");

    AsioProbeResult invalid_stage{std::unexpected(AsioFailure{
        .stage = AsioFailureStage::none,
        .domain = AsioResultDomain::none,
    })};
    auto failure_bytes = EncodeAsioProbeResult(invalid_stage);
    failures += Expect(
        failure_bytes.has_value(),
        "baseline failure result encodes");
    if (failure_bytes.has_value()) {
        WriteU32(*failure_bytes, 12, 999);
        failures += Expect(
            HasProtocolError(
                DecodeAsioProbeResult(*failure_bytes),
                AsioProbeProtocolError::unknown_enum),
            "unknown failure stage is rejected");
        WriteU32(
            *failure_bytes,
            12,
            static_cast<std::uint32_t>(AsioFailureStage::none));
        WriteU32(*failure_bytes, 16, 999);
        failures += Expect(
            HasProtocolError(
                DecodeAsioProbeResult(*failure_bytes),
                AsioProbeProtocolError::unknown_enum),
            "unknown failure domain is rejected");
    }

    AsioCapabilityReport too_many = SampleReport();
    too_many.output_channels.resize(kAsioProbeMaxChannels + 1);
    failures += Expect(
        HasProtocolError(
            EncodeAsioProbeResult(AsioProbeResult{std::move(too_many)}),
            AsioProbeProtocolError::limit_exceeded),
        "encoder enforces channel-count bound");
    return failures;
}

int TestResultFramingRejectsTruncationAndWrongKind() {
    const auto encoded = EncodeAsioProbeResult(AsioProbeResult{SampleReport()});
    if (!encoded.has_value()) {
        return 1;
    }
    int failures{};
    for (std::size_t size = 0; size < encoded->size(); ++size) {
        const auto decoded = DecodeAsioProbeResult(
            std::span<const std::byte>{encoded->data(), size});
        if (decoded.has_value()) {
            failures += Expect(false, "every result truncation is rejected");
            break;
        }
    }

    auto request = EncodeAsioProbeRequest({
        AsioProbeMode::inspect,
        "FlexASIO",
        0,
        0,
    });
    failures += Expect(
        request.has_value() && HasProtocolError(
            DecodeAsioProbeResult(*request),
            AsioProbeProtocolError::wrong_kind),
        "request cannot be decoded as result");

    auto trailing = *encoded;
    trailing.push_back(std::byte{0x7F});
    failures += Expect(
        HasProtocolError(
            DecodeAsioProbeResult(trailing),
            AsioProbeProtocolError::trailing_data),
        "result trailing data is rejected");
    return failures;
}

int TestDeterministicFuzzInputsStayBounded() {
    std::uint32_t state = 0xC0FFEEU;
    auto next = [&]() noexcept {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        return state;
    };

    for (std::size_t iteration = 0; iteration < 4'096; ++iteration) {
        const auto size = static_cast<std::size_t>(next() % 257U);
        std::vector<std::byte> bytes(size);
        for (auto& byte : bytes) {
            byte = static_cast<std::byte>(next() & 0xFFU);
        }
        (void)DecodeAsioProbeRequest(bytes);
        (void)DecodeAsioProbeResult(bytes);
    }
    return 0;
}

} // namespace

int main() {
    int failures{};
    failures += TestRequestRoundTrip();
    failures += TestCapabilityAndFailureRoundTrips();
    failures += TestEnvelopeValidation();
    failures += TestRequestValueValidation();
    failures += TestCapabilityBoundsAndEnums();
    failures += TestResultFramingRejectsTruncationAndWrongKind();
    failures += TestDeterministicFuzzInputsStayBounded();
    return failures == 0 ? 0 : 1;
}

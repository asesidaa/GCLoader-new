#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioControlPanel.h"
#include "Audio/Asio/AsioTypes.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace gc::audio
{
    inline constexpr std::uint32_t kAsioProbeMagic = 0x4F495341;
    inline constexpr std::uint16_t kAsioProbeProtocolVersion = 2;
    inline constexpr std::uint32_t kAsioProbeMaxPayloadBytes = 64 * 1024;
    inline constexpr std::uint32_t kAsioProbeMaxDriverNameBytes = 1024;
    inline constexpr std::uint32_t kAsioProbeMaxChannelNameBytes = 32;
    inline constexpr std::uint32_t kAsioProbeMaxChannels =
        kMaxAsioReportedChannels;
    inline constexpr std::uint32_t kAsioProbeMaxFailureTextBytes = 4096;
    inline constexpr std::uint32_t kAsioProbeEnvelopeBytes = 12;
    inline constexpr std::uint32_t kAsioProbeMaxMessageBytes =
        kAsioProbeEnvelopeBytes + kAsioProbeMaxPayloadBytes;

    enum class AsioProbeProtocolError : std::uint8_t
    {
        wrong_magic,
        wrong_version,
        wrong_kind,
        truncated,
        trailing_data,
        invalid_utf8,
        limit_exceeded,
        integer_overflow,
        unknown_enum,
        invalid_value,
        allocation_failure,
    };

    struct AsioProbeRequest
    {
        AsioProbeMode mode{AsioProbeMode::inspect};
        std::string driver_name;
        std::uint32_t buffer_frames{};
        std::uint32_t output_base_channel{};
    };

    using AsioProbeResult =
    std::expected<AsioCapabilityReport, AsioFailure>;
    using AsioControlPanelResult = std::expected<void, AsioFailure>;

    [[nodiscard]] std::expected<
        std::vector<std::byte>,
        AsioProbeProtocolError>
    EncodeAsioProbeRequest(const AsioProbeRequest& request) noexcept;

    [[nodiscard]] std::expected<
        AsioProbeRequest,
        AsioProbeProtocolError>
    DecodeAsioProbeRequest(std::span<const std::byte> bytes) noexcept;

    [[nodiscard]] std::expected<
        std::vector<std::byte>,
        AsioProbeProtocolError>
    EncodeAsioProbeResult(const AsioProbeResult& result) noexcept;

    [[nodiscard]] std::expected<
        AsioProbeResult,
        AsioProbeProtocolError>
    DecodeAsioProbeResult(std::span<const std::byte> bytes) noexcept;

    [[nodiscard]] std::expected<
        std::vector<std::byte>,
        AsioProbeProtocolError>
    EncodeAsioControlPanelRequest(
        const AsioControlPanelRequest& request) noexcept;

    [[nodiscard]] std::expected<
        AsioControlPanelRequest,
        AsioProbeProtocolError>
    DecodeAsioControlPanelRequest(
        std::span<const std::byte> bytes) noexcept;

    [[nodiscard]] std::expected<
        std::vector<std::byte>,
        AsioProbeProtocolError>
    EncodeAsioControlPanelResult(
        const AsioControlPanelResult& result) noexcept;

    [[nodiscard]] std::expected<
        AsioControlPanelResult,
        AsioProbeProtocolError>
    DecodeAsioControlPanelResult(
        std::span<const std::byte> bytes) noexcept;
} // namespace gc::audio

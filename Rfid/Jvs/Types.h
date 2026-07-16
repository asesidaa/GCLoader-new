#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>

namespace gc::rfid::jvs {

inline constexpr std::size_t kMaxDecodedAfterCount = 255;
inline constexpr std::size_t kMaxPayloadSize = 254;
inline constexpr std::size_t kMaxEncodedFrameSize = 515;
inline constexpr std::byte kSync{0xE0};
inline constexpr std::byte kMarker{0xD0};

struct Address {
    std::uint8_t value{};

    constexpr auto operator<=>(const Address&) const = default;

    [[nodiscard]] constexpr bool is_master() const noexcept
    {
        return value == 0x00;
    }

    [[nodiscard]] constexpr bool is_standard_slave() const noexcept
    {
        return value >= 0x01 && value <= 0x1F;
    }

    [[nodiscard]] constexpr bool is_broadcast() const noexcept
    {
        return value == 0xFF;
    }
};

struct CommandId {
    std::uint8_t value{};
    constexpr auto operator<=>(const CommandId&) const = default;
};

namespace address {
inline constexpr Address master{0x00};
inline constexpr Address broadcast{0xFF};
} // namespace address

namespace command {
inline constexpr CommandId reset{0xF0};
inline constexpr CommandId set_address{0xF1};
inline constexpr CommandId read_id{0x10};
inline constexpr CommandId command_format_revision{0x11};
inline constexpr CommandId jvs_revision{0x12};
inline constexpr CommandId communication_revision{0x13};
inline constexpr CommandId capabilities{0x14};
inline constexpr CommandId read_switches{0x20};
inline constexpr CommandId read_coins{0x21};
inline constexpr CommandId read_general_input{0x26};
inline constexpr CommandId retransmit{0x2F};
inline constexpr CommandId decrease_coins{0x30};
inline constexpr CommandId increase_coins{0x31};
inline constexpr CommandId write_general_output{0x32};
} // namespace command

struct Status {
    std::uint8_t value{};
    constexpr auto operator<=>(const Status&) const = default;
};

struct Report {
    std::uint8_t value{};
    constexpr auto operator<=>(const Report&) const = default;
};

namespace status {
inline constexpr Status ok{0x01};
inline constexpr Status unknown_command{0x02};
inline constexpr Status checksum_error{0x03};
inline constexpr Status acknowledgement_overflow{0x04};
} // namespace status

namespace report {
inline constexpr Report ok{0x01};
inline constexpr Report invalid_input_parameter{0x02};
inline constexpr Report invalid_output_parameter{0x03};
inline constexpr Report busy{0x04};
} // namespace report

struct DecodedPacket {
    Address address{};
    std::uint8_t byte_count{};
    std::array<std::uint8_t, kMaxDecodedAfterCount> after_count{};

    [[nodiscard]] std::span<const std::uint8_t> payload() const noexcept
    {
        const auto size = byte_count == 0
            ? std::size_t{0}
            : static_cast<std::size_t>(byte_count - 1);
        return {after_count.data(), size};
    }

    [[nodiscard]] std::optional<std::uint8_t> checksum() const noexcept
    {
        if (byte_count == 0) {
            return std::nullopt;
        }
        return after_count[byte_count - 1];
    }
};

struct ChecksumFailure {
    Address address{};
    std::uint8_t byte_count{};
    std::uint8_t expected{};
    std::uint8_t actual{};
};

enum class FramingError {
    InvalidEscape,
    ZeroByteCount,
};

using DecodeEvent =
    std::variant<DecodedPacket, ChecksumFailure, FramingError>;

struct EncodedFrame {
    std::array<std::byte, kMaxEncodedFrameSize> storage{};
    std::uint16_t size{};

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept
    {
        return {storage.data(), size};
    }
};

} // namespace gc::rfid::jvs

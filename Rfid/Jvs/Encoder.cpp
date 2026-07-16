#include "Rfid/Jvs/Encoder.h"

#include <cstddef>
#include <cstdint>

namespace gc::rfid::jvs {
namespace {

bool AppendRaw(EncodedFrame& frame, std::byte value) noexcept
{
    if (frame.size >= frame.storage.size()) {
        return false;
    }
    frame.storage[frame.size++] = value;
    return true;
}

bool AppendEscaped(EncodedFrame& frame, std::uint8_t value) noexcept
{
    if (value == 0xD0) {
        return AppendRaw(frame, std::byte{0xD0}) &&
               AppendRaw(frame, std::byte{0xCF});
    }
    if (value == 0xE0) {
        return AppendRaw(frame, std::byte{0xD0}) &&
               AppendRaw(frame, std::byte{0xDF});
    }
    return AppendRaw(frame, static_cast<std::byte>(value));
}

} // namespace

std::expected<EncodedFrame, EncodeError> EncodePacket(
    Address address_value,
    std::span<const std::uint8_t> payload) noexcept
{
    if (payload.size() > kMaxPayloadSize) {
        return std::unexpected(EncodeError::PayloadTooLarge);
    }

    EncodedFrame frame;
    if (!AppendRaw(frame, kSync)) {
        return std::unexpected(EncodeError::CapacityInvariant);
    }

    const auto byte_count = static_cast<std::uint8_t>(payload.size() + 1);
    std::uint32_t checksum = address_value.value + byte_count;
    for (const auto value : payload) {
        checksum += value;
    }
    const auto checksum_byte = static_cast<std::uint8_t>(checksum & 0xFFu);

    if (!AppendEscaped(frame, address_value.value) ||
        !AppendEscaped(frame, byte_count)) {
        return std::unexpected(EncodeError::CapacityInvariant);
    }
    for (const auto value : payload) {
        if (!AppendEscaped(frame, value)) {
            return std::unexpected(EncodeError::CapacityInvariant);
        }
    }
    if (!AppendEscaped(frame, checksum_byte)) {
        return std::unexpected(EncodeError::CapacityInvariant);
    }

    return frame;
}

} // namespace gc::rfid::jvs

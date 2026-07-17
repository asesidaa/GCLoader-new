#pragma once

#include "Rfid/Jvs/Types.h"

#include <expected>
#include <span>

namespace gc::rfid::jvs {

enum class EncodeError {
    PayloadTooLarge,
    CapacityInvariant,
};

[[nodiscard]] std::expected<EncodedFrame, EncodeError> EncodePacket(
    Address address,
    std::span<const std::uint8_t> payload) noexcept;

} // namespace gc::rfid::jvs

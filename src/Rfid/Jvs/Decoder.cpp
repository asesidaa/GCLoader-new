#include "Rfid/Jvs/Decoder.h"

#include <cstddef>

namespace gc::rfid::jvs {

std::optional<DecodeEvent> Decoder::Push(std::byte raw) noexcept
{
    if (raw == kSync) {
        packet_ = {};
        checksum_sum_ = 0;
        received_after_count_ = 0;
        marker_pending_ = false;
        phase_ = Phase::Address;
        return std::nullopt;
    }

    if (phase_ == Phase::SeekingSync) {
        return std::nullopt;
    }

    if (marker_pending_) {
        marker_pending_ = false;
        if (raw == std::byte{0xDF}) {
            return PushDecoded(0xE0);
        }
        if (raw == std::byte{0xCF}) {
            return PushDecoded(0xD0);
        }
        Reset();
        return DecodeEvent{FramingError::InvalidEscape};
    }

    if (raw == kMarker) {
        marker_pending_ = true;
        return std::nullopt;
    }

    return PushDecoded(std::to_integer<std::uint8_t>(raw));
}

void Decoder::Reset() noexcept
{
    phase_ = Phase::SeekingSync;
    marker_pending_ = false;
    packet_ = {};
    checksum_sum_ = 0;
    received_after_count_ = 0;
}

std::optional<DecodeEvent> Decoder::PushDecoded(
    std::uint8_t value) noexcept
{
    switch (phase_) {
    case Phase::SeekingSync:
        return std::nullopt;

    case Phase::Address:
        packet_.address = Address{value};
        checksum_sum_ = value;
        phase_ = Phase::ByteCount;
        return std::nullopt;

    case Phase::ByteCount:
        if (value == 0) {
            Reset();
            return DecodeEvent{FramingError::ZeroByteCount};
        }
        packet_.byte_count = value;
        checksum_sum_ += value;
        received_after_count_ = 0;
        phase_ = Phase::AfterCount;
        return std::nullopt;

    case Phase::AfterCount:
        packet_.after_count[received_after_count_] = value;
        ++received_after_count_;

        if (received_after_count_ < packet_.byte_count) {
            checksum_sum_ += value;
            return std::nullopt;
        }

        const auto expected =
            static_cast<std::uint8_t>(checksum_sum_ & 0xFFu);
        const auto actual = value;
        if (expected == actual) {
            auto complete = packet_;
            Reset();
            return DecodeEvent{std::move(complete)};
        }

        const ChecksumFailure failure{
            .address = packet_.address,
            .byte_count = packet_.byte_count,
            .expected = expected,
            .actual = actual,
        };
        Reset();
        return DecodeEvent{failure};
    }

    Reset();
    return DecodeEvent{FramingError::InvalidEscape};
}

} // namespace gc::rfid::jvs

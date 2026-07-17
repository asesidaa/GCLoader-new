#pragma once

#include "Rfid/Jvs/Types.h"

#include <functional>
#include <optional>
#include <span>
#include <utility>

namespace gc::rfid::jvs {

class Decoder {
public:
    [[nodiscard]] std::optional<DecodeEvent> Push(std::byte raw) noexcept;

    template <typename Sink>
    void Consume(std::span<const std::byte> input, Sink&& sink)
    {
        for (const auto raw : input) {
            if (auto event = Push(raw)) {
                std::invoke(sink, std::move(*event));
            }
        }
    }

    void Reset() noexcept;

private:
    enum class Phase {
        SeekingSync,
        Address,
        ByteCount,
        AfterCount,
    };

    [[nodiscard]] std::optional<DecodeEvent> PushDecoded(
        std::uint8_t value) noexcept;

    Phase phase_{Phase::SeekingSync};
    bool marker_pending_{};
    DecodedPacket packet_{};
    std::uint16_t checksum_sum_{};
    std::uint16_t received_after_count_{};
};

} // namespace gc::rfid::jvs

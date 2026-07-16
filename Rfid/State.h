#pragma once

#include "Rfid/Jvs/Types.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

namespace gc::rfid {

inline constexpr std::array<std::uint8_t, 0x18> kCardData{
    0x04, 0xC2, 0x3D, 0xDA, 0x6F, 0x52, 0x80, 0x00,
    0x37, 0x30, 0x32, 0x30, 0x33, 0x39, 0x32, 0x30,
    0x31, 0x30, 0x32, 0x38, 0x31, 0x35, 0x30, 0x32};

class CardScanState {
public:
    void Arm() noexcept
    {
        present_.store(true, std::memory_order_relaxed);
    }

    [[nodiscard]] bool IsPresent() const noexcept
    {
        return present_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool Consume() noexcept
    {
        return present_.exchange(false, std::memory_order_relaxed);
    }

private:
    std::atomic_bool present_{};
};

struct State {
    std::optional<jvs::Address> assigned_address;
    std::array<std::uint16_t, 2> coins{};
    CardScanState card_scan;

    void ResetBus() noexcept;
};

} // namespace gc::rfid

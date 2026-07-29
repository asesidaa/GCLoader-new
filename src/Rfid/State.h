#pragma once

#include "Rfid/Jvs/Types.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

namespace gc::rfid {

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

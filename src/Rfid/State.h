#pragma once

#include "Rfid/CardData.h"
#include "Rfid/Jvs/Types.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <optional>

namespace gc::rfid {

struct CardScanSnapshot {
    bool present{};
    std::optional<CardData> card_data;
    std::uint64_t generation{};
};

class CardScanState {
public:
    CardScanState() noexcept = default;
    CardScanState(const CardScanState&) = delete;
    CardScanState& operator=(const CardScanState&) = delete;

    void Arm() noexcept;
    void Arm(CardData card_data) noexcept;
    [[nodiscard]] CardScanSnapshot Snapshot() const noexcept;
    [[nodiscard]] bool IsPresent() const noexcept;
    [[nodiscard]] bool Consume(std::uint64_t generation) noexcept;

private:
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    bool present_{};
    std::optional<CardData> card_data_;
    std::uint64_t generation_{};
};

struct State {
    std::optional<jvs::Address> assigned_address;
    std::array<std::uint16_t, 2> coins{};
    CardScanState card_scan;

    void ResetBus() noexcept;
};

} // namespace gc::rfid

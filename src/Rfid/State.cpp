#include "Rfid/State.h"

namespace gc::rfid {

void CardScanState::Arm() noexcept
{
    AcquireSRWLockExclusive(&lock_);
    card_data_.reset();
    ++generation_;
    present_ = true;
    ReleaseSRWLockExclusive(&lock_);
}

void CardScanState::Arm(CardData card_data) noexcept
{
    AcquireSRWLockExclusive(&lock_);
    card_data_ = card_data;
    ++generation_;
    present_ = true;
    ReleaseSRWLockExclusive(&lock_);
}

CardScanSnapshot CardScanState::Snapshot() const noexcept
{
    AcquireSRWLockShared(&lock_);
    const CardScanSnapshot snapshot{
        .present = present_,
        .card_data = card_data_,
        .generation = generation_,
    };
    ReleaseSRWLockShared(&lock_);
    return snapshot;
}

bool CardScanState::IsPresent() const noexcept
{
    return Snapshot().present;
}

bool CardScanState::Consume(std::uint64_t generation) noexcept
{
    AcquireSRWLockExclusive(&lock_);
    const bool consumed = present_ && generation_ == generation;
    if (consumed) {
        present_ = false;
        card_data_.reset();
    }
    ReleaseSRWLockExclusive(&lock_);
    return consumed;
}

void State::ResetBus() noexcept
{
    assigned_address.reset();
    coins.fill(0);
}

} // namespace gc::rfid

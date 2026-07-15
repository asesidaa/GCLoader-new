#pragma once

#include <atomic>

namespace gc::rfid {

class CardScanState {
public:
    void Arm() noexcept
    {
        present_.store(true);
    }

    bool IsPresent() const noexcept
    {
        return present_.load();
    }

    bool Consume() noexcept
    {
        return present_.exchange(false);
    }

private:
    std::atomic_bool present_{};
};

}

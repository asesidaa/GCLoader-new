#pragma once

#include <cstddef>
#include <cstdint>

namespace gc::timer_freeze {

inline constexpr std::uintptr_t kRvaGlobalFrameDeltaSeconds = 0x002350C0;
inline constexpr std::size_t kCountdownDeltaCallPatchSize = 5;
inline constexpr std::uint8_t kCountdownDeltaFrozenCallBytes[kCountdownDeltaCallPatchSize] = {
    0xD9, 0xEE, // fldz
    0x90, 0x90, 0x90,
};

struct CountdownDeltaPatchSite {
    std::uintptr_t call_rva;
    std::uintptr_t return_rva;
};

inline constexpr CountdownDeltaPatchSite kCountdownDeltaPatchSites[] = {
    {0x00030322, 0x00030327},
    {0x00030340, 0x00030345},
    {0x001B45D2, 0x001B45D7},
    {0x001B45F1, 0x001B45F6},
    {0x001B4871, 0x001B4876},
    {0x001B4890, 0x001B4895},
    {0x001A6FB4, 0x001A6FB9},
    {0x001A6FD3, 0x001A6FD8},
    {0x001A83B4, 0x001A83B9},
    {0x001A83D3, 0x001A83D8},
    {0x001AEF84, 0x001AEF89},
    {0x001AEFA3, 0x001AEFA8},
    {0x001BB104, 0x001BB109},
    {0x001BB123, 0x001BB128},
    {0x001C1805, 0x001C180A},
    {0x001C1824, 0x001C1829},
    {0x001C55F6, 0x001C55FB},
    {0x001C5615, 0x001C561A},
    {0x001C6746, 0x001C674B},
    {0x001C6765, 0x001C676A},
    {0x00201C22, 0x00201C27},
    {0x00201C41, 0x00201C46},
    {0x00201E70, 0x00201E75},
    {0x00201E8F, 0x00201E94},
    {0x00201FD4, 0x00201FD9},
    {0x00201FF3, 0x00201FF8},
    {0x00204FB6, 0x00204FBB},
    {0x00204FD5, 0x00204FDA},
    {0x002078A3, 0x002078A8},
    {0x002078C2, 0x002078C7},
    {0x0020B124, 0x0020B129},
    {0x0020B143, 0x0020B148},
};

inline const CountdownDeltaPatchSite* CountdownDeltaPatchForReturnRva(std::uintptr_t return_rva) noexcept {
    for (const auto& site : kCountdownDeltaPatchSites) {
        if (site.return_rva == return_rva) {
            return &site;
        }
    }

    return nullptr;
}

inline bool IsCountdownDeltaReturnRva(std::uintptr_t return_rva) noexcept {
    return CountdownDeltaPatchForReturnRva(return_rva) != nullptr;
}

void SetCountdownTimerFreezeEnabled(bool enabled) noexcept;
bool IsCountdownTimerFreezeEnabled() noexcept;
void CountdownTimerFreezeInit();

} // namespace gc::timer_freeze

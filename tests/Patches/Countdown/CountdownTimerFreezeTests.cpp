#include "Patches/Countdown/CountdownTimerFreeze.h"

#include <cstdint>
#include <iostream>

namespace {

struct RvaCase {
    std::uintptr_t rva;
    const char* name;
};

constexpr RvaCase kCountdownReturnRvas[] = {
    {0x00030327, "generic displayed countdown first delta return"},
    {0x00030345, "generic displayed countdown second delta return"},
    {0x001B45D7, "tutorial yes/no countdown first delta return"},
    {0x001B45F6, "tutorial yes/no countdown second delta return"},
    {0x001B4876, "song/category countdown first delta return"},
    {0x001B4895, "song/category countdown second delta return"},
    {0x001A6FB9, "5.9s displayed countdown first return"},
    {0x001A6FD8, "5.9s displayed countdown second return"},
    {0x001A83B9, "alternate 5.9s displayed countdown first return"},
    {0x001A83D8, "alternate 5.9s displayed countdown second return"},
    {0x001AEF89, "90.9s/30.9s displayed countdown first return"},
    {0x001AEFA8, "90.9s/30.9s displayed countdown second return"},
    {0x001BB109, "9.9s displayed countdown first return"},
    {0x001BB128, "9.9s displayed countdown second return"},
    {0x001C180A, "35.9s displayed countdown first return"},
    {0x001C1829, "35.9s displayed countdown second return"},
    {0x001C55FB, "song select 45.9s displayed countdown first return"},
    {0x001C561A, "song select 45.9s displayed countdown second return"},
    {0x001C674B, "song select 15.9s displayed countdown first return"},
    {0x001C676A, "song select 15.9s displayed countdown second return"},
    {0x00201C27, "mission displayed countdown first return"},
    {0x00201C46, "mission displayed countdown second return"},
    {0x00201E75, "90.9s mission displayed countdown first return"},
    {0x00201E94, "90.9s mission displayed countdown second return"},
    {0x00201FD9, "5.9s mission displayed countdown first return"},
    {0x00201FF8, "5.9s mission displayed countdown second return"},
    {0x00204FBB, "20.9s displayed countdown first return"},
    {0x00204FDA, "20.9s displayed countdown second return"},
    {0x002078A8, "12.9s displayed countdown first return"},
    {0x002078C7, "12.9s displayed countdown second return"},
    {0x0020B129, "final 15.9s displayed countdown first return"},
    {0x0020B148, "final 15.9s displayed countdown second return"},
};

constexpr RvaCase kNonCountdownReturnRvas[] = {
    {0x00000000, "null RVA"},
    {0x002350C0, "delta function entry"},
    {0x001B45D2, "select countdown call address, not return address"},
    {0x001B45D6, "one byte before select countdown return"},
    {0x001B45D8, "one byte after select countdown return"},
    {0x001AF1F8, "excluded transition/progress timer first return"},
    {0x001AF21A, "excluded transition/progress timer second return"},
};

constexpr std::uintptr_t kExpectedCallRvas[] = {
    0x00030322, 0x00030340,
    0x001B45D2, 0x001B45F1,
    0x001B4871, 0x001B4890,
    0x001A6FB4, 0x001A6FD3,
    0x001A83B4, 0x001A83D3,
    0x001AEF84, 0x001AEFA3,
    0x001BB104, 0x001BB123,
    0x001C1805, 0x001C1824,
    0x001C55F6, 0x001C5615,
    0x001C6746, 0x001C6765,
    0x00201C22, 0x00201C41,
    0x00201E70, 0x00201E8F,
    0x00201FD4, 0x00201FF3,
    0x00204FB6, 0x00204FD5,
    0x002078A3, 0x002078C2,
    0x0020B124, 0x0020B143,
};

int expect_countdown(std::uintptr_t rva, const char* name) {
    if (gc::timer_freeze::IsCountdownDeltaReturnRva(rva)) {
        return 0;
    }

    std::cerr << "Expected countdown return RVA for " << name << " (0x"
              << std::hex << rva << std::dec << ")\n";
    return 1;
}

int expect_not_countdown(std::uintptr_t rva, const char* name) {
    if (!gc::timer_freeze::IsCountdownDeltaReturnRva(rva)) {
        return 0;
    }

    std::cerr << "Expected non-countdown return RVA for " << name << " (0x"
              << std::hex << rva << std::dec << ")\n";
    return 1;
}

int expect_patch_call_rva(std::uintptr_t return_rva, std::uintptr_t expected_call_rva, const char* name) {
    const auto patch = gc::timer_freeze::CountdownDeltaPatchForReturnRva(return_rva);
    if (patch && patch->call_rva == expected_call_rva) {
        return 0;
    }

    std::cerr << "Expected patch call RVA 0x" << std::hex << expected_call_rva
              << " for " << name << " return RVA 0x" << return_rva << std::dec << "\n";
    return 1;
}

} // namespace

int main() {
    int failures = 0;

    for (const auto& rva_case : kCountdownReturnRvas) {
        failures += expect_countdown(rva_case.rva, rva_case.name);
    }

    for (const auto& rva_case : kNonCountdownReturnRvas) {
        failures += expect_not_countdown(rva_case.rva, rva_case.name);
        if (gc::timer_freeze::CountdownDeltaPatchForReturnRva(rva_case.rva)) {
            std::cerr << "Expected no patch descriptor for " << rva_case.name << " (0x"
                      << std::hex << rva_case.rva << std::dec << ")\n";
            ++failures;
        }
    }

    for (std::size_t i = 0; i < std::size(kCountdownReturnRvas); ++i) {
        failures += expect_patch_call_rva(
            kCountdownReturnRvas[i].rva,
            kExpectedCallRvas[i],
            kCountdownReturnRvas[i].name);
    }

    return failures == 0 ? 0 : 1;
}

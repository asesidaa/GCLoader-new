#include "Input/XioOnly/XioOnlyInputPatch.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

} // namespace

int main() {
    using namespace gc::xio_only_input;
    int failures = 0;

    failures += Expect(
        kPressedQueryRva == 0x00052C90 &&
            kHeldQueryRva == 0x00052CC0 &&
            kReleasedQueryRva == 0x00052CF0 &&
            kRepeatQueryRva == 0x00052D20,
        "exact native PC query RVAs");
    failures += Expect(
        kPcInputQuerySignature == std::array<std::uint8_t, 16>{
            0xA1, 0x20, 0xF1, 0x72, 0x00, 0x85, 0x05, 0x1C,
            0xF1, 0x72, 0x00, 0x75, 0x03, 0x33, 0xC0, 0xC3},
        "exact common query signature");

    auto pressed = kPcInputQuerySignature;
    auto held = kPcInputQuerySignature;
    auto released = kPcInputQuerySignature;
    auto repeat = kPcInputQuerySignature;
    PcInputQuerySite mismatch = PcInputQuerySite::None;
    failures += Expect(
        ValidatePcInputQuerySignatures(
            {pressed, held, released, repeat}, &mismatch),
        "all query signatures match");
    failures += Expect(
        mismatch == PcInputQuerySite::None,
        "matching set has no mismatch site");

    constexpr std::array expected_sites{
        PcInputQuerySite::Pressed,
        PcInputQuerySite::Held,
        PcInputQuerySite::Released,
        PcInputQuerySite::Repeat,
    };
    for (std::size_t index = 0; index < expected_sites.size(); ++index) {
        auto bad_pressed = pressed;
        auto bad_held = held;
        auto bad_released = released;
        auto bad_repeat = repeat;
        switch (index) {
        case 0:
            bad_pressed[0] ^= 0xFF;
            break;
        case 1:
            bad_held[1] ^= 0xFF;
            break;
        case 2:
            bad_released[2] ^= 0xFF;
            break;
        case 3:
            bad_repeat[3] ^= 0xFF;
            break;
        default:
            break;
        }
        failures += Expect(
            !ValidatePcInputQuerySignatures(
                {bad_pressed, bad_held, bad_released, bad_repeat},
                &mismatch),
            "mismatched query set is rejected");
        failures += Expect(
            mismatch == expected_sites[index],
            "mismatch reports exact query site");
    }

    failures += Expect(
        CompletePcInputHookSet({true, true, true, true}),
        "complete hook set is active");
    failures += Expect(
        !CompletePcInputHookSet({false, true, true, true}) &&
            !CompletePcInputHookSet({true, false, true, true}) &&
            !CompletePcInputHookSet({true, true, false, true}) &&
            !CompletePcInputHookSet({true, true, true, false}),
        "every partial hook set is inactive");

    return failures == 0 ? 0 : 1;
}

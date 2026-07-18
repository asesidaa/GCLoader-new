#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <utility>

namespace gc::xio_only_input {

inline constexpr std::uintptr_t kPressedQueryRva = 0x00052C90;
inline constexpr std::uintptr_t kHeldQueryRva = 0x00052CC0;
inline constexpr std::uintptr_t kReleasedQueryRva = 0x00052CF0;
inline constexpr std::uintptr_t kRepeatQueryRva = 0x00052D20;

inline constexpr std::array<std::uint8_t, 16> kPcInputQuerySignature{
    0xA1, 0x20, 0xF1, 0x72, 0x00, 0x85, 0x05, 0x1C,
    0xF1, 0x72, 0x00, 0x75, 0x03, 0x33, 0xC0, 0xC3,
};

enum class PcInputQuerySite {
    None,
    Pressed,
    Held,
    Released,
    Repeat,
};

struct PcInputQuerySignatureSpans {
    std::span<const std::uint8_t> pressed;
    std::span<const std::uint8_t> held;
    std::span<const std::uint8_t> released;
    std::span<const std::uint8_t> repeat;
};

struct PcInputHookSet {
    bool pressed{};
    bool held{};
    bool released{};
    bool repeat{};
};

[[nodiscard]] inline bool HasExpectedPcInputPrefix(
    std::span<const std::uint8_t> actual) noexcept {
    return actual.size() >= kPcInputQuerySignature.size() &&
        std::equal(
            kPcInputQuerySignature.begin(),
            kPcInputQuerySignature.end(),
            actual.begin());
}

[[nodiscard]] inline bool ValidatePcInputQuerySignatures(
    const PcInputQuerySignatureSpans& signatures,
    PcInputQuerySite* mismatch) noexcept {
    if (mismatch != nullptr) {
        *mismatch = PcInputQuerySite::None;
    }
    const std::array checks{
        std::pair{signatures.pressed, PcInputQuerySite::Pressed},
        std::pair{signatures.held, PcInputQuerySite::Held},
        std::pair{signatures.released, PcInputQuerySite::Released},
        std::pair{signatures.repeat, PcInputQuerySite::Repeat},
    };
    for (const auto& [bytes, site] : checks) {
        if (!HasExpectedPcInputPrefix(bytes)) {
            if (mismatch != nullptr) {
                *mismatch = site;
            }
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr bool CompletePcInputHookSet(
    const PcInputHookSet& hooks) noexcept {
    return hooks.pressed && hooks.held && hooks.released && hooks.repeat;
}

[[nodiscard]] bool XioOnlyInputPatchInit();

} // namespace gc::xio_only_input

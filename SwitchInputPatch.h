#pragma once

#include "SwitchInputPolicy.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gc::switch_input {

inline constexpr std::uintptr_t kGameplayPressedQueryRva = 0x00259640;
inline constexpr std::uintptr_t kGameplayHeldQueryRva = 0x00259570;
inline constexpr std::uintptr_t kDiagonalMatchRva = 0x001D32A0;

inline constexpr std::array<std::uint8_t, 16> kGameplayQueryEntrySignature{
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0x89, 0x4D,
    0xEC, 0xC6, 0x45, 0xFF, 0x00, 0x8B, 0x4D, 0xEC,
};

inline constexpr std::array<std::uint8_t, 9> kDiagonalMatchSignature{
    0x0F, 0xB6, 0x55, 0x8B, 0x83, 0xFA, 0x01, 0x75, 0x2B,
};

inline constexpr std::ptrdiff_t kDiagonalNativeMatchOffset = -0x75;
inline constexpr std::ptrdiff_t kDiagonalTargetDirectionOffset = -0x7C;
inline constexpr std::ptrdiff_t kDiagonalCurrentDirectionOffset = -0x68;

enum class SwitchHookSite {
    None,
    PressedEdge,
    HeldState,
    DiagonalMatch,
};

constexpr std::uintptr_t RvaForHookSite(SwitchHookSite site) noexcept {
    switch (site) {
    case SwitchHookSite::PressedEdge:
        return kGameplayPressedQueryRva;
    case SwitchHookSite::HeldState:
        return kGameplayHeldQueryRva;
    case SwitchHookSite::DiagonalMatch:
        return kDiagonalMatchRva;
    case SwitchHookSite::None:
        return 0;
    }
    return 0;
}

constexpr const char* HookSiteName(SwitchHookSite site) noexcept {
    switch (site) {
    case SwitchHookSite::PressedEdge:
        return "pressed_edge";
    case SwitchHookSite::HeldState:
        return "held_state";
    case SwitchHookSite::DiagonalMatch:
        return "diagonal_match";
    case SwitchHookSite::None:
        return "none";
    }
    return "unknown";
}

struct SwitchInputSignatureSpans {
    std::span<const std::uint8_t> pressed_edge;
    std::span<const std::uint8_t> held_state;
    std::span<const std::uint8_t> diagonal_match;
};

inline bool HasExpectedPrefix(
    std::span<const std::uint8_t> actual,
    std::span<const std::uint8_t> expected) noexcept {
    return actual.size() >= expected.size() &&
           std::equal(expected.begin(), expected.end(), actual.begin());
}

inline bool ValidateSwitchInputSignatures(
    const SwitchInputSignatureSpans& signatures,
    SwitchHookSite* mismatch) noexcept {
    if (mismatch != nullptr) {
        *mismatch = SwitchHookSite::None;
    }

    if (!HasExpectedPrefix(
            signatures.pressed_edge,
            kGameplayQueryEntrySignature)) {
        if (mismatch != nullptr) {
            *mismatch = SwitchHookSite::PressedEdge;
        }
        return false;
    }
    if (!HasExpectedPrefix(
            signatures.held_state,
            kGameplayQueryEntrySignature)) {
        if (mismatch != nullptr) {
            *mismatch = SwitchHookSite::HeldState;
        }
        return false;
    }
    if (!HasExpectedPrefix(
            signatures.diagonal_match,
            kDiagonalMatchSignature)) {
        if (mismatch != nullptr) {
            *mismatch = SwitchHookSite::DiagonalMatch;
        }
        return false;
    }
    return true;
}

enum class SwitchPatchState {
    Arcade,
    Switch,
};

struct HookCreationResults {
    bool pressed_edge{false};
    bool held_state{false};
    bool diagonal_match{false};
};

constexpr SwitchPatchState ResolveSwitchPatchState(
    HookCreationResults hooks) noexcept {
    return hooks.pressed_edge && hooks.held_state && hooks.diagonal_match
        ? SwitchPatchState::Switch
        : SwitchPatchState::Arcade;
}

using StackRead = bool (*)(
    void* context,
    std::ptrdiff_t offset,
    void* output,
    std::size_t size) noexcept;

using StackWrite = bool (*)(
    void* context,
    std::ptrdiff_t offset,
    const void* input,
    std::size_t size) noexcept;

struct StackAccessor {
    void* context{nullptr};
    StackRead read{nullptr};
    StackWrite write{nullptr};
};

inline bool TryApplySwitchDiagonalMatch(
    const StackAccessor& stack) noexcept {
    if (stack.read == nullptr || stack.write == nullptr) {
        return false;
    }

    std::uint8_t native_match = 0;
    if (!stack.read(
            stack.context,
            kDiagonalNativeMatchOffset,
            &native_match,
            sizeof(native_match)) ||
        native_match != 0) {
        return false;
    }

    LogicalInputId target_direction = 0;
    LogicalInputId current_direction = 0;
    if (!stack.read(
            stack.context,
            kDiagonalTargetDirectionOffset,
            &target_direction,
            sizeof(target_direction)) ||
        !stack.read(
            stack.context,
            kDiagonalCurrentDirectionOffset,
            &current_direction,
            sizeof(current_direction)) ||
        !IsSwitchDiagonalComponent(target_direction, current_direction)) {
        return false;
    }

    constexpr std::uint8_t kMatched = 1;
    return stack.write(
        stack.context,
        kDiagonalNativeMatchOffset,
        &kMatched,
        sizeof(kMatched));
}

void SwitchInputPatchInit();

} // namespace gc::switch_input

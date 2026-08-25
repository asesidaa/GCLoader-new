#pragma once

#include "Input/Types/InputTypes.h"

#include <array>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <cstdint>

namespace gc::input {

namespace FastIoBits {
inline constexpr std::uint32_t P1_SERVICE_I = 1u << 1;
inline constexpr std::uint32_t P1_SERVICE_F1 = 1u << 2;
inline constexpr std::uint32_t P1_SERVICE_P = 1u << 3;
inline constexpr std::uint32_t P1_START = 1u << 4;
inline constexpr std::uint32_t P2_START = 1u << 5;
inline constexpr std::uint32_t TEST_MODE = 1u << 6;
inline constexpr std::uint32_t P1_UP = 1u << 8;
inline constexpr std::uint32_t P2_UP = 1u << 9;
inline constexpr std::uint32_t P1_DOWN = 1u << 10;
inline constexpr std::uint32_t P2_DOWN = 1u << 11;
inline constexpr std::uint32_t P1_LEFT = 1u << 12;
inline constexpr std::uint32_t P2_LEFT = 1u << 13;
inline constexpr std::uint32_t P1_RIGHT = 1u << 14;
inline constexpr std::uint32_t P2_RIGHT = 1u << 15;
inline constexpr std::uint32_t P1_BUTTON_1 = 1u << 16;
inline constexpr std::uint32_t P2_BUTTON_1 = 1u << 17;
inline constexpr std::uint32_t P2_SERVICE = 1u << 2;
}

using LogicalInput = LogicalAction;

inline constexpr std::size_t kGameplayLogicalInputCount = 10;

inline constexpr std::array<
    std::uint32_t,
    static_cast<std::size_t>(LogicalInput::Count)>
    kFastIoMaskByLogicalAction{
        FastIoBits::P1_UP,
        FastIoBits::P2_UP,
        FastIoBits::P1_DOWN,
        FastIoBits::P2_DOWN,
        FastIoBits::P1_BUTTON_1,
        FastIoBits::P1_LEFT,
        FastIoBits::P2_LEFT,
        FastIoBits::P1_RIGHT,
        FastIoBits::P2_RIGHT,
        FastIoBits::P2_BUTTON_1,
        FastIoBits::P1_SERVICE_F1,
        FastIoBits::P1_SERVICE_I,
        FastIoBits::P1_SERVICE_P,
        FastIoBits::P1_START,
        FastIoBits::P2_START,
        FastIoBits::P2_SERVICE,
        FastIoBits::TEST_MODE};

enum class InputSource : std::uint8_t {
    Keyboard,
    Controller,
    Count
};

enum class GameplaySource : std::uint8_t {
    Keyboard,
    Controller
};

class InputSnapshotState {
public:
    void Set(LogicalInput input, InputSource source, bool pressed) noexcept;
    void ClearKeyboard() noexcept;
    void ClearController() noexcept;
    std::uint32_t Compose(GameplaySource source) const noexcept;

private:
    static constexpr std::size_t kLogicalInputCount =
        static_cast<std::size_t>(LogicalInput::Count);
    static constexpr std::size_t kInputSourceCount =
        static_cast<std::size_t>(InputSource::Count);

    std::array<std::array<bool, kLogicalInputCount>, kInputSourceCount>
        sources_{};
};

}

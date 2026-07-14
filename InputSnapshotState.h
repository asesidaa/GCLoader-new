#pragma once

#include <array>
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

enum class LogicalInput : std::uint8_t {
    LeftBoosterUp,
    LeftBoosterDown,
    LeftBoosterLeft,
    LeftBoosterRight,
    LeftBoosterButton,
    RightBoosterUp,
    RightBoosterDown,
    RightBoosterLeft,
    RightBoosterRight,
    RightBoosterButton,
    Service1,
    Service2,
    Service3,
    P1Start,
    P2Start,
    P2Service,
    Test,
    Count
};

enum class InputSource : std::uint8_t {
    Keyboard,
    GamepadButton,
    GamepadAxis,
    Count
};

enum class GameplaySource : std::uint8_t {
    Keyboard,
    Gamepad
};

class InputSnapshotState {
public:
    void Set(LogicalInput input, InputSource source, bool pressed) noexcept;
    void ClearKeyboard() noexcept;
    void ClearGamepad() noexcept;
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

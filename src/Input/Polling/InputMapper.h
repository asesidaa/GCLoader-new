#pragma once

#include "Input/Polling/InputSnapshotState.h"
#include "Input/Types/InputSettings.h"

#include <cstdint>
#include <span>
#include <vector>

namespace gc::input
{
    class InputMapper
    {
    public:
        InputMapper(
            InputMode mode,
            std::span<const KeyboardBinding> keyboard,
            std::span<const ControllerBinding> controller);

        void ApplyKeyboardTransition(PhysicalKey key, bool pressed) noexcept;
        void ApplyControllerBindingStates(
            std::span<const std::uint8_t> states) noexcept;
        void ClearKeyboard() noexcept;
        void ClearController() noexcept;
        void ClearAll() noexcept;
        [[nodiscard]] std::uint32_t GetInput() const noexcept;

    private:
        void RebuildKeyboardSnapshot() noexcept;

        InputMode mode_;
        std::vector<KeyboardBinding> keyboard_bindings_;
        std::vector<ControllerBinding> controller_bindings_;
        std::vector<std::uint8_t> keyboard_states_;
        std::vector<std::uint8_t> controller_states_;
        InputSnapshotState snapshot_;
    };
} // namespace gc::input

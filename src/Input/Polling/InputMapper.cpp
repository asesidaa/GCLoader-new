#include "Input/Polling/InputMapper.h"

#include <algorithm>

namespace gc::input {

InputMapper::InputMapper(
    InputMode mode,
    std::span<const KeyboardBinding> keyboard,
    std::span<const DigitalControlBinding> controller)
    : mode_(mode),
      keyboard_bindings_(keyboard.begin(), keyboard.end()),
      controller_bindings_(controller.begin(), controller.end()),
      keyboard_states_(keyboard.size()),
      controller_states_(controller.size())
{
}

void InputMapper::ApplyKeyboardTransition(
    PhysicalKey key,
    bool pressed) noexcept
{
    for (std::size_t index = 0; index < keyboard_bindings_.size(); ++index)
    {
        if (keyboard_bindings_[index].key == key)
        {
            keyboard_states_[index] = pressed ? 1 : 0;
        }
    }
    RebuildKeyboardSnapshot();
}

void InputMapper::ApplyControllerBindingStates(
    std::span<const std::uint8_t> states) noexcept
{
    snapshot_.ClearController();
    std::ranges::fill(controller_states_, 0);
    const std::size_t count = std::min(states.size(), controller_states_.size());
    for (std::size_t index = 0; index < count; ++index)
    {
        controller_states_[index] = states[index] != 0 ? 1 : 0;
    }
    for (std::size_t index = 0; index < controller_states_.size(); ++index)
    {
        if (controller_states_[index] != 0 &&
            controller_bindings_[index].action != LogicalAction::Count)
        {
            snapshot_.Set(
                controller_bindings_[index].action,
                InputSource::Controller,
                true);
        }
    }
}

void InputMapper::ClearKeyboard() noexcept
{
    std::ranges::fill(keyboard_states_, 0);
    snapshot_.ClearKeyboard();
}

void InputMapper::ClearController() noexcept
{
    std::ranges::fill(controller_states_, 0);
    snapshot_.ClearController();
}

void InputMapper::ClearAll() noexcept
{
    ClearKeyboard();
    ClearController();
}

std::uint32_t InputMapper::GetInput() const noexcept
{
    return snapshot_.Compose(
        mode_ == InputMode::Keyboard
            ? GameplaySource::Keyboard
            : GameplaySource::Controller);
}

void InputMapper::RebuildKeyboardSnapshot() noexcept
{
    snapshot_.ClearKeyboard();
    for (std::size_t index = 0; index < keyboard_states_.size(); ++index)
    {
        if (keyboard_states_[index] != 0 &&
            keyboard_bindings_[index].action != LogicalAction::Count)
        {
            snapshot_.Set(
                keyboard_bindings_[index].action,
                InputSource::Keyboard,
                true);
        }
    }
}

} // namespace gc::input

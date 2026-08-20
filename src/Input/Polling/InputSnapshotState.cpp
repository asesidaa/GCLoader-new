#include "Input/Polling/InputSnapshotState.h"

namespace gc::input {
namespace {

constexpr std::size_t index(LogicalInput input) noexcept
{
    return static_cast<std::size_t>(input);
}

constexpr std::size_t index(InputSource source) noexcept
{
    return static_cast<std::size_t>(source);
}

constexpr bool is_gameplay(LogicalInput input) noexcept
{
    return input <= LogicalInput::RightBoosterButton;
}

}

void InputSnapshotState::Set(
    LogicalInput input,
    InputSource source,
    bool pressed) noexcept
{
    if (input == LogicalInput::Count || source == InputSource::Count)
    {
        return;
    }

    sources_[index(source)][index(input)] = pressed;
}

void InputSnapshotState::ClearKeyboard() noexcept
{
    sources_[index(InputSource::Keyboard)].fill(false);
}

void InputSnapshotState::ClearController() noexcept
{
    sources_[index(InputSource::Controller)].fill(false);
}

std::uint32_t InputSnapshotState::Compose(GameplaySource source) const noexcept
{
    std::uint32_t result = 0;
    for (std::size_t logical_index = 0;
         logical_index < kLogicalInputCount;
         ++logical_index)
    {
        const auto logical = static_cast<LogicalInput>(logical_index);
        bool pressed = false;
        if (!is_gameplay(logical) || source == GameplaySource::Keyboard)
        {
            pressed =
                sources_[index(InputSource::Keyboard)][logical_index];
        }
        else
        {
            pressed =
                sources_[index(InputSource::Controller)][logical_index];
        }

        if (pressed)
        {
            result |= kFastIoMaskByLogicalAction[logical_index];
        }
    }

    return result;
}

}

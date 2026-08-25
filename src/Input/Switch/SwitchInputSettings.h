#pragma once

#include "Input/Types/InputTypes.h"

namespace gc::config
{
    class ConfigCompiler;
}

namespace gc::switch_input
{
    class SwitchInputSettings final
    {
    public:
        [[nodiscard]] input::GameplayInputStyle style() const noexcept
        {
            return style_;
        }

    private:
        explicit SwitchInputSettings(
            input::GameplayInputStyle style) noexcept
            : style_(style)
        {
        }

        friend class gc::config::ConfigCompiler;
        input::GameplayInputStyle style_{};
    };
} // namespace gc::switch_input

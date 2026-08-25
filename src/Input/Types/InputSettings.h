#pragma once

#include "Input/Types/InputTypes.h"

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace gc::config
{
    class ConfigCompiler;
}

namespace gc::input
{
    struct HidControlAddress
    {
        std::uint16_t usage_page{};
        std::uint16_t usage{};
        std::uint16_t link_collection{};
        std::uint8_t report_id{};
    };

    class XInputButtonBinding final
    {
    public:
        [[nodiscard]] LogicalAction action() const noexcept
        {
            return action_;
        }

        [[nodiscard]] XInputControl control() const noexcept
        {
            return control_;
        }

    private:
        XInputButtonBinding(
            LogicalAction action,
            XInputControl control) noexcept
            : action_(action), control_(control)
        {
        }

        friend class gc::config::ConfigCompiler;
        LogicalAction action_{};
        XInputControl control_{};
    };

    class XInputAxisBinding final
    {
    public:
        [[nodiscard]] LogicalAction action() const noexcept
        {
            return action_;
        }

        [[nodiscard]] XInputControl control() const noexcept
        {
            return control_;
        }

        [[nodiscard]] ControlDirection direction() const noexcept
        {
            return direction_;
        }

    private:
        XInputAxisBinding(
            LogicalAction action,
            XInputControl control,
            ControlDirection direction) noexcept
            : action_(action), control_(control), direction_(direction)
        {
        }

        friend class gc::config::ConfigCompiler;
        LogicalAction action_{};
        XInputControl control_{};
        ControlDirection direction_{};
    };

    class XInputTriggerBinding final
    {
    public:
        [[nodiscard]] LogicalAction action() const noexcept
        {
            return action_;
        }

        [[nodiscard]] XInputControl control() const noexcept
        {
            return control_;
        }

    private:
        XInputTriggerBinding(
            LogicalAction action,
            XInputControl control) noexcept
            : action_(action), control_(control)
        {
        }

        friend class gc::config::ConfigCompiler;
        LogicalAction action_{};
        XInputControl control_{};
    };

    class RawHidButtonBinding final
    {
    public:
        [[nodiscard]] LogicalAction action() const noexcept
        {
            return action_;
        }

        [[nodiscard]] const HidControlAddress& address() const noexcept
        {
            return address_;
        }

    private:
        RawHidButtonBinding(
            LogicalAction action,
            HidControlAddress address) noexcept
            : action_(action), address_(address)
        {
        }

        friend class gc::config::ConfigCompiler;
        LogicalAction action_{};
        HidControlAddress address_{};
    };

    class RawHidValueBinding final
    {
    public:
        [[nodiscard]] LogicalAction action() const noexcept
        {
            return action_;
        }

        [[nodiscard]] const HidControlAddress& address() const noexcept
        {
            return address_;
        }

        [[nodiscard]] ControlDirection direction() const noexcept
        {
            return direction_;
        }

        [[nodiscard]] std::int32_t neutral_value() const noexcept
        {
            return neutral_value_;
        }

    private:
        RawHidValueBinding(
            LogicalAction action,
            HidControlAddress address,
            ControlDirection direction,
            std::int32_t neutral_value) noexcept
            : action_(action),
              address_(address),
              direction_(direction),
              neutral_value_(neutral_value)
        {
        }

        friend class gc::config::ConfigCompiler;
        LogicalAction action_{};
        HidControlAddress address_{};
        ControlDirection direction_{};
        std::int32_t neutral_value_{};
    };

    class RawHidHatBinding final
    {
    public:
        [[nodiscard]] LogicalAction action() const noexcept
        {
            return action_;
        }

        [[nodiscard]] const HidControlAddress& address() const noexcept
        {
            return address_;
        }

        [[nodiscard]] ControlDirection direction() const noexcept
        {
            return direction_;
        }

    private:
        RawHidHatBinding(
            LogicalAction action,
            HidControlAddress address,
            ControlDirection direction) noexcept
            : action_(action), address_(address), direction_(direction)
        {
        }

        friend class gc::config::ConfigCompiler;
        LogicalAction action_{};
        HidControlAddress address_{};
        ControlDirection direction_{};
    };

    using ControllerBinding = std::variant<
        XInputButtonBinding,
        XInputAxisBinding,
        XInputTriggerBinding,
        RawHidButtonBinding,
        RawHidValueBinding,
        RawHidHatBinding>;

    [[nodiscard]] inline LogicalAction BindingAction(
        const ControllerBinding& binding) noexcept
    {
        return std::visit(
            [](const auto& concrete) noexcept
            {
                return concrete.action();
            },
            binding);
    }

    class XInputControllerSettings final
    {
    public:
        [[nodiscard]] std::uint32_t slot() const noexcept
        {
            return slot_;
        }

        [[nodiscard]] std::span<const ControllerBinding> bindings() const noexcept
        {
            return bindings_;
        }

    private:
        XInputControllerSettings(
            std::uint32_t slot,
            std::vector<ControllerBinding> bindings)
            : slot_(slot), bindings_(std::move(bindings))
        {
        }

        friend class gc::config::ConfigCompiler;
        std::uint32_t slot_{};
        std::vector<ControllerBinding> bindings_;
    };

    class RawHidControllerSettings final
    {
    public:
        [[nodiscard]] const std::string& device_path() const noexcept
        {
            return device_path_;
        }

        [[nodiscard]] std::span<const ControllerBinding> bindings() const noexcept
        {
            return bindings_;
        }

    private:
        RawHidControllerSettings(
            std::string device_path,
            std::vector<ControllerBinding> bindings)
            : device_path_(std::move(device_path)),
              bindings_(std::move(bindings))
        {
        }

        friend class gc::config::ConfigCompiler;
        std::string device_path_;
        std::vector<ControllerBinding> bindings_;
    };

    using ControllerSettings = std::variant<
        XInputControllerSettings,
        RawHidControllerSettings>;

    class InputSettings final
    {
    public:
        [[nodiscard]] std::uint32_t poll_hz() const noexcept
        {
            return poll_hz_;
        }

        [[nodiscard]] bool absolute_publication_enabled() const noexcept
        {
            return absolute_publication_enabled_;
        }

        [[nodiscard]] InputMode mode() const noexcept
        {
            return mode_;
        }

        [[nodiscard]] std::uint32_t press_percent() const noexcept
        {
            return press_percent_;
        }

        [[nodiscard]] std::uint32_t release_percent() const noexcept
        {
            return release_percent_;
        }

        [[nodiscard]] std::span<const KeyboardBinding> keyboard() const noexcept
        {
            return keyboard_;
        }

        [[nodiscard]] const ControllerSettings& controller() const noexcept
        {
            return controller_;
        }

    private:
        InputSettings(
            std::uint32_t poll_hz,
            bool absolute_publication_enabled,
            InputMode mode,
            std::uint32_t press_percent,
            std::uint32_t release_percent,
            std::vector<KeyboardBinding> keyboard,
            ControllerSettings controller)
            : poll_hz_(poll_hz),
              absolute_publication_enabled_(absolute_publication_enabled),
              mode_(mode),
              press_percent_(press_percent),
              release_percent_(release_percent),
              keyboard_(std::move(keyboard)),
              controller_(std::move(controller))
        {
        }

        friend class gc::config::ConfigCompiler;
        std::uint32_t poll_hz_{};
        bool absolute_publication_enabled_{};
        InputMode mode_{};
        std::uint32_t press_percent_{};
        std::uint32_t release_percent_{};
        std::vector<KeyboardBinding> keyboard_;
        ControllerSettings controller_;
    };
} // namespace gc::input

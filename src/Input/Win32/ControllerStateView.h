#pragma once

#include "Input/Types/InputSettings.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>

namespace gc::input
{
    struct ControllerControlDescriptor
    {
        DigitalControlBinding binding;
        std::string label;
    };

    class ControllerStateView
    {
    public:
        virtual ~ControllerStateView() = default;

        [[nodiscard]] virtual const ControllerIdentity& identity() const noexcept = 0;
        [[nodiscard]] virtual std::span<const ControllerControlDescriptor>
        controls() const noexcept = 0;

        [[nodiscard]] std::optional<double> Activation(
            const ControllerBinding& binding) const noexcept
        {
            return std::visit(
                [this](const auto& concrete) noexcept
                {
                    return Activation(RawBinding(concrete));
                },
                binding);
        }

        [[nodiscard]] virtual std::optional<double> Activation(
            const DigitalControlBinding& binding) const noexcept = 0;
        [[nodiscard]] virtual std::optional<std::int32_t> RawValue(
            const DigitalControlBinding& binding) const noexcept = 0;

    private:
        [[nodiscard]] static DigitalControlBinding RawBinding(
            const XInputButtonBinding& binding) noexcept
        {
            return {
                .action = binding.action(),
                .type = DigitalControlType::XInputButton,
                .control = binding.control(),
            };
        }

        [[nodiscard]] static DigitalControlBinding RawBinding(
            const XInputAxisBinding& binding) noexcept
        {
            return {
                .action = binding.action(),
                .type = DigitalControlType::XInputAxis,
                .control = binding.control(),
                .direction = binding.direction(),
            };
        }

        [[nodiscard]] static DigitalControlBinding RawBinding(
            const XInputTriggerBinding& binding) noexcept
        {
            return {
                .action = binding.action(),
                .type = DigitalControlType::XInputTrigger,
                .control = binding.control(),
            };
        }

        [[nodiscard]] static DigitalControlBinding RawBinding(
            const RawHidButtonBinding& binding) noexcept
        {
            const auto& address = binding.address();
            return {
                .action = binding.action(),
                .type = DigitalControlType::RawHidButton,
                .usage_page = address.usage_page,
                .usage = address.usage,
                .link_collection = address.link_collection,
                .report_id = address.report_id,
            };
        }

        [[nodiscard]] static DigitalControlBinding RawBinding(
            const RawHidValueBinding& binding) noexcept
        {
            const auto& address = binding.address();
            return {
                .action = binding.action(),
                .type = DigitalControlType::RawHidValue,
                .direction = binding.direction(),
                .usage_page = address.usage_page,
                .usage = address.usage,
                .link_collection = address.link_collection,
                .report_id = address.report_id,
                .neutral_value = binding.neutral_value(),
            };
        }

        [[nodiscard]] static DigitalControlBinding RawBinding(
            const RawHidHatBinding& binding) noexcept
        {
            const auto& address = binding.address();
            return {
                .action = binding.action(),
                .type = DigitalControlType::RawHidHat,
                .direction = binding.direction(),
                .usage_page = address.usage_page,
                .usage = address.usage,
                .link_collection = address.link_collection,
                .report_id = address.report_id,
            };
        }
    };
} // namespace gc::input

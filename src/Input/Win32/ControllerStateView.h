#pragma once

#include "Input/Types/InputTypes.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace gc::input {

struct ControllerControlDescriptor {
    DigitalControlBinding binding;
    std::string label;
};

class ControllerStateView {
public:
    virtual ~ControllerStateView() = default;

    [[nodiscard]] virtual const ControllerIdentity& identity() const noexcept = 0;
    [[nodiscard]] virtual std::span<const ControllerControlDescriptor>
    controls() const noexcept = 0;
    [[nodiscard]] virtual std::optional<double> Activation(
        const DigitalControlBinding& binding) const noexcept = 0;
    [[nodiscard]] virtual std::optional<std::int32_t> RawValue(
        const DigitalControlBinding& binding) const noexcept = 0;
};

} // namespace gc::input

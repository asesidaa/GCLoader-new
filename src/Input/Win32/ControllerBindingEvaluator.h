#pragma once

#include "Input/Types/DigitalLatch.h"
#include "Input/Types/InputTypes.h"
#include "Input/Win32/ControllerStateView.h"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace gc::input {

class ControllerBindingEvaluator {
public:
    [[nodiscard]] static std::expected<
        ControllerBindingEvaluator,
        std::string> Create(
        std::span<const DigitalControlBinding> bindings,
        std::uint32_t press_percent,
        std::uint32_t release_percent);

    [[nodiscard]] std::span<const std::uint8_t> Update(
        const ControllerStateView& view) noexcept;
    void Clear() noexcept;

private:
    std::vector<DigitalControlBinding> bindings_;
    std::vector<DigitalLatch> latches_;
    std::vector<std::uint8_t> states_;
};

} // namespace gc::input

#include "Input/Win32/ControllerBindingEvaluator.h"

#include <algorithm>

namespace gc::input
{
    std::expected<ControllerBindingEvaluator, std::string>
    ControllerBindingEvaluator::Create(
        std::span<const ControllerBinding> bindings,
        std::uint32_t press_percent,
        std::uint32_t release_percent)
    {
        const auto initial_latch = DigitalLatch::Create(
            press_percent, release_percent);
        if (!initial_latch)
        {
            return std::unexpected(initial_latch.error());
        }

        ControllerBindingEvaluator result;
        result.bindings_.assign(bindings.begin(), bindings.end());
        result.latches_.assign(bindings.size(), *initial_latch);
        result.states_.resize(bindings.size());
        return result;
    }

    std::span<const std::uint8_t> ControllerBindingEvaluator::Update(
        const ControllerStateView& view) noexcept
    {
        for (std::size_t index = 0; index < bindings_.size(); ++index)
        {
            const auto activation = view.Activation(bindings_[index]);
            if (!activation)
            {
                latches_[index].Reset();
                states_[index] = 0;
                continue;
            }
            states_[index] = latches_[index].Update(*activation) ? 1 : 0;
        }
        return states_;
    }

    void ControllerBindingEvaluator::Clear() noexcept
    {
        for (auto& latch : latches_)
        {
            latch.Reset();
        }
        std::ranges::fill(states_, 0);
    }
} // namespace gc::input

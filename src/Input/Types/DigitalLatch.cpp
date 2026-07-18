#include "Input/Types/DigitalLatch.h"

#include <algorithm>
#include <cmath>

namespace gc::input {

std::expected<DigitalLatch, std::string> DigitalLatch::Create(
    std::uint32_t press_percent,
    std::uint32_t release_percent) noexcept
{
    if (press_percent > 100 || release_percent > 100)
    {
        return std::unexpected("digital thresholds must be from 0 through 100");
    }
    if (release_percent >= press_percent)
    {
        return std::unexpected(
            "digital release threshold must be lower than press threshold");
    }

    return DigitalLatch{
        static_cast<double>(press_percent) / 100.0,
        static_cast<double>(release_percent) / 100.0};
}

DigitalLatch::DigitalLatch(
    double press_threshold,
    double release_threshold) noexcept
    : press_threshold_{press_threshold},
      release_threshold_{release_threshold}
{
}

bool DigitalLatch::Update(double activation) noexcept
{
    if (!std::isfinite(activation))
    {
        activation = 0.0;
    }
    activation = std::clamp(activation, 0.0, 1.0);

    if (pressed_)
    {
        pressed_ = activation >= release_threshold_;
    }
    else
    {
        pressed_ = activation >= press_threshold_;
    }
    return pressed_;
}

void DigitalLatch::Reset() noexcept
{
    pressed_ = false;
}

bool DigitalLatch::pressed() const noexcept
{
    return pressed_;
}

} // namespace gc::input

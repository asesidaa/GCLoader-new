#include "Input/Win32/InputCapture.h"

#include "Input/Types/DigitalLatch.h"

#include <algorithm>
#include <array>
#include <utility>

namespace gc::input {

std::expected<InputCapture, std::string> InputCapture::Create(
    std::uint32_t press_percent,
    std::uint32_t release_percent)
{
    const auto validated = DigitalLatch::Create(
        press_percent, release_percent);
    if (!validated)
    {
        return std::unexpected(validated.error());
    }
    return InputCapture(
        static_cast<double>(press_percent) / 100.0,
        static_cast<double>(release_percent) / 100.0);
}

InputCapture::InputCapture(
    double press_threshold,
    double release_threshold) noexcept
    : press_threshold_(press_threshold),
      release_threshold_(release_threshold)
{
}

void InputCapture::BeginKeyboard()
{
    ResetCapture();
    mode_ = Mode::Keyboard;
}

std::expected<void, std::string> InputCapture::BeginController(
    LogicalAction action,
    ControllerIdentity identity,
    const ControllerStateView& initial_view)
{
    ResetCapture();
    if (!IsGameplayAction(action))
    {
        return std::unexpected(
            "Controller capture requires a gameplay action");
    }
    if (initial_view.identity() != identity)
    {
        return std::unexpected(
            "Controller capture identity does not match initial view");
    }

    selected_identity_ = std::move(identity);
    for (const auto& descriptor : initial_view.controls())
    {
        if (descriptor.binding.type == DigitalControlType::RawHidValue)
        {
            const auto neutral = initial_view.RawValue(descriptor.binding);
            if (!neutral)
            {
                continue;
            }
            constexpr std::array directions{
                ControlDirection::Positive,
                ControlDirection::Negative,
            };
            for (const auto direction : directions)
            {
                auto binding = descriptor.binding;
                binding.action = action;
                binding.direction = direction;
                binding.neutral_value = *neutral;
                const double activation =
                    initial_view.Activation(binding).value_or(0.0);
                candidates_.push_back(Candidate{
                    .binding = std::move(binding),
                    .label = descriptor.label +
                        (direction == ControlDirection::Positive
                             ? " Positive"
                             : " Negative"),
                    .armed = activation < release_threshold_,
                });
            }
            continue;
        }

        auto binding = descriptor.binding;
        binding.action = action;
        const double activation =
            initial_view.Activation(binding).value_or(0.0);
        candidates_.push_back(Candidate{
            .binding = std::move(binding),
            .label = descriptor.label,
            .armed = activation < release_threshold_,
        });
    }
    mode_ = Mode::Controller;
    return {};
}

void InputCapture::OnKeyboardTransition(PhysicalKey key, bool pressed)
{
    if (key.make_code == 0)
    {
        return;
    }
    const auto existing = std::ranges::find(held_keys_, key);
    const bool was_held = existing != held_keys_.end();
    if (pressed && !was_held)
    {
        held_keys_.push_back(key);
    }
    else if (!pressed && was_held)
    {
        held_keys_.erase(existing);
    }

    if (mode_ == Mode::Keyboard && pressed && !was_held)
    {
        result_ = CaptureResult{
            .controller_identity = std::nullopt,
            .value = key,
        };
        result_label_.clear();
        mode_ = Mode::None;
    }
}

std::expected<void, std::string> InputCapture::SampleController(
    const ControllerStateView& view)
{
    if (mode_ != Mode::Controller || !selected_identity_)
    {
        return std::unexpected("Controller capture is not active");
    }
    if (view.identity() != *selected_identity_)
    {
        return std::unexpected("Controller capture identity changed");
    }

    for (auto& candidate : candidates_)
    {
        const double activation =
            view.Activation(candidate.binding).value_or(0.0);
        if (!candidate.armed)
        {
            if (activation < release_threshold_)
            {
                candidate.armed = true;
            }
            continue;
        }
        if (activation >= press_threshold_)
        {
            CompleteController(candidate);
            break;
        }
    }
    return {};
}

void InputCapture::Cancel() noexcept
{
    ResetCapture();
}

std::optional<CaptureResult> InputCapture::TakeResult()
{
    auto result = std::move(result_);
    result_.reset();
    result_label_.clear();
    return result;
}

std::string_view InputCapture::ResultLabel() const noexcept
{
    return result_label_;
}

void InputCapture::ResetCapture() noexcept
{
    mode_ = Mode::None;
    selected_identity_.reset();
    candidates_.clear();
    result_.reset();
    result_label_.clear();
}

void InputCapture::CompleteController(const Candidate& candidate)
{
    result_ = CaptureResult{
        .controller_identity = selected_identity_,
        .value = candidate.binding,
    };
    result_label_ = candidate.label;
    mode_ = Mode::None;
}

} // namespace gc::input

#pragma once

#include "Input/Types/InputTypes.h"
#include "Input/Win32/ControllerStateView.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gc::input {

struct CaptureResult {
    std::optional<ControllerIdentity> controller_identity;
    std::variant<PhysicalKey, DigitalControlBinding> value;
};

class InputCapture {
public:
    [[nodiscard]] static std::expected<InputCapture, std::string> Create(
        std::uint32_t press_percent,
        std::uint32_t release_percent);

    void BeginKeyboard();
    [[nodiscard]] std::expected<void, std::string> BeginController(
        LogicalAction action,
        ControllerIdentity identity,
        const ControllerStateView& initial_view);
    void OnKeyboardTransition(PhysicalKey key, bool pressed);
    [[nodiscard]] std::expected<void, std::string> SampleController(
        const ControllerStateView& view);
    void Cancel() noexcept;
    [[nodiscard]] std::optional<CaptureResult> TakeResult();
    [[nodiscard]] std::string_view ResultLabel() const noexcept;

private:
    enum class Mode : std::uint8_t {
        None,
        Keyboard,
        Controller,
    };

    struct Candidate {
        DigitalControlBinding binding;
        std::string label;
        bool armed{};
    };

    InputCapture(double press_threshold, double release_threshold) noexcept;
    void ResetCapture() noexcept;
    void CompleteController(const Candidate& candidate);

    double press_threshold_{};
    double release_threshold_{};
    Mode mode_{Mode::None};
    std::optional<ControllerIdentity> selected_identity_;
    std::vector<PhysicalKey> held_keys_;
    std::vector<Candidate> candidates_;
    std::optional<CaptureResult> result_;
    std::string result_label_;
};

} // namespace gc::input

#pragma once

#include "Input/Win32/ControllerStateView.h"
#include "Input/Win32/XInputApi.h"

#include <Windows.h>
#include <Xinput.h>

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gc::input {

class XInputController final : public ControllerStateView {
public:
    using Clock = std::chrono::steady_clock;

    ~XInputController() override;
    XInputController(XInputController&& other) noexcept;
    XInputController& operator=(XInputController&& other) noexcept;
    XInputController(const XInputController&) = delete;
    XInputController& operator=(const XInputController&) = delete;

    [[nodiscard]] static std::expected<XInputController, std::string> Create(
        std::uint32_t slot,
        XInputApi api);

    [[nodiscard]] std::expected<bool, std::string> Poll() noexcept;
    [[nodiscard]] std::expected<bool, std::string> PollAt(
        Clock::time_point now) noexcept;
    void RequestReconnectProbe() noexcept;
    void Clear() noexcept;

    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] std::uint32_t slot() const noexcept;
    [[nodiscard]] const std::wstring& loaded_name() const noexcept;

    [[nodiscard]] const ControllerIdentity& identity() const noexcept override;
    [[nodiscard]] std::span<const ControllerControlDescriptor>
    controls() const noexcept override;
    [[nodiscard]] std::optional<double> Activation(
        const DigitalControlBinding& binding) const noexcept override;
    [[nodiscard]] std::optional<std::int32_t> RawValue(
        const DigitalControlBinding& binding) const noexcept override;

private:
    XInputController() = default;

    void MoveFrom(XInputController&& other) noexcept;
    [[nodiscard]] bool HasDescriptor(
        const DigitalControlBinding& binding) const noexcept;

    ControllerIdentity identity_;
    std::uint32_t slot_{};
    XInputApi api_{};
    XINPUT_STATE state_{};
    std::vector<ControllerControlDescriptor> descriptors_;
    std::optional<Clock::time_point> last_disconnected_probe_;
    bool connected_{};
    bool packet_initialized_{};
    bool reconnect_probe_requested_{true};
};

} // namespace gc::input

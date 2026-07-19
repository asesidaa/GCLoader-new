#pragma once

#include "Input/Win32/ControllerCatalog.h"
#include "Input/Win32/ControllerStateView.h"
#include "Input/Win32/HidApi.h"

#include <Windows.h>
#include <hidpi.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gc::input {

class RawHidController final : public ControllerStateView {
public:
    RawHidController(RawHidController&&) noexcept = default;
    RawHidController& operator=(RawHidController&&) noexcept = default;
    RawHidController(const RawHidController&) = delete;
    RawHidController& operator=(const RawHidController&) = delete;

    [[nodiscard]] static std::expected<RawHidController, std::string> Open(
        const RawHidDeviceInfo& device,
        HidApi api = ProductionHidApi());

    [[nodiscard]] std::expected<bool, std::string> Apply(
        HANDLE source_device,
        const RAWHID& packet);
    void Clear() noexcept;

    [[nodiscard]] std::expected<void, std::string> ValidateBinding(
        const DigitalControlBinding& binding) const;

    [[nodiscard]] const ControllerIdentity& identity() const noexcept override;
    [[nodiscard]] std::span<const ControllerControlDescriptor>
    controls() const noexcept override;
    [[nodiscard]] std::optional<double> Activation(
        const DigitalControlBinding& binding) const noexcept override;
    [[nodiscard]] std::optional<std::int32_t> RawValue(
        const DigitalControlBinding& binding) const noexcept override;

private:
    enum class ControlKind : std::uint8_t {
        Button,
        Value,
        Hat,
    };

    struct ControlState {
        ControlKind kind{};
        std::uint16_t usage_page{};
        std::uint16_t usage{};
        std::uint16_t link_collection{};
        std::uint8_t report_id{};
        std::uint16_t bit_size{};
        std::int32_t logical_min{};
        std::int32_t logical_max{};
        std::int32_t value{};
        bool has_null{};
        bool valid{};
        bool pressed{};
    };

    RawHidController() = default;

    [[nodiscard]] std::expected<bool, std::string> ApplyReport(
        std::span<const std::byte> report);
    [[nodiscard]] const ControlState* FindState(
        const DigitalControlBinding& binding) const noexcept;
    [[nodiscard]] ControlState* FindState(
        const DigitalControlBinding& binding) noexcept;

    ControllerIdentity identity_;
    HANDLE raw_device_{};
    HidApi api_{};
    HIDP_CAPS caps_{};
    std::vector<std::byte> preparsed_data_;
    std::vector<ControlState> states_;
    std::vector<ControllerControlDescriptor> descriptors_;
    std::vector<USAGE> button_scratch_;
    bool has_numbered_reports_{};
};

} // namespace gc::input

#include "Input/Win32/RawHidController.h"

#include "Input/Win32/RawInputPacket.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace gc::input {
namespace {

constexpr std::uint16_t kGenericDesktopPage = 0x01;
constexpr std::uint16_t kHatSwitchUsage = 0x39;

std::string Win32Failure(const char* operation)
{
    return std::string(operation) + " failed with Win32 error " +
        std::to_string(GetLastError());
}

std::string HidFailure(const char* operation, NTSTATUS status)
{
    return std::string(operation) + " failed with HID status " +
        std::to_string(static_cast<std::uint32_t>(status));
}

bool HidApiComplete(const HidApi& api) noexcept
{
    return api.get_raw_input_device_info != nullptr &&
        api.get_caps != nullptr &&
        api.get_button_caps != nullptr &&
        api.get_value_caps != nullptr &&
        api.get_usages != nullptr &&
        api.get_usage_value != nullptr;
}

std::pair<std::uint32_t, std::uint32_t> UsageRange(
    const HIDP_BUTTON_CAPS& cap)
{
    if (cap.IsRange)
    {
        return {cap.Range.UsageMin, cap.Range.UsageMax};
    }
    return {cap.NotRange.Usage, cap.NotRange.Usage};
}

std::pair<std::uint32_t, std::uint32_t> UsageRange(
    const HIDP_VALUE_CAPS& cap)
{
    if (cap.IsRange)
    {
        return {cap.Range.UsageMin, cap.Range.UsageMax};
    }
    return {cap.NotRange.Usage, cap.NotRange.Usage};
}

DigitalControlBinding AddressBinding(
    DigitalControlType type,
    std::uint16_t usage_page,
    std::uint16_t usage,
    std::uint16_t link_collection,
    std::uint8_t report_id)
{
    return DigitalControlBinding{
        .type = type,
        .usage_page = usage_page,
        .usage = usage,
        .link_collection = link_collection,
        .report_id = report_id,
    };
}

std::string AddressLabel(
    const char* family,
    std::uint16_t usage_page,
    std::uint16_t usage,
    std::uint16_t link_collection,
    std::uint8_t report_id)
{
    return std::string("HID ") + family + " " +
        std::to_string(usage_page) + ":" + std::to_string(usage) +
        " link " + std::to_string(link_collection) +
        " report " + std::to_string(report_id);
}

std::int32_t DecodeLogicalValue(
    ULONG raw,
    std::uint16_t bit_size,
    std::int32_t logical_min) noexcept
{
    if (bit_size == 0 || bit_size >= 32)
    {
        return static_cast<std::int32_t>(raw);
    }

    const std::uint32_t mask = (1u << bit_size) - 1u;
    std::uint32_t value = raw & mask;
    if (logical_min < 0)
    {
        const std::uint32_t sign_bit = 1u << (bit_size - 1u);
        if ((value & sign_bit) != 0)
        {
            value |= ~mask;
        }
    }
    return static_cast<std::int32_t>(value);
}

bool IsCardinal(ControlDirection direction) noexcept
{
    return direction == ControlDirection::Up ||
        direction == ControlDirection::Right ||
        direction == ControlDirection::Down ||
        direction == ControlDirection::Left;
}

const char* DirectionLabel(ControlDirection direction) noexcept
{
    switch (direction)
    {
    case ControlDirection::Up:
        return "Up";
    case ControlDirection::Right:
        return "Right";
    case ControlDirection::Down:
        return "Down";
    case ControlDirection::Left:
        return "Left";
    default:
        return "";
    }
}

double HatActivation(
    bool valid,
    std::int32_t value,
    std::int32_t logical_min,
    std::int32_t logical_max,
    ControlDirection direction) noexcept
{
    if (!valid || value < logical_min || value > logical_max ||
        !IsCardinal(direction))
    {
        return 0.0;
    }

    const std::int64_t position =
        static_cast<std::int64_t>(value) - logical_min;
    const std::int64_t count =
        static_cast<std::int64_t>(logical_max) - logical_min + 1;
    if (count <= 0)
    {
        return 0.0;
    }

    if (count <= 4)
    {
        const std::array directions{
            ControlDirection::Up,
            ControlDirection::Right,
            ControlDirection::Down,
            ControlDirection::Left,
        };
        const auto index = static_cast<std::size_t>(
            std::min<std::int64_t>(position, 3));
        return directions[index] == direction ? 1.0 : 0.0;
    }

    const std::int64_t sector = std::min<std::int64_t>(
        position * 8 / count,
        7);
    switch (direction)
    {
    case ControlDirection::Up:
        return sector == 7 || sector <= 1 ? 1.0 : 0.0;
    case ControlDirection::Right:
        return sector >= 1 && sector <= 3 ? 1.0 : 0.0;
    case ControlDirection::Down:
        return sector >= 3 && sector <= 5 ? 1.0 : 0.0;
    case ControlDirection::Left:
        return sector >= 5 && sector <= 7 ? 1.0 : 0.0;
    default:
        return 0.0;
    }
}

} // namespace

std::expected<RawHidController, std::string> RawHidController::Open(
    const RawHidDeviceInfo& device,
    HidApi api)
{
    if (device.raw_device == nullptr || device.device_path.empty())
    {
        return std::unexpected("Raw HID device identity is incomplete");
    }
    if (!HidApiComplete(api))
    {
        return std::unexpected("HID API table is incomplete");
    }

    RawHidController controller;
    controller.identity_ = ControllerIdentity{
        .backend = ControllerBackend::RawHid,
        .device_id = device.device_path,
    };
    controller.raw_device_ = device.raw_device;
    controller.api_ = api;

    UINT preparsed_size = 0;
    if (api.get_raw_input_device_info(
            device.raw_device,
            RIDI_PREPARSEDDATA,
            nullptr,
            &preparsed_size) == UINT_MAX)
    {
        return std::unexpected(
            Win32Failure("GetRawInputDeviceInfoW(preparsed size)"));
    }
    if (preparsed_size == 0)
    {
        return std::unexpected("Raw HID preparsed data is empty");
    }

    controller.preparsed_data_.resize(preparsed_size);
    UINT writable_size = preparsed_size;
    if (api.get_raw_input_device_info(
            device.raw_device,
            RIDI_PREPARSEDDATA,
            controller.preparsed_data_.data(),
            &writable_size) == UINT_MAX)
    {
        return std::unexpected(
            Win32Failure("GetRawInputDeviceInfoW(preparsed data)"));
    }
    if (writable_size == 0 || writable_size > controller.preparsed_data_.size())
    {
        return std::unexpected("Raw HID preparsed data changed size");
    }
    controller.preparsed_data_.resize(writable_size);

    auto* preparsed = reinterpret_cast<PHIDP_PREPARSED_DATA>(
        controller.preparsed_data_.data());
    NTSTATUS status = api.get_caps(preparsed, &controller.caps_);
    if (status != HIDP_STATUS_SUCCESS)
    {
        return std::unexpected(HidFailure("HidP_GetCaps", status));
    }
    if (controller.caps_.InputReportByteLength == 0)
    {
        return std::unexpected("Raw HID input report length is zero");
    }

    std::vector<HIDP_BUTTON_CAPS> button_caps(
        controller.caps_.NumberInputButtonCaps);
    if (!button_caps.empty())
    {
        USHORT count = static_cast<USHORT>(button_caps.size());
        status = api.get_button_caps(
            HidP_Input,
            button_caps.data(),
            &count,
            preparsed);
        if (status != HIDP_STATUS_SUCCESS)
        {
            return std::unexpected(
                HidFailure("HidP_GetButtonCaps", status));
        }
        button_caps.resize(count);
    }

    std::vector<HIDP_VALUE_CAPS> value_caps(
        controller.caps_.NumberInputValueCaps);
    if (!value_caps.empty())
    {
        USHORT count = static_cast<USHORT>(value_caps.size());
        status = api.get_value_caps(
            HidP_Input,
            value_caps.data(),
            &count,
            preparsed);
        if (status != HIDP_STATUS_SUCCESS)
        {
            return std::unexpected(
                HidFailure("HidP_GetValueCaps", status));
        }
        value_caps.resize(count);
    }

    for (const auto& cap : button_caps)
    {
        if (cap.IsAlias)
        {
            continue;
        }
        const auto [first, last] = UsageRange(cap);
        if (first > last || last > std::numeric_limits<std::uint16_t>::max())
        {
            return std::unexpected("Raw HID button usage range is invalid");
        }
        for (std::uint32_t usage = first; usage <= last; ++usage)
        {
            controller.states_.push_back(ControlState{
                .kind = ControlKind::Button,
                .usage_page = cap.UsagePage,
                .usage = static_cast<std::uint16_t>(usage),
                .link_collection = cap.LinkCollection,
                .report_id = cap.ReportID,
            });
            controller.descriptors_.push_back(ControllerControlDescriptor{
                .binding = AddressBinding(
                    DigitalControlType::RawHidButton,
                    cap.UsagePage,
                    static_cast<std::uint16_t>(usage),
                    cap.LinkCollection,
                    cap.ReportID),
                .label = AddressLabel(
                    "Button",
                    cap.UsagePage,
                    static_cast<std::uint16_t>(usage),
                    cap.LinkCollection,
                    cap.ReportID),
            });
            controller.has_numbered_reports_ |= cap.ReportID != 0;
        }
    }

    for (const auto& cap : value_caps)
    {
        if (cap.IsAlias || cap.BitSize == 0 || cap.BitSize > 32 ||
            cap.LogicalMin > cap.LogicalMax || cap.ReportCount > 1)
        {
            continue;
        }
        const auto [first, last] = UsageRange(cap);
        if (first > last || last > std::numeric_limits<std::uint16_t>::max())
        {
            return std::unexpected("Raw HID value usage range is invalid");
        }
        for (std::uint32_t usage = first; usage <= last; ++usage)
        {
            const bool is_hat = cap.UsagePage == kGenericDesktopPage &&
                usage == kHatSwitchUsage;
            const auto type = is_hat
                ? DigitalControlType::RawHidHat
                : DigitalControlType::RawHidValue;
            controller.states_.push_back(ControlState{
                .kind = is_hat ? ControlKind::Hat : ControlKind::Value,
                .usage_page = cap.UsagePage,
                .usage = static_cast<std::uint16_t>(usage),
                .link_collection = cap.LinkCollection,
                .report_id = cap.ReportID,
                .bit_size = cap.BitSize,
                .logical_min = cap.LogicalMin,
                .logical_max = cap.LogicalMax,
                .has_null = cap.HasNull != FALSE,
            });

            auto binding = AddressBinding(
                type,
                cap.UsagePage,
                static_cast<std::uint16_t>(usage),
                cap.LinkCollection,
                cap.ReportID);
            const auto label = AddressLabel(
                is_hat ? "Hat" : "Value",
                cap.UsagePage,
                static_cast<std::uint16_t>(usage),
                cap.LinkCollection,
                cap.ReportID);
            if (is_hat)
            {
                constexpr std::array directions{
                    ControlDirection::Up,
                    ControlDirection::Right,
                    ControlDirection::Down,
                    ControlDirection::Left,
                };
                for (const auto direction : directions)
                {
                    binding.direction = direction;
                    controller.descriptors_.push_back(
                        ControllerControlDescriptor{
                            .binding = binding,
                            .label = label + " " + DirectionLabel(direction),
                        });
                }
            }
            else
            {
                controller.descriptors_.push_back(ControllerControlDescriptor{
                    .binding = binding,
                    .label = label,
                });
            }
            controller.has_numbered_reports_ |= cap.ReportID != 0;
        }
    }

    const std::size_t button_count = static_cast<std::size_t>(std::ranges::count_if(
        controller.states_,
        [](const ControlState& state) {
            return state.kind == ControlKind::Button;
        }));
    controller.button_scratch_.resize(std::max<std::size_t>(button_count, 1));
    return controller;
}

std::expected<bool, std::string> RawHidController::Apply(
    HANDLE source_device,
    const RAWHID& packet)
{
    if (source_device != raw_device_)
    {
        return false;
    }
    if (packet.dwSizeHid != caps_.InputReportByteLength)
    {
        Clear();
        return std::unexpected("Raw HID report length does not match descriptor");
    }

    const auto reports = HidReports(packet);
    if (!reports)
    {
        Clear();
        return std::unexpected(reports.error());
    }

    bool changed = false;
    for (std::size_t index = 0; index < reports->size(); ++index)
    {
        const auto applied = ApplyReport((*reports)[index]);
        if (!applied)
        {
            Clear();
            return std::unexpected(applied.error());
        }
        changed |= *applied;
    }
    return changed;
}

void RawHidController::Clear() noexcept
{
    for (auto& state : states_)
    {
        state.pressed = false;
        state.valid = false;
        state.value = 0;
    }
}

std::expected<void, std::string> RawHidController::ValidateBinding(
    const DigitalControlBinding& binding) const
{
    const auto* state = FindState(binding);
    if (state == nullptr)
    {
        return std::unexpected("Raw HID binding address is unavailable");
    }
    if (state->kind == ControlKind::Value)
    {
        if (!binding.direction ||
            (*binding.direction != ControlDirection::Positive &&
             *binding.direction != ControlDirection::Negative) ||
            !binding.neutral_value)
        {
            return std::unexpected(
                "Raw HID value binding requires a direction and neutral");
        }
        if (*binding.neutral_value < state->logical_min ||
            *binding.neutral_value > state->logical_max)
        {
            return std::unexpected(
                "Raw HID value neutral is outside its logical range");
        }
    }
    if (state->kind == ControlKind::Hat &&
        (!binding.direction || !IsCardinal(*binding.direction)))
    {
        return std::unexpected(
            "Raw HID hat binding requires a cardinal direction");
    }
    return {};
}

const ControllerIdentity& RawHidController::identity() const noexcept
{
    return identity_;
}

std::span<const ControllerControlDescriptor>
RawHidController::controls() const noexcept
{
    return descriptors_;
}

std::optional<double> RawHidController::Activation(
    const DigitalControlBinding& binding) const noexcept
{
    const auto* state = FindState(binding);
    if (state == nullptr)
    {
        return std::nullopt;
    }
    if (state->kind == ControlKind::Button)
    {
        return state->pressed ? 1.0 : 0.0;
    }
    if (state->kind == ControlKind::Hat)
    {
        if (!binding.direction)
        {
            return std::nullopt;
        }
        return HatActivation(
            state->valid,
            state->value,
            state->logical_min,
            state->logical_max,
            *binding.direction);
    }
    if (!binding.direction || !binding.neutral_value)
    {
        return std::nullopt;
    }
    if (!state->valid)
    {
        return 0.0;
    }
    if (state->has_null &&
        (state->value < state->logical_min ||
         state->value > state->logical_max))
    {
        return 0.0;
    }

    const std::int64_t value = state->value;
    const std::int64_t neutral = *binding.neutral_value;
    if (neutral < state->logical_min || neutral > state->logical_max)
    {
        return std::nullopt;
    }
    double normalized = 0.0;
    if (*binding.direction == ControlDirection::Positive &&
        state->logical_max > neutral && value > neutral)
    {
        normalized = static_cast<double>(value - neutral) /
            static_cast<double>(
                static_cast<std::int64_t>(state->logical_max) - neutral);
    }
    else if (*binding.direction == ControlDirection::Negative &&
             state->logical_min < neutral && value < neutral)
    {
        normalized = static_cast<double>(neutral - value) /
            static_cast<double>(
                neutral - static_cast<std::int64_t>(state->logical_min));
    }
    else if (*binding.direction != ControlDirection::Positive &&
             *binding.direction != ControlDirection::Negative)
    {
        return std::nullopt;
    }
    return std::clamp(normalized, 0.0, 1.0);
}

std::optional<std::int32_t> RawHidController::RawValue(
    const DigitalControlBinding& binding) const noexcept
{
    const auto* state = FindState(binding);
    if (state == nullptr || state->kind != ControlKind::Value || !state->valid)
    {
        return std::nullopt;
    }
    return state->value;
}

std::expected<bool, std::string> RawHidController::ApplyReport(
    std::span<const std::byte> report)
{
    if (report.size() != caps_.InputReportByteLength)
    {
        return std::unexpected("Raw HID report is truncated");
    }
    const std::uint8_t report_id = has_numbered_reports_
        ? std::to_integer<std::uint8_t>(report.front())
        : 0;
    auto* preparsed = reinterpret_cast<PHIDP_PREPARSED_DATA>(
        preparsed_data_.data());
    auto* report_data = reinterpret_cast<PCHAR>(
        const_cast<std::byte*>(report.data()));

    bool changed = false;
    for (auto& state : states_)
    {
        if (state.report_id != report_id)
        {
            continue;
        }

        if (state.kind == ControlKind::Button)
        {
            ULONG usage_count = static_cast<ULONG>(button_scratch_.size());
            const NTSTATUS status = api_.get_usages(
                HidP_Input,
                state.usage_page,
                state.link_collection,
                button_scratch_.data(),
                &usage_count,
                preparsed,
                report_data,
                static_cast<ULONG>(report.size()));
            if (status != HIDP_STATUS_SUCCESS)
            {
                return std::unexpected(HidFailure("HidP_GetUsages", status));
            }
            if (usage_count > button_scratch_.size())
            {
                return std::unexpected(
                    "HidP_GetUsages returned an invalid usage count");
            }
            const bool pressed = std::find(
                button_scratch_.begin(),
                button_scratch_.begin() + usage_count,
                state.usage) != button_scratch_.begin() + usage_count;
            changed |= state.pressed != pressed || !state.valid;
            state.pressed = pressed;
            state.valid = true;
            continue;
        }

        ULONG raw_value = 0;
        const NTSTATUS status = api_.get_usage_value(
            HidP_Input,
            state.usage_page,
            state.link_collection,
            state.usage,
            &raw_value,
            preparsed,
            report_data,
            static_cast<ULONG>(report.size()));
        if (status != HIDP_STATUS_SUCCESS)
        {
            return std::unexpected(
                HidFailure("HidP_GetUsageValue", status));
        }
        const std::int32_t value = DecodeLogicalValue(
            raw_value,
            state.bit_size,
            state.logical_min);
        if (!state.has_null &&
            (value < state.logical_min || value > state.logical_max))
        {
            return std::unexpected(
                "Raw HID value is outside its declared logical range");
        }
        changed |= state.value != value || !state.valid;
        state.value = value;
        state.valid = true;
    }
    return changed;
}

const RawHidController::ControlState* RawHidController::FindState(
    const DigitalControlBinding& binding) const noexcept
{
    if (!binding.usage_page || !binding.usage ||
        !binding.link_collection || !binding.report_id ||
        *binding.usage_page > std::numeric_limits<std::uint16_t>::max() ||
        *binding.usage > std::numeric_limits<std::uint16_t>::max() ||
        *binding.link_collection > std::numeric_limits<std::uint16_t>::max() ||
        *binding.report_id > std::numeric_limits<std::uint8_t>::max())
    {
        return nullptr;
    }

    const ControlKind expected_kind = [&] {
        switch (binding.type)
        {
        case DigitalControlType::RawHidButton:
            return ControlKind::Button;
        case DigitalControlType::RawHidValue:
            return ControlKind::Value;
        case DigitalControlType::RawHidHat:
            return ControlKind::Hat;
        default:
            return static_cast<ControlKind>(0xff);
        }
    }();
    for (const auto& state : states_)
    {
        if (state.kind == expected_kind &&
            state.usage_page == *binding.usage_page &&
            state.usage == *binding.usage &&
            state.link_collection == *binding.link_collection &&
            state.report_id == *binding.report_id)
        {
            return &state;
        }
    }
    return nullptr;
}

RawHidController::ControlState* RawHidController::FindState(
    const DigitalControlBinding& binding) noexcept
{
    return const_cast<ControlState*>(
        std::as_const(*this).FindState(binding));
}

} // namespace gc::input

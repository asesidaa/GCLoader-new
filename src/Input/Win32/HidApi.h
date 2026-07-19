#pragma once

#include <Windows.h>
#include <hidsdi.h>

namespace gc::input {

struct HidApi {
    decltype(&GetRawInputDeviceInfoW) get_raw_input_device_info{};
    decltype(&HidP_GetCaps) get_caps{};
    decltype(&HidP_GetButtonCaps) get_button_caps{};
    decltype(&HidP_GetValueCaps) get_value_caps{};
    decltype(&HidP_GetUsages) get_usages{};
    decltype(&HidP_GetUsageValue) get_usage_value{};
};

[[nodiscard]] HidApi ProductionHidApi() noexcept;

} // namespace gc::input

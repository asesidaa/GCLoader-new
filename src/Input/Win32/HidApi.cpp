#include "Input/Win32/HidApi.h"

#include <type_traits>

namespace gc::input {

static_assert(std::is_trivially_copyable_v<HidApi>);

HidApi ProductionHidApi() noexcept
{
    return {
        .get_raw_input_device_info = ::GetRawInputDeviceInfoW,
        .get_caps = ::HidP_GetCaps,
        .get_button_caps = ::HidP_GetButtonCaps,
        .get_value_caps = ::HidP_GetValueCaps,
        .get_usages = ::HidP_GetUsages,
        .get_usage_value = ::HidP_GetUsageValue,
    };
}

} // namespace gc::input

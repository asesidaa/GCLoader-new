#pragma once

#include <Windows.h>

#include <expected>
#include <string>

namespace gc::input {

[[nodiscard]] std::expected<void, std::string>
InstallRawInputRegistrationGuard();

BOOL WINAPI RegisterOwnedRawInputDevices(
    PCRAWINPUTDEVICE devices,
    UINT device_count,
    UINT device_size) noexcept;

} // namespace gc::input

#pragma once

#include <Windows.h>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gc::input {

struct RawHidDeviceInfo {
    HANDLE raw_device{};
    std::string device_path;
    std::wstring product_name;
    std::uint16_t vendor_id{};
    std::uint16_t product_id{};
    std::uint16_t usage_page{};
    std::uint16_t usage{};
};

[[nodiscard]] std::expected<std::vector<RawHidDeviceInfo>, std::string>
EnumerateRawHidDevices();
[[nodiscard]] bool IsXInputShadowPath(std::string_view path) noexcept;
[[nodiscard]] bool IsRawHidControllerUsage(
    std::uint16_t usage_page,
    std::uint16_t usage) noexcept;
[[nodiscard]] const RawHidDeviceInfo* FindExactRawHidDevice(
    std::span<const RawHidDeviceInfo> devices,
    std::string_view configured_path) noexcept;

} // namespace gc::input

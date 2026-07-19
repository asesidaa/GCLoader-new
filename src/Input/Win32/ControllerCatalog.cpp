#include "Input/Win32/ControllerCatalog.h"

#include <hidsdi.h>

#include <algorithm>
#include <array>
#include <limits>

namespace gc::input {
namespace {

std::string Win32Failure(const char* operation)
{
    return std::string(operation) + " failed with Win32 error " +
        std::to_string(GetLastError());
}

std::expected<std::string, std::string> WideToUtf8(std::wstring_view value)
{
    if (value.empty())
    {
        return std::string{};
    }
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return std::unexpected("Raw HID device path is too long");
    }

    const int source_size = static_cast<int>(value.size());
    const int byte_count = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        source_size,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (byte_count == 0)
    {
        return std::unexpected(Win32Failure("WideCharToMultiByte(size)"));
    }

    std::string result(byte_count, '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            source_size,
            result.data(),
            byte_count,
            nullptr,
            nullptr) != byte_count)
    {
        return std::unexpected(Win32Failure("WideCharToMultiByte(data)"));
    }
    return result;
}

std::expected<std::wstring, std::string> Utf8ToWide(std::string_view value)
{
    if (value.empty())
    {
        return std::wstring{};
    }
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return std::unexpected("Configured Raw HID path is too long");
    }

    const int source_size = static_cast<int>(value.size());
    const int character_count = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        source_size,
        nullptr,
        0);
    if (character_count == 0)
    {
        return std::unexpected(Win32Failure("MultiByteToWideChar(size)"));
    }

    std::wstring result(character_count, L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            source_size,
            result.data(),
            character_count) != character_count)
    {
        return std::unexpected(Win32Failure("MultiByteToWideChar(data)"));
    }
    return result;
}

std::expected<std::wstring, std::string> DevicePath(HANDLE device)
{
    UINT character_count = 0;
    if (GetRawInputDeviceInfoW(
            device,
            RIDI_DEVICENAME,
            nullptr,
            &character_count) == UINT_MAX)
    {
        return std::unexpected(
            Win32Failure("GetRawInputDeviceInfoW(path size)"));
    }
    if (character_count == 0)
    {
        return std::unexpected("Raw HID device has an empty path");
    }

    std::wstring path(character_count, L'\0');
    UINT writable_count = character_count;
    if (GetRawInputDeviceInfoW(
            device,
            RIDI_DEVICENAME,
            path.data(),
            &writable_count) == UINT_MAX)
    {
        return std::unexpected(
            Win32Failure("GetRawInputDeviceInfoW(path data)"));
    }
    path.resize(writable_count);
    if (!path.empty() && path.back() == L'\0')
    {
        path.pop_back();
    }
    return path;
}

std::wstring ProductName(const std::wstring& path)
{
    const HANDLE device = CreateFileW(
        path.c_str(),
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (device == INVALID_HANDLE_VALUE)
    {
        return {};
    }

    std::array<wchar_t, 256> name{};
    const BOOLEAN succeeded = HidD_GetProductString(
        device,
        name.data(),
        static_cast<ULONG>(name.size() * sizeof(wchar_t)));
    CloseHandle(device);
    return succeeded ? std::wstring(name.data()) : std::wstring{};
}

bool EqualWindowsPaths(std::string_view left, std::string_view right) noexcept
{
    try
    {
        const auto left_wide = Utf8ToWide(left);
        const auto right_wide = Utf8ToWide(right);
        if (!left_wide || !right_wide ||
            left_wide->size() > static_cast<std::size_t>(INT_MAX) ||
            right_wide->size() > static_cast<std::size_t>(INT_MAX))
        {
            return false;
        }
        return CompareStringOrdinal(
                   left_wide->data(),
                   static_cast<int>(left_wide->size()),
                   right_wide->data(),
                   static_cast<int>(right_wide->size()),
                   TRUE) == CSTR_EQUAL;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace

std::expected<std::vector<RawHidDeviceInfo>, std::string>
EnumerateRawHidDevices()
{
    UINT count = 0;
    if (GetRawInputDeviceList(
            nullptr, &count, sizeof(RAWINPUTDEVICELIST)) == UINT_MAX)
    {
        return std::unexpected(Win32Failure("GetRawInputDeviceList(size)"));
    }

    std::vector<RAWINPUTDEVICELIST> raw_devices(count);
    UINT listed = count;
    const UINT list_result = count == 0
        ? 0
        : GetRawInputDeviceList(
              raw_devices.data(),
              &listed,
              sizeof(RAWINPUTDEVICELIST));
    if (list_result == UINT_MAX)
    {
        return std::unexpected(Win32Failure("GetRawInputDeviceList(data)"));
    }
    raw_devices.resize(list_result);

    std::vector<RawHidDeviceInfo> result;
    for (const auto& raw_device : raw_devices)
    {
        if (raw_device.dwType != RIM_TYPEHID)
        {
            continue;
        }

        RID_DEVICE_INFO info{};
        info.cbSize = sizeof(RID_DEVICE_INFO);
        UINT info_size = sizeof(RID_DEVICE_INFO);
        if (GetRawInputDeviceInfoW(
                raw_device.hDevice,
                RIDI_DEVICEINFO,
                &info,
                &info_size) == UINT_MAX)
        {
            continue;
        }
        if (!IsRawHidControllerUsage(
                info.hid.usUsagePage,
                info.hid.usUsage))
        {
            continue;
        }

        const auto wide_path = DevicePath(raw_device.hDevice);
        if (!wide_path)
        {
            continue;
        }
        const auto utf8_path = WideToUtf8(*wide_path);
        if (!utf8_path)
        {
            return std::unexpected(utf8_path.error());
        }
        if (IsXInputShadowPath(*utf8_path))
        {
            continue;
        }

        result.push_back(RawHidDeviceInfo{
            .raw_device = raw_device.hDevice,
            .device_path = *utf8_path,
            .product_name = ProductName(*wide_path),
            .vendor_id = static_cast<std::uint16_t>(info.hid.dwVendorId),
            .product_id = static_cast<std::uint16_t>(info.hid.dwProductId),
            .usage_page = info.hid.usUsagePage,
            .usage = info.hid.usUsage,
        });
    }
    return result;
}

bool IsXInputShadowPath(std::string_view path) noexcept
{
    for (std::size_t index = 0; index + 2 < path.size(); ++index)
    {
        const char first = path[index];
        const char second = path[index + 1];
        if ((first == 'I' || first == 'i') &&
            (second == 'G' || second == 'g') &&
            path[index + 2] == '_')
        {
            return true;
        }
    }
    return false;
}

bool IsRawHidControllerUsage(
    std::uint16_t usage_page,
    std::uint16_t usage) noexcept
{
    return usage_page == 0x01 &&
        (usage == 0x05 || usage == 0x04 || usage == 0x08);
}

const RawHidDeviceInfo* FindExactRawHidDevice(
    std::span<const RawHidDeviceInfo> devices,
    std::string_view configured_path) noexcept
{
    for (const auto& device : devices)
    {
        if (EqualWindowsPaths(device.device_path, configured_path))
        {
            return &device;
        }
    }
    return nullptr;
}

} // namespace gc::input

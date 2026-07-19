#include "Input/Win32/ControllerCatalog.h"

#include <Windows.h>

#include <iostream>
#include <string_view>
#include <vector>

namespace {

int expect_true(bool actual, std::string_view name)
{
    if (actual)
    {
        return 0;
    }
    std::cerr << name << ": expected true\n";
    return 1;
}

} // namespace

int main()
{
    using namespace gc::input;

    int failures = 0;
    failures += expect_true(
        IsXInputShadowPath(R"(\\?\HID#VID_045E&PID_028E&IG_00#1)"),
        "uppercase XInput shadow marker");
    failures += expect_true(
        IsXInputShadowPath(R"(\\?\hid#vid_045e&pid_028e&ig_00#1)"),
        "lowercase XInput shadow marker");
    failures += expect_true(
        !IsXInputShadowPath(R"(\\?\HID#VID_1234&PID_5678#1)"),
        "ordinary HID device retained");

    failures += expect_true(
        IsRawHidControllerUsage(0x01, 0x05),
        "gamepad usage accepted");
    failures += expect_true(
        IsRawHidControllerUsage(0x01, 0x04),
        "joystick usage accepted");
    failures += expect_true(
        IsRawHidControllerUsage(0x01, 0x08),
        "multi-axis usage accepted");
    failures += expect_true(
        !IsRawHidControllerUsage(0x01, 0x06),
        "keyboard usage rejected");
    failures += expect_true(
        !IsRawHidControllerUsage(0x0c, 0x05),
        "wrong usage page rejected");

    std::vector<RawHidDeviceInfo> devices{
        {
            .raw_device = reinterpret_cast<HANDLE>(1),
            .device_path = R"(\\?\HID#VID_1234&PID_ABCD#ONE)",
            .product_name = L"Arcade Controller",
            .vendor_id = 0x1234,
            .product_id = 0xabcd,
            .usage_page = 0x01,
            .usage = 0x05,
        },
        {
            .raw_device = reinterpret_cast<HANDLE>(2),
            .device_path = R"(\\?\HID#VID_9876&PID_5432#TWO)",
            .product_name = L"Second Controller",
            .vendor_id = 0x9876,
            .product_id = 0x5432,
            .usage_page = 0x01,
            .usage = 0x04,
        },
    };

    const auto* exact = FindExactRawHidDevice(
        devices,
        R"(\\?\hid#vid_1234&pid_abcd#one)");
    failures += expect_true(
        exact != nullptr && exact->raw_device == reinterpret_cast<HANDLE>(1),
        "exact path matches case-insensitively");
    failures += expect_true(
        FindExactRawHidDevice(devices, R"(VID_1234&PID_ABCD)") == nullptr,
        "path substring does not match");
    failures += expect_true(
        FindExactRawHidDevice(devices, "Arcade Controller") == nullptr,
        "friendly name does not match");
    failures += expect_true(
        FindExactRawHidDevice(
            devices,
            R"(\\?\HID#VID_1234&PID_ABCD#ONE\EXTRA)") == nullptr,
        "path prefix does not match");

    if (failures != 0)
    {
        return 1;
    }

    std::cout << "ControllerCatalogTests passed\n";
    return 0;
}

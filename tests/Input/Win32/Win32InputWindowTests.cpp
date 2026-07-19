#include "Input/Win32/Win32InputWindow.h"

#include <Windows.h>

#include <array>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

class RecordingSink final : public gc::input::RawInputMessageSink {
public:
    void OnRawInput(HRAWINPUT raw_input) noexcept override
    {
        last_raw_input = raw_input;
        ++raw_input_count;
    }

    void OnRawInputDeviceChange(WPARAM event, HANDLE device) noexcept override
    {
        last_device_event = event;
        last_device = device;
        ++device_change_count;
    }

    HRAWINPUT last_raw_input{};
    HANDLE last_device{};
    WPARAM last_device_event{};
    int raw_input_count{};
    int device_change_count{};
};

int expect_true(bool actual, std::string_view name)
{
    if (actual)
    {
        return 0;
    }
    std::cerr << name << ": expected true\n";
    return 1;
}

std::vector<RAWINPUTDEVICE> registered_devices()
{
    UINT count = 0;
    if (GetRegisteredRawInputDevices(
            nullptr, &count, sizeof(RAWINPUTDEVICE)) == UINT_MAX)
    {
        return {};
    }

    std::vector<RAWINPUTDEVICE> devices(count);
    if (count != 0 && GetRegisteredRawInputDevices(
            devices.data(), &count, sizeof(RAWINPUTDEVICE)) == UINT_MAX)
    {
        return {};
    }
    devices.resize(count);
    return devices;
}

const RAWINPUTDEVICE* find_registration(
    const std::vector<RAWINPUTDEVICE>& devices,
    USHORT usage)
{
    for (const auto& device : devices)
    {
        if (device.usUsagePage == 0x01 && device.usUsage == usage)
        {
            return &device;
        }
    }
    return nullptr;
}

} // namespace

int main()
{
    using namespace gc::input;

    int failures = 0;
    RecordingSink sink;
    Win32InputWindow window(sink);
    const auto created = window.Create(GetModuleHandleW(nullptr));
    failures += expect_true(created.has_value(), "window creation");
    if (!created)
    {
        std::cerr << created.error() << '\n';
        return 1;
    }

    const HWND hwnd = window.hwnd();
    failures += expect_true(hwnd != nullptr, "non-null HWND");
    failures += expect_true(
        window.owner_thread_id() == GetCurrentThreadId(),
        "owner thread recorded");
    failures += expect_true(IsWindow(hwnd) != FALSE, "valid HWND");
    const HWND root = GetAncestor(hwnd, GA_ROOT);
    const HWND ancestor_parent = GetAncestor(hwnd, GA_PARENT);
    const HWND parent = GetParent(hwnd);
    failures += expect_true(
        root == hwnd &&
            ancestor_parent == GetDesktopWindow() &&
            ancestor_parent != HWND_MESSAGE &&
            parent == nullptr,
        "top-level window");
    failures += expect_true(
        GetWindow(hwnd, GW_OWNER) == nullptr,
        "unowned window");

    const auto style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    const auto extended_style =
        static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    failures += expect_true((style & WS_POPUP) != 0, "popup style");
    failures += expect_true(
        (extended_style & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) ==
            (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE),
        "hidden utility styles");
    failures += expect_true(IsWindowVisible(hwnd) == FALSE, "window hidden");

    const auto devices = registered_devices();
    constexpr std::array<USHORT, 4> usages{0x06, 0x05, 0x04, 0x08};
    for (const USHORT usage : usages)
    {
        const auto* registration = find_registration(devices, usage);
        failures += expect_true(
            registration != nullptr,
            "Raw Input usage registered");
        if (registration != nullptr)
        {
            failures += expect_true(
                registration->hwndTarget == hwnd,
                "registration targets input window");
            failures += expect_true(
                (registration->dwFlags & RIDEV_INPUTSINK) != 0 &&
                    (registration->dwFlags & RIDEV_NOLEGACY) == 0 &&
                    (registration->dwFlags & RIDEV_NOHOTKEYS) == 0,
                "effective registration keeps legacy input enabled");
        }
    }

    const auto fake_device = reinterpret_cast<HANDLE>(0x1234);
    SendMessageW(
        hwnd,
        WM_INPUT_DEVICE_CHANGE,
        GIDC_ARRIVAL,
        reinterpret_cast<LPARAM>(fake_device));
    failures += expect_true(
        sink.device_change_count == 1 &&
            sink.last_device_event == GIDC_ARRIVAL &&
            sink.last_device == fake_device,
        "device change forwarded");

    window.Destroy();
    failures += expect_true(window.hwnd() == nullptr, "HWND cleared");
    failures += expect_true(IsWindow(hwnd) == FALSE, "window destroyed");

    const auto after_destroy = registered_devices();
    for (const USHORT usage : usages)
    {
        failures += expect_true(
            find_registration(after_destroy, usage) == nullptr,
            "Raw Input usage removed");
    }

    if (failures != 0)
    {
        return 1;
    }

    std::cout << "Win32InputWindowTests passed\n";
    return 0;
}

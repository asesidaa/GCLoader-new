#include "Input/Win32/Win32InputWindow.h"

#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace gc::input {
namespace {

constexpr DWORD kRegistrationFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
constexpr std::array<USHORT, 4> kRawInputUsages{0x06, 0x05, 0x04, 0x08};

std::array<RAWINPUTDEVICE, kRawInputUsages.size()> Registrations(
    HWND target,
    DWORD flags)
{
    std::array<RAWINPUTDEVICE, kRawInputUsages.size()> devices{};
    for (std::size_t index = 0; index < devices.size(); ++index)
    {
        devices[index] = RAWINPUTDEVICE{
            .usUsagePage = 0x01,
            .usUsage = kRawInputUsages[index],
            .dwFlags = flags,
            .hwndTarget = target,
        };
    }
    return devices;
}

std::string Win32Failure(const char* operation)
{
    return std::string(operation) + " failed with Win32 error " +
        std::to_string(GetLastError());
}

} // namespace

Win32InputWindow::Win32InputWindow(
    RawInputMessageSink& sink,
    RegistrationFunction register_raw_input_devices) noexcept
    : sink_(sink),
      register_raw_input_devices_(register_raw_input_devices)
{
}

Win32InputWindow::~Win32InputWindow()
{
    Destroy();
}

std::expected<void, std::string> Win32InputWindow::Create(HINSTANCE instance)
{
    if (hwnd_ != nullptr || class_registered_)
    {
        return std::unexpected("Raw Input window already exists");
    }
    if (instance == nullptr)
    {
        return std::unexpected("Raw Input window requires an HINSTANCE");
    }

    instance_ = instance;
    owner_thread_id_ = GetCurrentThreadId();
    class_name_ = L"GCLoader.RawInputWindow." + std::to_wstring(
        reinterpret_cast<std::uintptr_t>(this));

    WNDCLASSEXW window_class{
        .cbSize = sizeof(WNDCLASSEXW),
        .lpfnWndProc = WindowProc,
        .hInstance = instance_,
        .lpszClassName = class_name_.c_str(),
    };
    if (RegisterClassExW(&window_class) == 0)
    {
        const auto error = Win32Failure("RegisterClassExW");
        instance_ = nullptr;
        owner_thread_id_ = 0;
        class_name_.clear();
        return std::unexpected(error);
    }
    class_registered_ = true;

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        class_name_.c_str(),
        L"",
        WS_POPUP,
        0,
        0,
        1,
        1,
        nullptr,
        nullptr,
        instance_,
        this);
    if (hwnd_ == nullptr)
    {
        const auto error = Win32Failure("CreateWindowExW");
        Destroy();
        return std::unexpected(error);
    }

    auto registrations = Registrations(hwnd_, kRegistrationFlags);
    if (!register_raw_input_devices_(
            registrations.data(),
            static_cast<UINT>(registrations.size()),
            sizeof(RAWINPUTDEVICE)))
    {
        const auto error = Win32Failure("RegisterRawInputDevices");
        Destroy();
        return std::unexpected(error);
    }

    const auto verified = VerifyRegistrations();
    if (!verified)
    {
        const auto error = verified.error();
        Destroy();
        return std::unexpected(error);
    }
    return {};
}

void Win32InputWindow::Destroy() noexcept
{
    if (hwnd_ != nullptr && owner_thread_id_ != GetCurrentThreadId())
    {
        return;
    }

    if (hwnd_ != nullptr)
    {
        RemoveRegistrations();
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (class_registered_)
    {
        UnregisterClassW(class_name_.c_str(), instance_);
        class_registered_ = false;
    }
    class_name_.clear();
    instance_ = nullptr;
    owner_thread_id_ = 0;
}

HWND Win32InputWindow::hwnd() const noexcept
{
    return hwnd_;
}

DWORD Win32InputWindow::owner_thread_id() const noexcept
{
    return owner_thread_id_;
}

LRESULT CALLBACK Win32InputWindow::WindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wparam,
    LPARAM lparam) noexcept
{
    Win32InputWindow* self = reinterpret_cast<Win32InputWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<Win32InputWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self));
    }

    if (self != nullptr)
    {
        if (message == WM_INPUT)
        {
            self->sink_.OnRawInput(reinterpret_cast<HRAWINPUT>(lparam));
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
        if (message == WM_INPUT_DEVICE_CHANGE)
        {
            self->sink_.OnRawInputDeviceChange(
                wparam,
                reinterpret_cast<HANDLE>(lparam));
            return 0;
        }
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

std::expected<void, std::string>
Win32InputWindow::VerifyRegistrations() const
{
    UINT count = 0;
    if (GetRegisteredRawInputDevices(
            nullptr, &count, sizeof(RAWINPUTDEVICE)) == UINT_MAX)
    {
        return std::unexpected(
            Win32Failure("GetRegisteredRawInputDevices(size)"));
    }

    std::vector<RAWINPUTDEVICE> devices(count);
    if (count != 0 && GetRegisteredRawInputDevices(
            devices.data(), &count, sizeof(RAWINPUTDEVICE)) == UINT_MAX)
    {
        return std::unexpected(
            Win32Failure("GetRegisteredRawInputDevices(data)"));
    }
    devices.resize(count);

    for (const USHORT usage : kRawInputUsages)
    {
        bool found = false;
        for (const auto& device : devices)
        {
            if (device.usUsagePage == 0x01 &&
                device.usUsage == usage &&
                device.hwndTarget == hwnd_ &&
                (device.dwFlags & RIDEV_INPUTSINK) != 0 &&
                (device.dwFlags & RIDEV_NOLEGACY) == 0 &&
                (device.dwFlags & RIDEV_NOHOTKEYS) == 0)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            std::ostringstream details;
            details << "Raw Input registration verification failed for usage "
                    << usage << "; expected target=" << hwnd_
                    << " flags=0x" << std::hex << kRegistrationFlags;
            for (const auto& device : devices)
            {
                if (device.usUsagePage == 0x01 && device.usUsage == usage)
                {
                    details << "; observed target=" << device.hwndTarget
                            << " flags=0x" << device.dwFlags;
                }
            }
            return std::unexpected(details.str());
        }
    }
    return {};
}

void Win32InputWindow::RemoveRegistrations() noexcept
{
    auto registrations = Registrations(nullptr, RIDEV_REMOVE);
    register_raw_input_devices_(
        registrations.data(),
        static_cast<UINT>(registrations.size()),
        sizeof(RAWINPUTDEVICE));
}

} // namespace gc::input

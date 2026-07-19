#pragma once

#include <Windows.h>

#include <expected>
#include <string>

namespace gc::input {

class RawInputMessageSink {
public:
    virtual ~RawInputMessageSink() = default;
    virtual void OnRawInput(HRAWINPUT input) noexcept = 0;
    virtual void OnRawInputDeviceChange(
        WPARAM change,
        HANDLE device) noexcept = 0;
};

class Win32InputWindow {
public:
    explicit Win32InputWindow(RawInputMessageSink& sink) noexcept;
    ~Win32InputWindow();

    Win32InputWindow(const Win32InputWindow&) = delete;
    Win32InputWindow& operator=(const Win32InputWindow&) = delete;

    [[nodiscard]] std::expected<void, std::string> Create(
        HINSTANCE instance);
    void Destroy() noexcept;

    [[nodiscard]] HWND hwnd() const noexcept;
    [[nodiscard]] DWORD owner_thread_id() const noexcept;

private:
    static LRESULT CALLBACK WindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wparam,
        LPARAM lparam) noexcept;

    [[nodiscard]] std::expected<void, std::string>
    VerifyRegistrations() const;
    void RemoveRegistrations() noexcept;

    RawInputMessageSink& sink_;
    HWND hwnd_{};
    HINSTANCE instance_{};
    DWORD owner_thread_id_{};
    std::wstring class_name_;
    bool class_registered_{};
};

} // namespace gc::input

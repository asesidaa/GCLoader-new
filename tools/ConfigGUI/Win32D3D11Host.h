#pragma once

#include <Windows.h>

#include <expected>
#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct IDXGISwapChain;
struct ImVec4;

class Win32D3D11Host {
public:
    using MessageHandler = LRESULT (*)(
        void* context,
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam) noexcept;

    Win32D3D11Host() = default;
    ~Win32D3D11Host();

    Win32D3D11Host(const Win32D3D11Host&) = delete;
    Win32D3D11Host& operator=(const Win32D3D11Host&) = delete;

    [[nodiscard]] std::expected<void, std::string> Open(
        HINSTANCE instance,
        MessageHandler handler,
        void* context);
    [[nodiscard]] bool PumpMessages() noexcept;
    void BeginFrame() noexcept;
    void Render(const ImVec4& clear_color) noexcept;
    void Close() noexcept;

    [[nodiscard]] HWND window() const noexcept;
    [[nodiscard]] ID3D11Device* device() const noexcept;
    [[nodiscard]] bool quit_requested() const noexcept;

private:
    static LRESULT CALLBACK WindowProc(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam) noexcept;
    LRESULT HandleMessage(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam) noexcept;

    [[nodiscard]] std::expected<void, std::string> CreateDeviceD3D();
    [[nodiscard]] bool CreateRenderTarget() noexcept;
    void CleanupRenderTarget() noexcept;
    void CleanupDeviceD3D() noexcept;
    void ApplyDeferredResize() noexcept;

    HINSTANCE instance_{};
    HWND window_{};
    std::wstring class_name_;
    MessageHandler message_handler_{};
    void* message_context_{};
    ID3D11Device* device_{};
    ID3D11DeviceContext* device_context_{};
    IDXGISwapChain* swap_chain_{};
    ID3D11RenderTargetView* render_target_{};
    UINT resize_width_{};
    UINT resize_height_{};
    bool class_registered_{};
    bool imgui_context_created_{};
    bool imgui_win32_initialized_{};
    bool imgui_dx11_initialized_{};
    bool quit_requested_{};
    bool closing_{};
};

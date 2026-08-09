#include "Win32D3D11Host.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <utility>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam);

namespace {

std::string Win32Failure(const char* operation)
{
    return std::string(operation) + " failed with Win32 error " +
        std::to_string(GetLastError());
}

std::string HresultFailure(const char* operation, HRESULT result)
{
    std::ostringstream message;
    message << operation << " failed with HRESULT 0x"
            << std::hex << std::uppercase
            << static_cast<std::uint32_t>(result);
    return message.str();
}

template <typename Interface>
void Release(Interface*& value) noexcept
{
    if (value != nullptr)
    {
        value->Release();
        value = nullptr;
    }
}

} // namespace

Win32D3D11Host::~Win32D3D11Host()
{
    Close();
}

std::expected<void, std::string> Win32D3D11Host::Open(
    HINSTANCE instance,
    MessageHandler handler,
    void* context)
{
    if (window_ != nullptr || class_registered_ || imgui_context_created_)
    {
        return std::unexpected("ConfigGUI host is already open");
    }
    if (instance == nullptr)
    {
        return std::unexpected("ConfigGUI host requires an HINSTANCE");
    }

    instance_ = instance;
    message_handler_ = handler;
    message_context_ = context;
    quit_requested_ = false;
    closing_ = false;
    class_name_ = L"GCLoader.ConfigGUI." + std::to_wstring(
        reinterpret_cast<std::uintptr_t>(this));

    WNDCLASSEXW window_class{
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_CLASSDC,
        .lpfnWndProc = WindowProc,
        .hInstance = instance_,
        .hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)),
        .lpszClassName = class_name_.c_str(),
    };
    if (RegisterClassExW(&window_class) == 0)
    {
        const auto error = Win32Failure("RegisterClassExW");
        Close();
        return std::unexpected(error);
    }
    class_registered_ = true;

    RECT window_rect{0, 0, 800, 600};
    constexpr DWORD window_style = WS_OVERLAPPEDWINDOW;
    if (!AdjustWindowRectEx(&window_rect, window_style, FALSE, 0))
    {
        const auto error = Win32Failure("AdjustWindowRectEx");
        Close();
        return std::unexpected(error);
    }

    window_ = CreateWindowExW(
        0,
        class_name_.c_str(),
        L"GCLoader Configuration",
        window_style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        window_rect.right - window_rect.left,
        window_rect.bottom - window_rect.top,
        nullptr,
        nullptr,
        instance_,
        this);
    if (window_ == nullptr)
    {
        const auto error = Win32Failure("CreateWindowExW");
        Close();
        return std::unexpected(error);
    }

    if (auto device_result = CreateDeviceD3D(); !device_result)
    {
        const auto error = std::move(device_result.error());
        Close();
        return std::unexpected(error);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    imgui_context_created_ = true;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(window_))
    {
        Close();
        return std::unexpected("ImGui_ImplWin32_Init failed");
    }
    imgui_win32_initialized_ = true;
    if (!ImGui_ImplDX11_Init(device_, device_context_))
    {
        Close();
        return std::unexpected("ImGui_ImplDX11_Init failed");
    }
    imgui_dx11_initialized_ = true;

    ShowWindow(window_, SW_SHOWDEFAULT);
    UpdateWindow(window_);
    return {};
}

bool Win32D3D11Host::PumpMessages() noexcept
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        if (message.message == WM_QUIT)
        {
            quit_requested_ = true;
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return !quit_requested_;
}

void Win32D3D11Host::BeginFrame() noexcept
{
    if (!imgui_win32_initialized_ || !imgui_dx11_initialized_)
    {
        return;
    }

    ApplyDeferredResize();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void Win32D3D11Host::Render(const ImVec4& clear_color) noexcept
{
    if (!imgui_dx11_initialized_ || device_context_ == nullptr ||
        swap_chain_ == nullptr || render_target_ == nullptr)
    {
        return;
    }

    ImGui::Render();
    const float clear[4]{
        clear_color.x * clear_color.w,
        clear_color.y * clear_color.w,
        clear_color.z * clear_color.w,
        clear_color.w,
    };
    device_context_->OMSetRenderTargets(1, &render_target_, nullptr);
    device_context_->ClearRenderTargetView(render_target_, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    swap_chain_->Present(1, 0);
}

void Win32D3D11Host::Close() noexcept
{
    closing_ = true;

    if (imgui_dx11_initialized_)
    {
        ImGui_ImplDX11_Shutdown();
        imgui_dx11_initialized_ = false;
    }
    if (imgui_win32_initialized_)
    {
        ImGui_ImplWin32_Shutdown();
        imgui_win32_initialized_ = false;
    }
    if (imgui_context_created_)
    {
        ImGui::DestroyContext();
        imgui_context_created_ = false;
    }

    CleanupDeviceD3D();
    if (window_ != nullptr)
    {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (class_registered_)
    {
        UnregisterClassW(class_name_.c_str(), instance_);
        class_registered_ = false;
    }

    resize_width_ = 0;
    resize_height_ = 0;
    message_handler_ = nullptr;
    message_context_ = nullptr;
    class_name_.clear();
    instance_ = nullptr;
    closing_ = false;
}

HWND Win32D3D11Host::window() const noexcept
{
    return window_;
}

ID3D11Device* Win32D3D11Host::device() const noexcept
{
    return device_;
}

bool Win32D3D11Host::quit_requested() const noexcept
{
    return quit_requested_;
}

LRESULT CALLBACK Win32D3D11Host::WindowProc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam) noexcept
{
    auto* self = reinterpret_cast<Win32D3D11Host*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<Win32D3D11Host*>(create->lpCreateParams);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self));
    }
    if (self == nullptr)
    {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    return self->HandleMessage(window, message, wparam, lparam);
}

LRESULT Win32D3D11Host::HandleMessage(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam) noexcept
{
    if (imgui_win32_initialized_ &&
        ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam))
    {
        return TRUE;
    }

    if (message_handler_ != nullptr)
    {
        const LRESULT native_input_result = message_handler_(
            message_context_, window, message, wparam, lparam);
        if (native_input_result != 0)
        {
            return native_input_result;
        }
    }

    switch (message)
    {
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED)
        {
            resize_width_ = LOWORD(lparam);
            resize_height_ = HIWORD(lparam);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wparam & 0xFFF0U) == SC_KEYMENU)
        {
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        window_ = nullptr;
        quit_requested_ = true;
        if (!closing_)
        {
            PostQuitMessage(0);
        }
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

std::expected<void, std::string> Win32D3D11Host::CreateDeviceD3D()
{
    DXGI_SWAP_CHAIN_DESC swap_chain_description{};
    swap_chain_description.BufferCount = 2;
    swap_chain_description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_chain_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_description.OutputWindow = window_;
    swap_chain_description.SampleDesc.Count = 1;
    swap_chain_description.Windowed = TRUE;
    swap_chain_description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    constexpr D3D_FEATURE_LEVEL feature_levels[]{
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL selected_feature_level{};
    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        feature_levels,
        static_cast<UINT>(std::size(feature_levels)),
        D3D11_SDK_VERSION,
        &swap_chain_description,
        &swap_chain_,
        &device_,
        &selected_feature_level,
        &device_context_);
    if (result == DXGI_ERROR_UNSUPPORTED)
    {
        result = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            feature_levels,
            static_cast<UINT>(std::size(feature_levels)),
            D3D11_SDK_VERSION,
            &swap_chain_description,
            &swap_chain_,
            &device_,
            &selected_feature_level,
            &device_context_);
    }
    if (FAILED(result))
    {
        return std::unexpected(
            HresultFailure("D3D11CreateDeviceAndSwapChain", result));
    }
    if (!CreateRenderTarget())
    {
        return std::unexpected("CreateRenderTarget failed");
    }
    return {};
}

bool Win32D3D11Host::CreateRenderTarget() noexcept
{
    if (swap_chain_ == nullptr || device_ == nullptr)
    {
        return false;
    }

    ID3D11Texture2D* back_buffer = nullptr;
    const HRESULT buffer_result = swap_chain_->GetBuffer(
        0,
        IID_PPV_ARGS(&back_buffer));
    if (FAILED(buffer_result))
    {
        return false;
    }
    const HRESULT target_result = device_->CreateRenderTargetView(
        back_buffer,
        nullptr,
        &render_target_);
    back_buffer->Release();
    return SUCCEEDED(target_result);
}

void Win32D3D11Host::CleanupRenderTarget() noexcept
{
    Release(render_target_);
}

void Win32D3D11Host::CleanupDeviceD3D() noexcept
{
    CleanupRenderTarget();
    Release(swap_chain_);
    Release(device_context_);
    Release(device_);
}

void Win32D3D11Host::ApplyDeferredResize() noexcept
{
    if (resize_width_ == 0 || resize_height_ == 0 || swap_chain_ == nullptr)
    {
        return;
    }

    CleanupRenderTarget();
    const HRESULT result = swap_chain_->ResizeBuffers(
        0,
        resize_width_,
        resize_height_,
        DXGI_FORMAT_UNKNOWN,
        0);
    resize_width_ = 0;
    resize_height_ = 0;
    if (SUCCEEDED(result))
    {
        (void) CreateRenderTarget();
    }
}

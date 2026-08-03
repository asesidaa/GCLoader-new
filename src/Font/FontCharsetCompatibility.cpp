#include "Font/FontCharsetCompatibility.h"

#include "Platform/Win32/Hooking/MinHookTransaction.h"

#include <plog/Log.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace gc::font {
namespace {

CreateFontIndirectWApi g_original_create_font_indirect_w{};
std::unique_ptr<gc::win32_hooks::MinHookTransaction>
    g_hook_transaction;
std::atomic_uint32_t g_font_call_count{};
std::atomic_uint32_t g_infinity_font_call_count{};
thread_local bool g_font_diagnostics_active{};

constexpr std::uint32_t kInitialFontLogLimit = 8;
constexpr std::uint32_t kInfinityFontLogLimit = 64;

struct FontSelection {
    bool logical_available{};
    LOGFONTW logical{};
    std::array<wchar_t, LF_FACESIZE + 1> physical_face{};
};

BYTE ForwardedCharset(BYTE requested) noexcept {
    if (requested == ANSI_CHARSET || requested == DEFAULT_CHARSET) {
        return SHIFTJIS_CHARSET;
    }
    return requested;
}

std::array<wchar_t, LF_FACESIZE + 1> TerminatedFace(
    const wchar_t (&face)[LF_FACESIZE]) noexcept {
    std::array<wchar_t, LF_FACESIZE + 1> terminated{};
    for (std::size_t index = 0; index < LF_FACESIZE; ++index) {
        terminated[index] = face[index];
    }
    return terminated;
}

FontSelection InspectFontSelection(HFONT font) noexcept {
    FontSelection selection{};
    if (font == nullptr) {
        return selection;
    }

    selection.logical_available = GetObjectW(
        font,
        sizeof(selection.logical),
        &selection.logical) == sizeof(selection.logical);

    const auto dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr) {
        return selection;
    }

    const auto previous = SelectObject(dc, font);
    if (previous != nullptr && previous != HGDI_ERROR) {
        static_cast<void>(GetTextFaceW(
            dc,
            static_cast<int>(selection.physical_face.size()),
            selection.physical_face.data()));
        static_cast<void>(SelectObject(dc, previous));
    }
    static_cast<void>(DeleteDC(dc));
    return selection;
}

void LogLocaleEmulatorModules(const char* stage) noexcept {
    try {
        PLOG_INFO
            << "FontCharsetCompatibility: modules stage=" << stage
            << " LocaleEmulator.dll="
            << GetModuleHandleW(L"LocaleEmulator.dll")
            << " LoaderDll.dll="
            << GetModuleHandleW(L"LoaderDll.dll");
    } catch (...) {
    }
}

void LogFontSelection(
    const LOGFONTW* requested,
    HFONT result) noexcept {
    if (requested == nullptr || g_font_diagnostics_active) {
        return;
    }

    const auto call = g_font_call_count.fetch_add(
        1,
        std::memory_order_relaxed) + 1;
    const auto infinity = detail::IsInfinityFontFace(requested);
    const auto infinity_call = infinity
        ? g_infinity_font_call_count.fetch_add(
              1,
              std::memory_order_relaxed) + 1
        : 0;
    if (call > kInitialFontLogLimit &&
        (!infinity || infinity_call > kInfinityFontLogLimit)) {
        return;
    }

    g_font_diagnostics_active = true;
    try {
        if (call == 1) {
            LogLocaleEmulatorModules("first_detour_call");
        }

        const auto selection = InspectFontSelection(result);
        const auto requested_face = TerminatedFace(requested->lfFaceName);
        const auto logical_face = TerminatedFace(
            selection.logical.lfFaceName);
        const auto physical_face = selection.physical_face[0] != L'\0'
            ? selection.physical_face.data()
            : L"<unavailable>";

        PLOG_INFO
            << "FontCharsetCompatibility: detour_hit call=" << call
            << " infinity_call=" << infinity_call
            << " requested_face=" << requested_face.data()
            << " requested_charset="
            << static_cast<unsigned int>(requested->lfCharSet)
            << " forwarded_charset="
            << static_cast<unsigned int>(
                   ForwardedCharset(requested->lfCharSet))
            << " height=" << requested->lfHeight
            << " weight=" << requested->lfWeight
            << " hfont=" << result
            << " logical_available=" << selection.logical_available
            << " logical_face="
            << (selection.logical_available
                    ? logical_face.data()
                    : L"<unavailable>")
            << " logical_charset="
            << static_cast<unsigned int>(selection.logical.lfCharSet)
            << " physical_face=" << physical_face;
    } catch (...) {
    }
    g_font_diagnostics_active = false;
}

HFONT WINAPI CreateFontIndirectWDetour(
    const LOGFONTW* requested) noexcept {
    const auto result = detail::InvokeCreateFontIndirectWDetour(
        requested,
        g_original_create_font_indirect_w);
    LogFontSelection(requested, result);
    return result;
}

void LogInstallException() noexcept {
    try {
        PLOG_WARNING
            << "FontCharsetCompatibility: initialization exception";
    } catch (...) {
    }
}

void LogInstallSuccess() noexcept {
    try {
        PLOG_INFO
            << "FontCharsetCompatibility: CreateFontIndirectW hook active"
            << " original="
            << reinterpret_cast<void*>(g_original_create_font_indirect_w);
    } catch (...) {
    }
    LogLocaleEmulatorModules("install");
}

} // namespace

bool detail::IsInfinityFontFace(
    const LOGFONTW* requested) noexcept {
    if (requested == nullptr) {
        return false;
    }
    constexpr std::wstring_view prefix = L"InfinityFont_";
    return std::wstring_view{
        requested->lfFaceName,
        LF_FACESIZE}.starts_with(prefix);
}

HFONT detail::InvokeCreateFontIndirectWDetour(
    const LOGFONTW* requested,
    CreateFontIndirectWApi original) noexcept {
    if (original == nullptr) {
        return nullptr;
    }
    if (requested == nullptr) {
        return original(nullptr);
    }

    auto adjusted = *requested;
    adjusted.lfCharSet = ForwardedCharset(adjusted.lfCharSet);
    return original(&adjusted);
}

bool InstallJapaneseFontCharsetCompatibility() noexcept {
    if (g_hook_transaction != nullptr) {
        return true;
    }

    try {
        auto candidate =
            std::make_unique<gc::win32_hooks::MinHookTransaction>();
        const std::array requests{
            gc::win32_hooks::HookRequest{
                .module_name = L"GDI32.dll",
                .export_name = "CreateFontIndirectW",
                .detour = reinterpret_cast<LPVOID>(
                    &CreateFontIndirectWDetour),
                .original = reinterpret_cast<LPVOID*>(
                    &g_original_create_font_indirect_w),
            },
        };

        const auto installed = candidate->Install(
            std::span<const gc::win32_hooks::HookRequest>{requests});
        if (!installed) {
            g_original_create_font_indirect_w = nullptr;
            // MinHookTransaction already logged the exact failed stage,
            // export, Win32 error, or MinHook status.
            return false;
        }

        g_hook_transaction = std::move(candidate);
        LogInstallSuccess();
        return true;
    } catch (...) {
        g_original_create_font_indirect_w = nullptr;
        LogInstallException();
        return false;
    }
}

} // namespace gc::font

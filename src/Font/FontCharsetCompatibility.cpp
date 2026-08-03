#include "Font/FontCharsetCompatibility.h"

#include "Platform/Win32/Hooking/MinHookTransaction.h"

#include <plog/Log.h>

#include <array>
#include <memory>
#include <span>
#include <utility>

namespace gc::font {
namespace {

CreateFontIndirectWApi g_original_create_font_indirect_w{};
std::unique_ptr<gc::win32_hooks::MinHookTransaction>
    g_hook_transaction;

HFONT WINAPI CreateFontIndirectWDetour(
    const LOGFONTW* requested) noexcept {
    return detail::InvokeCreateFontIndirectWDetour(
        requested,
        g_original_create_font_indirect_w);
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
            << "FontCharsetCompatibility: CreateFontIndirectW hook active";
    } catch (...) {
    }
}

} // namespace

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
    if (adjusted.lfCharSet == ANSI_CHARSET ||
        adjusted.lfCharSet == DEFAULT_CHARSET) {
        adjusted.lfCharSet = SHIFTJIS_CHARSET;
    }
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

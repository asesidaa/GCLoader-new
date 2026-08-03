#include "Font/FontCharsetCompatibility.h"

#include "Platform/Win32/Hooking/MinHookTransaction.h"

#include <plog/Log.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace gc::font {
namespace {

CreateFontIndirectWApi g_original_create_font_indirect_w{};
AddFontResourceExAApi g_original_add_font_resource_ex_a{};
std::unique_ptr<gc::win32_hooks::MinHookTransaction>
    g_hook_transaction;
std::atomic_uint32_t g_font_call_count{};
std::atomic_uint32_t g_infinity_font_call_count{};
std::atomic_uint32_t g_font_resource_call_count{};
thread_local bool g_font_diagnostics_active{};

constexpr std::uint32_t kInitialFontLogLimit = 8;
constexpr std::uint32_t kInfinityFontLogLimit = 64;
constexpr std::uint32_t kFontResourceLogLimit = 16;

struct FontSelection {
    bool logical_available{};
    LOGFONTW logical{};
    std::array<wchar_t, LF_FACESIZE + 1> physical_face{};
};

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

void LogNtGdiEntry() noexcept {
    try {
        const auto module = GetModuleHandleW(L"win32u.dll");
        const auto address = module != nullptr
            ? GetProcAddress(module, "NtGdiHfontCreate")
            : nullptr;
        if (address == nullptr) {
            PLOG_INFO
                << "FontCharsetCompatibility: ntgdi_entry"
                << " module=" << module
                << " address=" << address
                << " bytes=<unavailable>";
            return;
        }

        std::array<std::byte, 16> bytes{};
        std::memcpy(
            bytes.data(),
            reinterpret_cast<const void*>(address),
            bytes.size());
        const auto formatted = detail::FormatNtGdiEntryBytes(bytes);
        PLOG_INFO
            << "FontCharsetCompatibility: ntgdi_entry"
            << " module=" << module
            << " address=" << address
            << " bytes=" << formatted.data();
    } catch (...) {
    }
}

void LogFontResourceRegistration(
    LPCSTR file,
    DWORD flags,
    PVOID reserved,
    int result,
    DWORD last_error) noexcept {
    const auto call = g_font_resource_call_count.fetch_add(
        1,
        std::memory_order_relaxed) + 1;
    if (call > kFontResourceLogLimit || g_font_diagnostics_active) {
        return;
    }

    g_font_diagnostics_active = true;
    try {
        std::array<char, 4096> resolved{};
        const auto resolved_length = file != nullptr
            ? GetFullPathNameA(
                  file,
                  static_cast<DWORD>(resolved.size()),
                  resolved.data(),
                  nullptr)
            : 0;
        const auto resolved_path = resolved_length != 0 &&
                resolved_length < resolved.size()
            ? resolved.data()
            : "<unavailable>";
        const auto attributes = file != nullptr
            ? GetFileAttributesA(file)
            : INVALID_FILE_ATTRIBUTES;

        PLOG_INFO
            << "FontCharsetCompatibility: font_resource call=" << call
            << " file=" << (file != nullptr ? file : "<null>")
            << " resolved=" << resolved_path
            << " flags=" << flags
            << " reserved=" << reserved
            << " exists="
            << (attributes != INVALID_FILE_ATTRIBUTES)
            << " result=" << result
            << " last_error=" << last_error;
    } catch (...) {
    }
    g_font_diagnostics_active = false;
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
            LogNtGdiEntry();
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
            << " mode=pass_through"
            << " infinity_call=" << infinity_call
            << " requested_face=" << requested_face.data()
            << " requested_charset="
            << static_cast<unsigned int>(requested->lfCharSet)
            << " forwarded_charset="
            << static_cast<unsigned int>(requested->lfCharSet)
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
    const auto last_error = GetLastError();
    LogFontSelection(requested, result);
    SetLastError(last_error);
    return result;
}

int WINAPI AddFontResourceExADetour(
    LPCSTR file,
    DWORD flags,
    PVOID reserved) noexcept {
    return detail::InvokeAddFontResourceExADetour(
        file,
        flags,
        reserved,
        g_original_add_font_resource_ex_a,
        &LogFontResourceRegistration);
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
            << "FontCharsetCompatibility: diagnostic hooks active"
            << " mode=pass_through"
            << " create_font_original="
            << reinterpret_cast<void*>(g_original_create_font_indirect_w)
            << " add_font_resource_original="
            << reinterpret_cast<void*>(g_original_add_font_resource_ex_a);
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
    return original != nullptr ? original(requested) : nullptr;
}

int detail::InvokeAddFontResourceExADetour(
    LPCSTR file,
    DWORD flags,
    PVOID reserved,
    AddFontResourceExAApi original,
    FontResourceObserver observer) noexcept {
    if (original == nullptr) {
        return 0;
    }

    const auto result = original(file, flags, reserved);
    const auto last_error = GetLastError();
    if (observer != nullptr) {
        observer(file, flags, reserved, result, last_error);
    }
    SetLastError(last_error);
    return result;
}

std::array<char, 48> detail::FormatNtGdiEntryBytes(
    const std::array<std::byte, 16>& bytes) noexcept {
    constexpr char digits[] = "0123456789abcdef";
    std::array<char, 48> formatted{};
    std::size_t output_index = 0;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index != 0) {
            formatted[output_index++] = ' ';
        }
        const auto value = std::to_integer<unsigned int>(bytes[index]);
        formatted[output_index++] = digits[value >> 4];
        formatted[output_index++] = digits[value & 0x0F];
    }
    return formatted;
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
            gc::win32_hooks::HookRequest{
                .module_name = L"GDI32.dll",
                .export_name = "AddFontResourceExA",
                .detour = reinterpret_cast<LPVOID>(
                    &AddFontResourceExADetour),
                .original = reinterpret_cast<LPVOID*>(
                    &g_original_add_font_resource_ex_a),
            },
        };

        const auto installed = candidate->Install(
            std::span<const gc::win32_hooks::HookRequest>{requests});
        if (!installed) {
            g_original_create_font_indirect_w = nullptr;
            g_original_add_font_resource_ex_a = nullptr;
            // MinHookTransaction already logged the exact failed stage,
            // export, Win32 error, or MinHook status.
            return false;
        }

        g_hook_transaction = std::move(candidate);
        LogInstallSuccess();
        return true;
    } catch (...) {
        g_original_create_font_indirect_w = nullptr;
        g_original_add_font_resource_ex_a = nullptr;
        LogInstallException();
        return false;
    }
}

} // namespace gc::font

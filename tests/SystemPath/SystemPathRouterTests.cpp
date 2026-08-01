#include "SystemPath/SystemPathRouter.h"

#include <Windows.h>

#include <array>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

void DiagnoseRoute(
    const char* name,
    const std::expected<gc::system_path::RouteResult, DWORD>& result) {
    if (!result) {
        std::cerr << name << " error=" << result.error() << '\n';
        return;
    }
    std::wcerr << name << L" matched=" << result->matched
               << L" path='" << result->path.native() << L"'\n";
}

std::optional<std::pair<std::string, std::wstring>>
ActiveCodePageComponent() {
    constexpr std::array candidates{
        std::wstring_view{L"é"},
        std::wstring_view{L"Ж"},
        std::wstring_view{L"遊戲"},
        std::wstring_view{L"日本"},
    };
    for (const std::wstring_view candidate : candidates) {
        BOOL used_default = FALSE;
        const int required = WideCharToMultiByte(
            CP_ACP,
            WC_NO_BEST_FIT_CHARS,
            candidate.data(),
            static_cast<int>(candidate.size()),
            nullptr,
            0,
            nullptr,
            &used_default);
        if (required <= 0 || used_default != FALSE) {
            continue;
        }
        std::string encoded(static_cast<std::size_t>(required), '\0');
        used_default = FALSE;
        if (WideCharToMultiByte(
                CP_ACP,
                WC_NO_BEST_FIT_CHARS,
                candidate.data(),
                static_cast<int>(candidate.size()),
                encoded.data(),
                required,
                nullptr,
                &used_default) == required &&
            used_default == FALSE) {
            return std::pair{
                std::move(encoded),
                std::wstring{candidate},
            };
        }
    }
    return std::nullopt;
}

} // namespace

int main() {
    using namespace gc::system_path;
    int failures = 0;

    SystemPathRouter router{
        RuntimeRoot{
            .configured_path = ".\\system",
            .resolved_path = L"H:\\遊戲\\system",
            .redirect_enabled = true,
        }};
    failures += Expect(router.enabled(), "enabled runtime root enables router");

    const auto exact = router.RoutePathW(L"D:\\system");
    const auto mixed = router.RoutePathW(
        L"d:/SYSTEM/DUA/work/file.bin");
    const bool wide_routes_match = exact && exact->matched &&
            exact->path == L"H:\\遊戲\\system" &&
            mixed && mixed->matched &&
            mixed->path ==
                L"H:\\遊戲\\system\\DUA\\work\\file.bin";
    if (!wide_routes_match) {
        DiagnoseRoute("exact", exact);
        DiagnoseRoute("mixed", mixed);
    }
    failures += Expect(
        wide_routes_match,
        "wide logical system paths route by components");

    const auto normalized_inside = router.RoutePathW(
        L"D:\\system\\DUA\\.\\work\\..\\news\\item.bin");
    failures += Expect(
        normalized_inside && normalized_inside->matched &&
            normalized_inside->path ==
                L"H:\\遊戲\\system\\DUA\\news\\item.bin",
        "wide descendants normalize without escaping logical root");

    constexpr std::array<LPCWSTR, 9> unmatched_wide{
        nullptr,
        L"D:\\system2",
        L"D:\\system-file",
        L"D:\\system\\..\\outside",
        L"C:\\system",
        L"D:system",
        L"\\\\?\\D:\\system",
        L".\\system",
        L"D:\\0123456789abcdef0123456789abcdef_000\\TestModeFile",
    };
    for (const LPCWSTR path : unmatched_wide) {
        const auto result = router.RoutePathW(path);
        failures += Expect(
            result && !result->matched,
            "unowned wide path passes through without error");
    }

    const auto escaped_and_reentered = router.RoutePathW(
        L"D:\\system\\..\\system\\DUA\\news");
    failures += Expect(
        escaped_and_reentered && !escaped_and_reentered->matched,
        "path that escapes logical root is not recaptured after reentry");

    constexpr std::array<LPCSTR, 5> unmatched_ansi{
        nullptr,
        "D:\\system2",
        "C:\\system",
        "D:system",
        "D:\\0123456789abcdef0123456789abcdef_000\\TestModeFile",
    };
    for (const LPCSTR path : unmatched_ansi) {
        const auto result = router.RoutePathA(path);
        failures += Expect(
            result && !result->matched,
            "unowned ANSI path passes through without error");
    }

    const auto exact_ansi = router.RoutePathA("d:/SYSTEM");
    const auto descendant_ansi = router.RoutePathA(
        "D:\\system\\DUA\\event\\notice.txt");
    failures += Expect(
        exact_ansi && exact_ansi->matched &&
            exact_ansi->path == L"H:\\遊戲\\system" &&
            descendant_ansi && descendant_ansi->matched &&
            descendant_ansi->path ==
                L"H:\\遊戲\\system\\DUA\\event\\notice.txt",
        "ANSI logical system paths route through native destination");

    const auto active_component = ActiveCodePageComponent();
    failures += Expect(
        active_component.has_value(),
        "test found a lossless active-code-page component");
    if (active_component) {
        const std::string ansi_path =
            "D:\\system\\" + active_component->first + "\\item.bin";
        const auto active_route = router.RoutePathA(ansi_path.c_str());
        failures += Expect(
            active_route && active_route->matched &&
                active_route->path ==
                    std::filesystem::path{L"H:\\遊戲\\system"} /
                        active_component->second / L"item.bin",
            "ANSI route uses the active code page losslessly");
    }

    const auto converted = router.ConvertAnsiPath("D:\\system\\DUA");
    failures += Expect(
        converted && *converted == L"D:\\system\\DUA",
        "public ANSI conversion produces a native path");
    const auto null_conversion = router.ConvertAnsiPath(nullptr);
    failures += Expect(
        !null_conversion &&
            null_conversion.error() == ERROR_INVALID_PARAMETER,
        "null ANSI conversion is an explicit parameter error");

    const char malformed_ansi[]{
        'D', ':', '\\', 's', 'y', 's', 't', 'e', 'm', '\\',
        static_cast<char>(0x81), '\0'};
    const auto malformed = router.RoutePathA(malformed_ansi);
    failures += Expect(
        !malformed &&
            malformed.error() == ERROR_NO_UNICODE_TRANSLATION,
        "malformed active-code-page input reports translation failure");

    SystemPathRouter disabled{
        RuntimeRoot{
            .configured_path = "D:\\system",
            .resolved_path = L"D:\\system",
            .redirect_enabled = false,
        }};
    failures += Expect(
        !disabled.enabled(),
        "disabled runtime root disables router");
    const auto disabled_wide = disabled.RoutePathW(L"D:\\system");
    const auto disabled_ansi = disabled.RoutePathA(malformed_ansi);
    failures += Expect(
        disabled_wide && !disabled_wide->matched &&
            disabled_ansi && !disabled_ansi->matched,
        "disabled router passes through all inputs without conversion");

    return failures == 0 ? 0 : 1;
}

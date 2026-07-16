#include "TestModeStorage/Hooks.h"
#include "TestModeStorage/Redirector.h"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

int expect_redirect(
    const std::optional<std::string>& actual,
    const std::string& expected,
    const char* name) {
    if (actual && *actual == expected) {
        return 0;
    }

    std::cerr << "Expected " << name << " to redirect to '" << expected << "'";
    if (actual) {
        std::cerr << ", got '" << *actual << "'";
    } else {
        std::cerr << ", got no redirect";
    }
    std::cerr << "\n";
    return 1;
}

int expect_no_redirect(const std::optional<std::string>& actual, const char* name) {
    if (!actual) {
        return 0;
    }

    std::cerr << "Expected no redirect for " << name << ", got '" << *actual << "'\n";
    return 1;
}

int expect_redirect_w(
    const std::optional<std::wstring>& actual,
    const std::wstring& expected,
    const char* name) {
    if (actual && *actual == expected) {
        return 0;
    }

    std::wcerr << L"Expected " << name << L" to redirect to '" << expected << L"'";
    if (actual) {
        std::wcerr << L", got '" << *actual << L"'";
    } else {
        std::wcerr << L", got no redirect";
    }
    std::wcerr << L"\n";
    return 1;
}

int expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }

    std::cerr << "Expected " << name << "\n";
    return 1;
}

} // namespace

int main() {
    int failures = 0;

    constexpr auto kStorageRoot = "0123456789abcdef0123456789ABCDEF_042";
    const std::string current_dir = "C:\\Games\\Groove";

    failures += expect_redirect(
        gc::testmode_storage::RedirectPathA(
            std::string("D:\\") + kStorageRoot,
            current_dir),
        std::string("C:\\Games\\Groove\\") + kStorageRoot,
        "storage root directory");

    failures += expect_redirect(
        gc::testmode_storage::RedirectPathA(
            std::string("d:/") + kStorageRoot + "/TestModeFile/SystemSetting/file.tmlog",
            current_dir),
        std::string("C:\\Games\\Groove\\") + kStorageRoot + "\\TestModeFile\\SystemSetting\\file.tmlog",
        "mixed-separator test-mode file");

    failures += expect_redirect_w(
        gc::testmode_storage::RedirectPathW(
            std::wstring(L"D:\\") + L"fedcba9876543210fedcba9876543210_000\\TestModeFile\\HighScore\\file",
            L"C:\\GC"),
        L"C:\\GC\\fedcba9876543210fedcba9876543210_000\\TestModeFile\\HighScore\\file",
        "wide test-mode file");

    failures += expect_no_redirect(
        gc::testmode_storage::RedirectPathA(
            "D:\\not_a_storage_root\\TestModeFile\\SystemSetting\\file",
            current_dir),
        "non-storage root");

    failures += expect_no_redirect(
        gc::testmode_storage::RedirectPathA(
            "D:\\0123456789abcdef0123456789abcdef_0000\\TestModeFile\\SystemSetting\\file",
            current_dir),
        "storage-looking root with wrong suffix length");

    failures += expect_no_redirect(
        gc::testmode_storage::RedirectPathA(
            "C:\\0123456789abcdef0123456789abcdef_000\\TestModeFile\\SystemSetting\\file",
            current_dir),
        "non-D drive");

    constexpr auto kRoutable =
        "D:\\0123456789abcdef0123456789abcdef_000\\TestModeFile\\file";
    constexpr auto kRedirected =
        "C:\\GC\\0123456789abcdef0123456789abcdef_000\\TestModeFile\\file";
    gc::testmode_storage::Hooks disabled{false};
    const auto unchanged = disabled.RoutePathA(kRoutable, "C:\\GC");
    failures += expect(
        std::string_view{unchanged.get()} == kRoutable,
        "disabled storage route to remain unchanged");
    failures += expect(
        !unchanged.redirected,
        "disabled storage route to avoid owned replacement");

    gc::testmode_storage::Hooks enabled{true};
    const auto redirected = enabled.RoutePathA(kRoutable, "C:\\GC");
    failures += expect(
        std::string_view{redirected.get()} == kRedirected,
        "enabled storage route");
    failures += expect(
        redirected.redirected.has_value(),
        "enabled storage route to own replacement storage");

    constexpr auto kWideRoutable =
        L"D:\\fedcba9876543210fedcba9876543210_000\\TestModeFile\\file";
    constexpr auto kWideRedirected =
        L"C:\\GC\\fedcba9876543210fedcba9876543210_000\\TestModeFile\\file";
    const auto redirected_w = enabled.RoutePathW(kWideRoutable, L"C:\\GC");
    failures += expect(
        std::wstring_view{redirected_w.get()} == kWideRedirected,
        "enabled wide storage route");
    const auto unchanged_w = disabled.RoutePathW(kWideRoutable, L"C:\\GC");
    failures += expect(
        std::wstring_view{unchanged_w.get()} == kWideRoutable,
        "disabled wide storage route to remain unchanged");

    failures += expect(
        enabled.RoutePathA(nullptr, "C:\\GC").get() == nullptr &&
            enabled.RoutePathW(nullptr, L"C:\\GC").get() == nullptr,
        "null paths to remain null");

    constexpr auto kNonmatching = "D:\\not-a-storage-root\\file";
    const auto nonmatching = enabled.RoutePathA(kNonmatching, "C:\\GC");
    failures += expect(
        std::string_view{nonmatching.get()} == kNonmatching &&
            !nonmatching.redirected,
        "nonmatching path to pass through");

    const auto directory_failure = enabled.RoutePathA(kRoutable, "");
    failures += expect(
        std::string_view{directory_failure.get()} == kRoutable &&
            !directory_failure.redirected,
        "current-directory lookup failure to pass through");

    failures += expect(
        enabled.DiskSpaceDirectoryA("D:\\source") == nullptr &&
            enabled.DiskSpaceDirectoryW(L"D:\\source") == nullptr,
        "enabled disk-space query to use current volume");
    failures += expect(
        std::string_view{disabled.DiskSpaceDirectoryA("D:\\source")} ==
                "D:\\source" &&
            std::wstring_view{disabled.DiskSpaceDirectoryW(L"D:\\source")} ==
                L"D:\\source",
        "disabled disk-space query to preserve directory");
    failures += expect(enabled.enabled() && !disabled.enabled(),
                       "storage policy enabled state");

    return failures == 0 ? 0 : 1;
}

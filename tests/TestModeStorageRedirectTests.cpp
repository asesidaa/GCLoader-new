#include "TestModeStorageRedirect.h"

#include <iostream>
#include <optional>
#include <string>

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

    return failures == 0 ? 0 : 1;
}

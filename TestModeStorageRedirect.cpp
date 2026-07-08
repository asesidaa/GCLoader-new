#include "TestModeStorageRedirect.h"

#include <filesystem>

namespace gc::testmode_storage {
namespace {

namespace fs = std::filesystem;

bool is_hex(wchar_t value) {
    return (value >= L'0' && value <= L'9') ||
           (value >= L'a' && value <= L'f') ||
           (value >= L'A' && value <= L'F');
}

bool is_digit(wchar_t value) {
    return value >= L'0' && value <= L'9';
}

bool is_d_drive_absolute_path(const fs::path& path) {
    const auto root_name = path.root_name().native();
    return path.has_root_directory() &&
           root_name.size() == 2 &&
           (root_name[0] == L'D' || root_name[0] == L'd') &&
           root_name[1] == L':';
}

bool is_storage_root_name(const fs::path& component) {
    const auto name = component.native();
    if (name.size() != 36) {
        return false;
    }

    for (std::size_t i = 0; i < 32; ++i) {
        if (!is_hex(name[i])) {
            return false;
        }
    }

    if (name[32] != L'_') {
        return false;
    }

    for (std::size_t i = 33; i < 36; ++i) {
        if (!is_digit(name[i])) {
            return false;
        }
    }

    return true;
}

std::optional<fs::path> redirect_path(const fs::path& path, const fs::path& current_directory) {
    if (!is_d_drive_absolute_path(path) || current_directory.empty()) {
        return std::nullopt;
    }

    const auto relative = path.relative_path();
    auto component = relative.begin();
    if (component == relative.end() || !is_storage_root_name(*component)) {
        return std::nullopt;
    }

    auto redirected = (current_directory / relative).lexically_normal();
    redirected.make_preferred();
    return redirected;
}

} // namespace

std::optional<std::string> RedirectPathA(
    std::string_view path,
    std::string_view current_directory) {
    auto redirected = redirect_path(
        fs::path{std::string{path}},
        fs::path{std::string{current_directory}});
    if (!redirected) {
        return std::nullopt;
    }

    return redirected->string();
}

std::optional<std::wstring> RedirectPathW(
    std::wstring_view path,
    std::wstring_view current_directory) {
    auto redirected = redirect_path(
        fs::path{std::wstring{path}},
        fs::path{std::wstring{current_directory}});
    if (!redirected) {
        return std::nullopt;
    }

    return redirected->wstring();
}

} // namespace gc::testmode_storage

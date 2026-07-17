#include "TestModeStorage/Hooks.h"

#include "TestModeStorage/Redirector.h"
#include "plog/Log.h"

#include <exception>
#include <filesystem>

namespace gc::testmode_storage {

Hooks::Hooks(bool enabled) noexcept
    : enabled_{enabled}
{
}

RoutedPathA Hooks::RoutePathA(LPCSTR path) const noexcept
{
    RoutedPathA result{.original = path};
    if (!enabled_ || path == nullptr) {
        return result;
    }

    try {
        const auto current_directory =
            std::filesystem::current_path().string();
        return RoutePathA(path, current_directory);
    } catch (const std::filesystem::filesystem_error& error) {
        PLOG_WARNING
            << "Test-mode storage: failed to read current directory: "
            << error.what();
    } catch (const std::exception& error) {
        PLOG_WARNING
            << "Test-mode storage: failed to route ANSI path: "
            << error.what();
    } catch (...) {
        PLOG_WARNING
            << "Test-mode storage: failed to route ANSI path";
    }
    return result;
}

RoutedPathW Hooks::RoutePathW(LPCWSTR path) const noexcept
{
    RoutedPathW result{.original = path};
    if (!enabled_ || path == nullptr) {
        return result;
    }

    try {
        const auto current_directory =
            std::filesystem::current_path().wstring();
        return RoutePathW(path, current_directory);
    } catch (const std::filesystem::filesystem_error& error) {
        PLOG_WARNING
            << "Test-mode storage: failed to read current directory: "
            << error.what();
    } catch (const std::exception& error) {
        PLOG_WARNING
            << "Test-mode storage: failed to route wide path: "
            << error.what();
    } catch (...) {
        PLOG_WARNING
            << "Test-mode storage: failed to route wide path";
    }
    return result;
}

RoutedPathA Hooks::RoutePathA(
    LPCSTR path,
    std::string_view current_directory) const noexcept
{
    RoutedPathA result{.original = path};
    if (!enabled_ || path == nullptr) {
        return result;
    }

    try {
        result.redirected = RedirectPathA(path, current_directory);
    } catch (const std::exception& error) {
        PLOG_WARNING
            << "Test-mode storage: failed to route ANSI path: "
            << error.what();
    } catch (...) {
        PLOG_WARNING
            << "Test-mode storage: failed to route ANSI path";
    }
    return result;
}

RoutedPathW Hooks::RoutePathW(
    LPCWSTR path,
    std::wstring_view current_directory) const noexcept
{
    RoutedPathW result{.original = path};
    if (!enabled_ || path == nullptr) {
        return result;
    }

    try {
        result.redirected = RedirectPathW(path, current_directory);
    } catch (const std::exception& error) {
        PLOG_WARNING
            << "Test-mode storage: failed to route wide path: "
            << error.what();
    } catch (...) {
        PLOG_WARNING
            << "Test-mode storage: failed to route wide path";
    }
    return result;
}

LPCSTR Hooks::DiskSpaceDirectoryA(LPCSTR path) const noexcept
{
    return enabled_ ? nullptr : path;
}

LPCWSTR Hooks::DiskSpaceDirectoryW(LPCWSTR path) const noexcept
{
    return enabled_ ? nullptr : path;
}

bool Hooks::enabled() const noexcept
{
    return enabled_;
}

} // namespace gc::testmode_storage

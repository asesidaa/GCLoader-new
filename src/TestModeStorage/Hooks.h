#pragma once

#include <Windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace gc::testmode_storage {

struct RoutedPathA {
    LPCSTR original{};
    std::optional<std::string> redirected;

    [[nodiscard]] LPCSTR get() const noexcept
    {
        return redirected ? redirected->c_str() : original;
    }
};

struct RoutedPathW {
    LPCWSTR original{};
    std::optional<std::wstring> redirected;

    [[nodiscard]] LPCWSTR get() const noexcept
    {
        return redirected ? redirected->c_str() : original;
    }
};

class Hooks {
public:
    explicit Hooks(bool enabled) noexcept;

    [[nodiscard]] RoutedPathA RoutePathA(LPCSTR path) const noexcept;
    [[nodiscard]] RoutedPathW RoutePathW(LPCWSTR path) const noexcept;
    [[nodiscard]] RoutedPathA RoutePathA(
        LPCSTR path,
        std::string_view current_directory) const noexcept;
    [[nodiscard]] RoutedPathW RoutePathW(
        LPCWSTR path,
        std::wstring_view current_directory) const noexcept;
    [[nodiscard]] LPCSTR DiskSpaceDirectoryA(LPCSTR path) const noexcept;
    [[nodiscard]] LPCWSTR DiskSpaceDirectoryW(LPCWSTR path) const noexcept;
    [[nodiscard]] bool enabled() const noexcept;

private:
    bool enabled_{};
};

} // namespace gc::testmode_storage

#pragma once

#include "SystemPath/SystemRoot.h"

#include <Windows.h>

#include <expected>
#include <filesystem>

namespace gc::system_path {

struct RouteResult {
    bool matched{};
    std::filesystem::path path;
};

class SystemPathRouter {
public:
    explicit SystemPathRouter(RuntimeRoot root) noexcept;

    [[nodiscard]] std::expected<RouteResult, DWORD>
    RoutePathA(LPCSTR path) const noexcept;
    [[nodiscard]] std::expected<RouteResult, DWORD>
    RoutePathW(LPCWSTR path) const noexcept;
    [[nodiscard]] std::expected<std::filesystem::path, DWORD>
    ConvertAnsiPath(LPCSTR path) const noexcept;
    [[nodiscard]] bool enabled() const noexcept;

private:
    RuntimeRoot root_;
};

} // namespace gc::system_path

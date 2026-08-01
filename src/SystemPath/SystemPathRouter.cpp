#include "SystemPath/SystemPathRouter.h"

#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace gc::system_path {

namespace {

constexpr RouteResult Unmatched() noexcept {
    return {};
}

bool IsDDriveAbsolutePath(
    const std::filesystem::path& path) {
    const auto root_name = path.root_name().native();
    return path.has_root_directory() &&
        root_name.size() == 2 &&
        (root_name[0] == L'D' || root_name[0] == L'd') &&
        root_name[1] == L':';
}

bool ComponentEqualsIgnoreCase(
    const std::filesystem::path& component,
    std::wstring_view expected) noexcept {
    const auto& native = component.native();
    if (native.size() > static_cast<std::size_t>(
            std::numeric_limits<int>::max()) ||
        expected.size() > static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        return false;
    }
    return CompareStringOrdinal(
        native.data(),
        static_cast<int>(native.size()),
        expected.data(),
        static_cast<int>(expected.size()),
        TRUE) == CSTR_EQUAL;
}

bool IsDot(const std::filesystem::path& component) noexcept {
    return component.native() == L".";
}

bool IsDotDot(const std::filesystem::path& component) noexcept {
    return component.native() == L"..";
}

RouteResult RouteNativePath(
    const RuntimeRoot& root,
    const std::filesystem::path& source) {
    if (!IsDDriveAbsolutePath(source)) {
        return Unmatched();
    }

    const auto relative = source.relative_path();
    auto component = relative.begin();
    while (component != relative.end() && IsDot(*component)) {
        ++component;
    }
    if (component == relative.end() ||
        !ComponentEqualsIgnoreCase(*component, L"system")) {
        return Unmatched();
    }
    ++component;

    std::vector<std::filesystem::path> descendants;
    for (; component != relative.end(); ++component) {
        if (IsDot(*component)) {
            continue;
        }
        if (IsDotDot(*component)) {
            if (descendants.empty()) {
                return Unmatched();
            }
            descendants.pop_back();
            continue;
        }
        descendants.push_back(*component);
    }

    std::filesystem::path routed = root.resolved_path;
    for (const auto& descendant : descendants) {
        routed /= descendant;
    }
    routed = routed.lexically_normal();
    routed.make_preferred();
    return {
        .matched = true,
        .path = std::move(routed),
    };
}

DWORD FilesystemErrorCode(
    const std::filesystem::filesystem_error& error) noexcept {
    const int value = error.code().value();
    return value == 0
        ? ERROR_INVALID_NAME
        : static_cast<DWORD>(value);
}

} // namespace

SystemPathRouter::SystemPathRouter(RuntimeRoot root) noexcept
    : root_(std::move(root)) {
}

std::expected<RouteResult, DWORD> SystemPathRouter::RoutePathA(
    LPCSTR path) const noexcept {
    if (path == nullptr || !enabled()) {
        return Unmatched();
    }
    auto native = ConvertAnsiPath(path);
    if (!native) {
        return std::unexpected(native.error());
    }
    try {
        return RouteNativePath(root_, *native);
    } catch (const std::bad_alloc&) {
        return std::unexpected(ERROR_NOT_ENOUGH_MEMORY);
    } catch (const std::filesystem::filesystem_error& error) {
        return std::unexpected(FilesystemErrorCode(error));
    } catch (...) {
        return std::unexpected(ERROR_INVALID_NAME);
    }
}

std::expected<RouteResult, DWORD> SystemPathRouter::RoutePathW(
    LPCWSTR path) const noexcept {
    if (path == nullptr || !enabled()) {
        return Unmatched();
    }
    try {
        return RouteNativePath(root_, std::filesystem::path{path});
    } catch (const std::bad_alloc&) {
        return std::unexpected(ERROR_NOT_ENOUGH_MEMORY);
    } catch (const std::filesystem::filesystem_error& error) {
        return std::unexpected(FilesystemErrorCode(error));
    } catch (...) {
        return std::unexpected(ERROR_INVALID_NAME);
    }
}

std::expected<std::filesystem::path, DWORD>
SystemPathRouter::ConvertAnsiPath(LPCSTR path) const noexcept {
    if (path == nullptr) {
        return std::unexpected(ERROR_INVALID_PARAMETER);
    }
    try {
        const int required = MultiByteToWideChar(
            CP_ACP,
            MB_ERR_INVALID_CHARS,
            path,
            -1,
            nullptr,
            0);
        if (required <= 0) {
            const DWORD error = GetLastError();
            return std::unexpected(
                error == ERROR_SUCCESS
                    ? ERROR_NO_UNICODE_TRANSLATION
                    : error);
        }

        std::wstring native(static_cast<std::size_t>(required), L'\0');
        if (MultiByteToWideChar(
                CP_ACP,
                MB_ERR_INVALID_CHARS,
                path,
                -1,
                native.data(),
                required) != required) {
            const DWORD error = GetLastError();
            return std::unexpected(
                error == ERROR_SUCCESS
                    ? ERROR_NO_UNICODE_TRANSLATION
                    : error);
        }
        native.pop_back();
        return std::filesystem::path{std::move(native)};
    } catch (const std::bad_alloc&) {
        return std::unexpected(ERROR_NOT_ENOUGH_MEMORY);
    } catch (const std::filesystem::filesystem_error& error) {
        return std::unexpected(FilesystemErrorCode(error));
    } catch (...) {
        return std::unexpected(ERROR_INVALID_NAME);
    }
}

bool SystemPathRouter::enabled() const noexcept {
    return root_.redirect_enabled;
}

} // namespace gc::system_path

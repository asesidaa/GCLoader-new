#include "SystemPath/SystemRoot.h"

#include <Windows.h>

#include <limits>
#include <new>
#include <utility>

namespace gc::system_path {

namespace {

std::expected<std::filesystem::path, std::error_code>
PathFromUtf8(std::string_view value) noexcept {
    try {
        if (value.empty() ||
            value.find('\0') != std::string_view::npos ||
            value.size() > static_cast<std::size_t>(
                std::numeric_limits<int>::max())) {
            return std::unexpected(
                std::make_error_code(std::errc::invalid_argument));
        }
        const auto source_size = static_cast<int>(value.size());
        const int required = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            source_size,
            nullptr,
            0);
        if (required <= 0) {
            return std::unexpected(
                std::error_code{
                    static_cast<int>(GetLastError()),
                    std::system_category()});
        }

        std::wstring native(static_cast<std::size_t>(required), L'\0');
        if (MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                source_size,
                native.data(),
                required) != required) {
            return std::unexpected(
                std::error_code{
                    static_cast<int>(GetLastError()),
                    std::system_category()});
        }
        return std::filesystem::path{std::move(native)};
    } catch (const std::bad_alloc&) {
        return std::unexpected(
            std::make_error_code(std::errc::not_enough_memory));
    } catch (const std::filesystem::filesystem_error& error) {
        return std::unexpected(error.code());
    } catch (...) {
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    }
}

bool EquivalentWindowsPaths(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    std::wstring normalized_left = left.lexically_normal().native();
    std::wstring normalized_right = right.lexically_normal().native();
    for (wchar_t& character : normalized_left) {
        if (character == L'/') {
            character = L'\\';
        }
    }
    for (wchar_t& character : normalized_right) {
        if (character == L'/') {
            character = L'\\';
        }
    }
    while (normalized_left.size() > 1 &&
           normalized_left.back() == L'\\') {
        normalized_left.pop_back();
    }
    while (normalized_right.size() > 1 &&
           normalized_right.back() == L'\\') {
        normalized_right.pop_back();
    }
    if (normalized_left.size() > static_cast<std::size_t>(
            std::numeric_limits<int>::max()) ||
        normalized_right.size() > static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        return false;
    }
    return CompareStringOrdinal(
        normalized_left.data(),
        static_cast<int>(normalized_left.size()),
        normalized_right.data(),
        static_cast<int>(normalized_right.size()),
        TRUE) == CSTR_EQUAL;
}

std::expected<PreparedRoot, RootPrepareError> EnsureTree(
    RuntimeRoot runtime,
    bool configured_path_changed,
    RootPrepareStage stage,
    bool registry_enabled,
    bool configured_was_default,
    DirectoryActions actions) noexcept {
    std::filesystem::path current_path = runtime.resolved_path;
    try {
        if (actions.create_directories == nullptr) {
            return std::unexpected(RootPrepareError{
                .stage = stage,
                .path = current_path,
                .error = std::make_error_code(
                    std::errc::invalid_argument),
                .registry_enabled = registry_enabled,
                .configured_was_default = configured_was_default,
            });
        }

        for (const std::wstring_view leaf : kRequiredTreeLeaves) {
            current_path = runtime.resolved_path / leaf;
            std::error_code error;
            actions.create_directories(
                actions.context,
                current_path,
                error);
            if (error) {
                return std::unexpected(RootPrepareError{
                    .stage = stage,
                    .path = current_path,
                    .error = error,
                    .registry_enabled = registry_enabled,
                    .configured_was_default = configured_was_default,
                });
            }
        }
        return PreparedRoot{
            .runtime = std::move(runtime),
            .configured_path_changed = configured_path_changed,
        };
    } catch (const std::bad_alloc&) {
        return std::unexpected(RootPrepareError{
            .stage = stage,
            .path = std::move(current_path),
            .error = std::make_error_code(
                std::errc::not_enough_memory),
            .registry_enabled = registry_enabled,
            .configured_was_default = configured_was_default,
        });
    } catch (const std::filesystem::filesystem_error& error) {
        return std::unexpected(RootPrepareError{
            .stage = stage,
            .path = std::move(current_path),
            .error = error.code(),
            .registry_enabled = registry_enabled,
            .configured_was_default = configured_was_default,
        });
    } catch (...) {
        return std::unexpected(RootPrepareError{
            .stage = stage,
            .path = std::move(current_path),
            .error = std::make_error_code(std::errc::io_error),
            .registry_enabled = registry_enabled,
            .configured_was_default = configured_was_default,
        });
    }
}

bool ProductionCreateDirectories(
    void*,
    const std::filesystem::path& path,
    std::error_code& error) noexcept {
    try {
        return std::filesystem::create_directories(path, error);
    } catch (const std::bad_alloc&) {
        error = std::make_error_code(std::errc::not_enough_memory);
    } catch (const std::filesystem::filesystem_error& failure) {
        error = failure.code();
    } catch (...) {
        error = std::make_error_code(std::errc::io_error);
    }
    return false;
}

RootPrepareError InvalidConfiguredPathError(
    const RootPrepareRequest& request,
    std::error_code error) {
    return {
        .stage = RootPrepareStage::invalid_configured_path,
        .error = error,
        .registry_enabled = request.registry_enabled,
        .configured_was_default = false,
    };
}

} // namespace

DirectoryActions ProductionDirectoryActions() noexcept {
    return {
        .create_directories = &ProductionCreateDirectories,
    };
}

std::expected<PreparedRoot, RootPrepareError> PrepareGameSystemRoot(
    RootPrepareRequest request,
    DirectoryActions actions) noexcept {
    try {
        const std::filesystem::path logical_root{kLogicalSystemRoot};
        if (!request.registry_enabled) {
            return EnsureTree(
                RuntimeRoot{
                    .configured_path =
                        std::string{kDefaultConfiguredPath},
                    .resolved_path = logical_root,
                    .redirect_enabled = false,
                },
                false,
                RootPrepareStage::configured_tree,
                false,
                true,
                actions);
        }

        auto configured_path = PathFromUtf8(request.configured_path);
        if (!configured_path) {
            return std::unexpected(InvalidConfiguredPathError(
                request,
                configured_path.error()));
        }

        const std::filesystem::path normalized_configured =
            configured_path->lexically_normal();
        const bool configured_is_default = EquivalentWindowsPaths(
            normalized_configured,
            logical_root);
        const std::filesystem::path resolved =
            normalized_configured.is_absolute()
                ? normalized_configured
                : (request.config_directory / normalized_configured)
                      .lexically_normal();
        const bool redirect_enabled = !EquivalentWindowsPaths(
            resolved,
            logical_root);

        auto configured = EnsureTree(
            RuntimeRoot{
                .configured_path = std::string{request.configured_path},
                .resolved_path = resolved,
                .redirect_enabled = redirect_enabled,
            },
            false,
            RootPrepareStage::configured_tree,
            true,
            configured_is_default,
            actions);
        if (configured) {
            return configured;
        }
        if (!configured_is_default) {
            return std::unexpected(configured.error());
        }

        return EnsureTree(
            RuntimeRoot{
                .configured_path =
                    std::string{kFallbackConfiguredPath},
                .resolved_path =
                    (request.config_directory / L"system")
                        .lexically_normal(),
                .redirect_enabled = true,
            },
            true,
            RootPrepareStage::fallback_tree,
            true,
            true,
            actions);
    } catch (const std::bad_alloc&) {
        return std::unexpected(InvalidConfiguredPathError(
            request,
            std::make_error_code(std::errc::not_enough_memory)));
    } catch (const std::filesystem::filesystem_error& error) {
        return std::unexpected(InvalidConfiguredPathError(
            request,
            error.code()));
    } catch (...) {
        return std::unexpected(InvalidConfiguredPathError(
            request,
            std::make_error_code(std::errc::invalid_argument)));
    }
}

} // namespace gc::system_path

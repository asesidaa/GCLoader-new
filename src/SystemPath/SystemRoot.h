#pragma once

#include <array>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace gc::system_path {

inline constexpr std::wstring_view kLogicalSystemRoot = L"D:\\system";
inline constexpr std::string_view kDefaultConfiguredPath = "D:\\system";
inline constexpr std::string_view kFallbackConfiguredPath = ".\\system";

inline constexpr std::array<std::wstring_view, 8> kRequiredTreeLeaves{
    L"CmdFile\\log",
    L"DUA\\data",
    L"DUA\\decrypt",
    L"DUA\\download",
    L"DUA\\event",
    L"DUA\\news",
    L"DUA\\unpack",
    L"DUA\\work",
};

struct RuntimeRoot {
    std::string configured_path;
    std::filesystem::path resolved_path;
    bool redirect_enabled{};
};

enum class RootPrepareStage {
    invalid_configured_path,
    configured_tree,
    fallback_tree,
};

struct RootPrepareError {
    RootPrepareStage stage{};
    std::filesystem::path path;
    std::error_code error;
    bool registry_enabled{};
    bool configured_was_default{};
};

struct RootPrepareRequest {
    bool registry_enabled{};
    std::string_view configured_path;
    std::filesystem::path config_directory;
};

struct PreparedRoot {
    RuntimeRoot runtime;
    bool configured_path_changed{};
};

struct DirectoryActions {
    void* context{};
    bool (*create_directories)(
        void*,
        const std::filesystem::path&,
        std::error_code&) noexcept{};
};

[[nodiscard]] DirectoryActions ProductionDirectoryActions() noexcept;

[[nodiscard]] std::expected<PreparedRoot, RootPrepareError>
PrepareGameSystemRoot(
    RootPrepareRequest request,
    DirectoryActions actions = ProductionDirectoryActions()) noexcept;

} // namespace gc::system_path

#pragma once

namespace gc::crash_dump {

enum class InstallStatus {
    installed,
    filter_only,
    unavailable,
};

[[nodiscard]] constexpr const char* InstallStatusName(
    InstallStatus status) noexcept
{
    switch (status) {
    case InstallStatus::installed: return "installed";
    case InstallStatus::filter_only: return "filter_only";
    case InstallStatus::unavailable: return "unavailable";
    }
    return "unavailable";
}

[[nodiscard]] InstallStatus InstallGameCrashDumpHandler() noexcept;

} // namespace gc::crash_dump

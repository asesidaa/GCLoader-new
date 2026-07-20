#pragma once

#include "Patches/TestModeTiming/TimingSettingsModel.h"

#include <windows.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace gc::test_mode_timing {

enum class ConfigKey {
    GameTimeOffset,
    JudgTimeOffset,
};

enum class ConfigEditStage {
    Missing,
    Duplicate,
    Malformed,
};

struct ConfigEditError {
    ConfigEditStage stage{};
    ConfigKey key{};

    friend bool operator==(
        const ConfigEditError&,
        const ConfigEditError&) = default;
};

struct RewrittenConfig {
    std::vector<std::uint8_t> bytes;
    bool changed{};
};

[[nodiscard]] std::expected<RewrittenConfig, ConfigEditError>
RewriteTimingAssignments(
    std::span<const std::uint8_t> input,
    TimingOffsets offsets);

enum class SystemConfigStage {
    PathResolution,
    TargetOpen,
    TargetSize,
    TargetRead,
    TargetClose,
    Assignment,
    TempCreate,
    TempWrite,
    TempFlush,
    TempClose,
    Replace,
    Cleanup,
    Internal,
};

struct SystemConfigError {
    SystemConfigStage stage{};
    DWORD win32_error{};
    DWORD cleanup_error{};
    std::optional<ConfigEditError> edit_error{};
};

enum class SaveOutcome {
    Unchanged,
    Changed,
};

struct Win32FileApi {
    decltype(&::CreateFileW) create_file{&::CreateFileW};
    decltype(&::GetFileSizeEx) get_file_size{&::GetFileSizeEx};
    decltype(&::ReadFile) read_file{&::ReadFile};
    decltype(&::WriteFile) write_file{&::WriteFile};
    decltype(&::FlushFileBuffers) flush_file{&::FlushFileBuffers};
    decltype(&::CloseHandle) close_handle{&::CloseHandle};
    decltype(&::ReplaceFileW) replace_file{&::ReplaceFileW};
    decltype(&::DeleteFileW) delete_file{&::DeleteFileW};
    decltype(&::GetLastError) get_last_error{&::GetLastError};
    decltype(&::GetCurrentProcessId) get_process_id{&::GetCurrentProcessId};
};

[[nodiscard]] Win32FileApi ProductionWin32FileApi() noexcept;

class SystemConfigTimingStore {
public:
    explicit SystemConfigTimingStore(
        std::filesystem::path path,
        Win32FileApi api = ProductionWin32FileApi());

    [[nodiscard]] std::expected<SaveOutcome, SystemConfigError>
    Save(TimingOffsets offsets) noexcept;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
    Win32FileApi api_;
};

} // namespace gc::test_mode_timing

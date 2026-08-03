#pragma once

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <span>
#include <string_view>

namespace gc::locale_compatibility {

enum class FilesystemDiagnosticRole {
    game,
    service,
};

enum class AnsiFilesystemApi {
    create_file,
    get_file_attributes,
    find_first_file,
    find_next_file,
    create_directory,
    delete_file,
    move_file,
    copy_file,
};

enum class WideProbeOutcome {
    not_run,
    invalid_cp932,
    exists,
    missing,
    inaccessible,
};

inline constexpr std::array<AnsiFilesystemApi, 8>
    kObservedAnsiFilesystemApis{
        AnsiFilesystemApi::create_file,
        AnsiFilesystemApi::get_file_attributes,
        AnsiFilesystemApi::find_first_file,
        AnsiFilesystemApi::find_next_file,
        AnsiFilesystemApi::create_directory,
        AnsiFilesystemApi::delete_file,
        AnsiFilesystemApi::move_file,
        AnsiFilesystemApi::copy_file,
    };

struct AnsiFilesystemObservation {
    AnsiFilesystemApi api{};
    LPCSTR first_path{};
    LPCSTR second_path{};
    bool succeeded{};
    DWORD last_error{ERROR_SUCCESS};
};

inline constexpr std::size_t kFilesystemCategoryCapacity = 32;
inline constexpr std::size_t kFilesystemRenderedPathLimit = 192;
inline constexpr std::size_t kFilesystemInspectionLimit = 4096;

struct FilesystemDiagnosticActions {
    void* context{};
    WideProbeOutcome (*probe)(
        void*,
        AnsiFilesystemApi,
        std::wstring_view,
        std::wstring_view) noexcept{};
    void (*emit)(void*, std::string_view) noexcept{};
};

[[nodiscard]] FilesystemDiagnosticActions
ProductionFilesystemDiagnosticActions() noexcept;

class FilesystemDiagnostics {
public:
    FilesystemDiagnostics(
        FilesystemDiagnosticRole role,
        FilesystemDiagnosticActions actions) noexcept;

    FilesystemDiagnostics(const FilesystemDiagnostics&) = delete;
    FilesystemDiagnostics& operator=(const FilesystemDiagnostics&) = delete;
    FilesystemDiagnostics(FilesystemDiagnostics&&) = delete;
    FilesystemDiagnostics& operator=(FilesystemDiagnostics&&) = delete;

    void Start(std::span<const AnsiFilesystemApi> apis) noexcept;
    void Observe(const AnsiFilesystemObservation& observation) noexcept;

private:
    std::array<std::atomic_uint64_t, kFilesystemCategoryCapacity>
        non_ascii_{};
    std::array<std::atomic_uint64_t, kFilesystemCategoryCapacity>
        failures_{};
    std::atomic_bool started_{};
    std::atomic_bool cap_logged_{};
    FilesystemDiagnosticRole role_{};
    FilesystemDiagnosticActions actions_{};
};

} // namespace gc::locale_compatibility

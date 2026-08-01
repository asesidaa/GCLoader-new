#pragma once

#include <Windows.h>

#include <filesystem>

namespace gc::testmode_storage {

enum class NativeStorageProbeStage {
    none,
    create_file,
    open_file,
    write_file,
    flush_file,
};

struct NativeStorageProbeResult {
    bool available{};
    NativeStorageProbeStage failed_stage{};
    DWORD win32_error{ERROR_SUCCESS};
    DWORD cleanup_error{ERROR_SUCCESS};
    std::filesystem::path probe_path;
};

[[nodiscard]] const char* NativeStorageProbeStageName(
    NativeStorageProbeStage stage) noexcept;

[[nodiscard]] NativeStorageProbeResult ProbeNativeStorage(
    const std::filesystem::path& root) noexcept;

[[nodiscard]] NativeStorageProbeResult ProbeNativeStorage() noexcept;

} // namespace gc::testmode_storage

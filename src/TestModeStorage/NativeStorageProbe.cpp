#include "TestModeStorage/NativeStorageProbe.h"

#include <array>
#include <new>

namespace gc::testmode_storage {
namespace {

void RecordFailure(
    NativeStorageProbeResult& result,
    NativeStorageProbeStage stage,
    DWORD error) noexcept
{
    if (result.failed_stage != NativeStorageProbeStage::none) {
        return;
    }
    result.failed_stage = stage;
    result.win32_error = error == ERROR_SUCCESS
        ? ERROR_GEN_FAILURE
        : error;
}

void RecordCleanupFailure(
    NativeStorageProbeResult& result,
    DWORD error) noexcept
{
    if (result.cleanup_error == ERROR_SUCCESS) {
        result.cleanup_error = error == ERROR_SUCCESS
            ? ERROR_GEN_FAILURE
            : error;
    }
}

NativeStorageProbeResult ConstructionFailure(DWORD error) noexcept
{
    NativeStorageProbeResult result;
    RecordFailure(
        result,
        NativeStorageProbeStage::create_file,
        error);
    return result;
}

} // namespace

const char* NativeStorageProbeStageName(
    NativeStorageProbeStage stage) noexcept
{
    switch (stage) {
    case NativeStorageProbeStage::none:
        return "none";
    case NativeStorageProbeStage::create_file:
        return "create_file";
    case NativeStorageProbeStage::open_file:
        return "open_file";
    case NativeStorageProbeStage::write_file:
        return "write_file";
    case NativeStorageProbeStage::flush_file:
        return "flush_file";
    }
    return "unknown";
}

NativeStorageProbeResult ProbeNativeStorage(
    const std::filesystem::path& root) noexcept
{
    NativeStorageProbeResult result;
    std::array<wchar_t, MAX_PATH> path{};
    HANDLE file = INVALID_HANDLE_VALUE;
    bool temporary_created = false;

    try {
        do {
            if (root.empty()) {
                RecordFailure(
                    result,
                    NativeStorageProbeStage::create_file,
                    ERROR_INVALID_PARAMETER);
                break;
            }
            if (GetTempFileNameW(
                    root.c_str(),
                    L"GCT",
                    0,
                    path.data()) == 0) {
                RecordFailure(
                    result,
                    NativeStorageProbeStage::create_file,
                    GetLastError());
                break;
            }
            temporary_created = true;
            result.probe_path = path.data();

            file = CreateFileW(
                path.data(),
                GENERIC_WRITE,
                0,
                nullptr,
                TRUNCATE_EXISTING,
                FILE_ATTRIBUTE_TEMPORARY,
                nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                RecordFailure(
                    result,
                    NativeStorageProbeStage::open_file,
                    GetLastError());
                break;
            }

            constexpr unsigned char value{0};
            DWORD written{};
            const BOOL write_succeeded = WriteFile(
                    file,
                    &value,
                    sizeof(value),
                    &written,
                    nullptr);
            if (!write_succeeded || written != sizeof(value)) {
                const DWORD write_error = write_succeeded
                    ? ERROR_WRITE_FAULT
                    : GetLastError();
                RecordFailure(
                    result,
                    NativeStorageProbeStage::write_file,
                    write_error);
                break;
            }
            if (!FlushFileBuffers(file)) {
                RecordFailure(
                    result,
                    NativeStorageProbeStage::flush_file,
                    GetLastError());
                break;
            }
            result.available = true;
        } while (false);
    } catch (const std::bad_alloc&) {
        RecordFailure(
            result,
            NativeStorageProbeStage::create_file,
            ERROR_NOT_ENOUGH_MEMORY);
    } catch (...) {
        RecordFailure(
            result,
            NativeStorageProbeStage::create_file,
            ERROR_INVALID_PARAMETER);
    }

    if (file != INVALID_HANDLE_VALUE && !CloseHandle(file)) {
        RecordCleanupFailure(result, GetLastError());
    }
    if (temporary_created && !DeleteFileW(path.data())) {
        RecordCleanupFailure(result, GetLastError());
    }
    return result;
}

NativeStorageProbeResult ProbeNativeStorage() noexcept
{
    try {
        return ProbeNativeStorage(std::filesystem::path{L"D:\\"});
    } catch (const std::bad_alloc&) {
        return ConstructionFailure(ERROR_NOT_ENOUGH_MEMORY);
    } catch (...) {
        return ConstructionFailure(ERROR_INVALID_PARAMETER);
    }
}

} // namespace gc::testmode_storage

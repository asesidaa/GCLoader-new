#include "Diagnostics/CrashDumpHandler.h"

#include "Platform/Win32/Hooking/MinHookTransaction.h"

#include <Windows.h>
#include <DbgHelp.h>

#include <array>
#include <atomic>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace gc::crash_dump {
namespace {

constexpr std::size_t kMaxDumpPathChars = 32768;
constexpr std::size_t kReservedDumpSuffixChars = 96;

constexpr LONG kInstallUnattempted = 0;
constexpr LONG kInstallInProgress = 1;
constexpr LONG kInstallComplete = 2;
constexpr LONG kInstallFilterOnly = 3;
constexpr LONG kInstallUnavailable = 4;

constexpr MINIDUMP_TYPE kComprehensiveDumpType =
    static_cast<MINIDUMP_TYPE>(
        MiniDumpWithFullMemory |
        MiniDumpWithHandleData |
        MiniDumpWithUnloadedModules |
        MiniDumpWithProcessThreadData |
        MiniDumpWithFullMemoryInfo |
        MiniDumpWithThreadInfo |
        MiniDumpWithFullAuxiliaryState |
        MiniDumpWithPrivateWriteCopyMemory |
        MiniDumpIgnoreInaccessibleMemory |
        MiniDumpWithTokenInformation |
        MiniDumpWithModuleHeaders |
        MiniDumpWithAvxXStateContext);

constexpr MINIDUMP_TYPE kCompatibleFullDumpType =
    static_cast<MINIDUMP_TYPE>(
        MiniDumpWithFullMemory |
        MiniDumpWithHandleData |
        MiniDumpWithUnloadedModules |
        MiniDumpWithProcessThreadData |
        MiniDumpWithFullMemoryInfo |
        MiniDumpWithThreadInfo |
        MiniDumpIgnoreInaccessibleMemory);

std::array<wchar_t, kMaxDumpPathChars> g_dump_path{};
std::size_t g_dump_prefix_length{};
std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> g_downstream_filter{};
std::atomic_flag g_dump_in_progress = ATOMIC_FLAG_INIT;
std::atomic<LONG> g_install_state{kInstallUnattempted};
decltype(&SetUnhandledExceptionFilter) g_original_set_filter{};
win32_hooks::MinHookTransaction g_set_filter_hook{};

LONG WINAPI CrashDumpFilter(EXCEPTION_POINTERS* exception) noexcept;

InstallStatus PublicInstallStatus(LONG state) noexcept
{
    switch (state) {
    case kInstallComplete:
        return InstallStatus::installed;
    case kInstallFilterOnly:
        return InstallStatus::filter_only;
    default:
        return InstallStatus::unavailable;
    }
}

bool PrepareDumpPathPrefix() noexcept
{
    const DWORD length = GetModuleFileNameW(
        nullptr,
        g_dump_path.data(),
        static_cast<DWORD>(g_dump_path.size()));
    if (length == 0 || length >= g_dump_path.size()) {
        return false;
    }

    std::size_t last_separator = g_dump_path.size();
    std::size_t last_extension = g_dump_path.size();
    for (std::size_t index = 0; index < length; ++index) {
        const wchar_t value = g_dump_path[index];
        if (value == L'\\' || value == L'/') {
            last_separator = index;
            last_extension = g_dump_path.size();
        } else if (value == L'.') {
            last_extension = index;
        }
    }

    if (last_separator == g_dump_path.size() ||
        last_separator + 1 >= length) {
        return false;
    }

    const std::size_t stem_end =
        last_extension != g_dump_path.size() &&
            last_extension > last_separator + 1
        ? last_extension
        : length;
    constexpr wchar_t suffix[] = L"-crash-";
    constexpr std::size_t suffix_length = std::size(suffix) - 1;
    if (stem_end + suffix_length + kReservedDumpSuffixChars >=
        g_dump_path.size()) {
        return false;
    }

    for (std::size_t index = 0; index < suffix_length; ++index) {
        g_dump_path[stem_end + index] = suffix[index];
    }
    g_dump_prefix_length = stem_end + suffix_length;
    g_dump_path[g_dump_prefix_length] = L'\0';
    return true;
}

bool AppendFixedWidth(
    std::uint32_t value,
    std::size_t width,
    std::size_t* position) noexcept
{
    if (position == nullptr ||
        *position + width >= g_dump_path.size()) {
        return false;
    }

    for (std::size_t offset = 0; offset < width; ++offset) {
        const std::size_t destination = *position + width - offset - 1;
        g_dump_path[destination] =
            static_cast<wchar_t>(L'0' + value % 10);
        value /= 10;
    }
    *position += width;
    return value == 0;
}

bool AppendUnsigned(
    std::uint32_t value,
    std::size_t* position) noexcept
{
    std::array<wchar_t, 10> reversed{};
    std::size_t count = 0;
    do {
        reversed[count++] = static_cast<wchar_t>(L'0' + value % 10);
        value /= 10;
    } while (value != 0 && count < reversed.size());

    if (value != 0 || position == nullptr ||
        *position + count >= g_dump_path.size()) {
        return false;
    }
    for (std::size_t index = 0; index < count; ++index) {
        g_dump_path[*position + index] = reversed[count - index - 1];
    }
    *position += count;
    return true;
}

template<std::size_t Size>
bool AppendLiteral(
    const wchar_t (&literal)[Size],
    std::size_t* position) noexcept
{
    static_assert(Size != 0);
    constexpr std::size_t length = Size - 1;
    if (position == nullptr ||
        *position + length >= g_dump_path.size()) {
        return false;
    }
    for (std::size_t index = 0; index < length; ++index) {
        g_dump_path[*position + index] = literal[index];
    }
    *position += length;
    return true;
}

bool BuildDumpPath() noexcept
{
    SYSTEMTIME time{};
    GetSystemTime(&time);

    std::size_t position = g_dump_prefix_length;
    if (!AppendFixedWidth(time.wYear, 4, &position) ||
        !AppendFixedWidth(time.wMonth, 2, &position) ||
        !AppendFixedWidth(time.wDay, 2, &position) ||
        !AppendLiteral(L"T", &position) ||
        !AppendFixedWidth(time.wHour, 2, &position) ||
        !AppendFixedWidth(time.wMinute, 2, &position) ||
        !AppendFixedWidth(time.wSecond, 2, &position) ||
        !AppendLiteral(L".", &position) ||
        !AppendFixedWidth(time.wMilliseconds, 3, &position) ||
        !AppendLiteral(L"Z-p", &position) ||
        !AppendUnsigned(GetCurrentProcessId(), &position) ||
        !AppendLiteral(L"-t", &position) ||
        !AppendUnsigned(GetCurrentThreadId(), &position) ||
        !AppendLiteral(L".dmp", &position)) {
        return false;
    }
    g_dump_path[position] = L'\0';
    return true;
}

bool ResetDumpFile(HANDLE file) noexcept
{
    LARGE_INTEGER beginning{};
    return SetFilePointerEx(file, beginning, nullptr, FILE_BEGIN) != FALSE &&
        SetEndOfFile(file) != FALSE;
}

bool WriteDumpAttempt(
    HANDLE file,
    EXCEPTION_POINTERS* exception,
    MINIDUMP_TYPE dump_type) noexcept
{
    MINIDUMP_EXCEPTION_INFORMATION exception_information{
        .ThreadId = GetCurrentThreadId(),
        .ExceptionPointers = exception,
        .ClientPointers = FALSE,
    };
    return MiniDumpWriteDump(
               GetCurrentProcess(),
               GetCurrentProcessId(),
               file,
               dump_type,
               &exception_information,
               nullptr,
               nullptr) != FALSE;
}

void WriteCrashDump(EXCEPTION_POINTERS* exception) noexcept
{
    if (exception == nullptr || !BuildDumpPath()) {
        return;
    }

    const HANDLE file = CreateFileW(
        g_dump_path.data(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    bool written = WriteDumpAttempt(
        file,
        exception,
        kComprehensiveDumpType);
    if (!written && ResetDumpFile(file)) {
        written = WriteDumpAttempt(
            file,
            exception,
            kCompatibleFullDumpType);
    }
    if (!written && ResetDumpFile(file)) {
        static_cast<void>(WriteDumpAttempt(
            file,
            exception,
            MiniDumpNormal));
    }
    CloseHandle(file);
}

LONG InvokeDownstreamFilter(
    LPTOP_LEVEL_EXCEPTION_FILTER downstream,
    EXCEPTION_POINTERS* exception) noexcept
{
    __try {
        return downstream(exception);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
}

LONG WINAPI CrashDumpFilter(EXCEPTION_POINTERS* exception) noexcept
{
    if (g_dump_in_progress.test_and_set(std::memory_order_acq_rel)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    WriteCrashDump(exception);
    const auto downstream =
        g_downstream_filter.load(std::memory_order_acquire);
    LONG result = EXCEPTION_CONTINUE_SEARCH;
    if (downstream != nullptr && downstream != CrashDumpFilter) {
        result = InvokeDownstreamFilter(downstream, exception);
    }

    g_dump_in_progress.clear(std::memory_order_release);
    return result;
}

LPTOP_LEVEL_EXCEPTION_FILTER WINAPI SetUnhandledExceptionFilterDetour(
    LPTOP_LEVEL_EXCEPTION_FILTER requested) noexcept
{
    return g_downstream_filter.exchange(
        requested,
        std::memory_order_acq_rel);
}

} // namespace

InstallStatus InstallGameCrashDumpHandler() noexcept
{
    LONG state = g_install_state.load(std::memory_order_acquire);
    if (state != kInstallUnattempted) {
        return PublicInstallStatus(state);
    }

    LONG expected = kInstallUnattempted;
    if (!g_install_state.compare_exchange_strong(
            expected,
            kInstallInProgress,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return PublicInstallStatus(expected);
    }

    InstallStatus status = InstallStatus::unavailable;
    bool filter_registered = false;
    try {
        if (PrepareDumpPathPrefix()) {
            const auto previous =
                SetUnhandledExceptionFilter(CrashDumpFilter);
            g_downstream_filter.store(previous, std::memory_order_release);
            filter_registered = true;

            const std::array requests{
                win32_hooks::HookRequest{
                    .module_name = L"kernel32.dll",
                    .export_name = "SetUnhandledExceptionFilter",
                    .detour = reinterpret_cast<LPVOID>(
                        SetUnhandledExceptionFilterDetour),
                    .original = reinterpret_cast<LPVOID*>(
                        &g_original_set_filter),
                },
            };
            status = g_set_filter_hook.Install(requests)
                ? InstallStatus::installed
                : InstallStatus::filter_only;
        }
    } catch (...) {
        status = filter_registered
            ? InstallStatus::filter_only
            : InstallStatus::unavailable;
    }

    LONG completed_state = kInstallUnavailable;
    if (status == InstallStatus::installed) {
        completed_state = kInstallComplete;
    } else if (status == InstallStatus::filter_only) {
        completed_state = kInstallFilterOnly;
    }
    g_install_state.store(completed_state, std::memory_order_release);
    return status;
}

} // namespace gc::crash_dump

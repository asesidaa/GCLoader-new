#include "Diagnostics/CrashDumpHandler.h"
#include "Platform/Win32/UniqueHandle.h"


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
decltype(&SetUnhandledExceptionFilter) g_original_set_filter{};

LONG WINAPI CrashDumpFilter(EXCEPTION_POINTERS* exception) noexcept;

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

    gc::platform::win32::UniqueHandle file{CreateFileW(
        g_dump_path.data(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr)};
    if (!file) {
        return;
    }

    bool written = WriteDumpAttempt(
        file.get(),
        exception,
        kComprehensiveDumpType);
    if (!written && ResetDumpFile(file.get())) {
        written = WriteDumpAttempt(
            file.get(),
            exception,
            kCompatibleFullDumpType);
    }
    if (!written && ResetDumpFile(file.get())) {
        static_cast<void>(WriteDumpAttempt(
            file.get(),
            exception,
            MiniDumpNormal));
    }

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

std::expected<void, hooking::HookError> AddCrashDumpHook(hooking::HookPlan& plan) noexcept {
    if (!PrepareDumpPathPrefix())
        return std::unexpected(hooking::HookError{
            .stage = hooking::HookStage::invalid_plan,
            .identity = {"CrashDump", "SetUnhandledExceptionFilter"},
            .win32_error = ERROR_BAD_PATHNAME,
        });
    const auto previous = SetUnhandledExceptionFilter(CrashDumpFilter);
    g_downstream_filter.store(previous, std::memory_order_release);
    return plan.AddInlineExport(
        {"CrashDump", "SetUnhandledExceptionFilter"},
        {L"kernel32.dll", "SetUnhandledExceptionFilter"},
        &SetUnhandledExceptionFilterDetour, &g_original_set_filter);
}

} // namespace gc::crash_dump

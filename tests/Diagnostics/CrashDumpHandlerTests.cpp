#include "Diagnostics/CrashDumpHandler.h"

#include <Windows.h>
#include <DbgHelp.h>

#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

class UniqueHandle final {
public:
    explicit UniqueHandle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept
        : handle_{handle}
    {
    }

    ~UniqueHandle()
    {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept
    {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE handle_;
};

class UniqueView final {
public:
    explicit UniqueView(void* view = nullptr) noexcept : view_{view} {}

    ~UniqueView()
    {
        if (view_ != nullptr) {
            UnmapViewOfFile(view_);
        }
    }

    UniqueView(const UniqueView&) = delete;
    UniqueView& operator=(const UniqueView&) = delete;

    [[nodiscard]] void* get() const noexcept { return view_; }

private:
    void* view_;
};

class TempTree final {
public:
    explicit TempTree(std::filesystem::path root) : root_{std::move(root)} {}

    ~TempTree()
    {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    TempTree(const TempTree&) = delete;
    TempTree& operator=(const TempTree&) = delete;

private:
    std::filesystem::path root_;
};

int expect(bool condition, std::string_view name)
{
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

std::filesystem::path CurrentExecutablePath()
{
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(
        nullptr,
        path.data(),
        static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return {};
    }
    return std::filesystem::path{std::wstring_view{path.data(), length}};
}

std::filesystem::path CreateUniqueTempRoot()
{
    std::array<wchar_t, 32768> temp_directory{};
    const DWORD directory_length = GetTempPathW(
        static_cast<DWORD>(temp_directory.size()),
        temp_directory.data());
    if (directory_length == 0 || directory_length >= temp_directory.size()) {
        return {};
    }

    std::array<wchar_t, MAX_PATH> unique_file{};
    if (GetTempFileNameW(
            temp_directory.data(),
            L"gcd",
            0,
            unique_file.data()) == 0) {
        return {};
    }
    if (DeleteFileW(unique_file.data()) == FALSE) {
        return {};
    }

    std::filesystem::path root{unique_file.data()};
    root += L"-crash-dump-test";
    return root;
}

LONG WINAPI ConsumingDownstreamFilter(EXCEPTION_POINTERS*) noexcept
{
    return EXCEPTION_EXECUTE_HANDLER;
}

int RunCrashChild()
{
    using gc::crash_dump::InstallGameCrashDumpHandler;
    using gc::crash_dump::InstallStatus;

    const auto status = InstallGameCrashDumpHandler();
    if (status == InstallStatus::unavailable) {
        return 100;
    }
    if (status == InstallStatus::filter_only) {
        return 102;
    }

    SetUnhandledExceptionFilter(ConsumingDownstreamFilter);
    RaiseException(
        EXCEPTION_ACCESS_VIOLATION,
        EXCEPTION_NONCONTINUABLE,
        0,
        nullptr);
    return 101;
}

std::vector<std::filesystem::path> FindPidDumps(
    const std::filesystem::path& directory,
    DWORD process_id)
{
    std::vector<std::filesystem::path> dumps;
    const std::wstring pid_marker =
        L"-p" + std::to_wstring(process_id) + L"-t";
    for (const auto& entry : std::filesystem::directory_iterator{directory}) {
        if (!entry.is_regular_file() || entry.path().extension() != L".dmp") {
            continue;
        }
        const auto name = entry.path().filename().wstring();
        if (name.find(L"-crash-") != std::wstring::npos &&
            name.find(pid_marker) != std::wstring::npos) {
            dumps.push_back(entry.path());
        }
    }
    return dumps;
}

bool HasDumpStream(void* dump, MINIDUMP_STREAM_TYPE type)
{
    PMINIDUMP_DIRECTORY directory = nullptr;
    void* stream = nullptr;
    ULONG stream_size = 0;
    return MiniDumpReadDumpStream(
               dump,
               type,
               &directory,
               &stream,
               &stream_size) != FALSE &&
        directory != nullptr && stream != nullptr && stream_size != 0;
}

int InspectDump(const std::filesystem::path& dump_path)
{
    int failures = 0;
    UniqueHandle file{CreateFileW(
        dump_path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr)};
    failures += expect(file.valid(), "open generated dump");
    if (!file.valid()) {
        std::wcerr << L"Dump path: " << dump_path.c_str()
                   << L" win32_error=" << GetLastError() << L"\n";
        return failures;
    }

    UniqueHandle mapping{CreateFileMappingW(
        file.get(), nullptr, PAGE_READONLY, 0, 0, nullptr)};
    failures += expect(mapping.valid(), "map generated dump file");
    if (!mapping.valid()) {
        return failures;
    }

    UniqueView view{MapViewOfFile(
        mapping.get(), FILE_MAP_READ, 0, 0, 0)};
    failures += expect(view.get() != nullptr, "read generated dump mapping");
    if (view.get() == nullptr) {
        return failures;
    }

    const auto* header = static_cast<const MINIDUMP_HEADER*>(view.get());
    failures += expect(
        header->Signature == MINIDUMP_SIGNATURE,
        "minidump signature");

    constexpr std::uint64_t required_flags =
        MiniDumpWithFullMemory |
        MiniDumpWithHandleData |
        MiniDumpWithUnloadedModules |
        MiniDumpWithFullMemoryInfo |
        MiniDumpWithThreadInfo;
    if ((header->Flags & required_flags) != required_flags) {
        std::cerr << "Dump flags=0x" << std::hex << header->Flags
                  << " required=0x" << required_flags << std::dec << "\n";
    }
    failures += expect(
        (header->Flags & required_flags) == required_flags,
        "reported full-memory metadata flags");

    PMINIDUMP_DIRECTORY exception_directory = nullptr;
    void* exception_stream = nullptr;
    ULONG exception_size = 0;
    const BOOL has_exception = MiniDumpReadDumpStream(
        view.get(),
        ExceptionStream,
        &exception_directory,
        &exception_stream,
        &exception_size);
    failures += expect(
        has_exception != FALSE && exception_stream != nullptr &&
            exception_size >= sizeof(MINIDUMP_EXCEPTION_STREAM),
        "exception stream");
    if (has_exception != FALSE && exception_stream != nullptr &&
        exception_size >= sizeof(MINIDUMP_EXCEPTION_STREAM)) {
        const auto* exception =
            static_cast<const MINIDUMP_EXCEPTION_STREAM*>(exception_stream);
        failures += expect(
            exception->ExceptionRecord.ExceptionCode ==
                EXCEPTION_ACCESS_VIOLATION,
            "access violation exception code");
        failures += expect(
            exception->ThreadId != 0,
            "crashing thread id");
    }

    failures += expect(
        HasDumpStream(view.get(), Memory64ListStream),
        "full-memory stream");
    failures += expect(
        HasDumpStream(view.get(), MemoryInfoListStream),
        "memory-information stream");
    failures += expect(
        HasDumpStream(view.get(), ThreadInfoListStream),
        "thread-information stream");
    failures += expect(
        HasDumpStream(view.get(), HandleDataStream),
        "handle-data stream");
    return failures;
}

int RunParent()
{
    int failures = 0;
    const auto source_executable = CurrentExecutablePath();
    failures += expect(
        !source_executable.empty(),
        "resolve current test executable");
    if (source_executable.empty()) {
        return failures;
    }

    const auto root = CreateUniqueTempRoot();
    failures += expect(!root.empty(), "create unique temporary root path");
    if (root.empty()) {
        return failures;
    }
    TempTree cleanup{root};

    const auto executable_directory = root / L"崩壊-クラッシュ";
    const auto unrelated_current_directory = root / L"unrelated-cwd";
    std::error_code filesystem_error;
    std::filesystem::create_directories(
        executable_directory,
        filesystem_error);
    failures += expect(
        !filesystem_error,
        "create Unicode executable directory");
    filesystem_error.clear();
    std::filesystem::create_directories(
        unrelated_current_directory,
        filesystem_error);
    failures += expect(
        !filesystem_error,
        "create unrelated current directory");
    if (failures != 0) {
        return failures;
    }

    const auto copied_executable =
        executable_directory / source_executable.filename();
    std::filesystem::copy_file(
        source_executable,
        copied_executable,
        std::filesystem::copy_options::none,
        filesystem_error);
    failures += expect(!filesystem_error, "copy crash child executable");
    if (filesystem_error) {
        return failures;
    }

    std::wstring command_line =
        L"\"" + copied_executable.wstring() + L"\" --crash-child";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(
        copied_executable.c_str(),
        command_line.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        unrelated_current_directory.c_str(),
        &startup,
        &process);
    failures += expect(created != FALSE, "launch crash child");
    if (created == FALSE) {
        std::cerr << "CreateProcessW win32_error=" << GetLastError() << "\n";
        return failures;
    }

    UniqueHandle child_process{process.hProcess};
    UniqueHandle child_thread{process.hThread};
    const DWORD wait_result = WaitForSingleObject(
        child_process.get(),
        90'000);
    failures += expect(wait_result == WAIT_OBJECT_0, "crash child exits");
    if (wait_result != WAIT_OBJECT_0) {
        TerminateProcess(child_process.get(), 103);
        WaitForSingleObject(child_process.get(), 5'000);
        return failures;
    }

    DWORD exit_code = STILL_ACTIVE;
    failures += expect(
        GetExitCodeProcess(child_process.get(), &exit_code) != FALSE,
        "read crash child exit code");
    failures += expect(
        exit_code != 100 && exit_code != 101 && exit_code != 102 &&
            exit_code != STILL_ACTIVE,
        "child reached protected unhandled exception path");
    if (exit_code == 100 || exit_code == 101 || exit_code == 102) {
        std::cerr << "Crash child exit_code=" << exit_code << "\n";
    }

    const auto beside_executable =
        FindPidDumps(executable_directory, process.dwProcessId);
    const auto in_current_directory =
        FindPidDumps(unrelated_current_directory, process.dwProcessId);
    failures += expect(
        beside_executable.size() == 1,
        "exactly one dump beside child executable");
    failures += expect(
        in_current_directory.empty(),
        "no dump in child current directory");
    if (beside_executable.size() == 1) {
        failures += InspectDump(beside_executable.front());
    } else {
        std::cerr << "Found " << beside_executable.size()
                  << " matching dumps for child pid="
                  << process.dwProcessId << "\n";
    }
    return failures;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc == 2 && std::wstring_view{argv[1]} == L"--crash-child") {
        return RunCrashChild();
    }

    try {
        return RunParent() == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Unexpected test exception: " << error.what() << "\n";
        return 1;
    }
}

#include "Rfid/Jvs/Encoder.h"
#include "Rfid/Runtime.h"
#include "Rfid/Trace.h"
#include "Locale/FilesystemDiagnostics.h"
#include "SystemPath/SystemPathRouter.h"
#include "TestModeStorage/Hooks.h"
#include "TestModeStorage/Redirector.h"
#include "Win32Hooks/Kernel32Hooks.h"
#include "Platform/Win32/Hooking/MinHookTransaction.h"
#include "plog/Appenders/IAppender.h"
#include "plog/Init.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

class CaptureAppender final : public plog::IAppender {
public:
    void write(const plog::Record& record) override
    {
#if PLOG_CHAR_IS_UTF8
        const std::string message{record.getMessage()};
#else
        const auto message = plog::util::toNarrow(record.getMessage());
#endif
        std::scoped_lock lock{mutex_};
        messages_.push_back(message);
    }

    void Clear()
    {
        std::scoped_lock lock{mutex_};
        messages_.clear();
    }

    [[nodiscard]] bool Contains(std::string_view text) const
    {
        std::scoped_lock lock{mutex_};
        return std::ranges::any_of(messages_, [text](const auto& message) {
            return message.find(text) != std::string::npos;
        });
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::string> messages_;
};

CaptureAppender g_capture_appender;

using gc::win32_hooks::HookInstallError;
using gc::win32_hooks::HookInstallStage;
using gc::win32_hooks::HookRequest;
using gc::win32_hooks::MinHookApi;
using gc::win32_hooks::ResolverApi;

enum class OperationKind {
    resolve_module,
    resolve_export,
    initialize,
    create,
    enable,
    disable,
    remove,
};

struct Operation {
    OperationKind kind{};
    LPVOID target{};
    std::size_t index{};
};

struct FakeBackend {
    int fail_module{-1};
    int fail_export{-1};
    int fail_create{-1};
    int fail_enable{-1};
    MH_STATUS initialize_status{MH_OK};
    std::size_t module_calls{};
    std::size_t export_calls{};
    std::size_t create_calls{};
    std::size_t enable_calls{};
    std::vector<Operation> operations;
    std::vector<LPVOID> created;
    std::vector<LPVOID> enabled;
    std::vector<LPVOID> disabled;
    std::vector<LPVOID> removed;
};

FakeBackend* g_fake{};

LPVOID target_at(std::size_t index)
{
    return reinterpret_cast<LPVOID>(
        std::uintptr_t{0x1000} + index * 0x100);
}

HMODULE WINAPI FakeGetModuleHandleW(LPCWSTR)
{
    const auto index = g_fake->module_calls++;
    g_fake->operations.push_back({
        .kind = OperationKind::resolve_module,
        .index = index,
    });
    if (static_cast<int>(index) == g_fake->fail_module) {
        SetLastError(static_cast<DWORD>(1200 + index));
        return nullptr;
    }
    return reinterpret_cast<HMODULE>(
        std::uintptr_t{0x5000} + index * 0x100);
}

FARPROC WINAPI FakeGetProcAddress(HMODULE, LPCSTR)
{
    const auto index = g_fake->export_calls++;
    g_fake->operations.push_back({
        .kind = OperationKind::resolve_export,
        .index = index,
    });
    if (static_cast<int>(index) == g_fake->fail_export) {
        SetLastError(static_cast<DWORD>(2200 + index));
        return nullptr;
    }
    return reinterpret_cast<FARPROC>(target_at(index));
}

MH_STATUS WINAPI FakeInitialize()
{
    g_fake->operations.push_back({.kind = OperationKind::initialize});
    return g_fake->initialize_status;
}

MH_STATUS WINAPI FakeCreate(LPVOID target, LPVOID, LPVOID* original)
{
    const auto index = g_fake->create_calls++;
    g_fake->operations.push_back({
        .kind = OperationKind::create,
        .target = target,
        .index = index,
    });
    if (static_cast<int>(index) == g_fake->fail_create) {
        return MH_ERROR_MEMORY_ALLOC;
    }
    g_fake->created.push_back(target);
    if (original != nullptr) {
        *original = reinterpret_cast<LPVOID>(
            reinterpret_cast<std::uintptr_t>(target) + 1);
    }
    return MH_OK;
}

MH_STATUS WINAPI FakeEnable(LPVOID target)
{
    const auto index = g_fake->enable_calls++;
    g_fake->operations.push_back({
        .kind = OperationKind::enable,
        .target = target,
        .index = index,
    });
    if (static_cast<int>(index) == g_fake->fail_enable) {
        return MH_ERROR_MEMORY_PROTECT;
    }
    g_fake->enabled.push_back(target);
    return MH_OK;
}

MH_STATUS WINAPI FakeDisable(LPVOID target)
{
    g_fake->operations.push_back({
        .kind = OperationKind::disable,
        .target = target,
    });
    g_fake->disabled.push_back(target);
    return MH_OK;
}

MH_STATUS WINAPI FakeRemove(LPVOID target)
{
    g_fake->operations.push_back({
        .kind = OperationKind::remove,
        .target = target,
    });
    g_fake->removed.push_back(target);
    return MH_OK;
}

ResolverApi FakeResolver()
{
    return {
        .get_module_handle_w = FakeGetModuleHandleW,
        .get_proc_address = FakeGetProcAddress,
    };
}

MinHookApi FakeMinHook()
{
    return {
        .initialize = FakeInitialize,
        .create = FakeCreate,
        .enable = FakeEnable,
        .disable = FakeDisable,
        .remove = FakeRemove,
    };
}

struct Requests {
    std::array<LPVOID, 3> originals{};
    std::array<HookRequest, 3> values{
        HookRequest{L"kernel32.dll", "One",
                    reinterpret_cast<LPVOID>(0x7100), &originals[0]},
        HookRequest{L"kernel32.dll", "Two",
                    reinterpret_cast<LPVOID>(0x7200), &originals[1]},
        HookRequest{L"kernel32.dll", "Three",
                    reinterpret_cast<LPVOID>(0x7300), &originals[2]},
    };
};

int expect(bool condition, std::string_view name)
{
    if (condition) {
        return 0;
    }
    std::cerr << name << " failed\n";
    return 1;
}

int expect_error(
    const std::expected<void, HookInstallError>& result,
    HookInstallStage stage,
    LPCSTR export_name,
    LPVOID target,
    DWORD win32_error,
    MH_STATUS minhook_status,
    std::string_view name)
{
    if (result) {
        std::cerr << name << ": expected failure\n";
        return 1;
    }

    const auto& error = result.error();
    const bool same_name =
        export_name == nullptr
            ? error.export_name == nullptr
            : error.export_name != nullptr &&
                std::string_view{error.export_name} == export_name;
    return expect(
        error.stage == stage && same_name && error.target == target &&
            error.win32_error == win32_error &&
            error.minhook_status == minhook_status,
        name);
}

std::vector<LPVOID> reversed_targets(std::size_t count)
{
    std::vector<LPVOID> result;
    for (std::size_t index = count; index != 0; --index) {
        result.push_back(target_at(index - 1));
    }
    return result;
}

bool all_disables_precede_removes(const FakeBackend& fake)
{
    const auto first_remove = std::ranges::find_if(
        fake.operations,
        [](const Operation& operation) {
            return operation.kind == OperationKind::remove;
        });
    return std::ranges::none_of(
        std::ranges::subrange{first_remove, fake.operations.end()},
        [](const Operation& operation) {
            return operation.kind == OperationKind::disable;
        });
}

enum class OriginalCall : std::size_t {
    create_file_a,
    create_file_w,
    write_file,
    read_file,
    close_handle,
    get_comm_modem_status,
    escape_comm_function,
    clear_comm_error,
    set_comm_mask,
    setup_comm,
    get_comm_state,
    set_comm_state,
    set_comm_timeouts,
    get_comm_timeouts,
    find_first_file_a,
    find_first_file_w,
    create_directory_a,
    create_directory_w,
    delete_file_a,
    delete_file_w,
    get_file_attributes_a,
    get_file_attributes_w,
    get_disk_free_space_ex_a,
    get_disk_free_space_ex_w,
    move_file_a,
    move_file_w,
    find_next_file_a,
    copy_file_a,
    count,
};

constexpr DWORD kOriginalErrorBase = 0x6200;

struct OriginalFake {
    std::array<int, static_cast<std::size_t>(OriginalCall::count)> calls{};
    HANDLE handle{};
    const void* buffer{};
    DWORD byte_count{};
    DWORD third_dword{};
    DWORD fourth_dword{};
    LPDWORD transferred{};
    LPOVERLAPPED overlapped{};
    DWORD first_dword{};
    DWORD second_dword{};
    LPVOID first_pointer{};
    LPVOID second_pointer{};
    bool path_a_null{};
    bool path_w_null{};
    bool second_path_a_null{};
    bool second_path_w_null{};
    bool set_last_error{true};
    std::optional<DWORD> forced_last_error;
    HANDLE create_file_a_result{reinterpret_cast<HANDLE>(0x8101)};
    BOOL find_next_file_a_result{TRUE};
    BOOL copy_file_a_result{FALSE};
    BOOL fail_if_exists{};
    const void* path_pointer{};
    const void* second_path_pointer{};
    std::string path_a;
    std::wstring path_w;
    std::string second_path_a;
    std::wstring second_path_w;
    std::string find_next_name;
};

OriginalFake* g_original{};

std::size_t call_index(OriginalCall call)
{
    return static_cast<std::size_t>(call);
}

void record_call(OriginalCall call)
{
    ++g_original->calls[call_index(call)];
    if (g_original->set_last_error) {
        SetLastError(g_original->forced_last_error.value_or(
            kOriginalErrorBase + static_cast<DWORD>(call_index(call))));
    }
}

void record_path(LPCSTR path)
{
    g_original->path_pointer = path;
    g_original->path_a_null = path == nullptr;
    g_original->path_a = path == nullptr ? std::string{} : std::string{path};
}

void record_path(LPCWSTR path)
{
    g_original->path_pointer = path;
    g_original->path_w_null = path == nullptr;
    g_original->path_w = path == nullptr ? std::wstring{} : std::wstring{path};
}

void record_second_path(LPCSTR path)
{
    g_original->second_path_pointer = path;
    g_original->second_path_a_null = path == nullptr;
    g_original->second_path_a =
        path == nullptr ? std::string{} : std::string{path};
}

void record_second_path(LPCWSTR path)
{
    g_original->second_path_pointer = path;
    g_original->second_path_w_null = path == nullptr;
    g_original->second_path_w =
        path == nullptr ? std::wstring{} : std::wstring{path};
}

HANDLE WINAPI OriginalCreateFileA(
    LPCSTR path, DWORD desired_access, DWORD share_mode,
    LPSECURITY_ATTRIBUTES security, DWORD disposition,
    DWORD flags, HANDLE template_file)
{
    record_path(path);
    g_original->first_dword = desired_access;
    g_original->second_dword = share_mode;
    g_original->first_pointer = security;
    g_original->third_dword = disposition;
    g_original->fourth_dword = flags;
    g_original->handle = template_file;
    record_call(OriginalCall::create_file_a);
    return g_original->create_file_a_result;
}

HANDLE WINAPI OriginalCreateFileW(
    LPCWSTR path, DWORD desired_access, DWORD share_mode,
    LPSECURITY_ATTRIBUTES security, DWORD disposition,
    DWORD flags, HANDLE template_file)
{
    record_path(path);
    g_original->first_dword = desired_access;
    g_original->second_dword = share_mode;
    g_original->first_pointer = security;
    g_original->third_dword = disposition;
    g_original->fourth_dword = flags;
    g_original->handle = template_file;
    record_call(OriginalCall::create_file_w);
    return reinterpret_cast<HANDLE>(0x8102);
}

BOOL WINAPI OriginalWriteFile(
    HANDLE handle, LPCVOID buffer, DWORD size, LPDWORD written,
    LPOVERLAPPED overlapped)
{
    g_original->handle = handle;
    g_original->buffer = buffer;
    g_original->byte_count = size;
    g_original->transferred = written;
    g_original->overlapped = overlapped;
    if (written != nullptr) {
        *written = 73;
    }
    record_call(OriginalCall::write_file);
    return FALSE;
}

BOOL WINAPI OriginalReadFile(
    HANDLE handle, LPVOID buffer, DWORD size, LPDWORD read,
    LPOVERLAPPED overlapped)
{
    g_original->handle = handle;
    g_original->buffer = buffer;
    g_original->byte_count = size;
    g_original->transferred = read;
    g_original->overlapped = overlapped;
    if (read != nullptr) {
        *read = 37;
    }
    record_call(OriginalCall::read_file);
    return FALSE;
}

BOOL WINAPI OriginalCloseHandle(HANDLE handle)
{
    g_original->handle = handle;
    record_call(OriginalCall::close_handle);
    return FALSE;
}

BOOL WINAPI OriginalGetCommModemStatus(HANDLE handle, LPDWORD status)
{
    g_original->handle = handle;
    g_original->first_pointer = status;
    record_call(OriginalCall::get_comm_modem_status);
    return FALSE;
}

BOOL WINAPI OriginalEscapeCommFunction(HANDLE handle, DWORD function)
{
    g_original->handle = handle;
    g_original->first_dword = function;
    record_call(OriginalCall::escape_comm_function);
    return FALSE;
}

BOOL WINAPI OriginalClearCommError(
    HANDLE handle, LPDWORD errors, LPCOMSTAT status)
{
    g_original->handle = handle;
    g_original->first_pointer = errors;
    g_original->second_pointer = status;
    record_call(OriginalCall::clear_comm_error);
    return FALSE;
}

BOOL WINAPI OriginalSetCommMask(HANDLE handle, DWORD mask)
{
    g_original->handle = handle;
    g_original->first_dword = mask;
    record_call(OriginalCall::set_comm_mask);
    return FALSE;
}

BOOL WINAPI OriginalSetupComm(HANDLE handle, DWORD input, DWORD output)
{
    g_original->handle = handle;
    g_original->first_dword = input;
    g_original->second_dword = output;
    record_call(OriginalCall::setup_comm);
    return FALSE;
}

BOOL WINAPI OriginalGetCommState(HANDLE handle, LPDCB value)
{
    g_original->handle = handle;
    g_original->first_pointer = value;
    record_call(OriginalCall::get_comm_state);
    return FALSE;
}

BOOL WINAPI OriginalSetCommState(HANDLE handle, LPDCB value)
{
    g_original->handle = handle;
    g_original->first_pointer = value;
    record_call(OriginalCall::set_comm_state);
    return FALSE;
}

BOOL WINAPI OriginalSetCommTimeouts(
    HANDLE handle, LPCOMMTIMEOUTS value)
{
    g_original->handle = handle;
    g_original->first_pointer = value;
    record_call(OriginalCall::set_comm_timeouts);
    return FALSE;
}

BOOL WINAPI OriginalGetCommTimeouts(
    HANDLE handle, LPCOMMTIMEOUTS value)
{
    g_original->handle = handle;
    g_original->first_pointer = value;
    record_call(OriginalCall::get_comm_timeouts);
    return FALSE;
}

HANDLE WINAPI OriginalFindFirstFileA(LPCSTR path, LPWIN32_FIND_DATAA data)
{
    record_path(path);
    g_original->first_pointer = data;
    record_call(OriginalCall::find_first_file_a);
    return reinterpret_cast<HANDLE>(0x8201);
}

HANDLE WINAPI OriginalFindFirstFileW(LPCWSTR path, LPWIN32_FIND_DATAW data)
{
    record_path(path);
    g_original->first_pointer = data;
    record_call(OriginalCall::find_first_file_w);
    return reinterpret_cast<HANDLE>(0x8202);
}

BOOL WINAPI OriginalFindNextFileA(
    HANDLE handle,
    LPWIN32_FIND_DATAA data)
{
    g_original->handle = handle;
    g_original->first_pointer = data;
    if (g_original->find_next_file_a_result != FALSE &&
        data != nullptr && !g_original->find_next_name.empty()) {
        strncpy_s(
            data->cFileName,
            g_original->find_next_name.c_str(),
            _TRUNCATE);
    }
    record_call(OriginalCall::find_next_file_a);
    return g_original->find_next_file_a_result;
}

BOOL WINAPI OriginalCreateDirectoryA(
    LPCSTR path, LPSECURITY_ATTRIBUTES security)
{
    record_path(path);
    g_original->first_pointer = security;
    record_call(OriginalCall::create_directory_a);
    return FALSE;
}

BOOL WINAPI OriginalCreateDirectoryW(
    LPCWSTR path, LPSECURITY_ATTRIBUTES security)
{
    record_path(path);
    g_original->first_pointer = security;
    record_call(OriginalCall::create_directory_w);
    return FALSE;
}

BOOL WINAPI OriginalDeleteFileA(LPCSTR path)
{
    record_path(path);
    record_call(OriginalCall::delete_file_a);
    return FALSE;
}

BOOL WINAPI OriginalDeleteFileW(LPCWSTR path)
{
    record_path(path);
    record_call(OriginalCall::delete_file_w);
    return FALSE;
}

DWORD WINAPI OriginalGetFileAttributesA(LPCSTR path)
{
    record_path(path);
    record_call(OriginalCall::get_file_attributes_a);
    return 0x12345678;
}

DWORD WINAPI OriginalGetFileAttributesW(LPCWSTR path)
{
    record_path(path);
    record_call(OriginalCall::get_file_attributes_w);
    return 0x23456789;
}

BOOL WINAPI OriginalGetDiskFreeSpaceExA(
    LPCSTR path, PULARGE_INTEGER available, PULARGE_INTEGER total,
    PULARGE_INTEGER free)
{
    record_path(path);
    g_original->first_pointer = available;
    g_original->second_pointer = total;
    g_original->buffer = free;
    record_call(OriginalCall::get_disk_free_space_ex_a);
    return FALSE;
}

BOOL WINAPI OriginalGetDiskFreeSpaceExW(
    LPCWSTR path, PULARGE_INTEGER available, PULARGE_INTEGER total,
    PULARGE_INTEGER free)
{
    record_path(path);
    g_original->first_pointer = available;
    g_original->second_pointer = total;
    g_original->buffer = free;
    record_call(OriginalCall::get_disk_free_space_ex_w);
    return FALSE;
}

BOOL WINAPI OriginalMoveFileA(LPCSTR existing_path, LPCSTR new_path)
{
    record_path(existing_path);
    record_second_path(new_path);
    record_call(OriginalCall::move_file_a);
    return FALSE;
}

BOOL WINAPI OriginalMoveFileW(LPCWSTR existing_path, LPCWSTR new_path)
{
    record_path(existing_path);
    record_second_path(new_path);
    record_call(OriginalCall::move_file_w);
    return FALSE;
}

BOOL WINAPI OriginalCopyFileA(
    LPCSTR existing_path,
    LPCSTR new_path,
    BOOL fail_if_exists)
{
    record_path(existing_path);
    record_second_path(new_path);
    g_original->fail_if_exists = fail_if_exists;
    record_call(OriginalCall::copy_file_a);
    return g_original->copy_file_a_result;
}

gc::win32_hooks::OriginalKernel32Api OriginalApi()
{
    return {
        .create_file_a = OriginalCreateFileA,
        .create_file_w = OriginalCreateFileW,
        .write_file = OriginalWriteFile,
        .read_file = OriginalReadFile,
        .close_handle = OriginalCloseHandle,
        .get_comm_modem_status = OriginalGetCommModemStatus,
        .escape_comm_function = OriginalEscapeCommFunction,
        .clear_comm_error = OriginalClearCommError,
        .set_comm_mask = OriginalSetCommMask,
        .setup_comm = OriginalSetupComm,
        .get_comm_state = OriginalGetCommState,
        .set_comm_state = OriginalSetCommState,
        .set_comm_timeouts = OriginalSetCommTimeouts,
        .get_comm_timeouts = OriginalGetCommTimeouts,
        .find_first_file_a = OriginalFindFirstFileA,
        .find_first_file_w = OriginalFindFirstFileW,
        .create_directory_a = OriginalCreateDirectoryA,
        .create_directory_w = OriginalCreateDirectoryW,
        .delete_file_a = OriginalDeleteFileA,
        .delete_file_w = OriginalDeleteFileW,
        .get_file_attributes_a = OriginalGetFileAttributesA,
        .get_file_attributes_w = OriginalGetFileAttributesW,
        .get_disk_free_space_ex_a = OriginalGetDiskFreeSpaceExA,
        .get_disk_free_space_ex_w = OriginalGetDiskFreeSpaceExW,
        .move_file_a = OriginalMoveFileA,
        .move_file_w = OriginalMoveFileW,
        .find_next_file_a = OriginalFindNextFileA,
        .copy_file_a = OriginalCopyFileA,
    };
}

struct FilesystemCapture {
    std::vector<std::string> lines;
    OriginalCall expected_call{OriginalCall::count};
    bool callbacks_after_original{true};
    int probe_calls{};
    int emit_calls{};

    void CheckOriginalOrder() noexcept
    {
        if (expected_call != OriginalCall::count &&
            (g_original == nullptr ||
             g_original->calls[call_index(expected_call)] == 0)) {
            callbacks_after_original = false;
        }
    }

    static gc::locale_compatibility::WideProbeOutcome Probe(
        void* context,
        gc::locale_compatibility::AnsiFilesystemApi,
        std::wstring_view,
        std::wstring_view) noexcept
    {
        auto& capture = *static_cast<FilesystemCapture*>(context);
        capture.CheckOriginalOrder();
        ++capture.probe_calls;
        SetLastError(ERROR_BAD_PATHNAME);
        return gc::locale_compatibility::WideProbeOutcome::exists;
    }

    static void Emit(void* context, std::string_view line) noexcept
    {
        auto& capture = *static_cast<FilesystemCapture*>(context);
        capture.CheckOriginalOrder();
        ++capture.emit_calls;
        try {
            capture.lines.emplace_back(line);
        } catch (...) {
        }
        SetLastError(ERROR_WRITE_FAULT);
    }

    [[nodiscard]] gc::locale_compatibility::FilesystemDiagnosticActions
    Actions() noexcept
    {
        return {
            .context = this,
            .probe = &Probe,
            .emit = &Emit,
        };
    }
};

gc::system_path::SystemPathRouter DisabledSystemRouter()
{
    return gc::system_path::SystemPathRouter{
        gc::system_path::RuntimeRoot{
            .configured_path = "D:\\system",
            .resolved_path = L"D:\\system",
            .redirect_enabled = false,
        }};
}

gc::system_path::SystemPathRouter EnabledSystemRouter()
{
    return gc::system_path::SystemPathRouter{
        gc::system_path::RuntimeRoot{
            .configured_path = "H:\\system",
            .resolved_path = L"H:\\遊戲\\system",
            .redirect_enabled = true,
        }};
}

struct WorkerFake {
    int start_calls{};
    bool fail{};
    DWORD error{ERROR_NOT_ENOUGH_MEMORY};
};

WorkerFake* g_worker{};

std::expected<void, DWORD> StartWorker(
    gc::rfid::CardWorkerEntry, void*) noexcept
{
    ++g_worker->start_calls;
    if (g_worker->fail) {
        return std::unexpected(g_worker->error);
    }
    return {};
}

SHORT WorkerKeyState(int) noexcept
{
    return 0;
}

void WorkerSleep(std::chrono::milliseconds) noexcept
{
}

gc::rfid::CardWorkerApi WorkerApi(WorkerFake& worker)
{
    g_worker = &worker;
    return {
        .start_detached = StartWorker,
        .get_async_key_state = WorkerKeyState,
        .sleep_for = WorkerSleep,
    };
}

int test_success_and_already_initialized()
{
    int failures = 0;
    Requests requests;

    FakeBackend success;
    g_fake = &success;
    gc::win32_hooks::MinHookTransaction transaction{
        FakeResolver(), FakeMinHook()};
    failures += expect(transaction.Install(requests.values).has_value(),
                       "successful transaction");
    failures += expect(
        success.created == std::vector<LPVOID>{
            target_at(0), target_at(1), target_at(2)},
        "success creates each resolved target once");
    failures += expect(success.enabled == success.created,
                       "success enables each resolved target once");
    failures += expect(success.disabled.empty() && success.removed.empty(),
                       "success does not roll back");
    failures += expect(
        std::ranges::none_of(
            success.enabled,
            [](LPVOID target) {
                return target == MH_ALL_HOOKS ||
                    target == reinterpret_cast<LPVOID>(0x9999);
            }),
        "transaction never enables all or unrelated hooks");

    FakeBackend already_initialized{
        .initialize_status = MH_ERROR_ALREADY_INITIALIZED,
    };
    g_fake = &already_initialized;
    gc::win32_hooks::MinHookTransaction shared{
        FakeResolver(), FakeMinHook()};
    failures += expect(shared.Install(requests.values).has_value(),
                       "already initialized MinHook is accepted");

    const auto production_resolver =
        gc::win32_hooks::ProductionResolverApi();
    const auto production_minhook =
        gc::win32_hooks::ProductionMinHookApi();
    failures += expect(
        production_resolver.get_module_handle_w != nullptr &&
            production_resolver.get_proc_address != nullptr &&
            production_minhook.initialize != nullptr &&
            production_minhook.create != nullptr &&
            production_minhook.enable != nullptr &&
            production_minhook.disable != nullptr &&
            production_minhook.remove != nullptr,
        "production transaction APIs are complete");
    return failures;
}

int test_resolution_failures()
{
    int failures = 0;
    Requests requests;

    for (std::size_t index = 0; index < requests.values.size(); ++index) {
        FakeBackend module_failure{
            .fail_module = static_cast<int>(index),
        };
        g_fake = &module_failure;
        gc::win32_hooks::MinHookTransaction transaction{
            FakeResolver(), FakeMinHook()};
        const auto result = transaction.Install(requests.values);
        failures += expect_error(
            result, HookInstallStage::resolve_module,
            requests.values[index].export_name, nullptr,
            static_cast<DWORD>(1200 + index), MH_OK,
            "module resolution error detail");
        failures += expect(
            module_failure.module_calls == index + 1 &&
                module_failure.export_calls == index &&
                module_failure.create_calls == 0 &&
                module_failure.enable_calls == 0,
            "module failure performs no MinHook operation");

        FakeBackend export_failure{
            .fail_export = static_cast<int>(index),
        };
        g_fake = &export_failure;
        gc::win32_hooks::MinHookTransaction export_transaction{
            FakeResolver(), FakeMinHook()};
        const auto export_result =
            export_transaction.Install(requests.values);
        failures += expect_error(
            export_result, HookInstallStage::resolve_export,
            requests.values[index].export_name, nullptr,
            static_cast<DWORD>(2200 + index), MH_OK,
            "export resolution error detail");
        failures += expect(
            export_failure.module_calls == index + 1 &&
                export_failure.export_calls == index + 1 &&
                export_failure.create_calls == 0 &&
                export_failure.enable_calls == 0,
            "export failure performs no MinHook operation");
    }
    return failures;
}

int test_initialize_and_capacity_failures()
{
    int failures = 0;
    Requests requests;

    FakeBackend initialize_failure{
        .initialize_status = MH_ERROR_MEMORY_ALLOC,
    };
    g_fake = &initialize_failure;
    gc::win32_hooks::MinHookTransaction transaction{
        FakeResolver(), FakeMinHook()};
    failures += expect_error(
        transaction.Install(requests.values),
        HookInstallStage::initialize, nullptr, nullptr,
        ERROR_SUCCESS, MH_ERROR_MEMORY_ALLOC,
        "initialize error detail");
    failures += expect(
        initialize_failure.module_calls == requests.values.size() &&
            initialize_failure.export_calls == requests.values.size() &&
            initialize_failure.create_calls == 0,
        "initialize happens after complete resolution");

    std::array<HookRequest,
               gc::win32_hooks::kMaxOwnedKernel32Hooks + 1> too_many{};
    FakeBackend capacity_failure;
    g_fake = &capacity_failure;
    gc::win32_hooks::MinHookTransaction bounded{
        FakeResolver(), FakeMinHook()};
    failures += expect_error(
        bounded.Install(too_many), HookInstallStage::too_many_hooks,
        nullptr, nullptr, ERROR_INSUFFICIENT_BUFFER, MH_OK,
        "fixed transaction capacity");
    failures += expect(capacity_failure.operations.empty(),
                       "capacity failure performs no backend work");
    return failures;
}

int test_create_failures()
{
    int failures = 0;
    Requests requests;

    for (std::size_t index = 0; index < requests.values.size(); ++index) {
        FakeBackend fake{
            .fail_create = static_cast<int>(index),
        };
        g_fake = &fake;
        gc::win32_hooks::MinHookTransaction transaction{
            FakeResolver(), FakeMinHook()};
        const auto result = transaction.Install(requests.values);
        failures += expect_error(
            result, HookInstallStage::create,
            requests.values[index].export_name, target_at(index),
            ERROR_SUCCESS, MH_ERROR_MEMORY_ALLOC,
            "create error detail");
        failures += expect(fake.removed == reversed_targets(index),
                           "create rollback removes earlier targets in reverse");
        failures += expect(fake.disabled.empty(),
                           "create rollback has no enabled targets");
    }
    return failures;
}

int test_enable_failures()
{
    int failures = 0;
    Requests requests;

    for (std::size_t index = 0; index < requests.values.size(); ++index) {
        FakeBackend fake{
            .fail_enable = static_cast<int>(index),
        };
        g_fake = &fake;
        gc::win32_hooks::MinHookTransaction transaction{
            FakeResolver(), FakeMinHook()};
        const auto result = transaction.Install(requests.values);
        failures += expect_error(
            result, HookInstallStage::enable,
            requests.values[index].export_name, target_at(index),
            ERROR_SUCCESS, MH_ERROR_MEMORY_PROTECT,
            "enable error detail");
        failures += expect(fake.disabled == reversed_targets(index),
                           "enable rollback disables earlier targets in reverse");
        failures += expect(
            fake.removed == reversed_targets(requests.values.size()),
            "enable rollback removes every created target in reverse");
        failures += expect(all_disables_precede_removes(fake),
                           "enable rollback disables before removing");
    }
    return failures;
}

int test_kernel32_request_sets()
{
    constexpr std::array<std::string_view, 26> expected_names{
        "CreateFileA", "CreateFileW", "WriteFile", "ReadFile",
        "CloseHandle", "GetCommModemStatus", "EscapeCommFunction",
        "ClearCommError", "SetCommMask", "SetupComm", "GetCommState",
        "SetCommState", "SetCommTimeouts", "GetCommTimeouts",
        "FindFirstFileA", "FindFirstFileW", "CreateDirectoryA",
        "CreateDirectoryW", "DeleteFileA", "DeleteFileW",
        "GetFileAttributesA", "GetFileAttributesW",
        "GetDiskFreeSpaceExA", "GetDiskFreeSpaceExW",
        "MoveFileA", "MoveFileW",
    };

    int failures = 0;
    WorkerFake worker;
    gc::rfid::Runtime runtime{VK_F4, WorkerApi(worker)};
    gc::testmode_storage::Hooks storage_disabled{false};
    gc::testmode_storage::Hooks storage_enabled{true};
    auto system_disabled = DisabledSystemRouter();
    auto system_enabled = EnabledSystemRouter();
    OriginalFake original;
    g_original = &original;
    gc::win32_hooks::Kernel32Hooks no_paths{
        runtime, storage_disabled, system_disabled, OriginalApi()};
    gc::win32_hooks::Kernel32Hooks storage_only{
        runtime, storage_enabled, system_disabled, OriginalApi()};
    gc::win32_hooks::Kernel32Hooks system_only{
        runtime, storage_disabled, system_enabled, OriginalApi()};
    gc::win32_hooks::Kernel32Hooks both{
        runtime, storage_enabled, system_enabled, OriginalApi()};
    FilesystemCapture diagnostic_capture;
    gc::locale_compatibility::FilesystemDiagnostics diagnostics{
        gc::locale_compatibility::FilesystemDiagnosticRole::game,
        diagnostic_capture.Actions()};
    gc::win32_hooks::Kernel32Hooks diagnostic_only{
        runtime,
        storage_disabled,
        system_disabled,
        OriginalApi(),
        &diagnostics};
    gc::win32_hooks::Kernel32Hooks diagnostic_both{
        runtime,
        storage_enabled,
        system_enabled,
        OriginalApi(),
        &diagnostics};

    const auto no_paths_set = no_paths.BuildRequests();
    const auto storage_only_set = storage_only.BuildRequests();
    const auto system_only_set = system_only.BuildRequests();
    const auto both_set = both.BuildRequests();
    const auto diagnostic_only_set = diagnostic_only.BuildRequests();
    const auto diagnostic_both_set = diagnostic_both.BuildRequests();
    const auto no_paths_requests = no_paths_set.requests();
    const auto storage_requests = storage_only_set.requests();
    const auto system_requests = system_only_set.requests();
    const auto both_requests = both_set.requests();
    const auto diagnostic_only_requests = diagnostic_only_set.requests();
    const auto diagnostic_both_requests = diagnostic_both_set.requests();

    failures += expect(no_paths_requests.size() == 14,
                       "RFID-only request count");
    failures += expect(storage_requests.size() == 24,
                       "test-mode storage request count");
    failures += expect(system_requests.size() == 22,
                       "system-routing request count");
    failures += expect(both_requests.size() == 26,
                       "combined request union count");
    failures += expect(diagnostic_only_requests.size() == 21,
                       "RFID plus diagnostic request count");
    failures += expect(diagnostic_both_requests.size() == 28,
                       "combined route and diagnostic union count");
    failures += expect(gc::win32_hooks::kMaxOwnedKernel32Hooks == 32,
                       "Kernel32 request capacity is thirty-two");

    const auto exports_are_unique = [](std::span<const HookRequest> requests) {
        return std::ranges::all_of(
            requests,
            [requests](const HookRequest& candidate) {
                return candidate.export_name != nullptr &&
                    std::ranges::count_if(
                        requests,
                        [name = std::string_view{candidate.export_name}](
                            const HookRequest& request) {
                            return request.export_name != nullptr &&
                                std::string_view{request.export_name} == name;
                        }) == 1;
            });
    };
    failures += expect(exports_are_unique(no_paths_requests),
                       "RFID-only exports are unique");
    failures += expect(exports_are_unique(storage_requests),
                       "storage exports are unique");
    failures += expect(exports_are_unique(system_requests),
                       "system exports are unique");
    failures += expect(exports_are_unique(both_requests),
                       "combined exports are unique");
    failures += expect(exports_are_unique(diagnostic_only_requests),
                       "diagnostic-only exports are unique");
    failures += expect(exports_are_unique(diagnostic_both_requests),
                       "combined diagnostic exports are unique");

    for (const auto diagnostic_name :
         std::array<std::string_view, 8>{
             "CreateFileA",
             "GetFileAttributesA",
             "FindFirstFileA",
             "FindNextFileA",
             "CreateDirectoryA",
             "DeleteFileA",
             "MoveFileA",
             "CopyFileA"}) {
        failures += expect(
            std::ranges::count_if(
                diagnostic_only_requests,
                [diagnostic_name](const HookRequest& request) {
                    return request.export_name != nullptr &&
                        std::string_view{request.export_name} ==
                            diagnostic_name;
                }) == 1 &&
                std::ranges::count_if(
                    diagnostic_both_requests,
                    [diagnostic_name](const HookRequest& request) {
                        return request.export_name != nullptr &&
                            std::string_view{request.export_name} ==
                                diagnostic_name;
                    }) == 1,
            "diagnostic ANSI export appears exactly once");
    }

    failures += expect(
        std::ranges::count_if(
            both_requests,
            [](const HookRequest& request) {
                return request.export_name != nullptr &&
                    (std::string_view{request.export_name} == "MoveFileA" ||
                     std::string_view{request.export_name} == "MoveFileW");
            }) == 2,
        "combined union contains both move exports once");

    for (std::size_t index = 0; index < both_requests.size(); ++index) {
        failures += expect(
            both_requests[index].module_name != nullptr &&
                std::wstring_view{both_requests[index].module_name} ==
                    L"kernel32.dll" &&
                both_requests[index].export_name != nullptr &&
                std::string_view{both_requests[index].export_name} ==
                    expected_names[index] &&
                both_requests[index].detour != nullptr &&
                both_requests[index].original != nullptr,
            "request identity and slots");
    }
    return failures;
}

int test_create_file_and_storage_routing()
{
    using gc::win32_hooks::Kernel32Hooks;

    int failures = 0;
    OriginalFake original;
    g_original = &original;
    WorkerFake worker;
    gc::rfid::Runtime runtime{VK_F4, WorkerApi(worker)};
    gc::testmode_storage::Hooks enabled{true};
    auto system_enabled = EnabledSystemRouter();
    Kernel32Hooks hooks{runtime, enabled, system_enabled, OriginalApi()};

    const auto com_a = hooks.CreateFileA(
        "COM2", 1, 2, reinterpret_cast<LPSECURITY_ATTRIBUTES>(0x3000),
        3, 4, reinterpret_cast<HANDLE>(0x4000));
    const auto com_w = hooks.CreateFileW(
        L"COM2", 5, 6, reinterpret_cast<LPSECURITY_ATTRIBUTES>(0x3100),
        7, 8, reinterpret_cast<HANDLE>(0x4100));
    failures += expect(
        com_a == gc::rfid::EmulatedComHandle() &&
            com_w == gc::rfid::EmulatedComHandle() &&
            worker.start_calls == 2 &&
            original.calls[call_index(OriginalCall::create_file_a)] == 0 &&
            original.calls[call_index(OriginalCall::create_file_w)] == 0,
        "COM2 routing precedes storage and original APIs");

    constexpr auto path =
        "D:\\0123456789abcdef0123456789abcdef_000\\TestModeFile\\file";
    constexpr auto path_w =
        L"D:\\fedcba9876543210fedcba9876543210_000\\TestModeFile\\file";
    const auto expected = gc::testmode_storage::RedirectPathA(
        path, std::filesystem::current_path().string());
    const auto expected_w = gc::testmode_storage::RedirectPathW(
        path_w, std::filesystem::current_path().wstring());

    const auto create_result = hooks.CreateFileA(
        path, 0x11, 0x22,
        reinterpret_cast<LPSECURITY_ATTRIBUTES>(0x3300),
        0x33, 0x44, reinterpret_cast<HANDLE>(0x5500));
    failures += expect(
        create_result == reinterpret_cast<HANDLE>(0x8101) && expected &&
            original.path_a == *expected &&
            original.first_dword == 0x11 && original.second_dword == 0x22 &&
            original.first_pointer == reinterpret_cast<LPVOID>(0x3300) &&
            original.third_dword == 0x33 &&
            original.fourth_dword == 0x44 &&
            original.handle == reinterpret_cast<HANDLE>(0x5500),
        "enabled CreateFileA redirects and forwards arguments");
    failures += expect(
        GetLastError() ==
            kOriginalErrorBase + call_index(OriginalCall::create_file_a),
        "CreateFileA leaves original last error intact");

    static_cast<void>(hooks.CreateFileW(
        path_w, 0, 0, nullptr, OPEN_EXISTING, 0, nullptr));
    failures += expect(expected_w && original.path_w == *expected_w,
                       "wide routed path remains alive through original call");

    constexpr auto nonmatching = "D:\\ordinary\\file";
    static_cast<void>(hooks.GetFileAttributesA(nonmatching));
    failures += expect(original.path_a == nonmatching,
                       "nonmatching storage path is unchanged");
    static_cast<void>(hooks.GetFileAttributesA(nullptr));
    failures += expect(original.path_a_null,
                       "null storage path forwards without dereference");

    WIN32_FIND_DATAA find_a{};
    WIN32_FIND_DATAW find_w{};
    static_cast<void>(hooks.FindFirstFileA(path, &find_a));
    failures += expect(expected && original.path_a == *expected &&
                           original.first_pointer == &find_a,
                       "FindFirstFileA redirects with live storage");
    static_cast<void>(hooks.FindFirstFileW(path_w, &find_w));
    failures += expect(expected_w && original.path_w == *expected_w &&
                           original.first_pointer == &find_w,
                       "FindFirstFileW uses wide output type");

    static_cast<void>(hooks.CreateDirectoryA(path, nullptr));
    static_cast<void>(hooks.CreateDirectoryW(path_w, nullptr));
    static_cast<void>(hooks.DeleteFileA(path));
    static_cast<void>(hooks.DeleteFileW(path_w));
    failures += expect(
        original.calls[call_index(OriginalCall::create_directory_a)] == 1 &&
            original.calls[call_index(OriginalCall::create_directory_w)] == 1 &&
            original.calls[call_index(OriginalCall::delete_file_a)] == 1 &&
            original.calls[call_index(OriginalCall::delete_file_w)] == 1,
        "storage mutators each forward once");

    ULARGE_INTEGER available{}, total{}, free{};
    static_cast<void>(hooks.GetDiskFreeSpaceExA(
        "D:\\source", &available, &total, &free));
    failures += expect(original.path_a_null,
                       "enabled ANSI disk query uses current volume");
    static_cast<void>(hooks.GetDiskFreeSpaceExW(
        L"D:\\source", &available, &total, &free));
    failures += expect(original.path_w_null,
                       "enabled wide disk query uses current volume");

    OriginalFake disabled_original;
    g_original = &disabled_original;
    gc::testmode_storage::Hooks disabled{false};
    auto system_disabled = DisabledSystemRouter();
    Kernel32Hooks disabled_hooks{
        runtime, disabled, system_disabled, OriginalApi()};
    static_cast<void>(disabled_hooks.CreateFileA(
        path, 0, 0, nullptr, OPEN_EXISTING, 0, nullptr));
    static_cast<void>(disabled_hooks.GetDiskFreeSpaceExA(
        "D:\\source", &available, &total, &free));
    failures += expect(
        disabled_original.path_a == "D:\\source",
        "disabled storage routing forwards original directory");

    OriginalFake failure_original;
    g_original = &failure_original;
    WorkerFake failed_worker{
        .fail = true,
        .error = ERROR_NOT_ENOUGH_MEMORY,
    };
    gc::rfid::Runtime failed_runtime{VK_F4, WorkerApi(failed_worker)};
    Kernel32Hooks failed_hooks{
        failed_runtime, disabled, system_disabled, OriginalApi()};
    SetLastError(ERROR_SUCCESS);
    const auto failure = failed_hooks.CreateFileA(
        "COM2", 0, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    failures += expect(
        failure == INVALID_HANDLE_VALUE &&
            GetLastError() == ERROR_NOT_ENOUGH_MEMORY &&
            failed_worker.start_calls == 1 &&
            failure_original.calls[call_index(OriginalCall::create_file_a)] == 0,
        "worker failure publishes CreateFile error");
    return failures;
}

int test_filesystem_diagnostic_observation()
{
    using gc::win32_hooks::Kernel32Hooks;

    int failures = 0;
    OriginalFake original{
        .forced_last_error = ERROR_ACCESS_DENIED,
        .create_file_a_result = INVALID_HANDLE_VALUE,
    };
    g_original = &original;
    WorkerFake worker;
    gc::rfid::Runtime runtime{VK_F4, WorkerApi(worker)};
    gc::testmode_storage::Hooks storage_disabled{false};
    auto system_disabled = DisabledSystemRouter();
    FilesystemCapture capture;
    gc::locale_compatibility::FilesystemDiagnostics diagnostics{
        gc::locale_compatibility::FilesystemDiagnosticRole::game,
        capture.Actions()};
    Kernel32Hooks hooks{
        runtime,
        storage_disabled,
        system_disabled,
        OriginalApi(),
        &diagnostics};

    constexpr char missing_path[] = "data\\missing.dat";
    capture.expected_call = OriginalCall::create_file_a;
    SetLastError(ERROR_RETRY);
    const auto missing = hooks.CreateFileA(
        missing_path,
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    failures += expect(
        missing == INVALID_HANDLE_VALUE &&
            original.path_pointer == missing_path &&
            GetLastError() == ERROR_ACCESS_DENIED &&
            capture.lines.size() == 1 &&
            capture.callbacks_after_original,
        "unmatched CreateFileA failure is observed after forwarding");
    if (!capture.lines.empty()) {
        failures += expect(
            capture.lines[0].find("api=create_file") !=
                    std::string::npos &&
                capture.lines[0].find("class=failure") !=
                    std::string::npos &&
                capture.lines[0].find("succeeded=false") !=
                    std::string::npos &&
                capture.lines[0].find("error=5") !=
                    std::string::npos &&
                capture.lines[0].find(
                    "first_raw=\"data\\\\missing.dat\"") !=
                    std::string::npos,
            "CreateFileA diagnostic retains raw caller path and error");
    }

    gc::testmode_storage::Hooks storage_enabled{true};
    Kernel32Hooks storage_hooks{
        runtime,
        storage_enabled,
        system_disabled,
        OriginalApi(),
        &diagnostics};
    constexpr char storage_path[] =
        "D:\\0123456789abcdef0123456789abcdef_000"
        "\\TestModeFile\\file";
    static_cast<void>(storage_hooks.CreateFileA(
        storage_path,
        GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr));
    failures += expect(capture.lines.size() == 1,
        "test-mode storage-owned CreateFileA is not observed");

    auto system_enabled = EnabledSystemRouter();
    Kernel32Hooks system_hooks{
        runtime,
        storage_disabled,
        system_enabled,
        OriginalApi(),
        &diagnostics};
    static_cast<void>(system_hooks.CreateFileA(
        "D:\\system\\DUA\\data\\owned.bin",
        GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr));
    failures += expect(capture.lines.size() == 1,
        "system-owned CreateFileA is not observed");

    constexpr char japanese_name[] =
        "\x89\xBC" "_start.dds";
    original.find_next_file_a_result = TRUE;
    original.find_next_name = japanese_name;
    original.forced_last_error = ERROR_CRC;
    capture.expected_call = OriginalCall::find_next_file_a;
    WIN32_FIND_DATAA find_data{};
    const auto find_handle = reinterpret_cast<HANDLE>(0x9911);
    SetLastError(ERROR_RETRY);
    const auto found = hooks.FindNextFileA(find_handle, &find_data);
    failures += expect(
        found == TRUE && original.handle == find_handle &&
            original.first_pointer == &find_data &&
            std::string_view{find_data.cFileName} == japanese_name &&
            GetLastError() == ERROR_CRC &&
            capture.lines.size() == 2 &&
            capture.lines.back().find("api=find_next_file") !=
                std::string::npos &&
            capture.lines.back().find("class=non_ascii") !=
                std::string::npos &&
            capture.callbacks_after_original,
        "successful FindNextFileA observes returned CP932 name");

    original.find_next_file_a_result = FALSE;
    original.find_next_name.clear();
    original.forced_last_error = ERROR_NO_MORE_FILES;
    SetLastError(ERROR_RETRY);
    const auto completed = hooks.FindNextFileA(find_handle, &find_data);
    failures += expect(
        completed == FALSE && GetLastError() == ERROR_NO_MORE_FILES &&
            capture.lines.size() == 2,
        "FindNextFileA completion is forwarded without an event");

    constexpr char copy_source[] = "data\\source.bin";
    constexpr char copy_destination[] = "data\\destination.bin";
    original.copy_file_a_result = FALSE;
    original.forced_last_error = ERROR_FILE_EXISTS;
    capture.expected_call = OriginalCall::copy_file_a;
    SetLastError(ERROR_RETRY);
    const auto copied = hooks.CopyFileA(
        copy_source,
        copy_destination,
        TRUE);
    failures += expect(
        copied == FALSE &&
            original.path_pointer == copy_source &&
            original.second_path_pointer == copy_destination &&
            original.fail_if_exists == TRUE &&
            GetLastError() == ERROR_FILE_EXISTS &&
            capture.lines.size() == 3 &&
            capture.lines.back().find("api=copy_file") !=
                std::string::npos &&
            capture.lines.back().find("source.bin") !=
                std::string::npos &&
            capture.lines.back().find("destination.bin") !=
                std::string::npos &&
            capture.callbacks_after_original,
        "CopyFileA forwards both paths and flag before observation");
    failures += expect(
        capture.probe_calls != 0 && capture.emit_calls == 3,
        "diagnostic callbacks exercised LastError restoration");
    return failures;
}

int test_system_path_routing()
{
    using gc::win32_hooks::Kernel32Hooks;

    int failures = 0;
    OriginalFake original;
    g_original = &original;
    WorkerFake worker;
    gc::rfid::Runtime runtime{VK_F4, WorkerApi(worker)};
    gc::testmode_storage::Hooks storage{false};
    auto system = EnabledSystemRouter();
    Kernel32Hooks hooks{runtime, storage, system, OriginalApi()};

    const auto com = hooks.CreateFileA(
        "COM2", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    failures += expect(
        com == gc::rfid::EmulatedComHandle() && worker.start_calls == 2 &&
            original.calls[call_index(OriginalCall::create_file_a)] == 0 &&
            original.calls[call_index(OriginalCall::create_file_w)] == 0,
        "COM2 interception precedes system routing");

    constexpr auto logical_a = "D:\\system\\DUA\\data\\state.bin";
    constexpr auto logical_w = L"d:\\SYSTEM\\DUA\\news\\entry.bin";
    constexpr auto routed_a = L"H:\\遊戲\\system\\DUA\\data\\state.bin";
    constexpr auto routed_w = L"H:\\遊戲\\system\\DUA\\news\\entry.bin";

    SetLastError(0x7111);
    const auto created_a = hooks.CreateFileA(
        logical_a, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    failures += expect(
        created_a == reinterpret_cast<HANDLE>(0x8102) &&
            original.calls[call_index(OriginalCall::create_file_a)] == 0 &&
            original.calls[call_index(OriginalCall::create_file_w)] == 1 &&
            original.path_w == routed_a &&
            original.first_dword == GENERIC_WRITE &&
            original.second_dword == FILE_SHARE_READ &&
            GetLastError() == kOriginalErrorBase +
                call_index(OriginalCall::create_file_w),
        "matching CreateFileA uses Unicode original trampoline");

    const auto created_w = hooks.CreateFileW(
        logical_w, GENERIC_READ, FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, 0, nullptr);
    failures += expect(
        created_w == reinterpret_cast<HANDLE>(0x8102) &&
            original.path_w == routed_w &&
            original.calls[call_index(OriginalCall::create_file_w)] == 2 &&
            GetLastError() == kOriginalErrorBase +
                call_index(OriginalCall::create_file_w),
        "matching CreateFileW routes through Unicode trampoline");

    WIN32_FIND_DATAW find_data{};
    static_cast<void>(hooks.FindFirstFileW(logical_w, &find_data));
    failures += expect(
        original.path_w == routed_w && original.first_pointer == &find_data &&
            GetLastError() == kOriginalErrorBase +
                call_index(OriginalCall::find_first_file_w),
        "FindFirstFileW routes the system tree");
    static_cast<void>(hooks.CreateDirectoryW(logical_w, nullptr));
    failures += expect(
        original.path_w == routed_w &&
            GetLastError() == kOriginalErrorBase +
                call_index(OriginalCall::create_directory_w),
        "CreateDirectoryW routes the system tree");

    static_cast<void>(hooks.DeleteFileA(logical_a));
    failures += expect(
        original.calls[call_index(OriginalCall::delete_file_a)] == 0 &&
            original.calls[call_index(OriginalCall::delete_file_w)] == 1 &&
            original.path_w == routed_a &&
            GetLastError() == kOriginalErrorBase +
                call_index(OriginalCall::delete_file_w),
        "matching DeleteFileA uses Unicode original trampoline");
    static_cast<void>(hooks.DeleteFileW(logical_w));
    failures += expect(
        original.calls[call_index(OriginalCall::delete_file_w)] == 2 &&
            original.path_w == routed_w,
        "matching DeleteFileW routes through Unicode trampoline");

    const auto attributes_a = hooks.GetFileAttributesA(logical_a);
    failures += expect(
        attributes_a == 0x23456789 &&
            original.calls[call_index(OriginalCall::get_file_attributes_a)] ==
                0 &&
            original.calls[call_index(OriginalCall::get_file_attributes_w)] ==
                1 &&
            original.path_w == routed_a &&
            GetLastError() == kOriginalErrorBase +
                call_index(OriginalCall::get_file_attributes_w),
        "matching GetFileAttributesA uses Unicode original trampoline");
    const auto attributes_w = hooks.GetFileAttributesW(logical_w);
    failures += expect(
        attributes_w == 0x23456789 && original.path_w == routed_w,
        "matching GetFileAttributesW routes through Unicode trampoline");

    OriginalFake passthrough{
        .set_last_error = false,
    };
    g_original = &passthrough;
    constexpr char system2[] = "D:\\system2\\state.bin";
    constexpr DWORD incoming_error = 0x72A5;
    SetLastError(incoming_error);
    const auto passthrough_created = hooks.CreateFileA(
        system2, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    failures += expect(
        passthrough_created == reinterpret_cast<HANDLE>(0x8101) &&
            passthrough.calls[call_index(OriginalCall::create_file_a)] == 1 &&
            passthrough.calls[call_index(OriginalCall::create_file_w)] == 0 &&
            passthrough.path_pointer == system2 &&
            GetLastError() == incoming_error,
        "D system2 passes through exact pointer and incoming last error");

    SetLastError(incoming_error + 1);
    const auto null_attributes = hooks.GetFileAttributesA(nullptr);
    failures += expect(
        null_attributes == 0x12345678 && passthrough.path_a_null &&
            GetLastError() == incoming_error + 1,
        "null path passes through and preserves incoming last error");

    const char invalid_ansi[]{
        'D', ':', '\\', 's', 'y', 's', 't', 'e', 'm', '\\',
        static_cast<char>(0x81), '\0'};
    const auto invalid_probe = system.RoutePathA(invalid_ansi);
    failures += expect(!invalid_probe,
                       "invalid ANSI path exercises conversion failure");
    const DWORD conversion_error =
        invalid_probe ? ERROR_SUCCESS : invalid_probe.error();
    const auto calls_before_conversion = passthrough.calls;

    SetLastError(ERROR_SUCCESS);
    failures += expect(
        hooks.CreateFileA(
            invalid_ansi, 0, 0, nullptr, OPEN_EXISTING, 0, nullptr) ==
                INVALID_HANDLE_VALUE &&
            GetLastError() == conversion_error,
        "CreateFileA conversion failure returns native sentinel");
    SetLastError(ERROR_SUCCESS);
    failures += expect(
        !hooks.DeleteFileA(invalid_ansi) &&
            GetLastError() == conversion_error,
        "DeleteFileA conversion failure returns native sentinel");
    SetLastError(ERROR_SUCCESS);
    failures += expect(
        hooks.GetFileAttributesA(invalid_ansi) == INVALID_FILE_ATTRIBUTES &&
            GetLastError() == conversion_error,
        "GetFileAttributesA conversion failure returns native sentinel");
    failures += expect(
        passthrough.calls == calls_before_conversion,
        "conversion failures never reach an original trampoline");

    OriginalFake moves;
    g_original = &moves;
    constexpr char ordinary_source_a[] = "C:\\source.bin";
    constexpr char ordinary_destination_a[] = "C:\\destination.bin";
    constexpr char logical_source_a[] = "D:\\system\\DUA\\data\\source.bin";
    constexpr char logical_destination_a[] =
        "D:\\system\\DUA\\data\\destination.bin";
    constexpr wchar_t ordinary_source_w[] = L"C:\\source.bin";
    constexpr wchar_t ordinary_destination_w[] = L"C:\\destination.bin";
    constexpr wchar_t logical_source_w[] =
        L"D:\\system\\DUA\\data\\source.bin";
    constexpr wchar_t logical_destination_w[] =
        L"D:\\system\\DUA\\data\\destination.bin";
    constexpr auto routed_source_w =
        L"H:\\遊戲\\system\\DUA\\data\\source.bin";
    constexpr auto routed_destination_w =
        L"H:\\遊戲\\system\\DUA\\data\\destination.bin";

    static_cast<void>(hooks.MoveFileA(
        ordinary_source_a, ordinary_destination_a));
    failures += expect(
        moves.calls[call_index(OriginalCall::move_file_a)] == 1 &&
            moves.calls[call_index(OriginalCall::move_file_w)] == 0 &&
            moves.path_pointer == ordinary_source_a &&
            moves.second_path_pointer == ordinary_destination_a,
        "unmatched MoveFileA preserves both original pointers");
    static_cast<void>(hooks.MoveFileA(
        logical_source_a, ordinary_destination_a));
    failures += expect(
        moves.calls[call_index(OriginalCall::move_file_w)] == 1 &&
            moves.path_w == routed_source_w &&
            moves.second_path_w == ordinary_destination_w,
        "source-only MoveFileA converts and routes through W");
    static_cast<void>(hooks.MoveFileA(
        ordinary_source_a, logical_destination_a));
    failures += expect(
        moves.calls[call_index(OriginalCall::move_file_w)] == 2 &&
            moves.path_w == ordinary_source_w &&
            moves.second_path_w == routed_destination_w,
        "destination-only MoveFileA converts and routes through W");
    static_cast<void>(hooks.MoveFileA(
        logical_source_a, logical_destination_a));
    failures += expect(
        moves.calls[call_index(OriginalCall::move_file_w)] == 3 &&
            moves.path_w == routed_source_w &&
            moves.second_path_w == routed_destination_w,
        "fully matching MoveFileA routes both operands");
    static_cast<void>(hooks.MoveFileA(logical_source_a, nullptr));
    failures += expect(
        moves.calls[call_index(OriginalCall::move_file_w)] == 4 &&
            moves.path_w == routed_source_w && moves.second_path_w_null,
        "matching MoveFileA preserves null peer operand");

    static_cast<void>(hooks.MoveFileW(
        ordinary_source_w, ordinary_destination_w));
    failures += expect(
        moves.calls[call_index(OriginalCall::move_file_w)] == 5 &&
            moves.path_pointer == ordinary_source_w &&
            moves.second_path_pointer == ordinary_destination_w,
        "unmatched MoveFileW preserves both original pointers");
    static_cast<void>(hooks.MoveFileW(
        logical_source_w, ordinary_destination_w));
    failures += expect(
        moves.calls[call_index(OriginalCall::move_file_w)] == 6 &&
            moves.path_w == routed_source_w &&
            moves.second_path_w == ordinary_destination_w,
        "source-only MoveFileW routes source");
    static_cast<void>(hooks.MoveFileW(
        ordinary_source_w, logical_destination_w));
    failures += expect(
        moves.calls[call_index(OriginalCall::move_file_w)] == 7 &&
            moves.path_w == ordinary_source_w &&
            moves.second_path_w == routed_destination_w,
        "destination-only MoveFileW routes destination");
    static_cast<void>(hooks.MoveFileW(
        logical_source_w, logical_destination_w));
    failures += expect(
        moves.calls[call_index(OriginalCall::move_file_w)] == 8 &&
            moves.path_w == routed_source_w &&
            moves.second_path_w == routed_destination_w,
        "fully matching MoveFileW routes both operands");
    static_cast<void>(hooks.MoveFileW(logical_source_w, nullptr));
    failures += expect(
        moves.calls[call_index(OriginalCall::move_file_w)] == 9 &&
            moves.path_w == routed_source_w && moves.second_path_w_null,
        "matching MoveFileW preserves null peer operand");

    const auto move_calls_before_failure = moves.calls;
    SetLastError(ERROR_SUCCESS);
    failures += expect(
        !hooks.MoveFileA(invalid_ansi, logical_destination_a) &&
            GetLastError() == conversion_error &&
            moves.calls == move_calls_before_failure,
        "MoveFileA conversion failure returns native sentinel");
    return failures;
}

gc::rfid::jvs::EncodedFrame encode(
    gc::rfid::jvs::Address address,
    std::initializer_list<std::uint8_t> payload)
{
    const auto result = gc::rfid::jvs::EncodePacket(
        address,
        std::span<const std::uint8_t>{payload.begin(), payload.size()});
    if (!result) {
        std::terminate();
    }
    return *result;
}

int test_emulated_com_contract()
{
    using namespace gc::rfid::jvs;
    using gc::win32_hooks::Kernel32Hooks;

    int failures = 0;
    OriginalFake original;
    g_original = &original;
    WorkerFake worker;
    gc::rfid::Runtime runtime{VK_F4, WorkerApi(worker)};
    gc::testmode_storage::Hooks storage{false};
    auto system = DisabledSystemRouter();
    Kernel32Hooks hooks{runtime, storage, system, OriginalApi()};
    const auto handle = hooks.CreateFileA(
        "COM2", 0, 0, nullptr, OPEN_EXISTING, 0, nullptr);

    failures += expect(hooks.SetupComm(handle, 0x204, 0x204),
                       "emulated SetupComm");
    failures += expect(hooks.SetCommMask(handle, 1),
                       "emulated SetCommMask");
    DCB dcb{};
    failures += expect(hooks.GetCommState(handle, &dcb),
                       "emulated GetCommState");
    failures += expect(
        dcb.DCBlength == sizeof(DCB) && dcb.BaudRate == CBR_115200 &&
            dcb.ByteSize == 8 && dcb.Parity == NOPARITY &&
            dcb.StopBits == ONESTOPBIT,
        "GetCommState returns complete default DCB");
    dcb.XonChar = 0x31;
    failures += expect(hooks.SetCommState(handle, &dcb),
                       "emulated SetCommState");
    DCB stored_dcb{};
    failures += expect(hooks.GetCommState(handle, &stored_dcb) &&
                           std::memcmp(&stored_dcb, &dcb, sizeof(DCB)) == 0,
                       "GetCommState observes stored setter value");

    COMMTIMEOUTS timeouts{};
    timeouts.ReadTotalTimeoutConstant = 20;
    failures += expect(hooks.SetCommTimeouts(handle, &timeouts),
                       "emulated SetCommTimeouts");
    COMMTIMEOUTS stored_timeouts{};
    failures += expect(
        hooks.GetCommTimeouts(handle, &stored_timeouts) &&
            std::memcmp(
                &stored_timeouts, &timeouts, sizeof(COMMTIMEOUTS)) == 0,
        "GetCommTimeouts observes stored setter value");

    DWORD modem = 0xFFFFFFFF;
    failures += expect(hooks.GetCommModemStatus(handle, &modem) && modem == 0,
                       "modem status before assignment");

    const auto assignment = encode(
        address::broadcast, {command::set_address.value, 0x01});
    const auto split = assignment.bytes().size() / 2;
    DWORD written = 0xFFFFFFFF;
    failures += expect(
        hooks.WriteFile(
            handle, assignment.bytes().data(), static_cast<DWORD>(split),
            &written, nullptr) && written == split,
        "first fragmented emulated write");
    std::array<std::byte, 8> read_storage{};
    DWORD read = 0xFFFFFFFF;
    failures += expect(
        hooks.ReadFile(handle, read_storage.data(), read_storage.size(),
                       &read, nullptr) && read == 0,
        "incomplete packet read is timing-free and empty");
    failures += expect(
        hooks.WriteFile(
            handle, assignment.bytes().data() + split,
            static_cast<DWORD>(assignment.bytes().size() - split),
            &written, nullptr) &&
            written == assignment.bytes().size() - split,
        "second fragmented emulated write");
    failures += expect(
        hooks.GetCommModemStatus(handle, &modem) && modem == MS_CTS_ON,
        "modem status after assignment");

    DWORD errors = 0xFFFFFFFF;
    COMSTAT status;
    std::memset(&status, 0xFF, sizeof(status));
    failures += expect(hooks.ClearCommError(handle, &errors, &status) &&
                           errors == 0 &&
                           status.cbInQue == runtime.port().PendingByteCount(),
                       "ClearCommError initializes both optional outputs");
    const auto expected_status = runtime.port().CommStatus();
    failures += expect(
        std::memcmp(&status, &expected_status, sizeof(COMSTAT)) == 0,
        "ClearCommError returns fully initialized COMSTAT");
    failures += expect(hooks.ClearCommError(handle, nullptr, nullptr),
                       "ClearCommError accepts both outputs null");

    const auto expected_assignment = encode(address::master, {0x01, 0x01});
    std::vector<std::byte> response;
    while (runtime.port().PendingByteCount() != 0) {
        std::byte value{};
        read = 99;
        failures += expect(
            hooks.ReadFile(handle, &value, 1, &read, nullptr) && read == 1,
            "one-byte emulated read");
        response.push_back(value);
    }
    failures += expect(std::ranges::equal(
                           response, expected_assignment.bytes()),
                       "fragmented reads preserve one encoded frame");

    const auto first_request = encode(Address{0x01}, {0x11});
    const auto second_request = encode(Address{0x01}, {0x12});
    std::vector<std::byte> pipeline;
    pipeline.insert(
        pipeline.end(), first_request.bytes().begin(), first_request.bytes().end());
    pipeline.insert(
        pipeline.end(), second_request.bytes().begin(), second_request.bytes().end());
    failures += expect(
        hooks.WriteFile(handle, pipeline.data(),
                        static_cast<DWORD>(pipeline.size()), &written, nullptr) &&
            written == pipeline.size(),
        "pipelined raw write count");
    response.clear();
    while (runtime.port().PendingByteCount() != 0) {
        std::array<std::byte, 32> fragment{};
        failures += expect(
            hooks.ReadFile(handle, fragment.data(), fragment.size(),
                           &read, nullptr),
            "pipelined reply read");
        response.insert(response.end(), fragment.begin(), fragment.begin() + read);
    }
    const auto expected_first = encode(address::master, {0x01, 0x01, 0x13});
    failures += expect(std::ranges::equal(response, expected_first.bytes()),
                       "pipelining never exposes a second frame");
    read = 99;
    failures += expect(hooks.ReadFile(
                           handle, read_storage.data(), read_storage.size(),
                           &read, nullptr) && read == 0,
                       "empty synchronous read returns zero");

    OVERLAPPED overlapped{};
    written = 99;
    SetLastError(ERROR_SUCCESS);
    failures += expect(
        !hooks.WriteFile(handle, nullptr, 1, &written, nullptr) &&
            written == 0 && GetLastError() == ERROR_INVALID_PARAMETER,
        "invalid write buffer initializes count and fails");
    written = 99;
    failures += expect(
        !hooks.WriteFile(handle, read_storage.data(), 1, &written, &overlapped) &&
            written == 0 && GetLastError() == ERROR_INVALID_PARAMETER,
        "overlapped emulated write initializes count and fails");
    failures += expect(
        !hooks.WriteFile(handle, read_storage.data(), 1, nullptr, nullptr) &&
            GetLastError() == ERROR_INVALID_PARAMETER,
        "synchronous write requires count output");
    read = 99;
    failures += expect(
        !hooks.ReadFile(handle, nullptr, 1, &read, nullptr) && read == 0 &&
            GetLastError() == ERROR_INVALID_PARAMETER,
        "invalid read buffer initializes count and fails");
    read = 99;
    failures += expect(
        !hooks.ReadFile(handle, read_storage.data(), 1, &read, &overlapped) &&
            read == 0 && GetLastError() == ERROR_INVALID_PARAMETER,
        "overlapped emulated read initializes count and fails");

    failures += expect(
        !hooks.GetCommState(handle, nullptr) &&
            GetLastError() == ERROR_INVALID_PARAMETER &&
            !hooks.SetCommState(handle, nullptr) &&
            !hooks.GetCommTimeouts(handle, nullptr) &&
            !hooks.SetCommTimeouts(handle, nullptr) &&
            !hooks.GetCommModemStatus(handle, nullptr),
        "required COM pointers are validated");
    failures += expect(hooks.EscapeCommFunction(handle, SETDTR) &&
                           hooks.EscapeCommFunction(handle, CLRDTR),
                       "emulated line controls");
    failures += expect(hooks.CloseHandle(handle) && !runtime.port().IsOpen(),
                       "emulated close resets logical port");
    return failures;
}

int test_non_emulated_forwarding()
{
    using gc::win32_hooks::Kernel32Hooks;

    int failures = 0;
    OriginalFake original;
    g_original = &original;
    WorkerFake worker;
    gc::rfid::Runtime runtime{VK_F4, WorkerApi(worker)};
    gc::testmode_storage::Hooks storage{false};
    auto system = DisabledSystemRouter();
    Kernel32Hooks hooks{runtime, storage, system, OriginalApi()};

    const auto handle = reinterpret_cast<HANDLE>(0x7777);
    std::array<std::byte, 4> buffer{};
    DWORD transferred = 0;
    OVERLAPPED overlapped{};
    SetLastError(ERROR_SUCCESS);
    failures += expect(
        !hooks.WriteFile(handle, buffer.data(), buffer.size(),
                         &transferred, &overlapped) &&
            transferred == 73 && original.handle == handle &&
            original.buffer == buffer.data() &&
            original.byte_count == buffer.size() &&
            original.transferred == &transferred &&
            original.overlapped == &overlapped &&
            GetLastError() ==
                kOriginalErrorBase + call_index(OriginalCall::write_file),
        "non-emulated write forwards every argument and last error");

    static_cast<void>(hooks.ReadFile(
        handle, buffer.data(), buffer.size(), &transferred, &overlapped));
    static_cast<void>(hooks.CloseHandle(handle));
    DWORD value{};
    COMSTAT status{};
    DCB dcb{};
    COMMTIMEOUTS timeouts{};
    static_cast<void>(hooks.GetCommModemStatus(handle, &value));
    static_cast<void>(hooks.EscapeCommFunction(handle, SETRTS));
    static_cast<void>(hooks.ClearCommError(handle, &value, &status));
    static_cast<void>(hooks.SetCommMask(handle, 1));
    static_cast<void>(hooks.SetupComm(handle, 2, 3));
    static_cast<void>(hooks.GetCommState(handle, &dcb));
    static_cast<void>(hooks.SetCommState(handle, &dcb));
    static_cast<void>(hooks.SetCommTimeouts(handle, &timeouts));
    static_cast<void>(hooks.GetCommTimeouts(handle, &timeouts));

    for (auto call = call_index(OriginalCall::write_file);
         call <= call_index(OriginalCall::get_comm_timeouts); ++call) {
        failures += expect(original.calls[call] == 1,
                           "each non-emulated COM call forwards once");
    }
    failures += expect(
        original.handle == handle &&
            GetLastError() ==
                kOriginalErrorBase +
                    call_index(OriginalCall::get_comm_timeouts),
        "forwarded COM call performs no operation after original");
    return failures;
}

int test_diagnostic_formatting()
{
    using gc::rfid::trace::FormatBytes;
    using gc::win32_hooks::HookInstallStageName;

    int failures = 0;
    constexpr std::array bytes{
        std::byte{0xE0}, std::byte{0x00}, std::byte{0xD0},
        std::byte{0xFF}, std::byte{0x42}};

    failures += expect(
        FormatBytes({}) == "<empty>",
        "empty RFID trace bytes are explicit");
    failures += expect(
        FormatBytes(bytes) == "e0 00 d0 ff 42",
        "RFID trace bytes use stable lowercase hex");
    failures += expect(
        FormatBytes(bytes, 3) == "e0 00 d0 ... (+2 bytes)",
        "RFID trace bytes report bounded truncation");
    failures += expect(
        HookInstallStageName(HookInstallStage::resolve_export) ==
            std::string_view{"resolve_export"} &&
            HookInstallStageName(HookInstallStage::enable) ==
                std::string_view{"enable"},
        "hook installation stages have stable diagnostic names");
    return failures;
}

int test_successful_operations_are_not_traced()
{
    using gc::win32_hooks::Kernel32Hooks;

    int failures = 0;
    g_capture_appender.Clear();

    FakeBackend backend;
    g_fake = &backend;
    LPVOID original{};
    const std::array requests{
        HookRequest{
            .module_name = L"kernel32.dll",
            .export_name = "TraceApi",
            .detour = target_at(99),
            .original = &original,
        }};
    gc::win32_hooks::MinHookTransaction transaction{
        FakeResolver(), FakeMinHook()};
    failures += expect(
        transaction.Install(requests).has_value(),
        "quiet hook transaction installs");

    WorkerFake worker;
    gc::rfid::Runtime runtime{VK_F4, WorkerApi(worker)};
    gc::testmode_storage::Hooks storage{false};
    auto system = DisabledSystemRouter();
    Kernel32Hooks hooks{runtime, storage, system, OriginalApi()};
    const auto handle = hooks.CreateFileA(
        "COM2", GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, 0, nullptr);
    failures += expect(
        handle == gc::rfid::EmulatedComHandle(),
        "quiet COM2 open succeeds");

    DCB dcb{};
    COMMTIMEOUTS timeouts{};
    DWORD modem_status{};
    DWORD errors{};
    COMSTAT status{};
    failures += expect(
        hooks.SetupComm(handle, 0x204, 0x204) &&
            hooks.SetCommMask(handle, 1) &&
            hooks.GetCommState(handle, &dcb) &&
            hooks.SetCommState(handle, &dcb) &&
            hooks.GetCommTimeouts(handle, &timeouts) &&
            hooks.SetCommTimeouts(handle, &timeouts) &&
            hooks.GetCommModemStatus(handle, &modem_status) &&
            hooks.EscapeCommFunction(handle, SETDTR),
        "quiet COM2 setup succeeds");

    const auto request = encode(gc::rfid::jvs::Address{0xFF}, {0xF1, 0x01});
    DWORD written{};
    failures += expect(
        hooks.WriteFile(
            handle, request.bytes().data(),
            static_cast<DWORD>(request.bytes().size()),
            &written, nullptr),
        "quiet request write succeeds");

    failures += expect(
        hooks.ClearCommError(handle, &errors, &status),
        "quiet queue query succeeds");
    std::array<std::byte, 64> reply{};
    DWORD read{};
    failures += expect(
        hooks.ReadFile(
            handle, reply.data(), static_cast<DWORD>(reply.size()),
            &read, nullptr) &&
            read != 0 && hooks.CloseHandle(handle),
        "quiet reply read and close succeed");

    failures += expect(
        !g_capture_appender.Contains("MinHookTransaction: transaction") &&
            !g_capture_appender.Contains(
                "MinHookTransaction: resolved export=") &&
            !g_capture_appender.Contains(
                "MinHookTransaction: created export=") &&
            !g_capture_appender.Contains(
                "MinHookTransaction: enabled export=") &&
            !g_capture_appender.Contains(
                "MinHookTransaction: initialization") &&
            !g_capture_appender.Contains("RFID COM2 trace api=") &&
            !g_capture_appender.Contains("RFID JVS decoded") &&
            !g_capture_appender.Contains("RFID JVS no reply") &&
            !g_capture_appender.Contains("RFID JVS retransmit queued") &&
            !g_capture_appender.Contains("RFID JVS queued reply"),
        "successful hook, COM, and JVS operations are not traced");
    return failures;
}

} // namespace

int main()
{
    plog::init(plog::info, &g_capture_appender);
    const int failures =
        test_success_and_already_initialized() +
        test_resolution_failures() +
        test_initialize_and_capacity_failures() +
        test_create_failures() +
        test_enable_failures() +
        test_kernel32_request_sets() +
        test_create_file_and_storage_routing() +
        test_filesystem_diagnostic_observation() +
        test_system_path_routing() +
        test_emulated_com_contract() +
        test_non_emulated_forwarding() +
        test_diagnostic_formatting() +
        test_successful_operations_are_not_traced();
    return failures == 0 ? 0 : 1;
}

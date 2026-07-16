#include "Rfid/Jvs/Encoder.h"
#include "Rfid/Runtime.h"
#include "TestModeStorage/Hooks.h"
#include "TestModeStorage/Redirector.h"
#include "Win32Hooks/Kernel32Hooks.h"
#include "Win32Hooks/MinHookTransaction.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

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
    std::string path_a;
    std::wstring path_w;
};

OriginalFake* g_original{};

std::size_t call_index(OriginalCall call)
{
    return static_cast<std::size_t>(call);
}

void record_call(OriginalCall call)
{
    ++g_original->calls[call_index(call)];
    SetLastError(kOriginalErrorBase + static_cast<DWORD>(call_index(call)));
}

void record_path(LPCSTR path)
{
    g_original->path_a_null = path == nullptr;
    g_original->path_a = path == nullptr ? std::string{} : std::string{path};
}

void record_path(LPCWSTR path)
{
    g_original->path_w_null = path == nullptr;
    g_original->path_w = path == nullptr ? std::wstring{} : std::wstring{path};
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
    return reinterpret_cast<HANDLE>(0x8101);
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
    };
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
    constexpr std::array<std::string_view, 24> expected_names{
        "CreateFileA", "CreateFileW", "WriteFile", "ReadFile",
        "CloseHandle", "GetCommModemStatus", "EscapeCommFunction",
        "ClearCommError", "SetCommMask", "SetupComm", "GetCommState",
        "SetCommState", "SetCommTimeouts", "GetCommTimeouts",
        "FindFirstFileA", "FindFirstFileW", "CreateDirectoryA",
        "CreateDirectoryW", "DeleteFileA", "DeleteFileW",
        "GetFileAttributesA", "GetFileAttributesW",
        "GetDiskFreeSpaceExA", "GetDiskFreeSpaceExW",
    };

    int failures = 0;
    WorkerFake worker;
    gc::rfid::Runtime runtime{VK_F4, WorkerApi(worker)};
    gc::testmode_storage::Hooks storage{false};
    OriginalFake original;
    g_original = &original;
    gc::win32_hooks::Kernel32Hooks hooks{
        runtime, storage, OriginalApi()};

    const auto com_only_set = hooks.BuildRequests(false);
    const auto com_only = com_only_set.requests();
    failures += expect(com_only.size() == 14,
                       "disabled storage builds fourteen requests");
    const auto all_set = hooks.BuildRequests(true);
    const auto all = all_set.requests();
    failures += expect(all.size() == expected_names.size(),
                       "enabled storage builds twenty-four requests");

    for (std::size_t index = 0; index < all.size(); ++index) {
        failures += expect(
            all[index].module_name != nullptr &&
                std::wstring_view{all[index].module_name} == L"kernel32.dll" &&
                all[index].export_name != nullptr &&
                std::string_view{all[index].export_name} == expected_names[index] &&
                all[index].detour != nullptr && all[index].original != nullptr,
            "request identity and slots");
        failures += expect(
            std::ranges::count_if(
                all,
                [name = expected_names[index]](const HookRequest& request) {
                    return request.export_name != nullptr &&
                        std::string_view{request.export_name} == name;
                }) == 1,
            "request exports are unique");
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
    Kernel32Hooks hooks{runtime, enabled, OriginalApi()};

    const auto com_a = hooks.CreateFileA(
        "COM2", 1, 2, reinterpret_cast<LPSECURITY_ATTRIBUTES>(0x3000),
        3, 4, reinterpret_cast<HANDLE>(0x4000));
    const auto com_w = hooks.CreateFileW(
        L"COM2", 5, 6, reinterpret_cast<LPSECURITY_ATTRIBUTES>(0x3100),
        7, 8, reinterpret_cast<HANDLE>(0x4100));
    failures += expect(
        com_a == gc::rfid::EmulatedComHandle() &&
            com_w == gc::rfid::EmulatedComHandle() &&
            worker.start_calls == 1 &&
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
    Kernel32Hooks disabled_hooks{runtime, disabled, OriginalApi()};
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
    Kernel32Hooks failed_hooks{failed_runtime, disabled, OriginalApi()};
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
    Kernel32Hooks hooks{runtime, storage, OriginalApi()};
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
    Kernel32Hooks hooks{runtime, storage, OriginalApi()};

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

} // namespace

int main()
{
    const int failures =
        test_success_and_already_initialized() +
        test_resolution_failures() +
        test_initialize_and_capacity_failures() +
        test_create_failures() +
        test_enable_failures() +
        test_kernel32_request_sets() +
        test_create_file_and_storage_routing() +
        test_emulated_com_contract() +
        test_non_emulated_forwarding();
    return failures == 0 ? 0 : 1;
}

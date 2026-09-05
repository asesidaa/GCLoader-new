#include "Nesys/ThreadPriorityOverride.h"

#include <Windows.h>

#include <atomic>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <iomanip>
#include <intrin.h>
#include <limits>
#include <optional>

#include "plog/Log.h"

namespace gc::nesys_service {
namespace {

ExecutableImageRange g_executable_image{};
ProcessRole g_process_role{ProcessRole::Game};
SetThreadPriorityFn g_original_set_thread_priority{};
std::atomic_bool g_first_clamp_logged{false};

bool try_read_executable_image_range(
    HMODULE module,
    ExecutableImageRange* output) noexcept {
    if (module == nullptr || output == nullptr) {
        return false;
    }

    __try {
        const auto begin = reinterpret_cast<std::uintptr_t>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0) {
            return false;
        }

        const auto nt_offset = static_cast<std::uintptr_t>(dos->e_lfanew);
        if (begin >
            std::numeric_limits<std::uintptr_t>::max() - nt_offset) {
            return false;
        }

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            begin + nt_offset);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
            nt->OptionalHeader.SizeOfImage == 0) {
            return false;
        }

        const auto size = static_cast<std::uintptr_t>(
            nt->OptionalHeader.SizeOfImage);
        if (begin > std::numeric_limits<std::uintptr_t>::max() - size) {
            return false;
        }

        *output = ExecutableImageRange{begin, begin + size};
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void log_first_clamp(
    std::uintptr_t caller,
    int requested,
    int effective) noexcept {
    if (g_first_clamp_logged.exchange(true, std::memory_order_relaxed)) {
        return;
    }

    const auto caller_rva = g_executable_image.Contains(caller)
        ? caller - g_executable_image.begin
        : caller;
    try {
        PLOG_INFO
            << "ThreadPriorityOverride: first executable clamp"
            << " role=" << ProcessRoleName(g_process_role)
            << " caller_rva=0x" << std::hex << caller_rva
            << std::dec
            << " requested=" << requested
            << " effective=" << effective;
    } catch (...) {
    }
}

BOOL WINAPI set_thread_priority_detour(HANDLE thread, int priority) {
    return ForwardExecutableThreadPriority(
        g_executable_image,
        reinterpret_cast<std::uintptr_t>(_ReturnAddress()),
        thread,
        priority,
        g_original_set_thread_priority,
        &log_first_clamp);
}

} // namespace

bool ExecutableImageRange::Contains(std::uintptr_t address) const noexcept {
    return begin < end && address >= begin && address < end;
}

std::optional<ExecutableImageRange> ReadExecutableImageRange(
    HMODULE module) noexcept {
    ExecutableImageRange image{};
    if (!try_read_executable_image_range(module, &image)) {
        return std::nullopt;
    }
    return image;
}

int NormalizeExecutableThreadPriority(
    const ExecutableImageRange& image,
    std::uintptr_t caller,
    int requested_priority) noexcept {
    if (image.Contains(caller) &&
        requested_priority < THREAD_PRIORITY_NORMAL) {
        return THREAD_PRIORITY_NORMAL;
    }
    return requested_priority;
}

BOOL ForwardExecutableThreadPriority(
    const ExecutableImageRange& image,
    std::uintptr_t caller,
    HANDLE thread,
    int requested_priority,
    SetThreadPriorityFn original,
    ThreadPriorityClampDiagnosticFn diagnostic) noexcept {
    if (original == nullptr) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }

    const int effective = NormalizeExecutableThreadPriority(
        image,
        caller,
        requested_priority);
    const BOOL result = original(thread, effective);
    const DWORD last_error = GetLastError();
    if (effective != requested_priority && diagnostic != nullptr) {
        diagnostic(caller, requested_priority, effective);
    }
    SetLastError(last_error);
    return result;
}

bool InitializeThreadPriorityOverride(ProcessRole role) noexcept {
    const auto image = ReadExecutableImageRange(GetModuleHandleW(nullptr));
    if (!image.has_value()) {
        return false;
    }

    g_executable_image = *image;
    g_process_role = role;
    g_first_clamp_logged.store(false, std::memory_order_relaxed);
    return true;
}

std::expected<void, hooking::HookError> AddThreadPriorityHook(
    hooking::HookPlan& hooks) noexcept {
    if (const auto added = hooks.AddInlineExport(
            {"NesysPriority", "SetThreadPriority"}, {L"kernel32.dll", "SetThreadPriority"},
            &set_thread_priority_detour, &g_original_set_thread_priority); !added) return added;
    return {};
}

} // namespace gc::nesys_service

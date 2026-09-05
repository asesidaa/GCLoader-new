#pragma once

#include "Platform/Win32/Hooking/HookPlan.h"
#include "Nesys/NesysServiceProcess.h"

#include <Windows.h>

// ReSharper disable once CppUnusedIncludeDirective
#include <cstdint>
#include <optional>
#include <vector>

namespace gc::nesys_service {

struct ExecutableImageRange {
    std::uintptr_t begin{};
    std::uintptr_t end{};

    bool Contains(std::uintptr_t address) const noexcept;
};

using SetThreadPriorityFn = BOOL(WINAPI*)(HANDLE, int);
using ThreadPriorityClampDiagnosticFn =
    void(*)(std::uintptr_t, int, int) noexcept;

std::optional<ExecutableImageRange> ReadExecutableImageRange(
    HMODULE module) noexcept;

int NormalizeExecutableThreadPriority(
    const ExecutableImageRange& image,
    std::uintptr_t caller,
    int requested_priority) noexcept;

BOOL ForwardExecutableThreadPriority(
    const ExecutableImageRange& image,
    std::uintptr_t caller,
    HANDLE thread,
    int requested_priority,
    SetThreadPriorityFn original,
    ThreadPriorityClampDiagnosticFn diagnostic) noexcept;

bool InitializeThreadPriorityOverride(ProcessRole role) noexcept;
[[nodiscard]] std::expected<void, hooking::HookError> AddThreadPriorityHook(
    hooking::HookPlan& hooks) noexcept;

} // namespace gc::nesys_service

#pragma once

#include "Platform/Win32/Hooking/HookPlan.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gc::nesys_service::diagnostics {

[[nodiscard]] std::uint64_t MonotonicMilliseconds() noexcept;
[[nodiscard]] bool IsNesysPipeNameA(LPCSTR path) noexcept;
[[nodiscard]] bool IsNesysPipeNameW(LPCWSTR path) noexcept;
[[nodiscard]] bool IsTrackedNesysPipeHandle(HANDLE handle) noexcept;

void ObserveGamePipeOpenA(
    LPCSTR path,
    HANDLE handle,
    std::uint64_t started_ms,
    std::uint64_t finished_ms,
    DWORD last_error) noexcept;
void ObserveGamePipeOpenW(
    LPCWSTR path,
    HANDLE handle,
    std::uint64_t started_ms,
    std::uint64_t finished_ms,
    DWORD last_error) noexcept;
void ObserveGamePipeWrite(
    HANDLE handle,
    LPCVOID buffer,
    DWORD bytes_to_write,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept;
void ObserveGamePipeFlush(
    HANDLE handle,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept;
void ObserveGameHandleClose(HANDLE handle) noexcept;

[[nodiscard]] std::expected<void, hooking::HookError> AddServiceRequestPipelineHooks(
    hooking::HookPlan& hooks) noexcept;

} // namespace gc::nesys_service::diagnostics

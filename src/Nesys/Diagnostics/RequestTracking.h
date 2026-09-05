#pragma once

#include "Nesys/Diagnostics/RequestFormatting.h"

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

} // namespace gc::nesys_service::diagnostics

namespace gc::nesys_service::diagnostics::detail {
enum class TrackedHandleKind : std::uint8_t {
    None,
    Pipe,
    VendorLog,
};
bool is_vendor_log_path(LPCSTR path) noexcept;
TrackedHandleKind tracked_handle_kind(HANDLE handle) noexcept;
void observe_pipe_open(
    PipeEndpoint endpoint,
    HANDLE handle,
    std::uint64_t started_ms,
    std::uint64_t finished_ms,
    DWORD last_error) noexcept;
void observe_pipe_write(
    HANDLE handle,
    LPCVOID buffer,
    DWORD bytes_to_write,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept;
void observe_pipe_flush(
    HANDLE handle,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept;
void forget_pipe(HANDLE handle) noexcept;
void observe_vendor_log_open(
    LPCSTR path,
    HANDLE handle,
    std::uint64_t started_ms,
    std::uint64_t finished_ms,
    DWORD last_error) noexcept;
void observe_vendor_log_seek(
    HANDLE handle,
    DWORD result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept;
void observe_vendor_log_write(
    HANDLE handle,
    DWORD requested,
    DWORD transferred,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept;
void observe_vendor_log_close(
    HANDLE handle,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept;
void observe_http_open(
    HINTERNET handle,
    BOOL success,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept;
void observe_http_timeouts(
    HINTERNET handle,
    int resolve_timeout,
    int connect_timeout,
    int send_timeout,
    int receive_timeout,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept;
void observe_http_connect(
    HINTERNET session_handle,
    HINTERNET connection_handle,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept;
void observe_http_open_request(
    HINTERNET connection_handle,
    LPCWSTR path,
    HINTERNET request_handle,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept;
void observe_http_send(
    HINTERNET handle,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept;
void observe_http_receive(
    HINTERNET handle,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept;
void observe_http_query(
    HINTERNET handle,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept;
void observe_http_read(
    HINTERNET handle,
    DWORD transferred,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept;
void observe_http_close(
    HINTERNET handle,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept;
}

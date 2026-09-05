#include "Nesys/Diagnostics/RequestHooks.h"
#include "Nesys/Diagnostics/RequestTracking.h"

namespace gc::nesys_service::diagnostics {
namespace {
using namespace detail;
decltype(&::CreateFileA) g_original_create_file_a{};
decltype(&::CreateNamedPipeA) g_original_create_named_pipe_a{};
decltype(&::SetFilePointer) g_original_set_file_pointer{};
decltype(&::WriteFile) g_original_write_file{};
decltype(&::FlushFileBuffers) g_original_flush_file_buffers{};
decltype(&::CloseHandle) g_original_close_handle{};
decltype(&::WinHttpOpen) g_original_win_http_open{};
decltype(&::WinHttpSetTimeouts) g_original_win_http_set_timeouts{};
decltype(&::WinHttpConnect) g_original_win_http_connect{};
decltype(&::WinHttpOpenRequest) g_original_win_http_open_request{};
decltype(&::WinHttpSendRequest) g_original_win_http_send_request{};
decltype(&::WinHttpReceiveResponse) g_original_win_http_receive_response{};
decltype(&::WinHttpQueryDataAvailable)
    g_original_win_http_query_data_available{};
decltype(&::WinHttpReadData) g_original_win_http_read_data{};
decltype(&::WinHttpCloseHandle) g_original_win_http_close_handle{};


HANDLE WINAPI create_file_a_detour(
    LPCSTR file_name,
    DWORD desired_access,
    DWORD share_mode,
    LPSECURITY_ATTRIBUTES security_attributes,
    DWORD creation_disposition,
    DWORD flags_and_attributes,
    HANDLE template_file) {
    const bool tracked = is_vendor_log_path(file_name);
    const auto started_ms = tracked ? MonotonicMilliseconds() : 0;
    const HANDLE result = g_original_create_file_a(
        file_name,
        desired_access,
        share_mode,
        security_attributes,
        creation_disposition,
        flags_and_attributes,
        template_file);
    const DWORD last_error = GetLastError();
    if (tracked) {
        observe_vendor_log_open(
            file_name,
            result,
            started_ms,
            MonotonicMilliseconds(),
            last_error);
    }
    SetLastError(last_error);
    return result;
}
HANDLE WINAPI create_named_pipe_a_detour(
    LPCSTR name,
    DWORD open_mode,
    DWORD pipe_mode,
    DWORD max_instances,
    DWORD output_buffer_size,
    DWORD input_buffer_size,
    DWORD default_timeout,
    LPSECURITY_ATTRIBUTES security_attributes) {
    const bool tracked = IsNesysPipeNameA(name);
    const auto started_ms = tracked ? MonotonicMilliseconds() : 0;
    const HANDLE result = g_original_create_named_pipe_a(
        name,
        open_mode,
        pipe_mode,
        max_instances,
        output_buffer_size,
        input_buffer_size,
        default_timeout,
        security_attributes);
    const DWORD last_error = GetLastError();
    if (tracked) {
        observe_pipe_open(
            PipeEndpoint::Nesys,
            result,
            started_ms,
            MonotonicMilliseconds(),
            last_error);
    }
    SetLastError(last_error);
    return result;
}
DWORD WINAPI set_file_pointer_detour(
    HANDLE file,
    LONG distance,
    PLONG distance_high,
    DWORD move_method) {
    const bool tracked =
        tracked_handle_kind(file) == TrackedHandleKind::VendorLog;
    const auto started_ms = tracked ? MonotonicMilliseconds() : 0;
    const DWORD result = g_original_set_file_pointer(
        file,
        distance,
        distance_high,
        move_method);
    const DWORD last_error = GetLastError();
    if (tracked) {
        observe_vendor_log_seek(
            file,
            result,
            last_error,
            started_ms,
            MonotonicMilliseconds());
    }
    SetLastError(last_error);
    return result;
}
BOOL WINAPI write_file_detour(
    HANDLE file,
    LPCVOID buffer,
    DWORD bytes_to_write,
    LPDWORD bytes_written,
    LPOVERLAPPED overlapped) {
    const auto kind = tracked_handle_kind(file);
    const auto started_ms = kind != TrackedHandleKind::None
        ? MonotonicMilliseconds()
        : 0;
    const BOOL result = g_original_write_file(
        file,
        buffer,
        bytes_to_write,
        bytes_written,
        overlapped);
    const DWORD last_error = GetLastError();
    const auto finished_ms = kind != TrackedHandleKind::None
        ? MonotonicMilliseconds()
        : 0;
    if (kind == TrackedHandleKind::Pipe) {
        observe_pipe_write(
            file,
            buffer,
            bytes_to_write,
            result,
            last_error,
            started_ms,
            finished_ms);
    } else if (kind == TrackedHandleKind::VendorLog) {
        const DWORD transferred =
            bytes_written != nullptr ? *bytes_written : 0;
        observe_vendor_log_write(
            file,
            bytes_to_write,
            transferred,
            result,
            last_error,
            started_ms,
            finished_ms);
    }
    SetLastError(last_error);
    return result;
}
BOOL WINAPI flush_file_buffers_detour(HANDLE file) {
    const bool tracked = IsTrackedNesysPipeHandle(file);
    const auto started_ms = tracked ? MonotonicMilliseconds() : 0;
    const BOOL result = g_original_flush_file_buffers(file);
    const DWORD last_error = GetLastError();
    if (tracked) {
        observe_pipe_flush(
            file,
            result,
            last_error,
            started_ms,
            MonotonicMilliseconds());
    }
    SetLastError(last_error);
    return result;
}
BOOL WINAPI close_handle_detour(HANDLE handle) {
    const auto kind = tracked_handle_kind(handle);
    const auto started_ms = kind == TrackedHandleKind::VendorLog
        ? MonotonicMilliseconds()
        : 0;
    const BOOL result = g_original_close_handle(handle);
    const DWORD last_error = GetLastError();
    if (kind == TrackedHandleKind::VendorLog) {
        observe_vendor_log_close(
            handle,
            result,
            last_error,
            started_ms,
            MonotonicMilliseconds());
    } else if (kind == TrackedHandleKind::Pipe) {
        forget_pipe(handle);
    }
    SetLastError(last_error);
    return result;
}
HINTERNET WINAPI win_http_open_detour(
    LPCWSTR user_agent,
    DWORD access_type,
    LPCWSTR proxy_name,
    LPCWSTR proxy_bypass,
    DWORD flags) {
    const auto started_ms = MonotonicMilliseconds();
    const HINTERNET result = g_original_win_http_open(
        user_agent,
        access_type,
        proxy_name,
        proxy_bypass,
        flags);
    const DWORD last_error = GetLastError();
    observe_http_open(
        result,
        result != nullptr,
        last_error,
        started_ms,
        MonotonicMilliseconds());
    SetLastError(last_error);
    return result;
}
BOOL WINAPI win_http_set_timeouts_detour(
    HINTERNET handle,
    int resolve_timeout,
    int connect_timeout,
    int send_timeout,
    int receive_timeout) {
    const auto started_ms = MonotonicMilliseconds();
    const BOOL result = g_original_win_http_set_timeouts(
        handle,
        resolve_timeout,
        connect_timeout,
        send_timeout,
        receive_timeout);
    const DWORD last_error = GetLastError();
    observe_http_timeouts(
        handle,
        resolve_timeout,
        connect_timeout,
        send_timeout,
        receive_timeout,
        result,
        last_error,
        started_ms,
        MonotonicMilliseconds());
    SetLastError(last_error);
    return result;
}
HINTERNET WINAPI win_http_connect_detour(
    HINTERNET session,
    LPCWSTR server_name,
    INTERNET_PORT server_port,
    DWORD reserved) {
    const auto started_ms = MonotonicMilliseconds();
    const HINTERNET result = g_original_win_http_connect(
        session,
        server_name,
        server_port,
        reserved);
    const DWORD last_error = GetLastError();
    observe_http_connect(
        session,
        result,
        last_error,
        started_ms,
        MonotonicMilliseconds());
    SetLastError(last_error);
    return result;
}
HINTERNET WINAPI win_http_open_request_detour(
    HINTERNET connection,
    LPCWSTR verb,
    LPCWSTR object_name,
    LPCWSTR version,
    LPCWSTR referrer,
    LPCWSTR* accept_types,
    DWORD flags) {
    const auto started_ms = MonotonicMilliseconds();
    const HINTERNET result = g_original_win_http_open_request(
        connection,
        verb,
        object_name,
        version,
        referrer,
        accept_types,
        flags);
    const DWORD last_error = GetLastError();
    observe_http_open_request(
        connection,
        object_name,
        result,
        last_error,
        started_ms,
        MonotonicMilliseconds());
    SetLastError(last_error);
    return result;
}
BOOL WINAPI win_http_send_request_detour(
    HINTERNET request,
    LPCWSTR headers,
    DWORD headers_length,
    LPVOID optional,
    DWORD optional_length,
    DWORD total_length,
    DWORD_PTR context) {
    const auto started_ms = MonotonicMilliseconds();
    const BOOL result = g_original_win_http_send_request(
        request,
        headers,
        headers_length,
        optional,
        optional_length,
        total_length,
        context);
    const DWORD last_error = GetLastError();
    observe_http_send(
        request,
        result,
        last_error,
        started_ms,
        MonotonicMilliseconds());
    SetLastError(last_error);
    return result;
}
BOOL WINAPI win_http_receive_response_detour(
    HINTERNET request,
    LPVOID reserved) {
    const auto started_ms = MonotonicMilliseconds();
    const BOOL result =
        g_original_win_http_receive_response(request, reserved);
    const DWORD last_error = GetLastError();
    observe_http_receive(
        request,
        result,
        last_error,
        started_ms,
        MonotonicMilliseconds());
    SetLastError(last_error);
    return result;
}
BOOL WINAPI win_http_query_data_available_detour(
    HINTERNET request,
    LPDWORD available) {
    const auto started_ms = MonotonicMilliseconds();
    const BOOL result =
        g_original_win_http_query_data_available(request, available);
    const DWORD last_error = GetLastError();
    observe_http_query(
        request,
        result,
        last_error,
        started_ms,
        MonotonicMilliseconds());
    SetLastError(last_error);
    return result;
}
BOOL WINAPI win_http_read_data_detour(
    HINTERNET request,
    LPVOID buffer,
    DWORD bytes_to_read,
    LPDWORD bytes_read) {
    const auto started_ms = MonotonicMilliseconds();
    const BOOL result = g_original_win_http_read_data(
        request,
        buffer,
        bytes_to_read,
        bytes_read);
    const DWORD last_error = GetLastError();
    observe_http_read(
        request,
        result && bytes_read != nullptr ? *bytes_read : 0,
        result,
        last_error,
        started_ms,
        MonotonicMilliseconds());
    SetLastError(last_error);
    return result;
}
BOOL WINAPI win_http_close_handle_detour(HINTERNET handle) {
    const auto started_ms = MonotonicMilliseconds();
    const BOOL result = g_original_win_http_close_handle(handle);
    const DWORD last_error = GetLastError();
    observe_http_close(
        handle,
        result,
        last_error,
        started_ms,
        MonotonicMilliseconds());
    SetLastError(last_error);
    return result;
}
}
std::expected<void, hooking::HookError> AddServiceRequestPipelineHooks(
    hooking::HookPlan& hooks) noexcept {
    if (const auto added = hooks.AddInlineExport(
            {"NesysPipeline", "CreateFileA"}, {L"kernel32.dll", "CreateFileA"},
            &create_file_a_detour, &g_original_create_file_a); !added) return added;
    if (const auto added = hooks.AddInlineExport(
            {"NesysPipeline", "CreateNamedPipeA"}, {L"kernel32.dll", "CreateNamedPipeA"},
            &create_named_pipe_a_detour, &g_original_create_named_pipe_a); !added) return added;
    if (const auto added = hooks.AddInlineExport(
            {"NesysPipeline", "SetFilePointer"}, {L"kernel32.dll", "SetFilePointer"},
            &set_file_pointer_detour, &g_original_set_file_pointer); !added) return added;
    if (const auto added = hooks.AddInlineExport(
            {"NesysPipeline", "WriteFile"}, {L"kernel32.dll", "WriteFile"},
            &write_file_detour, &g_original_write_file); !added) return added;
    if (const auto added = hooks.AddInlineExport(
            {"NesysPipeline", "FlushFileBuffers"}, {L"kernel32.dll", "FlushFileBuffers"},
            &flush_file_buffers_detour, &g_original_flush_file_buffers); !added) return added;
    if (const auto added = hooks.AddInlineExport(
            {"NesysPipeline", "CloseHandle"}, {L"kernel32.dll", "CloseHandle"},
            &close_handle_detour, &g_original_close_handle); !added) return added;
    if (const auto added = hooks.AddInlineExport(
            {"NesysPipeline", "WinHttpOpen"}, {L"winhttp.dll", "WinHttpOpen"},
            &win_http_open_detour, &g_original_win_http_open); !added) return added;
    if (const auto added = hooks.AddInlineExport(
            {"NesysPipeline", "WinHttpSetTimeouts"}, {L"winhttp.dll", "WinHttpSetTimeouts"},
            &win_http_set_timeouts_detour, &g_original_win_http_set_timeouts); !added) return added;
    if (const auto added = hooks.AddInlineExport(
            {"NesysPipeline", "WinHttpConnect"}, {L"winhttp.dll", "WinHttpConnect"},
            &win_http_connect_detour, &g_original_win_http_connect); !added) return added;
    if (const auto added = hooks.AddInlineExport(
            {"NesysPipeline", "WinHttpOpenRequest"}, {L"winhttp.dll", "WinHttpOpenRequest"},
            &win_http_open_request_detour, &g_original_win_http_open_request); !added) return added;
    if (const auto added = hooks.AddInlineExport(
            {"NesysPipeline", "WinHttpSendRequest"}, {L"winhttp.dll", "WinHttpSendRequest"},
            &win_http_send_request_detour, &g_original_win_http_send_request); !added) return added;
    if (const auto added = hooks.AddInlineExport(
            {"NesysPipeline", "WinHttpReceiveResponse"}, {L"winhttp.dll", "WinHttpReceiveResponse"},
            &win_http_receive_response_detour, &g_original_win_http_receive_response); !added) return added;
    if (const auto added = hooks.AddInlineExport(
            {"NesysPipeline", "WinHttpQueryDataAvailable"}, {L"winhttp.dll", "WinHttpQueryDataAvailable"},
            &win_http_query_data_available_detour, &g_original_win_http_query_data_available); !added) return added;
    if (const auto added = hooks.AddInlineExport(
            {"NesysPipeline", "WinHttpReadData"}, {L"winhttp.dll", "WinHttpReadData"},
            &win_http_read_data_detour, &g_original_win_http_read_data); !added) return added;
    if (const auto added = hooks.AddInlineExport(
            {"NesysPipeline", "WinHttpCloseHandle"}, {L"winhttp.dll", "WinHttpCloseHandle"},
            &win_http_close_handle_detour, &g_original_win_http_close_handle); !added) return added;
    return {};
}


}
